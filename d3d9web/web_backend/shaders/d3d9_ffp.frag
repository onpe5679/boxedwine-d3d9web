#version 300 es
// d3d9_ffp.frag
// D3D9 Fixed-Function Pipeline (FFP) uber-shader — pixel/fragment stage.
// Ported to GLSL ES 3.00 (WebGL2) from DXVK's hand-written D3D9 FFP shaders.
//
// Source of the ported logic:
//   dxvk (https://github.com/doitsujin/dxvk), src/d3d9/shaders/d3d9_fixed_function_frag.glsl
//   and src/d3d9/shaders/d3d9_fixed_function_common.glsl
//   Copyright (c) 2017 Philip Rebohle, 2019 Joshua Ashton, 2019 Robin
//   Kertels, 2023 Jeffrey Ellison. DXVK is distributed under the
//   zlib/libpng license; see PORTING_NOTES.md for the full, unaltered
//   license notice as required by that license.
//
// This file is NOT a byte-for-byte translation: it targets GLSL ES 3.00,
// which lacks several features the original Vulkan/SPIR-V GLSL relies on
// (bindless/descriptor-indexed samplers, push constants, SSBOs, `fma`,
// `bitfieldExtract`, `precise`, arrays-of-arrays, `demote`). See
// PORTING_NOTES.md for a full list of every deviation and why it was made.
// See UNIFORMS.md for the exact uniform contract the host (JS) side must
// fill in.
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
precision highp sampler2D;
precision highp samplerCube;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const uint TextureStageCount = 8u;
const uint MaxClipPlaneCount = 6u;

#define D3DFOGMODE uint
const uint D3DFOG_NONE   = 0u;
const uint D3DFOG_EXP    = 1u;
const uint D3DFOG_EXP2   = 2u;
const uint D3DFOG_LINEAR = 3u;

// ---------------------------------------------------------------------------
// Polyfills for GLSL ES 3.00 (WebGL2) — see PORTING_NOTES.md "Removed / not
// available in ES 3.00" section for why each of these is needed.
// ---------------------------------------------------------------------------

// bitfieldExtract() is GLSL ES 3.10+, not available in ES 3.00 (WebGL2).
// This polyfill matches the unsigned-value semantics DXVK relies on here
// (this shader only ever extracts unsigned bitfields).
uint bitfieldExtract(uint value, int offset, int bits) {
    if (bits <= 0) return 0u;
    uint off = uint(offset);
    uint b = uint(bits);
    uint mask = (b >= 32u) ? 0xFFFFFFFFu : ((1u << b) - 1u);
    return (value >> off) & mask;
}

// unpackUnorm4x8() is GLSL ES 3.10+, not available in ES 3.00. D3DCOLOR is
// packed as 0xAARRGGBB (A most-significant byte, B least-significant byte).
// This directly returns (r,g,b,a), replacing DXVK's
// `unpackUnorm4x8(color).bgra` (unpackUnorm4x8 yields components in
// least-significant-byte-first order, i.e. (B,G,R,A); swizzling .bgra on
// that gives (R,G,B,A) — the same result computed directly below).
vec4 decodeD3DColor(uint color) {
    float a = float((color >> 24u) & 0xFFu) * (1.0 / 255.0);
    float r = float((color >> 16u) & 0xFFu) * (1.0 / 255.0);
    float g = float((color >> 8u)  & 0xFFu) * (1.0 / 255.0);
    float b = float( color         & 0xFFu) * (1.0 / 255.0);
    return vec4(r, g, b, a);
}

// spvNMin/spvNClamp were DXVK's SPIRV-Cross-generated wrappers around the
// GLSL.std.450 NMin/NClamp NaN-propagating extended instructions. Plain
// min()/clamp() differ only in unspecified behaviour when a NaN operand is
// present; see PORTING_NOTES.md.
float spvNClamp(float x, float lo, float hi) { return clamp(x, lo, hi); }

// ---------------------------------------------------------------------------
// Varyings (must match d3d9_ffp.vert locations)
// ---------------------------------------------------------------------------

// NOTE: no explicit layout(location=N) here -- see d3d9_ffp.vert's matching
// comment and PORTING_NOTES.md "Real-driver compile fixes -- varying
// locations". Core GLSL ES 3.00 (WebGL2) rejects explicit `location` on
// inter-stage varyings (confirmed against a real WebGL2/ANGLE driver); these
// must instead be matched to d3d9_ffp.vert's outputs purely by name, hence
// the shared `v_` names below (matching the pre-existing `v_ClipDistance`
// convention, which already worked this way).
in vec4 v_Normal;
in vec4 v_Texcoord0;
in vec4 v_Texcoord1;
in vec4 v_Texcoord2;
in vec4 v_Texcoord3;
in vec4 v_Texcoord4;
in vec4 v_Texcoord5;
in vec4 v_Texcoord6;
in vec4 v_Texcoord7;
in vec4 v_Color0;
in vec4 v_Color1;
in float v_Fog;

