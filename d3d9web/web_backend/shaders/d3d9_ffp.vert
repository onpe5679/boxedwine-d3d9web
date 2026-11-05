#version 300 es
// d3d9_ffp.vert
// D3D9 Fixed-Function Pipeline (FFP) uber-shader — vertex stage.
// Ported to GLSL ES 3.00 (WebGL2) from DXVK's hand-written D3D9 FFP shaders.
//
// Source of the ported logic:
//   dxvk (https://github.com/doitsujin/dxvk), src/d3d9/shaders/d3d9_fixed_function_vert.vert
//   and src/d3d9/shaders/d3d9_fixed_function_common.glsl
//   Copyright (c) 2017 Philip Rebohle, 2019 Joshua Ashton, 2019 Robin
//   Kertels, 2023 Jeffrey Ellison. DXVK is distributed under the
//   zlib/libpng license; see PORTING_NOTES.md for the full, unaltered
//   license notice as required by that license.
//
// This file is NOT a byte-for-byte translation: it targets GLSL ES 3.00,
// which lacks several features the original Vulkan/SPIR-V GLSL relies on
// (SSBOs, push constants, `fma`, `bitfieldExtract`, `precise`, `gl_ClipDistance`).
// See PORTING_NOTES.md for a full list of every deviation and why it was
// made. See UNIFORMS.md for the exact uniform contract the host (JS) side
// must fill in.
//
// NOTE: #version must be the literal first line of the file. The base GLSL
// ES spec allows comments/whitespace before #version, but ANGLE (the GLSL
// ES -> native translator every Chromium/Firefox WebGL2 implementation on
// Windows uses) rejects that with "#version directive must occur on the
// first line of the shader" -- confirmed by actually compiling this file
// against a real WebGL2/ANGLE driver (see ../harness/). Moved here as a
// result; see PORTING_NOTES.md "Real-driver compile fixes".

precision highp float;
precision highp int;

// ---------------------------------------------------------------------------
// Vertex attributes (locations must match host-side vertex layout)
// ---------------------------------------------------------------------------

layout(location = 0) in vec4 in_Position0;
layout(location = 1) in vec4 in_Normal0;
// NOTE: locations 2/3 are ALIASED between two D3D9 usages that DXVK gives
// separate slots (POSITION1/NORMAL1 for vertex tweening, BLENDWEIGHT0/
// BLENDINDICES0 for multi-matrix vertex blending/skinning). See
// PORTING_NOTES.md "Real-driver compile fixes -- vertex attribute count"
// for why: DXVK's 18-attribute layout does not fit WebGL2's universal
// MAX_VERTEX_ATTRIBS=16 (confirmed on real hardware, not just the spec's
// guaranteed minimum -- see that section). This aliasing is safe because
// transformVertex() below only ever reads ONE of these two per draw call,
// gated on the single-valued blendMode() (D3D9FF_VertexBlendMode is
// Disabled/Normal/Tween, never more than one at once) -- confirmed by
// reading the actual control flow, not assumed. The host must bind
// whichever D3D9 vertex stream is relevant to this location before each
// draw, based on that draw's blend mode.
layout(location = 2) in vec4 in_TweenPositionOrBlendWeight;
layout(location = 3) in vec4 in_TweenNormalOrBlendIndices;
layout(location = 4) in vec4 in_Texcoord0;
layout(location = 5) in vec4 in_Texcoord1;
layout(location = 6) in vec4 in_Texcoord2;
layout(location = 7) in vec4 in_Texcoord3;
layout(location = 8) in vec4 in_Texcoord4;
layout(location = 9) in vec4 in_Texcoord5;
layout(location = 10) in vec4 in_Texcoord6;
layout(location = 11) in vec4 in_Texcoord7;
layout(location = 12) in vec4 in_Color0;
layout(location = 13) in vec4 in_Color1;
layout(location = 14) in float in_Fog;
layout(location = 15) in float in_PointSize;
// in_BlendWeight / in_BlendIndices were previously separate locations 16/17;
// now aliased onto locations 2/3 (in_TweenPositionOrBlendWeight /
// in_TweenNormalOrBlendIndices above) to fit within MAX_VERTEX_ATTRIBS=16.

invariant gl_Position;

// ---------------------------------------------------------------------------
// Varyings (locations must match d3d9_ffp.frag)
// ---------------------------------------------------------------------------

