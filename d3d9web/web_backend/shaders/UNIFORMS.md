# D3D9 FFP shader — uniform / sampler contract

This is the host-side (JS) contract for `d3d9_ffp.vert` / `d3d9_ffp.frag`. It
describes every uniform block, every sampler, every packed bitfield, and (see
section 6) the vertex attribute layout the shaders expect. Treat this as the
input to the command-ABI design — whatever binary "set FFP state" command
format gets designed on the JS side has to be able to produce exactly these
bytes.

This document only covers the **shader-facing** contract (block layout,
field meaning, bit-packing). It does not prescribe how the JS side should be
architected (e.g. whether it keeps a shadow copy of D3D9 render/texture-stage
state and re-packs on change, or something else) — that's downstream of this.

## 0. Recommended packing strategy: don't hand-compute std140 offsets

GLSL ES 3.00 (WebGL2) uniform blocks use the `std140` layout, which has
non-obvious padding rules (`vec3` aligns like `vec4`, every array element of
a type smaller than `vec4` is padded out to 16 bytes, struct size/alignment
rounds up to a multiple of 16, etc). Hand-deriving byte offsets for blocks
this large is exactly the kind of bookkeeping that silently drifts out of
sync the next time a shader field is added or reordered.

**Recommended approach:** after linking the WebGL2 program, query the real
offsets with the standard introspection API and build each block's byte
buffer from that, instead of hardcoding offsets:

```js
const blockIndex = gl.getUniformBlockIndex(program, 'FFVertexData');
const indices = gl.getUniformIndices(program, ['data.WorldView', 'data.Lights[0].Diffuse', /* ... */]);
const offsets = gl.getActiveUniforms(program, indices, gl.UNIFORM_OFFSET);
// offsets[i] is now the byte offset of that field within the block, as
// actually laid out by the driver's GLSL compiler.
```

This sidesteps an entire class of packing bugs and stays correct if the
shader's uniform block ever changes shape. This document instead focuses on
**field names, types, array sizes, and semantic meaning** — the things
`getActiveUniforms` cannot tell you.

If a fixed, precomputed byte layout is still wanted for a binary wire
protocol, the general std140 rules needed to derive it by hand are: scalars
(4-byte align), `vec2` (8-byte align), `vec3`/`vec4` (16-byte align, `vec3`
consumes 16 bytes of space despite being 12 bytes of data), `mat4` (four
16-byte-aligned `vec4` columns = 64 bytes), array-of-anything-smaller-than-
vec4 (each element padded to a 16-byte stride), struct (aligned/padded to a
multiple of 16). None of this has been hand-verified against a real GLSL ES
compiler in this environment — see PORTING_NOTES.md's "Validation method and
limitations" section.

## 1. Vertex shader (`d3d9_ffp.vert`) uniform blocks

### `FFVertexData` (std140)

One instance, one `D3D9FixedFunctionVS data` member plus trailing scalars
that used to be DXVK's push-constant data and a subset of its spec-constant
data. Field names inside `data` match DXVK's own `D3D9FixedFunctionVS` /
`D3D9Light` / `D3DMATERIAL9` / `D3D9ViewportInfo` structs 1:1 (see DXVK's
`d3d9_state.h` / `d3d9_fixed_function.h` for the C++ side that used to fill
this), **except** the two changes called out in PORTING_NOTES.md
(no arrays-of-arrays, no scalar/row_major layout).

| Field | Type | Meaning |
|---|---|---|
| `data.WorldView` | `mat4` | World * View matrix |
| `data.NormalMatrix` | `mat4` | Matrix for transforming normals (inverse-transpose of WorldView, upper 3x3 used) |
| `data.InverseView` | `mat4` | Inverse of the View matrix (used to reconstruct world-space position for clip planes) |
| `data.Projection` | `mat4` | Projection matrix |
| `data.WorldViewProj` | `mat4` | Pre-multiplied World * View * Projection (used directly when vertex blending is off — do not decompose into separate multiplies, some content relies on this exact multiplication order) |
| `data.TexcoordMatrices[8]` | `mat4[8]` | Per-texture-stage D3DTS_TEXTURE0..7 transform matrices |
| `data.ViewportInfo.inverseOffset` | `vec4` | Pre-transformed-vertex (`D3DFVF_XYZRHW`) viewport offset term |
| `data.ViewportInfo.inverseExtent` | `vec4` | Pre-transformed-vertex viewport scale term |
| `data.Lights[8]` | `D3D9Light[8]` | See below |
| `data.Material` | `D3DMATERIAL9` | Current `D3DMATERIAL9` (Diffuse/Ambient/Specular/Emissive as `vec4`, Power as `float`) |
| `data.GlobalAmbient` | `uint` | Packed `D3DCOLOR` (`D3DRS_AMBIENT`), 0xAARRGGBB |
| `data.TweenFactor` | `float` | Vertex tweening interpolation factor (`D3DRS_TWEENFACTOR`) |
| `data.DataPrimitives[12]` | `uint[12]` | 48 packed bytes of per-draw flags/enums — see section 4 |

