# D3D9 FFP host-side state -> UBO packing layer

Turns a plain-data D3D9 fixed-function-pipeline state object into the
std140 uniform-buffer bytes `../shaders/d3d9_ffp.vert` / `d3d9_ffp.frag`
expect, per `../shaders/UNIFORMS.md`. Does **not** define a command/wire
ABI -- see that section's task note for scope.

## Files

- `ffp-state.js` -- pure data structures (render state, 8 texture stages,
  samplers, material, 8 lights, transform matrices, etc.) and
  `createDefaultState()`. No WebGL dependency.
- `ffp-constants.js` -- `D3DTOP_*`/`D3DTA_*`/compare-op/light-type/etc.
  numeric constants copied verbatim from the shader source, so callers
  don't hand-write magic numbers.
- `bitpack.js` -- pure bitfield packing for the 4 shared packed-uint
  fields (`alphaTestAndModeArgs`, `fogArgs`, `packedProjMaskAndFfArgs`,
  sampler types/modes), the per-stage op/arg fields, and the 48-byte
  `DataPrimitives` block. No WebGL dependency.
- `std140.js` -- runtime std140 offset introspection: `getBlockInfo`,
  `queryUniformLayout`, `queryBaseElementLayout`/`elementOffset` (see
  "Array offset gotcha" below), and small `DataView` write helpers.
- `pack-ffp.js` -- the actual packer: `packFFVertexData`,
  `packFFClipPlanes`, `packFFBlendMatrices`, `packFFPixelState`,
  `packFFPixelStages`, and `packAll(gl, program, state)`.
- `index.js` -- re-export barrel.
- `roundtrip/` -- WebGL2 round-trip verification (pack -> upload -> draw
  with the real shaders -> `readPixels` -> compare). See its own header
  comment and `run-headless.js --help`-equivalent usage below.

## Runtime offset-query approach

Per `UNIFORMS.md` section 0, no std140 byte offset is hardcoded anywhere
in this layer. Every offset comes from `gl.getUniformIndices()` +
`gl.getActiveUniforms(..., gl.UNIFORM_OFFSET)` (and `UNIFORM_MATRIX_STRIDE`
for matrices) against the actual linked program.

### Array offset gotcha (found while building this, not in UNIFORMS.md)

`gl.getUniformIndices()` reliably resolves **every element** of an array
of *structs* by name (e.g. `data.Lights[0].Diffuse` through
`data.Lights[7].Phi`, `stages[0].Constant` through `stages[7].BumpEnvLOffset`)
-- each struct-array element's fields are distinct named leaf uniforms per
the GLSL spec.

It does **not** do the same for a plain array of a *basic* type
(`mat4[8]`, `uint[12]`, `vec4[6]`) directly declared as a struct/block
member (`TexcoordMatrices[8]`, `DataPrimitives[12]`, `clipPlanes[6]`,
`worldViewArray[256]` -- `MAX_BLEND_MATRICES`, see `ffp-constants.js`):
only index `0` resolves; `TexcoordMatrices[1]`
through `[7]` (and the `DataPrimitives`/`clipPlanes`/`worldViewArray`
equivalents) come back `INVALID_INDEX`, confirmed against a real Chrome/
WebGL2 driver while building this layer -- see the "sanity mutation" note
in the round-trip report for how this was caught (an earlier version of
this code queried every index by name and threw on exactly this set of
names). This matches the OpenGL/GLES uniform-introspection convention
that a basic-type array is reported as a single active uniform (at its
first element) regardless of which elements are referenced, with `size`
giving the array length -- struct arrays don't have this shortcut because
there's no single "whole struct" uniform to collapse into.

**Fix applied here:** for these four arrays, only element `0` is queried
by name (`queryBaseElementLayout`); every other element's offset is
computed as `offset0 + i * arrayStride` (`elementOffset`), using the
driver-reported `UNIFORM_ARRAY_STRIDE` -- still zero hand-computed byte
counts, just one name query instead of N.

## Round-trip verification

```sh
cd tools/web_backend/host/roundtrip
node run-headless.js
```

