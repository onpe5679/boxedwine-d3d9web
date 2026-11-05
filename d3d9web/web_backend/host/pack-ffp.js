// pack-ffp.js
//
// Packs a ffp-state.js state object into std140 UBO bytes for each of the
// 5 uniform blocks declared by ../shaders/d3d9_ffp.vert /
// d3d9_ffp.frag (FFVertexData, FFClipPlanes, FFBlendMatrices on the vertex
// side; FFPixelState, FFPixelStages on the fragment side). Byte offsets
// are always obtained via std140.js's runtime introspection against the
// actual linked `program` passed in -- nothing here hardcodes a std140
// offset, per ../shaders/UNIFORMS.md section 0.
//
// This module does not invent a command ABI: it only answers "given this
// state object, what bytes go in this UBO", for a caller that already has
// a linked WebGL2 program. Where the state comes from (a D3D9 state
// bridge) is out of scope.

'use strict';

import {
  getBlockInfo,
  queryUniformLayout,
  elementOffset,
  writeF32,
  writeU32,
  writeVec4,
  writeMat4,
} from './std140.js';
import {
  packD3DColor,
  packAlphaTestAndModeArgs,
  packFogArgs,
  packProjMaskAndFfArgs,
  packPerStage2Bit,
  packStageArg,
  packStageOp16,
  packStageOpsPair,
  packStageArgs32,
  packReservedAndPointSize,
  packPointSizeMinMax,
  packDataPrimitives,
  dataPrimitivesToDwords,
} from './bitpack.js';
import { MAX_BLEND_MATRICES } from './ffp-constants.js';

const LIGHT_FIELDS = [
  'Diffuse',
  'Specular',
  'Ambient',
  'Position',
  'Direction',
  'Type',
  'Range',
  'Falloff',
  'Attenuation0',
  'Attenuation1',
  'Attenuation2',
  'Theta',
  'Phi',
];

// std140 reflection is immutable for the lifetime of a linked WebGLProgram.
// Preserve the runtime driver query contract, but perform it once per program
// instead of repeating metadata lookups for every D3D draw.
const programLayouts = new WeakMap();

function cachedBlockLayout(gl, program, cacheKey, blockName, names) {
  let layouts = programLayouts.get(program);
  if (!layouts) {
    layouts = new Map();
    programLayouts.set(program, layouts);
  }
  let entry = layouts.get(cacheKey);
  if (!entry) {
    entry = {
      block: getBlockInfo(gl, program, blockName),
      uniforms: queryUniformLayout(gl, program, names),
    };
    layouts.set(cacheKey, entry);
  }
  return entry;
}

function packStageArgTriplet(argTriplet) {
  return argTriplet.map((a) => packStageArg(a));
}

// --- FFVertexData -------------------------------------------------------

function ffVertexDataNames() {
  const names = [
    'data.WorldView',
    'data.NormalMatrix',
    'data.InverseView',
    'data.Projection',
    'data.WorldViewProj',
    'data.ViewportInfo.inverseOffset',
    'data.ViewportInfo.inverseExtent',
    'data.Material.Diffuse',
    'data.Material.Ambient',
    'data.Material.Specular',
    'data.Material.Emissive',
    'data.Material.Power',
    'data.GlobalAmbient',
    'data.TweenFactor',
    'packedFogColorAndAlphaRef',
    'fogDistanceScale',
    'fogDistanceEnd',
    'fogDensity',
    'packedReservedAndPointSize',
    'packedPointSizeMinMax',
    'pointScaleA',
    'pointScaleB',
    'pointScaleC',
    'alphaTestAndModeArgs',
    'fogArgs',
    'packedProjMaskAndFfArgs',
    // TexcoordMatrices[8] and DataPrimitives[12] are plain (non-struct)
    // arrays -- only their element-0 name is queried here; see
    // queryBaseElementLayout() in std140.js for why the rest of each
    // array is addressed via arithmetic (offset + i*arrayStride) instead
    // of by name.
    'data.TexcoordMatrices[0]',
    'data.DataPrimitives[0]',
  ];
  for (let i = 0; i < 8; i++) {
    for (const f of LIGHT_FIELDS) names.push(`data.Lights[${i}].${f}`);
  }
  return names;
}

const FF_VERTEX_DATA_NAMES = ffVertexDataNames();

