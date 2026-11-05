// Exact current-corpus contract for the lower-risk NtCurrentTeb ->
// pthread_getspecific original-entry region.  This file is intentionally data
// only: the runtime does not discover or broaden the region from profiles.

export const NTCURRENTTEB_ORIGINAL_REGION_SPEC = Object.freeze({
  kind: 'ntcurrentteb-pthread-getspecific-original-region-v1',
  rawManifestSha256:
    '176cdd642acc52c501b417dbd44a46bfb71ba704feca2d2e28f529cfe6fa38b0',
  runtimeStem:
    'boxedwine.upstreamjit64fn6inlineabi2korean6chat1touch2zeroreloctoken1',
  hostKey: 'v5-106791a1-a2793124',
  terminalKey: 'v5-106791a1-a2793124',
  expected: Object.freeze({
    registeredEntries: 30550,
    groups: 94,
    hostGroup: 93,
    hostGroupBytesBefore: 344510,
    movedEntryBytes: 11374,
    hostGroupBytesAfter: 355884,
    budgetBytes: 524288,
    routerFunctions: 9,
    routedDispatcherSites: 39,
    staticTargetSites: 9,
    dynamicNormalTargetSites: 4,
    normalTargetSites: 13,
    residualFallbackSites: 26,
    selectiveStaticSourceRedirectedSites: 9,
    selectiveDynamicSourceRedirectedSites: 8,
    selectiveRedirectedSites: 17,
    selectiveRetainedDispatcherSites: 22,
    selectiveRouterResidualFallbackSites: 4,
    maxRouteDepth: 9,
  }),
  entries: Object.freeze([
    Object.freeze({key: 'v5-10679184-ef9b0d5e', relocCount: 3, originalGroup: 69}),
    Object.freeze({key: 'v5-10627b70-4f6728a6', relocCount: 0, originalGroup: 69}),
    Object.freeze({key: 'v5-1067918d-4a806d18', relocCount: 2, originalGroup: 21}),
    Object.freeze({key: 'v5-10627a60-9ff3e198', relocCount: 0, originalGroup: 21}),
    Object.freeze({key: 'v5-104f0db0-9e3656b2', relocCount: 7, originalGroup: 88}),
    Object.freeze({key: 'v5-1059200c-4f6728a6', relocCount: 0, originalGroup: 48}),
    Object.freeze({key: 'v5-104f0dbd-d8de1fe5', relocCount: 6, originalGroup: 11}),
    Object.freeze({key: 'v5-104f0df8-cbb0ecdf', relocCount: 3, originalGroup: 20}),
    Object.freeze({key: 'v5-104f0e11-f2dc3761', relocCount: 0, originalGroup: 10}),
    Object.freeze({key: 'v5-106791a1-a2793124', relocCount: 0, originalGroup: 93}),
  ]),
  routes: Object.freeze([
    Object.freeze({sourceKey: 'v5-10679184-ef9b0d5e', targetKey: 'v5-10627b70-4f6728a6', dispatcherSites: 5, staticTargetSites: 2, dynamicNormalTargetSites: 0}),
    Object.freeze({sourceKey: 'v5-10627b70-4f6728a6', targetKey: 'v5-1067918d-4a806d18', dispatcherSites: 2, staticTargetSites: 0, dynamicNormalTargetSites: 1}),
    Object.freeze({sourceKey: 'v5-1067918d-4a806d18', targetKey: 'v5-10627a60-9ff3e198', dispatcherSites: 5, staticTargetSites: 2, dynamicNormalTargetSites: 0}),
    Object.freeze({sourceKey: 'v5-10627a60-9ff3e198', targetKey: 'v5-104f0db0-9e3656b2', dispatcherSites: 2, staticTargetSites: 0, dynamicNormalTargetSites: 1}),
    Object.freeze({sourceKey: 'v5-104f0db0-9e3656b2', targetKey: 'v5-1059200c-4f6728a6', dispatcherSites: 7, staticTargetSites: 2, dynamicNormalTargetSites: 0}),
    Object.freeze({sourceKey: 'v5-1059200c-4f6728a6', targetKey: 'v5-104f0dbd-d8de1fe5', dispatcherSites: 2, staticTargetSites: 0, dynamicNormalTargetSites: 1}),
    Object.freeze({sourceKey: 'v5-104f0dbd-d8de1fe5', targetKey: 'v5-104f0df8-cbb0ecdf', dispatcherSites: 5, staticTargetSites: 1, dynamicNormalTargetSites: 0}),
    Object.freeze({sourceKey: 'v5-104f0df8-cbb0ecdf', targetKey: 'v5-104f0e11-f2dc3761', dispatcherSites: 9, staticTargetSites: 2, dynamicNormalTargetSites: 0}),
    Object.freeze({sourceKey: 'v5-104f0e11-f2dc3761', targetKey: 'v5-106791a1-a2793124', dispatcherSites: 2, staticTargetSites: 0, dynamicNormalTargetSites: 1}),
  ]),
});