(Reuses `../../harness/node_modules`'s already-installed `puppeteer-core`
rather than requiring a second `npm install` -- see that script's header.)

**Actual output, last run:**

```
--- d3d9_ffp.vert compile status: SUCCESS ---
--- d3d9_ffp.frag compile status: SUCCESS ---
--- Program link status: SUCCESS ---
[PASS] A: stage0 SELECTARG1(TFACTOR) reproduces packed D3DCOLOR -- expected rgba=10,220,50,200 got rgba=10,220,50,200 glError=NO_ERROR
[PASS] B: directional light Diffuse reproduces packed Light/Material state -- expected rgba=100,150,200,255 got rgba=100,150,200,255 glError=NO_ERROR
[PASS] C1: alphaRef=100 < alpha=128 survives with correct color -- expected rgba=5,250,90,128 got rgba=5,250,90,128 glError=NO_ERROR
[PASS] C2: alphaRef=200 > alpha=128 is discarded (background preserved) -- expected rgba=0,0,0,0 (clear/background) got rgba=0,0,0,0 glError=NO_ERROR
[PASS] D: vertex-blend index 255 resolves to worldViewArray[MAX_BLEND_MATRICES-1] (the last element) -- expected rgba=100,150,200,255 got rgba=100,150,200,255 glError=NO_ERROR
OVERALL: PASS
```

**Sanity check that these tests actually detect bugs** (not just
tautologically pass): a temporary one-line mutation swapping the R/B
byte order in `packD3DColor` was introduced, the suite was re-run, and
Tests A and C1 failed exactly as expected (showing the swapped channel
values), while Test B correctly kept passing (it doesn't route its tested
value through `packD3DColor`). The mutation was then reverted and the
suite re-confirmed at 4/4 passing. This was a one-off manual check done
while building the layer, not something `run-headless.js` re-runs itself.
Test D got the same negative-control treatment when it was added: with
`worldViewArray[MAX_BLEND_MATRICES-1]`'s z-flip temporarily reverted to a
plain identity matrix, the suite dropped to 4/5 with Test D failing (the
blended normal stayed unflipped, producing black instead of the lit
color) -- confirming the test actually discriminates a wrong/identity read
of the array's last element, not just a buffer that happens to be the
right size. Reverted and re-confirmed at 5/5.

### What these 4 tests (5 assertions) actually cover

- **A**: `textureFactor` (D3DCOLOR packing + `decodeD3DColor`),
  `packedStageOps01` (colorOp/alphaOp bits), `packedStageArgs0` (arg1
  selector bits for `D3DTA_TFACTOR`), `D3DTOP_DISABLE` short-circuit on
  stage 1.
- **B**: `DataPrimitives` bytes 40/41/42/43 (useLighting, lightCount,
  diffuseSource, ambientSource), `Lights[0]` struct field offsets
  (Diffuse/Ambient/Position/Direction/Type/Attenuation0), `Material`
  fields, `GlobalAmbient`, `packedProjMaskAndFfArgs` bit 8
  (specular-enable), and `packedStageArgs0` again with a *different*
  selector value (`D3DTA_DIFFUSE`) than test A. Also the main check that
  per-element struct-array offset introspection is actually correct (a
  wrong offset here would misread `Type`/`Direction` and break the
  exact-1.0 `hitDot` assumption, not just shift the color slightly).
- **C**: `packedFogColorAndAlphaRefFS` bits [24:32) (alpha ref byte),
  `alphaTestAndModeArgsFS` bits [0:3) (compare op) and, specifically,
  [4:8) taking the value 9 (the ">8 float-fallback" precision path, a
  different code path than A/B which both use `ALWAYS` and never reach
  this logic).
- **D**: `packFFBlendMatrices`'s element-offset arithmetic for the array's
  new *last* element (`worldViewArray[MAX_BLEND_MATRICES-1]`, index 255
  after the 64->256 capacity increase -- see `../shaders/PORTING_NOTES.md`
  "Vertex blend matrices"), `DataPrimitives` bytes 33/34/35 (`blendMode`,
  `vertexBlendIndexed`, `vertexBlendCount`), and the vertex-blend loop's
  indexed-lookup + weighting logic in `d3d9_ffp.vert`'s `transformVertex()`.
  Confirmed to actually discriminate a wrong/identity read (not just
  produce a correctly-sized buffer) -- see the negative-control note above.

### What is NOT verified by these tests

Fog (vertex or pixel), clip planes, texcoord generation/transform
matrices, point size/point sprites, bump-environment mapping
(`FFPixelStages`'s `BumpEnvMat*`/`BumpEnvLScale`/`BumpEnvLOffset`),
sampler `type`/`mode` bits (3D/cube/DREF paths), `storeToTemp`/
temp-register stage chaining, texture sampling itself (no real texture
content used, only a 1x1 dummy), point and spot lights (only directional
was exercised), multiple simultaneous lights, and the
`complement`/`alphaReplicate` argument modifier bits. `packFFClipPlanes`
is exercised only in the sense that it must produce a correctly-sized,
non-erroring buffer (the draw calls bind all 5 blocks every time) -- its
*content* is never read back and checked. `packFFBlendMatrices` now has
one element (the last one) content-verified by test D; every other slot
(0..254) and non-indexed/multi-matrix blending (`vertexBlendIndexed=false`,
`vertexBlendCount>0`) remain unverified.