// NOTE: no explicit layout(location=N) here (unlike the attributes above).
// Confirmed by actually compiling against a real WebGL2/ANGLE driver: core
// GLSL ES 3.00 (WebGL2) only accepts explicit `location` on the two fixed
// pipeline boundaries -- vertex-shader inputs (the vertex-fetch stage) and
// fragment-shader outputs (the framebuffer-attachment stage). Applying it to
// an inter-stage vertex-output/fragment-input varying, as DXVK's Vulkan/
// SPIR-V source does (Vulkan always uses explicit Location decorations),
// fails with "'location' : invalid layout qualifier: only valid on program
// inputs and outputs" -- the `GL_EXT_separate_shader_objects` extension that
// would allow it is also not exposed by this driver (tested; produces
// "extension is not supported"). See PORTING_NOTES.md "Real-driver compile
// fixes -- varying locations". Fixed by dropping `location` and matching
// vertex-output/fragment-input pairs by name instead (core, always-portable
// GLSL behaviour) -- hence the `v_` names below, matching d3d9_ffp.frag's
// `v_ClipDistance` convention exactly (the one varying pair that already had
// no explicit location and always worked).
out vec4 v_Normal;
out vec4 v_Texcoord0;
out vec4 v_Texcoord1;
out vec4 v_Texcoord2;
out vec4 v_Texcoord3;
out vec4 v_Texcoord4;
out vec4 v_Texcoord5;
out vec4 v_Texcoord6;
out vec4 v_Texcoord7;
out vec4 v_Color0;
out vec4 v_Color1;
out float v_Fog;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const uint TextureStageCount = 8u;
const uint MaxEnabledLights = 8u;

// Was a `[]` (unbounded) storage-buffer array in DXVK (`VertexBlendData`,
// a `readonly buffer`). GLSL ES 3.00 / WebGL2 has no SSBOs (that needs
// GLES 3.1+), so this is a fixed-size uniform array instead. 256 is D3D9's
// own hard ceiling for this state (D3DTS_WORLDMATRIX(i) is only defined for
// i in 0..255 -- see D3DTRANSFORMSTATETYPE), so this cap can never truncate
// real D3D9 content, unlike the previous 64-slot placeholder (real
// character-select-screen SetTransform traffic was measured using world
// matrices up to index 61, i.e. slot 317 = D3DTS_WORLDMATRIX(61), already
// leaving only 2 slots of margin in a 64-slot table -- see
// https://github.com/onpe5679/azrok-server/docs/research/gate1/154_GATE1_ARGFLUSH_CHARACTER_SELECT_20260719.md). 256 mat4s is
// 16384 bytes in std140 (256 * 64B/mat4), which fits inside a single
// uniform block on every conformant WebGL2 implementation (spec-guaranteed
// MAX_UNIFORM_BLOCK_SIZE floor is exactly 16384 bytes) and used only a
// quarter of the real, harness-measured MAX_UNIFORM_BLOCK_SIZE (65536
// bytes) on this environment's driver -- see PORTING_NOTES.md "Vertex blend
// matrices — capped array" for the full measurement and the combined
// (all-blocks) budget check.
const uint MAX_BLEND_MATRICES = 256u;

#define D3DFOGMODE uint
const uint D3DFOG_NONE   = 0u;
const uint D3DFOG_EXP    = 1u;
const uint D3DFOG_EXP2   = 2u;
const uint D3DFOG_LINEAR = 3u;

const uint MaxClipPlaneCount = 6u;

// Replacement for DXVK's `gl_ClipDistance[]`. See PORTING_NOTES.md
// "Clip planes" and d3d9_ffp.frag's `applyClipPlanes()`.
out float v_ClipDistance[MaxClipPlaneCount];

// ---------------------------------------------------------------------------
// Polyfills for GLSL ES 3.00 (WebGL2) — duplicated from d3d9_ffp.frag since
// GLSL ES has no #include; see that file for the rationale of each.
// ---------------------------------------------------------------------------

uint bitfieldExtract(uint value, int offset, int bits) {
    if (bits <= 0) return 0u;
    uint off = uint(offset);
    uint b = uint(bits);
    uint mask = (b >= 32u) ? 0xFFFFFFFFu : ((1u << b) - 1u);
    return (value >> off) & mask;
}

vec4 decodeD3DColor(uint color) {
    float a = float((color >> 24u) & 0xFFu) * (1.0 / 255.0);
    float r = float((color >> 16u) & 0xFFu) * (1.0 / 255.0);
    float g = float((color >> 8u)  & 0xFFu) * (1.0 / 255.0);
    float b = float( color         & 0xFFu) * (1.0 / 255.0);
    return vec4(r, g, b, a);
}

float spvNMin(float a, float b) { return min(a, b); }
float spvNClamp(float x, float lo, float hi) { return clamp(x, lo, hi); }

// ---------------------------------------------------------------------------
// Uniform data
// ---------------------------------------------------------------------------