export function packFFVertexData(gl, program, state) {
  const { block, uniforms: L } = cachedBlockLayout(
    gl, program, 'FFVertexData', 'FFVertexData', FF_VERTEX_DATA_NAMES
  );
  const { dataSize } = block;
  const buf = new ArrayBuffer(dataSize);
  const view = new DataView(buf);

  const off = (name) => L.get(name).offset;
  const mstride = (name) => L.get(name).matrixStride;

  const t = state.transform;
  writeMat4(view, off('data.WorldView'), mstride('data.WorldView'), t.worldView);
  writeMat4(view, off('data.NormalMatrix'), mstride('data.NormalMatrix'), t.normalMatrix);
  writeMat4(view, off('data.InverseView'), mstride('data.InverseView'), t.inverseView);
  writeMat4(view, off('data.Projection'), mstride('data.Projection'), t.projection);
  writeMat4(view, off('data.WorldViewProj'), mstride('data.WorldViewProj'), t.worldViewProj);

  const texcoordMatBase = L.get('data.TexcoordMatrices[0]');
  for (let i = 0; i < 8; i++) {
    writeMat4(view, elementOffset(texcoordMatBase, i), texcoordMatBase.matrixStride, t.texcoordMatrices[i]);
  }
  writeVec4(view, off('data.ViewportInfo.inverseOffset'), t.viewportInverseOffset);
  writeVec4(view, off('data.ViewportInfo.inverseExtent'), t.viewportInverseExtent);

  for (let i = 0; i < 8; i++) {
    const light = state.lights[i];
    writeVec4(view, off(`data.Lights[${i}].Diffuse`), light.diffuse);
    writeVec4(view, off(`data.Lights[${i}].Specular`), light.specular);
    writeVec4(view, off(`data.Lights[${i}].Ambient`), light.ambient);
    writeVec4(view, off(`data.Lights[${i}].Position`), light.position);
    writeVec4(view, off(`data.Lights[${i}].Direction`), light.direction);
    writeU32(view, off(`data.Lights[${i}].Type`), light.type);
    writeF32(view, off(`data.Lights[${i}].Range`), light.range);
    writeF32(view, off(`data.Lights[${i}].Falloff`), light.falloff);
    writeF32(view, off(`data.Lights[${i}].Attenuation0`), light.attenuation0);
    writeF32(view, off(`data.Lights[${i}].Attenuation1`), light.attenuation1);
    writeF32(view, off(`data.Lights[${i}].Attenuation2`), light.attenuation2);
    writeF32(view, off(`data.Lights[${i}].Theta`), light.theta);
    writeF32(view, off(`data.Lights[${i}].Phi`), light.phi);
  }

  const m = state.material;
  writeVec4(view, off('data.Material.Diffuse'), m.diffuse);
  writeVec4(view, off('data.Material.Ambient'), m.ambient);
  writeVec4(view, off('data.Material.Specular'), m.specular);
  writeVec4(view, off('data.Material.Emissive'), m.emissive);
  writeF32(view, off('data.Material.Power'), m.power);

  writeU32(view, off('data.GlobalAmbient'), packD3DColor(state.globalAmbient));
  writeF32(view, off('data.TweenFactor'), state.tweenFactor);

  const dpBytes = packDataPrimitives(state.dataPrimitives);
  const dpDwords = dataPrimitivesToDwords(dpBytes);
  const dataPrimitivesBase = L.get('data.DataPrimitives[0]');
  for (let i = 0; i < 12; i++) writeU32(view, elementOffset(dataPrimitivesBase, i), dpDwords[i]);

  const sh = state.shared;
  writeU32(
    view,
    off('packedFogColorAndAlphaRef'),
    packD3DColor({ a: sh.alphaRef, r: sh.fogColor.r, g: sh.fogColor.g, b: sh.fogColor.b })
  );
  writeF32(view, off('fogDistanceScale'), sh.fogDistanceScale);
  writeF32(view, off('fogDistanceEnd'), sh.fogDistanceEnd);
  writeF32(view, off('fogDensity'), sh.fogDensity);

  writeU32(view, off('packedReservedAndPointSize'), packReservedAndPointSize(state.point.size));
  writeU32(view, off('packedPointSizeMinMax'), packPointSizeMinMax(state.point.sizeMin, state.point.sizeMax));
  writeF32(view, off('pointScaleA'), state.point.scaleA);
  writeF32(view, off('pointScaleB'), state.point.scaleB);
  writeF32(view, off('pointScaleC'), state.point.scaleC);

  writeU32(
    view,
    off('alphaTestAndModeArgs'),
    packAlphaTestAndModeArgs({
      compareOp: sh.alphaCompareOp,
      precisionBits: sh.alphaPrecisionBits,
      pointSpriteEnable: sh.pointSpriteEnable,
      clipPlaneCount: sh.clipPlaneCount,
      drefScaleShift: sh.drefScaleShift,
    })
  );
  writeU32(
    view,
    off('fogArgs'),
    packFogArgs({
      fogEnable: sh.fogEnable,
      vertexFogMode: sh.vertexFogMode,
      pixelFogMode: sh.pixelFogMode,
      useZ: sh.useZFog,
    })
  );
  writeU32(
    view,
    off('packedProjMaskAndFfArgs'),
    packProjMaskAndFfArgs({
      projectedMask: sh.projectedMask,
      specularEnable: sh.specularEnable,
      pointScaleEnable: sh.pointScaleEnable,
      boundPsIsSM3: sh.boundPsIsSM3,
    })
  );

  return new Uint8Array(buf);
}