// Replacement for DXVK's `gl_ClipDistance[]` hardware clip planes. WebGL2
// only exposes clip distances via the non-universal WEBGL_clip_cull_distance
// extension, so this port instead forwards per-plane distances as an
// ordinary varying and discards in the fragment shader. See
// PORTING_NOTES.md "Clip planes".
in float v_ClipDistance[MaxClipPlaneCount];

out vec4 out_Color0;

// ---------------------------------------------------------------------------
// Uniform blocks — see UNIFORMS.md for the full host-side contract.
// ---------------------------------------------------------------------------

layout(std140) uniform FFPixelState {
    // Packed bitfields — identical layout to DXVK's SpecConsts, minus the
    // fields that shader never reads on the pixel side.
    uint alphaTestAndModeArgsFS;
    uint fogArgsFS;
    uint packedProjMaskAndFfArgsFS;
    uint psSamplerTypes;
    uint psSamplerModes;
    uint packedStageOps01;
    uint packedStageOps23;
    uint packedStageOps45;
    uint packedStageOps67;
    uint packedStageArgs0;
    uint packedStageArgs1;
    uint packedStageArgs2;
    uint packedStageArgs3;
    uint packedStageArgs4;
    uint packedStageArgs5;
    uint packedStageArgs6;
    uint packedStageArgs7;

    // D3D9SharedPushData equivalent
    uint  packedFogColorAndAlphaRefFS;
    float fogDistanceScaleFS;
    float fogDistanceEndFS;
    float fogDensityFS;

    // D3D9FfpsPushData equivalent
    uint textureFactor;
};

struct D3D9SharedPSStage {
    uint  Constant;
    // Was `float BumpEnvMat[2][2]` in DXVK. GLSL ES 3.00 has no
    // arrays-of-arrays (that needs ES 3.10+ / GL_ARB_arrays_of_arrays), so
    // the 2x2 matrix is flattened to 4 scalars. RowCol naming: `RC` where R
    // is the row and C the column, matching DXVK's BumpEnvMat[R][C].
    float BumpEnvMat00;
    float BumpEnvMat01;
    float BumpEnvMat10;
    float BumpEnvMat11;
    float BumpEnvLScale;
    float BumpEnvLOffset;
};

layout(std140) uniform FFPixelStages {
    D3D9SharedPSStage stages[8]; // TextureStageCount
};

// Per-stage textures. DXVK indexes a bindless/descriptor-indexed sampler
// array (`t2d[]`/`tcube[]`/`t3d[]` + `sampler_heap[]`); WebGL2 disallows
// non-uniform/dynamic sampler-array indexing, so every stage gets its own
// statically-typed, statically-bound sampler uniform instead (see
// PORTING_NOTES.md "Bindless samplers"). Volume (3D) textures are not
// implemented — see PORTING_NOTES.md "Dropped features".
uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
uniform sampler2D uTexture4;
uniform sampler2D uTexture5;
uniform sampler2D uTexture6;
uniform sampler2D uTexture7;

uniform samplerCube uTextureCube0;
uniform samplerCube uTextureCube1;
uniform samplerCube uTextureCube2;
uniform samplerCube uTextureCube3;
uniform samplerCube uTextureCube4;
uniform samplerCube uTextureCube5;
uniform samplerCube uTextureCube6;
uniform samplerCube uTextureCube7;

// ---------------------------------------------------------------------------
// Alpha test
// ---------------------------------------------------------------------------

const uint VK_COMPARE_OP_NEVER             = 0u;
const uint VK_COMPARE_OP_LESS              = 1u;
const uint VK_COMPARE_OP_EQUAL             = 2u;
const uint VK_COMPARE_OP_LESS_OR_EQUAL     = 3u;
const uint VK_COMPARE_OP_GREATER           = 4u;
const uint VK_COMPARE_OP_NOT_EQUAL         = 5u;
const uint VK_COMPARE_OP_GREATER_OR_EQUAL  = 6u;
const uint VK_COMPARE_OP_ALWAYS            = 7u;

struct AlphaTestState {
    uint compareOp;
    uint precisionBits;
};

AlphaTestState getAlphaTestState() {
    AlphaTestState result;
    result.compareOp = bitfieldExtract(alphaTestAndModeArgsFS, 0, 3);
    result.precisionBits = bitfieldExtract(alphaTestAndModeArgsFS, 4, 4);
    return result;
}

