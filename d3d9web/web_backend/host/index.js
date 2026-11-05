// index.js
//
// Convenience re-export barrel for the host-side D3D9 FFP state -> UBO
// packing layer. See ffp-state.js (pure state shape), pack-ffp.js (state
// -> std140 bytes, via runtime offset introspection), std140.js
// (introspection primitives), bitpack.js (bitfield packing helpers), and
// ffp-constants.js (D3DTOP_*/D3DTA_*/etc. numeric constants copied from
// the shaders). See ./roundtrip/ for the WebGL2 round-trip verification.

'use strict';

export * from './ffp-constants.js';
export * from './bitpack.js';
export * from './std140.js';
export * from './ffp-state.js';
export * from './pack-ffp.js';
