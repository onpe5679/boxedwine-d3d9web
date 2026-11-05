// ffp-constants.js
//
// Numeric constants mirrored 1:1 from ../shaders/d3d9_ffp.vert /
// d3d9_ffp.frag's own `const uint FOO = N;` declarations (and the bit
// layout tables in ../shaders/UNIFORMS.md section 3-4). Kept as a single
// named table so callers (state builders, packers, round-trip tests) don't
// hand-write magic numbers that could silently drift from the shader
// source. Every value here was copied from the GLSL source, not
// reverse-engineered or guessed.

'use strict';

// D3DTOP_* texture-stage color/alpha op codes (d3d9_ffp.frag).
export const D3DTOP = Object.freeze({
  DISABLE: 1,
  SELECTARG1: 2,
  SELECTARG2: 3,
  MODULATE: 4,
  MODULATE2X: 5,
  MODULATE4X: 6,
  ADD: 7,
  ADDSIGNED: 8,
  ADDSIGNED2X: 9,
  SUBTRACT: 10,
  ADDSMOOTH: 11,
  BLENDDIFFUSEALPHA: 12,
  BLENDTEXTUREALPHA: 13,
  BLENDFACTORALPHA: 14,
  BLENDTEXTUREALPHAPM: 15,
  BLENDCURRENTALPHA: 16,
  PREMODULATE: 17,
  MODULATEALPHA_ADDCOLOR: 18,
  MODULATECOLOR_ADDALPHA: 19,
  MODULATEINVALPHA_ADDCOLOR: 20,
  MODULATEINVCOLOR_ADDALPHA: 21,
  BUMPENVMAP: 22,
  BUMPENVMAPLUMINANCE: 23,
  DOTPRODUCT3: 24,
  MULTIPLYADD: 25,
  LERP: 26,
});

// D3DTA_* argument selectors (d3d9_ffp.frag). Note: this is DXVK's own
// renumbering (shifted from D3D9's raw D3DTA_* values), not the literal
// D3D9 enum -- see UNIFORMS.md section 3.
export const D3DTA = Object.freeze({
  DIFFUSE: 0x00,
  CURRENT: 0x01,
  TEXTURE: 0x02,
  TFACTOR: 0x03,
  SPECULAR: 0x04,
  TEMP: 0x05,
  CONSTANT: 0x06,
  SELECTMASK: 0x07,
  COMPLEMENT: 0x08,
  ALPHAREPLICATE: 0x10,
});

// VK_COMPARE_OP_* alpha-test compare ops (d3d9_ffp.frag).
export const COMPARE_OP = Object.freeze({
  NEVER: 0,
  LESS: 1,
  EQUAL: 2,
  LESS_OR_EQUAL: 3,
  GREATER: 4,
  NOT_EQUAL: 5,
  GREATER_OR_EQUAL: 6,
  ALWAYS: 7,
});

// D3DLIGHTTYPE (d3d9_ffp.vert). Note 0 is "unset", not a valid D3D9 value.
export const LIGHT_TYPE = Object.freeze({
  POINT: 1,
  SPOT: 2,
  DIRECTIONAL: 3,
});

// D3DMATERIALCOLORSOURCE (d3d9_ffp.vert).
export const MATERIAL_COLOR_SOURCE = Object.freeze({
  MATERIAL: 0,
  COLOR1: 1,
  COLOR2: 2,
});

// D3D9FF_VertexBlendMode (d3d9_ffp.vert).
export const VERTEX_BLEND_MODE = Object.freeze({
  DISABLED: 0,
  NORMAL: 1,
  TWEEN: 2,
});

// D3DFOGMODE (shared).
export const FOG_MODE = Object.freeze({
  NONE: 0,
  EXP: 1,
  EXP2: 2,
  LINEAR: 3,
});

// Sampler type / mode 2-bit codes (d3d9_ffp.frag).
export const SAMPLER_TYPE = Object.freeze({
  TEXTURE_2D: 0,
  TEXTURE_3D: 1,
  TEXTURE_CUBE: 2,
  TEXTURE_NULL: 3,
});

export const SAMPLER_MODE = Object.freeze({
  DEFAULT: 0,
  FETCH4: 1,
  DREF: 2,
  DREF_CLAMP: 3,
});

// FFBlendMatrices' worldViewArray size / MAX_BLEND_MATRICES (d3d9_ffp.vert).
// 256 is D3D9's own hard ceiling for this state (D3DTS_WORLDMATRIX(i) is
// only defined for i in 0..255) -- see PORTING_NOTES.md "Vertex blend
// matrices -- capped array" for the full capacity rationale and the
// harness-measured uniform-block budget it was checked against. Single
// source of truth for both ffp-state.js (array length) and pack-ffp.js
// (pack loop bound), so the two can never silently drift apart from the
// shader's own array size.
export const MAX_BLEND_MATRICES = 256;