// ---------------------------------------------------------------------------
// Sampler state
// ---------------------------------------------------------------------------

const uint TEXTURE_TYPE_2D         = 0u;
const uint TEXTURE_TYPE_3D         = 1u;
const uint TEXTURE_TYPE_CUBE       = 2u;
const uint TEXTURE_TYPE_NULL       = 3u;

const uint SAMPLER_MODE_DEFAULT    = 0u;
const uint SAMPLER_MODE_FETCH4     = 1u;
const uint SAMPLER_MODE_DREF       = 2u;
const uint SAMPLER_MODE_DREF_CLAMP = 3u;

struct SamplerState {
    bool projected;
    uint type;
    uint mode;
    float drefScale;
};

SamplerState getSamplerState(uint idx) {
    uint drefScaleShift = bitfieldExtract(alphaTestAndModeArgsFS, 24, 8);

    SamplerState result;
    result.projected = bitfieldExtract(packedProjMaskAndFfArgsFS, int(idx), 1) != 0u;
    result.type = bitfieldExtract(psSamplerTypes, int(idx + idx), 2);
    result.mode = bitfieldExtract(psSamplerModes, int(idx + idx), 2);
    result.drefScale = 1.0 / max(1.0, float(1u << drefScaleShift) - 1.0);
    return result;
}

// ---------------------------------------------------------------------------
// Fixed-function texture stage parameters
// ---------------------------------------------------------------------------

const uint D3DTOP_DISABLE                   = 1u;
const uint D3DTOP_SELECTARG1                = 2u;
const uint D3DTOP_SELECTARG2                = 3u;
const uint D3DTOP_MODULATE                  = 4u;
const uint D3DTOP_MODULATE2X                = 5u;
const uint D3DTOP_MODULATE4X                = 6u;
const uint D3DTOP_ADD                       = 7u;
const uint D3DTOP_ADDSIGNED                 = 8u;
const uint D3DTOP_ADDSIGNED2X               = 9u;
const uint D3DTOP_SUBTRACT                  = 10u;
const uint D3DTOP_ADDSMOOTH                 = 11u;
const uint D3DTOP_BLENDDIFFUSEALPHA         = 12u;
const uint D3DTOP_BLENDTEXTUREALPHA         = 13u;
const uint D3DTOP_BLENDFACTORALPHA          = 14u;
const uint D3DTOP_BLENDTEXTUREALPHAPM       = 15u;
const uint D3DTOP_BLENDCURRENTALPHA         = 16u;
const uint D3DTOP_PREMODULATE               = 17u;
const uint D3DTOP_MODULATEALPHA_ADDCOLOR    = 18u;
const uint D3DTOP_MODULATECOLOR_ADDALPHA    = 19u;
const uint D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20u;
const uint D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21u;
const uint D3DTOP_BUMPENVMAP                = 22u;
const uint D3DTOP_BUMPENVMAPLUMINANCE       = 23u;
const uint D3DTOP_DOTPRODUCT3               = 24u;
const uint D3DTOP_MULTIPLYADD               = 25u;
const uint D3DTOP_LERP                      = 26u;

// These differ from the actual D3D definitions: DXVK shifts the modifiers
// one bit to the right since only 7 argument selector values are defined.
// Kept identical here for ABI compatibility with the packed uniform data.
const uint D3DTA_SELECTMASK                 = 0x07u;

const uint D3DTA_DIFFUSE                    = 0x00u;
const uint D3DTA_CURRENT                    = 0x01u;
const uint D3DTA_TEXTURE                    = 0x02u;
const uint D3DTA_TFACTOR                    = 0x03u;
const uint D3DTA_SPECULAR                   = 0x04u;
const uint D3DTA_TEMP                       = 0x05u;
const uint D3DTA_CONSTANT                   = 0x06u;

const uint D3DTA_COMPLEMENT                 = 0x08u;
const uint D3DTA_ALPHAREPLICATE             = 0x10u;

struct TextureStageArguments {
    uint arg0;
    uint arg1;
    uint arg2;
};

struct TextureStage {
    bool storeToTemp;
    uint colorOp;
    uint alphaOp;
    TextureStageArguments colorArgs;
    TextureStageArguments alphaArgs;
};

