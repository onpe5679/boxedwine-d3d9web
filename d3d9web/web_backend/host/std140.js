// std140.js
//
// Runtime std140 uniform-block layout introspection. Per
// ../shaders/UNIFORMS.md section 0, byte offsets inside a std140 uniform
// block are queried from the driver via gl.getUniformIndices() +
// gl.getActiveUniforms(..., gl.UNIFORM_OFFSET) instead of hand-derived --
// std140 padding rules are non-obvious and this sidesteps that whole class
// of bug, and stays correct if a shader's uniform block shape ever changes.
//
// This module only reads layout metadata; it does not know about D3D9
// state or what any given field means -- see ffp-state.js / pack-ffp.js
// for that.

'use strict';

// Returns { index, dataSize } for a named uniform block, both queried from
// the linked program. Throws if the block isn't active in this program
// (e.g. wrong name, or optimized out because nothing references it).
export function getBlockInfo(gl, program, blockName) {
  const index = gl.getUniformBlockIndex(program, blockName);
  if (index === gl.INVALID_INDEX) {
    throw new Error(`std140.getBlockInfo: uniform block not found (not active?): ${blockName}`);
  }
  const dataSize = gl.getActiveUniformBlockParameter(program, index, gl.UNIFORM_BLOCK_DATA_SIZE);
  return { index, dataSize };
}

// Queries per-uniform layout (byte offset, array stride, matrix stride,
// row-major flag) for every full GLSL uniform name in `names` (e.g.
// 'data.Lights[0].Diffuse', 'stages[3].BumpEnvMat00', 'clipPlanes[5]').
//
// Returns a Map<name, {offset, arrayStride, matrixStride, rowMajor}>.
//
// Throws (rather than silently defaulting to offset 0) if any requested
// name does not resolve to an active uniform in `program` -- a silent
// wrong-but-plausible offset would produce a packed buffer that looks
// fine but is actually wrong, which is exactly the failure mode this
// runtime-introspection approach exists to avoid.
export function queryUniformLayout(gl, program, names) {
  const indices = gl.getUniformIndices(program, names);
  const missing = [];
  for (let i = 0; i < names.length; i++) {
    if (indices[i] === gl.INVALID_INDEX) missing.push(names[i]);
  }
  if (missing.length > 0) {
    throw new Error(
      `std140.queryUniformLayout: ${missing.length} name(s) are not active uniforms in this ` +
        `program (typo, or optimized out because the shader never statically reads them): ` +
        missing.join(', ')
    );
  }

  const offsets = gl.getActiveUniforms(program, indices, gl.UNIFORM_OFFSET);
  const arrayStrides = gl.getActiveUniforms(program, indices, gl.UNIFORM_ARRAY_STRIDE);
  const matrixStrides = gl.getActiveUniforms(program, indices, gl.UNIFORM_MATRIX_STRIDE);
  const isRowMajor = gl.getActiveUniforms(program, indices, gl.UNIFORM_IS_ROW_MAJOR);

  const layout = new Map();
  for (let i = 0; i < names.length; i++) {
    layout.set(names[i], {
      offset: offsets[i],
      arrayStride: arrayStrides[i],
      matrixStride: matrixStrides[i],
      rowMajor: !!isRowMajor[i],
    });
  }
  return layout;
}

// Queries a single "base element" name for a plain (non-struct) array
// member, e.g. 'data.TexcoordMatrices[0]', 'data.DataPrimitives[0]',
// 'clipPlanes[0]', 'worldViewArray[0]'. Returns its {offset, arrayStride,
// matrixStride, rowMajor}.
//
// Why only index 0: per the OpenGL/GLES uniform-introspection spec, an
// array of a *basic* type (scalar/vector/matrix -- as opposed to an array
// of structs) is always reported as exactly ONE active uniform, named at
// its first element, regardless of which elements the shader actually
// references. Querying 'TexcoordMatrices[1]' etc. by name for such an
// array reliably returns INVALID_INDEX (confirmed empirically against a
// real WebGL2/Chrome driver while building this module -- see
// PORTING_NOTES-equivalent discussion in host/README.md), even though the
// exact same by-index-name query pattern *does* work for every element of
// an array of structs (e.g. 'data.Lights[3].Diffuse'), because each
// struct-array element's fields are distinct named leaf uniforms.
// UNIFORM_ARRAY_STRIDE from this single query is valid for every element
// of the array (std140 requires one uniform stride across the whole
// array), so element i's offset is computed as `base.offset +
// i*base.arrayStride` -- still fully driver-derived, never a hardcoded
// byte count.
export function queryBaseElementLayout(gl, program, baseElementName) {
  const layout = queryUniformLayout(gl, program, [baseElementName]);
  return layout.get(baseElementName);
}

// Computes array element `index`'s byte offset from a base element's
// queried layout (see queryBaseElementLayout above).
export function elementOffset(base, index) {
  if (!base || base.arrayStride <= 0) {
    throw new Error('elementOffset: base element has no valid array stride (not an array uniform?)');
  }
  return base.offset + index * base.arrayStride;
}

// --- Small DataView write helpers, used by pack-ffp.js -----------------
// All std140 buffers are written little-endian: WebGL/typed-array uploads
// use the host's native byte order and never byte-swap, and every
// mainstream WebGL2 target (x86/x64, Apple Silicon) is little-endian, so
// this matches what the GPU will actually read.

export function writeF32(view, offset, value) {
  view.setFloat32(offset, value, true);
}

export function writeU32(view, offset, value) {
  view.setUint32(offset, value >>> 0, true);
}

export function writeVec4(view, offset, arr) {
  for (let i = 0; i < 4; i++) view.setFloat32(offset + i * 4, arr[i] !== undefined ? arr[i] : 0, true);
}

// `mat` is a length-16 array in column-major order (mat[c*4+r] = column c,
// row r), matching GLSL's own mat4 element order. `matrixStride` is the
// driver-reported byte distance between consecutive columns (always 16 in
// std140 for a mat4, but queried, never assumed -- see module header).
export function writeMat4(view, offset, matrixStride, mat) {
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      view.setFloat32(offset + c * matrixStride + r * 4, mat[c * 4 + r], true);
    }
  }
}