struct D3D9ViewportInfo {
    vec4 inverseOffset;
    vec4 inverseExtent;
};

#define D3DLIGHTTYPE uint
const uint D3DLIGHT_POINT       = 1u;
const uint D3DLIGHT_SPOT        = 2u;
const uint D3DLIGHT_DIRECTIONAL = 3u;

struct D3D9Light {
    vec4 Diffuse;
    vec4 Specular;
    vec4 Ambient;

    vec4 Position;
    vec4 Direction;

    uint Type;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
};

struct D3DMATERIAL9 {
    vec4  Diffuse;
    vec4  Ambient;
    vec4  Specular;
    vec4  Emissive;
    float Power;
};

struct D3D9FixedFunctionVS {
    mat4 WorldView;
    mat4 NormalMatrix;
    mat4 InverseView;
    mat4 Projection;
    mat4 WorldViewProj;

    mat4 TexcoordMatrices[8]; // TextureStageCount

    D3D9ViewportInfo ViewportInfo;

    D3D9Light Lights[8]; // MaxEnabledLights
    D3DMATERIAL9 Material;
    uint GlobalAmbient;
    float TweenFactor;

    // Packed per-byte flags/enums — see UNIFORMS.md for the exact bit
    // layout of every DataPrimitives[N] byte (mirrors DXVK's
    // D3D9PackedFFVSData exactly).
    uint DataPrimitives[12];
};

#define D3D9FF_VertexBlendMode uint
const uint D3D9FF_VertexBlendMode_Disabled = 0u;
const uint D3D9FF_VertexBlendMode_Normal   = 1u;
const uint D3D9FF_VertexBlendMode_Tween    = 2u;

#define D3DMATERIALCOLORSOURCE uint
const uint D3DMCS_MATERIAL = 0u;
const uint D3DMCS_COLOR1   = 1u;
const uint D3DMCS_COLOR2   = 2u;

#define D3DTEXTURETRANSFORMFLAGS uint
const uint D3DTTFF_DISABLE   = 0u;
const uint D3DTTFF_COUNT1    = 1u;
const uint D3DTTFF_COUNT2    = 2u;
const uint D3DTTFF_COUNT3    = 3u;
const uint D3DTTFF_COUNT4    = 4u;
const uint D3DTTFF_PROJECTED = 256u;

const uint DXVK_TSS_TCI_PASSTHRU                    = 0x00000000u;
const uint DXVK_TSS_TCI_CAMERASPACENORMAL           = 0x00010000u;
const uint DXVK_TSS_TCI_CAMERASPACEPOSITION         = 0x00020000u;
const uint DXVK_TSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000u;
const uint DXVK_TSS_TCI_SPHEREMAP                   = 0x00040000u;

const uint TCIOffset = 16u;
const uint TCIMask = (7u << TCIOffset);

layout(std140) uniform FFVertexData {
    D3D9FixedFunctionVS data;

    // D3D9SharedPushData equivalent
    uint  packedFogColorAndAlphaRef;
    float fogDistanceScale;
    float fogDistanceEnd;
    float fogDensity;

    // D3D9VsPushData equivalent
    uint packedReservedAndPointSize;
    uint packedPointSizeMinMax;

    // D3D9FfvsPushData equivalent
    float pointScaleA;
    float pointScaleB;
    float pointScaleC;

    // Subset of DXVK's SpecConsts that the vertex shader needs.
    uint alphaTestAndModeArgs;    // bits 16..18: clip plane count
    uint fogArgs;
    uint packedProjMaskAndFfArgs; // bit 16: point scaling enabled, bit 24: SM3 bound ps
};

layout(std140) uniform FFClipPlanes {
    vec4 clipPlanes[6]; // MaxClipPlaneCount
};

layout(std140) uniform FFBlendMatrices {
    mat4 worldViewArray[256]; // MAX_BLEND_MATRICES
};

// ---------------------------------------------------------------------------
// Packed VS flag/enum accessors (D3D9PackedFFVSData byte layout — see
// UNIFORMS.md). Unchanged from DXVK except bitfieldExtract -> polyfill.
// ---------------------------------------------------------------------------

uint extractByte(uint offset) {
    uint dword = data.DataPrimitives[offset / 4u];
    return bitfieldExtract(dword, (int(offset) % 4) * 8, 8);
}

