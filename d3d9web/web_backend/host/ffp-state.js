// ffp-state.js
//
// Pure data structures for D3D9 fixed-function-pipeline state -- render
// state, the 8 texture stages, samplers, material, 8 lights, transform
// matrices -- matching ../shaders/UNIFORMS.md field-for-field. No WebGL,
// no byte offsets, no bit-packing here; see pack-ffp.js for the code that
// turns this shape into std140 UBO bytes.
//
// This intentionally does NOT define a command/wire ABI. It is only the
// in-memory shape a caller fills in before handing it to pack-ffp.js. How
// this state gets populated from wherever D3D9 state actually lives (the
// bridge) is out of scope here -- see the task note in the module this
// file ships alongside.
//
// Shared bitfields (alpha test, fog mode, projected-mask, specular-enable,
// point-scale-enable, bound-PS-SM3, fog color, fog distances, alpha ref)
// are stored ONCE under `shared`, not duplicated per vertex/pixel side.
// UNIFORMS.md section 2 explicitly calls out that the host must write the
// identical value into both the vertex-side and FS-suffixed pixel-side
// copy of each of these fields (they're separate anonymous-uniform-block
// members only because WebGL2's linker rejects same-named fields across
// anonymous blocks in different stages -- see PORTING_NOTES.md item 3).
// Keeping one source of truth here and letting pack-ffp.js write it to
// both places removes the "forgot to update one of the two copies" bug
// class by construction.

'use strict';

import { MAX_BLEND_MATRICES } from './ffp-constants.js';

function identityMat4() {
  // Column-major, matching GLSL's own mat4 element order.
  // prettier-ignore
  return [
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
  ];
}

function zeroVec4() {
  return [0, 0, 0, 0];
}

export function createDefaultLight() {
  return {
    diffuse: zeroVec4(),
    specular: zeroVec4(),
    ambient: zeroVec4(),
    position: zeroVec4(),
    direction: zeroVec4(),
    type: 0, // 0 = unset; shader values 1=POINT, 2=SPOT, 3=DIRECTIONAL
    range: 0,
    falloff: 0,
    attenuation0: 0,
    attenuation1: 0,
    attenuation2: 0,
    theta: 0,
    phi: 0,
  };
}

export function createDefaultMaterial() {
  return {
    diffuse: zeroVec4(),
    ambient: zeroVec4(),
    specular: zeroVec4(),
    emissive: zeroVec4(),
    power: 0,
  };
}

// One D3D9SharedPSStage entry (FFPixelStages block): the per-stage state
// that isn't a compact bitfield (constant color + flattened 2x2 bump-env
// matrix + luminance scale/offset).
export function createDefaultPixelStage() {
  return {
    constant: { a: 0, r: 0, g: 0, b: 0 },
    bumpEnvMat00: 0,
    bumpEnvMat01: 0,
    bumpEnvMat10: 0,
    bumpEnvMat11: 0,
    bumpEnvLScale: 0,
    bumpEnvLOffset: 0,
  };
}

// One texture-stage argument selector (D3DTA_* + modifiers).
export function createDefaultArg() {
  return { selector: 0, complement: false, alphaReplicate: false };
}

// One texture stage's op/arg state (packedStageOps.. / packedStageArgs..).
export function createDefaultTextureStage() {
  return {
    colorOp: 1, // D3DTOP_DISABLE
    alphaOp: 1, // D3DTOP_DISABLE
    storeToTemp: false,
    colorArgs: [createDefaultArg(), createDefaultArg(), createDefaultArg()],
    alphaArgs: [createDefaultArg(), createDefaultArg(), createDefaultArg()],
  };
}

export function createDefaultSampler() {
  return { type: 0, mode: 0 }; // TEXTURE_TYPE_2D, SAMPLER_MODE_DEFAULT
}

function createDefaultDataPrimitives() {
  return {
    texcoordIndex: [0, 0, 0, 0, 0, 0, 0, 0],
    texcoordFlags: [0, 0, 0, 0, 0, 0, 0, 0],
    texcoordTransformFlags: [0, 0, 0, 0, 0, 0, 0, 0],
    texcoordDeclCounts: [0, 0, 0, 0, 0, 0, 0, 0],
    vertexHasPositionT: false,
    vertexHasColor0: false,
    vertexHasColor1: false,
    vertexColor0NeedsBgra: false,
    vertexColor1NeedsBgra: false,
    vertexHasPointSize: false,
    vertexHasFog: false,
    blendMode: 0, // Disabled
    vertexBlendIndexed: false,
    vertexBlendCount: 0,
    vertexClipping: false,
    normalizeNormals: false,
    localViewer: false,
    rangeFog: false,
    useLighting: false,
    lightCount: 0,
    diffuseSource: 0, // D3DMCS_MATERIAL
    ambientSource: 0,
    specularSource: 0,
    emissiveSource: 0,
  };
}

// Full D3D9 FFP host-side state. All-zero/identity defaults; callers
// override only the fields relevant to what they're doing.
export function createDefaultState() {
  return {
    transform: {
      worldView: identityMat4(),
      normalMatrix: identityMat4(),
      inverseView: identityMat4(),
      projection: identityMat4(),
      worldViewProj: identityMat4(),
      texcoordMatrices: Array.from({ length: 8 }, identityMat4),
      viewportInverseOffset: zeroVec4(),
      viewportInverseExtent: [1, 1, 1, 1],
    },

    lights: Array.from({ length: 8 }, createDefaultLight),
    material: createDefaultMaterial(),
    globalAmbient: { a: 0, r: 0, g: 0, b: 0 },
    tweenFactor: 0,
    dataPrimitives: createDefaultDataPrimitives(),

    point: {
      size: 1, // D3DRS_POINTSIZE default-ish; decoded via /8.0 fixed-point
      sizeMin: 0,
      sizeMax: 64,
      scaleA: 1,
      scaleB: 0,
      scaleC: 0,
    },

    // Shared between vertex and (FS-suffixed) pixel blocks -- see module
    // header. pack-ffp.js writes each of these to both sides.
    shared: {
      alphaCompareOp: 7, // VK_COMPARE_OP_ALWAYS
      alphaPrecisionBits: 8,
      pointSpriteEnable: false,
      clipPlaneCount: 0,
      drefScaleShift: 0,

      fogEnable: false,
      vertexFogMode: 0, // D3DFOG_NONE
      pixelFogMode: 0,
      useZFog: false,

      projectedMask: 0, // 8 bits, bit i = stage i D3DTTFF_PROJECTED
      specularEnable: false,
      pointScaleEnable: false,
      boundPsIsSM3: false,

      fogColor: { r: 0, g: 0, b: 0 },
      alphaRef: 0, // 0-255, D3DRS_ALPHAREF
      fogDistanceScale: 0,
      fogDistanceEnd: 0,
      fogDensity: 0,
    },

    clipPlanes: Array.from({ length: 6 }, zeroVec4),
    blendMatrices: Array.from({ length: MAX_BLEND_MATRICES }, identityMat4),

    textureFactor: { a: 255, r: 255, g: 255, b: 255 },
    textureStages: Array.from({ length: 8 }, createDefaultTextureStage),
    samplers: Array.from({ length: 8 }, createDefaultSampler),
    pixelStages: Array.from({ length: 8 }, createDefaultPixelStage),
  };
}
