// Browser runtime for the guest d3d9.dll -> BoxedWine INT 0x9A bridge.
//
// This file deliberately owns only the command/state and WebGL object layer.
// The fixed-function state model, bit packing, std140 layout queries, and
// shaders remain the already driver-tested modules under this directory.

'use strict';

import {
  COMPARE_OP,
  D3DTA,
  D3DTOP,
  createDefaultState,
  packFFBlendMatrices,
  packFFClipPlanes,
  packFFPixelStages,
  packFFPixelState,
  packFFVertexData,
} from './index.js';

const RESULT_OK = 0x6001;

const COMMAND = Object.freeze({
  INIT: 1,
  CLEAR: 2,
  SET_TRANSFORM: 3,
  SET_VIEWPORT: 4,
  SET_RENDER_STATE: 5,
  SET_TEXTURE_STAGE_STATE: 6,
  SET_SAMPLER_STATE: 7,
  SET_SCISSOR: 8,
  SET_FVF: 9,
  DRAW_PRIMITIVE_UP: 10,
  PRESENT: 11,
  CREATE_TEXTURE: 12,
  CREATE_BUFFER: 13,
  UPLOAD_RESOURCE: 14,
  DESTROY_RESOURCE: 15,
  BIND_TEXTURE: 16,
  BIND_STREAM: 17,
  BIND_INDICES: 18,
  DRAW_INDEXED: 19,
  CREATE_DECLARATION: 20,
  BIND_DECLARATION: 21,
  SET_MATERIAL: 22,
  SET_LIGHT: 23,
  LIGHT_ENABLE: 24,
});

const BLOCK_NAMES = [
  'FFVertexData',
  'FFClipPlanes',
  'FFBlendMatrices',
  'FFPixelState',
  'FFPixelStages',
];

// ES modules do not reliably expose the embedding document's `location` on
// their global object.  Read this module's own URL: the launcher explicitly
// forwards the small, query-gated renderer control set when diagnostics are
// requested, while the normal production import has only its cache version.
const runtimeParams = new URLSearchParams(new URL(import.meta.url).search);
const gpuTimingEnabled = runtimeParams.has('gpuprobe');
const stage1MaterialMipFallbackEnabled = runtimeParams.has('hostmip');
const resourceTraceEnabled = runtimeParams.has('resourceTrace');
const hostProfileEnabled = runtimeParams.has('hostprofile');
// Diagnostic-only: establish whether a frame contains directly adjacent UP
// draws before attempting any draw coalescing.  State/order preservation makes
// this measurement a prerequisite, not an optimization switch.
const drawRunProbeEnabled = runtimeParams.has('drawRunProbe');
const stateChurnProbeEnabled = runtimeParams.has('stateChurnProbe');
// Diagnostic-only frame planner.  It never suppresses, defers/buffers, merges
// or reorders a command; it only snapshots the state visible at each draw and
// computes the theoretical WEBGL_multi_draw call count for blend/depth-state
// candidates within mutation/clear/non-candidate-delimited segments.  This
// filter is deliberately not called an opacity or reorder proof.
const framePlanProbeEnabled = runtimeParams.has('framePlanProbe');
const drawUpBatchEnabled = runtimeParams.has('upBatch');
const drawUpRingEnabled = runtimeParams.has('upRing') && !drawUpBatchEnabled;
const vertexColorShaderSwizzleEnabled = runtimeParams.has('shaderColor');
const bufferDiffHandle = Number.parseInt(runtimeParams.get('bufferDiff') || '0', 10) >>> 0;
const bufferDiffManualArm = runtimeParams.has('bufferDiffManual');
const transientTexturePoolABEnabled = runtimeParams.has('poolAB');
const rgb565DirectABEnabled = runtimeParams.has('rgb565AB');
// Candidate only: probe whether D3D's default WRAP mode is pulling opposite
// edge texels into the A4R4G4B4 dynamic text rows. It is deliberately scoped
// to this format and requires an explicit query flag; normal sampler mapping
// remains authoritative unless an actual quest-panel A/B accepts it.
const a4TextClampEnabled = runtimeParams.has('a4TextClamp');
// Alternate A4 packing is a diagnostic candidate only.  The normal path
// explicitly rotates D3D A,R,G,B nibbles into WebGL R,G,B,A order; this flag
// lets an actual quest-panel A/B falsify that interpretation without changing
// the production converter.
const a4RawOrderEnabled = runtimeParams.has('a4RawOrder');
// The client text DIB->A4 packer stores source G in the D3D R nibble and
// source R in the D3D G nibble. Probe the compensating WebGL order only by
// explicit URL before changing the renderer default.
const a4SwapRGEnabled = runtimeParams.has('a4SwapRG');
const a4TextLinearEnabled = runtimeParams.has('a4TextLinear');
const a4DumpHandle = Number.parseInt(runtimeParams.get('a4Dump') || '', 10);
const a4DumpAllEnabled = runtimeParams.get('a4Dump') === 'all';
// Quest-list title investigation only.  The reported corruption survives a
// recycle-on/off DLL A/B, so identify the actual A4R4G4B4 text draw before
// changing sampling or blend behavior.  This records and never modifies the
// selected draw/state path.
const questTitleTraceEnabled = runtimeParams.has('questTitleTrace');
const requestedRenderScale = Number.parseFloat(runtimeParams.get('renderScale') || '1');
const renderScale = Number.isFinite(requestedRenderScale) && requestedRenderScale >= 0.5 &&
  requestedRenderScale <= 1 ? requestedRenderScale : 1;
// The player path defaults to the low-observation renderer.  The formerly
// default scene inventories and per-draw maps are useful only when explicitly
// requested with `diagnostics=1`; `lean` remains accepted for existing URLs.
// Renderer state, resource ownership and upload/draw caches stay identical.
const runtimeDiagnosticsEnabled = runtimeParams.has('diagnostics') || resourceTraceEnabled ||
  transientTexturePoolABEnabled || rgb565DirectABEnabled;
const leanMarkerEnabled = runtimeParams.has('leanMarker');
let transientTexturePoolEnabled = runtimeParams.has('pool') && !transientTexturePoolABEnabled;
let rgb565DirectEnabled = !runtimeParams.has('copy565') && !rgb565DirectABEnabled;
const rgb565MetricsEnabled = runtimeDiagnosticsEnabled || rgb565DirectABEnabled;
const transientTexturePoolMetricsEnabled = runtimeDiagnosticsEnabled || transientTexturePoolABEnabled;
let rgb565DirectUses = 0;
let rgb565CopyUses = 0;
let fpsPublishStartPresents = 0;
let fpsPublishStartMs = 0;
let firstPresentPublished = false;
// Match the page HUD scope exactly.  Build-wide channels contaminate the
// measurement when player and peer render in separate browser tabs.
const fpsScope = runtimeParams.get('fpsScope') || runtimeParams.get('p') ||
  runtimeParams.get('instance') || 'default';
const fpsBroadcast = typeof globalThis.BroadcastChannel === 'function'
  ? new globalThis.BroadcastChannel(
    `azrok-d3d9web-fps-${encodeURIComponent(runtimeParams.get('build') || 'default')}-${encodeURIComponent(fpsScope)}`)
  : null;

// The renderer normally lives in a pthread worker, while the page HUD lives
// in the window realm. Publish one bounded sample per 120 Presents instead of
// messaging on the frame-hot path. This is also the cadence used by the
// existing performance evidence windows.
function publishFpsSample(debug) {
  if (typeof performance === 'undefined') return;
  const now = performance.now();
  if (!fpsPublishStartMs) {
    fpsPublishStartPresents = debug.presents;
    fpsPublishStartMs = now;
    return;
  }
  const deltaPresents = debug.presents - fpsPublishStartPresents;
  if (deltaPresents < 120) return;
  const sample = {
    cmd: 'd3d9web-fps-sample',
    presents: debug.presents,
    deltaPresents,
    elapsedMs: now - fpsPublishStartMs,
  };
  globalThis.D3D9WebFpsSample = sample;
  if (typeof document === 'undefined' && typeof globalThis.postMessage === 'function') {
    globalThis.postMessage(sample);
  }
  if (fpsBroadcast) fpsBroadcast.postMessage(sample);
  // Module.print is proxied back to the page by the Emscripten pthread shell.
  // Keep this text fallback because some BoxedWine worker layers consume
  // arbitrary postMessage payloads before window.Worker listeners see them.
  report(`D3D9WEB_FPS_SAMPLE presents=${sample.presents} delta=${sample.deltaPresents} elapsed_ms=${sample.elapsedMs.toFixed(3)}`);
  fpsPublishStartPresents = debug.presents;
  fpsPublishStartMs = now;
}

// The page boot surface must leave on the first successfully blitted guest
// frame, not at the 120-Present cadence used only by the FPS HUD.
function publishFirstPresent(debug) {
  if (firstPresentPublished) return;
  firstPresentPublished = true;
  const sample = {
    cmd: 'd3d9web-first-present',
    presents: debug && debug.presents > 0 ? debug.presents : 1,
  };
  globalThis.D3D9WebFirstPresent = sample;
  if (typeof document === 'undefined' && typeof globalThis.postMessage === 'function') {
    globalThis.postMessage(sample);
  }
  if (fpsBroadcast) fpsBroadcast.postMessage(sample);
  report(`D3D9WEB_FIRST_PRESENT presents=${sample.presents}`);
}

function report(message) {
  console.log(message);
  if (globalThis.Module && typeof globalThis.Module.print === 'function') {
    globalThis.Module.print(message);
  }
  // Browser console output is ephemeral and the long-running worker can abort
  // before a human copies it. Keep only bounded 120-Present evidence in the
  // page runtime; never persist hot-path trace lines or touch user storage.
  if (message.startsWith('D3D9WEB_BRIDGE_PROFILE') ||
      message.startsWith('D3D9WEB_INDEXED_CACHE') ||
      message.startsWith('D3D9WEB_RESOURCE_LIFETIME_PROFILE') ||
      message.startsWith('D3D9WEB_DRAW_RUN_PROFILE')) {
    const samples = globalThis.D3D9WebMetricSamples ||
      (globalThis.D3D9WebMetricSamples = []);
    samples.push({ at: new Date().toISOString(), message });
    if (samples.length > 64) samples.splice(0, samples.length - 64);
  }
}

function publishProbe(patch) {
  const probe = globalThis.D3D9WebProbe || (globalThis.D3D9WebProbe = {});
  Object.assign(probe, patch);
}

// Bounded live diagnosis for the original-client gate.  Emit only once per
// 64 commands; per-frame logging would alter the interpreter's behavior.
function recordDispatch(command, byteLength) {
  const debug = globalThis.D3D9WebDebug || (globalThis.D3D9WebDebug = {
    commands: 0,
    presents: 0,
    lastCommand: 0,
    lastBytes: 0,
    hostSamples: 0,
    hostTotalMs: 0,
    hostMaxMs: 0,
    opcodes: new Uint32Array(32),
    hostOpcodeSamples: new Uint32Array(32),
    hostOpcodeTotalUs: new Float64Array(32),
    hostOpcodeMaxUs: new Float64Array(32),
  });
  if (!hostProfileEnabled) {
    if (command === COMMAND.PRESENT) {
      debug.presents += 1;
      publishFpsSample(debug);
    }
    return 0;
  }
  debug.commands += 1;
  debug.lastCommand = command;
  debug.lastBytes = byteLength;
  if (command < debug.opcodes.length) debug.opcodes[command] += 1;
  if (command === COMMAND.PRESENT) {
    debug.presents += 1;
    publishFpsSample(debug);
  }
  if ((debug.commands & 0x3ff) === 0 && typeof document !== 'undefined') {
    document.documentElement.dataset.d3d9webLoop =
      `${debug.commands}:${debug.presents}:${debug.lastCommand}:${debug.lastBytes}`;
    document.documentElement.dataset.d3d9webOps = Array.from(debug.opcodes).join(',');
    const avgMs = debug.hostSamples ? debug.hostTotalMs / debug.hostSamples : 0;
    document.documentElement.dataset.d3d9webHostDispatchMs =
      `${debug.hostSamples}:${avgMs.toFixed(3)}:${debug.hostMaxMs.toFixed(3)}`;
  }
  return (debug.commands & 0x3f) === 0 && typeof performance !== 'undefined'
    ? performance.now() : 0;
}

function recordHostDispatchDuration(startMs, command) {
  if (!startMs || typeof performance === 'undefined') return;
  const elapsedMs = performance.now() - startMs;
  const debug = globalThis.D3D9WebDebug;
  debug.hostSamples += 1;
  debug.hostTotalMs += elapsedMs;
  debug.hostMaxMs = Math.max(debug.hostMaxMs, elapsedMs);
  if (command < debug.hostOpcodeSamples.length) {
    const elapsedUs = elapsedMs * 1000;
    debug.hostOpcodeSamples[command] += 1;
    debug.hostOpcodeTotalUs[command] += elapsedUs;
    debug.hostOpcodeMaxUs[command] = Math.max(debug.hostOpcodeMaxUs[command], elapsedUs);
  }
}

// Per-command console output dominates the pure-interpreter browser build.
// Keep lifecycle/error evidence, but leave frame-hot tracing off for normal
// original-client runs.
const HOT_TRACE = false;
function traceHot(message) {
  if (HOT_TRACE) report(message);
}

function compile(gl, type, source, label) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error(`${label}: ${gl.getShaderInfoLog(shader) || 'compile failed'}`);
  }
  return shader;
}

function bindDummyTextures(gl, program) {
  const dummy2D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, dummy2D);
  gl.texImage2D(
    gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
    new Uint8Array([255, 255, 255, 255])
  );
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  const dummyCube = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_CUBE_MAP, dummyCube);
  for (const face of [
    gl.TEXTURE_CUBE_MAP_POSITIVE_X, gl.TEXTURE_CUBE_MAP_NEGATIVE_X,
    gl.TEXTURE_CUBE_MAP_POSITIVE_Y, gl.TEXTURE_CUBE_MAP_NEGATIVE_Y,
    gl.TEXTURE_CUBE_MAP_POSITIVE_Z, gl.TEXTURE_CUBE_MAP_NEGATIVE_Z,
  ]) {
    gl.texImage2D(
      face, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
      new Uint8Array([255, 255, 255, 255])
    );
  }
  gl.texParameteri(gl.TEXTURE_CUBE_MAP, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_CUBE_MAP, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  for (let stage = 0; stage < 8; stage++) {
    const unit2D = stage * 2;
    const unitCube = unit2D + 1;
    gl.activeTexture(gl.TEXTURE0 + unit2D);
    gl.bindTexture(gl.TEXTURE_2D, dummy2D);
    gl.uniform1i(gl.getUniformLocation(program, `uTexture${stage}`), unit2D);
    gl.activeTexture(gl.TEXTURE0 + unitCube);
    gl.bindTexture(gl.TEXTURE_CUBE_MAP, dummyCube);
    gl.uniform1i(gl.getUniformLocation(program, `uTextureCube${stage}`), unitCube);
  }
  return { dummy2D, dummyCube };
}

function parseD3DTA(value) {
  return {
    selector: value & 0x0f,
    complement: (value & 0x10) !== 0,
    alphaReplicate: (value & 0x20) !== 0,
  };
}

function dwordAsFloat(value) {
  const bytes = new ArrayBuffer(4);
  new DataView(bytes).setUint32(0, value >>> 0, true);
  return new DataView(bytes).getFloat32(0, true);
}

function identityRowMajor() {
  return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
}

function multiplyRowMajor(a, b) {
  const out = new Array(16).fill(0);
  for (let row = 0; row < 4; row++) {
    for (let column = 0; column < 4; column++) {
      for (let k = 0; k < 4; k++) out[row * 4 + column] += a[row * 4 + k] * b[k * 4 + column];
    }
  }
  return out;
}

function d3dToGlslMatrix(matrix) {
  const out = new Array(16);
  for (let row = 0; row < 4; row++) {
    for (let column = 0; column < 4; column++) out[column * 4 + row] = matrix[row * 4 + column];
  }
  return out;
}

function inverseRowMajor(matrix) {
  const augmented = Array.from({ length: 4 }, (_, row) => [
    ...matrix.slice(row * 4, row * 4 + 4),
    ...identityRowMajor().slice(row * 4, row * 4 + 4),
  ]);
  for (let column = 0; column < 4; column++) {
    let pivot = column;
    for (let row = column + 1; row < 4; row++) {
      if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) pivot = row;
    }
    if (Math.abs(augmented[pivot][column]) < 1e-12) return identityRowMajor();
    [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
    const divisor = augmented[column][column];
    for (let i = 0; i < 8; i++) augmented[column][i] /= divisor;
    for (let row = 0; row < 4; row++) {
      if (row === column) continue;
      const factor = augmented[row][column];
      for (let i = 0; i < 8; i++) augmented[row][i] -= factor * augmented[column][i];
    }
  }
  return augmented.flatMap((row) => row.slice(4));
}

function transformRowVector(vector, matrix) {
  return [0, 1, 2, 3].map((column) =>
    vector[0] * matrix[column] + vector[1] * matrix[4 + column] +
    vector[2] * matrix[8 + column] + vector[3] * matrix[12 + column]);
}

function normalizeDirection(vector) {
  const length = Math.hypot(vector[0], vector[1], vector[2]);
  if (length < 1e-12) return [0, 0, 0, 0];
  return [vector[0] / length, vector[1] / length, vector[2] / length, 0];
}

function primitiveVertexCount(type, primitiveCount) {
  switch (type) {
    case 1: return primitiveCount;       // POINTLIST
    case 2: return primitiveCount * 2;   // LINELIST
    case 3: return primitiveCount + 1;   // LINESTRIP
    case 4: return primitiveCount * 3;   // TRIANGLELIST
    case 5: return primitiveCount + 2;   // TRIANGLESTRIP
    case 6: return primitiveCount + 2;   // TRIANGLEFAN
    default: return 0;
  }
}

function primitiveMode(gl, type) {
  return [0, gl.POINTS, gl.LINES, gl.LINE_STRIP, gl.TRIANGLES,
    gl.TRIANGLE_STRIP, gl.TRIANGLE_FAN][type] ?? 0;
}

const fvfLayoutCache = new Map();
function fvfLayout(fvf, stride) {
  const cacheKey = `${fvf >>> 0}:${stride >>> 0}`;
  const cached = fvfLayoutCache.get(cacheKey);
  if (cached) return cached;
  const positionMask = fvf & 0x00e;
  let offset = 0;
  const attributes = [];
  let positionT = false;

  if (positionMask === 0x004) { // D3DFVF_XYZRHW
    attributes.push({ location: 0, size: 4, type: 'float', offset });
    offset += 16;
    positionT = true;
  } else if (positionMask === 0x002) { // D3DFVF_XYZ
    attributes.push({ location: 0, size: 3, type: 'float', offset });
    offset += 12;
  } else {
    throw new Error(`unsupported FVF position mask 0x${positionMask.toString(16)}`);
  }

  if (fvf & 0x010) { // NORMAL
    attributes.push({ location: 1, size: 3, type: 'float', offset });
    offset += 12;
  }
  if (fvf & 0x040) { // DIFFUSE D3DCOLOR
    attributes.push({ location: 12, size: 4, type: 'color', offset });
    offset += 4;
  }
  if (fvf & 0x080) { // SPECULAR D3DCOLOR
    attributes.push({ location: 13, size: 4, type: 'color', offset });
    offset += 4;
  }

  const texCount = (fvf >>> 8) & 0x0f;
  for (let i = 0; i < texCount; i++) {
    const format = (fvf >>> (16 + i * 2)) & 0x03;
    const components = format === 0 ? 2 : format === 1 ? 3 : format === 2 ? 4 : 1;
    attributes.push({ location: 4 + i, size: components, type: 'float', offset });
    offset += components * 4;
  }

  if (offset > stride) {
    throw new Error(`FVF 0x${fvf.toString(16)} needs ${offset} bytes, stride=${stride}`);
  }
  const layout = { attributes, positionT, texCount };
  fvfLayoutCache.set(cacheKey, layout);
  return layout;
}

function declarationTypeInfo(type) {
  switch (type) {
    case 0: return { size: 1, type: 'float', normalized: false, bytes: 4 };
    case 1: return { size: 2, type: 'float', normalized: false, bytes: 8 };
    case 2: return { size: 3, type: 'float', normalized: false, bytes: 12 };
    case 3: return { size: 4, type: 'float', normalized: false, bytes: 16 };
    case 4: return { size: 4, type: 'color', normalized: true, bytes: 4 };
    case 5: return { size: 4, type: 'ubyte', normalized: false, bytes: 4 };
    case 6: return { size: 2, type: 'short', normalized: false, bytes: 4 };
    case 7: return { size: 4, type: 'short', normalized: false, bytes: 8 };
    case 8: return { size: 4, type: 'ubyte', normalized: true, bytes: 4 };
    case 9: return { size: 2, type: 'short', normalized: true, bytes: 4 };
    case 10: return { size: 4, type: 'short', normalized: true, bytes: 8 };
    case 11: return { size: 2, type: 'ushort', normalized: true, bytes: 4 };
    case 12: return { size: 4, type: 'ushort', normalized: true, bytes: 8 };
    case 15: return { size: 2, type: 'half', normalized: false, bytes: 4 };
    case 16: return { size: 4, type: 'half', normalized: false, bytes: 8 };
    default: throw new Error(`unsupported declaration type ${type}`);
  }
}

function declarationLocation(usage, usageIndex) {
  if ((usage === 0 || usage === 9) && usageIndex === 0) return 0;
  if (usage === 1 && usageIndex === 0) return 2;
  if (usage === 2 && usageIndex === 0) return 3;
  if (usage === 3 && usageIndex === 0) return 1;
  if (usage === 4 && usageIndex === 0) return 15;
  if (usage === 5 && usageIndex < 8) return 4 + usageIndex;
  if (usage === 10 && usageIndex < 2) return 12 + usageIndex;
  if (usage === 11 && usageIndex === 0) return 14;
  throw new Error(`unsupported declaration usage ${usage}:${usageIndex}`);
}

function declarationLayout(elements, stride) {
  const attributes = [];
  let positionT = false;
  let texCount = 0;
  for (const element of elements) {
    if (element.stream !== 0 || element.method !== 0) {
      throw new Error(`unsupported declaration stream/method ${element.stream}/${element.method}`);
    }
    const info = declarationTypeInfo(element.type);
    if (element.offset + info.bytes > stride) {
      throw new Error(`declaration element exceeds stride offset=${element.offset} bytes=${info.bytes} stride=${stride}`);
    }
    const location = declarationLocation(element.usage, element.usageIndex);
    attributes.push({ location, offset: element.offset, ...info });
    if (element.usage === 9) positionT = true;
    if (element.usage === 5) texCount = Math.max(texCount, element.usageIndex + 1);
  }
  return { attributes, positionT, texCount };
}