uint texcoordIndex(uint index) {
    return extractByte(index);
}
bool vertexHasPositionT() {
    return extractByte(28u) != 0u;
}
bool vertexHasColor0() {
    return extractByte(29u) != 0u;
}
bool vertexHasColor1() {
    return extractByte(30u) != 0u;
}
bool vertexHasPointSize() {
    return extractByte(31u) != 0u;
}
bool useLighting() {
    return extractByte(40u) != 0u;
}
bool normalizeNormals() {
    return extractByte(37u) != 0u;
}
bool localViewer() {
    return extractByte(38u) != 0u;
}
bool rangeFog() {
    return extractByte(39u) != 0u;
}

uint texcoordFlags(uint index) {
    return extractByte(TextureStageCount + index);
}
uint diffuseSource() {
    return extractByte(42u);
}
uint ambientSource() {
    return extractByte(43u);
}
uint specularSource() {
    return extractByte(44u);
}
uint emissiveSource() {
    return extractByte(45u);
}

uint texcoordTransformFlags(uint index) {
    return extractByte(TextureStageCount * 2u + index);
}
uint lightCount() {
    return extractByte(41u);
}

uint vertexTexcoordDeclMask() {
    return data.DataPrimitives[24u / 4u];
}
bool vertexHasFog() {
    return extractByte(32u) != 0u;
}
uint blendMode() {
    return extractByte(33u);
}
bool vertexBlendIndexed() {
    return extractByte(34u) != 0u;
}
uint vertexBlendCount() {
    return extractByte(35u);
}
bool vertexClipping() {
    return extractByte(36u) != 0u;
}

// Number of active clipping planes
uint getClipPlaneCount() {
    return bitfieldExtract(alphaTestAndModeArgs, 16, 3);
}

// Checks whether specular lighting is enabled. GLSL requires functions to be
// defined before first use (no forward declarations), so this must appear
// before computeLighting() below, which calls it.
bool isSpecularEnabled() {
    return bitfieldExtract(packedProjMaskAndFfArgs, 8, 1) != 0u;
}

// Checks whether point scaling is enabled
bool isPointScalingEnabled() {
    return bitfieldExtract(packedProjMaskAndFfArgs, 16, 1) != 0u;
}

// Checks whether active pixel shader is SM3
bool boundPsIsShaderModel3() {
    return bitfieldExtract(packedProjMaskAndFfArgs, 24, 1) != 0u;
}

// Checks sampler projection state without accessing all sampler state
bool isSamplerProjected(uint idx) {
    return bitfieldExtract(packedProjMaskAndFfArgs, int(idx), 1) != 0u;
}

// ---------------------------------------------------------------------------
// Fog (vertex-mode contribution)
// ---------------------------------------------------------------------------

struct FogState {
    bool enable;
    uint pixelMode;
    uint vertexMode;
    bool useZ;
};

FogState getFogState() {
    FogState result;
    result.enable = bitfieldExtract(fogArgs, 0, 1) != 0u;
    result.vertexMode = bitfieldExtract(fogArgs, 8, 2);
    result.pixelMode = bitfieldExtract(fogArgs, 16, 2);
    result.useZ = bitfieldExtract(fogArgs, 24, 1) != 0u;
    return result;
}

float calculateFog(vec4 vPos) {
    FogState fogState = getFogState();

    vec4 specular = in_Color1;
    bool hasSpecular = vertexHasColor1();

    float fogScale = fogDistanceScale;
    float fogEnd = fogDistanceEnd;
    float fogDens = fogDensity;

    if (!fogState.enable)
        return 0.0;

    float w = vPos.w;
    float z = vPos.z;
    float depth;
    if (rangeFog()) {
        vec3 pos3 = vPos.xyz;
        depth = length(pos3);
    } else {
        depth = vertexHasFog() ? in_Fog : abs(z);
    }
    float fogFactor;
    if (vertexHasPositionT()) {
        fogFactor = hasSpecular ? specular.w : 1.0;
    } else if (fogState.vertexMode == D3DFOG_LINEAR) {
        // (end - d) / (end - start)
        fogFactor = fogEnd - depth;
        fogFactor = fogFactor * fogScale;
        fogFactor = spvNClamp(fogFactor, 0.0, 1.0);
    } else if (fogState.vertexMode == D3DFOG_EXP2 || fogState.vertexMode == D3DFOG_EXP) {
        // D3DFOG_EXP2: 1 / (e^[d * density])^2
        // D3DFOG_EXP:  1 / (e^[d * density])
        fogFactor = depth * fogDens;

        if (fogState.vertexMode == D3DFOG_EXP2)
            fogFactor *= fogFactor;

        // Provides the rcp.
        fogFactor = -fogFactor;
        fogFactor = exp(fogFactor);
    } else {
        // D3DFOG_NONE
        fogFactor = hasSpecular ? specular.w : 1.0;
    }

    return fogFactor;
}