// --- FFClipPlanes --------------------------------------------------------

export function packFFClipPlanes(gl, program, state) {
  const { block, uniforms: L } = cachedBlockLayout(
    gl, program, 'FFClipPlanes', 'FFClipPlanes', ['clipPlanes[0]']
  );
  const { dataSize } = block;
  // clipPlanes[6] is a plain vec4 array -- only element 0 is individually
  // queryable by name (see queryBaseElementLayout() in std140.js); the
  // rest are addressed via offset + i*arrayStride.
  const base = L.get('clipPlanes[0]');
  const buf = new ArrayBuffer(dataSize);
  const view = new DataView(buf);
  for (let i = 0; i < 6; i++) {
    writeVec4(view, elementOffset(base, i), state.clipPlanes[i]);
  }
  return new Uint8Array(buf);
}

// --- FFBlendMatrices ------------------------------------------------------

export function packFFBlendMatrices(gl, program, state) {
  const { block, uniforms: L } = cachedBlockLayout(
    gl, program, 'FFBlendMatrices', 'FFBlendMatrices', ['worldViewArray[0]']
  );
  const { dataSize } = block;
  // worldViewArray[MAX_BLEND_MATRICES] is a plain mat4 array -- same
  // element-0-only querying rule as clipPlanes above. The loop bound comes
  // from ffp-constants.js (mirrors d3d9_ffp.vert's own MAX_BLEND_MATRICES),
  // not a hardcoded literal, so it can't silently drift from the shader's
  // array size; the byte offsets themselves are still entirely
  // driver-queried (see std140.js), never hardcoded.
  const base = L.get('worldViewArray[0]');
  const buf = new ArrayBuffer(dataSize);
  const view = new DataView(buf);
  for (let i = 0; i < MAX_BLEND_MATRICES; i++) {
    writeMat4(view, elementOffset(base, i), base.matrixStride, state.blendMatrices[i]);
  }
  return new Uint8Array(buf);
}

// --- FFPixelState ---------------------------------------------------------

function ffPixelStateNames() {
  return [
    'alphaTestAndModeArgsFS',
    'fogArgsFS',
    'packedProjMaskAndFfArgsFS',
    'psSamplerTypes',
    'psSamplerModes',
    'packedStageOps01',
    'packedStageOps23',
    'packedStageOps45',
    'packedStageOps67',
    'packedStageArgs0',
    'packedStageArgs1',
    'packedStageArgs2',
    'packedStageArgs3',
    'packedStageArgs4',
    'packedStageArgs5',
    'packedStageArgs6',
    'packedStageArgs7',
    'packedFogColorAndAlphaRefFS',
    'fogDistanceScaleFS',
    'fogDistanceEndFS',
    'fogDensityFS',
    'textureFactor',
  ];
}

const FF_PIXEL_STATE_NAMES = ffPixelStateNames();