TextureStage getTextureStage(uint idx) {
    uint ops = 0u;
    uint args = 0u;

    switch (idx) {
        case 0u: ops = packedStageOps01 >>  0; args = packedStageArgs0; break;
        case 1u: ops = packedStageOps01 >> 16; args = packedStageArgs1; break;
        case 2u: ops = packedStageOps23 >>  0; args = packedStageArgs2; break;
        case 3u: ops = packedStageOps23 >> 16; args = packedStageArgs3; break;
        case 4u: ops = packedStageOps45 >>  0; args = packedStageArgs4; break;
        case 5u: ops = packedStageOps45 >> 16; args = packedStageArgs5; break;
        case 6u: ops = packedStageOps67 >>  0; args = packedStageArgs6; break;
        case 7u: ops = packedStageOps67 >> 16; args = packedStageArgs7; break;
    }

    TextureStage result;
    result.storeToTemp = bitfieldExtract(ops, 15, 1) != 0u;
    result.colorOp = bitfieldExtract(ops, 0, 5);
    result.alphaOp = bitfieldExtract(ops, 8, 5);
    result.colorArgs.arg0 = bitfieldExtract(args,  0, 5);
    result.colorArgs.arg1 = bitfieldExtract(args,  5, 5);
    result.colorArgs.arg2 = bitfieldExtract(args, 10, 5);
    result.alphaArgs.arg0 = bitfieldExtract(args, 16, 5);
    result.alphaArgs.arg1 = bitfieldExtract(args, 21, 5);
    result.alphaArgs.arg2 = bitfieldExtract(args, 26, 5);
    return result;
}

// Checks whether point sprites are enabled
bool isPointSpriteEnabled() {
    return bitfieldExtract(alphaTestAndModeArgsFS, 8, 1) != 0u;
}

// Checks whether specular lighting is enabled
bool isSpecularEnabled() {
    return bitfieldExtract(packedProjMaskAndFfArgsFS, 8, 1) != 0u;
}

// ---------------------------------------------------------------------------
// Fog
// ---------------------------------------------------------------------------

struct FogState {
    bool enable;
    uint pixelMode;
    uint vertexMode;
    bool useZ;
};

FogState getFogState() {
    FogState result;
    result.enable = bitfieldExtract(fogArgsFS, 0, 1) != 0u;
    result.vertexMode = bitfieldExtract(fogArgsFS, 8, 2);
    result.pixelMode = bitfieldExtract(fogArgsFS, 16, 2);
    result.useZ = bitfieldExtract(fogArgsFS, 24, 1) != 0u;
    return result;
}

vec4 calculateFog(vec4 oColor) {
    FogState fogState = getFogState();

    vec3 fogColor = decodeD3DColor(packedFogColorAndAlphaRefFS).rgb;
    float fogScale = fogDistanceScaleFS;
    float fogEnd = fogDistanceEndFS;
    float fogDens = fogDensityFS;

    if (!fogState.enable)
        return oColor;

    float depth = fogState.useZ ? gl_FragCoord.z : (1.0 / gl_FragCoord.w);
    float fogFactor;

    if (fogState.pixelMode == D3DFOG_NONE) {
        fogFactor = v_Fog;
    } else if (fogState.pixelMode == D3DFOG_LINEAR) {
        // (end - d) / (end - start), pre-baked into fogScale on the host side
        fogFactor = fogEnd - depth;
        fogFactor = fogFactor * fogScale;
    } else {
        // D3DFOG_EXP2: 1 / (e^[d * density])^2
        // D3DFOG_EXP:  1 / (e^[d * density])
        fogFactor = depth * fogDens;

        if (fogState.pixelMode == D3DFOG_EXP2)
            fogFactor *= fogFactor;

        // Provides the rcp.
        fogFactor = -fogFactor;
        fogFactor = exp(fogFactor);
    }

    fogFactor = spvNClamp(fogFactor, 0.0, 1.0);

    vec3 color3 = oColor.rgb;
    vec3 lerped = mix(fogColor, color3, vec3(fogFactor));
    return vec4(lerped, oColor.a);
}

// ---------------------------------------------------------------------------
// Bump mapping
// ---------------------------------------------------------------------------

vec4 calculateBumpmapCoords(uint stage, vec4 baseCoords, vec4 previousStageTextureVal) {
    uint previousStage = stage - 1u;
    vec2 t = previousStageTextureVal.xy;

    D3D9SharedPSStage prev = stages[previousStage];

    vec4 coords = baseCoords;
    // Was a `[[unroll]] for (uint i = 0; i < 2; i++)` loop over
    // BumpEnvMat[i][0..1] in DXVK; manually unrolled here since the
    // BumpEnvMat storage was flattened (see D3D9SharedPSStage above) and
    // `[[unroll]]` (GL_EXT_control_flow_attributes) does not exist in ES 3.00.
    coords.x = coords.x + dot(vec2(prev.BumpEnvMat00, prev.BumpEnvMat01), t);
    coords.y = coords.y + dot(vec2(prev.BumpEnvMat10, prev.BumpEnvMat11), t);
    return coords;
}