// ---------------------------------------------------------------------------
// Point size
// ---------------------------------------------------------------------------

// Return point size, min, max as raw float
vec3 decodePointSize() {
    uvec3 pointData = uvec3(
        bitfieldExtract(packedReservedAndPointSize, 16, 16),
        bitfieldExtract(packedPointSizeMinMax,  0, 16),
        bitfieldExtract(packedPointSizeMinMax, 16, 16));
    return vec3(pointData) / 8.0;
}

float calculatePointSize(vec4 vtx) {
    vec3 pointSizeArgs = decodePointSize();

    float value = vertexHasPointSize() ? in_PointSize : pointSizeArgs.x;

    float scaleC = pointScaleC;
    float scaleB = pointScaleB;
    float scaleA = pointScaleA;

    vec3 vtx3 = vtx.xyz;

    float DeSqr = dot(vtx3, vtx3);
    float De    = sqrt(DeSqr);
    float scaleValue = scaleC * DeSqr;
    scaleValue = scaleB * De + scaleValue;
    scaleValue += scaleA;
    scaleValue = sqrt(scaleValue);
    scaleValue = value / scaleValue;

    value = isPointScalingEnabled() ? scaleValue : value;

    float pointSizeMin = pointSizeArgs.y;
    float pointSizeMax = pointSizeArgs.z;

    return clamp(value, pointSizeMin, pointSizeMax);
}

// ---------------------------------------------------------------------------
// Precise dot / matrix-vector helpers
// ---------------------------------------------------------------------------

// DXVK marks these `precise` to bit-exact match programmable-shader output
// (some games rely on FFP and programmable VS producing identical vertex
// positions, e.g. Railroad Tycoon 3). GLSL ES 3.00 has no `precise`
// qualifier (desktop GLSL 4.00+ only) and no `fma()` (ES 3.10+ only), so the
// fused single-rounding-step guarantee cannot be reproduced here; the
// summation order below is kept identical to the original fma-chain
// (innermost x term first, then + y, then + z, then + w) to stay as close
// as achievable without those two features. See PORTING_NOTES.md
// "Precision: fma / precise".
float dp4(vec4 a, vec4 b) {
    float r = a.x * b.x;
    r = r + a.y * b.y;
    r = r + a.z * b.z;
    r = r + a.w * b.w;
    return r;
}

vec4 vecTimesMat4(vec4 a, mat4 b) {
    return vec4(dp4(a, b[0]), dp4(a, b[1]),
                dp4(a, b[2]), dp4(a, b[3]));
}

float mul_legacy(float a, float b) {
    return (b == 0.0 ? 0.0 : a) * (a == 0.0 ? 0.0 : b);
}

void emitVsClipping(vec4 vtx) {
    vec4 worldPos = data.InverseView * vtx;

    uint clipPlaneCount = getClipPlaneCount();

    // Compute clip distances as a plain varying (see comment at
    // `v_ClipDistance` above and PORTING_NOTES.md "Clip planes") instead of
    // writing DXVK's `gl_ClipDistance[]`.
    for (uint i = 0u; i < MaxClipPlaneCount; i++)
        v_ClipDistance[i] = i < clipPlaneCount ? dp4(worldPos, clipPlanes[i]) : 0.0;
}

vec4 pickMaterialSource(uint source, vec4 material) {
    if (source == D3DMCS_COLOR1 && vertexHasColor0())
        return in_Color0;
    else if (source == D3DMCS_COLOR2 && vertexHasColor1())
        return in_Color1;
    else
        return material;
}

// ---------------------------------------------------------------------------
// Vertex transform (position, tweening, vertex blending / skinning)
// ---------------------------------------------------------------------------

struct Vertex {
    vec4 coord;
    vec4 transformed;
    vec3 normal;
};