`D3D9Light` fields (`data.Lights[i]`):

| Field | Type | Meaning |
|---|---|---|
| `Diffuse`, `Specular`, `Ambient` | `vec4` | Light color components |
| `Position` | `vec4` | World-space position (xyz used) |
| `Direction` | `vec4` | World-space direction (xyz used) |
| `Type` | `uint` | `1`=`D3DLIGHT_POINT`, `2`=`D3DLIGHT_SPOT`, `3`=`D3DLIGHT_DIRECTIONAL` |
| `Range`, `Falloff` | `float` | D3DLIGHT9 fields, same meaning |
| `Attenuation0/1/2` | `float` | D3DLIGHT9 fields, same meaning (attenuation = 1/(a0 + a1*d + a2*d^2)) |
| `Theta`, `Phi` | `float` | Spot light cone angles — **already cosines**, not radians (matches DXVK: the shader does `dot()` against these directly, never `cos()`) |

Trailing `FFVertexData` scalars (were DXVK's push-constant blocks — merged
here since WebGL2 has no push-constant concept):

| Field | Type | Meaning |
|---|---|---|
| `packedFogColorAndAlphaRef` | `uint` | Packed `D3DCOLOR`; RGB = fog color (`D3DRS_FOGCOLOR`), top byte (bits 24-31) doubles as the alpha-test reference value (`D3DRS_ALPHAREF`, 0-255) |
| `fogDistanceScale` | `float` | Precomputed `1 / (D3DRS_FOGEND - D3DRS_FOGSTART)` for linear fog |
| `fogDistanceEnd` | `float` | `D3DRS_FOGEND` |
| `fogDensity` | `float` | `D3DRS_FOGDENSITY` |
| `packedReservedAndPointSize` | `uint` | Bits [16:32) = default point size, as a fixed-point value (divide by 8.0 to get the float) |
| `packedPointSizeMinMax` | `uint` | Bits [0:16) = point size min, bits [16:32) = point size max, same /8.0 fixed-point encoding |
| `pointScaleA/B/C` | `float` | `D3DRS_POINTSCALE_A/B/C` |
| `alphaTestAndModeArgs` | `uint` | Shared with the pixel shader — see section 3, only bits [16:19) (clip plane count) are read here |
| `fogArgs` | `uint` | Shared with the pixel shader — see section 3 |
| `packedProjMaskAndFfArgs` | `uint` | Shared with the pixel shader — see section 3, bits [8:9), [16:17), [24:25) are read here |

### `FFClipPlanes` (std140)

```glsl
uniform FFClipPlanes { vec4 clipPlanes[6]; };
```

6 user clip planes in D3D9's plane-equation form (`ax+by+cz+dw`), evaluated
against a world-space position reconstructed via `data.InverseView`. Only
the first `getClipPlaneCount()` (from `alphaTestAndModeArgs` bits [16:19))
entries are actually used per draw; unused entries can be zero.

### `FFBlendMatrices` (std140)

```glsl
uniform FFBlendMatrices { mat4 worldViewArray[256]; };
```