// ---------------------------------------------------------------------------
// Texture sampling
// ---------------------------------------------------------------------------

float adjustDref(float dref, SamplerState state) {
    dref *= state.drefScale;

    if (state.mode == SAMPLER_MODE_DREF_CLAMP)
        dref = clamp(dref, 0.0, 1.0);

    return dref;
}

// Per-stage dispatch helpers, replacing DXVK's `t2d[stage]`/`tcube[stage]`
// descriptor-array indexing. `stage` is always a compile-time-constant
// literal at every call site in main() (the 8 stages are unrolled), so this
// switch resolves to a single direct sample in practice; it also sidesteps
// WebGL2/ANGLE's restrictions on dynamically-indexed sampler arrays entirely
// by never declaring a sampler array in the first place.
vec4 fetchStage2D(uint stage, vec2 uv) {
    switch (stage) {
        case 0u: return texture(uTexture0, uv);
        case 1u: return texture(uTexture1, uv);
        case 2u: return texture(uTexture2, uv);
        case 3u: return texture(uTexture3, uv);
        case 4u: return texture(uTexture4, uv);
        case 5u: return texture(uTexture5, uv);
        case 6u: return texture(uTexture6, uv);
        case 7u: return texture(uTexture7, uv);
        default: return vec4(0.0);
    }
}

vec4 fetchStageCube(uint stage, vec3 uv) {
    switch (stage) {
        case 0u: return texture(uTextureCube0, uv);
        case 1u: return texture(uTextureCube1, uv);
        case 2u: return texture(uTextureCube2, uv);
        case 3u: return texture(uTextureCube3, uv);
        case 4u: return texture(uTextureCube4, uv);
        case 5u: return texture(uTextureCube5, uv);
        case 6u: return texture(uTextureCube6, uv);
        case 7u: return texture(uTextureCube7, uv);
        default: return vec4(0.0);
    }
}

vec4 sampleTexture(uint stage, vec4 texcoord, vec4 previousStageTextureVal) {
    SamplerState state = getSamplerState(stage);

    if (state.projected)
        texcoord /= texcoord.w;

    uint previousStageColorOp = 0u;

    if (stage > 0u) {
        previousStageColorOp = getTextureStage(stage - 1u).colorOp;

        if (previousStageColorOp == D3DTOP_BUMPENVMAP || previousStageColorOp == D3DTOP_BUMPENVMAPLUMINANCE)
            texcoord = calculateBumpmapCoords(stage, texcoord, previousStageTextureVal);
    }

    // We can encounter null textures in certain scenarios where only alpha
    // samples the texture, or the color op itself implicitly samples. For
    // some reason this appears to return a null vector on native.
    vec4 texVal = vec4(0.0);

    if (state.type == TEXTURE_TYPE_2D) {
        if (state.mode >= SAMPLER_MODE_DREF) {
            // DXVK uses a hardware sampler2DShadow here. WebGL2 does support
            // shadow samplers, but giving every stage both a regular and a
            // shadow sampler would double the texture-unit budget (already
            // at the WebGL2-guaranteed minimum of 16, see UNIFORMS.md); this
            // is therefore a manual, PCF-less depth compare against a
            // normally-sampled texture instead. NOT VERIFIED against real
            // content — DREF sampling on the FFP path is a rare D3D9 hack
            // (self-sampling a depth texture) and Azrok Plaza has not been
            // confirmed to use it. See PORTING_NOTES.md.
            float dref = adjustDref(texcoord.z, state);
            float depth = fetchStage2D(stage, texcoord.xy).x;
            texVal = vec4(depth >= dref ? 1.0 : 0.0);
        } else {
            texVal = fetchStage2D(stage, texcoord.xy);
        }
    } else if (state.type == TEXTURE_TYPE_CUBE) {
        if (state.mode >= SAMPLER_MODE_DREF) {
            float dref = adjustDref(texcoord.w, state);
            float depth = fetchStageCube(stage, texcoord.xyz).x;
            texVal = vec4(depth >= dref ? 1.0 : 0.0);
        } else {
            texVal = fetchStageCube(stage, texcoord.xyz);
        }
    } else if (state.type == TEXTURE_TYPE_3D) {
        // Volume (3D) textures are NOT implemented in this port — dropped to
        // stay within the WebGL2-guaranteed minimum texture-unit budget.
        // See PORTING_NOTES.md "Dropped features". Falls through to the
        // same "null texture" vec4(0.0) behaviour as TEXTURE_TYPE_NULL.
        texVal = vec4(0.0);
    }
    // TEXTURE_TYPE_NULL falls through with texVal == vec4(0.0), matching
    // DXVK's documented native behaviour for null textures.

    if (stage != 0u && previousStageColorOp == D3DTOP_BUMPENVMAPLUMINANCE) {
        float lScale = stages[stage - 1u].BumpEnvLScale;
        float lOffset = stages[stage - 1u].BumpEnvLOffset;
        float scale = texVal.z;
        scale = scale * lScale + lOffset;
        scale = clamp(scale, 0.0, 1.0);
        texVal *= scale;
    }

    return texVal;
}