export function packFFPixelState(gl, program, state) {
  const { block, uniforms: L } = cachedBlockLayout(
    gl, program, 'FFPixelState', 'FFPixelState', FF_PIXEL_STATE_NAMES
  );
  const { dataSize } = block;
  const buf = new ArrayBuffer(dataSize);
  const view = new DataView(buf);
  const off = (name) => L.get(name).offset;

  const sh = state.shared;
  writeU32(
    view,
    off('alphaTestAndModeArgsFS'),
    packAlphaTestAndModeArgs({
      compareOp: sh.alphaCompareOp,
      precisionBits: sh.alphaPrecisionBits,
      pointSpriteEnable: sh.pointSpriteEnable,
      clipPlaneCount: sh.clipPlaneCount,
      drefScaleShift: sh.drefScaleShift,
    })
  );
  writeU32(
    view,
    off('fogArgsFS'),
    packFogArgs({
      fogEnable: sh.fogEnable,
      vertexFogMode: sh.vertexFogMode,
      pixelFogMode: sh.pixelFogMode,
      useZ: sh.useZFog,
    })
  );
  writeU32(
    view,
    off('packedProjMaskAndFfArgsFS'),
    packProjMaskAndFfArgs({
      projectedMask: sh.projectedMask,
      specularEnable: sh.specularEnable,
      pointScaleEnable: sh.pointScaleEnable,
      boundPsIsSM3: sh.boundPsIsSM3,
    })
  );

  writeU32(view, off('psSamplerTypes'), packPerStage2Bit(state.samplers.map((s) => s.type)));
  writeU32(view, off('psSamplerModes'), packPerStage2Bit(state.samplers.map((s) => s.mode)));

  const op16 = state.textureStages.map((st) =>
    packStageOp16({ colorOp: st.colorOp, alphaOp: st.alphaOp, storeToTemp: st.storeToTemp })
  );
  writeU32(view, off('packedStageOps01'), packStageOpsPair(op16[0], op16[1]));
  writeU32(view, off('packedStageOps23'), packStageOpsPair(op16[2], op16[3]));
  writeU32(view, off('packedStageOps45'), packStageOpsPair(op16[4], op16[5]));
  writeU32(view, off('packedStageOps67'), packStageOpsPair(op16[6], op16[7]));

  for (let i = 0; i < 8; i++) {
    const st = state.textureStages[i];
    const [colorArg0, colorArg1, colorArg2] = packStageArgTriplet(st.colorArgs);
    const [alphaArg0, alphaArg1, alphaArg2] = packStageArgTriplet(st.alphaArgs);
    writeU32(
      view,
      off(`packedStageArgs${i}`),
      packStageArgs32({ colorArg0, colorArg1, colorArg2, alphaArg0, alphaArg1, alphaArg2 })
    );
  }

  writeU32(
    view,
    off('packedFogColorAndAlphaRefFS'),
    packD3DColor({ a: sh.alphaRef, r: sh.fogColor.r, g: sh.fogColor.g, b: sh.fogColor.b })
  );
  writeF32(view, off('fogDistanceScaleFS'), sh.fogDistanceScale);
  writeF32(view, off('fogDistanceEndFS'), sh.fogDistanceEnd);
  writeF32(view, off('fogDensityFS'), sh.fogDensity);

  writeU32(view, off('textureFactor'), packD3DColor(state.textureFactor));

  return new Uint8Array(buf);
}

// --- FFPixelStages --------------------------------------------------------

const PIXEL_STAGE_SCALAR_FIELDS = [
  ['bumpEnvMat00', 'BumpEnvMat00'],
  ['bumpEnvMat01', 'BumpEnvMat01'],
  ['bumpEnvMat10', 'BumpEnvMat10'],
  ['bumpEnvMat11', 'BumpEnvMat11'],
  ['bumpEnvLScale', 'BumpEnvLScale'],
  ['bumpEnvLOffset', 'BumpEnvLOffset'],
];

const FF_PIXEL_STAGE_NAMES = [];
for (let i = 0; i < 8; i++) {
  FF_PIXEL_STAGE_NAMES.push(`stages[${i}].Constant`);
  for (const [, glslField] of PIXEL_STAGE_SCALAR_FIELDS) {
    FF_PIXEL_STAGE_NAMES.push(`stages[${i}].${glslField}`);
  }
}

export function packFFPixelStages(gl, program, state) {
  const { block, uniforms: L } = cachedBlockLayout(
    gl, program, 'FFPixelStages', 'FFPixelStages', FF_PIXEL_STAGE_NAMES
  );
  const { dataSize } = block;
  const buf = new ArrayBuffer(dataSize);
  const view = new DataView(buf);

  for (let i = 0; i < 8; i++) {
    const stage = state.pixelStages[i];
    writeU32(view, L.get(`stages[${i}].Constant`).offset, packD3DColor(stage.constant));
    for (const [stateField, glslField] of PIXEL_STAGE_SCALAR_FIELDS) {
      writeF32(view, L.get(`stages[${i}].${glslField}`).offset, stage[stateField]);
    }
  }

  return new Uint8Array(buf);
}

// --- Convenience: pack every block at once --------------------------------

export function packAll(gl, program, state) {
  return {
    FFVertexData: packFFVertexData(gl, program, state),
    FFClipPlanes: packFFClipPlanes(gl, program, state),
    FFBlendMatrices: packFFBlendMatrices(gl, program, state),
    FFPixelState: packFFPixelState(gl, program, state),
    FFPixelStages: packFFPixelStages(gl, program, state),
  };
}