Replaces DXVK's unbounded `VertexBlendData` SSBO (`WorldViewArray[]`) — see
PORTING_NOTES.md "Vertex blend matrices — capped array". **256 is D3D9's own
hard ceiling for this state** (`D3DTS_WORLDMATRIX(i)` is only defined for `i`
in `0..255`), not an arbitrary placeholder like the previous 64-slot version
— this array can now never truncate legal D3D9 content, by construction. If
`MAX_BLEND_MATRICES` in `d3d9_ffp.vert` is ever changed, this array size must
change with it (and vice versa) — `ffp-constants.js`'s `MAX_BLEND_MATRICES`
export is the single source of truth for the JS-side array length/pack-loop
bound, so only the GLSL literal in `d3d9_ffp.vert` needs to stay in sync by
hand. The shader clamps indices into this array (`min(arrayIndex,
MAX_BLEND_MATRICES - 1u)`) so an out-of-range blend index reads the last
matrix instead of undefined-behaving; it does not error, so silently-
truncated skinning is a possible (currently unverified, and now far less
likely given the 256-slot ceiling matches D3D9's own maximum) symptom to
watch for.

**Capacity measurement (harness-verified, see PORTING_NOTES.md "Vertex blend
matrices — capped array" for the full numbers):** 256 `mat4`s is exactly
16384 bytes in std140 (256 × 64 bytes/mat4) — this fits within a single
uniform block on **every** conformant WebGL2 implementation, since
`MAX_UNIFORM_BLOCK_SIZE`'s spec-guaranteed floor is exactly 16384 bytes, and
used only about a quarter of this environment's real, harness-measured
`MAX_UNIFORM_BLOCK_SIZE` (65536 bytes, real Chrome/ANGLE driver). The
combined vertex-uniform budget across all 3 vertex-side blocks (`FFVertexData`
2096B + `FFClipPlanes` 96B + `FFBlendMatrices` 16384B ≈ 18576B) is also
comfortably inside both the spec-minimum and the measured
`MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS` (245760 words ≈ 983040 bytes,
measured; spec floor ≈ 50176 words).

## 2. Fragment shader (`d3d9_ffp.frag`) uniform blocks

### `FFPixelState` (std140)

**Naming note:** the fields below carry an `FS` suffix
(`alphaTestAndModeArgsFS`, `fogArgsFS`, `packedProjMaskAndFfArgsFS`,
`packedFogColorAndAlphaRefFS`, `fogDistanceScaleFS`, `fogDistanceEndFS`,
`fogDensityFS`) that the vertex shader's identically-*valued* fields (section
1's `FFVertexData`) do not have. This is a real-driver-verified fix, not a
stylistic choice: `FFVertexData` and `FFPixelState` are both anonymous
uniform blocks (no instance name), and WebGL2's linker rejects a program
where two different anonymous blocks in different stages declare an
identically-named field (`Ambiguous field '...' in blocks '...' (VERTEX
shader) and '...' (FRAGMENT shader) which don't have instance names.`) — see
PORTING_NOTES.md "Real-driver compile fixes" item 3. **The host must still
write the same value into both the vertex-side and `FS`-suffixed
fragment-side field for each of these** — only the GLSL identifier differs
per stage, the meaning and required value do not.

| Field | Type | Meaning |
|---|---|---|
| `alphaTestAndModeArgsFS` | `uint` | See section 3 (same value as vertex shader's `alphaTestAndModeArgs`) |
| `fogArgsFS` | `uint` | See section 3 (same value as vertex shader's `fogArgs`) |
| `packedProjMaskAndFfArgsFS` | `uint` | See section 3 (same value as vertex shader's `packedProjMaskAndFfArgs`) |
| `psSamplerTypes` | `uint` | See section 3 |
| `psSamplerModes` | `uint` | See section 3 |
| `packedStageOps01/23/45/67` | `uint` x4 | See section 3 |
| `packedStageArgs0..7` | `uint` x8 | See section 3 |
| `packedFogColorAndAlphaRefFS` | `uint` | Same value as the vertex shader's `packedFogColorAndAlphaRef` field (RGB fog color + packed alpha ref) |
| `fogDistanceScaleFS/EndFS`, `fogDensityFS` | `float` | Same meaning/value as the vertex-shader fields of the same name minus the `FS` suffix |
| `textureFactor` | `uint` | Packed `D3DCOLOR`, `D3DRS_TEXTUREFACTOR` |

### `FFPixelStages` (std140)

```glsl
struct D3D9SharedPSStage {
    uint  Constant;
    float BumpEnvMat00, BumpEnvMat01, BumpEnvMat10, BumpEnvMat11;
    float BumpEnvLScale;
    float BumpEnvLOffset;
};
uniform FFPixelStages { D3D9SharedPSStage stages[8]; };
```

Per-texture-stage state that isn't a compact bitfield:

| Field | Meaning |
|---|---|
| `Constant` | Packed `D3DCOLOR`, `D3DTSS_CONSTANT` for that stage |
| `BumpEnvMat00/01/10/11` | `D3DTSS_BUMPENVMAT00/01/10/11` — flattened 2x2 matrix (`RC` = row R, column C), was `float[2][2]` in DXVK, see PORTING_NOTES.md |
| `BumpEnvLScale/LOffset` | `D3DTSS_BUMPENVLSCALE` / `D3DTSS_BUMPENVLOFFSET` |

## 3. Packed bitfields (shared between vertex and fragment shaders)

These four `uint`s carry the same bit layout **and the same required value**
on both the vertex and the fragment side (mirrors DXVK's `SpecConsts`
buffer, minus the fields neither shader reads) — the host writes one
logical value per field, into both stages' uniform data. The GLSL
*identifier* differs per stage for three of them, though: the fragment
shader's copies of `alphaTestAndModeArgs`/`fogArgs`/`packedProjMaskAndFfArgs`
are named `alphaTestAndModeArgsFS`/`fogArgsFS`/`packedProjMaskAndFfArgsFS`
instead (an `FS`-suffixed fork forced by a real WebGL2 linker error — see
PORTING_NOTES.md "Real-driver compile fixes" item 3 and section 2 above).
The bit layouts described below apply identically regardless of which
name is used in a given stage.

### `alphaTestAndModeArgs`

| Bits | Width | Meaning |
|---|---|---|
| `[0:3)` | 3 | Alpha test compare op (`VK_COMPARE_OP_*` numbering: 0=NEVER, 1=LESS, 2=EQUAL, 3=LESS_OR_EQUAL, 4=GREATER, 5=NOT_EQUAL, 6=GREATER_OR_EQUAL, 7=ALWAYS) |
| `[4:8)` | 4 | Alpha test precision bits (`<=8` selects the fixed-point integer-precision alpha-test path; matches `D3DPMISCCAPS`-derived alpha precision) |
| `[8:9)` | 1 | Point sprites enabled (`D3DRS_POINTSPRITEENABLE`) — fragment shader only |
| `[16:19)` | 3 | Active clip plane count (0-6) — vertex shader only |
| `[24:32)` | 8 | `drefScaleShift`, used to compute the DREF (shadow-sample) scale factor — fragment shader only |

### `fogArgs`

| Bits | Width | Meaning |
|---|---|---|
| `[0:1)` | 1 | Fog enabled (`D3DRS_FOGENABLE`) |
| `[8:10)` | 2 | Vertex fog mode (`D3DRS_FOGVERTEXMODE`): 0=NONE, 1=EXP, 2=EXP2, 3=LINEAR |
| `[16:18)` | 2 | Pixel fog mode (`D3DRS_FOGTABLEMODE`), same numbering |
| `[24:25)` | 1 | Use `gl_FragCoord.z` (device depth) instead of `1/w` for fog distance (`D3DRS_RANGEFOGENABLE`-adjacent; matches whatever DXVK's own `useZ` condition was — see DXVK source for the exact D3D9 state this derives from) |

### `packedProjMaskAndFfArgs`

| Bits | Width | Meaning |
|---|---|---|
| `[0:8)` | 8 (1 bit each) | Per-stage "is projected" flag, bit `i` = stage `i`'s `D3DTTFF_PROJECTED` flag |
| `[8:9)` | 1 | Specular enabled (`D3DRS_SPECULARENABLE`) |
| `[16:17)` | 1 | Point scaling enabled (`D3DRS_POINTSCALEENABLE`) — vertex shader only |
| `[24:25)` | 1 | Bound pixel shader is Shader Model 3 — vertex shader only (affects default specular alpha) |

### `psSamplerTypes` / `psSamplerModes`

Both: bits `[i*2 : i*2+2)` for stage `i` (0-7), 2 bits each, 16 of the 32
bits used.

- `psSamplerTypes` value: `0`=2D, `1`=3D (volume — **not implemented**, see
  PORTING_NOTES.md), `2`=Cube, `3`=Null (no texture bound).
- `psSamplerModes` value: `0`=default, `1`=FETCH4, `2`=DREF, `3`=DREF_CLAMP.
  `>= 2` means "depth-compare" in the shader (`SAMPLER_MODE_DREF`); see
  PORTING_NOTES.md for how DREF is approximated in this port.

### `packedStageOps01` / `23` / `45` / `67`

Each of these 4 uints packs two texture stages' "ops" as 16 bits each (low
16 bits = even stage, high 16 bits = odd stage — e.g. `packedStageOps23`
bits `[0:16)` are stage 2, bits `[16:32)` are stage 3). Per-stage 16-bit
layout:

| Bits (within the 16) | Width | Meaning |
|---|---|---|
| `[0:5)` | 5 | Color op (`D3DTOP_*`, see the `D3DTOP_*` constants in the shaders — note DXVK's numbering, `D3DTOP_DISABLE=1`, not D3D9's raw enum) |
| `[8:13)` | 5 | Alpha op (same `D3DTOP_*` numbering) |
| `[15:16)` | 1 | `storeToTemp` — whether this stage writes `D3DTA_TEMP` instead of the running "current" color |

### `packedStageArgs0..7`

One `uint` per stage, 6 x 5-bit argument fields (30 of 32 bits used):

| Bits | Width | Meaning |
|---|---|---|
| `[0:5)` | 5 | Color arg 0 (only meaningful for `D3DTOP_MULTIPLYADD`/`D3DTOP_LERP`) |
| `[5:10)` | 5 | Color arg 1 |
| `[10:15)` | 5 | Color arg 2 |
| `[16:21)` | 5 | Alpha arg 0 |
| `[21:26)` | 5 | Alpha arg 1 |
| `[26:31)` | 5 | Alpha arg 2 |

Each 5-bit arg field: bits `[0:3)` = selector (`0`=DIFFUSE, `1`=CURRENT,
`2`=TEXTURE, `3`=TFACTOR, `4`=SPECULAR, `5`=TEMP, `6`=CONSTANT — **note this
is DXVK's own renumbering, shifted from D3D9's raw `D3DTA_*` values, not
the literal D3D9 enum**), bit `[3]` = complement (`1 - x`) modifier, bit
`[4]` = alpha-replicate (`.aaaa`) modifier.

## 4. `DataPrimitives[12]` byte layout (vertex shader only)

48 packed bytes (`uint[12]`), byte `n` read via
`DataPrimitives[n/4] >> ((n%4)*8) & 0xFF`. Matches DXVK's
`D3D9PackedFFVSData` byte-for-byte:

| Byte(s) | Meaning |
|---|---|
| `0..7` | Per-texcoord-stage (0-7) input stream index (`texcoordIndex`) |
| `8..15` | Per-texcoord-stage TCI generation flags (`texcoordFlags`) — one of `DXVK_TSS_TCI_PASSTHRU`(0)/`CAMERASPACENORMAL`(1)/`CAMERASPACEPOSITION`(2)/`CAMERASPACEREFLECTIONVECTOR`(3)/`SPHEREMAP`(4), pre-shift (shader left-shifts by 16 before comparing) |
| `16..23` | Per-texcoord-stage `D3DTEXTURETRANSFORMFLAGS` (`texcoordTransformFlags`): 0=DISABLE, 1-4=COUNT1-4 (bare component count), 256=PROJECTED (added to a count) |
| `24..27` (as one `uint`) | `vertexTexcoordDeclMask`: 8 x 3-bit fields, field `i` = number of components the vertex declaration actually provides for texcoord stream `i` (0-4) |
| `28` | `vertexHasPositionT` (bool: vertex format includes `D3DFVF_XYZRHW`) |
| `29` | `vertexHasColor0` (bool) |
| `30` | `vertexHasColor1` (bool) |
| `31` | `vertexHasPointSize` (bool) |
| `32` | `vertexHasFog` (bool) |
| `33` | `blendMode` (`0`=Disabled, `1`=Normal, `2`=Tween) |
| `34` | `vertexBlendIndexed` (bool) |
| `35` | `vertexBlendCount` (0-3; loop runs `vertexBlendCount()+1` iterations, i.e. 1-4 blend matrices) |
| `36` | `vertexClipping` (bool; read but not otherwise consumed in this shader, kept for ABI parity with DXVK) |
| `37` | `normalizeNormals` (bool) |
| `38` | `localViewer` (bool) |
| `39` | `rangeFog` (bool) |
| `40` | `useLighting` (bool) |
| `41` | `lightCount` (0-8) |
| `42` | `diffuseSource` (`D3DMATERIALCOLORSOURCE`: 0=Material, 1=Color1, 2=Color2) |
| `43` | `ambientSource` |
| `44` | `specularSource` |
| `45` | `emissiveSource` |
| `46..47` | unused |

## 5. Samplers

Per-stage, statically-typed, statically-bound (no descriptor indexing — see
PORTING_NOTES.md "Bindless samplers"):

| Uniform | Type | Texture unit budget note |
|---|---|---|
| `uTexture0` .. `uTexture7` | `sampler2D` | one texture unit each |
| `uTextureCube0` .. `uTextureCube7` | `samplerCube` | one texture unit each |

16 texture units total — this is exactly the WebGL2-guaranteed minimum for
`MAX_TEXTURE_IMAGE_UNITS` in a fragment shader. **The host must bind every
stage's texture to the matching unit every draw** (e.g. stage 3 with a 2D
texture bound → bind that texture to the unit backing `uTexture3`; leave
`uTextureCube3` pointing at any valid texture or a 1x1 dummy, since it's
still an active uniform even on frames where that stage doesn't use a cube
map). Query `gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS)` at startup — if a
target device reports more than 16, there is currently no shader-side code
that takes advantage of it (e.g. for volume textures); see
PORTING_NOTES.md.

## 6. Vertex attribute layout (`d3d9_ffp.vert`)

Not a uniform, but load-bearing for host integration and real-driver-tested
(see PORTING_NOTES.md "Real-driver compile fixes" item 2), so documented
here too. All 16 locations are `vec4`-typed generic attributes except where
noted; unused components for a given vertex format are left as whatever the
D3D9 vertex declaration doesn't provide (WebGL2 pads missing `vec4`
components with `(0,0,0,1)` when the bound buffer supplies fewer than 4
components, matching D3D9's own default-value behaviour for absent vertex
stream components).

| Location | Attribute | Meaning |
|---|---|---|
| 0 | `in_Position0` | D3DDECLUSAGE_POSITION, stream 0 (or POSITIONT if `vertexHasPositionT()`) |
| 1 | `in_Normal0` | D3DDECLUSAGE_NORMAL, stream 0 |
| 2 | `in_TweenPositionOrBlendWeight` | **Aliased** — see below |
| 3 | `in_TweenNormalOrBlendIndices` | **Aliased** — see below |
| 4-11 | `in_Texcoord0` .. `in_Texcoord7` | D3DDECLUSAGE_TEXCOORD 0-7 |
| 12 | `in_Color0` | D3DDECLUSAGE_COLOR 0 (diffuse) |
| 13 | `in_Color1` | D3DDECLUSAGE_COLOR 1 (specular) |
| 14 | `in_Fog` (`float`) | D3DDECLUSAGE_TESSFACTOR-style per-vertex fog input (read only if `vertexHasFog()`) |
| 15 | `in_PointSize` (`float`) | D3DDECLUSAGE_PSIZE (read only if `vertexHasPointSize()`) |

**Locations 2 and 3 are aliased between two mutually-exclusive D3D9 usages**
that DXVK's original Vulkan source gives four separate locations (18 total,
which does not fit WebGL2's `MAX_VERTEX_ATTRIBS` — confirmed to be exactly
16 even on a real, current, high-end GPU, not just a spec-minimum/
software-renderer artifact; see PORTING_NOTES.md). The shader's own control
flow (`blendMode()`, single-valued: Disabled/Normal/Tween) guarantees only
one interpretation is ever read per draw:

| Location | If `blendMode() == Tween` | If `blendMode() == Normal` (vertex blending/skinning) |
|---|---|---|
| 2 | `in_TweenPositionOrBlendWeight` = tween target position (was D3DDECLUSAGE_POSITION stream 1) | `in_TweenPositionOrBlendWeight` = blend weights (was D3DDECLUSAGE_BLENDWEIGHT 0) |
| 3 | `in_TweenNormalOrBlendIndices` = tween target normal (was D3DDECLUSAGE_NORMAL stream 1) | `in_TweenNormalOrBlendIndices` = blend indices (was D3DDECLUSAGE_BLENDINDICES 0) |

**The host must bind whichever D3D9 vertex stream is relevant to the
current draw's blend mode to locations 2/3 before issuing that draw** — a
real, per-draw host-side responsibility this aliasing introduces that
DXVK's original (unaliased, Vulkan) layout did not need.

There are no `sampler3D` (volume texture) or `sampler2DShadow`/
`samplerCubeShadow` (hardware depth-compare) uniforms in this port — see
PORTING_NOTES.md "Dropped features" for why, and how DREF sampling
(`psSamplerModes >= SAMPLER_MODE_DREF`) is approximated instead.