// ---------------------------------------------------------------------------
// Argument / stage-op evaluation
// ---------------------------------------------------------------------------

vec4 readArgValue(uint stage, uint arg, vec4 current, vec4 temp, vec4 textureVal, bool premodulate) {
    vec4 reg = vec4(1.0);
    switch (arg & D3DTA_SELECTMASK) {
        case D3DTA_CONSTANT:
            reg = decodeD3DColor(stages[stage].Constant);
            break;
        case D3DTA_CURRENT:
            // Was `mix(current, current * textureVal, premodulate.xxxx)` —
            // `premodulate` is a scalar bool in DXVK's source; scalar swizzle
            // is not valid GLSL ES 3.00 (or, per spec, standard GLSL at
            // all). Rewritten with an explicit bvec4 constructor, which is
            // guaranteed-portable and has identical semantics (broadcast the
            // scalar to all four components).
            reg = mix(current, current * textureVal, bvec4(premodulate));
            break;
        case D3DTA_DIFFUSE:
            reg = v_Color0;
            break;
        case D3DTA_SPECULAR:
            reg = v_Color1;
            break;
        case D3DTA_TEMP:
            reg = temp;
            break;
        case D3DTA_TEXTURE:
            reg = textureVal;
            break;
        case D3DTA_TFACTOR:
            reg = decodeD3DColor(textureFactor);
            break;
    }

    // reg = 1 - reg
    if ((arg & D3DTA_COMPLEMENT) != 0u)
        reg = vec4(1.0) - reg;

    // reg = reg.wwww
    if ((arg & D3DTA_ALPHAREPLICATE) != 0u)
        reg = reg.aaaa;

    return reg;
}

struct TextureStageArgumentValues {
    vec4 arg0;
    vec4 arg1;
    vec4 arg2;
};

TextureStageArgumentValues readArgValues(uint stage, TextureStageArguments args, vec4 current, vec4 temp, vec4 textureVal, bool premodulate) {
    TextureStageArgumentValues argVals;
    argVals.arg0 = readArgValue(stage, args.arg0, current, temp, textureVal, premodulate);
    argVals.arg1 = readArgValue(stage, args.arg1, current, temp, textureVal, premodulate);
    argVals.arg2 = readArgValue(stage, args.arg2, current, temp, textureVal, premodulate);
    return argVals;
}

vec4 complement(vec4 val) {
    return vec4(1.0) - val;
}

vec4 saturate(vec4 val) {
    return clamp(val, vec4(0.0), vec4(1.0));
}