Vertex transformVertex() {
    Vertex result;
    result.coord = in_Position0;
    result.normal = in_Normal0.xyz;

    if (blendMode() == D3D9FF_VertexBlendMode_Tween) {
        result.coord = mix(result.coord, in_TweenPositionOrBlendWeight, data.TweenFactor);
        result.normal = mix(result.normal, in_TweenNormalOrBlendIndices.xyz, data.TweenFactor);
    }

    if (!vertexHasPositionT()) {
        if (blendMode() == D3D9FF_VertexBlendMode_Normal) {
            float blendWeightRemaining = 1.0;

            vec4 vtxSum = vec4(0.0);
            vec3 nrmSum = vec3(0.0);

            for (uint i = 0u; i <= vertexBlendCount(); i++) {
                uint arrayIndex = i;

                if (vertexBlendIndexed())
                    arrayIndex = uint(roundEven(in_TweenNormalOrBlendIndices[i]));

                // Clamp defensively: worldViewArray[] is a fixed-size array
                // in this port (see MAX_BLEND_MATRICES above), unlike
                // DXVK's unbounded SSBO.
                arrayIndex = min(arrayIndex, MAX_BLEND_MATRICES - 1u);

                mat4 worldView = worldViewArray[arrayIndex];
                mat3 nrmMtx = mat3(worldView);

                vec4 vtxResult = vecTimesMat4(result.coord, worldView);
                vec3 nrmResult = result.normal * nrmMtx;

                float weight = blendWeightRemaining;

                if (i < vertexBlendCount()) {
                    weight = in_TweenPositionOrBlendWeight[i];
                    blendWeightRemaining -= weight;
                }

                // Was `fma(vtxResult, weight.xxxx, vtxSum)` /
                // `fma(nrmResult, weight.xxx, nrmSum)` in DXVK. `weight` is
                // a scalar float there; scalar swizzle (`.xxxx`/`.xxx`) is
                // not valid GLSL, and `fma()` does not exist in ES 3.00.
                // Rewritten with explicit vector constructors and plain
                // multiply-add.
                vtxSum = vtxResult * vec4(weight) + vtxSum;
                nrmSum = nrmResult * vec3(weight) + nrmSum;
            }

            result.coord = vtxSum;
            result.normal = nrmSum;
            result.transformed = vecTimesMat4(result.coord, data.Projection);
        } else {
            // Apply pre-multiplied world-view-projection matrix, Railroad Tycoon 3
            // relies on this and will break if we apply matrices one by one.
            result.transformed = vecTimesMat4(result.coord, data.WorldViewProj);
            result.coord = vecTimesMat4(result.coord, data.WorldView);
            result.normal = mat3(data.NormalMatrix) * result.normal;
        }

        if (normalizeNormals()) {
            float normalScale = inversesqrt(dot(result.normal, result.normal));
            result.normal.x = mul_legacy(result.normal.x, normalScale);
            result.normal.y = mul_legacy(result.normal.y, normalScale);
            result.normal.z = mul_legacy(result.normal.z, normalScale);
        }

        return result;
    } else {
        // We still need to account for perspective correction here...
        result.transformed = result.coord * data.ViewportInfo.inverseExtent + data.ViewportInfo.inverseOffset;

        float rhw = result.transformed.w == 0.0 ? 1.0 : 1.0 / result.transformed.w;
        result.transformed.xyz *= rhw;
        result.transformed.w = rhw;
        return result;
    }
}

// ---------------------------------------------------------------------------
// Texture coordinate generation / transform
// ---------------------------------------------------------------------------

vec4 loadTexcoord(uint idx) {
    vec4 result = vec4(0.0);
    result = mix(result, in_Texcoord0, bvec4(idx == 0u));
    result = mix(result, in_Texcoord1, bvec4(idx == 1u));
    result = mix(result, in_Texcoord2, bvec4(idx == 2u));
    result = mix(result, in_Texcoord3, bvec4(idx == 3u));
    result = mix(result, in_Texcoord4, bvec4(idx == 4u));
    result = mix(result, in_Texcoord5, bvec4(idx == 5u));
    result = mix(result, in_Texcoord6, bvec4(idx == 6u));
    result = mix(result, in_Texcoord7, bvec4(idx == 7u));
    return result;
}

float vectorExtract(vec4 vector, uint idx) {
    float result = vector.x;
    result = mix(result, vector.y, idx == 1u);
    result = mix(result, vector.z, idx == 2u);
    result = mix(result, vector.w, idx == 3u);
    return result;
}

