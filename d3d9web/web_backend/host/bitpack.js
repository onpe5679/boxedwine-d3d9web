// bitpack.js
//
// Pure (no WebGL dependency) bit-packing helpers for the four shared
// packed-uint fields and the per-stage packed fields documented in
// ../shaders/UNIFORMS.md sections 3 and 4. Every bit offset/width here is
// copied directly from that document (which was itself derived from the
// shader's own bitfieldExtract() call sites) -- see UNIFORMS.md for the
// authoritative field-by-field bit layout this file implements.
//
// These functions only compute plain JS numbers (treated as uint32); they
// do not know about WebGL, uniform blocks, or byte offsets. See pack-ffp.js
// for the code that writes these values into the actual std140 buffers at
// runtime-queried offsets.

'use strict';

function u32(x) {
  return x >>> 0;
}

// D3DCOLOR is packed 0xAARRGGBB (A most-significant byte, B
// least-significant byte) -- see UNIFORMS.md and decodeD3DColor() in both
// shaders. Each component is an 8-bit integer 0-255.
export function packD3DColor({ a = 255, r = 255, g = 255, b = 255 } = {}) {
  return u32(((a & 0xff) << 24) | ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}

// UNIFORMS.md section 3: `alphaTestAndModeArgs` / `alphaTestAndModeArgsFS`.
export function packAlphaTestAndModeArgs({
  compareOp = 7, // VK_COMPARE_OP_ALWAYS
  precisionBits = 8,
  pointSpriteEnable = 0,
  clipPlaneCount = 0,
  drefScaleShift = 0,
} = {}) {
  return u32(
    (compareOp & 0x7) |
      ((precisionBits & 0xf) << 4) |
      ((pointSpriteEnable ? 1 : 0) << 8) |
      ((clipPlaneCount & 0x7) << 16) |
      ((drefScaleShift & 0xff) << 24)
  );
}

// UNIFORMS.md section 3: `fogArgs` / `fogArgsFS`.
export function packFogArgs({
  fogEnable = 0,
  vertexFogMode = 0,
  pixelFogMode = 0,
  useZ = 0,
} = {}) {
  return u32(
    (fogEnable ? 1 : 0) |
      ((vertexFogMode & 0x3) << 8) |
      ((pixelFogMode & 0x3) << 16) |
      ((useZ ? 1 : 0) << 24)
  );
}

// UNIFORMS.md section 3: `packedProjMaskAndFfArgs` / `...FS`.
export function packProjMaskAndFfArgs({
  projectedMask = 0, // 8 bits, bit i = stage i D3DTTFF_PROJECTED
  specularEnable = 0,
  pointScaleEnable = 0,
  boundPsIsSM3 = 0,
} = {}) {
  return u32(
    (projectedMask & 0xff) |
      ((specularEnable ? 1 : 0) << 8) |
      ((pointScaleEnable ? 1 : 0) << 16) |
      ((boundPsIsSM3 ? 1 : 0) << 24)
  );
}

// UNIFORMS.md section 3: `psSamplerTypes` / `psSamplerModes`. `perStage`
// must have exactly 8 entries (stage 0..7), each a 2-bit value (0-3).
export function packPerStage2Bit(perStage) {
  if (!perStage || perStage.length !== 8) {
    throw new Error('packPerStage2Bit: expected an array of exactly 8 values (stages 0..7)');
  }
  let v = 0;
  for (let i = 0; i < 8; i++) {
    v |= (perStage[i] & 0x3) << (i * 2);
  }
  return u32(v);
}

// UNIFORMS.md section 3: one texture-stage argument's 5-bit field
// (`[0:3) selector, [3] complement, [4] alpha-replicate`).
export function packStageArg({ selector = 0, complement = false, alphaReplicate = false } = {}) {
  return (selector & 0x7) | ((complement ? 1 : 0) << 3) | ((alphaReplicate ? 1 : 0) << 4);
}

// UNIFORMS.md section 3: one texture stage's 16-bit op field
// (`[0:5) colorOp, [8:13) alphaOp, [15] storeToTemp`). Defaults to
// D3DTOP_DISABLE (1) for both ops, matching an inactive/disabled stage.
export function packStageOp16({ colorOp = 1, alphaOp = 1, storeToTemp = false } = {}) {
  return ((colorOp & 0x1f) | ((alphaOp & 0x1f) << 8) | ((storeToTemp ? 1 : 0) << 15)) & 0xffff;
}

// Combines two adjacent stages' 16-bit op fields into one
// `packedStageOps01`/`23`/`45`/`67` uint (low 16 bits = even stage, high 16
// bits = odd stage).
export function packStageOpsPair(evenOp16, oddOp16) {
  return u32((evenOp16 & 0xffff) | ((oddOp16 & 0xffff) << 16));
}

// Combines one stage's 6 already-packed 5-bit argument fields (from
// packStageArg()) into one `packedStageArgs0..7` uint
// (`[0:5) colorArg0, [5:10) colorArg1, [10:15) colorArg2,
//   [16:21) alphaArg0, [21:26) alphaArg1, [26:31) alphaArg2`).
export function packStageArgs32({ colorArg0, colorArg1, colorArg2, alphaArg0, alphaArg1, alphaArg2 }) {
  return u32(
    (colorArg0 & 0x1f) |
      ((colorArg1 & 0x1f) << 5) |
      ((colorArg2 & 0x1f) << 10) |
      ((alphaArg0 & 0x1f) << 16) |
      ((alphaArg1 & 0x1f) << 21) |
      ((alphaArg2 & 0x1f) << 26)
  );
}

// UNIFORMS.md section 1: `packedReservedAndPointSize` (bits [16:32) =
// default point size, fixed-point /8.0) and `packedPointSizeMinMax` (bits
// [0:16) = min, [16:32) = max, same /8.0 encoding).
export function encodeFixed8(value) {
  // Matches the shader's `/8.0` decode exactly in reverse: value * 8,
  // rounded to the nearest integer fixed-point unit.
  return Math.round(value * 8) & 0xffff;
}

export function packReservedAndPointSize(defaultPointSize) {
  return u32((encodeFixed8(defaultPointSize) & 0xffff) << 16);
}

export function packPointSizeMinMax(min, max) {
  return u32((encodeFixed8(min) & 0xffff) | ((encodeFixed8(max) & 0xffff) << 16));
}

// UNIFORMS.md section 4: `DataPrimitives[12]` -- 48 packed bytes, byte n
// read via `DataPrimitives[n/4] >> ((n%4)*8) & 0xFF`. Builds the 48-byte
// buffer from named fields, then callers read it back as 12 little-endian
// uint32s (one per DataPrimitives[i]) to write into the UBO.
export function packDataPrimitives(fields) {
  const bytes = new Uint8Array(48);
  const b = (i, v) => {
    bytes[i] = v & 0xff;
  };

  for (let i = 0; i < 8; i++) b(i, fields.texcoordIndex ? fields.texcoordIndex[i] : 0);
  for (let i = 0; i < 8; i++) b(8 + i, fields.texcoordFlags ? fields.texcoordFlags[i] : 0);
  for (let i = 0; i < 8; i++) b(16 + i, fields.texcoordTransformFlags ? fields.texcoordTransformFlags[i] : 0);

  // vertexTexcoordDeclMask: 8 x 3-bit fields packed into the 4 bytes
  // 24..27, field i = component count (0-4) for texcoord stream i.
  const declCounts = fields.texcoordDeclCounts || [0, 0, 0, 0, 0, 0, 0, 0];
  let declMask = 0;
  for (let i = 0; i < 8; i++) declMask |= (declCounts[i] & 0x7) << (i * 3);
  b(24, declMask & 0xff);
  b(25, (declMask >>> 8) & 0xff);
  b(26, (declMask >>> 16) & 0xff);
  b(27, (declMask >>> 24) & 0xff);

  b(28, fields.vertexHasPositionT ? 1 : 0);
  b(29, fields.vertexHasColor0 ? 1 : 0);
  b(30, fields.vertexHasColor1 ? 1 : 0);
  b(31, fields.vertexHasPointSize ? 1 : 0);
  b(32, fields.vertexHasFog ? 1 : 0);
  b(33, fields.blendMode || 0);
  b(34, fields.vertexBlendIndexed ? 1 : 0);
  b(35, fields.vertexBlendCount || 0);
  b(36, fields.vertexClipping ? 1 : 0);
  b(37, fields.normalizeNormals ? 1 : 0);
  b(38, fields.localViewer ? 1 : 0);
  b(39, fields.rangeFog ? 1 : 0);
  b(40, fields.useLighting ? 1 : 0);
  b(41, fields.lightCount || 0);
  b(42, fields.diffuseSource || 0);
  b(43, fields.ambientSource || 0);
  b(44, fields.specularSource || 0);
  b(45, fields.emissiveSource || 0);
  // Browser-only D3DCOLOR decode flags. A declaration may legally bind a
  // non-D3DCOLOR type to COLOR0/1, so the shader must not blindly swap every
  // attribute at locations 12/13.
  b(46, fields.vertexColor0NeedsBgra ? 1 : 0);
  b(47, fields.vertexColor1NeedsBgra ? 1 : 0);

  return bytes;
}

// Reads the 48-byte DataPrimitives buffer back out as 12 little-endian
// uint32 dwords (one per `DataPrimitives[i]` shader-side array element).
export function dataPrimitivesToDwords(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const dwords = new Array(12);
  for (let i = 0; i < 12; i++) dwords[i] = view.getUint32(i * 4, true);
  return dwords;
}