// All `fma(a, b, c)` calls in the original were rewritten to plain
// `a * b + c` — GLSL ES 3.00 does not have `fma()` (added in ES 3.10). This
// only loses the single-rounding-step precision guarantee of a truly fused
// multiply-add; see PORTING_NOTES.md "Precision: fma / precise".
vec4 calculateTextureStage(uint op, vec4 dst, TextureStageArgumentValues arg, vec4 current, vec4 textureVal) {
    if (op == D3DTOP_SELECTARG1) {
        return arg.arg1;
    } else if (op == D3DTOP_SELECTARG2) {
        return arg.arg2;
    } else if (op == D3DTOP_MODULATE4X) {
        return saturate(arg.arg1 * arg.arg2 * 4.0);
    } else if (op == D3DTOP_MODULATE2X) {
        return saturate(arg.arg1 * arg.arg2 * 2.0);
    } else if (op == D3DTOP_MODULATE) {
        return saturate(arg.arg1 * arg.arg2);
    } else if (op == D3DTOP_ADDSIGNED2X) {
        return saturate(2.0 * (arg.arg1 + (arg.arg2 - vec4(0.5))));
    } else if (op == D3DTOP_ADDSIGNED) {
        return saturate(arg.arg1 + (arg.arg2 - vec4(0.5)));
    } else if (op == D3DTOP_ADD) {
        return saturate(arg.arg1 + arg.arg2);
    } else if (op == D3DTOP_SUBTRACT) {
        return saturate(arg.arg1 - arg.arg2);
    } else if (op == D3DTOP_ADDSMOOTH) {
        return saturate(complement(arg.arg1) * arg.arg2 + arg.arg1);
    } else if (op == D3DTOP_BLENDDIFFUSEALPHA) {
        return mix(arg.arg2, arg.arg1, v_Color0.aaaa);
    } else if (op == D3DTOP_BLENDTEXTUREALPHA) {
        return mix(arg.arg2, arg.arg1, textureVal.aaaa);
    } else if (op == D3DTOP_BLENDFACTORALPHA) {
        return mix(arg.arg2, arg.arg1, decodeD3DColor(textureFactor).aaaa);
    } else if (op == D3DTOP_BLENDTEXTUREALPHAPM) {
        return saturate(arg.arg2 * complement(textureVal.aaaa) + arg.arg1);
    } else if (op == D3DTOP_BLENDCURRENTALPHA) {
        return mix(arg.arg2, arg.arg1, current.aaaa);
    } else if (op == D3DTOP_PREMODULATE) {
        return arg.arg1;
    } else if (op == D3DTOP_MODULATEALPHA_ADDCOLOR) {
        return saturate(arg.arg1.aaaa * arg.arg2 + arg.arg1);
    } else if (op == D3DTOP_MODULATECOLOR_ADDALPHA) {
        return saturate(arg.arg1 * arg.arg2 + arg.arg1.aaaa);
    } else if (op == D3DTOP_MODULATEINVALPHA_ADDCOLOR) {
        return saturate(complement(arg.arg1.aaaa) * arg.arg2 + arg.arg1);
    } else if (op == D3DTOP_MODULATEINVCOLOR_ADDALPHA) {
        return saturate(complement(arg.arg1) * arg.arg2 + arg.arg1.aaaa);
    } else if (op == D3DTOP_BUMPENVMAPLUMINANCE || op == D3DTOP_BUMPENVMAP) {
        // Load texture for the next stage...
        return dst;
    } else if (op == D3DTOP_DOTPRODUCT3) {
        return saturate(vec4(dot(arg.arg1.rgb - vec3(0.5), arg.arg2.rgb - vec3(0.5)) * 4.0));
    } else if (op == D3DTOP_MULTIPLYADD) {
        return saturate(arg.arg1 * arg.arg2 + arg.arg0);
    } else if (op == D3DTOP_LERP) {
        return mix(arg.arg2, arg.arg1, arg.arg0);
    }

    // Unhandled texture op! (also covers D3DTOP_DISABLE, handled earlier by
    // the caller, and any reserved/unused opcode value.)
    return dst;
}

// ---------------------------------------------------------------------------
// Alpha test
// ---------------------------------------------------------------------------