function attributeGlType(gl, type) {
  if (type === 'float') return gl.FLOAT;
  if (type === 'color' || type === 'ubyte') return gl.UNSIGNED_BYTE;
  if (type === 'short') return gl.SHORT;
  if (type === 'ushort') return gl.UNSIGNED_SHORT;
  if (type === 'half') return gl.HALF_FLOAT;
  throw new Error(`unsupported WebGL attribute type ${type}`);
}

function swizzleVertexColors(bytes, stride, layout) {
  if (vertexColorShaderSwizzleEnabled) return bytes;
  const colorOffsets = layout.colorOffsets || (layout.colorOffsets = layout.attributes
    .filter((a) => a.type === 'color')
    .map((a) => a.offset));
  if (!colorOffsets.length) return bytes;
  const out = new Uint8Array(bytes);
  for (let base = 0; base + stride <= out.length; base += stride) {
    for (const offset of colorOffsets) {
      const blue = out[base + offset];
      out[base + offset] = out[base + offset + 2];
      out[base + offset + 2] = blue;
    }
  }
  return out;
}

function textureBytesPerPixel(format) {
  if (format === 21 || format === 22) return 4;
  if (format === 23 || format === 26) return 2;
  return 0;
}

// Decode original D3D base-level pixels for diagnostics, independently of the
// host upload conversion and its experimental flags. Mip bytes are not pixels
// of the base image. Unsupported/missing resources must never break a draw.
function diagnosticTextureRgba(resource) {
  const bpp = resource && textureBytesPerPixel(resource.format);
  if (!bpp || !resource.raw || !Number.isSafeInteger(resource.width) ||
      !Number.isSafeInteger(resource.height) || resource.width <= 0 || resource.height <= 0) return null;
  const count = resource.width * resource.height;
  if (!Number.isSafeInteger(count) || count * bpp > resource.raw.byteLength) return null;
  const rgba = new Uint8ClampedArray(count * 4);
  const bytes = resource.raw;
  const words = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let pixel = 0; pixel < count; pixel++) {
    const offset = pixel * 4;
    if (bpp === 4) {
      rgba[offset] = bytes[offset + 2];
      rgba[offset + 1] = bytes[offset + 1];
      rgba[offset + 2] = bytes[offset];
      rgba[offset + 3] = resource.format === 22 ? 255 : bytes[offset + 3];
    } else {
      const value = words.getUint16(pixel * 2, true);
      if (resource.format === 23) {
        rgba[offset] = Math.round(((value >>> 11) & 31) * 255 / 31);
        rgba[offset + 1] = Math.round(((value >>> 5) & 63) * 255 / 63);
        rgba[offset + 2] = Math.round((value & 31) * 255 / 31);
        rgba[offset + 3] = 255;
      } else {
        rgba[offset] = ((value >>> 8) & 15) * 17;
        rgba[offset + 1] = ((value >>> 4) & 15) * 17;
        rgba[offset + 2] = (value & 15) * 17;
        rgba[offset + 3] = ((value >>> 12) & 15) * 17;
      }
    }
  }
  return rgba;
}

const nativeLittleEndian = new Uint8Array(new Uint16Array([1]).buffer)[0] === 1;

function buildMipLayout(width, height, levels, bytesPerPixel) {
  const mips = [];
  let offset = 0;
  for (let level = 0; level < levels; level++) {
    const levelWidth = Math.max(1, width >>> level);
    const levelHeight = Math.max(1, height >>> level);
    const pitch = levelWidth * bytesPerPixel;
    const byteSize = pitch * levelHeight;
    mips.push({ level, width: levelWidth, height: levelHeight, pitch, offset, byteSize });
    offset += byteSize;
  }
  return { mips, totalBytes: offset };
}

function convertTextureBytes(gl, resource, source = resource.raw) {
  switch (resource.format) {
    case 21: { // D3DFMT_A8R8G8B8: little-endian BGRA bytes.
      const rgba = new Uint8Array(source.length);
      for (let i = 0; i < source.length; i += 4) {
        rgba[i] = source[i + 2];
        rgba[i + 1] = source[i + 1];
        rgba[i + 2] = source[i];
        rgba[i + 3] = source[i + 3];
      }
      return { internalFormat: gl.RGBA8, format: gl.RGBA, type: gl.UNSIGNED_BYTE, pixels: rgba };
    }
    case 22: { // D3DFMT_X8R8G8B8: little-endian BGRX bytes.
      const rgba = new Uint8Array(source.length);
      for (let i = 0; i < source.length; i += 4) {
        rgba[i] = source[i + 2];
        rgba[i + 1] = source[i + 1];
        rgba[i + 2] = source[i];
        rgba[i + 3] = 255;
      }
      return { internalFormat: gl.RGBA8, format: gl.RGBA, type: gl.UNSIGNED_BYTE, pixels: rgba };
    }
    case 23: { // D3DFMT_R5G6B5: bit layout already matches WebGL RGB565.
      if (rgb565DirectEnabled && nativeLittleEndian && (source.byteOffset & 1) === 0) {
        if (rgb565MetricsEnabled) rgb565DirectUses += 1;
        return {
          internalFormat: gl.RGB565, format: gl.RGB,
          type: gl.UNSIGNED_SHORT_5_6_5,
          pixels: new Uint16Array(source.buffer, source.byteOffset, source.byteLength >>> 1),
        };
      }
      if (rgb565MetricsEnabled) rgb565CopyUses += 1;
      const view = new DataView(source.buffer, source.byteOffset, source.byteLength);
      const words = new Uint16Array(source.byteLength >>> 1);
      for (let i = 0; i < words.length; i++) words[i] = view.getUint16(i * 2, true);
      return { internalFormat: gl.RGB565, format: gl.RGB, type: gl.UNSIGNED_SHORT_5_6_5, pixels: words };
    }
    case 26: { // D3DFMT_A4R4G4B4 -> WebGL RGBA4 nibble order.
      const view = new DataView(source.buffer, source.byteOffset, source.byteLength);
      const words = new Uint16Array(source.byteLength >>> 1);
      for (let i = 0; i < words.length; i++) {
        const argb = view.getUint16(i * 2, true);
        words[i] = a4RawOrderEnabled
          ? argb
          : a4SwapRGEnabled
            ? ((argb & 0x00f0) << 8) | (argb & 0x0f00) |
              ((argb & 0x000f) << 4) | ((argb >>> 12) & 0x000f)
            : ((argb & 0x0fff) << 4) | ((argb >>> 12) & 0x000f);
      }
      return { internalFormat: gl.RGBA4, format: gl.RGBA, type: gl.UNSIGNED_SHORT_4_4_4_4, pixels: words };
    }
    default: throw new Error(`unsupported texture format ${resource.format}`);
  }
}

function d3dAddressMode(gl, value) {
  switch (value) {
    case 2: return gl.MIRRORED_REPEAT;
    case 3:
    case 4: return gl.CLAMP_TO_EDGE;
    case 5: return gl.MIRRORED_REPEAT;
    case 1:
    default: return gl.REPEAT;
  }
}

function d3dCompareFunc(gl, value) {
  switch (value) {
    case 1: return gl.NEVER;
    case 2: return gl.LESS;
    case 3: return gl.EQUAL;
    case 4: return gl.LEQUAL;
    case 5: return gl.GREATER;
    case 6: return gl.NOTEQUAL;
    case 7: return gl.GEQUAL;
    case 8: return gl.ALWAYS;
    default: return gl.ALWAYS;
  }
}

function d3dMagFilter(gl, value) {
  return value === 1 ? gl.NEAREST : gl.LINEAR;
}

function d3dMinFilter(gl, minValue, mipValue, hasMipmaps) {
  const nearest = minValue === 1;
  if (!hasMipmaps || mipValue === 0) return nearest ? gl.NEAREST : gl.LINEAR;
  if (mipValue === 1) return nearest ? gl.NEAREST_MIPMAP_NEAREST : gl.LINEAR_MIPMAP_NEAREST;
  return nearest ? gl.NEAREST_MIPMAP_LINEAR : gl.LINEAR_MIPMAP_LINEAR;
}

function createRenderer(canvas, vertexSource, fragmentSource) {
  const sourceCanvas = canvas;
  let renderCanvas = sourceCanvas;
  let canvasMode = 'shared';
  let gl = renderCanvas.getContext('webgl2', {
    alpha: false, depth: true, stencil: true, antialias: false,
    preserveDrawingBuffer: true,
  });
  if (!gl) {
    canvasMode = 'overlay';
    renderCanvas = document.getElementById('d3d9web-canvas');
    if (!renderCanvas) {
      renderCanvas = document.createElement('canvas');
      renderCanvas.id = 'd3d9web-canvas';
      renderCanvas.style.position = 'absolute';
      renderCanvas.style.left = '0';
      renderCanvas.style.top = '0';
      renderCanvas.style.pointerEvents = 'none';
      renderCanvas.style.zIndex = '2';
      sourceCanvas.parentElement.style.position = 'relative';
      sourceCanvas.parentElement.appendChild(renderCanvas);
    }
    renderCanvas.width = sourceCanvas.width || 800;
    renderCanvas.height = sourceCanvas.height || 600;
    renderCanvas.style.width = `${sourceCanvas.clientWidth || renderCanvas.width}px`;
    renderCanvas.style.height = `${sourceCanvas.clientHeight || renderCanvas.height}px`;
    // Keep Wine/SDL startup UI visible until the guest submits real geometry.
    renderCanvas.style.visibility = 'hidden';
    gl = renderCanvas.getContext('webgl2', {
      alpha: false, depth: true, stencil: true, antialias: false,
      preserveDrawingBuffer: true,
    });
  }
  if (!gl) throw new Error('WebGL2 context unavailable');

  const program = gl.createProgram();
  gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, vertexSource, 'd3d9_ffp.vert'));
  gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, fragmentSource, 'd3d9_ffp.frag'));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    throw new Error(gl.getProgramInfoLog(program) || 'program link failed');
  }
  gl.useProgram(program);

  const buffersByBlock = {};
  BLOCK_NAMES.forEach((name, binding) => {
    const index = gl.getUniformBlockIndex(program, name);
    if (index === gl.INVALID_INDEX) throw new Error(`uniform block missing: ${name}`);
    gl.uniformBlockBinding(program, index, binding);
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, binding, buffer);
    buffersByBlock[name] = buffer;
  });
  const dummyTextures = bindDummyTextures(gl, program);

  const vao = gl.createVertexArray();
  const upBuffer = gl.createBuffer();
  // Dynamic buffers keep these two scratch objects.  Static indexed geometry
  // is held in the per-key maps below so unrelated draw calls no longer evict
  // each other's WebGL copies every frame.
  const indexedVertexBuffer = gl.createBuffer();
  const indexedIndexBuffer = gl.createBuffer();
  const backbufferTexture = gl.createTexture();
  const backbufferDepthStencil = gl.createRenderbuffer();
  const backbufferFramebuffer = gl.createFramebuffer();
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, upBuffer);

  const renderer = {
    gl, canvas: renderCanvas, sourceCanvas, canvasMode, program, buffersByBlock, vao, upBuffer, indexedVertexBuffer,
    indexedIndexBuffer, indexedVertexCache: new Map(), indexedIndexCache: new Map(),
    dummyTextures, backbufferTexture, backbufferDepthStencil,
    backbufferFramebuffer, backbufferWidth: 0, backbufferHeight: 0,
    upBufferCapacity: 1024 * 1024, upBufferOffset: 0, upBufferNeedsOrphan: true,
  };
  resizeBackbuffer(renderer, renderCanvas.width, renderCanvas.height);
  return renderer;
}

function resizeBackbuffer(renderer, width, height) {
  if (!renderer || !width || !height ||
      (renderer.backbufferWidth === width && renderer.backbufferHeight === height)) return;
  const { gl } = renderer;
  gl.bindTexture(gl.TEXTURE_2D, renderer.backbufferTexture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0,
    gl.RGBA, gl.UNSIGNED_BYTE, null);
  gl.bindRenderbuffer(gl.RENDERBUFFER, renderer.backbufferDepthStencil);
  gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH24_STENCIL8, width, height);
  gl.bindFramebuffer(gl.FRAMEBUFFER, renderer.backbufferFramebuffer);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
    gl.TEXTURE_2D, renderer.backbufferTexture, 0);
  gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_STENCIL_ATTACHMENT,
    gl.RENDERBUFFER, renderer.backbufferDepthStencil);
  const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
  if (status !== gl.FRAMEBUFFER_COMPLETE) {
    throw new Error(`WebGL backbuffer incomplete 0x${status.toString(16)}`);
  }
  renderer.backbufferWidth = width;
  renderer.backbufferHeight = height;
}

function uploadState(renderer, state, uploadBlendMatrices = true, uploadClipPlanes = true) {
  const { gl, program, buffersByBlock } = renderer;
  const packed = {
    FFVertexData: packFFVertexData(gl, program, state),
    FFPixelState: packFFPixelState(gl, program, state),
    FFPixelStages: packFFPixelStages(gl, program, state),
  };
  if (uploadClipPlanes) packed.FFClipPlanes = packFFClipPlanes(gl, program, state);
  if (uploadBlendMatrices) {
    packed.FFBlendMatrices = packFFBlendMatrices(gl, program, state);
  }
  for (const [name, bytes] of Object.entries(packed)) {
    gl.bindBuffer(gl.UNIFORM_BUFFER, buffersByBlock[name]);
    gl.bufferData(gl.UNIFORM_BUFFER, bytes, gl.DYNAMIC_DRAW);
  }
}