function parseKey(key) {
  const match = /^v5-([0-9a-f]{8})-([0-9a-f]{8})$/i.exec(String(key));
  if (!match) throw new Error(`Original-region malformed key: ${key}`);
  return {
    eip: Number.parseInt(match[1], 16) >>> 0,
    blockHash: Number.parseInt(match[2], 16) >>> 0,
  };
}

export function validateOriginalRegionSpec(
    spec = NTCURRENTTEB_ORIGINAL_REGION_SPEC) {
  if (!spec || !/^[0-9a-f]{64}$/.test(spec.rawManifestSha256 || '')) {
    throw new Error('Original-region raw-manifest SHA-256 is missing or invalid');
  }
  if (spec.runtimeStem !==
      'boxedwine.upstreamjit64fn6inlineabi2korean6chat1touch2zeroreloctoken1') {
    throw new Error('Original-region runtime stem identity mismatch');
  }
  if (!Array.isArray(spec.entries) || spec.entries.length !== 10 ||
      !Array.isArray(spec.routes) || spec.routes.length !== 9) {
    throw new Error('Original-region requires exactly 10 entries and 9 routes');
  }
  const entryByKey = new Map();
  for (const entry of spec.entries) {
    parseKey(entry.key);
    if (entryByKey.has(entry.key)) {
      throw new Error(`Original-region duplicate entry: ${entry.key}`);
    }
    if (!Number.isSafeInteger(entry.relocCount) || entry.relocCount < 0 ||
        !Number.isSafeInteger(entry.originalGroup) || entry.originalGroup < 0) {
      throw new Error(`Original-region invalid entry contract: ${entry.key}`);
    }
    entryByKey.set(entry.key, entry);
  }
  if (!entryByKey.has(spec.hostKey) || spec.terminalKey !== spec.hostKey) {
    throw new Error('Original-region host/terminal key identity mismatch');
  }

  const outgoing = new Map();
  const indegree = new Map([...entryByKey.keys()].map((key) => [key, 0]));
  let dispatcherSites = 0;
  let staticTargetSites = 0;
  let dynamicNormalTargetSites = 0;
  for (const route of spec.routes) {
    if (!entryByKey.has(route.sourceKey) || !entryByKey.has(route.targetKey)) {
      throw new Error(
        `Original-region route references an unknown key: ${route.sourceKey} -> ${route.targetKey}`);
    }
    if (route.sourceKey === spec.terminalKey) {
      throw new Error('Original-region terminal epilogue route is forbidden');
    }
    if (outgoing.has(route.sourceKey)) {
      throw new Error(`Original-region duplicate source router: ${route.sourceKey}`);
    }
    if (!Number.isSafeInteger(route.dispatcherSites) || route.dispatcherSites <= 0 ||
        !Number.isSafeInteger(route.staticTargetSites) || route.staticTargetSites < 0 ||
        !Number.isSafeInteger(route.dynamicNormalTargetSites) ||
        route.dynamicNormalTargetSites < 0 ||
        route.staticTargetSites + route.dynamicNormalTargetSites <= 0 ||
        route.staticTargetSites + route.dynamicNormalTargetSites > route.dispatcherSites) {
      throw new Error(`Original-region invalid route-site contract: ${route.sourceKey}`);
    }
    outgoing.set(route.sourceKey, route.targetKey);
    indegree.set(route.targetKey, indegree.get(route.targetKey) + 1);
    dispatcherSites += route.dispatcherSites;
    staticTargetSites += route.staticTargetSites;
    dynamicNormalTargetSites += route.dynamicNormalTargetSites;
  }
  if (outgoing.has(spec.terminalKey)) {
    throw new Error('Original-region terminal epilogue route is forbidden');
  }

  const ready = [...indegree].filter(([, degree]) => degree === 0)
    .map(([key]) => key);
  const depth = new Map(ready.map((key) => [key, 0]));
  let visited = 0;
  while (ready.length) {
    const key = ready.pop();
    visited++;
    const next = outgoing.get(key);
    if (!next) continue;
    depth.set(next, Math.max(depth.get(next) || 0, (depth.get(key) || 0) + 1));
    const degree = indegree.get(next) - 1;
    indegree.set(next, degree);
    if (degree === 0) ready.push(next);
  }
  if (visited !== entryByKey.size) {
    throw new Error(
      `Original-region route graph is cyclic: visited ${visited}/${entryByKey.size}`);
  }
  const maxRouteDepth = Math.max(...depth.values());
  if (maxRouteDepth !== spec.expected.maxRouteDepth ||
      maxRouteDepth !== spec.routes.length) {
    throw new Error(
      `Original-region route-depth mismatch: got ${maxRouteDepth}, ` +
      `expected ${spec.expected.maxRouteDepth}`);
  }
  const normalTargetSites = staticTargetSites + dynamicNormalTargetSites;
  const selectiveStaticSourceRedirectedSites = spec.routes
    .filter((route) => route.staticTargetSites > 0)
    .reduce((sum, route) => sum + route.staticTargetSites, 0);
  const selectiveDynamicSourceRedirectedSites = spec.routes
    .filter((route) => route.dynamicNormalTargetSites > 0)
    .reduce((sum, route) => sum + route.dispatcherSites, 0);
  const selectiveRedirectedSites = selectiveStaticSourceRedirectedSites +
    selectiveDynamicSourceRedirectedSites;
  const selectiveRetainedDispatcherSites = dispatcherSites -
    selectiveRedirectedSites;
  const selectiveRouterResidualFallbackSites = selectiveRedirectedSites -
    normalTargetSites;
  if (dispatcherSites !== spec.expected.routedDispatcherSites ||
      staticTargetSites !== spec.expected.staticTargetSites ||
      dynamicNormalTargetSites !== spec.expected.dynamicNormalTargetSites ||
      normalTargetSites !== spec.expected.normalTargetSites ||
      dispatcherSites - normalTargetSites !== spec.expected.residualFallbackSites ||
      selectiveStaticSourceRedirectedSites !==
        spec.expected.selectiveStaticSourceRedirectedSites ||
      selectiveDynamicSourceRedirectedSites !==
        spec.expected.selectiveDynamicSourceRedirectedSites ||
      selectiveRedirectedSites !== spec.expected.selectiveRedirectedSites ||
      selectiveRetainedDispatcherSites !==
        spec.expected.selectiveRetainedDispatcherSites ||
      selectiveRouterResidualFallbackSites !==
        spec.expected.selectiveRouterResidualFallbackSites) {
    throw new Error(
      `Original-region site totals mismatch: routed=${dispatcherSites} ` +
      `static=${staticTargetSites} dynamic=${dynamicNormalTargetSites} ` +
      `normal=${normalTargetSites} residual=${dispatcherSites - normalTargetSites}`);
  }
  return {
    entryByKey,
    routeBySource: new Map(spec.routes.map((route) => [route.sourceKey, route])),
    maxRouteDepth,
    dispatcherSites,
    staticTargetSites,
    dynamicNormalTargetSites,
    normalTargetSites,
    residualFallbackSites: dispatcherSites - normalTargetSites,
    selectiveStaticSourceRedirectedSites,
    selectiveDynamicSourceRedirectedSites,
    selectiveRedirectedSites,
    selectiveRetainedDispatcherSites,
    selectiveRouterResidualFallbackSites,
  };
}

export function originalRegionKeyIdentity(key) {
  return parseKey(key);
}