bool alphaTest(float alpha) {
    AlphaTestState test = getAlphaTestState();

    uint alphaRefInitial = bitfieldExtract(packedFogColorAndAlphaRefFS, 24, 8);
    float alphaRef;

    if (test.compareOp < VK_COMPARE_OP_ALWAYS) {
        // Check if the given bit precision is supported
        bool useIntPrecision = test.precisionBits <= 8u;

        if (useIntPrecision) {
            // Adjust alpha ref to the given range
            uint alphaRefInt = (alphaRefInitial << test.precisionBits) | (alphaRefInitial >> (8u - test.precisionBits));

            // Convert alpha ref to float since we'll do the comparison based on that
            alphaRef = float(alphaRefInt);

            // Adjust alpha to the given range and round
            float alphaFactor = float((256u << test.precisionBits) - 1u);

            alpha = roundEven(alpha * alphaFactor);
        } else {
            alphaRef = float(alphaRefInitial) / 255.0;
        }

        if (test.compareOp == VK_COMPARE_OP_LESS) {
            return alpha < alphaRef;
        } else if (test.compareOp == VK_COMPARE_OP_EQUAL) {
            return alpha == alphaRef;
        } else if (test.compareOp == VK_COMPARE_OP_LESS_OR_EQUAL) {
            return alpha <= alphaRef;
        } else if (test.compareOp == VK_COMPARE_OP_GREATER) {
            return alpha > alphaRef;
        } else if (test.compareOp == VK_COMPARE_OP_NOT_EQUAL) {
            return alpha != alphaRef;
        } else if (test.compareOp == VK_COMPARE_OP_GREATER_OR_EQUAL) {
            return alpha >= alphaRef;
        } else {
            return false; // NEVER
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Clip plane discard (see PORTING_NOTES.md "Clip planes")
// ---------------------------------------------------------------------------

void applyClipPlanes() {
    for (uint i = 0u; i < MaxClipPlaneCount; i++) {
        if (v_ClipDistance[i] < 0.0)
            discard;
    }
}

// ---------------------------------------------------------------------------
// Texture stage loop
// ---------------------------------------------------------------------------

struct TextureStageState {
    vec4 current;
    vec4 temp;
    vec4 previousStageTextureVal;
    bool premodulateColor;
    bool premodulateAlpha;
};

vec4 getTexCoord(uint stage) {
    // If point sprites are enabled, we need to replace the
    // input texture coordinate with the point coordinate
    if (isPointSpriteEnabled())
        return vec4(gl_PointCoord, 0.0, 0.0);

    switch (stage) {
        case 0u: return v_Texcoord0;
        case 1u: return v_Texcoord1;
        case 2u: return v_Texcoord2;
        case 3u: return v_Texcoord3;
        case 4u: return v_Texcoord4;
        case 5u: return v_Texcoord5;
        case 6u: return v_Texcoord6;
        case 7u: return v_Texcoord7;
    }

    return vec4(0.0);
}

TextureStageState runTextureStage(uint stage, TextureStageState state) {
    TextureStage info = getTextureStage(stage);

    // This cancels all subsequent stages.
    if (info.colorOp == D3DTOP_DISABLE)
        return state;

    vec4 prev = info.storeToTemp ? state.temp : state.current;
    vec4 next = prev;

    vec4 textureVal = sampleTexture(stage, getTexCoord(stage), state.previousStageTextureVal);

    // Fast path if alpha/color path is identical.
    // D3DTOP_DOTPRODUCT3 also has special quirky behaviour here.
    bool fastPath = info.colorOp == info.alphaOp &&
        info.colorArgs.arg0 == info.alphaArgs.arg0 &&
        info.colorArgs.arg1 == info.alphaArgs.arg1 &&
        info.colorArgs.arg2 == info.alphaArgs.arg2 &&
        state.premodulateColor == state.premodulateAlpha;

    if (fastPath || info.colorOp == D3DTOP_DOTPRODUCT3) {
        TextureStageArgumentValues colorArgVals = readArgValues(stage, info.colorArgs, state.current, state.temp, textureVal, state.premodulateColor);
        next = calculateTextureStage(info.colorOp, prev, colorArgVals, state.current, textureVal);
    } else {
        TextureStageArgumentValues colorArgVals = readArgValues(stage, info.colorArgs, state.current, state.temp, textureVal, state.premodulateColor);
        next.rgb = calculateTextureStage(info.colorOp, prev, colorArgVals, state.current, textureVal).rgb;

        if (info.alphaOp != D3DTOP_DISABLE) {
            TextureStageArgumentValues alphaArgVals = readArgValues(stage, info.alphaArgs, state.current, state.temp, textureVal, state.premodulateAlpha);
            next.a = calculateTextureStage(info.alphaOp, prev, alphaArgVals, state.current, textureVal).a;
        }
    }

    state.temp = info.storeToTemp ? next : state.temp;
    state.current = info.storeToTemp ? state.current : next;
    state.previousStageTextureVal = textureVal;
    state.premodulateColor = info.colorOp == D3DTOP_PREMODULATE;
    state.premodulateAlpha = info.alphaOp == D3DTOP_PREMODULATE;
    return state;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

void main() {
    applyClipPlanes();

    // v_Color0 is diffuse
    // v_Color1 is specular

    TextureStageState state;
    // Current starts of as equal to diffuse.
    state.current = v_Color0;
    // Temp starts off as equal to vec4(0)
    state.temp = vec4(0.0);
    state.previousStageTextureVal = vec4(0.0);
    state.premodulateColor = false;
    state.premodulateAlpha = false;

    // Unrolled (matches DXVK's own manual unroll, done for the same reason:
    // a real loop here was observed by DXVK to unroll poorly on some
    // drivers; ANGLE/WebGL2 drivers are at least as likely to have the same
    // issue, if not worse, so the manual unroll is kept as-is).
    state = runTextureStage(0u, state);
    state = runTextureStage(1u, state);
    state = runTextureStage(2u, state);
    state = runTextureStage(3u, state);
    state = runTextureStage(4u, state);
    state = runTextureStage(5u, state);
    state = runTextureStage(6u, state);
    state = runTextureStage(7u, state);

    if (isSpecularEnabled())
        state.current.xyz += v_Color1.xyz;

    state.current = calculateFog(state.current);

    out_Color0 = state.current;

    // DXVK uses `demote` (GL_EXT_demote_to_helper_invocation) here, which
    // keeps the invocation alive as a helper invocation for derivative
    // purposes. Nothing after this point uses derivatives (no dFdx/dFdy,
    // no textureGrad), so a plain `discard` is behaviourally equivalent for
    // this shader. See PORTING_NOTES.md "demote -> discard".
    if (!alphaTest(state.current.a))
        discard;
}