vec4 transformTexCoord(uint idx, vec4 vertex, vec3 normal) {
    // 0b111 = 7
    uint inputIndex = texcoordIndex(idx);
    uint inputFlags = texcoordFlags(idx) << TCIOffset;
    uint texcoordCount = bitfieldExtract(vertexTexcoordDeclMask(), int(inputIndex) * 3, 3);

    vec4 transformed = vec4(0.0);

    uint flags = texcoordTransformFlags(idx);

    // Passing 0xffffffff results in it getting clamped to the dimensions of the texture coords and getting treated as PROJECTED
    // but D3D9 does not apply the transformation matrix.
    bool applyTransform = flags > D3DTTFF_COUNT1 && flags <= D3DTTFF_COUNT4;

    uint count = min(flags, 4u);

    // A projection component index of 4 means we won't do projection
    uint projIndex = count != 0u ? count - 1u : 4u;

    if (inputFlags == DXVK_TSS_TCI_CAMERASPACENORMAL) {
        transformed = vec4(normal, 1.0);

        if (!applyTransform) {
            count = 3u;
            projIndex = 4u;
        }
    } else if (inputFlags == DXVK_TSS_TCI_CAMERASPACEPOSITION) {
        transformed = vertex;
        if (!applyTransform) {
            count = 3u;
            projIndex = 4u;
        }
    } else if (inputFlags == DXVK_TSS_TCI_CAMERASPACEREFLECTIONVECTOR) {
        vec3 reflection = reflect(normalize(vertex.xyz), normal);
        transformed = vec4(reflection, 1.0);

        if (!applyTransform) {
            count = 3u;
            projIndex = 4u;
        }
    } else if (inputFlags == DXVK_TSS_TCI_SPHEREMAP) {
        vec3 reflection = reflect(normalize(vertex.xyz), normal);

        float m = length(reflection + vec3(0.0, 0.0, 1.0)) * 2.0;
        transformed = vec4(reflection.xy / m + 0.5, 0.0, 1.0);
    } else {
        // DXVK_TSS_TCI_PASSTHRU (default)
        transformed = loadTexcoord(inputIndex & 0xffu);

        if (texcoordCount < 4u) {
            // Vulkan sets the w component to 1.0 if that's not provided by the vertex buffer, D3D9 expects 0 here
            transformed.w = 0.0;
        }

        if (applyTransform && !vertexHasPositionT()) {
            /* This doesn't happen every time and I cannot figure out the difference between when it does and doesn't.
               Keep it disabled for now, it's more likely that games rely on the zero texcoord than the weird 1 here.
               if (texcoordCount <= 1u) {
                   // y gets padded to 1 for some reason
                   transformed.y = 1.0;
               } */

            // The first component after the last one thats backed by a vertex buffer gets padded to 1 for some reason.
            if (texcoordCount >= 1u) {
                // Was `equal(uvec4(0u,1u,2u,3u), texcoordCount.xxxx)` in
                // DXVK — `texcoordCount` is a scalar uint there; scalar
                // swizzle is not valid GLSL. Rewritten with an explicit
                // uvec4 constructor (broadcasts the scalar), identical
                // semantics.
                transformed = mix(transformed, vec4(1.0), equal(uvec4(0u, 1u, 2u, 3u), uvec4(texcoordCount)));
            }
        } else if (texcoordCount != 0u && !applyTransform) {
            // COUNT0, COUNT1, COUNT > 4 => take count from vertex decl if that's not zero
            count = texcoordCount;
        }

        projIndex = count != 0u ? count - 1u : 4u;
    }

    if (applyTransform && !vertexHasPositionT())
        transformed = transformed * data.TexcoordMatrices[idx];

    // Discard the components that exceed the specified D3DTTFF_COUNT. Was
    // `lessThan(count.xxxx, uvec4(1u,2u,3u,4u))` in DXVK (`count` is a
    // scalar uint there); rewritten with an explicit uvec4 constructor for
    // the same scalar-swizzle reason as above.
    vec4 result = mix(transformed, vec4(0.0), lessThan(uvec4(count), uvec4(1u, 2u, 3u, 4u)));

    if (isSamplerProjected(idx) && projIndex < 4u) {
        // The projection idx is always based on the flags, even when the input
        // mode is not DXVK_TSS_TCI_PASSTHRU. The w component is only used for
        // projection or unused, so always insert the divisor there. The pixel
        // shader will then decide whether to project or not.
        result.w = vectorExtract(transformed, projIndex);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

struct Lighting {
    vec4 diffuse;
    vec4 specular;
};

Lighting computeLighting(vec4 vertex, vec3 normal) {
    Lighting result;
    result.diffuse = vertexHasColor0() ? in_Color0 : vec4(1.0, 1.0, 1.0, 1.0);
    result.specular = vertexHasColor1() ? in_Color1 : vec4(0.0, 0.0, 0.0, boundPsIsShaderModel3() ? 0.0 : 1.0);

    // D3DFVF_XYZRHW vertices are already transformed and lit.  D3D9 does
    // not run fixed-function lighting over them even when D3DRS_LIGHTING is
    // globally enabled; Acknex relies on this for its 2D/UI quads.
    if (useLighting() && !vertexHasPositionT()) {
        vec4 diffuseValue = vec4(0.0);
        vec4 specularValue = vec4(0.0);
        vec4 ambientValue = vec4(0.0);

        for (uint i = 0u; i < lightCount(); i++) {
            D3D9Light light = data.Lights[i];

            vec3 delta = light.Position.xyz - vertex.xyz;
            float dist = length(delta);

            // Directional light properties
            vec3 hitDir = -light.Direction.xyz;
            float atten = 1.0;

            if (light.Type != D3DLIGHT_DIRECTIONAL) {
                // Range-based attenuation
                atten = dist * light.Attenuation2 + light.Attenuation1;
                atten = dist * atten + light.Attenuation0;
                atten = 1.0 / atten;
                atten = spvNMin(atten, 3.402823466e+38);
                atten = dist > light.Range ? 0.0 : atten;

                hitDir = normalize(delta);
            }

            if (light.Type == D3DLIGHT_SPOT) {
                // Angle-based attenuation
                float rho = dot(-hitDir, light.Direction.xyz);
                float spotAtten = rho - light.Phi;
                      spotAtten = spotAtten / (light.Theta - light.Phi);
                      spotAtten = pow(spotAtten, light.Falloff);

                bool insideThetaAndPhi = rho <= light.Theta;
                bool insidePhi = rho > light.Phi;
                     spotAtten = insidePhi ? spotAtten : 0.0;
                     spotAtten = insideThetaAndPhi ? spotAtten : 1.0;
                     spotAtten = clamp(spotAtten, 0.0, 1.0);

                atten *= spotAtten;
            }

            // Ambient + Diffuse
            float hitDot = dot(normal, hitDir);
                  hitDot = clamp(hitDot, 0.0, 1.0);

            float diffuseness = hitDot * atten;
            ambientValue += light.Ambient * atten;
            diffuseValue += light.Diffuse * diffuseness;

            // Specular
            vec3 mid;

            if (localViewer()) {
                mid = normalize(vertex.xyz);
                mid = hitDir - mid;
            } else {
                mid = hitDir - vec3(0.0, 0.0, 1.0);
            }

            float midDot = dot(normal, normalize(mid));
                  midDot = clamp(midDot, 0.0, 1.0);

            if (midDot > 0.0 && hitDot > 0.0) {
                float specularness = pow(midDot, data.Material.Power) * atten;
                specularValue += light.Specular * specularness;
            }
        }

        vec4 matDiffuse  = pickMaterialSource(diffuseSource(), data.Material.Diffuse);
        vec4 matAmbient  = pickMaterialSource(ambientSource(), data.Material.Ambient);
        vec4 matEmissive = pickMaterialSource(emissiveSource(), data.Material.Emissive);
        vec4 matSpecular = pickMaterialSource(specularSource(), data.Material.Specular);

        vec4 finalColor0 = matAmbient * decodeD3DColor(data.GlobalAmbient) + matEmissive;
             finalColor0 = matAmbient * ambientValue + finalColor0;
             finalColor0 = matDiffuse * diffuseValue + finalColor0;
             finalColor0.a = matDiffuse.a;

        vec4 finalColor1 = matSpecular * specularValue;

        // Saturate
        finalColor0 = clamp(finalColor0, vec4(0.0), vec4(1.0));
        finalColor1 = clamp(finalColor1, vec4(0.0), vec4(1.0));

        result.diffuse = finalColor0;

        if (isSpecularEnabled())
            result.specular = finalColor1;
    }

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

void main() {
    Vertex vtx = transformVertex();

    // D3D clip-space depth is [0,w], while WebGL uses [-w,w].  Apply the
    // conversion after both the transformed and XYZRHW paths have produced
    // their D3D clip coordinates so perspective-correct interpolation stays
    // intact in either case.
    gl_Position = vec4(
        vtx.transformed.xy,
        2.0 * vtx.transformed.z - vtx.transformed.w,
        vtx.transformed.w);
    gl_PointSize = calculatePointSize(vtx.coord);

    emitVsClipping(vtx.coord);

    v_Normal = vec4(vtx.normal, 1.0);

    v_Texcoord0 = transformTexCoord(0u, vtx.coord, vtx.normal);
    v_Texcoord1 = transformTexCoord(1u, vtx.coord, vtx.normal);
    v_Texcoord2 = transformTexCoord(2u, vtx.coord, vtx.normal);
    v_Texcoord3 = transformTexCoord(3u, vtx.coord, vtx.normal);
    v_Texcoord4 = transformTexCoord(4u, vtx.coord, vtx.normal);
    v_Texcoord5 = transformTexCoord(5u, vtx.coord, vtx.normal);
    v_Texcoord6 = transformTexCoord(6u, vtx.coord, vtx.normal);
    v_Texcoord7 = transformTexCoord(7u, vtx.coord, vtx.normal);

    Lighting lighting = computeLighting(vtx.coord, vtx.normal);
    v_Color0 = lighting.diffuse;
    v_Color1 = lighting.specular;

    v_Fog = calculateFog(vtx.coord);
}
