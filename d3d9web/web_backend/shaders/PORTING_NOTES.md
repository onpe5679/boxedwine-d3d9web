# Porting notes — D3D9 FFP shaders, DXVK (Vulkan/GLSL 450) -> WebGL2 (GLSL ES 3.00)

This document records every deviation from the DXVK source these shaders
were ported from, why each was necessary, what was dropped, what's
unverified, and how the port was checked given there is no local GLSL ES
compiler in this environment.

## License notice (required by DXVK's zlib/libpng license)

`d3d9_ffp.vert` and `d3d9_ffp.frag` are a derivative work of DXVK's
`src/d3d9/shaders/d3d9_fixed_function_vert.vert`,
`d3d9_fixed_function_frag.glsl`, and `d3d9_fixed_function_common.glsl`
(https://github.com/doitsujin/dxvk). DXVK's unaltered license text:

> Copyright (c) 2017 Philip Rebohle
> Copyright (c) 2019 Joshua Ashton
> Copyright (c) 2019 Robin Kertels
> Copyright (c) 2023 Jeffrey Ellison
>
> zlib/libpng license
>
> This software is provided 'as-is', without any express or implied warranty.
>
> Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:
>
> The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
>
> Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
>
> This notice may not be removed or altered from any source distribution.

In line with "altered source versions must be plainly marked as such": these
files are a heavily altered port targeting a different shading language
(GLSL ES 3.00 instead of Vulkan GLSL 450) and a different binding model
(plain WebGL2 uniforms/textures instead of Vulkan descriptor
sets/push-constants/SSBOs); they are **not** the original DXVK source. Every
file carries a header pointing back to the original DXVK source paths and
this license notice.

WineD3D's fixed-function shader-generation code (LGPL) was consulted only
for general background during the earlier research pass and **none of its
code or text was copied into this port** — only DXVK (zlib) was used as a
source, per the task constraint that WineD3D's LGPL code must not be reused
verbatim.

## Scope of what was ported

Both files implement the full D3D9 fixed-function pipeline as a single
uber-shader with uniform/bitfield branching (no shader permutations),
matching DXVK's own strategy exactly — DXVK doesn't compile per-state-
combination shader variants either, it branches at runtime on packed
spec-constant-like data, which is precisely what makes this approach
transplantable to a WebGL2 uber-shader (see task rationale: compiling
hundreds of permutations at runtime in a browser would stall the main
thread; GLSL ES 3.00 has `switch`, and (via a small polyfill, see below)
bitfield extraction, so the same branching strategy carries over).

Ported in full:
- All 26 `D3DTOP_*` texture-stage operations (`calculateTextureStage`).
- Argument reading incl. `COMPLEMENT`/`ALPHAREPLICATE` modifiers and the
  `D3DTA_*` selectors (`readArgValue`/`readArgValues`).
- The 8-stage unrolled texture-stage loop, including the "fast path" for
  identical color/alpha ops and the `D3DTOP_DOTPRODUCT3` special case.
- Bump-environment-mapping (`D3DTOP_BUMPENVMAP`/`BUMPENVMAPLUMINANCE`),
  including the luminance scale/offset post-multiply.
- Alpha test, with both the integer-precision and float fallback paths.
- Pixel and vertex fog (`D3DFOG_NONE/EXP/EXP2/LINEAR`, both per-pixel and
  per-vertex modes, range fog).
- Full fixed-function lighting (`computeLighting`): point/spot/directional
  lights, D3D9's exact attenuation formula `1/(a0 + a1*d + a2*d^2)`, spot
  cone falloff, ambient/diffuse/specular material-source selection
  (`D3DMCS_MATERIAL/COLOR1/COLOR2`).
- Vertex tweening, vertex blending/skinning (multi-matrix, indexed or
  sequential), position-T (`D3DFVF_XYZRHW`) passthrough path.
- Texture coordinate generation and transform: passthrough,
  camera-space-normal, camera-space-position, camera-space-reflection-
  vector, sphere-map, plus the `D3DTTFF_COUNT1..4`/`PROJECTED` transform-
  matrix application and component-count masking.
- Point size (including the D3D9 point-scale distance-attenuation formula)
  and point sprites (`gl_PointCoord` substitution for stage 0's texcoord
  when point sprites are enabled — this only rewires which texcoord feeds
  the *existing* per-stage sampling/op pipeline, it doesn't special-case
  which texture stage receives it, matching DXVK).
- User clip planes (reworked to not depend on `gl_ClipDistance` — see
  below).

Deliberately **not** ported (see "Dropped features" below): palette (P8)
textures/color-key, volume (3D) textures, hardware shadow-sampler DREF
(approximated instead), `WEBGL_clip_cull_distance`-based hardware clipping
(replaced with a universally-supported fallback).

## Removed / rewritten because not available in GLSL ES 3.00

Every one of these was checked against a primary or near-primary source
(the Khronos GLSL ES 3.00 spec via docs.gl's per-version support tables,
Khronos registry pages, or the WebGL extension registry) rather than
assumed from general GLSL familiarity — several of them contradict what
"GLSL ES 3.00 supports X" intuition would suggest, which is exactly why they
were checked instead of assumed:

| Construct | DXVK uses it for | ES 3.00 status | What this port does instead |
|---|---|---|---|
| `fma(a,b,c)` | dozens of call sites: lighting, `dp4`, most `D3DTOP_*` ops | **Not in ES 3.00** — added in ES 3.10 (verified via docs.gl's fma page: checkmark only under 3.10, none under 1.00/3.00) | Plain `a * b + c` everywhere. Loses the single-rounding-step precision guarantee of a true FMA — see "Precision" below. |
| `bitfieldExtract(uint,int,int)` | unpacking every packed spec-const/push-const bitfield (~30 call sites) | **Not in ES 3.00** — added in ES 3.10 (verified via docs.gl: checkmark only under 3.10) | A local polyfill function with the identical name and signature (`uint bitfieldExtract(uint value, int offset, int bits)`), built from core-ES-3.00 shift/mask operators (`<<`, `>>`, `&` on `uint` are confirmed core ES 3.00, unlike the function itself). Because the polyfill's name/signature matches the built-in exactly, every call site needed zero changes. |
| `unpackUnorm4x8(uint)` | `decodeD3DColor()` | **Not in ES 3.00** — the 4x8-width pack/unpack variants are ES 3.10+; only the 2x16 variants (`packUnorm2x16` etc.) are in ES 3.00 (verified via docs.gl's packUnorm page, explicit table distinguishing the 2x16 vs 4x8 variants) | `decodeD3DColor()` extracts each byte manually via shift+mask+divide. D3DCOLOR is `0xAARRGGBB`; DXVK's `unpackUnorm4x8(color).bgra` and this port's direct byte extraction produce the same `(r,g,b,a)` result (worked through explicitly in the function's comment). |
| `precise` qualifier | `dp4`/`vecTimesMat4` (bit-exact match with programmable-shader VS output, e.g. for Railroad Tycoon 3) | **Not in ES 3.00 or any ES version** — desktop GLSL 4.00+ only | Qualifier dropped; the arithmetic order in `dp4` is kept identical to the original fma-chain's evaluation order (x term first, then +y, +z, +w) as the closest available approximation. See "Precision" below — this is a real, unresolved fidelity gap. |
| `gl_ClipDistance[]` | `emitVsClipping()` (VS) implicit consumption by the rasterizer/clipper | **Not core in WebGL2** — only available via the non-universal `WEBGL_clip_cull_distance` extension (confirmed via the WebGL extension registry and the WebGL working group's own tracking issue for adding it) | Clip distances are written to a plain `out float v_ClipDistance[6]` varying instead, and the fragment shader explicitly `discard`s any fragment where a distance is negative (`applyClipPlanes()`, called first thing in `main()`). Works on every WebGL2 implementation unconditionally, at the cost of doing the clip as a per-fragment discard instead of true primitive clipping — see "Clip planes" below for the visual-fidelity trade-off. |
| `float BumpEnvMat[2][2]` (array-of-arrays) | `D3D9SharedPSStage.BumpEnvMat`, read in `calculateBumpmapCoords` | **Arrays-of-arrays not in ES 3.00** — that's a GLSL 4.30/`ARB_arrays_of_arrays` (desktop) or ES 3.20 feature, confirmed absent from ES 3.00/3.10 core | Flattened to 4 named scalars (`BumpEnvMat00/01/10/11`, `RC` = row R column C) on the `D3D9SharedPSStage` struct; `calculateBumpmapCoords`'s loop (which also used a now-removed `[[unroll]]` attribute, see below) was manually unrolled into two straight-line statements against the flattened fields. |
| `[[unroll]]` (`GL_EXT_control_flow_attributes`) | the 2-iteration bump-env-map loop | Not core GLSL ES 3.00 (an extension, and one WebGL2 doesn't expose) | The one loop it decorated was manually unrolled anyway (see previous row), so the attribute is simply gone with no remaining need for it. |
| `demote;` (`GL_EXT_demote_to_helper_invocation`) | alpha-test failure at the very end of `main()` | Not core GLSL ES 3.00 | Replaced with plain `discard;`. `demote` differs from `discard` only in keeping the invocation alive as a *helper invocation* so later derivative instructions (`dFdx`/`dFdy`/implicit-LOD `texture()` calls) in the same invocation still get valid neighboring-pixel data. Verified nothing in this shader runs after the alpha test, so there's no derivative-dependent code that could be affected — `discard` is behaviourally equivalent here specifically because it's the last statement in `main()`. |
| `spirv_instruction(...) spvNMin`/`spvNClamp` (`GL_EXT_spirv_intrinsics`) | NaN-propagating min/clamp (`GLSL.std.450` `NMin`/`NClamp` extended instructions) | Vulkan/SPIR-V-only mechanism, meaningless outside that pipeline | Replaced with thin wrapper functions of the same name calling plain `min()`/`clamp()`. Behaviourally identical except for unspecified behaviour when a NaN operand is present (GLSL's plain `min`/`clamp` don't guarantee NaN propagation or suppression the way the `N`-prefixed SPIR-V ext-instructions do) — considered a negligible, undocumented-either-way edge case for this content. |
| Scalar `.xxxx`/`.xxx`/`.aaaa`-style swizzle on non-vector expressions | `weight.xxxx`/`weight.xxx` (`weight` is `float`) in `transformVertex`'s blend loop; `texcoordCount.xxxx` and `count.xxxx` (`uint`) in `transformTexCoord`; `premodulate.xxxx` (`bool`) in `readArgValue` | Component swizzle syntax only applies to vector-typed expressions in the GLSL grammar; **applying it to a scalar is not valid GLSL in any version**, ES or desktop. (This is present verbatim in DXVK's real upstream source — fetched and diffed against the live GitHub file to rule out a corrupted local copy before deciding how to handle it; see "Validation method" below.) | Every occurrence rewritten with an explicit vector constructor that broadcasts the scalar to every component instead (`vec4(weight)`, `vec3(weight)`, `uvec4(texcoordCount)`, `uvec4(count)`, `bvec4(premodulate)`), which is unambiguously valid GLSL and has identical semantics to what the scalar-swizzle syntax is presumably relied on to mean (whatever DXVK's own build toolchain does with it — not investigated further, since the constructor form sidesteps the question entirely). |
| `#include "d3d9_fixed_function_common.glsl"` (`GL_GOOGLE_include_directive`) | pulling shared structs/helpers into both stages | No preprocessor `#include` in GLSL ES (or any standard GLSL) — this is a Google/glslang-specific extension for pre-SPIRV tooling | The shared content (packed-bitfield accessors, `decodeD3DColor`, the `bitfieldExtract` polyfill, `FogState`, etc.) is duplicated verbatim at the top of both `d3d9_ffp.vert` and `d3d9_ffp.frag`, since WebGL2 compiles each stage from an independent, self-contained source string. Task deliverable is exactly these two files (no third "common" file), which matches this. |
| Vulkan descriptor model: `layout(set=..., binding=...)`, `push_constant`, `scalar`/`row_major` block layout qualifiers, `GL_EXT_scalar_block_layout`, `GL_EXT_nonuniform_qualifier` | every uniform/buffer/sampler declaration | None of this exists in GLSL ES / WebGL2 — WebGL2 has exactly one flavor of uniform block layout (`std140`) and no push-constants, no descriptor sets, no scalar/row_major layout control | All state consolidated into a small number of plain `layout(std140) uniform` blocks (see `UNIFORMS.md`) and individual `uniform sampler2D/samplerCube` declarations. This is a real ABI difference from DXVK's native (scalar, push-constant) layout — host code cannot byte-copy DXVK's C++-side structs, it must pack to std140 rules (or better, use `gl.getActiveUniforms(...,UNIFORM_OFFSET)` introspection — see `UNIFORMS.md` section 0). |
| Bindless/descriptor-indexed sampler arrays: `texture2D t2d[8]`/`textureCube tcube[8]`/`texture3D t3d[8]` + `sampler sampler_heap[]`, combined via `sampler2D(t2d[stage], sampler_heap[...])` | per-stage texture sampling, so one shader can bind an arbitrary number/order of physical textures without recompiling | WebGL2 (GLSL ES 3.00) disallows dynamically/non-uniformly indexing sampler arrays (this is the ES 3.00-vs-desktop-4.00 "bindless" gap the task called out) | Each of the 8 stages gets its own **named**, statically-typed `sampler2D`/`samplerCube` uniform (`uTexture0..7`, `uTextureCube0..7`) rather than an indexed array at all — see "Bindless samplers" below for why this is safer than a small indexed array would have been. |
| `readonly buffer VertexBlendData { mat4 WorldViewArray[]; }` (SSBO) | vertex blending/skinning matrix palette, unbounded size | **SSBOs need GLES 3.1+ / GLSL ES 3.10+ (`buffer` blocks)** — not available in ES 3.00 / WebGL2 at all | Fixed-size `uniform` array (`FFBlendMatrices.worldViewArray[256]`, D3D9's own `D3DTS_WORLDMATRIX` ceiling), see "Vertex blend matrices" below. |

## Bindless samplers — why per-stage named uniforms, not a small sampler array

A tempting middle ground would have been `uniform sampler2D uTexture[8];`
(a small, fixed-size sampler array) instead of 8 separately-named uniforms.
That was deliberately avoided: GLSL ES 3.00 permits indexing a sampler array
with a non-constant expression only if it is *dynamically uniform*, and in
practice ANGLE (the translation layer every Chromium- and Firefox-based
WebGL2 implementation on Windows/most platforms uses to turn GLSL ES into
HLSL/Metal/native GL) has historically been inconsistent/restrictive about
this exact case. Since every call site in this shader already passes a
compile-time-constant stage index (the 8 stages are unrolled in `main()`,
never looped), a `switch`-based dispatch to individually-named samplers
(`fetchStage2D`/`fetchStageCube` in the fragment shader) gets the same
practical result — one direct, statically-resolvable sample per stage —
without ever declaring a sampler array or depending on any
dynamically-uniform-indexing guarantee at all. This was the safer choice
given no actual WebGL2/ANGLE build is available in this environment to test
against (see "Validation method" below).

## Dropped features

- **Palette (P8) textures and color-key.** Per explicit task instruction:
  DXVK doesn't implement these at all (P8/color-key is a WineD3D-only
  legacy-texture-format feature), and whether Azrok Plaza's client actually
  uses paletted textures is **not yet confirmed** — a separate probe is
  pending. Not implemented here; if the probe later confirms the game uses
  P8 textures, add support following WineD3D's approach conceptually (do
  not copy WineD3D's LGPL code) as a follow-up, once actually needed.
- **Volume (3D) textures.** `psSamplerTypes` can encode `TEXTURE_TYPE_3D`,
  and `sampleTexture()`'s type dispatch has a case for it, but it always
  returns `vec4(0.0)` (the same "null texture" fallback DXVK itself uses for
  unbound textures) rather than actually sampling. This was dropped
  specifically for texture-unit budget reasons: WebGL2 only guarantees 16
  fragment texture image units at minimum spec, and this port already uses
  all 16 for the 2D + Cube sampler pairs per stage (see `UNIFORMS.md`
  section 5); adding an 8-wide `sampler3D` array would require 24 total,
  exceeding the guaranteed floor on minimum-spec hardware. **Not verified**
  whether Azrok Plaza's fixed-function content ever binds a volume texture
  to a texture stage at all — if a later probe shows it's never used, this
  is a non-issue; if it is used, the fix is either to detect
  `MAX_TEXTURE_IMAGE_UNITS > 16` at runtime and conditionally add
  `sampler3D` uniforms in a shader variant, or to drop cube-map support
  (probably safer to assume unused first, given a 2005-era D3D9
  fixed-function MMO client is far more likely to use volume textures than
  environment cube maps, if it uses either).
- **Hardware shadow-sampler DREF sampling** (`psSamplerModes >=
  SAMPLER_MODE_DREF`). DXVK uses real `sampler2DShadow`/`samplerCubeShadow`
  hardware depth-compare samplers for this (GLSL ES 3.00 does support both
  shadow sampler types — this specific piece is not an ES 3.00 gap). It was
  still reimplemented as a manual, non-PCF-filtered `depth >= dref ? 1.0 :
  0.0` compare against a normally-sampled texture, purely to avoid doubling
  the sampler count per stage (a shadow variant needs its own separate
  sampler uniform — you cannot reuse the same `sampler2D` uniform as both a
  regular and shadow sampler), which would blow the 16-unit budget above.
  DREF sampling on the fixed-function path is an obscure D3D9
  self-sampling-a-depth-texture trick, essentially never used outside
  custom shadow-mapping engines — **not verified** whether Azrok Plaza uses
  it at all (plausible it never does, given it's an FFP-only client, but
  this has not been checked against real content/traffic).
- **Hardware clip distances via `WEBGL_clip_cull_distance`.** Implemented
  instead as a universal varying+discard fallback (see next section) so
  the shader works unconditionally; the extension path was not attempted as
  an optional fast path since that would require the host to compile two
  shader variants (with/without the extension) or use `#ifdef` +
  preprocessor injection, which is integration-layer work outside this
  task's scope ("실행/통합은 하지 말 것"). Left as a documented future
  optimization, not a correctness gap.

## Clip planes — varying + fragment discard instead of `gl_ClipDistance`

`emitVsClipping()` now writes `v_ClipDistance[i]` (a plain `out float[6]`
varying) instead of the built-in `gl_ClipDistance[i]`. The fragment shader's
`applyClipPlanes()` (called first in `main()`) discards any fragment where
an active plane's interpolated distance is negative.

Trade-off versus true hardware clip distances: hardware clipping happens at
primitive assembly, generating new clipped polygon edges exactly on the
clip plane, whereas fragment discard skips whole fragments after
rasterization/interpolation without ever generating a smooth clipped edge.
For opaque, non-antialiased geometry the visible difference is limited to
sub-pixel edge aliasing right at the clip boundary; it would be more
noticeable with edge antialiasing or additive/blended geometry straddling a
clip plane. Not benchmarked or visually verified in this environment (no
running WebGL2 context available here) — flagged as a NOT VERIFIED visual-
fidelity item, not a functional one (the discard-based approach cannot
produce a *wrong* result, only a slightly different edge treatment).

## Vertex blend matrices — capped array instead of an SSBO

DXVK's `VertexBlendData` is an unbounded `readonly buffer` (SSBO), which
doesn't exist in GLSL ES 3.00. This port uses a fixed-size `uniform mat4
worldViewArray[N]` instead (`MAX_BLEND_MATRICES = N` in `d3d9_ffp.vert`).

**Update 2026-07-19: raised from 64 to 256, harness-verified.** The original
64-slot cap was picked only because it comfortably fit the WebGL2-guaranteed
minimum UBO size (16384 bytes; 64 `mat4`s = 4096 bytes), **not because it was
derived from any measurement of what Azrok Plaza actually uses**. A later
argument-histogram measurement of real character-select-screen `SetTransform`
traffic (`https://github.com/onpe5679/azrok-server/docs/research/gate1/154_GATE1_ARGFLUSH_CHARACTER_SELECT_20260719.md`)
found the game filling `SetTransform`'s 64-unique-slot argflush table with
fixed slots 2/3 plus **world matrix slots 256..317** — i.e. **at least 62
distinct world matrices in a single scene**, already leaving only 2 slots of
headroom in the old 64-slot cap, with 64 itself confirmed to be only a probe-
table ceiling, not a proof that no higher index is ever used.

**New capacity: 256**, chosen as D3D9's own hard ceiling for this state —
`D3DTS_WORLDMATRIX(i)` (`D3DTRANSFORMSTATETYPE`) is only defined for `i` in
`0..255`, so an array of exactly this size can never truncate legal D3D9
content, unlike any smaller placeholder. This was checked against real
uniform-block limits, not assumed:

- **Real measurement, via `../harness/` (real Chrome/ANGLE driver on this
  machine, `node run-headless.js`, added `MAX_VERTEX_UNIFORM_COMPONENTS` /
  `MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS` logging to `index.html` for this
  check):**
  ```
  MAX_VERTEX_UNIFORM_BLOCKS: 14
  MAX_UNIFORM_BLOCK_SIZE: 65536
  MAX_VERTEX_UNIFORM_COMPONENTS: 16384
  MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS: 245760
  ```
  (`MAX_VERTEX_UNIFORM_COMPONENTS` only bounds the vertex shader's
  *default-block*, non-UBO uniforms — it does not constrain `worldViewArray`
  at all, since that's entirely inside a `uniform` block; it's logged anyway
  per the task's request for a real measurement. The actually-relevant
  numbers for a UBO array are `MAX_UNIFORM_BLOCK_SIZE`, per GLES 3.0 §6.7,
  the per-block cap, and `MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS`, the
  combined budget across every block plus the default block.)
- **Per-block math:** `mat4` is 64 bytes in std140 (4 columns × 16-byte-
  aligned `vec4`). 256 × 64B = **16384 bytes exactly** — which is not only
  well under this environment's real, measured 65536-byte
  `MAX_UNIFORM_BLOCK_SIZE` (uses ~25% of it), it is *exactly* the GLES 3.0
  spec's guaranteed-minimum floor for `MAX_UNIFORM_BLOCK_SIZE`. That means
  256 `mat4`s fits in a single uniform block on **any** conformant WebGL2
  implementation, not just this one measured GPU/driver — a stronger,
  more portable guarantee than "fits on the hardware in this environment."
- **Combined-budget math:** the vertex shader's 3 uniform blocks total
  `FFVertexData` (2096B) + `FFClipPlanes` (96B) + `FFBlendMatrices` (16384B
  after this change) ≈ 18576 bytes ≈ 4644 32-bit words — far under both the
  real measured combined budget (245760 words) and the GLES 3.0 spec-minimum
  combined floor (`MAX_VERTEX_UNIFORM_COMPONENTS` minimum 1024 words +
  `MAX_VERTEX_UNIFORM_BLOCKS` minimum 12 × `MAX_UNIFORM_BLOCK_SIZE` minimum
  16384B/4 words = 1024 + 49152 = 50176 words minimum guaranteed). No risk of
  exceeding the combined cap even on a bare-minimum-spec device.
- **Verified working, not just computed:** after raising `MAX_BLEND_MATRICES`
  to `256u` and `worldViewArray[256]` in `d3d9_ffp.vert` (and the matching
  `MAX_BLEND_MATRICES = 256` export in `../host/ffp-constants.js`, consumed
  by `ffp-state.js`'s array length and `pack-ffp.js`'s pack loop — no
  hardcoded `64`/`256` literal left on the JS side), `../harness/`'s
  `run-headless.js` re-ran clean: both shaders compile, link, and the bonus
  draw call still produces zero GL errors, with `FFBlendMatrices`'s real,
  driver-reported `UNIFORM_BLOCK_DATA_SIZE` now `16384` bytes (was `4096`).
  A new round-trip test (`../host/roundtrip/index.html`, test D) exercises
  `worldViewArray[255]` (`MAX_BLEND_MATRICES - 1`, the array's new last
  element) specifically — see `../host/README.md` for what it checks and its
  negative-control confirmation that it actually discriminates a wrong read.

The shader still clamps blend indices into range (`min(arrayIndex,
MAX_BLEND_MATRICES - 1u)`) instead of leaving an out-of-range index
undefined, so an index beyond 255 (which D3D9 itself never produces, since
`D3DTS_WORLDMATRIX` tops out there) would silently reuse the last matrix
rather than crash or corrupt memory — now a purely theoretical concern given
256 already covers D3D9's entire valid range, rather than the
real, measured risk the old 64-slot cap carried.

**Remaining, deliberately out of scope for this change:** whether Azrok
Plaza's content ever uses *non-indexed* multi-matrix blending
(`vertexBlendIndexed=false`, `vertexBlendCount>0`, i.e. more than one
*sequential* blend matrix per vertex rather than a single indexed lookup)
was not re-investigated here — the 256-slot ceiling is about the *range* of
indices `D3DTS_WORLDMATRIX` can address, not how many are blended per vertex
simultaneously (still governed by `vertexBlendCount` — DXVK models 1-4
simultaneous blend weights per vertex, per `DataPrimitives` byte 35's 2-bit
field, unrelated to this array's total capacity).

## Precision: `fma` / `precise`

DXVK marks `dp4`/`vecTimesMat4` (and heavily uses `fma()` throughout
lighting and texture-stage math) specifically so the fixed-function vertex
pipeline produces **bit-identical** results to DXVK's own programmable-
shader path, because some content (the comment cites Railroad Tycoon 3)
depends on FFP and programmable-shader vertex positions matching exactly
(e.g. a depth pre-pass done with one path and the main pass with the
other). GLSL ES 3.00 has neither `fma()` (a true fused multiply-add, one
rounding step) nor the `precise` qualifier (which would forbid the compiler
from reordering/refactoring the expression at all) — both are desktop-
GLSL/ES-3.10+-only. This port uses plain `a * b + c` (two rounding steps)
everywhere, with intermediate expressions kept in the same left-to-right
grouping as the original fma-chain to minimize (but not eliminate) the
divergence. **This is a real, currently-unquantified fidelity gap** — if
Azrok Plaza turns out to rely on FFP/programmable-VS bit-exactness anywhere
(unlikely for a fixed-function-only client, but not checked), this is the
first place to look.

## Uniform block reorganization

DXVK spreads this state across several distinct Vulkan resource kinds
(regular UBOs, an SSBO, spec-constant-like uniform buffers, and two flavors
of push-constant block) because Vulkan gives fine-grained control over each
one's update frequency/cost. WebGL2 has exactly one relevant concept (a
`std140 uniform` block) and no push-constants, so this port consolidates
everything into 4 blocks total (`FFVertexData`, `FFClipPlanes`,
`FFBlendMatrices` on the vertex side; `FFPixelState`, `FFPixelStages` on the
fragment side) — see `UNIFORMS.md` for the exact field-by-field layout of
each. This is a simplification, not a functional change; it does mean host
code must re-pack to `std140` rules (or use runtime introspection, the
recommended approach — see `UNIFORMS.md` section 0) rather than reusing any
byte layout from DXVK's C++ structs directly.

## Validation method and its limits

**No WebGL2/ANGLE/browser context, and no standalone GLSL ES *compiler*
(`glslangValidator`, ANGLE's `translator`, etc.), was available in this
environment** — this is the main residual gap, see below. What *was*
available and used:

### Automated grammar-level parse (real tool, not just manual review)

Both files were parsed with
[`@shaderfrog/glsl-parser`](https://github.com/ShaderFrog/glsl-parser)
v7.0.1 (installed from the public npm registry; a PEG-grammar-based parser
that explicitly targets "GLSL ES 1.0 and 3.0", built for the Shaderfrog
shader-editing tool), via:

```js
import { parse, GlslSyntaxError } from '@shaderfrog/glsl-parser';
const program = parse(sourceText, { failOnWarn: false, grammarSource: filename });
```

**Result: both `d3d9_ffp.vert` and `d3d9_ffp.frag` parsed successfully
(exit code 0, no `GlslSyntaxError` thrown).** The parser did emit a number
of "Encountered undefined variable" console warnings (e.g. for
`data`/`alphaTestAndModeArgs`/`stages`/`gl_Position`/`gl_FragCoord`) — these
are not real errors: this parser's scope resolution doesn't track plain
(non-block-instance-name) members of `layout(std140) uniform Block { ... };`
interface blocks as in-scope by their bare field names, and it doesn't
special-case built-ins (`gl_Position`, `gl_FragCoord`, `gl_PointCoord`,
`gl_PointSize`) as pre-declared. Both are documented, known characteristics
of this specific tool, not findings about the shader code. `failOnWarn` was
deliberately left `false` for this reason; with it forced `true` both files
still failed only on these same known-benign warnings, not on anything
structural.

**What a clean parse here actually confirms:** every brace, statement,
`switch`/`case`, struct/function declaration, and expression in both files
is grammatically well-formed GLSL ES per this parser's PEG grammar — this
is precisely the class of error a careful-but-manual read is most likely to
miss (e.g. a missing semicolon, an unbalanced brace introduced while
editing, or a genuinely-invalid construct like the scalar-swizzle pattern
this port specifically rewrote). It's a real, independent, tool-based check
layered on top of the manual review below, not a restatement of it.

**What it does NOT confirm** (this tool's own documented limitations,
plus what "grammar-valid" doesn't mean):
- **GLSL's declare-before-use rule.** Unlike a real GLSL compiler, this
  parser resolves function calls regardless of the callee's position in the
  file (it does not enforce GLSL's single-pass, no-forward-declaration
  rule). It would **not** have caught the `isSpecularEnabled()`-defined-
  after-its-only-caller ordering bug this port had at one point — that was
  only caught by manual re-reading, before this tool was even run. This is
  the clearest evidence the two checks (manual + tool) catch different
  things and neither alone is sufficient.
- **ES-version-gated builtin availability.** The parser doesn't know that
  e.g. `fma`/`bitfieldExtract`/`unpackUnorm4x8` are ES-3.10+-only and would
  happily parse code that called them directly — it has no per-version
  builtin-function table. All of the "not available in ES 3.00" claims in
  this document come from the external sources listed below, not from this
  parser.
- **std140 layout correctness**, actual driver acceptance of the
  `layout(std140) uniform` blocks, texture-unit-count limits, or anything
  else that depends on a real linker/backend rather than grammar.
- **Runtime correctness/fidelity** (i.e. "does it look right") — entirely
  out of scope for a static parse regardless of tool.

### Sources used for "is X available in GLSL ES 3.00" claims

Every claim in this document about what GLSL ES 3.00 does or doesn't
support was checked against:

- docs.gl's per-function, per-GLSL-version support tables (mirrors the
  Khronos reference pages; used for `fma`, `bitfieldExtract`,
  `unpackUnorm4x8`/`packUnorm4x8`, `roundEven`).
- The Khronos WebGL extension registry (for `WEBGL_clip_cull_distance` /
  `gl_ClipDistance`).
- General web search cross-referencing Khronos spec text/forum threads/
  compiler-issue-tracker discussion (for arrays-of-arrays, integer bitwise
  operator support, switch-statement int/uint interop, and — importantly —
  ruling out any implicit `int`<->`uint`<->`float` conversions in GLSL ES
  3.00, which desktop GLSL programmers often assume exist but don't in the
  ES profile).
- A direct fetch-and-diff of the actual upstream DXVK source file on GitHub
  (`raw.githubusercontent.com/doitsujin/dxvk/master/...`) against the
  locally-cached copy, specifically to rule out a transcription/OCR error
  before deciding how to handle the scalar-swizzle (`weight.xxxx` etc.)
  pattern — confirmed byte-identical to upstream, so that pattern is
  genuinely present in DXVK's real source and was a deliberate rewrite
  target, not a copy error on this port's part.

What this does **not** replace: an actual compile. Every `uint`/`int`
literal-suffix decision, every function-definition-before-use ordering
issue (one was caught by manual re-read: `isSpecularEnabled()` was
originally placed after `computeLighting()`, which calls it, and has since
been moved earlier in `d3d9_ffp.vert`), and every array-bound/type-match
detail was checked by careful manual re-reading of the full file text
(both files were re-read in full after writing, and grepped for every
removed construct — `fma(`, `precise`, `demote`, `gl_ClipDistance`,
`sampler_heap`, `#include`, `spirv_instruction`, `push_constant`,
`scalar`/`row_major` layout, `nonuniform`, and scalar-swizzle patterns — to
confirm zero live occurrences remain outside of comments explaining the
removal). This is a best-effort manual review, not a substitute for
compiling the shaders in an actual WebGL2 context — **before integrating
these shaders, compile both against a real WebGL2 context
(`gl.getShaderInfoLog`/`gl.getProgramInfoLog`) and fix whatever a real
ANGLE/native-GL compiler flags** that this manual review missed.

**Update: this has since been done.** See the next section — the gap
called out above is closed as of the harness added at `../harness/`.

## Real-driver compile fixes (harness-verified)

A minimal WebGL2 harness (`../harness/`, see its README.md) was built to
actually `gl.compileShader()`/`gl.linkProgram()` these two files against a
real driver, instead of only the grammar-level parse described above. It was
run against real Chrome (system install, not a bundled/mocked browser) in
two configurations: ANGLE's SwiftShader (software) backend, and ANGLE's
native D3D11 backend against a real GPU (`ANGLE (NVIDIA, NVIDIA GeForce RTX
5090 ..., D3D11 vs_5_0 ps_5_0)`) — both gave identical compile/link results,
which is what turned up the following three real, driver-verified problems.
All three are now fixed in these files; **as of this fix, both shaders
compile, link, and pass a bonus test draw call (dummy zeroed uniform buffers
+ 1x1 dummy textures bound to every sampler, `gl.drawArrays` with zero
`gl.getError()`) with no errors**, confirmed by actually running the
harness, not by static inspection.

1. **`#version` must be the literal first source line.** The base GLSL ES
   spec allows comments/whitespace before `#version`; ANGLE does not
   (`"#version directive must occur on the first line of the shader"`).
   Both files originally led with a multi-line copyright/description
   comment before `#version 300 es`. Fixed by moving `#version` to line 1
   and the comment below it. No semantic change — this is purely about
   physical line ordering in the source text.

2. **18 vertex attribute locations exceeds the real `MAX_VERTEX_ATTRIBS`.**
   DXVK's original layout uses locations 0-17 (18 separate attributes:
   position/normal x2 for tweening, 8 texcoords, 2 colors, fog, point size,
   blend weight, blend indices). WebGL2's spec only *guarantees*
   `MAX_VERTEX_ATTRIBS >= 16`, and this was confirmed to be the **actual**
   number reported by a real, current, high-end GPU (RTX 5090) through
   Chrome's default Windows backend (ANGLE/D3D11) — not just a
   minimum-spec/software-renderer artifact. Compiling with locations up to
   17 fails with `'location' : Attribute location out of range`.

   Fixed by aliasing two pairs of attributes onto shared locations, since
   `transformVertex()`'s own control flow guarantees at most one member of
   each pair is ever read for a given draw call (`blendMode()` is
   single-valued: `Disabled`/`Normal`/`Tween`, checked as alternatives, never
   combined):
   - location 2: `in_TweenPositionOrBlendWeight` (was `in_Position1` *or*
     `in_BlendWeight`)
   - location 3: `in_TweenNormalOrBlendIndices` (was `in_Normal1` *or*
     `in_BlendIndices`)

   This **is** a semantic/ABI change from DXVK, which gives these 4 inputs
   separate Vulkan attribute locations (Vulkan devices commonly expose far
   more than 16). The host must now bind whichever D3D9 vertex stream
   (POSITION1/NORMAL1 for tweening, or BLENDWEIGHT0/BLENDINDICES0 for
   multi-matrix blending) is relevant to the current draw's `blendMode` to
   locations 2/3 — see UNIFORMS.md's new vertex-attribute-layout section.
   Total attribute count is now exactly 16 (0-15), matching the real,
   universal limit measured above.

3. **Anonymous uniform blocks in different stages can't share field names.**
   `FFVertexData` (vertex) and `FFPixelState` (fragment) are both anonymous
   blocks (no instance name), and both declared several identically-named
   trailing scalar fields that are meant to hold the same value in both
   stages (`alphaTestAndModeArgs`, `fogArgs`, `packedProjMaskAndFfArgs`,
   `packedFogColorAndAlphaRef`, `fogDistanceScale`, `fogDistanceEnd`,
   `fogDensity`). Each shader stage compiled individually without any
   error, but **linking the program failed**:
   `Ambiguous field 'alphaTestAndModeArgs' in blocks 'FFPixelState' (VERTEX
   shader) and 'FFVertexData' (FRAGMENT shader) which don't have instance
   names.` This has no equivalent in DXVK's world at all: Vulkan/SPIR-V
   modules are never linked together the way a GL/WebGL program is, so
   DXVK's original source simply never encounters this class of error —
   it is purely a WebGL2/OpenGL-ES program-linking concern introduced by
   this port's choice of anonymous (instance-name-less) blocks.

   Fixed by renaming the **fragment shader's** copies of these 7 fields
   with an `FS` suffix (`alphaTestAndModeArgsFS`, `fogArgsFS`,
   `packedProjMaskAndFfArgsFS`, `packedFogColorAndAlphaRefFS`,
   `fogDistanceScaleFS`, `fogDistanceEndFS`, `fogDensityFS`); the vertex
   shader's names are untouched. The bit layout and intended value are
   still identical between the two stages (the host still writes the same
   value into both places, per UNIFORMS.md) — only the GLSL-visible
   identifier differs per stage now, purely to satisfy WebGL2's linker.
   See UNIFORMS.md section 3 for the updated field names.

4. **(Also fixed while investigating #3, same root cause class.)**
   Vertex-to-fragment varyings (`out_Normal`/`in_Normal`,
   `out_Texcoord0..7`/`in_Texcoord0..7`, `out_Color0`/`in_Color0`,
   `out_Color1`/`in_Color1`, `out_Fog`/`in_Fog`) originally had explicit
   `layout(location=N)` qualifiers, mirroring DXVK's Vulkan source (which
   always uses explicit `Location` decorations for inter-stage I/O — this
   is simply how SPIR-V interface matching works, there is no
   name-matching fallback in Vulkan). Compiling either shader stage alone
   failed: `'location' : invalid layout qualifier: only valid on program
   inputs and outputs`. Root cause (confirmed empirically, see below): core
   GLSL ES 3.00 (WebGL2) only accepts explicit `location` on the two fixed
   pipeline boundaries — vertex-shader inputs (vertex-fetch stage) and
   fragment-shader outputs (framebuffer-attachment stage) — not on
   inter-stage varyings. The `GL_EXT_separate_shader_objects` extension
   that would lift this restriction was tested directly
   (`#extension GL_EXT_separate_shader_objects : enable`) and is not
   exposed by this driver (`"extension is not supported"`).

   Fixed by dropping `layout(location=N)` from all 12 varying declarations
   in both files and renaming both sides of each pair to a single shared
   name, following the `v_` convention `v_ClipDistance` (the one varying
   that was already declared this way, without an explicit location, and
   which is exactly why it never had this problem): `v_Normal`,
   `v_Texcoord0..7`, `v_Color0`, `v_Color1`, `v_Fog`. Cross-stage varying
   matching in GLSL ES 3.00 without explicit locations is by exact name, so
   this requires both files to use identical identifiers — purely a
   renaming fix, no logic change. The fragment shader's own final color
   output (`out vec4 out_Color0;`, a *different* variable — the actual
   framebuffer write, not a varying) was already declared without an
   explicit location and needed no change; there is no longer a naming
   collision between it and the (now renamed) `v_Color0` varying input.

## Remaining risks / open questions (for follow-up measurement, not this task)

1. Does Azrok Plaza use paletted (P8) textures or color-key at all? (Blocks
   whether the deferred palette/color-key work is ever needed.)
2. Does it ever bind a volume (3D) or cube-map texture to a fixed-function
   texture stage? (Blocks whether the texture-unit budget needs
   rebalancing away from the current 8x2D + 8xCube split.)
3. Does it ever use DREF/shadow-mode sampling on an FFP texture stage?
   (Blocks whether the manual, non-PCF compare approximation is adequate or
   needs real hardware shadow samplers.)
4. ~~What's the real maximum simultaneous vertex-blend-matrix count the
   content uses? (Governs whether `MAX_BLEND_MATRICES = 64` is generous or
   insufficient.)~~ **SUPERSEDED.** A character-select-screen `SetTransform`
   measurement found at least 62 distinct world matrices in one scene
   (`https://github.com/onpe5679/azrok-server/docs/research/gate1/154_GATE1_ARGFLUSH_CHARACTER_SELECT_20260719.md`),
   already exceeding the old 64-slot cap's headroom. `MAX_BLEND_MATRICES`
   was raised to 256 — D3D9's own `D3DTS_WORLDMATRIX` index ceiling, not a
   guess — and harness-verified against real `MAX_UNIFORM_BLOCK_SIZE` /
   `MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS` limits. See "Vertex blend
   matrices — capped array" above for the full measurement. What's still
   open: the *simultaneous per-vertex* blend count (`vertexBlendCount`, 1-4
   matrices blended per vertex) was not re-measured — this item was only
   ever about the *index range* `D3DTS_WORLDMATRIX` can address, which 256
   now covers completely.
5. Does any content rely on FFP/programmable-VS bit-exact position matching
   (the Railroad-Tycoon-3-style case DXVK's `precise`/`fma` usage exists
   for)? (Governs whether the fma/precise precision gap is a real visual
   bug risk or purely theoretical for this content.)
6. ~~Both shaders are confirmed grammar-valid GLSL ES ... but actual
   compilation against a real WebGL2 context/linker has not happened.~~
   **SUPERSEDED.** Both shaders have now been compiled and linked against a
   real WebGL2/ANGLE driver (SwiftShader software backend and a real RTX
   5090 via the native D3D11 backend), and a bonus draw call with dummy
   uniform data was exercised with zero GL errors. Four real, driver-level
   problems were found this way (none of them catchable by grammar parsing
   or manual review) and fixed — see "Real-driver compile fixes" above and
   `../harness/README.md` for how to reproduce this. What remains
   **not** verified is *rendering correctness/fidelity* against real game
   content (real vertex/index buffers, real texture data, real per-draw
   uniform values) — the harness only proves the shaders compile, link, and
   accept a structurally-valid (all-zero) set of inputs without erroring,
   not that they render Azrok Plaza's actual scenes correctly. That remains
   future integration-testing work, out of scope for this task.