export async function prepareD3D9WebRuntime({
  canvas,
  vertexUrl = '/tools/web_backend/shaders/d3d9_ffp.vert',
  fragmentUrl = '/tools/web_backend/shaders/d3d9_ffp.frag',
} = {}) {
  if (!canvas) throw new Error('prepareD3D9WebRuntime: canvas is required');
  canvas.dataset.d3d9webRuntimeVersion = 'dpupinventory2';
  canvas.dataset.d3d9webQuestTitleTraceEnabled = questTitleTraceEnabled ? '1' : '0';
  let [vertexSource, fragmentSource] = await Promise.all([
    fetch(vertexUrl).then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}: ${vertexUrl}`);
      return r.text();
    }),
    fetch(fragmentUrl).then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}: ${fragmentUrl}`);
      return r.text();
    }),
  ]);
  if (vertexColorShaderSwizzleEnabled) {
    const color0Declaration = 'layout(location = 12) in vec4 in_Color0;';
    const color1Declaration = 'layout(location = 13) in vec4 in_Color1;';
    const colorAccessors = `bool vertexHasColor1() {
    return extractByte(30u) != 0u;
}`;
    if (!vertexSource.includes(color0Declaration) ||
        !vertexSource.includes(color1Declaration) ||
        !vertexSource.includes(colorAccessors)) {
      throw new Error('D3D9 vertex color shader declarations/accessor marker not found');
    }
    vertexSource = vertexSource
      .replace(color0Declaration, 'layout(location = 12) in vec4 in_Color0Raw;')
      .replace(color1Declaration, 'layout(location = 13) in vec4 in_Color1Raw;')
      .replace(/\bin_Color0\b/g, 'vertexColor0()')
      .replace(/\bin_Color1\b/g, 'vertexColor1()')
      .replace(colorAccessors, `${colorAccessors}
vec4 vertexColor0() {
    return extractByte(46u) != 0u ? in_Color0Raw.bgra : in_Color0Raw;
}
vec4 vertexColor1() {
    return extractByte(47u) != 0u ? in_Color1Raw.bgra : in_Color1Raw;
}`);
  }

  let renderer = null;
  let gpuTimerExt = null;
  const gpuTimerPending = [];
  let gpuTimerNanoseconds = 0;
  let gpuTimerSamples = 0;
  let gpuTimerBatches = 0;
  let dispatchBatchProbeCount = 0;
  const dispatchProbeCommands = [];
  const state = createDefaultState();
  let fvf = 0;
  let viewport = { x: 0, y: 0, width: 800, height: 600, minZ: 0, maxZ: 1 };
  let scissor = { left: 0, top: 0, right: 800, bottom: 600 };
  const renderStates = new Map();
  const transformSlots = new Map();
  const lightDefinitions = new Map();
  const enabledLights = new Set();
  let baseTransformDirty = true;
  let viewTransformDirty = true;
  let lightsDirty = true;
  let blendMatricesDirty = true;
  let clipPlanesDirty = true;
  let ffpStateDirty = true;
  let appliedVertexLayoutSignature = '';
  const dirtyBlendSlots = new Set();
  const dirtyTextureStages = new Set();
  let materialCommands = 0;
  let lightCommands = 0;
  let lightEnableCommands = 0;
  let drawCalls = 0;
  let indexedDrawCalls = 0;
  let frameDrawCalls = 0;
  // Acknex creates its software starter window before the real D3D scene.
  // Its device then emits clear-only Presents while the main window is still
  // being prepared. Keep the overlay hidden until a frame with real geometry
  // is presented, otherwise an empty WebGL canvas covers that starter window.
  let frameHasDraw = false;
  let overlayHasPresentedDraw = false;
  const drawUpRingProfile = { calls: 0, bytes: 0, orphans: 0, grows: 0, maxOffset: 0 };
  const drawUpBatchProfile = { batches: 0, calls: 0, bytes: 0, uploads: 0 };
  let activeDrawUpBatch = null;
  let carpetTracePending = false;
  let carpetTraceActive = false;
  let carpetTraceDone = false;
  let carpetTraceLastPixels = null;
  let dpupCapturePending = false;
  let dpupCaptureActive = false;
  let dpupCaptureDone = false;
  let dpupCaptureOrdinal = 0;
  let dpupCaptureDropped = 0;
  const dpupCaptureRecords = [];
  const dpupLiveKeys = new Set();
  const dpupLiveRecords = [];
  const questTitleTraceRecords = [];
  const questTitleCommandCounts = new Uint32Array(24);
  let a4DumpPublished = false;
  const a4DumpRows = new Map();
  let a4DumpAllLastPublishedAt = 0;
  let dpupLiveDrawCount = 0;
  let drawSamplerProfileSampleCounter = 0;
  const indexedLiveKeys = new Set();
  const indexedLiveRecords = [];
  const carpetTraceTargets = [
    { name: 'carpet_expected', x: 100, y: 350 },
    { name: 'carpet_duplicate', x: 100, y: 500 },
    { name: 'carpet_duplicate_edge', x: 250, y: 450 },
    { name: 'floor_control', x: 600, y: 500 },
  ];
  let glDrawValidationCount = 0;
  let vertexInputSignature = '';
  let enabledVertexAttributeMask = 0;
  let indexedVertexUploadKey = '';
  let indexedIndexUploadKey = '';
  let indexedVertexCacheHits = 0;
  let indexedVertexCacheMisses = 0;
  let indexedIndexCacheHits = 0;
  let indexedIndexCacheMisses = 0;
  let indexedCachePresents = 0;
  let leanMarkerPresents = 0;
  let rgb565ABPhasePresents = 0;
  let rgb565ABFirstPhase = true;
  let hostBatchProfileCalls = 0;
  let hostBatchProfileMicros = 0;
  let hostBatchProfileMaxMicros = 0;
  const drawRunProfile = { presents: 0, draws: 0, adjacentPairs: 0, runs: 0, maxRun: 0 };
  // Query-gated only: count transport-level repeats before considering any
  // change to state application.  A same-value command is not automatically
  // removable until a visible A/B proves that its host-side effects are
  // redundant for the original client.
  const stateChurnProfile = {
    presents: 0,
    render: { commands: 0, same: 0 },
    textureStage: { commands: 0, same: 0 },
    sampler: { commands: 0, same: 0 },
    textureBind: { commands: 0, same: 0 },
    streamBind: { commands: 0, same: 0 },
  };
  const stateChurnTextureStageValues = Array.from({ length: 8 }, () => new Map());
  const stateChurnSamplerValues = Array.from({ length: 8 }, () => new Map());
  const framePlanProfile = {
    presents: 0,
    draws: 0,
    upDraws: 0,
    indexedDraws: 0,
    candidateDraws: 0,
    candidateGroups: 0,
    theoreticalCalls: 0,
    maxGroup: 0,
    reasons: new Map(),
  };
  let framePlanGroups = new Map();
  let framePlanDraws = 0;
  let framePlanNonCandidateCalls = 0;
  let framePlanSegment = 0;
  const framePlanNumberBits = new DataView(new ArrayBuffer(8));

  function framePlanBarrier() {
    if (framePlanProbeEnabled) framePlanSegment += 1;
  }

  function framePlanCandidateReason() {
    if ((renderStates.get(27) ?? 0) !== 0) return 'blend';
    if ((renderStates.get(7) ?? 1) === 0) return 'depth-off';
    if ((renderStates.get(14) ?? 1) === 0) return 'depth-write-off';
    if ((renderStates.get(52) ?? 0) !== 0) return 'stencil';
    if ((renderStates.get(168) ?? 0x0f) !== 0x0f) return 'color-mask';
    return '';
  }

  function framePlanMapEntries(map) {
    return [...map.entries()].sort((left, right) => Number(left[0]) - Number(right[0]));
  }

  function framePlanCanonical(value) {
    if (typeof value === 'number') {
      framePlanNumberBits.setFloat64(0, value, true);
      return `n${framePlanNumberBits.getUint32(4, true).toString(16).padStart(8, '0')}` +
        `${framePlanNumberBits.getUint32(0, true).toString(16).padStart(8, '0')}`;
    }
    if (typeof value === 'string') return `s${JSON.stringify(value)}`;
    if (typeof value === 'boolean') return value ? 't' : 'f';
    if (value === null) return 'z';
    if (value === undefined) return 'u';
    if (Array.isArray(value) || ArrayBuffer.isView(value)) {
      return `[${Array.from(value, framePlanCanonical).join(',')}]`;
    }
    if (typeof value === 'object') {
      return `{${Object.keys(value).sort().map((key) =>
        `${JSON.stringify(key)}:${framePlanCanonical(value[key])}`).join(',')}}`;
    }
    return `${typeof value}:${String(value)}`;
  }

  function framePlanTextureIdentity(handle) {
    const resource = resources.get(handle);
    return [handle >>> 0, resource?.kind ?? 0, resource?.uploadVersion ?? 0,
      resource?.raw?.byteLength ?? 0, resource?.materialMipLevels ?? 0,
      resource?.materialMipUploadVersion ?? 0];
  }

  function framePlanStateKey(primitiveType, packetFvf, stride) {
    // Canonical strings are kept only for the current frame (at most one per
    // draw). Numeric values are serialized by exact f64 bits, preserving -0
    // and non-finite values that JSON would collapse to 0/null.
    return framePlanCanonical({
      segment: framePlanSegment,
      primitiveType: primitiveType >>> 0,
      fvf: packetFvf >>> 0,
      stride: stride >>> 0,
      viewport,
      scissor,
      render: framePlanMapEntries(renderStates),
      textures: boundTextures.map(framePlanTextureIdentity),
      samplers: samplerStates,
      state,
    });
  }

  function recordFramePlanUpDraw(primitiveType, packetFvf, stride) {
    if (!framePlanProbeEnabled) return;
    framePlanDraws += 1;
    framePlanProfile.upDraws += 1;
    const reason = framePlanCandidateReason();
    if (reason) {
      framePlanNonCandidateCalls += 1;
      framePlanProfile.reasons.set(reason, (framePlanProfile.reasons.get(reason) || 0) + 1);
      framePlanBarrier();
      return;
    }
    framePlanProfile.candidateDraws += 1;
    const key = framePlanStateKey(primitiveType, packetFvf, stride);
    const count = (framePlanGroups.get(key) || 0) + 1;
    framePlanGroups.set(key, count);
    framePlanProfile.maxGroup = Math.max(framePlanProfile.maxGroup, count);
  }

  function recordFramePlanIndexedDraw() {
    if (!framePlanProbeEnabled) return;
    framePlanDraws += 1;
    framePlanNonCandidateCalls += 1;
    framePlanProfile.indexedDraws += 1;
    framePlanProfile.reasons.set('indexed', (framePlanProfile.reasons.get('indexed') || 0) + 1);
    framePlanBarrier();
  }

  function recordFramePlanPresent() {
    if (!framePlanProbeEnabled) return;
    const groupedCalls = framePlanGroups.size;
    framePlanProfile.presents += 1;
    framePlanProfile.draws += framePlanDraws;
    framePlanProfile.candidateGroups += groupedCalls;
    framePlanProfile.theoreticalCalls += framePlanNonCandidateCalls + groupedCalls;
    framePlanGroups = new Map();
    framePlanDraws = 0;
    framePlanNonCandidateCalls = 0;
    framePlanSegment = 0;
    if (framePlanProfile.presents !== 120) return;
    const extension = renderer?.gl?.getExtension('WEBGL_multi_draw');
    const snapshot = {
      presents: framePlanProfile.presents,
      draws: framePlanProfile.draws,
      upDraws: framePlanProfile.upDraws,
      indexedDraws: framePlanProfile.indexedDraws,
      candidateDraws: framePlanProfile.candidateDraws,
      candidateGroups: framePlanProfile.candidateGroups,
      theoreticalCalls: framePlanProfile.theoreticalCalls,
      theoreticalReduction: framePlanProfile.draws - framePlanProfile.theoreticalCalls,
      maxGroup: framePlanProfile.maxGroup,
      multiDrawSupported: Boolean(extension?.multiDrawArraysWEBGL),
      reasons: Object.fromEntries([...framePlanProfile.reasons.entries()].sort()),
      candidateFilter: 'blend-off-depth-on-write-stencil-off-full-color-mask',
      semantics: 'state-candidate-density-only-not-opacity-or-reorder-proof',
    };
    report(`D3D9WEB_FRAME_PLAN presents=${snapshot.presents} draws=${snapshot.draws} up=${snapshot.upDraws} indexed=${snapshot.indexedDraws} candidates=${snapshot.candidateDraws} groups=${snapshot.candidateGroups} theoretical_calls=${snapshot.theoreticalCalls} reduction=${snapshot.theoreticalReduction} max_group=${snapshot.maxGroup} multi_draw=${snapshot.multiDrawSupported ? 1 : 0} reasons=${JSON.stringify(snapshot.reasons)}`);
    publishProbe({ framePlan: snapshot });
    framePlanProfile.presents = 0;
    framePlanProfile.draws = 0;
    framePlanProfile.upDraws = 0;
    framePlanProfile.indexedDraws = 0;
    framePlanProfile.candidateDraws = 0;
    framePlanProfile.candidateGroups = 0;
    framePlanProfile.theoreticalCalls = 0;
    framePlanProfile.maxGroup = 0;
    framePlanProfile.reasons.clear();
  }

  function recordStateChurnPresent() {
    if (!stateChurnProbeEnabled) return;
    stateChurnProfile.presents += 1;
    if (stateChurnProfile.presents !== 120) return;
    const snapshot = {
      presents: 120,
      render: { ...stateChurnProfile.render },
      textureStage: { ...stateChurnProfile.textureStage },
      sampler: { ...stateChurnProfile.sampler },
      textureBind: { ...stateChurnProfile.textureBind },
      streamBind: { ...stateChurnProfile.streamBind },
    };
    report(`D3D9WEB_STATE_CHURN presents=${snapshot.presents} render=${snapshot.render.same}/${snapshot.render.commands} texture_stage=${snapshot.textureStage.same}/${snapshot.textureStage.commands} sampler=${snapshot.sampler.same}/${snapshot.sampler.commands} texture_bind=${snapshot.textureBind.same}/${snapshot.textureBind.commands} stream_bind=${snapshot.streamBind.same}/${snapshot.streamBind.commands}`);
    publishProbe({ stateChurn: snapshot });
    stateChurnProfile.presents = 0;
    for (const bucket of [stateChurnProfile.render, stateChurnProfile.textureStage, stateChurnProfile.sampler, stateChurnProfile.textureBind, stateChurnProfile.streamBind]) {
      bucket.commands = 0;
      bucket.same = 0;
    }
  }

  function probeDrawRuns(bytes) {
    if (!drawRunProbeEnabled) return;
    let offset = 0;
    let runLength = 0;
    while (offset + 8 <= bytes.byteLength) {
      const commandBytes = new DataView(bytes.buffer, bytes.byteOffset + offset, 4).getUint32(0, true);
      if (commandBytes < 4 || offset + 4 + commandBytes > bytes.byteLength) return;
      const command = new DataView(bytes.buffer, bytes.byteOffset + offset + 4, 4).getUint32(0, true);
      if (command === COMMAND.DRAW_PRIMITIVE_UP) {
        drawRunProfile.draws += 1;
        runLength += 1;
      } else {
        if (runLength) {
          drawRunProfile.runs += 1;
          drawRunProfile.adjacentPairs += Math.max(0, runLength - 1);
          drawRunProfile.maxRun = Math.max(drawRunProfile.maxRun, runLength);
          runLength = 0;
        }
        if (command === COMMAND.PRESENT) {
          drawRunProfile.presents += 1;
          if (drawRunProfile.presents === 120) {
            const sample = {
              presents: 120,
              draws: drawRunProfile.draws,
              runs: drawRunProfile.runs,
              adjacentPairs: drawRunProfile.adjacentPairs,
              maxRun: drawRunProfile.maxRun,
            };
            report(`D3D9WEB_DRAW_RUN_PROFILE presents=${sample.presents} draws=${sample.draws} runs=${sample.runs} adjacent_pairs=${sample.adjacentPairs} max_run=${sample.maxRun}`);
            publishProbe({ drawRun: sample });
            drawRunProfile.presents = 0;
            drawRunProfile.draws = 0;
            drawRunProfile.adjacentPairs = 0;
            drawRunProfile.runs = 0;
            drawRunProfile.maxRun = 0;
          }
        }
      }
      offset += 4 + commandBytes;
    }
    if (runLength) {
      drawRunProfile.runs += 1;
      drawRunProfile.adjacentPairs += Math.max(0, runLength - 1);
      drawRunProfile.maxRun = Math.max(drawRunProfile.maxRun, runLength);
    }
  }
  let guestProfileBaseline = null;
  let guestProfileLatest = null;
  // Measurement-only, bounded to one report each 120 Presents.  This keeps
  // per-resource upload attribution visible without tracing every command.
  const uploadProfile = new Map();
  const bufferDiffProfile = {
    generation: 0, length: 0,
    uploads: 0, bytes: 0, changed: 0, sameUploads: 0,
    minChanged: Number.POSITIVE_INFINITY, maxChanged: 0,
    minFirst: Number.POSITIVE_INFINITY, maxLast: -1, changedBlocks: 0,
  };
  let bufferDiffDone = false;
  let bufferResourceGeneration = 0;
  // Keep the range evidence separate from the aggregate upload byte count.
  // A non-zero buffer start is significant: the guest-side UploadResource
  // contract requires those bytes to be copied from the resource base, not
  // from the already offset lock pointer.
  const partialBufferUploadProfile = new Map();
  const drawSamplerProfile = new Map();
  // Measurement only: distinguish genuinely dynamic indexed resources from
  // static resources that merely alternate through the shared scratch GL
  // buffer.  Cache policy must be based on this usage/version evidence, not
  // on a visually static screenshot.
  const indexedDrawResourceProfile = new Map();
  const alphaStateProfile = {
    enables: 0, disables: 0, functions: 0, reenableAfterDisable: 0,
    lastEnabled: null,
  };
  const resourceLifetimeProfile = {
    creates: 0, destroys: 0, deleted: 0, missing: 0, boundBlocked: 0,
  };
  let resourceEvents = 0;
  let transientTraceReference = null;
  let transientTraceSamples = 0;
  let transientTraceAllSame = true;
  let transientSteadyTraceArmed = false;
  const resources = new Map();
  const transientTexturePool = [];
  const transientTexturePoolProfile = { hits: 0, misses: 0, returns: 0, evicts: 0 };
  let textureContextGeneration = 1;
  const textureContextListenerCanvases = new WeakSet();
  const boundTextures = Array(8).fill(0);
  const samplerStates = Array.from({ length: 8 }, () => ({
    addressU: 1, addressV: 1, mag: 2, min: 2, mip: 0,
  }));
  // Preserve the complete D3DTSS_TEXCOORDINDEX value for the one-frame
  // carpet probe.  The renderer currently consumes only the low coordinate
  // index, so this also makes any missing TCI generation flag directly
  // observable before changing shader behavior.
  const rawTexcoordIndex = Array.from({ length: 8 }, (_, stage) => stage);
  const rawTextureStageStates = Array.from({ length: 2 }, () => Array(33).fill(0));
  const rawSamplerStates = Array.from({ length: 2 }, () => Array(14).fill(0));
  const samplerDirty = Array(8).fill(true);
  const gpuTextureHandles = Array(8).fill(-1);
  let samplerObjects = null;
  let stream0 = { handle: 0, offset: 0, stride: 0 };
  let indexHandle = 0;
  let declarationHandle = 0;
  let overlayCssWidth = -1;
  let overlayCssHeight = -1;
  let appliedViewportX = NaN;
  let appliedViewportY = NaN;
  let appliedViewportWidth = NaN;
  let appliedViewportHeight = NaN;
  let appliedViewportMinZ = NaN;
  let appliedViewportMaxZ = NaN;
  let appliedViewportCanvasHeight = NaN;

  function attachTextureContextListeners(targetCanvas) {
    if (!targetCanvas || typeof targetCanvas.addEventListener !== 'function' ||
        textureContextListenerCanvases.has(targetCanvas)) return;
    textureContextListenerCanvases.add(targetCanvas);
    const invalidateTexturePool = () => {
      transientTexturePool.length = 0;
      textureContextGeneration += 1;
      gpuTextureHandles.fill(-1);
    };
    targetCanvas.addEventListener('webglcontextlost', invalidateTexturePool);
    targetCanvas.addEventListener('webglcontextrestored', invalidateTexturePool);
  }

  // Resource loading can produce thousands of Unlock uploads before the
  // first frame.  Sampling preserves progress evidence without making the
  // pure-interpreter build spend most of its time formatting log lines.
  function sampleResourceEvent() {
    if (!runtimeDiagnosticsEnabled) return false;
    resourceEvents += 1;
    // Keep enough startup evidence to diagnose a dead loader. Long-running
    // per-resource console traffic is opt-in because console.log + Module.print
    // runs outside the host batch timer and perturbs steady-frame results.
    return resourceEvents <= 24 ||
      (resourceTraceEnabled && (resourceEvents & 0x7f) === 0);
  }

  function prepareDrawUpBatch(bytes) {
    let packetOffset = 0;
    let arenaBytes = 0;
    const entries = [];
    while (packetOffset < bytes.byteLength) {
      if (bytes.byteLength - packetOffset < 4) throw new Error('short DrawUP batch length');
      const commandBytes = new DataView(bytes.buffer, bytes.byteOffset + packetOffset, 4)
        .getUint32(0, true);
      packetOffset += 4;
      if (commandBytes < 4 || commandBytes > bytes.byteLength - packetOffset) {
        throw new Error(`invalid DrawUP batch command size ${commandBytes}`);
      }
      const commandView = new DataView(bytes.buffer, bytes.byteOffset + packetOffset, commandBytes);
      if (commandView.getUint32(0, true) === COMMAND.DRAW_PRIMITIVE_UP) {
        if (commandBytes < 24) throw new Error('short DrawPrimitiveUP in batch prepass');
        const stride = commandView.getUint32(12, true);
        const vertexBytes = commandView.getUint32(20, true);
        if (!stride || commandBytes !== 24 + vertexBytes) {
          throw new Error(`bad DrawPrimitiveUP prepass stride=${stride} bytes=${vertexBytes}`);
        }
        arenaBytes = Math.ceil(arenaBytes / stride) * stride;
        entries.push({ commandOffset: packetOffset, commandBytes, arenaOffset: arenaBytes, stride, vertexBytes });
        arenaBytes += vertexBytes;
      }
      packetOffset += commandBytes;
    }
    if (!entries.length) return null;
    const arena = new Uint8Array(arenaBytes);
    for (const entry of entries) {
      const commandBytes = bytes.subarray(entry.commandOffset, entry.commandOffset + entry.commandBytes);
      const commandView = new DataView(commandBytes.buffer, commandBytes.byteOffset, commandBytes.byteLength);
      const packetFvf = commandView.getUint32(16, true);
      const layout = fvfLayout(packetFvf, entry.stride);
      arena.set(swizzleVertexColors(commandBytes.subarray(24), entry.stride, layout), entry.arenaOffset);
    }
    drawUpBatchProfile.batches += 1;
    drawUpBatchProfile.calls += entries.length;
    drawUpBatchProfile.bytes += arenaBytes;
    return { arena, entries, index: 0, uploaded: false };
  }

  function ensureRenderer(width = 800, height = 600) {
    // Assigning canvas.width/height resets the WebGL drawing buffer even when
    // the value is unchanged. More importantly, resizing the overlay from a
    // synchronous BoxedWine INT 0x9A callback can block the guest call. Resize
    // only when D3D actually changes the backbuffer dimensions.
    if (width && canvas.width !== width) canvas.width = width;
    if (height && canvas.height !== height) canvas.height = height;
    if (!renderer) {
      renderer = createRenderer(canvas, vertexSource, fragmentSource);
      canvas = renderer.canvas;
      attachTextureContextListeners(canvas);
      if (width && canvas.width !== width) canvas.width = width;
      if (height && canvas.height !== height) canvas.height = height;
      samplerObjects = Array.from({ length: 8 }, () => renderer.gl.createSampler());
      if (gpuTimingEnabled) gpuTimerExt = renderer.gl.getExtension('EXT_disjoint_timer_query_webgl2');
      report(`D3D9WEB_PHASE6_FFP_INIT webgl2=1 shader=driver-linked blocks=${BLOCK_NAMES.length} canvas=${renderer.canvasMode}`);
    }
    // SDL keeps a 640x480 backing store for browser input, whereas D3D renders
    // to the separate 1024x768 overlay.  Do not copy one backing size to the
    // other, but do give both elements the D3D CSS size: otherwise the overlay
    // inherits the source canvas's 640x480 CSS box and visibly downsizes a
    // correctly rendered 1024x768 frame.  DOM coordinates still pass through
    // the source canvas's 640x480 backing store before Wine expands them.
    if (renderer.canvasMode === 'overlay') {
      const source = renderer.sourceCanvas;
      const output = renderer.canvas;
      const parent = source.parentElement;
      if (overlayCssWidth !== output.width || overlayCssHeight !== output.height) {
        overlayCssWidth = output.width;
        overlayCssHeight = output.height;
        const cssWidth = `${output.width}px`;
        const cssHeight = `${output.height}px`;
        output.style.width = cssWidth;
        output.style.height = cssHeight;
        output.style.maxWidth = 'none';
        source.style.width = cssWidth;
        source.style.height = cssHeight;
        source.style.maxWidth = 'none';
        if (parent) {
          parent.style.width = cssWidth;
          parent.style.height = cssHeight;
        }
      }
    }
    // Keep the guest-visible output canvas at its original D3D dimensions.
    // An explicit renderScale reduces only the offscreen WebGL target, then
    // Present upscales it to that unchanged canvas.  This keeps original UI
    // coordinates and browser input mapping stable for narrow-device runs.
    resizeBackbuffer(renderer,
      Math.max(1, Math.round(canvas.width * renderScale)),
      Math.max(1, Math.round(canvas.height * renderScale)));
    renderer.gl.bindFramebuffer(renderer.gl.FRAMEBUFFER, renderer.backbufferFramebuffer);
    return renderer;
  }

  function presentBackbuffer() {
    const current = ensureRenderer(viewport.width, viewport.height);
    const { gl } = current;
    const scissorEnabled = gl.isEnabled(gl.SCISSOR_TEST);
    if (scissorEnabled) gl.disable(gl.SCISSOR_TEST);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, current.backbufferFramebuffer);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    gl.blitFramebuffer(0, 0, current.backbufferWidth, current.backbufferHeight,
      0, 0, canvas.width, canvas.height, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    if (scissorEnabled) gl.enable(gl.SCISSOR_TEST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, current.backbufferFramebuffer);
    const presentedDraw = frameHasDraw;
    if (presentedDraw) overlayHasPresentedDraw = true;
    if (current.canvasMode === 'overlay' && overlayHasPresentedDraw) {
      current.canvas.style.visibility = 'visible';
    }
    if (drawUpRingEnabled) {
      current.upBufferOffset = 0;
      current.upBufferNeedsOrphan = true;
    }
    if (runtimeDiagnosticsEnabled && carpetTraceActive) {
      report(`D3D9WEB_CARPET_TRACE_END frame_draws=${frameDrawCalls}`);
      carpetTraceActive = false;
      carpetTraceDone = true;
    } else if (runtimeDiagnosticsEnabled && !carpetTraceDone && frameDrawCalls > 80 && resources.size > 100) {
      carpetTracePending = true;
    }
    if (runtimeDiagnosticsEnabled && dpupCaptureActive) {
      globalThis.D3D9WebDpupCapture = {
        records: dpupCaptureRecords.slice(),
        dropped: dpupCaptureDropped,
        frameDraws: frameDrawCalls,
      };
      report(`D3D9WEB_DPUP_HASHES ${dpupCaptureRecords.map((r) =>
        `${r.ordinal}:${r.primitiveCount}:${r.fnv1a.toString(16).padStart(8, '0')}`).join(',')}`);
      const compactState = (r) => {
        const tssTypes = [1, 2, 3, 4, 5, 6, 11, 24, 26, 27, 28, 32];
        const samplerTypes = [1, 2, 5, 6, 7];
        const tss = r.tss.map((values) => tssTypes.map((type) => values[type]).join('.')).join('/');
        const sampler = r.sampler.map((values) => samplerTypes.map((type) => values[type]).join('.')).join('/');
        const words = (values) => values.map((word) => word.toString(16).padStart(8, '0')).join('.');
        return `${r.ordinal}:${r.textures.join('.')}:w${words(r.world)}:v${words(r.view)}:p${words(r.projection)}` +
          `:vp${r.viewport.x}.${r.viewport.y}.${r.viewport.width}.${r.viewport.height}.${r.viewport.minZWord.toString(16)}.${r.viewport.maxZWord.toString(16)}` +
          `:t${tss}:s${sampler}:r${r.render.join('.')}:b${r.bindingOk.length ? r.bindingOk.join('.') : '-'}`;
      };
      report(`D3D9WEB_DPUP_STATES ${dpupCaptureRecords.map(compactState).join(',')}`);
      const carpetTextureHandles = new Set(dpupCaptureRecords
        .filter((r) => r.ordinal >= 22 && r.ordinal <= 29)
        .flatMap((r) => r.textures).filter(Boolean));
      const textureHash = (raw) => {
        let hash = 0x811c9dc5;
        for (let i = 0; i < raw.length; i++) {
          hash ^= raw[i];
          hash = Math.imul(hash, 0x01000193) >>> 0;
        }
        return hash >>> 0;
      };
      report(`D3D9WEB_DPUP_TEXTURES ${Array.from(carpetTextureHandles).map((handle) => {
        const resource = resources.get(handle);
        if (!resource || resource.kind !== 1) return `${handle}:missing`;
        const rows = resource.mips.map((mip) => {
          const first = resource.raw.subarray(mip.offset, mip.offset + mip.pitch);
          const lastOffset = mip.offset + (mip.height - 1) * mip.pitch;
          const last = resource.raw.subarray(lastOffset, lastOffset + mip.pitch);
          return `${mip.level}:${textureHash(first).toString(16).padStart(8, '0')}/${textureHash(last).toString(16).padStart(8, '0')}`;
        }).join('.');
        return `${handle}:${resource.width}x${resource.height}:fmt${resource.format}:levels${resource.levels}:bytes${resource.raw.length}:fnv${textureHash(resource.raw).toString(16).padStart(8, '0')}:rows${rows}`;
      }).join(',')}`);
      report(`D3D9WEB_DPUP_CAPTURE_END records=${dpupCaptureRecords.length} dropped=${dpupCaptureDropped} frame_draws=${frameDrawCalls}`);
      dpupCaptureActive = false;
      dpupCaptureDone = true;
    } else if (runtimeDiagnosticsEnabled && !dpupCaptureDone && frameDrawCalls > 80 && resources.size > 100) {
      dpupCapturePending = true;
    }
    frameDrawCalls = 0;
    frameHasDraw = false;
    if (runtimeDiagnosticsEnabled && ++indexedCachePresents === 120) {
      report(`D3D9WEB_INDEXED_CACHE presents=120 vb_hit=${indexedVertexCacheHits} vb_miss=${indexedVertexCacheMisses} ib_hit=${indexedIndexCacheHits} ib_miss=${indexedIndexCacheMisses}`);
      const uploads = Array.from(uploadProfile.entries())
        .sort(([, left], [, right]) => right.bytes - left.bytes || right.finals - left.finals)
        .slice(0, 6)
        .map(([key, sample]) => `${key}:final=${sample.finals}:chunk=${sample.chunks}:bytes=${sample.bytes}`)
        .join(',');
      report(`D3D9WEB_UPLOAD_PROFILE presents=120 top=${uploads || 'none'}`);
      report(`D3D9WEB_RESOURCE_LIFETIME_PROFILE presents=120 create=${resourceLifetimeProfile.creates} destroy=${resourceLifetimeProfile.destroys} deleted=${resourceLifetimeProfile.deleted} missing=${resourceLifetimeProfile.missing} bound_blocked=${resourceLifetimeProfile.boundBlocked} live=${resources.size}`);
      report(`D3D9WEB_TEXTURE_POOL presents=120 enabled=${transientTexturePoolEnabled ? 1 : 0} hit=${transientTexturePoolProfile.hits} miss=${transientTexturePoolProfile.misses} return=${transientTexturePoolProfile.returns} evict=${transientTexturePoolProfile.evicts} pooled=${transientTexturePool.length}`);
      if (!rgb565DirectABEnabled) {
        report(`D3D9WEB_RGB565_DIRECT presents=120 enabled=${rgb565DirectEnabled ? 1 : 0} direct=${rgb565DirectUses} copy=${rgb565CopyUses}`);
        rgb565DirectUses = 0;
        rgb565CopyUses = 0;
      }
      if (resourceTraceEnabled && !transientSteadyTraceArmed &&
          resourceLifetimeProfile.creates === 120 && resourceLifetimeProfile.destroys === 120) {
        transientSteadyTraceArmed = true;
        transientTraceReference = null;
        transientTraceSamples = 0;
        transientTraceAllSame = true;
        delete canvas.dataset.d3d9webTransientExact;
      }
      const partialBuffers = Array.from(partialBufferUploadProfile.entries())
        .sort(([, left], [, right]) => right - left)
        .slice(0, 8)
        .map(([key, count]) => `${key}:final=${count}`)
        .join(',');
      report(`D3D9WEB_PARTIAL_BUFFER_PROFILE presents=120 top=${partialBuffers || 'none'}`);
      const samplerDraws = Array.from(drawSamplerProfile.entries())
        .sort(([, left], [, right]) => right - left)
        .slice(0, 10)
        .map(([key, draws]) => `${key}:draw=${draws}`)
        .join(',');
      report(`D3D9WEB_SAMPLER_DRAW_PROFILE presents=120 top=${samplerDraws || 'none'}`);
      const indexedResources = Array.from(indexedDrawResourceProfile.entries())
        .sort(([, left], [, right]) => right - left)
        .slice(0, 12)
        .map(([key, draws]) => `${key}:draw=${draws}`)
        .join(',');
      report(`D3D9WEB_INDEXED_RESOURCE_PROFILE presents=120 top=${indexedResources || 'none'}`);
      report(`D3D9WEB_ALPHA_STATE_PROFILE presents=120 enable=${alphaStateProfile.enables} disable=${alphaStateProfile.disables} func=${alphaStateProfile.functions} reenable=${alphaStateProfile.reenableAfterDisable}`);
      report(`D3D9WEB_UP_RING presents=120 enabled=${drawUpRingEnabled ? 1 : 0} calls=${drawUpRingProfile.calls} bytes=${drawUpRingProfile.bytes} orphans=${drawUpRingProfile.orphans} grows=${drawUpRingProfile.grows} max_offset=${drawUpRingProfile.maxOffset}`);
      report(`D3D9WEB_UP_BATCH presents=120 enabled=${drawUpBatchEnabled ? 1 : 0} batches=${drawUpBatchProfile.batches} calls=${drawUpBatchProfile.calls} bytes=${drawUpBatchProfile.bytes} uploads=${drawUpBatchProfile.uploads}`);
      if (guestProfileLatest && guestProfileBaseline) {
        const current = guestProfileLatest;
        const previous = guestProfileBaseline;
        const delta32 = (value, baseline) => (value - baseline) >>> 0;
        const adjustedMicros = (ticks, calls) => {
          const adjusted = ticks > BigInt(calls) * current.timerOverhead ?
            ticks - BigInt(calls) * current.timerOverhead : 0n;
          return current.frequency ? Number(adjusted * 1000000n / current.frequency) : 0;
        };
        const copyCalls = delta32(current.copyCalls, previous.copyCalls);
        const uploadIntCalls = delta32(current.uploadIntCalls, previous.uploadIntCalls);
        const presentIntCalls = delta32(current.presentIntCalls, previous.presentIntCalls);
        report(`D3D9WEB_GUEST_PROFILE present_delta=${delta32(current.present, previous.present)} packet_copy_us=${adjustedMicros(current.copyTicks - previous.copyTicks, copyCalls)} packet_copy_calls=${copyCalls} packet_copy_bytes=${delta32(current.copyBytes, previous.copyBytes)} upload_int_us=${adjustedMicros(current.uploadIntTicks - previous.uploadIntTicks, uploadIntCalls)} upload_int_calls=${uploadIntCalls} present_int_us=${adjustedMicros(current.presentIntTicks - previous.presentIntTicks, presentIntCalls)} present_int_calls=${presentIntCalls} timer_ticks=${current.timerOverhead.toString()}`);
        const copySiteNames = ['updatesurface', 'palette', 'declaration', 'transform', 'drawup_header', 'drawup_payload', 'clear'];
        const copySites = copySiteNames.map((name, index) =>
          `${name}:calls=${delta32(current.copySiteCalls[index], previous.copySiteCalls[index])}:bytes=${delta32(current.copySiteBytes[index], previous.copySiteBytes[index])}`);
        report(`D3D9WEB_GUEST_COPY_SITES presents=${delta32(current.present, previous.present)} ${copySites.join(',')}`);
        if (current.stateCalls && previous.stateCalls) {
          const stateNames = ['transform', 'viewport', 'texture'];
          const stateSites = stateNames.map((name, index) =>
            `${name}:calls=${delta32(current.stateCalls[index], previous.stateCalls[index])}:same=${delta32(current.stateSame[index], previous.stateSame[index])}`);
          report(`D3D9WEB_GUEST_STATE_EQUALITY presents=${delta32(current.present, previous.present)} ${stateSites.join(',')}`);
        }
        guestProfileBaseline = current;
      }
      uploadProfile.clear();
      partialBufferUploadProfile.clear();
      drawSamplerProfile.clear();
      indexedDrawResourceProfile.clear();
      alphaStateProfile.enables = 0;
      alphaStateProfile.disables = 0;
      alphaStateProfile.functions = 0;
      alphaStateProfile.reenableAfterDisable = 0;
      resourceLifetimeProfile.creates = 0;
      resourceLifetimeProfile.destroys = 0;
      resourceLifetimeProfile.deleted = 0;
      resourceLifetimeProfile.missing = 0;
      resourceLifetimeProfile.boundBlocked = 0;
      drawUpRingProfile.calls = 0;
      drawUpRingProfile.bytes = 0;
      drawUpRingProfile.orphans = 0;
      drawUpRingProfile.grows = 0;
      drawUpRingProfile.maxOffset = 0;
      drawUpBatchProfile.batches = 0;
      drawUpBatchProfile.calls = 0;
      drawUpBatchProfile.bytes = 0;
      drawUpBatchProfile.uploads = 0;
      transientTexturePoolProfile.hits = 0;
      transientTexturePoolProfile.misses = 0;
      transientTexturePoolProfile.returns = 0;
      transientTexturePoolProfile.evicts = 0;
      if (transientTexturePoolABEnabled) transientTexturePoolEnabled = !transientTexturePoolEnabled;
      indexedCachePresents = 0;
      indexedVertexCacheHits = 0;
      indexedVertexCacheMisses = 0;
      indexedIndexCacheHits = 0;
      indexedIndexCacheMisses = 0;
    }
    if (rgb565DirectABEnabled) {
      rgb565ABPhasePresents += 1;
      const phaseTarget = rgb565ABFirstPhase ? 121 : 120;
      if (rgb565ABPhasePresents === phaseTarget) {
        report(`D3D9WEB_RGB565_DIRECT presents=120 enabled=${rgb565DirectEnabled ? 1 : 0} direct=${rgb565DirectUses} copy=${rgb565CopyUses}`);
        rgb565DirectEnabled = !rgb565DirectEnabled;
        rgb565DirectUses = 0;
        rgb565CopyUses = 0;
        rgb565ABPhasePresents = 0;
        rgb565ABFirstPhase = false;
      }
    }
    if (leanMarkerEnabled && ++leanMarkerPresents === 120) {
      report(`D3D9WEB_PRESENT_WINDOW lean=${runtimeDiagnosticsEnabled ? 0 : 1} presents=120`);
      leanMarkerPresents = 0;
    }
    if (gpuTimingEnabled && gpuTimerBatches >= 120) {
      const averageUs = gpuTimerSamples ? Math.round(gpuTimerNanoseconds / gpuTimerSamples / 1000) : 0;
      report(`D3D9WEB_GPU_PROFILE batches=${gpuTimerBatches} samples=${gpuTimerSamples} avg_us=${averageUs} ext=${gpuTimerExt ? 1 : 0}`);
      gpuTimerNanoseconds = 0;
      gpuTimerSamples = 0;
      gpuTimerBatches = 0;
    }
  }

  function captureGuestProfile(view, bytes) {
    if (bytes.byteLength < 64) return;
    const readCounter = (offset) =>
      BigInt(view.getUint32(offset, true)) |
      (BigInt(view.getUint32(offset + 4, true)) << 32n);
    guestProfileLatest = {
      present: view.getUint32(4, true),
      copyTicks: readCounter(8),
      copyCalls: view.getUint32(16, true),
      copyBytes: view.getUint32(20, true),
      uploadIntTicks: readCounter(24),
      uploadIntCalls: view.getUint32(32, true),
      presentIntTicks: readCounter(36),
      presentIntCalls: view.getUint32(44, true),
      frequency: readCounter(48),
      timerOverhead: readCounter(56),
      copySiteCalls: bytes.byteLength >= 120 ?
        Array.from({ length: 7 }, (_, index) => view.getUint32(64 + index * 4, true)) : Array(7).fill(0),
      copySiteBytes: bytes.byteLength >= 120 ?
        Array.from({ length: 7 }, (_, index) => view.getUint32(92 + index * 4, true)) : Array(7).fill(0),
      stateCalls: bytes.byteLength >= 144 ?
        Array.from({ length: 3 }, (_, index) => view.getUint32(120 + index * 4, true)) : null,
      stateSame: bytes.byteLength >= 144 ?
        Array.from({ length: 3 }, (_, index) => view.getUint32(132 + index * 4, true)) : null,
    };
    if (!guestProfileBaseline) guestProfileBaseline = guestProfileLatest;
  }

  function pollGpuTimers(gl) {
    if (!gpuTimerExt) return;
    if (gl.getParameter(gpuTimerExt.GPU_DISJOINT_EXT)) {
      for (const query of gpuTimerPending) gl.deleteQuery(query);
      gpuTimerPending.length = 0;
      return;
    }
    while (gpuTimerPending.length && gl.getQueryParameter(gpuTimerPending[0], gl.QUERY_RESULT_AVAILABLE)) {
      const query = gpuTimerPending.shift();
      gpuTimerNanoseconds += gl.getQueryParameter(query, gl.QUERY_RESULT);
      gpuTimerSamples += 1;
      gl.deleteQuery(query);
    }
  }

  function beginGpuBatch() {
    if (!gpuTimingEnabled || !renderer || !gpuTimerExt) return null;
    const query = renderer.gl.createQuery();
    renderer.gl.beginQuery(gpuTimerExt.TIME_ELAPSED_EXT, query);
    return query;
  }

  function endGpuBatch(query) {
    if (!query || !renderer || !gpuTimerExt) return;
    renderer.gl.endQuery(gpuTimerExt.TIME_ELAPSED_EXT);
    gpuTimerPending.push(query);
    gpuTimerBatches += 1;
    pollGpuTimers(renderer.gl);
  }

  function checkGlDrawError(gl, label) {
    // WebGL validation is useful evidence, but a synchronous query after all
    // 55 D3D draws per frame dominates the interpreter path.  Sample it while
    // retaining a bounded, real-driver failure signal.
    if (!runtimeDiagnosticsEnabled || (++glDrawValidationCount & 0xff) !== 0) return;
    const error = gl.getError();
    if (error !== gl.NO_ERROR) throw new Error(`WebGL ${label} error 0x${error.toString(16)}`);
  }

  function applySampler(stage) {
    if (!renderer || !samplerObjects || stage >= 8) return;
    const { gl } = renderer;
    const sampler = samplerObjects[stage];
    const values = samplerStates[stage];
    const texture = resources.get(boundTextures[stage]);
    const clampA4Text = a4TextClampEnabled && texture?.format === 26;
    gl.samplerParameteri(sampler, gl.TEXTURE_WRAP_S,
      clampA4Text ? gl.CLAMP_TO_EDGE : d3dAddressMode(gl, values.addressU));
    gl.samplerParameteri(sampler, gl.TEXTURE_WRAP_T,
      clampA4Text ? gl.CLAMP_TO_EDGE : d3dAddressMode(gl, values.addressV));
    gl.samplerParameteri(sampler, gl.TEXTURE_MAG_FILTER, d3dMagFilter(gl, values.mag));
    const materialMipFallback = Boolean(
      stage === 1 && texture && texture.materialMipLevels === 4);
    const forceLinearA4 = a4TextLinearEnabled && texture?.format === 26;
    if (forceLinearA4) {
      gl.samplerParameteri(sampler, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.samplerParameteri(sampler, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    } else {
      gl.samplerParameteri(sampler, gl.TEXTURE_MIN_FILTER,
        d3dMinFilter(gl, values.min,
          values.mip || (materialMipFallback ? 2 : 0),
          Boolean(texture && (texture.levels > 1 || materialMipFallback))));
    }
    gl.bindSampler(stage * 2, sampler);
  }

  function setSamplerState(stage, type, value) {
    if (stage >= 8) throw new Error(`sampler stage out of range ${stage}`);
    if (stateChurnProbeEnabled) {
      const prior = stateChurnSamplerValues[stage].get(type);
      stateChurnProfile.sampler.commands += 1;
      if (prior === (value >>> 0)) stateChurnProfile.sampler.same += 1;
      stateChurnSamplerValues[stage].set(type, value >>> 0);
    }
    if (runtimeDiagnosticsEnabled && stage < 2 && type < 14) rawSamplerStates[stage][type] = value >>> 0;
    const values = samplerStates[stage];
    if (type === 1) values.addressU = value;
    else if (type === 2) values.addressV = value;
    else if (type === 5) values.mag = value;
    else if (type === 6) values.min = value;
    else if (type === 7) values.mip = value;
    samplerDirty[stage] = true;
  }

  function createTextureResource(view, bytes) {
    if (bytes.byteLength !== 40) throw new Error(`bad CreateTexture size ${bytes.byteLength}`);
    const handle = view.getUint32(4, true);
    const width = view.getUint32(8, true);
    const height = view.getUint32(12, true);
    const levels = view.getUint32(16, true);
    const usage = view.getUint32(20, true);
    const format = view.getUint32(24, true);
    const pool = view.getUint32(28, true);
    const pitch = view.getUint32(32, true);
    const totalBytes = view.getUint32(36, true);
    const bytesPerPixel = textureBytesPerPixel(format);
    const layout = buildMipLayout(width, height, levels, bytesPerPixel);
    if (!handle || !width || !height || !levels || levels > 16 ||
        !bytesPerPixel || resources.has(handle) || pitch !== width * bytesPerPixel ||
        totalBytes !== layout.totalBytes) {
      throw new Error(`bad CreateTexture handle=${handle} size=${width}x${height} levels=${levels}`);
    }
    const { gl } = ensureRenderer(viewport.width, viewport.height);
    const poolKey = transientTexturePoolEnabled &&
      width === 256 && height === 256 && levels === 1 && format === 23 &&
      pitch === 512 && totalBytes === 131072 ?
      `${textureContextGeneration}:${width}:${height}:${levels}:${usage}:${format}:${pool}:${pitch}:${totalBytes}` : '';
    const pooledIndex = poolKey ? transientTexturePool.findIndex((entry) => entry.key === poolKey) : -1;
    const pooled = pooledIndex >= 0 ? transientTexturePool.splice(pooledIndex, 1)[0] : null;
    if (poolKey) {
      if (transientTexturePoolMetricsEnabled) {
        if (pooled) transientTexturePoolProfile.hits += 1;
        else transientTexturePoolProfile.misses += 1;
      }
    }
    const resource = {
      handle, kind: 1, width, height, levels, usage, format, pool, pitch, totalBytes,
      mips: layout.mips,
      raw: pooled ? pooled.raw : new Uint8Array(totalBytes),
      nextUploadOffset: 0, uploadStartOffset: 0, uploadVersion: 0,
      materialMipLevels: 0, materialMipUploadVersion: 0,
      glTexture: pooled ? pooled.glTexture : gl.createTexture(), poolKey,
    };
    if (pooled) resource.raw.fill(0);
    // Texture creation is renderer-internal work, not a D3D SetTexture.
    // Keep the GL unit-0 binding cache honest after using it as the upload unit.
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, resource.glTexture);
    if (!pooled) {
      const info = convertTextureBytes(gl, resource);
      gl.texStorage2D(gl.TEXTURE_2D, levels, info.internalFormat, width, height);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_BASE_LEVEL, 0);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, levels - 1);
    }
    gpuTextureHandles[0] = -1;
    resources.set(handle, resource);
    if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.creates += 1;
    if (sampleResourceEvent()) report(`D3D9WEB_PHASE7_CREATE kind=texture handle=${handle} format=${format} size=${width}x${height} levels=${levels} bytes=${totalBytes}`);
  }

  function createBufferResource(view, bytes) {
    if (bytes.byteLength !== 28) throw new Error(`bad CreateBuffer size ${bytes.byteLength}`);
    const handle = view.getUint32(4, true);
    const kind = view.getUint32(8, true);
    const length = view.getUint32(12, true);
    const usage = view.getUint32(16, true);
    const formatOrFvf = view.getUint32(20, true);
    const pool = view.getUint32(24, true);
    if (!handle || (kind !== 2 && kind !== 3) || !length || resources.has(handle)) {
      throw new Error(`bad CreateBuffer handle=${handle} kind=${kind} length=${length}`);
    }
    resources.set(handle, {
      handle, kind, totalBytes: length, usage, formatOrFvf, pool,
      raw: new Uint8Array(length), nextUploadOffset: 0, uploadVersion: 0,
    });
    if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.creates += 1;
    if (sampleResourceEvent()) report(`D3D9WEB_PHASE7_CREATE kind=${kind === 2 ? 'vb' : 'ib'} handle=${handle} bytes=${length} format=${formatOrFvf}`);
  }

  function ensureStage1MaterialMips(resource) {
    if (!resource || resource.kind !== 1 || resource.levels !== 1 ||
        resource.materialMipLevels === 4 || resource.usage !== 0 ||
        resource.format !== 23 || resource.width !== 512 || resource.height !== 512) {
      return null;
    }
    const { gl } = ensureRenderer(viewport.width, viewport.height);
    const replacement = gl.createTexture();
    const previous = resource.glTexture;
    const source = resource.raw.subarray(0, resource.mips[0].byteSize);
    const info = convertTextureBytes(gl, resource, source);
    gl.activeTexture(gl.TEXTURE0 + 2);
    gl.bindTexture(gl.TEXTURE_2D, replacement);
    gl.texStorage2D(gl.TEXTURE_2D, 4, info.internalFormat,
      resource.width, resource.height);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_BASE_LEVEL, 0);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAX_LEVEL, 3);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, resource.width, resource.height,
      info.format, info.type, info.pixels);
    gl.generateMipmap(gl.TEXTURE_2D);
    resource.glTexture = replacement;
    resource.materialMipLevels = 4;
    resource.materialMipUploadVersion = resource.uploadVersion || 0;
    for (let stage = 0; stage < 8; stage++) {
      if (boundTextures[stage] === resource.handle) gpuTextureHandles[stage] = -1;
    }
    samplerDirty[1] = true;
    report(`D3D9WEB_STAGE1_MIP handle=${resource.handle} size=512x512 levels=4 upload=${resource.uploadVersion || 0}`);
    return previous;
  }

  function uploadResource(view, bytes) {
    if (bytes.byteLength < 40) throw new Error('short UploadResource packet');
    const handle = view.getUint32(4, true);
    const kind = view.getUint32(8, true);
    const offset = view.getUint32(12, true);
    const totalBytes = view.getUint32(16, true);
    const chunkBytes = view.getUint32(20, true);
    const finalChunk = view.getUint32(24, true);
    const lockRequestedBytes = view.getUint32(28, true);
    const lockFlags = view.getUint32(32, true);
    const lockHash = view.getUint32(36, true);
    if (runtimeDiagnosticsEnabled) {
      const uploadKey = `${kind}:${handle}`;
      const uploadSample = uploadProfile.get(uploadKey) || { finals: 0, chunks: 0, bytes: 0 };
      uploadSample.chunks += 1;
      uploadSample.bytes += chunkBytes;
      if (finalChunk) uploadSample.finals += 1;
      uploadProfile.set(uploadKey, uploadSample);
    }
    const resource = resources.get(handle);
    if (!resource || resource.kind !== kind || resource.totalBytes !== totalBytes ||
        bytes.byteLength !== 40 + chunkBytes || offset + chunkBytes > totalBytes) {
      throw new Error(`bad UploadResource handle=${handle} kind=${kind} offset=${offset} chunk=${chunkBytes}`);
    }
    if (finalChunk !== 0 && finalChunk !== 1) throw new Error(`bad UploadResource final=${finalChunk}`);
    const bufferUploadStarts = resource.nextUploadOffset === 0;
    if (bufferUploadStarts) {
      resource.nextUploadOffset = offset;
      resource.uploadStartOffset = offset;
    }
    if (offset !== resource.nextUploadOffset) {
      throw new Error(`non-sequential UploadResource handle=${handle} expected=${resource.nextUploadOffset} got=${offset}`);
    }
    const uploadPayload = bytes.subarray(40);
    const manualBufferDiffMatch = bufferDiffManualArm ?
      /^#bufferdiff=(\d+)$/.exec(globalThis.location?.hash || '') : null;
    const activeBufferDiffHandle = manualBufferDiffMatch ?
      (Number.parseInt(manualBufferDiffMatch[1], 10) >>> 0) : bufferDiffHandle;
    const startBufferDiff = bufferUploadStarts && !bufferDiffDone &&
      activeBufferDiffHandle === handle && kind === 2 &&
      resource.uploadVersion !== 0 &&
      (!bufferDiffManualArm || manualBufferDiffMatch !== null);
    if (startBufferDiff) {
      if (!resource.bufferDiffGeneration) {
        resource.bufferDiffGeneration = ++bufferResourceGeneration;
      }
      if (bufferDiffProfile.generation !== resource.bufferDiffGeneration) {
        bufferDiffProfile.generation = resource.bufferDiffGeneration;
        bufferDiffProfile.length = resource.totalBytes;
        bufferDiffProfile.uploads = 0;
        bufferDiffProfile.bytes = 0;
        bufferDiffProfile.changed = 0;
        bufferDiffProfile.sameUploads = 0;
        bufferDiffProfile.minChanged = Number.POSITIVE_INFINITY;
        bufferDiffProfile.maxChanged = 0;
        bufferDiffProfile.minFirst = Number.POSITIVE_INFINITY;
        bufferDiffProfile.maxLast = -1;
        bufferDiffProfile.changedBlocks = 0;
      }
      resource.bufferDiffCurrent = {
        bytes: 0, changed: 0, first: Number.POSITIVE_INFINITY, last: -1,
        blocks: new Set(),
      };
    }
    if (resource.bufferDiffCurrent) {
      const diff = resource.bufferDiffCurrent;
      diff.bytes += chunkBytes;
      for (let i = 0; i < chunkBytes; i++) {
        if (resource.raw[offset + i] === uploadPayload[i]) continue;
        const absolute = offset + i;
        diff.changed += 1;
        diff.first = Math.min(diff.first, absolute);
        diff.last = Math.max(diff.last, absolute);
        diff.blocks.add(absolute >>> 8);
      }
    }
    resource.raw.set(uploadPayload, offset);
    resource.nextUploadOffset = offset + chunkBytes;
    if (!finalChunk) return;
    // Textures still use full-resource uploads. Vertex/index buffers may
    // legally upload only the D3DLOCK range; their retained raw backing
    // preserves bytes outside that range for later indexed draws.
    if (kind === 1 &&
        (resource.uploadStartOffset !== 0 || resource.nextUploadOffset !== totalBytes)) {
      throw new Error(`partial texture UploadResource handle=${handle}`);
    }
    resource.nextUploadOffset = 0;
    resource.uploadVersion = (resource.uploadVersion + 1) >>> 0;
    if (resource.bufferDiffCurrent) {
      const diff = resource.bufferDiffCurrent;
      delete resource.bufferDiffCurrent;
      bufferDiffProfile.uploads += 1;
      bufferDiffProfile.bytes += diff.bytes;
      bufferDiffProfile.changed += diff.changed;
      bufferDiffProfile.changedBlocks += diff.blocks.size;
      if (diff.changed === 0) bufferDiffProfile.sameUploads += 1;
      bufferDiffProfile.minChanged = Math.min(bufferDiffProfile.minChanged, diff.changed);
      bufferDiffProfile.maxChanged = Math.max(bufferDiffProfile.maxChanged, diff.changed);
      if (diff.last >= 0) {
        bufferDiffProfile.minFirst = Math.min(bufferDiffProfile.minFirst, diff.first);
        bufferDiffProfile.maxLast = Math.max(bufferDiffProfile.maxLast, diff.last);
      }
      if (bufferDiffProfile.uploads === 8) {
        report(`D3D9WEB_BUFFER_DIFF handle=${handle} generation=${bufferDiffProfile.generation} length=${bufferDiffProfile.length} uploads=${bufferDiffProfile.uploads} submitted_bytes=${bufferDiffProfile.bytes} changed=${bufferDiffProfile.changed} same=${bufferDiffProfile.sameUploads} min_changed=${bufferDiffProfile.minChanged} max_changed=${bufferDiffProfile.maxChanged} first=${Number.isFinite(bufferDiffProfile.minFirst) ? bufferDiffProfile.minFirst : -1} last=${bufferDiffProfile.maxLast} block_instances256=${bufferDiffProfile.changedBlocks}`);
        bufferDiffDone = true;
      }
    }
    if (runtimeDiagnosticsEnabled && kind !== 1 && resource.uploadStartOffset !== 0) {
      const key = `${kind}:${handle}:offset=${resource.uploadStartOffset}:bytes=${totalBytes}`;
      partialBufferUploadProfile.set(key, (partialBufferUploadProfile.get(key) || 0) + 1);
    }
    if (resourceTraceEnabled && kind === 2 && lockHash !== 0) {
      report(`D3D9WEB_VB_LOCK handle=${handle} offset=${offset} requested=${lockRequestedBytes} resolved=${chunkBytes} flags=0x${lockFlags.toString(16)} hash=0x${lockHash.toString(16)}`);
    }
    if (kind === 1) {
      if (resourceTraceEnabled && resource.width === 256 && resource.height === 256 &&
          resource.format === 23 && totalBytes === 131072) {
        const payload = bytes.subarray(40);
        const step = Math.max(1, Math.floor(payload.byteLength / 256));
        let sampleHash = 2166136261;
        for (let i = 0; i < payload.byteLength; i += step) {
          sampleHash = Math.imul(sampleHash ^ payload[i], 16777619) >>> 0;
        }
        report(`D3D9WEB_TRANSIENT_TEXTURE_SAMPLE handle=${handle} sample_hash=0x${sampleHash.toString(16).padStart(8, '0')}`);
        if (transientSteadyTraceArmed && transientTraceSamples < 32) {
          let exactSame = true;
          if (!transientTraceReference) {
            transientTraceReference = payload.slice();
          } else if (transientTraceReference.byteLength !== payload.byteLength) {
            exactSame = false;
          } else {
            for (let i = 0; i < payload.byteLength; ++i) {
              if (transientTraceReference[i] !== payload[i]) {
                exactSame = false;
                break;
              }
            }
          }
          if (!exactSame) transientTraceAllSame = false;
          transientTraceSamples += 1;
          report(`D3D9WEB_TRANSIENT_TEXTURE_EXACT sample=${transientTraceSamples} handle=${handle} exact_same=${exactSame ? 1 : 0}`);
          if (transientTraceSamples === 32) {
            canvas.dataset.d3d9webTransientExact = JSON.stringify({
              samples: transientTraceSamples,
              bytes: payload.byteLength,
              allSame: transientTraceAllSame,
            });
          }
        }
      }
      const { gl } = ensureRenderer(viewport.width, viewport.height);
      const previousAlignment = gl.getParameter(gl.UNPACK_ALIGNMENT);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, resource.glTexture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      for (const mip of resource.mips) {
        const source = resource.raw.subarray(mip.offset, mip.offset + mip.byteSize);
        const info = convertTextureBytes(gl, resource, source);
        gl.texSubImage2D(gl.TEXTURE_2D, mip.level, 0, 0, mip.width, mip.height,
          info.format, info.type, info.pixels);
      }
      if (resource.materialMipLevels === 4) {
        gl.generateMipmap(gl.TEXTURE_2D);
        resource.materialMipUploadVersion = resource.uploadVersion;
      }
      // Uploading B temporarily replaces the actual unit-0 binding even when
      // the D3D shadow state and draw cache still say A. Force the next draw
      // to restore A instead of sampling B through a stale cache hit.
      gpuTextureHandles[0] = -1;
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, previousAlignment);
    }
    if (sampleResourceEvent()) report(`D3D9WEB_PHASE7_UPLOAD kind=${kind} handle=${handle} bytes=${totalBytes} levels=${resource.levels || 1} final=1`);
  }

  function destroyResource(view, bytes) {
    if (bytes.byteLength !== 12) throw new Error(`bad DestroyResource size ${bytes.byteLength}`);
    const handle = view.getUint32(4, true);
    const kind = view.getUint32(8, true);
    const resource = resources.get(handle);
    if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.destroys += 1;
    // The guest can detach a resource more than once during Wine cleanup.
    // The second destroy is a no-op at this host boundary, never a JS error
    // that would unwind back into the emulated process.
    if (!resource) {
      if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.missing += 1;
      if (sampleResourceEvent()) report(`D3D9WEB_RESOURCE_DESTROY_MISSING handle=${handle} kind=${kind}`);
      return;
    }
    if (resource.kind !== kind) {
      throw new Error(`bad DestroyResource kind handle=${handle} expected=${resource.kind} got=${kind}`);
    }
    // Ordered transport owns snapshots of all earlier uploads/draws, so they
    // are consumed before this command within the same batch. A zero-ref D3D resource
    // must also be absent from every device binding; retain and report only
    // if that invariant is violated instead of deleting live GL state.
    const bound = kind === 1 ? boundTextures.includes(handle) :
      kind === 2 ? stream0.handle === handle :
      kind === 3 ? indexHandle === handle :
      kind === 4 ? declarationHandle === handle : false;
    if (bound) {
      if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.boundBlocked += 1;
      if (sampleResourceEvent()) report(`D3D9WEB_RESOURCE_DESTROY_BOUND handle=${handle} kind=${kind}`);
      return;
    }
    if (kind === 1 && resource.glTexture) {
      const { gl } = ensureRenderer(viewport.width, viewport.height);
      for (let stage = 0; stage < gpuTextureHandles.length; stage++) {
        if (stage === 0 || gpuTextureHandles[stage] === handle) {
          gl.activeTexture(gl.TEXTURE0 + stage * 2);
          gl.bindTexture(gl.TEXTURE_2D, null);
          gpuTextureHandles[stage] = -1;
        }
      }
      gl.activeTexture(gl.TEXTURE0);
      if (resource.poolKey && transientTexturePool.length < 2) {
        transientTexturePool.push({
          key: resource.poolKey, glTexture: resource.glTexture, raw: resource.raw,
        });
        if (transientTexturePoolMetricsEnabled) transientTexturePoolProfile.returns += 1;
      } else {
        gl.deleteTexture(resource.glTexture);
        if (resource.poolKey && transientTexturePoolMetricsEnabled) transientTexturePoolProfile.evicts += 1;
      }
    }
    resources.delete(handle);
    if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.deleted += 1;
    if (sampleResourceEvent()) report(`D3D9WEB_RESOURCE_DESTROYED handle=${handle} kind=${kind}`);
  }

  function bindTexture(view, bytes) {
    if (bytes.byteLength !== 12) throw new Error(`bad BindTexture size ${bytes.byteLength}`);
    const stage = view.getUint32(4, true);
    const handle = view.getUint32(8, true);
    if (stage >= 8) throw new Error(`bad texture stage ${stage}`);
    const resource = handle ? resources.get(handle) : null;
    if (handle && (!resource || resource.kind !== 1)) throw new Error(`bad texture handle ${handle}`);
    if (stateChurnProbeEnabled) {
      stateChurnProfile.textureBind.commands += 1;
      if (boundTextures[stage] === handle) stateChurnProfile.textureBind.same += 1;
    }
    const { gl, dummyTextures } = ensureRenderer(viewport.width, viewport.height);
    boundTextures[stage] = handle;
    gl.activeTexture(gl.TEXTURE0 + stage * 2);
    gl.bindTexture(gl.TEXTURE_2D, resource ? resource.glTexture : dummyTextures.dummy2D);
    gpuTextureHandles[stage] = handle;
    applySampler(stage);
    samplerDirty[stage] = false;
  }

  function bindStream(view, bytes) {
    if (bytes.byteLength !== 20) throw new Error(`bad BindStream size ${bytes.byteLength}`);
    const stream = view.getUint32(4, true);
    const handle = view.getUint32(8, true);
    const offset = view.getUint32(12, true);
    const stride = view.getUint32(16, true);
    const resource = handle ? resources.get(handle) : null;
    if (stream !== 0 || (handle && (!resource || resource.kind !== 2)) ||
        (resource && offset > resource.totalBytes)) throw new Error(`bad stream binding handle=${handle}`);
    if (stateChurnProbeEnabled) {
      stateChurnProfile.streamBind.commands += 1;
      if (stream0.handle === handle && stream0.offset === offset && stream0.stride === stride) stateChurnProfile.streamBind.same += 1;
    }
    stream0 = { handle, offset, stride };
  }

  function bindIndices(view, bytes) {
    if (bytes.byteLength !== 8) throw new Error(`bad BindIndices size ${bytes.byteLength}`);
    const handle = view.getUint32(4, true);
    const resource = handle ? resources.get(handle) : null;
    if (handle && (!resource || resource.kind !== 3)) throw new Error(`bad index binding ${handle}`);
    indexHandle = handle;
  }

  function createDeclaration(view, bytes) {
    if (bytes.byteLength < 12) throw new Error('short CreateDeclaration packet');
    const handle = view.getUint32(4, true);
    const elementCount = view.getUint32(8, true);
    if (!handle || !elementCount || elementCount > 64 ||
        bytes.byteLength !== 12 + elementCount * 8 || resources.has(handle)) {
      throw new Error(`bad CreateDeclaration handle=${handle} count=${elementCount}`);
    }
    const elements = [];
    for (let i = 0; i < elementCount; i++) {
      const offset = 12 + i * 8;
      elements.push({
        stream: view.getUint16(offset, true),
        offset: view.getUint16(offset + 2, true),
        type: view.getUint8(offset + 4),
        method: view.getUint8(offset + 5),
        usage: view.getUint8(offset + 6),
        usageIndex: view.getUint8(offset + 7),
      });
    }
    // Validate the directly measured fixed-function subset before accepting it.
    declarationLayout(elements, 0xFFFF);
    resources.set(handle, { handle, kind: 4, elements, totalBytes: elementCount * 8 });
    if (runtimeDiagnosticsEnabled) resourceLifetimeProfile.creates += 1;
    report(`D3D9WEB_PHASE8_CREATE kind=declaration handle=${handle} elements=${elementCount}`);
  }

  function bindDeclaration(view, bytes) {
    if (bytes.byteLength !== 8) throw new Error(`bad BindDeclaration size ${bytes.byteLength}`);
    const handle = view.getUint32(4, true);
    const resource = handle ? resources.get(handle) : null;
    if (handle && (!resource || resource.kind !== 4)) {
      throw new Error(`bad declaration binding ${handle}`);
    }
    declarationHandle = handle;
  }

  function applyViewport() {
    if (appliedViewportX === viewport.x &&
        appliedViewportY === viewport.y &&
        appliedViewportWidth === viewport.width &&
        appliedViewportHeight === viewport.height &&
        appliedViewportMinZ === viewport.minZ &&
        appliedViewportMaxZ === viewport.maxZ &&
        appliedViewportCanvasHeight === renderer.backbufferHeight) return;
    appliedViewportX = viewport.x;
    appliedViewportY = viewport.y;
    appliedViewportWidth = viewport.width;
    appliedViewportHeight = viewport.height;
    appliedViewportMinZ = viewport.minZ;
    appliedViewportMaxZ = viewport.maxZ;
    appliedViewportCanvasHeight = renderer.backbufferHeight;
    const { gl } = renderer;
    const scaleX = renderer.backbufferWidth / Math.max(1, canvas.width);
    const scaleY = renderer.backbufferHeight / Math.max(1, canvas.height);
    gl.viewport(
      Math.floor(viewport.x * scaleX),
      renderer.backbufferHeight - Math.ceil((viewport.y + viewport.height) * scaleY),
      Math.max(1, Math.ceil(viewport.width * scaleX)),
      Math.max(1, Math.ceil(viewport.height * scaleY))
    );
    state.transform.viewportInverseExtent[0] = 2 / Math.max(1, viewport.width);
    state.transform.viewportInverseExtent[1] = -2 / Math.max(1, viewport.height);
    state.transform.viewportInverseExtent[2] = 1;
    state.transform.viewportInverseExtent[3] = 1;
    state.transform.viewportInverseOffset[0] = -1;
    state.transform.viewportInverseOffset[1] = 1;
    state.transform.viewportInverseOffset[2] = 0;
    state.transform.viewportInverseOffset[3] = 0;
    gl.depthRange(viewport.minZ, viewport.maxZ);
    ffpStateDirty = true;
  }

  function clearBackbuffer(view, bytes) {
    if (bytes.byteLength < 24) throw new Error(`bad Clear size ${bytes.byteLength}`);
    const count = view.getUint32(4, true);
    const flags = view.getUint32(8, true);
    const color = view.getUint32(12, true);
    const z = view.getFloat32(16, true);
    const stencil = view.getUint32(20, true);
    const validFlags = 1 | 2 | 4;
    if (!(flags & validFlags) || (flags & ~validFlags) || bytes.byteLength !== 24 + count * 16) {
      throw new Error(`bad Clear packet count=${count} flags=0x${flags.toString(16)} bytes=${bytes.byteLength}`);
    }
    const current = ensureRenderer(viewport.width, viewport.height);
    const { gl } = current;
    const scaleX = current.backbufferWidth / Math.max(1, canvas.width);
    const scaleY = current.backbufferHeight / Math.max(1, canvas.height);
    let mask = 0;
    if (flags & 1) {
      gl.clearColor(((color >>> 16) & 255) / 255, ((color >>> 8) & 255) / 255,
        (color & 255) / 255, (color >>> 24) / 255);
      gl.colorMask(true, true, true, true);
      mask |= gl.COLOR_BUFFER_BIT;
    }
    if (flags & 2) {
      gl.clearDepth(z);
      gl.depthMask(true);
      mask |= gl.DEPTH_BUFFER_BIT;
    }
    if (flags & 4) {
      gl.clearStencil(stencil);
      gl.stencilMaskSeparate(gl.FRONT, 0xffffffff);
      gl.stencilMaskSeparate(gl.BACK, 0xffffffff);
      mask |= gl.STENCIL_BUFFER_BIT;
    }

    const viewportRect = {
      left: viewport.x, top: viewport.y,
      right: viewport.x + viewport.width, bottom: viewport.y + viewport.height,
    };
    const regions = count ? Array.from({ length: count }, (_, i) => {
      const offset = 24 + i * 16;
      return {
        left: Math.max(viewportRect.left, view.getInt32(offset, true)),
        top: Math.max(viewportRect.top, view.getInt32(offset + 4, true)),
        right: Math.min(viewportRect.right, view.getInt32(offset + 8, true)),
        bottom: Math.min(viewportRect.bottom, view.getInt32(offset + 12, true)),
      };
    }) : [viewportRect];

    gl.enable(gl.SCISSOR_TEST);
    for (const region of regions) {
      if (region.right <= region.left || region.bottom <= region.top) continue;
      const left = Math.floor(region.left * scaleX);
      const right = Math.ceil(region.right * scaleX);
      const top = Math.floor(region.top * scaleY);
      const bottom = Math.ceil(region.bottom * scaleY);
      gl.scissor(left, current.backbufferHeight - bottom,
        Math.max(1, right - left), Math.max(1, bottom - top));
      gl.clear(mask);
    }

    const colorWrite = renderStates.get(168) ?? 0x0f;
    gl.colorMask(Boolean(colorWrite & 1), Boolean(colorWrite & 2),
      Boolean(colorWrite & 4), Boolean(colorWrite & 8));
    gl.depthMask(Boolean(renderStates.get(14) ?? 1));
    const stencilWrite = renderStates.get(59) ?? 0xffffffff;
    gl.stencilMaskSeparate(gl.FRONT, stencilWrite);
    gl.stencilMaskSeparate(gl.BACK, stencilWrite);
    const scissorLeft = Math.floor(scissor.left * scaleX);
    const scissorRight = Math.ceil(scissor.right * scaleX);
    const scissorTop = Math.floor(scissor.top * scaleY);
    const scissorBottom = Math.ceil(scissor.bottom * scaleY);
    gl.scissor(scissorLeft, current.backbufferHeight - scissorBottom,
      Math.max(1, scissorRight - scissorLeft), Math.max(1, scissorBottom - scissorTop));
    if (renderStates.get(174)) gl.enable(gl.SCISSOR_TEST); else gl.disable(gl.SCISSOR_TEST);
    if (runtimeDiagnosticsEnabled && carpetTracePending && (flags & 1)) {
      carpetTracePending = false;
      carpetTraceActive = true;
      carpetTraceLastPixels = readCarpetTracePixels(gl);
      report(`D3D9WEB_CARPET_TRACE_BEGIN targets=${carpetTraceTargets.map((target) => `${target.name}:${target.x},${target.y}`).join(',')}`);
    }
    if (runtimeDiagnosticsEnabled && dpupCapturePending && (flags & 1)) {
      dpupCapturePending = false;
      dpupCaptureActive = true;
      dpupCaptureOrdinal = 0;
      dpupCaptureDropped = 0;
      dpupCaptureRecords.length = 0;
      report('D3D9WEB_DPUP_CAPTURE_BEGIN');
    }
    if (HOT_TRACE) traceHot(`D3D9WEB_PHASE8_CLEAR flags=0x${flags.toString(16)} count=${count} color=0x${color.toString(16).toUpperCase()} z=${z} stencil=${stencil}`);
  }

  function markTransformDirty(slot) {
    ffpStateDirty = true;
    if (slot === 2) {
      baseTransformDirty = true;
      viewTransformDirty = true;
      lightsDirty = true;
      blendMatricesDirty = true;
    } else if (slot === 3) {
      baseTransformDirty = true;
    } else if (slot >= 16 && slot < 24) {
      dirtyTextureStages.add(slot - 16);
    } else if (slot >= 256 && slot < 512) {
      dirtyBlendSlots.add(slot);
      blendMatricesDirty = true;
      if (slot === 256) baseTransformDirty = true;
    }
  }

  function recomputeTransformsIfDirty() {
    const identity = identityRowMajor();
    const world = transformSlots.get(256) || identity;
    const view = transformSlots.get(2) || identity;
    const projection = transformSlots.get(3) || identity;

    if (baseTransformDirty) {
      const worldView = multiplyRowMajor(world, view);
      state.transform.worldView = d3dToGlslMatrix(worldView);
      state.transform.worldViewProj = d3dToGlslMatrix(
        multiplyRowMajor(worldView, projection));
      state.transform.projection = d3dToGlslMatrix(projection);
      state.transform.normalMatrix = d3dToGlslMatrix(inverseRowMajor(worldView));
      // The shader uses inverseView with a column-vector multiply, unlike the
      // row-vector D3D transform paths. Keep this one in raw row-major order.
      state.transform.inverseView = inverseRowMajor(view);
      baseTransformDirty = false;
    }

    for (const stage of dirtyTextureStages) {
      const textureMatrix = transformSlots.get(16 + stage);
      if (textureMatrix) {
        state.transform.texcoordMatrices[stage] = d3dToGlslMatrix(textureMatrix);
      }
    }
    dirtyTextureStages.clear();

    if (viewTransformDirty) {
      for (const [slot, blendWorld] of transformSlots) {
        if (slot >= 256 && slot < 512) {
          state.blendMatrices[slot - 256] = d3dToGlslMatrix(
            multiplyRowMajor(blendWorld, view));
        }
      }
      viewTransformDirty = false;
      dirtyBlendSlots.clear();
    } else {
      for (const slot of dirtyBlendSlots) {
        state.blendMatrices[slot - 256] = d3dToGlslMatrix(
          multiplyRowMajor(transformSlots.get(slot), view));
      }
      dirtyBlendSlots.clear();
    }

    if (lightsDirty) rebuildActiveLights();
  }

  function readFloat4(view, offset) {
    return [0, 1, 2, 3].map((i) => view.getFloat32(offset + i * 4, true));
  }

  function setMaterial(view, bytes) {
    if (bytes.byteLength !== 72) throw new Error(`bad SetMaterial size ${bytes.byteLength}`);
    state.material = {
      diffuse: readFloat4(view, 4), ambient: readFloat4(view, 20),
      specular: readFloat4(view, 36), emissive: readFloat4(view, 52),
      power: view.getFloat32(68, true),
    };
    ffpStateDirty = true;
    if (HOT_TRACE) materialCommands++;
  }

  function rebuildActiveLights() {
    const view = transformSlots.get(2) || identityRowMajor();
    const active = [...enabledLights].sort((a, b) => a - b)
      .map((index) => lightDefinitions.get(index)).filter(Boolean).slice(0, 8)
      .map((light) => ({
        ...light,
        position: transformRowVector(light.position, view),
        direction: normalizeDirection(transformRowVector(light.direction, view)),
      }));
    for (let i = 0; i < 8; i++) {
      state.lights[i] = active[i] || {
        diffuse: [0, 0, 0, 0], specular: [0, 0, 0, 0], ambient: [0, 0, 0, 0],
        position: [0, 0, 0, 0], direction: [0, 0, 0, 0], type: 0,
        range: 0, falloff: 0, attenuation0: 0, attenuation1: 0,
        attenuation2: 0, theta: 0, phi: 0,
      };
    }
    state.dataPrimitives.lightCount = active.length;
    lightsDirty = false;
  }

  function setLight(view, bytes) {
    if (bytes.byteLength !== 112) throw new Error(`bad SetLight size ${bytes.byteLength}`);
    const index = view.getUint32(4, true);
    if (index >= 8) throw new Error(`light index out of range ${index}`);
    lightDefinitions.set(index, {
      type: view.getUint32(8, true), diffuse: readFloat4(view, 12),
      specular: readFloat4(view, 28), ambient: readFloat4(view, 44),
      position: [view.getFloat32(60, true), view.getFloat32(64, true), view.getFloat32(68, true), 1],
      direction: [view.getFloat32(72, true), view.getFloat32(76, true), view.getFloat32(80, true), 0],
      range: view.getFloat32(84, true), falloff: view.getFloat32(88, true),
      attenuation0: view.getFloat32(92, true), attenuation1: view.getFloat32(96, true),
      attenuation2: view.getFloat32(100, true), theta: view.getFloat32(104, true),
      phi: view.getFloat32(108, true),
    });
    lightsDirty = true;
    ffpStateDirty = true;
    if (HOT_TRACE) lightCommands++;
  }

  function lightEnable(view, bytes) {
    if (bytes.byteLength !== 16) throw new Error(`bad LightEnable size ${bytes.byteLength}`);
    const index = view.getUint32(4, true);
    if (index >= 8) throw new Error(`light index out of range ${index}`);
    if (view.getUint32(8, true)) enabledLights.add(index); else enabledLights.delete(index);
    lightsDirty = true;
    ffpStateDirty = true;
    if (HOT_TRACE) lightEnableCommands++;
  }

  function d3dBlendFactor(gl, value) {
    switch (value >>> 0) {
      case 1: return gl.ZERO;                    // D3DBLEND_ZERO
      case 2: return gl.ONE;                     // D3DBLEND_ONE
      case 3: return gl.SRC_COLOR;               // D3DBLEND_SRCCOLOR
      case 4: return gl.ONE_MINUS_SRC_COLOR;     // D3DBLEND_INVSRCCOLOR
      case 5: return gl.SRC_ALPHA;               // D3DBLEND_SRCALPHA
      case 6: return gl.ONE_MINUS_SRC_ALPHA;     // D3DBLEND_INVSRCALPHA
      case 7: return gl.DST_ALPHA;               // D3DBLEND_DESTALPHA
      case 8: return gl.ONE_MINUS_DST_ALPHA;     // D3DBLEND_INVDESTALPHA
      case 9: return gl.DST_COLOR;               // D3DBLEND_DESTCOLOR
      case 10: return gl.ONE_MINUS_DST_COLOR;    // D3DBLEND_INVDESTCOLOR
      case 11: return gl.SRC_ALPHA_SATURATE;     // D3DBLEND_SRCALPHASAT
      default: return gl.ONE;
    }
  }

  function applyBlendFactors(gl) {
    const src = renderStates.get(19) ?? 2; // D3DRS_SRCBLEND, D3DBLEND_ONE
    const dst = renderStates.get(20) ?? 1; // D3DRS_DESTBLEND, D3DBLEND_ZERO
    gl.blendFunc(d3dBlendFactor(gl, src), d3dBlendFactor(gl, dst));
  }

  function applyRenderState(id, value) {
    const normalized = value >>> 0;
    if (stateChurnProbeEnabled) {
      stateChurnProfile.render.commands += 1;
      if (renderStates.get(id) === normalized) stateChurnProfile.render.same += 1;
    }
    renderStates.set(id, normalized);
    ffpStateDirty = true;
    if (!renderer) return;
    const { gl } = renderer;
    switch (id) {
      case 7: value ? gl.enable(gl.DEPTH_TEST) : gl.disable(gl.DEPTH_TEST); break;
      case 14: gl.depthMask(Boolean(value)); break;
      // D3DRS_ALPHATESTENABLE gates D3DRS_ALPHAFUNC.  Acknex suppresses
      // unchanged SetRenderState calls in the guest DLL, so simply retaining
      // the most recently seen function loses the test after off -> on: the
      // function command is not resent.  Keep the function in renderStates
      // and derive the shader operation from the enable bit on every change.
      case 15:
        if (runtimeDiagnosticsEnabled) {
          if (value) {
            alphaStateProfile.enables += 1;
            if (alphaStateProfile.lastEnabled === false) alphaStateProfile.reenableAfterDisable += 1;
          } else {
            alphaStateProfile.disables += 1;
          }
          alphaStateProfile.lastEnabled = Boolean(value);
        }
        state.shared.alphaCompareOp = value
          ? Math.max(0, Math.min(7, ((renderStates.get(25) ?? 8) | 0) - 1))
          : COMPARE_OP.ALWAYS;
        break;
      case 22:
        if (value === 1) gl.disable(gl.CULL_FACE);
        // D3D names the winding to discard; WebGL names the face to discard.
        // With the renderer's CCW front-face convention, CW is WebGL's BACK
        // and CCW is WebGL's FRONT.
        else { gl.enable(gl.CULL_FACE); gl.cullFace(value === 2 ? gl.BACK : gl.FRONT); }
        break;
      case 23: gl.depthFunc(d3dCompareFunc(gl, value)); break;
      case 24: state.shared.alphaRef = value & 0xff; break;
      case 25:
        if (runtimeDiagnosticsEnabled) alphaStateProfile.functions += 1;
        // A function written while testing is disabled must be remembered,
        // but it must not reactivate alpha rejection before state 15 does.
        state.shared.alphaCompareOp = renderStates.get(15)
          ? Math.max(0, Math.min(7, (value | 0) - 1))
          : COMPARE_OP.ALWAYS;
        break;
      case 19:
      case 20:
        applyBlendFactors(gl);
        break;
      case 27:
        value ? gl.enable(gl.BLEND) : gl.disable(gl.BLEND);
        if (value) applyBlendFactors(gl);
        break;
      case 28: state.shared.fogEnable = Boolean(value); break;
      case 29: state.shared.specularEnable = Boolean(value); break;
      case 34:
        state.shared.fogColor = { r: (value >>> 16) & 255, g: (value >>> 8) & 255, b: value & 255 };
        break;
      case 35: state.shared.pixelFogMode = value & 3; break;
      case 36: state.shared.fogDistanceScale = dwordAsFloat(value); break;
      case 37: state.shared.fogDistanceEnd = dwordAsFloat(value); break;
      case 38: state.shared.fogDensity = dwordAsFloat(value); break;
      case 48: state.dataPrimitives.rangeFog = Boolean(value); break;
      case 60:
        state.textureFactor = { a: value >>> 24, r: (value >>> 16) & 255, g: (value >>> 8) & 255, b: value & 255 };
        break;
      case 136: state.dataPrimitives.vertexClipping = Boolean(value); break;
      case 137: state.dataPrimitives.useLighting = Boolean(value); break;
      case 139:
        state.globalAmbient = { a: value >>> 24, r: (value >>> 16) & 255, g: (value >>> 8) & 255, b: value & 255 };
        break;
      case 140: state.shared.vertexFogMode = value & 3; break;
      case 142: state.dataPrimitives.localViewer = Boolean(value); break;
      case 143: state.dataPrimitives.normalizeNormals = Boolean(value); break;
      case 145: state.dataPrimitives.diffuseSource = value & 3; break;
      case 146: state.dataPrimitives.specularSource = value & 3; break;
      case 147: state.dataPrimitives.ambientSource = value & 3; break;
      case 148: state.dataPrimitives.emissiveSource = value & 3; break;
      case 154: state.point.size = value & 0xffff; break;
      case 156: state.shared.pointSpriteEnable = Boolean(value); break;
      case 157: state.shared.pointScaleEnable = Boolean(value); break;
      case 174: value ? gl.enable(gl.SCISSOR_TEST) : gl.disable(gl.SCISSOR_TEST); break;
      // D3DRS_COLORWRITEENABLE.  Without this the mask was only ever synced
      // inside clearBackbuffer, so a guest that toggles it within a frame drew
      // against the previous frame's mask.  The chat overlay does exactly that
      // (measured: 88 x "15 then 0" per second, ending each frame on 0), which
      // left every draw masked out and froze the navy clear on screen.
      // Bit order matches the D3D9 contract already used by clearBackbuffer:
      // RED=1, GREEN=2, BLUE=4, ALPHA=8.
      case 168:
        gl.colorMask(Boolean(value & 1), Boolean(value & 2),
          Boolean(value & 4), Boolean(value & 8));
        break;
      default: break;
    }
  }

  function setTextureStageState(stage, type, value) {
    if (stage >= 8) return;
    if (stateChurnProbeEnabled) {
      const prior = stateChurnTextureStageValues[stage].get(type);
      stateChurnProfile.textureStage.commands += 1;
      if (prior === (value >>> 0)) stateChurnProfile.textureStage.same += 1;
      stateChurnTextureStageValues[stage].set(type, value >>> 0);
    }
    ffpStateDirty = true;
    if (runtimeDiagnosticsEnabled && stage < 2 && type < 33) rawTextureStageStates[stage][type] = value >>> 0;
    const st = state.textureStages[stage];
    switch (type) {
      case 1: st.colorOp = value; break;
      case 2: st.colorArgs[1] = parseD3DTA(value); break;
      case 3: st.colorArgs[2] = parseD3DTA(value); break;
      case 4: st.alphaOp = value; break;
      case 5: st.alphaArgs[1] = parseD3DTA(value); break;
      case 6: st.alphaArgs[2] = parseD3DTA(value); break;
      case 11:
        rawTexcoordIndex[stage] = value;
        state.dataPrimitives.texcoordIndex[stage] = value & 7;
        break;
      case 24:
        state.dataPrimitives.texcoordTransformFlags[stage] = value & 0xff;
        if (value & 0x100) state.shared.projectedMask |= (1 << stage);
        else state.shared.projectedMask &= ~(1 << stage);
        break;
      case 26: st.colorArgs[0] = parseD3DTA(value); break;
      case 27: st.alphaArgs[0] = parseD3DTA(value); break;
      case 28: st.storeToTemp = (value & 0x0f) === D3DTA.TEMP; break;
      case 32:
        state.pixelStages[stage].constant = {
          a: value >>> 24, r: (value >>> 16) & 255, g: (value >>> 8) & 255, b: value & 255,
        };
        break;
      default: break;
    }
  }

  function readCarpetTracePixels(gl) {
    return carpetTraceTargets.map((target) => {
      const pixel = new Uint8Array(4);
      gl.readPixels(target.x, canvas.height - target.y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      return Array.from(pixel);
    });
  }

  function summarizeUvRange(layout, stride, vertexBytes) {
    const vertexCount = Math.floor(vertexBytes.byteLength / stride);
    const view = new DataView(vertexBytes.buffer, vertexBytes.byteOffset, vertexBytes.byteLength);
    const summaries = [];
    for (let stage = 0; stage < 8; stage++) {
      const sourceIndex = rawTexcoordIndex[stage] & 7;
      const attr = layout.attributes.find((candidate) => candidate.location === 4 + sourceIndex);
      if (!attr || attr.type !== 'float' || attr.size < 2) {
        summaries.push(`s${stage}=src${sourceIndex}:none`);
        continue;
      }
      let minU = Infinity;
      let maxU = -Infinity;
      let minV = Infinity;
      let maxV = -Infinity;
      for (let vertex = 0; vertex < vertexCount; vertex++) {
        const offset = vertex * stride + attr.offset;
        const u = view.getFloat32(offset, true);
        const v = view.getFloat32(offset + 4, true);
        if (Number.isFinite(u) && Number.isFinite(v)) {
          minU = Math.min(minU, u); maxU = Math.max(maxU, u);
          minV = Math.min(minV, v); maxV = Math.max(maxV, v);
        }
      }
      summaries.push(Number.isFinite(minU) ?
        `s${stage}=src${sourceIndex}:${minU.toFixed(3)}..${maxU.toFixed(3)}/${minV.toFixed(3)}..${maxV.toFixed(3)}` :
        `s${stage}=src${sourceIndex}:nan`);
    }
    return summaries.join(',');
  }

  function traceCarpetPixelChanges(kind, layout, stride, vertexBytes) {
    if (!runtimeDiagnosticsEnabled || !carpetTraceActive || !renderer || !carpetTraceLastPixels) return;
    const pixels = readCarpetTracePixels(renderer.gl);
    const changed = [];
    for (let index = 0; index < pixels.length; index++) {
      if (pixels[index].some((value, channel) => value !== carpetTraceLastPixels[index][channel])) {
        changed.push(`${carpetTraceTargets[index].name}:${carpetTraceLastPixels[index].join('.')}>${pixels[index].join('.')}`);
      }
    }
    carpetTraceLastPixels = pixels;
    if (!changed.length) return;
    const stageState = boundTextures.map((handle, stage) =>
      `s${stage}=t${handle}/a${samplerStates[stage].addressU}.${samplerStates[stage].addressV}` +
      `/tci0x${rawTexcoordIndex[stage].toString(16)}/tt0x${state.dataPrimitives.texcoordTransformFlags[stage].toString(16)}`)
      .join(',');
    report(`D3D9WEB_CARPET_TRACE_DRAW kind=${kind} changed=${changed.join(',')} stages=${stageState} uv=${summarizeUvRange(layout, stride, vertexBytes)}`);
  }

  function recordQuestTitleDraw(kind, primitiveType, primitiveCount, stride,
    packetFvf, vertexBytes, drawLayout = null) {
    // The browser verifier arms this only after PatHouse has appeared.  Text
    // resources churn during bootstrap, so collecting from process start
    // changes the workload under investigation.
    if (!questTitleTraceEnabled || canvas.dataset.d3d9webQuestTitleTraceArm !== '1') return;
    drawLayout = drawLayout || fvfLayout(packetFvf, stride);
    const texture = resources.get(boundTextures[0]);
    // Do not infer this resource's dimensions.  The actual title was excluded
    // by the former 1024x32 assumption; its screen rectangle below is the
    // authoritative selector.  The bounded rolling window still prevents a
    // broad trace from retaining bootstrap history into PatHouse.
    const stateIds = [7, 14, 15, 19, 20, 23, 24, 25, 27, 168];
    const stateDefaults = [1, 1, 0, 2, 1, 4, 0, 8, 0, 15];
    const fnv1a = (data) => {
      let hash = 0x811c9dc5;
      for (let i = 0; i < data.length; i++) {
        hash ^= data[i];
        hash = Math.imul(hash, 0x01000193) >>> 0;
      }
      return hash.toString(16).padStart(8, '0');
    };
    // This is deliberately decoded from the original, pre-swizzle DrawUP
    // bytes.  Quest list titles are transformed XYZRHW quads, so their screen
    // rectangle identifies the actual text resource independently of its
    // handle allocation order or identical font-state family.
    const position = drawLayout.attributes
      .find((attribute) => attribute.location === 0);
    const texcoord = drawLayout.attributes
      .find((attribute) => attribute.location === 4);
    const vertexCount = primitiveVertexCount(primitiveType, primitiveCount);
    const range = (offset, components) => {
      if (!vertexCount || !components) return null;
      const view = new DataView(vertexBytes.buffer, vertexBytes.byteOffset,
        vertexBytes.byteLength);
      const min = Array(components).fill(Number.POSITIVE_INFINITY);
      const max = Array(components).fill(Number.NEGATIVE_INFINITY);
      for (let vertex = 0; vertex < vertexCount; vertex++) {
        const base = vertex * stride + offset;
        for (let component = 0; component < components; component++) {
          const value = view.getFloat32(base + component * 4, true);
          min[component] = Math.min(min[component], value);
          max[component] = Math.max(max[component], value);
        }
      }
      return { min, max };
    };
    questTitleTraceRecords.push({
      kind, primitiveType, primitiveCount, stride, fvf: packetFvf >>> 0,
      positionT: drawLayout.positionT,
      texture: texture ? {
        handle: texture.handle, width: texture.width, height: texture.height,
        format: texture.format, uploadVersion: texture.uploadVersion,
        fnv: texture.raw ? fnv1a(texture.raw) : null,
      } : { handle: boundTextures[0] || 0, missing: true },
      sampler0: rawSamplerStates[0].slice(),
      textureStage0: rawTextureStageStates[0].slice(),
      render: stateIds.map((id, index) => renderStates.get(id) ?? stateDefaults[index]),
      viewport: { ...viewport },
      screen: position?.type === 'float' ? range(position.offset, Math.min(position.size, 2)) : null,
      uv0: texcoord?.type === 'float' ? range(texcoord.offset, Math.min(texcoord.size, 2)) : null,
      geometryTypes: { position: position?.type, uv0: texcoord?.type },
      vertices: Array.from(vertexBytes),
    });
    // Character selection draws enough 1024x32 labels to fill the bounded
    // record before the player reaches PatHouse. Retain the most recent frame
    // family so the quest panel, not the bootstrap scene, is inspectable.
    if (questTitleTraceRecords.length > 128) questTitleTraceRecords.shift();
    // Keep the live object accessible to the in-app verifier without creating
    // a large JSON copy or emitting a probe message on every rendered frame.
    globalThis.D3D9WebQuestTitleTrace = questTitleTraceRecords;
    if (a4DumpAllEnabled && texture?.raw && textureBytesPerPixel(texture.format)) {
      a4DumpRows.delete(texture.handle);
      a4DumpRows.set(texture.handle, texture);
      if (a4DumpRows.size > 16) a4DumpRows.delete(a4DumpRows.keys().next().value);
    }
    if (!a4DumpPublished && Number.isInteger(a4DumpHandle) &&
        texture?.handle === a4DumpHandle && diagnosticTextureRgba(texture) &&
        typeof OffscreenCanvas === 'function') {
      a4DumpPublished = true;
      const scratch = new OffscreenCanvas(texture.width, texture.height);
      const context = scratch.getContext('2d');
      const image = context.createImageData(texture.width, texture.height);
      image.data.set(diagnosticTextureRgba(texture));
      context.putImageData(image, 0, 0);
      scratch.convertToBlob({ type: 'image/png' }).then(async (blob) => {
        const bytes = new Uint8Array(await blob.arrayBuffer());
        let binary = '';
        for (let offset = 0; offset < bytes.length; offset += 0x8000) {
          binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
        }
        canvas.dataset.d3d9webA4DumpPng = btoa(binary);
        canvas.dataset.d3d9webA4DumpHandle = String(texture.handle);
      }).catch((error) => {
        canvas.dataset.d3d9webA4DumpError = String(error);
      });
    }
    if (a4DumpAllEnabled && a4DumpRows.size >= 1 &&
        performance.now() - a4DumpAllLastPublishedAt >= 1000 &&
        typeof OffscreenCanvas === 'function') {
      a4DumpAllLastPublishedAt = performance.now();
      // The later allocations belong to the active PatHouse UI; startup
      // character-select assets otherwise dominate the diagnostic sheet.
      const rows = [...a4DumpRows.values()].slice(-16);
      const scratch = new OffscreenCanvas(296, rows.length * 64);
      const context = scratch.getContext('2d');
      context.fillStyle = '#ffffff';
      context.font = '10px sans-serif';
      for (let row = 0; row < rows.length; row++) {
        const source = rows[row];
        const sourceCanvas = new OffscreenCanvas(source.width, source.height);
        const sourceContext = sourceCanvas.getContext('2d');
        const image = sourceContext.createImageData(source.width, source.height);
        const rgba = diagnosticTextureRgba(source);
        if (!rgba) continue;
        image.data.set(rgba);
        sourceContext.putImageData(image, 0, 0);
        context.fillText(String(source.handle), 2, row * 64 + 12);
        context.drawImage(sourceCanvas, 40, row * 64, 256, 64);
      }
      scratch.convertToBlob({ type: 'image/png' }).then(async (blob) => {
        const bytes = new Uint8Array(await blob.arrayBuffer());
        let binary = '';
        for (let offset = 0; offset < bytes.length; offset += 0x8000) {
          binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
        }
        canvas.dataset.d3d9webA4DumpPng = btoa(binary);
        canvas.dataset.d3d9webA4DumpHandle = rows.map((item) => item.handle).join(',');
      }).catch((error) => {
        canvas.dataset.d3d9webA4DumpError = String(error);
      });
    }
  }

  function drawPrimitiveUP(view, bytes) {
    const primitiveType = view.getUint32(4, true);
    const primitiveCount = view.getUint32(8, true);
    const stride = view.getUint32(12, true);
    const packetFvf = view.getUint32(16, true);
    const vertexBytes = view.getUint32(20, true);
    const vertexCount = primitiveVertexCount(primitiveType, primitiveCount);
    if (!vertexCount || vertexBytes !== vertexCount * stride || bytes.length !== 24 + vertexBytes) {
      throw new Error(`bad DrawPrimitiveUP packet type=${primitiveType} count=${primitiveCount} stride=${stride} bytes=${vertexBytes}`);
    }
    const layout = fvfLayout(packetFvf, stride);
    recordQuestTitleDraw('up', primitiveType, primitiveCount, stride,
      packetFvf, bytes.subarray(24));
    const liveDrawCount = runtimeDiagnosticsEnabled ? (++dpupLiveDrawCount) >>> 0 : 0;
    if (runtimeDiagnosticsEnabled && (liveDrawCount & 0xff) === 0) {
      canvas.dataset.d3d9webDpupCount = String(liveDrawCount);
    }
    if (HOT_TRACE) {
      canvas.dataset.d3d9webDpupLast = `${packetFvf}:${primitiveType}:${primitiveCount}:${stride}:${boundTextures[0]}:${boundTextures[1]}`;
    }
    // The inventory is startup diagnostics, not a permanent per-draw task.
    // Freeze it after a bounded warm-up even when this game exposes fewer
    // than the historical 96 unique draw shapes.
    if (runtimeDiagnosticsEnabled && liveDrawCount <= 8192 && dpupLiveRecords.length < 96) {
      const liveKey = `${packetFvf}:${primitiveType}:${primitiveCount}:${stride}:${boundTextures[0]}:${boundTextures[1]}`;
      if (!dpupLiveKeys.has(liveKey)) {
        dpupLiveKeys.add(liveKey);
        const fnv1aLive = (data) => {
          let hash = 0x811c9dc5;
          for (let i = 0; i < data.length; i++) {
            hash ^= data[i];
            hash = Math.imul(hash, 0x01000193) >>> 0;
          }
          return hash.toString(16).padStart(8, '0');
        };
        const liveTexture = (handle) => {
          const resource = resources.get(handle);
          if (!resource || resource.kind !== 1) return { handle, missing: true };
          return {
            handle,
            width: resource.width,
            height: resource.height,
            format: resource.format,
            levels: resource.levels,
            bytes: resource.raw.length,
            fnv: fnv1aLive(resource.raw),
          };
        };
        const liveFloatWords = (values) => Array.from(values, (value) =>
          new Uint32Array(new Float32Array([value]).buffer)[0] >>> 0);
        dpupLiveRecords.push({
          fvf: packetFvf >>> 0,
          primitiveType,
          primitiveCount,
          stride,
          vertexFnv: fnv1aLive(bytes.subarray(24)),
          textures: [liveTexture(boundTextures[0]), liveTexture(boundTextures[1])],
          view: liveFloatWords(transformSlots.get(2) || identityRowMajor()),
          projection: liveFloatWords(transformSlots.get(3) || identityRowMajor()),
          viewport: { ...viewport },
          texcoordIndex: [rawTextureStageStates[0][11], rawTextureStageStates[1][11]],
          transformFlags: [rawTextureStageStates[0][24], rawTextureStageStates[1][24]],
        });
        canvas.dataset.d3d9webDpupInventory = JSON.stringify(dpupLiveRecords);
        publishProbe({ dpupCount: liveDrawCount, dpupInventory: dpupLiveRecords });
      }
    }
    const dpupCarpetCandidate = runtimeDiagnosticsEnabled &&
      packetFvf === 0x00100312 && primitiveType === 6 &&
      stride === 52 && (primitiveCount === 2 || primitiveCount === 3 || primitiveCount === 5);
    if (dpupCarpetCandidate && !canvas.dataset.d3d9webCarpet) {
      const stage0Resource = resources.get(boundTextures[0]);
      const stage1Resource = resources.get(boundTextures[1]);
      if (stage0Resource?.kind === 1 && stage0Resource.width === 256 && stage0Resource.height === 256 &&
          stage0Resource.format === 22 && stage1Resource?.kind === 1 &&
          stage1Resource.width === 512 && stage1Resource.height === 512 &&
          stage1Resource.format === 23 && stage1Resource.levels === 4) {
        const fnv1a = (data) => {
          let hash = 0x811c9dc5;
          for (let i = 0; i < data.length; i++) {
            hash ^= data[i];
            hash = Math.imul(hash, 0x01000193) >>> 0;
          }
          return hash.toString(16).padStart(8, '0');
        };
        const textureSummary = (resource) => ({
          handle: resource.handle,
          width: resource.width,
          height: resource.height,
          format: resource.format,
          levels: resource.levels,
          bytes: resource.raw.length,
          fnv: fnv1a(resource.raw),
          mips: resource.mips.map((mip) => ({
            level: mip.level,
            firstRow: fnv1a(resource.raw.subarray(mip.offset, mip.offset + mip.pitch)),
            lastRow: fnv1a(resource.raw.subarray(
              mip.offset + (mip.height - 1) * mip.pitch,
              mip.offset + mip.height * mip.pitch)),
          })),
        });
        const floatWords = (values) => Array.from(values, (value) =>
          new Uint32Array(new Float32Array([value]).buffer)[0] >>> 0);
        canvas.dataset.d3d9webCarpet = JSON.stringify({
          primitiveCount,
          stride,
          fvf: packetFvf >>> 0,
          vertexFnv: fnv1a(bytes.subarray(24)),
          textures: [textureSummary(stage0Resource), textureSummary(stage1Resource)],
          world: floatWords(transformSlots.get(0) || identityRowMajor()),
          view: floatWords(transformSlots.get(2) || identityRowMajor()),
          projection: floatWords(transformSlots.get(3) || identityRowMajor()),
          viewport: { ...viewport },
          texcoordIndex: [rawTextureStageStates[0][11], rawTextureStageStates[1][11]],
          transformFlags: [rawTextureStageStates[0][24], rawTextureStageStates[1][24]],
          address: [
            [rawSamplerStates[0][1], rawSamplerStates[0][2]],
            [rawSamplerStates[1][1], rawSamplerStates[1][2]],
          ],
        });
      }
    }
    if (runtimeDiagnosticsEnabled && !dpupCaptureDone && !dpupCaptureActive && !dpupCapturePending && dpupCarpetCandidate) {
      // Arm the next complete frame from the measured carpet draw itself.
      // The former resources.size > 100 gate is not stable across lobby entry modes.
      dpupCapturePending = true;
    }
    if (runtimeDiagnosticsEnabled && dpupCaptureActive) {
      dpupCaptureOrdinal++;
      if (dpupCarpetCandidate) {
        if (dpupCaptureRecords.length < 256) {
          const rawVertices = bytes.subarray(24);
          let hash = 0x811c9dc5;
          for (let i = 0; i < rawVertices.length; i++) {
            hash ^= rawVertices[i];
            hash = Math.imul(hash, 0x01000193) >>> 0;
          }
          const worldFloats = transformSlots.get(256) || identityRowMajor();
          const worldWords = Array.from(new Uint32Array(new Float32Array(worldFloats).buffer));
          const viewFloats = transformSlots.get(2) || identityRowMajor();
          const projectionFloats = transformSlots.get(3) || identityRowMajor();
          const floatWord = (value) => new Uint32Array(new Float32Array([value]).buffer)[0] >>> 0;
          const renderStateIds = [7, 14, 15, 19, 20, 22, 23, 24, 25, 27, 168, 174];
          const renderStateDefaults = [1, 1, 0, 2, 1, 3, 4, 0, 8, 0, 15, 0];
          dpupCaptureRecords.push({
            ordinal: dpupCaptureOrdinal,
            primitiveType,
            primitiveCount,
            stride,
            fvf: packetFvf >>> 0,
            vertexBytes,
            fnv1a: hash >>> 0,
            textures: [boundTextures[0] >>> 0, boundTextures[1] >>> 0],
            world: worldWords,
            view: Array.from(new Uint32Array(new Float32Array(viewFloats).buffer)),
            projection: Array.from(new Uint32Array(new Float32Array(projectionFloats).buffer)),
            viewport: {
              ...viewport,
              minZWord: floatWord(viewport.minZ),
              maxZWord: floatWord(viewport.maxZ),
            },
            tss: rawTextureStageStates.map((values) => values.slice()),
            sampler: rawSamplerStates.map((values) => values.slice()),
            render: renderStateIds.map((id, index) => renderStates.get(id) ?? renderStateDefaults[index]),
            bindingOk: [],
            vertices: Array.from(rawVertices),
          });
        } else {
          dpupCaptureDropped++;
        }
      }
    }
    let batchEntry = null;
    if (activeDrawUpBatch) {
      batchEntry = activeDrawUpBatch.entries[activeDrawUpBatch.index++];
      if (!batchEntry || batchEntry.stride !== stride || batchEntry.vertexBytes !== vertexBytes) {
        throw new Error('DrawPrimitiveUP batch prepass order mismatch');
      }
    }
    const upload = batchEntry ? null : swizzleVertexColors(bytes.subarray(24), stride, layout);
    const { gl, program, vao, upBuffer } = ensureRenderer(viewport.width, viewport.height);
    applyViewport();
    bindTexturesForDraw(layout.positionT);
    if (runtimeDiagnosticsEnabled && dpupCaptureActive && dpupCaptureRecords.length) {
      const captured = dpupCaptureRecords[dpupCaptureRecords.length - 1];
      if (captured.ordinal === dpupCaptureOrdinal && captured.ordinal >= 22 && captured.ordinal <= 29) {
        captured.bindingOk = [0, 1].map((stage) => {
          gl.activeTexture(gl.TEXTURE0 + stage * 2);
          const resource = resources.get(boundTextures[stage]);
          const expected = resource ? resource.glTexture : renderer.dummyTextures.dummy2D;
          return gl.getParameter(gl.TEXTURE_BINDING_2D) === expected ? 1 : 0;
        });
      }
    }
    if (runtimeDiagnosticsEnabled) recordDrawSamplerProfile('up');

    applyVertexLayoutState(layout);
    recomputeTransformsIfDirty();
    recordFramePlanUpDraw(primitiveType, packetFvf, stride);
    if (ffpStateDirty || blendMatricesDirty || clipPlanesDirty) {
      uploadState(renderer, state, blendMatricesDirty, clipPlanesDirty);
      ffpStateDirty = false;
      blendMatricesDirty = false;
      clipPlanesDirty = false;
    }
    gl.useProgram(program);
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, upBuffer);
    let firstVertex = 0;
    if (batchEntry) {
      if (!activeDrawUpBatch.uploaded) {
        gl.bufferData(gl.ARRAY_BUFFER, activeDrawUpBatch.arena, gl.DYNAMIC_DRAW);
        activeDrawUpBatch.uploaded = true;
        drawUpBatchProfile.uploads += 1;
      }
      firstVertex = batchEntry.arenaOffset / stride;
    } else if (drawUpRingEnabled) {
      // Keep attribute pointers at byte zero and express the ring offset as
      // drawArrays(first).  Align each allocation to the current vertex stride
      // so firstVertex remains integral even when adjacent draws use different
      // FVF layouts/strides.
      let uploadOffset = Math.ceil(renderer.upBufferOffset / stride) * stride;
      const requiredBytes = uploadOffset + upload.byteLength;
      if (requiredBytes > renderer.upBufferCapacity) {
        let nextCapacity = renderer.upBufferCapacity;
        while (nextCapacity < requiredBytes) nextCapacity *= 2;
        if (nextCapacity !== renderer.upBufferCapacity) drawUpRingProfile.grows += 1;
        renderer.upBufferCapacity = nextCapacity;
        renderer.upBufferOffset = 0;
        renderer.upBufferNeedsOrphan = true;
        uploadOffset = 0;
      }
      if (renderer.upBufferNeedsOrphan) {
        gl.bufferData(gl.ARRAY_BUFFER, renderer.upBufferCapacity, gl.DYNAMIC_DRAW);
        renderer.upBufferNeedsOrphan = false;
        drawUpRingProfile.orphans += 1;
      }
      gl.bufferSubData(gl.ARRAY_BUFFER, uploadOffset, upload);
      renderer.upBufferOffset = uploadOffset + upload.byteLength;
      firstVertex = uploadOffset / stride;
      drawUpRingProfile.calls += 1;
      drawUpRingProfile.bytes += upload.byteLength;
      drawUpRingProfile.maxOffset = Math.max(drawUpRingProfile.maxOffset, renderer.upBufferOffset);
    } else {
      gl.bufferData(gl.ARRAY_BUFFER, upload, gl.DYNAMIC_DRAW); // orphan per UP draw
    }
    configureVertexInput(gl, 'up', upBuffer, layout, stride);
    gl.drawArrays(primitiveMode(gl, primitiveType), firstVertex, vertexCount);
    frameHasDraw = true;
    if (runtimeDiagnosticsEnabled && carpetTraceActive) {
      traceCarpetPixelChanges(`up:p${primitiveType}/c${primitiveCount}/fvf0x${packetFvf.toString(16)}`,
        layout, stride, bytes.subarray(24));
    }
    checkGlDrawError(gl, 'draw');
    if (HOT_TRACE) drawCalls++;
    if (runtimeDiagnosticsEnabled) frameDrawCalls++;
    if (HOT_TRACE) {
      const vertexView = new DataView(bytes.buffer, bytes.byteOffset + 24, vertexBytes);
      const color0 = layout.attributes.find((a) => a.location === 12);
      const texcoord0 = layout.attributes.find((a) => a.location === 4);
      const packedColor0 = color0 ? vertexView.getUint32(color0.offset, true) : 0;
      const u0 = texcoord0 ? vertexView.getFloat32(texcoord0.offset, true) : 0;
      const v0 = texcoord0 ? vertexView.getFloat32(texcoord0.offset + 4, true) : 0;
      const stage0 = state.textureStages[0];
      traceHot(`D3D9WEB_PHASE6_DRAW up=1 fvf=0x${packetFvf.toString(16).toUpperCase()} primitive=${primitiveType} vertices=${vertexCount} stride=${stride} texture=${boundTextures[0] || 0} colorOp=${stage0.colorOp} colorArgs=${stage0.colorArgs[1].selector}/${stage0.colorArgs[2].selector} alphaOp=${stage0.alphaOp} alphaArgs=${stage0.alphaArgs[1].selector}/${stage0.alphaArgs[2].selector} texcoord0=${state.dataPrimitives.texcoordDeclCounts[0]} vertexColor=0x${packedColor0.toString(16).toUpperCase()} uv=${u0.toFixed(3)},${v0.toFixed(3)} bgraSwizzle=1`);
    }
  }

  function bindTexturesForDraw(positionT = false) {
    const { gl, dummyTextures } = renderer;
    const retiredTexture = positionT || !stage1MaterialMipFallbackEnabled ? null :
      ensureStage1MaterialMips(resources.get(boundTextures[1]));
    for (let stage = 0; stage < 8; stage++) {
      const handle = boundTextures[stage];
      const resource = resources.get(handle);
      if (gpuTextureHandles[stage] !== handle) {
        gl.activeTexture(gl.TEXTURE0 + stage * 2);
        gl.bindTexture(gl.TEXTURE_2D, resource ? resource.glTexture : dummyTextures.dummy2D);
        gpuTextureHandles[stage] = handle;
        samplerDirty[stage] = true;
      }
      if (samplerDirty[stage]) {
        applySampler(stage);
        samplerDirty[stage] = false;
      }
    }
    if (retiredTexture) gl.deleteTexture(retiredTexture);
  }

  function recordDrawSamplerProfile(drawKind) {
    if (!runtimeDiagnosticsEnabled) return;
    if ((++drawSamplerProfileSampleCounter & 0x3f) !== 0) return;
    const sampler = samplerStates[0];
    const textureFlags = state.dataPrimitives.texcoordTransformFlags[0];
    const key = `${drawKind}:tex=${boundTextures[0] || 0}:uv=${sampler.addressU}/${sampler.addressV}:tt=${textureFlags}`;
    drawSamplerProfile.set(key, (drawSamplerProfile.get(key) || 0) + 64);
  }

  function applyVertexLayoutState(layout) {
    if (!layout.dataPrimitiveSignature) {
      const texcoordDeclCounts = Array(8).fill(0);
      let hasColor0 = false;
      let hasColor1 = false;
      let color0NeedsBgra = false;
      let color1NeedsBgra = false;
      for (const attr of layout.attributes) {
        if (attr.location === 12) {
          hasColor0 = true;
          color0NeedsBgra = attr.type === 'color';
        } else if (attr.location === 13) {
          hasColor1 = true;
          color1NeedsBgra = attr.type === 'color';
        }
        else if (attr.location >= 4 && attr.location < 12) {
          texcoordDeclCounts[attr.location - 4] = attr.size;
        }
      }
      layout.dataPrimitiveSignature =
        `${layout.positionT ? 1 : 0}:${hasColor0 ? 1 : 0}:${hasColor1 ? 1 : 0}:` +
        `${color0NeedsBgra ? 1 : 0}:${color1NeedsBgra ? 1 : 0}:${texcoordDeclCounts.join(',')}`;
      layout.dataPrimitiveState = {
        hasColor0, hasColor1, color0NeedsBgra, color1NeedsBgra, texcoordDeclCounts,
      };
    }
    if (appliedVertexLayoutSignature === layout.dataPrimitiveSignature) return;
    appliedVertexLayoutSignature = layout.dataPrimitiveSignature;
    state.dataPrimitives.vertexHasPositionT = layout.positionT;
    state.dataPrimitives.vertexHasColor0 = layout.dataPrimitiveState.hasColor0;
    state.dataPrimitives.vertexHasColor1 = layout.dataPrimitiveState.hasColor1;
    state.dataPrimitives.vertexColor0NeedsBgra =
      vertexColorShaderSwizzleEnabled && layout.dataPrimitiveState.color0NeedsBgra;
    state.dataPrimitives.vertexColor1NeedsBgra =
      vertexColorShaderSwizzleEnabled && layout.dataPrimitiveState.color1NeedsBgra;
    for (let i = 0; i < 8; i++) {
      state.dataPrimitives.texcoordDeclCounts[i] = layout.dataPrimitiveState.texcoordDeclCounts[i];
    }
    ffpStateDirty = true;
  }

  function configureVertexInput(gl, sourceKey, buffer, layout, stride) {
    if (!layout.vertexInputSignature) {
      layout.vertexInputSignature = `${stride}:${layout.attributes.map((attr) =>
        `${attr.location}/${attr.size}/${attr.type}/${attr.normalized ? 1 : 0}/${attr.offset}`).join(',')}`;
    }
    const signature = `${sourceKey}:${layout.vertexInputSignature}`;
    if (signature === vertexInputSignature) return;
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    let nextMask = 0;
    for (const attr of layout.attributes) nextMask |= 1 << attr.location;
    for (let location = 0; location < 16; location++) {
      const bit = 1 << location;
      if ((enabledVertexAttributeMask & bit) && !(nextMask & bit)) gl.disableVertexAttribArray(location);
    }
    for (const attr of layout.attributes) {
      const bit = 1 << attr.location;
      if (!(enabledVertexAttributeMask & bit)) gl.enableVertexAttribArray(attr.location);
      gl.vertexAttribPointer(attr.location, attr.size, attributeGlType(gl, attr.type),
        attr.normalized ?? (attr.type === 'color'), stride, attr.offset);
    }
    enabledVertexAttributeMask = nextMask;
    vertexInputSignature = signature;
  }

  function drawIndexed(view, bytes) {
    if (bytes.byteLength !== 28) throw new Error(`bad DrawIndexed size ${bytes.byteLength}`);
    const primitiveType = view.getUint32(4, true);
    const baseVertexIndex = view.getInt32(8, true);
    const minVertexIndex = view.getUint32(12, true);
    const numVertices = view.getUint32(16, true);
    const startIndex = view.getUint32(20, true);
    const primitiveCount = view.getUint32(24, true);
    const indexCount = primitiveVertexCount(primitiveType, primitiveCount);
    const vb = resources.get(stream0.handle);
    const ib = resources.get(indexHandle);
    if (!indexCount || !vb || vb.kind !== 2 || !ib || ib.kind !== 3 || !stream0.stride) {
      throw new Error('DrawIndexed missing valid stream/index binding');
    }
    recordFramePlanIndexedDraw();
    const indexSize = ib.formatOrFvf === 101 ? 2 : ib.formatOrFvf === 102 ? 4 : 0;
    if (!indexSize || startIndex * indexSize + indexCount * indexSize > ib.raw.byteLength) {
      throw new Error(`DrawIndexed bad index range start=${startIndex} count=${indexCount}`);
    }
    const declaration = resources.get(declarationHandle);
    const layout = declaration ?
      declarationLayout(declaration.elements, stream0.stride) :
      fvfLayout(fvf, stream0.stride);
    const indexedDrawCount = runtimeDiagnosticsEnabled
      ? (Number(canvas.dataset.d3d9webIndexedCount || '0') + 1) >>> 0 : 0;
    if (runtimeDiagnosticsEnabled) canvas.dataset.d3d9webIndexedCount = String(indexedDrawCount);
    if (runtimeDiagnosticsEnabled && indexedLiveRecords.length < 64) {
      const indexedLiveKey = `${fvf}:${primitiveType}:${primitiveCount}:${stream0.stride}:${boundTextures[0]}:${boundTextures[1]}`;
      if (!indexedLiveKeys.has(indexedLiveKey)) {
        indexedLiveKeys.add(indexedLiveKey);
      const fnv1aIndexed = (data) => {
        let hash = 0x811c9dc5;
        for (let i = 0; i < data.length; i++) {
          hash ^= data[i];
          hash = Math.imul(hash, 0x01000193) >>> 0;
        }
        return hash.toString(16).padStart(8, '0');
      };
      const indexedTexture = (handle) => {
        const resource = resources.get(handle);
        if (!resource || resource.kind !== 1) return { handle, missing: true };
        return {
          handle,
          width: resource.width,
          height: resource.height,
          format: resource.format,
          levels: resource.levels,
          bytes: resource.raw.length,
          fnv: fnv1aIndexed(resource.raw),
        };
      };
      const indexedFloatWords = (values) => Array.from(values, (value) =>
        new Uint32Array(new Float32Array([value]).buffer)[0] >>> 0);
      indexedLiveRecords.push({
        fvf: fvf >>> 0,
        declarationHandle,
        primitiveType,
        primitiveCount,
        baseVertexIndex,
        minVertexIndex,
        numVertices,
        startIndex,
        stride: stream0.stride,
        vb: { handle: vb.handle, bytes: vb.raw.length, version: vb.uploadVersion, fnv: fnv1aIndexed(vb.raw) },
        ib: { handle: ib.handle, bytes: ib.raw.length, version: ib.uploadVersion, fnv: fnv1aIndexed(ib.raw) },
        textures: [indexedTexture(boundTextures[0]), indexedTexture(boundTextures[1])],
        view: indexedFloatWords(transformSlots.get(2) || identityRowMajor()),
        projection: indexedFloatWords(transformSlots.get(3) || identityRowMajor()),
        viewport: { ...viewport },
        texcoordIndex: [rawTextureStageStates[0][11], rawTextureStageStates[1][11]],
        transformFlags: [rawTextureStageStates[0][24], rawTextureStageStates[1][24]],
        texcoordDeclCounts: layout.attributes
          .filter((attribute) => attribute.location >= 4 && attribute.location < 12)
          .map((attribute) => [attribute.location - 4, attribute.size]),
      });
        canvas.dataset.d3d9webIndexedInventory = JSON.stringify(indexedLiveRecords);
        publishProbe({ indexedCount: indexedDrawCount, indexedInventory: indexedLiveRecords });
      }
    }
    const vertexBytes = vb.raw.subarray(stream0.offset);
    const vertexCount = Math.floor(vertexBytes.byteLength / stream0.stride);
    if (!numVertices || minVertexIndex + numVertices > vertexCount) {
      throw new Error(`DrawIndexed bad vertex range min=${minVertexIndex} count=${numVertices} available=${vertexCount}`);
    }
    // The quest-list row does not appear in DrawPrimitiveUP.  When the
    // explicitly armed trace is running, rebuild this indexed primitive in
    // draw order solely for the same screen/UV inspection used by UP draws.
    // This has no effect on the GPU upload or index buffer below.
    if (questTitleTraceEnabled && canvas.dataset.d3d9webQuestTitleTraceArm === '1') {
      const indexView = new DataView(ib.raw.buffer, ib.raw.byteOffset, ib.raw.byteLength);
      const orderedVertices = new Uint8Array(indexCount * stream0.stride);
      for (let index = 0; index < indexCount; index++) {
        const offset = (startIndex + index) * indexSize;
        const original = indexSize === 2 ? indexView.getUint16(offset, true) : indexView.getUint32(offset, true);
        const adjusted = original + baseVertexIndex;
        if (adjusted < 0 || adjusted >= vertexCount) continue;
        orderedVertices.set(vertexBytes.subarray(adjusted * stream0.stride,
          (adjusted + 1) * stream0.stride), index * stream0.stride);
      }
      recordQuestTitleDraw('indexed', primitiveType, primitiveCount, stream0.stride,
        fvf, orderedVertices, layout);
    }
    const layoutKey = declaration ? `d${declarationHandle}` : `f${fvf}`;
    const vertexKey = `${vb.handle}:${vb.uploadVersion}:${stream0.offset}:${stream0.stride}:${layoutKey}`;
    const indexKey = `${ib.handle}:${ib.uploadVersion}:${startIndex}:${indexCount}:${baseVertexIndex}:${minVertexIndex}:${numVertices}:${indexSize}`;
    if (runtimeDiagnosticsEnabled) {
      const resourceProfileKey =
        `vb=${vb.handle}/u${vb.usage.toString(16)}/v${vb.uploadVersion}/b${vb.totalBytes}` +
        `:ib=${ib.handle}/u${ib.usage.toString(16)}/v${ib.uploadVersion}/b${ib.totalBytes}`;
      indexedDrawResourceProfile.set(resourceProfileKey,
        (indexedDrawResourceProfile.get(resourceProfileKey) || 0) + 1);
    }
    const { gl, program, vao, indexedVertexBuffer, indexedIndexBuffer,
      indexedVertexCache, indexedIndexCache } =
      ensureRenderer(viewport.width, viewport.height);
    applyViewport();
    bindTexturesForDraw(layout.positionT);
    if (runtimeDiagnosticsEnabled) recordDrawSamplerProfile('idx');
    applyVertexLayoutState(layout);
    recomputeTransformsIfDirty();
    if (ffpStateDirty || blendMatricesDirty || clipPlanesDirty) {
      uploadState(renderer, state, blendMatricesDirty, clipPlanesDirty);
      ffpStateDirty = false;
      blendMatricesDirty = false;
      clipPlanesDirty = false;
    }
    gl.useProgram(program);
    gl.bindVertexArray(vao);
    // Kept disabled in the live path until every static resource's lifetime
    // and vertex-array rebinding have a full scene regression test. The
    // scratch buffers below are the known-good behavior.
    const staticVertex = false;
    let vertexBuffer = indexedVertexBuffer;
    if (staticVertex && indexedVertexCache.has(vertexKey)) {
      vertexBuffer = indexedVertexCache.get(vertexKey);
      if (runtimeDiagnosticsEnabled) indexedVertexCacheHits += 1;
    } else if (staticVertex) {
      vertexBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
      const upload = swizzleVertexColors(vertexBytes, stream0.stride, layout);
      gl.bufferData(gl.ARRAY_BUFFER, upload, gl.STATIC_DRAW);
      indexedVertexCache.set(vertexKey, vertexBuffer);
      if (runtimeDiagnosticsEnabled) indexedVertexCacheMisses += 1;
    } else {
      gl.bindBuffer(gl.ARRAY_BUFFER, indexedVertexBuffer);
      if (vertexKey !== indexedVertexUploadKey) {
        if (runtimeDiagnosticsEnabled) indexedVertexCacheMisses += 1;
        const upload = swizzleVertexColors(vertexBytes, stream0.stride, layout);
        gl.bufferData(gl.ARRAY_BUFFER, upload, gl.DYNAMIC_DRAW);
        indexedVertexUploadKey = vertexKey;
      } else {
        if (runtimeDiagnosticsEnabled) indexedVertexCacheHits += 1;
      }
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);

    const staticIndex = false;
    let indexBuffer = indexedIndexBuffer;
    if (staticIndex && indexedIndexCache.has(indexKey)) {
      indexBuffer = indexedIndexCache.get(indexKey);
      if (runtimeDiagnosticsEnabled) indexedIndexCacheHits += 1;
    } else if (staticIndex) {
      indexBuffer = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
      const indexView = new DataView(ib.raw.buffer, ib.raw.byteOffset, ib.raw.byteLength);
      const rebased = indexSize === 2 ? new Uint16Array(indexCount) : new Uint32Array(indexCount);
      for (let i = 0; i < indexCount; i++) {
        const byteOffset = (startIndex + i) * indexSize;
        const original = indexSize === 2 ? indexView.getUint16(byteOffset, true) : indexView.getUint32(byteOffset, true);
        const adjusted = original + baseVertexIndex;
        if (adjusted < 0 || adjusted >= vertexCount || adjusted < minVertexIndex + baseVertexIndex ||
            adjusted >= minVertexIndex + baseVertexIndex + numVertices) {
          throw new Error(`DrawIndexed index ${original} base=${baseVertexIndex} outside declared range`);
        }
        rebased[i] = adjusted;
      }
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, rebased, gl.STATIC_DRAW);
      indexedIndexCache.set(indexKey, indexBuffer);
      if (runtimeDiagnosticsEnabled) indexedIndexCacheMisses += 1;
    } else {
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexedIndexBuffer);
      if (indexKey !== indexedIndexUploadKey) {
        if (runtimeDiagnosticsEnabled) indexedIndexCacheMisses += 1;
        const indexView = new DataView(ib.raw.buffer, ib.raw.byteOffset, ib.raw.byteLength);
        const rebased = indexSize === 2 ? new Uint16Array(indexCount) : new Uint32Array(indexCount);
        for (let i = 0; i < indexCount; i++) {
          const byteOffset = (startIndex + i) * indexSize;
          const original = indexSize === 2 ? indexView.getUint16(byteOffset, true) : indexView.getUint32(byteOffset, true);
          const adjusted = original + baseVertexIndex;
          if (adjusted < 0 || adjusted >= vertexCount || adjusted < minVertexIndex + baseVertexIndex ||
              adjusted >= minVertexIndex + baseVertexIndex + numVertices) {
            throw new Error(`DrawIndexed index ${original} base=${baseVertexIndex} outside declared range`);
          }
          rebased[i] = adjusted;
        }
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, rebased, gl.DYNAMIC_DRAW);
        indexedIndexUploadKey = indexKey;
      } else {
        if (runtimeDiagnosticsEnabled) indexedIndexCacheHits += 1;
      }
    }
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer);
    configureVertexInput(gl, 'indexed', vertexBuffer, layout, stream0.stride);
    gl.drawElements(primitiveMode(gl, primitiveType), indexCount,
      indexSize === 2 ? gl.UNSIGNED_SHORT : gl.UNSIGNED_INT, 0);
    frameHasDraw = true;
    if (runtimeDiagnosticsEnabled && carpetTraceActive) {
      traceCarpetPixelChanges(`idx:p${primitiveType}/c${primitiveCount}/vb${vb.handle}/ib${ib.handle}`,
        layout, stream0.stride, vertexBytes);
    }
    checkGlDrawError(gl, 'indexed draw');
    if (HOT_TRACE) drawCalls++;
    if (runtimeDiagnosticsEnabled) frameDrawCalls++;
    if (HOT_TRACE) {
      indexedDrawCalls++;
      traceHot(`D3D9WEB_PHASE7_DRAW indexed=1 layout=${declaration ? `decl:${declarationHandle}` : `fvf:0x${fvf.toString(16).toUpperCase()}`} primitive=${primitiveType} indices=${indexCount} stride=${stream0.stride} texture=${boundTextures[0] || 0} bgraSwizzle=1`);
    }
    if (runtimeDiagnosticsEnabled && canvas.width === 800 && canvas.height === 600 && primitiveType === 4 &&
        primitiveCount === 1 && boundTextures[0]) {
      const sample = (x, y) => {
        const pixel = new Uint8Array(4);
        gl.readPixels(x, canvas.height - y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        return Array.from(pixel);
      };
      const red = sample(350, 250);
      const green = sample(450, 250);
      const blue = sample(300, 420);
      const white = sample(500, 420);
      const pixelPass = red[0] > 240 && red[1] < 16 && red[2] < 16 &&
        green[0] < 16 && green[1] > 240 && green[2] < 16 &&
        blue[0] < 16 && blue[1] < 16 && blue[2] > 240 &&
        white[0] > 240 && white[1] > 240 && white[2] > 240;
      report(`D3D9WEB_PHASE7_PIXELS red=${red.join(',')} green=${green.join(',')} blue=${blue.join(',')} white=${white.join(',')} result=${pixelPass ? 'PASS' : 'FAIL'}`);
      if (!pixelPass) throw new Error('indexed texture pixel verification failed');
    }
  }

  return {
    samplePixels(points) {
      if (!renderer) throw new Error('renderer not initialized');
      const { gl } = renderer;
      return points.map(([x, y]) => {
        const pixel = new Uint8Array(4);
        gl.readPixels(x, canvas.height - y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        return { x, y, rgba: Array.from(pixel) };
      });
    },
    dispatchBatch(packet) {
      let gpuQuery = null;
      const hostBatchStart = hostProfileEnabled && typeof performance !== 'undefined' ? performance.now() : 0;
      try {
        const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);
        if (drawRunProbeEnabled) probeDrawRuns(bytes);
        activeDrawUpBatch = drawUpBatchEnabled ? prepareDrawUpBatch(bytes) : null;
        if (runtimeDiagnosticsEnabled && dispatchBatchProbeCount < 8) {
          dispatchBatchProbeCount += 1;
          const commandIds = [];
          let probeOffset = 0;
          while (probeOffset + 8 <= bytes.byteLength && commandIds.length < 32) {
            const probeView = new DataView(bytes.buffer, bytes.byteOffset + probeOffset, 8);
            const commandBytes = probeView.getUint32(0, true);
            if (commandBytes < 4 || probeOffset + 4 + commandBytes > bytes.byteLength) break;
            commandIds.push(probeView.getUint32(4, true));
            probeOffset += 4 + commandBytes;
          }
          canvas.dataset.d3d9webBatchProbe = JSON.stringify({
            count: dispatchBatchProbeCount,
            bytes: bytes.byteLength,
            commands: commandIds,
          });
          publishProbe({ batch: { count: dispatchBatchProbeCount, bytes: bytes.byteLength, commands: commandIds } });
        }
        gpuQuery = beginGpuBatch();
        let offset = 0;
        while (offset < bytes.byteLength) {
          if (bytes.byteLength - offset < 4) throw new Error('short batch length');
          const commandBytes = new DataView(bytes.buffer, bytes.byteOffset + offset, 4)
            .getUint32(0, true);
          offset += 4;
          if (commandBytes < 4 || commandBytes > bytes.byteLength - offset) {
            throw new Error(`invalid batch command size ${commandBytes}`);
          }
          const result = this.dispatch(bytes.subarray(offset, offset + commandBytes));
          if (result !== RESULT_OK) return result;
          offset += commandBytes;
        }
        return RESULT_OK;
      } catch (error) {
        report(`D3D9WEB_PHASE6_ERROR batch ${error && error.stack ? error.stack : error}`);
        return 0;
      } finally {
        activeDrawUpBatch = null;
        endGpuBatch(gpuQuery);
        if (hostBatchStart && typeof performance !== 'undefined') {
          const hostBatchMicros = (performance.now() - hostBatchStart) * 1000;
          hostBatchProfileCalls += 1;
          hostBatchProfileMicros += hostBatchMicros;
          hostBatchProfileMaxMicros = Math.max(hostBatchProfileMaxMicros, hostBatchMicros);
          if (hostBatchProfileCalls === 120) {
            report(`D3D9WEB_HOST_JS_PROFILE batches=120 total_us=${Math.round(hostBatchProfileMicros)} avg_us=${Math.round(hostBatchProfileMicros / 120)} max_us=${Math.round(hostBatchProfileMaxMicros)}`);
            const debug = globalThis.D3D9WebDebug;
            if (debug?.hostOpcodeSamples) {
              const opcodeProfile = Array.from(debug.hostOpcodeSamples, (samples, opcode) => ({
                opcode,
                samples,
                totalUs: debug.hostOpcodeTotalUs[opcode],
                maxUs: debug.hostOpcodeMaxUs[opcode],
              })).filter((entry) => entry.samples)
                .sort((left, right) => right.totalUs - left.totalUs)
                .map((entry) => `${entry.opcode}:n${entry.samples}/total${Math.round(entry.totalUs)}/avg${Math.round(entry.totalUs / entry.samples)}/max${Math.round(entry.maxUs)}`)
                .join(',');
              report(`D3D9WEB_HOST_OPCODE_PROFILE sampled_1_in_64 top=${opcodeProfile || 'none'}`);
              debug.hostOpcodeSamples.fill(0);
              debug.hostOpcodeTotalUs.fill(0);
              debug.hostOpcodeMaxUs.fill(0);
            }
            hostBatchProfileCalls = 0;
            hostBatchProfileMicros = 0;
            hostBatchProfileMaxMicros = 0;
          }
        }
      }
    },
    dispatch(packet) {
      try {
        const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);
        if (bytes.byteLength < 4) throw new Error('short command packet');
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const command = view.getUint32(0, true);
        if (questTitleTraceEnabled && canvas.dataset.d3d9webQuestTitleTraceArm === '1' &&
            command < questTitleCommandCounts.length) {
          questTitleCommandCounts[command] += 1;
          if (command === COMMAND.PRESENT) {
            canvas.dataset.d3d9webQuestTitleCommandTrace = JSON.stringify(
              Array.from(questTitleCommandCounts));
          }
        }
        if (runtimeDiagnosticsEnabled && dispatchProbeCommands.length < 64) {
          dispatchProbeCommands.push(command);
          canvas.dataset.d3d9webDispatchProbe = JSON.stringify(dispatchProbeCommands);
          publishProbe({ directCommands: dispatchProbeCommands.slice() });
        }
        // Lean mode still records PRESENT only for the low-rate FPS HUD. All
        // other command counters remain disabled exactly as before.
        const hostDispatchStart = runtimeDiagnosticsEnabled || hostProfileEnabled ||
          command === COMMAND.PRESENT ? recordDispatch(command, bytes.byteLength) : 0;
        try {
          switch (command) {
          case COMMAND.INIT: {
            framePlanBarrier();
            const width = view.getUint32(4, true);
            const height = view.getUint32(8, true);
            viewport = { ...viewport, width, height };
            scissor = { left: 0, top: 0, right: width, bottom: height };
            // A D3D9 Reset begins a new device-state lifetime.  Keeping the
            // previous scene's host-side shadow map can restore a stale
            // COLORWRITE mask after Clear, leaving SelectEgg pixels behind
            // while the PatHouse HUD continues to render over them.
            renderStates.clear();
            const current = ensureRenderer(width, height);
            current.gl.colorMask(true, true, true, true);
            report('D3D9WEB_INIT_STATE_RESET color_write=15 render_states=0');
            applyViewport();
            break;
          }
          case COMMAND.CLEAR: {
            framePlanBarrier();
            clearBackbuffer(view, bytes);
            break;
          }
          case COMMAND.SET_TRANSFORM: {
            const slot = view.getUint32(4, true);
            const matrix = Array.from({ length: 16 }, (_, i) => view.getFloat32(8 + i * 4, true));
            transformSlots.set(slot, matrix);
            markTransformDirty(slot);
            break;
          }
          case COMMAND.SET_VIEWPORT:
            framePlanBarrier();
            viewport = {
              x: view.getUint32(4, true), y: view.getUint32(8, true),
              width: view.getUint32(12, true), height: view.getUint32(16, true),
              minZ: view.getFloat32(20, true), maxZ: view.getFloat32(24, true),
            };
            if (HOT_TRACE) traceHot(`D3D9WEB_PHASE8_VIEWPORT_BEGIN ${viewport.width}x${viewport.height}`);
            ensureRenderer(viewport.width, viewport.height);
            applyViewport();
            traceHot('D3D9WEB_PHASE8_VIEWPORT_END');
            break;
          case COMMAND.SET_RENDER_STATE:
            ensureRenderer(viewport.width, viewport.height);
            applyRenderState(view.getUint32(4, true), view.getUint32(8, true));
            break;
          case COMMAND.SET_TEXTURE_STAGE_STATE:
            setTextureStageState(view.getUint32(4, true), view.getUint32(8, true), view.getUint32(12, true));
            break;
          case COMMAND.SET_SAMPLER_STATE:
            setSamplerState(view.getUint32(4, true), view.getUint32(8, true),
              view.getUint32(12, true));
            break;
          case COMMAND.SET_SCISSOR: {
            scissor = {
              left: view.getInt32(4, true), top: view.getInt32(8, true),
              right: view.getInt32(12, true), bottom: view.getInt32(16, true),
            };
            const current = ensureRenderer(viewport.width, viewport.height);
            const { gl } = current;
            const scaleX = current.backbufferWidth / Math.max(1, canvas.width);
            const scaleY = current.backbufferHeight / Math.max(1, canvas.height);
            const left = Math.floor(scissor.left * scaleX);
            const right = Math.ceil(scissor.right * scaleX);
            const top = Math.floor(scissor.top * scaleY);
            const bottom = Math.ceil(scissor.bottom * scaleY);
            gl.scissor(left, current.backbufferHeight - bottom,
              Math.max(1, right - left), Math.max(1, bottom - top));
            break;
          }
          case COMMAND.SET_FVF:
            fvf = view.getUint32(4, true);
            declarationHandle = 0;
            break;
          case COMMAND.DRAW_PRIMITIVE_UP:
            if (view.getUint32(16, true) !== fvf) throw new Error('draw FVF differs from shadow FVF');
            drawPrimitiveUP(view, bytes);
            break;
          case COMMAND.CREATE_TEXTURE: framePlanBarrier(); createTextureResource(view, bytes); break;
          case COMMAND.CREATE_BUFFER: framePlanBarrier(); createBufferResource(view, bytes); break;
          case COMMAND.UPLOAD_RESOURCE: framePlanBarrier(); uploadResource(view, bytes); break;
          case COMMAND.DESTROY_RESOURCE: framePlanBarrier(); destroyResource(view, bytes); break;
          case COMMAND.BIND_TEXTURE: bindTexture(view, bytes); break;
          case COMMAND.BIND_STREAM: bindStream(view, bytes); break;
          case COMMAND.BIND_INDICES: bindIndices(view, bytes); break;
          case COMMAND.DRAW_INDEXED: drawIndexed(view, bytes); break;
          case COMMAND.CREATE_DECLARATION: createDeclaration(view, bytes); break;
          case COMMAND.BIND_DECLARATION: bindDeclaration(view, bytes); break;
          case COMMAND.SET_MATERIAL: setMaterial(view, bytes); break;
          case COMMAND.SET_LIGHT: setLight(view, bytes); break;
          case COMMAND.LIGHT_ENABLE: lightEnable(view, bytes); break;
          case COMMAND.PRESENT:
            if (runtimeDiagnosticsEnabled) captureGuestProfile(view, bytes);
            recordStateChurnPresent();
            recordFramePlanPresent();
            const presentedDraw = frameHasDraw;
            presentBackbuffer();
            if (presentedDraw) publishFirstPresent(globalThis.D3D9WebDebug);
            if (HOT_TRACE) {
              traceHot(`D3D9WEB_PHASE6_PRESENT draws=${drawCalls} fvf=0x${fvf.toString(16).toUpperCase()}`);
              if (indexedDrawCalls) {
                traceHot(`D3D9WEB_PHASE7_PRESENT draws=${drawCalls} indexed=${indexedDrawCalls} resources=${resources.size}`);
              }
              if (materialCommands || lightCommands || lightEnableCommands) {
                recomputeTransformsIfDirty();
                traceHot(`D3D9WEB_PHASE9_STATE material=${materialCommands} light=${lightCommands} enable=${lightEnableCommands} active=${state.dataPrimitives.lightCount}`);
              }
            }
            break;
            default: throw new Error(`unknown command ${command}`);
          }
        } finally {
          recordHostDispatchDuration(hostDispatchStart, command);
        }
        return RESULT_OK;
      } catch (error) {
        report(`D3D9WEB_PHASE6_ERROR ${error && error.stack ? error.stack : error}`);
        return 0;
      }
    },
  };
}
