#!/usr/bin/env node

import { deflateRawSync, inflateRawSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { existsSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import {
  NTCURRENTTEB_ORIGINAL_REGION_SPEC,
  originalRegionKeyIdentity,
  validateOriginalRegionSpec,
} from './ntcurrentteb-original-region-spec.mjs';

const WASM_MAGIC = Buffer.from([0x00, 0x61, 0x73, 0x6d]);
const CACHE_NAME_RE = /^v5-([0-9a-f]{8})-([0-9a-f]{8})\.wasm$/i;
const GROUPED_MANIFEST = 'boxedwine-jit-grouped-manifest.json';
const PROFILE_NAME = 'boxedwine-jit-profile.txt';

const LOCAL_PASSES = [
  'optimize-instructions',
  'precompute',
  'simplify-locals',
  'local-cse',
  'dce',
  'vacuum',
  'coalesce-locals',
];

const gWarnings = [];

function usage() {
  console.log(`Usage:
  node boxedwine-wasm-jit-cache-pipeline.mjs [options] input.zip output.zip

Options:
  --binaryen-js PATH       Optimize wasm modules with Binaryen JS/WASM.
                           Defaults to ./binaryen_js.js if present.
  --flat                   Binaryen-optimize the per-block modules and re-emit
                           a flat cache zip (same v5-*.wasm layout + manifest),
                           skipping grouping, direct-call rewriting and profile
                           split hints. Use this as the ungrouped baseline;
                           both flat and grouped output remain per-build, so
                           record with the same target (jit or multiThreadedJit)
                           that will replay the output.
  --no-direct-calls        Merge modules but retain dispatcher exits instead
                           of rewriting intra-group exits as direct calls.
  --mrg1-runtime-stem ID   Bind a MOVSD runtime-zero legacy cache to this
                           exact private runtime stem.  Requires the paired
                           raw manifest SHA and --no-direct-calls.
  --mrg1-raw-manifest-sha256 HEX
                           Exact SHA-256 of the input raw manifest bytes.
  --parity-current-runtime-stem ID
                           Bind a current-corpus popcnt-parity cache to this
                           exact private runtime stem and its raw-manifest SHA.
  --parity-current-raw-manifest-sha256 HEX
                           Exact SHA-256 of the current parity raw manifest.
  --int9a-specialized-runtime-stem ID
                           Bind an INT 0x9A specialized cache to this exact
                           private runtime stem and its raw-manifest SHA.
  --int9a-specialized-raw-manifest-sha256 HEX
                           Exact SHA-256 of the private INT 0x9A raw manifest.
  --direct-call-kinds LIST Rewrite only the named exit kinds (comma-separated
                           next1,next2,jump). Intended for correctness triage;
                           the default is next1,next2. Generic jump rewriting
                           remains experimental because its inferred target
                           can cross structured control-flow arms.
  --mt-cyclic-limit N      Allow strict MT next1/next2 fast-success stitches
                           through cyclic graph nodes, returning to the C++
                           dispatcher after at most N direct edges. Default: 0.
  --mt-cycle-break-forward Admit cycle-touching MT edges without a runtime
                           depth counter when they cross SCCs or increase EIP
                           inside one SCC. Back-edges retain the dispatcher exit.
                           It may be combined with --mt-cyclic-limit so only
                           the residual back/self edges pay the bounded guard.
  --mt-direct-chain-depth-offset N
                           Explicit CPU depth-field offset for an older input
                           manifest. Must match the replay runtime exactly.
  --mt-bailout-offset N    Explicit CPU wasmJitBailout offset paired with the
                           depth override. Must match the runtime exactly.
  --mt-pfn-jit-code-offset N
                           Exact DecodedOp::pfnJitCode offset used by MT
                           publication guards. Must match replay runtime.
  --mt-generic-indirect-limit N
                           Replace exact dynamic nextOp dispatcher tails with
                           worker-local return_call_indirect chains bounded to
                           N dynamic indirect edges (the outer first block can
                           make N+1 blocks). Default: 0, maximum: 256.
  --mt-next-op-offset N    Exact CPU::nextOp offset for generic indirect tails.
  --mt-indirect-reloc-map-base N
                           Shared table-slot -> relocBase map address.
  --mt-indirect-reloc-map-capacity N
                           Number of U32 entries in the shared reloc map.
  --mt-indirect-reloc-map-plain-load
                           Read the immutable reloc map with i32.load after
                           acquiring pfnJitCode. The publisher must initialize
                           the map before its release publication.
  --mt-generic-depth-local Reuse the generic dispatcher's checked depth in an
                           i32 local for the successful increment, removing
                           one redundant linear-memory load per taken tail.
  --mt-generic-call-ref    Preserve the worker table.get result as an exact
                           nullable block reference and use return_call_ref,
                           avoiding a second slot lookup in call_indirect.
  --mt-generic-last-slot-cache
                           Cache one worker/group-local slot proven non-null;
                           matching published slots skip the readiness lookup.
  --mt-generic-inline-fetch
                           Inline the CPU op-cache hit path of helpers.fn_6 in
                           the common dispatcher. Exact runtime ABI offsets
                           below are mandatory and checked again by the page.
  --mt-cpu-thread-offset N
  --mt-kthread-terminating-offset N
  --mt-kthread-process-offset N
  --mt-cpu-eip-offset N
  --mt-cpu-cs-address-offset N
  --mt-cpu-big-offset N
  --mt-cpu-op-cache-offset N
                           Exact wasm32 object-layout offsets for inline fetch.
  --mt-generic-table-pregrown
                           Omit the generic dispatcher's table.size bounds
                           guard. The replay runtime must grow every executing
                           worker's table to the reloc-map capacity before it
                           enters any generic grouped-cache block.
  --mt-generic-worker-ready-bitmap-offset N
                           Replace table.get/ref.is_null with a nested nonzero
                           check of the exact worker-local installed-slot byte
                           row pointer stored at this CPU offset.
  --mt-internal-clone-target-keys LIST
                           Opt in to caller-group-local, non-exported clones for
                           the exact comma-separated v5 target keys. Targets
                           must be two-op/four-byte/zero-reloc dynamic-return
                           blocks with every profiled exit count equal to zero.
                           Only resolved next1/next2 incoming edges are eligible.
  --mt-internal-clone-expected-base-stats ENTRIES,GROUPS,PATCHES,DIRECTS
                           Mandatory fail-closed corpus identity paired with
                           --mt-internal-clone-target-keys. DIRECTS is the
                           pre-clone rewritten direct-tail count.
  --mt-original-region-mode MODE
                           Build the pinned NtCurrentTeb -> pthread_getspecific
                           original-entry region as control, candidate, or
                           selective. All modes
                           modes co-locate the exact 10 registered exports and
                           append the same 9 internal routers; candidate alone
                           redirects the exact 39 generic-dispatch sites;
                           selective redirects only 9 statically proved sites
                           plus all 8 sites of four dynamic-normal sources. The
                           raw-manifest SHA, keys, routes, old groups, size
                           budget and 9-edge acyclic depth are built in.
  --threaded-tail-census-helper-indices WRAPPER,TYPED,ADMITTED,ZERO_RELOC
                           Instrument the rejected R2 dual-entry cache only.
                           All four exact runtime helper-import indices are
                           mandatory; source population mismatch is rejected.
  --budget BYTES           Target grouped wasm payload size. Default: 524288.
  --interior-profile PATH  Parse BoxedWine console output and add hot interior
                           target split hints to the grouped manifest. If omitted,
                           boxedwine-jit-profile.txt is read from the input zip.
  --profile-split-threshold N
                           Minimum exact interior-target count. Default: 10000.
  --profile-split-limit N  Maximum split hints to emit. Default: 16.
  --profile-split-rejection-samples N
                           Number of rejected split candidates to print. Default: 4.
  --compression-level N    Output zip deflate level, 0-9. Default: 9.
  -h, --help               Show this help.

Produces the current BoxedWine wasm JIT merged grouped-cache format:
  groups/group-XXXX.wasm
  boxedwine-jit-grouped-manifest.json

The input must contain v5 cache module names and boxedwine-jit-manifest.json.`);
}

function parseArgs(argv) {
  const opts = {
    binaryenJs: null,
    flat: false,
    directCalls: true,
    mrg1RuntimeStem: null,
    mrg1RawManifestSha256: null,
    parityCurrentRuntimeStem: null,
    parityCurrentRawManifestSha256: null,
    int9aSpecializedRuntimeStem: null,
    int9aSpecializedRawManifestSha256: null,
    directCallKinds: new Set(['next1', 'next2']),
    mtCyclicLimit: 0,
    mtCycleBreakForward: false,
    mtDirectChainDepthOffset: null,
    mtBailoutOffset: null,
    mtPfnJitCodeOffset: null,
    mtGenericIndirectLimit: 0,
    mtNextOpOffset: null,
    mtIndirectRelocMapBase: null,
    mtIndirectRelocMapCapacity: null,
    mtIndirectRelocMapPlainLoad: false,
    mtGenericDepthLocal: false,
    mtGenericCallRef: false,
    mtGenericLastSlotCache: false,
    mtGenericInlineFetch: false,
    mtGenericTablePregrown: false,
    mtGenericWorkerReadyBitmapOffset: null,
    mtInternalCloneTargetKeys: new Set(),
    mtInternalCloneExpectedBaseStats: null,
    mtOriginalRegionMode: null,
    threadedTailCensusHelperIndices: null,
    mtCpuThreadOffset: null,
    mtKthreadTerminatingOffset: null,
    mtKthreadProcessOffset: null,
    mtCpuEipOffset: null,
    mtCpuCsAddressOffset: null,
    mtCpuBigOffset: null,
    mtCpuOpCacheOffset: null,
    runtimeLazyZNZ: false,
    runtimeLazyZNZCounters: false,
    runtimeLogicComposite32: false,
    runtimeLogicComposite32RuntimeStem: null,
    runtimeLogicComposite32RawManifestSha256: null,
    mtCpuFlagsOffset: null,
    mtCpuResultOffset: null,
    mtCpuLazyFlagTypeOffset: null,
    mtCpuLazyFlagTypePrevOffset: null,
    budget: 512 * 1024,
    interiorProfile: null,
    // 10000 admits most genuinely hot pairs; the old 50000 default left all
    // but one pair below threshold on typical PERF recordings, and split
    // hints are individually cheap (one shorter block + one extra entry).
    profileSplitThreshold: 10000,
    profileSplitLimit: 16,
    profileSplitRejectionSamples: 4,
    compressionLevel: 9,
    positional: [],
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '-h' || arg === '--help') {
      usage();
      process.exit(0);
    } else if (arg === '--binaryen-js') {
      opts.binaryenJs = argv[++i];
    } else if (arg.startsWith('--binaryen-js=')) {
      opts.binaryenJs = arg.slice('--binaryen-js='.length);
    } else if (arg === '--flat') {
      opts.flat = true;
    } else if (arg === '--no-direct-calls') {
      opts.directCalls = false;
    } else if (arg === '--mrg1-runtime-stem') {
      opts.mrg1RuntimeStem = String(argv[++i] || '');
    } else if (arg.startsWith('--mrg1-runtime-stem=')) {
      opts.mrg1RuntimeStem = arg.slice('--mrg1-runtime-stem='.length);
    } else if (arg === '--mrg1-raw-manifest-sha256') {
      opts.mrg1RawManifestSha256 = String(argv[++i] || '').toLowerCase();
    } else if (arg.startsWith('--mrg1-raw-manifest-sha256=')) {
      opts.mrg1RawManifestSha256 = arg.slice('--mrg1-raw-manifest-sha256='.length).toLowerCase();
    } else if (arg === '--parity-current-runtime-stem') {
      opts.parityCurrentRuntimeStem = String(argv[++i] || '');
    } else if (arg.startsWith('--parity-current-runtime-stem=')) {
      opts.parityCurrentRuntimeStem = arg.slice('--parity-current-runtime-stem='.length);
    } else if (arg === '--parity-current-raw-manifest-sha256') {
      opts.parityCurrentRawManifestSha256 = String(argv[++i] || '').toLowerCase();
    } else if (arg.startsWith('--parity-current-raw-manifest-sha256=')) {
      opts.parityCurrentRawManifestSha256 = arg.slice('--parity-current-raw-manifest-sha256='.length).toLowerCase();
    } else if (arg === '--int9a-specialized-runtime-stem') {
      opts.int9aSpecializedRuntimeStem = String(argv[++i] || '');
    } else if (arg.startsWith('--int9a-specialized-runtime-stem=')) {
      opts.int9aSpecializedRuntimeStem = arg.slice('--int9a-specialized-runtime-stem='.length);
    } else if (arg === '--int9a-specialized-raw-manifest-sha256') {
      opts.int9aSpecializedRawManifestSha256 = String(argv[++i] || '').toLowerCase();
    } else if (arg.startsWith('--int9a-specialized-raw-manifest-sha256=')) {
      opts.int9aSpecializedRawManifestSha256 = arg.slice('--int9a-specialized-raw-manifest-sha256='.length).toLowerCase();
    } else if (arg === '--direct-call-kinds') {
      opts.directCallKinds = parseDirectCallKinds(argv[++i]);
    } else if (arg.startsWith('--direct-call-kinds=')) {
      opts.directCallKinds = parseDirectCallKinds(arg.slice('--direct-call-kinds='.length));
    } else if (arg === '--mt-cyclic-limit') {
      opts.mtCyclicLimit = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cyclic-limit=')) {
      opts.mtCyclicLimit = Number(arg.slice('--mt-cyclic-limit='.length));
    } else if (arg === '--mt-cycle-break-forward') {
      opts.mtCycleBreakForward = true;
    } else if (arg === '--mt-direct-chain-depth-offset') {
      opts.mtDirectChainDepthOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-direct-chain-depth-offset=')) {
      opts.mtDirectChainDepthOffset = Number(arg.slice('--mt-direct-chain-depth-offset='.length));
    } else if (arg === '--mt-bailout-offset') {
      opts.mtBailoutOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-bailout-offset=')) {
      opts.mtBailoutOffset = Number(arg.slice('--mt-bailout-offset='.length));
    } else if (arg === '--mt-pfn-jit-code-offset') {
      opts.mtPfnJitCodeOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-pfn-jit-code-offset=')) {
      opts.mtPfnJitCodeOffset = Number(arg.slice('--mt-pfn-jit-code-offset='.length));
    } else if (arg === '--mt-generic-indirect-limit') {
      opts.mtGenericIndirectLimit = Number(argv[++i]);
    } else if (arg.startsWith('--mt-generic-indirect-limit=')) {
      opts.mtGenericIndirectLimit = Number(arg.slice('--mt-generic-indirect-limit='.length));
    } else if (arg === '--mt-next-op-offset') {
      opts.mtNextOpOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-next-op-offset=')) {
      opts.mtNextOpOffset = Number(arg.slice('--mt-next-op-offset='.length));
    } else if (arg === '--mt-indirect-reloc-map-base') {
      opts.mtIndirectRelocMapBase = Number(argv[++i]);
    } else if (arg.startsWith('--mt-indirect-reloc-map-base=')) {
      opts.mtIndirectRelocMapBase = Number(arg.slice('--mt-indirect-reloc-map-base='.length));
    } else if (arg === '--mt-indirect-reloc-map-capacity') {
      opts.mtIndirectRelocMapCapacity = Number(argv[++i]);
    } else if (arg.startsWith('--mt-indirect-reloc-map-capacity=')) {
      opts.mtIndirectRelocMapCapacity = Number(arg.slice('--mt-indirect-reloc-map-capacity='.length));
    } else if (arg === '--mt-indirect-reloc-map-plain-load') {
      opts.mtIndirectRelocMapPlainLoad = true;
    } else if (arg === '--mt-generic-depth-local') {
      opts.mtGenericDepthLocal = true;
    } else if (arg === '--mt-generic-call-ref') {
      opts.mtGenericCallRef = true;
    } else if (arg === '--mt-generic-last-slot-cache') {
      opts.mtGenericLastSlotCache = true;
    } else if (arg === '--mt-generic-inline-fetch') {
      opts.mtGenericInlineFetch = true;
    } else if (arg === '--runtime-lazy-znz') {
      opts.runtimeLazyZNZ = true;
    } else if (arg === '--runtime-lazy-znz-counters') {
      opts.runtimeLazyZNZCounters = true;
    } else if (arg === '--runtime-logic-composite32') {
      opts.runtimeLogicComposite32 = true;
    } else if (arg === '--runtime-logic-composite32-runtime-stem') {
      opts.runtimeLogicComposite32RuntimeStem = String(argv[++i] || '');
    } else if (arg.startsWith('--runtime-logic-composite32-runtime-stem=')) {
      opts.runtimeLogicComposite32RuntimeStem = arg.slice('--runtime-logic-composite32-runtime-stem='.length);
    } else if (arg === '--runtime-logic-composite32-raw-manifest-sha256') {
      opts.runtimeLogicComposite32RawManifestSha256 = String(argv[++i] || '').toLowerCase();
    } else if (arg.startsWith('--runtime-logic-composite32-raw-manifest-sha256=')) {
      opts.runtimeLogicComposite32RawManifestSha256 = arg.slice('--runtime-logic-composite32-raw-manifest-sha256='.length).toLowerCase();
    } else if (arg === '--mt-cpu-flags-offset') {
      opts.mtCpuFlagsOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-flags-offset=')) {
      opts.mtCpuFlagsOffset = Number(arg.slice('--mt-cpu-flags-offset='.length));
    } else if (arg === '--mt-cpu-result-offset') {
      opts.mtCpuResultOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-result-offset=')) {
      opts.mtCpuResultOffset = Number(arg.slice('--mt-cpu-result-offset='.length));
    } else if (arg === '--mt-cpu-lazy-flag-type-offset') {
      opts.mtCpuLazyFlagTypeOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-lazy-flag-type-offset=')) {
      opts.mtCpuLazyFlagTypeOffset = Number(arg.slice('--mt-cpu-lazy-flag-type-offset='.length));
    } else if (arg === '--mt-cpu-lazy-flag-type-prev-offset') {
      opts.mtCpuLazyFlagTypePrevOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-lazy-flag-type-prev-offset=')) {
      opts.mtCpuLazyFlagTypePrevOffset = Number(arg.slice('--mt-cpu-lazy-flag-type-prev-offset='.length));
    } else if (arg === '--mt-cpu-thread-offset') {
      opts.mtCpuThreadOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-thread-offset=')) {
      opts.mtCpuThreadOffset = Number(arg.slice('--mt-cpu-thread-offset='.length));
    } else if (arg === '--mt-kthread-terminating-offset') {
      opts.mtKthreadTerminatingOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-kthread-terminating-offset=')) {
      opts.mtKthreadTerminatingOffset = Number(arg.slice('--mt-kthread-terminating-offset='.length));
    } else if (arg === '--mt-kthread-process-offset') {
      opts.mtKthreadProcessOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-kthread-process-offset=')) {
      opts.mtKthreadProcessOffset = Number(arg.slice('--mt-kthread-process-offset='.length));
    } else if (arg === '--mt-cpu-eip-offset') {
      opts.mtCpuEipOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-eip-offset=')) {
      opts.mtCpuEipOffset = Number(arg.slice('--mt-cpu-eip-offset='.length));
    } else if (arg === '--mt-cpu-cs-address-offset') {
      opts.mtCpuCsAddressOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-cs-address-offset=')) {
      opts.mtCpuCsAddressOffset = Number(arg.slice('--mt-cpu-cs-address-offset='.length));
    } else if (arg === '--mt-cpu-big-offset') {
      opts.mtCpuBigOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-big-offset=')) {
      opts.mtCpuBigOffset = Number(arg.slice('--mt-cpu-big-offset='.length));
    } else if (arg === '--mt-cpu-op-cache-offset') {
      opts.mtCpuOpCacheOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-cpu-op-cache-offset=')) {
      opts.mtCpuOpCacheOffset = Number(arg.slice('--mt-cpu-op-cache-offset='.length));
    } else if (arg === '--mt-generic-table-pregrown') {
      opts.mtGenericTablePregrown = true;
    } else if (arg === '--mt-generic-worker-ready-bitmap-offset') {
      opts.mtGenericWorkerReadyBitmapOffset = Number(argv[++i]);
    } else if (arg.startsWith('--mt-generic-worker-ready-bitmap-offset=')) {
      opts.mtGenericWorkerReadyBitmapOffset = Number(arg.slice('--mt-generic-worker-ready-bitmap-offset='.length));
    } else if (arg === '--mt-internal-clone-target-keys') {
      opts.mtInternalCloneTargetKeys = parseExactTargetKeys(argv[++i]);
    } else if (arg.startsWith('--mt-internal-clone-target-keys=')) {
      opts.mtInternalCloneTargetKeys = parseExactTargetKeys(
        arg.slice('--mt-internal-clone-target-keys='.length));
    } else if (arg === '--mt-internal-clone-expected-base-stats') {
      opts.mtInternalCloneExpectedBaseStats = parseInternalCloneExpectedBaseStats(argv[++i]);
    } else if (arg.startsWith('--mt-internal-clone-expected-base-stats=')) {
      opts.mtInternalCloneExpectedBaseStats = parseInternalCloneExpectedBaseStats(
        arg.slice('--mt-internal-clone-expected-base-stats='.length));
    } else if (arg === '--mt-original-region-mode') {
      opts.mtOriginalRegionMode = String(argv[++i] || '').toLowerCase();
    } else if (arg.startsWith('--mt-original-region-mode=')) {
      opts.mtOriginalRegionMode =
        arg.slice('--mt-original-region-mode='.length).toLowerCase();
    } else if (arg === '--threaded-tail-census-helper-indices') {
      opts.threadedTailCensusHelperIndices = String(argv[++i]).split(',').map(Number);
    } else if (arg.startsWith('--threaded-tail-census-helper-indices=')) {
      opts.threadedTailCensusHelperIndices = arg.slice('--threaded-tail-census-helper-indices='.length).split(',').map(Number);
    } else if (arg === '--budget') {
      opts.budget = Number(argv[++i]);
    } else if (arg.startsWith('--budget=')) {
      opts.budget = Number(arg.slice('--budget='.length));
    } else if (arg === '--interior-profile') {
      opts.interiorProfile = argv[++i];
    } else if (arg.startsWith('--interior-profile=')) {
      opts.interiorProfile = arg.slice('--interior-profile='.length);
    } else if (arg === '--profile-split-threshold') {
      opts.profileSplitThreshold = Number(argv[++i]);
    } else if (arg.startsWith('--profile-split-threshold=')) {
      opts.profileSplitThreshold = Number(arg.slice('--profile-split-threshold='.length));
    } else if (arg === '--profile-split-limit') {
      opts.profileSplitLimit = Number(argv[++i]);
    } else if (arg.startsWith('--profile-split-limit=')) {
      opts.profileSplitLimit = Number(arg.slice('--profile-split-limit='.length));
    } else if (arg === '--profile-split-rejection-samples') {
      opts.profileSplitRejectionSamples = Number(argv[++i]);
    } else if (arg.startsWith('--profile-split-rejection-samples=')) {
      opts.profileSplitRejectionSamples = Number(arg.slice('--profile-split-rejection-samples='.length));
    } else if (arg === '--compression-level') {
      opts.compressionLevel = Number(argv[++i]);
    } else if (arg.startsWith('--compression-level=')) {
      opts.compressionLevel = Number(arg.slice('--compression-level='.length));
    } else if (arg.startsWith('-')) {
      throw new Error(`Unknown option: ${arg}`);
    } else {
      opts.positional.push(arg);
    }
  }
  if (opts.positional.length !== 2) throw new Error('Expected input.zip and output.zip');
  if (!Number.isSafeInteger(opts.budget) || opts.budget <= 0) throw new Error('--budget must be a positive integer');
  if (!Number.isSafeInteger(opts.mtCyclicLimit) || opts.mtCyclicLimit < 0 || opts.mtCyclicLimit > 64) {
    throw new Error('--mt-cyclic-limit must be an integer from 0 to 64');
  }
  if (!Number.isSafeInteger(opts.mtGenericIndirectLimit) || opts.mtGenericIndirectLimit < 0 || opts.mtGenericIndirectLimit > 256) {
    throw new Error('--mt-generic-indirect-limit must be an integer from 0 to 256');
  }
  if (opts.mtGenericTablePregrown && opts.mtGenericIndirectLimit === 0) {
    throw new Error('--mt-generic-table-pregrown requires --mt-generic-indirect-limit');
  }
  if (opts.mtGenericCallRef && opts.mtGenericIndirectLimit === 0) {
    throw new Error('--mt-generic-call-ref requires --mt-generic-indirect-limit');
  }
  if (opts.mtGenericLastSlotCache && opts.mtGenericIndirectLimit === 0) {
    throw new Error('--mt-generic-last-slot-cache requires --mt-generic-indirect-limit');
  }
  if (opts.mtGenericInlineFetch && opts.mtGenericIndirectLimit === 0) {
    throw new Error('--mt-generic-inline-fetch requires --mt-generic-indirect-limit');
  }
  if (opts.mtGenericCallRef && opts.mtGenericWorkerReadyBitmapOffset !== null) {
    throw new Error('--mt-generic-call-ref is incompatible with --mt-generic-worker-ready-bitmap-offset');
  }
  if (opts.mtGenericLastSlotCache && (opts.mtGenericCallRef || opts.mtGenericWorkerReadyBitmapOffset !== null)) {
    throw new Error('--mt-generic-last-slot-cache requires the table-slot dispatcher path');
  }
  if (opts.mtGenericWorkerReadyBitmapOffset !== null && opts.mtGenericIndirectLimit === 0) {
    throw new Error('--mt-generic-worker-ready-bitmap-offset requires --mt-generic-indirect-limit');
  }
  if (opts.mtInternalCloneTargetKeys.size && (!opts.directCalls || opts.flat)) {
    throw new Error('--mt-internal-clone-target-keys requires grouped direct calls');
  }
  if (Boolean(opts.mtInternalCloneTargetKeys.size) !==
      Boolean(opts.mtInternalCloneExpectedBaseStats)) {
    throw new Error(
      '--mt-internal-clone-target-keys and --mt-internal-clone-expected-base-stats are required together');
  }
  if (opts.mtOriginalRegionMode !== null &&
      opts.mtOriginalRegionMode !== 'control' &&
      opts.mtOriginalRegionMode !== 'candidate' &&
      opts.mtOriginalRegionMode !== 'selective') {
    throw new Error(
      '--mt-original-region-mode must be control, candidate, or selective');
  }
  if (opts.mtOriginalRegionMode !== null &&
      (!opts.directCalls || opts.flat || opts.mtGenericIndirectLimit === 0 ||
       !opts.mtGenericInlineFetch)) {
    throw new Error(
      '--mt-original-region-mode requires grouped direct calls, generic indirect tails, and inline fetch');
  }
  if (opts.mtOriginalRegionMode !== null && opts.mtInternalCloneTargetKeys.size) {
    throw new Error(
      '--mt-original-region-mode cannot be combined with internal clone targets');
  }
  if (opts.mtOriginalRegionMode !== null &&
      (opts.mrg1RuntimeStem !== null || opts.parityCurrentRuntimeStem !== null ||
       opts.int9aSpecializedRuntimeStem !== null ||
       opts.runtimeLogicComposite32RuntimeStem !== null || opts.runtimeLazyZNZ ||
       opts.runtimeLogicComposite32 || opts.threadedTailCensusHelperIndices !== null)) {
    throw new Error(
      '--mt-original-region-mode is bound to its exact runtime and cannot be combined with other private runtime modes');
  }
  for (const [name, value] of [['--mt-direct-chain-depth-offset', opts.mtDirectChainDepthOffset], ['--mt-bailout-offset', opts.mtBailoutOffset], ['--mt-pfn-jit-code-offset', opts.mtPfnJitCodeOffset]]) {
    if (value !== null && (!Number.isSafeInteger(value) || value <= 0)) {
      throw new Error(`${name} must be a positive integer`);
    }
  }
  for (const [name, value] of [['--mt-next-op-offset', opts.mtNextOpOffset], ['--mt-indirect-reloc-map-base', opts.mtIndirectRelocMapBase], ['--mt-indirect-reloc-map-capacity', opts.mtIndirectRelocMapCapacity], ['--mt-generic-worker-ready-bitmap-offset', opts.mtGenericWorkerReadyBitmapOffset]]) {
    if (value !== null && (!Number.isSafeInteger(value) || value <= 0)) {
      throw new Error(`${name} must be a positive integer`);
    }
  }
  for (const [name, value] of [['--mt-cpu-thread-offset', opts.mtCpuThreadOffset], ['--mt-kthread-terminating-offset', opts.mtKthreadTerminatingOffset], ['--mt-kthread-process-offset', opts.mtKthreadProcessOffset], ['--mt-cpu-eip-offset', opts.mtCpuEipOffset], ['--mt-cpu-cs-address-offset', opts.mtCpuCsAddressOffset], ['--mt-cpu-big-offset', opts.mtCpuBigOffset], ['--mt-cpu-op-cache-offset', opts.mtCpuOpCacheOffset]]) {
    if (value !== null && (!Number.isSafeInteger(value) || value < 0)) {
      throw new Error(`${name} must be a non-negative integer`);
    }
  }
  if (opts.runtimeLazyZNZ) {
    for (const [name, value] of [['--mt-cpu-flags-offset', opts.mtCpuFlagsOffset], ['--mt-cpu-result-offset', opts.mtCpuResultOffset], ['--mt-cpu-lazy-flag-type-offset', opts.mtCpuLazyFlagTypeOffset], ['--mt-cpu-lazy-flag-type-prev-offset', opts.mtCpuLazyFlagTypePrevOffset]]) {
      if (!Number.isSafeInteger(value) || value < 0) throw new Error(`${name} is required with --runtime-lazy-znz and must be a non-negative integer`);
    }
  }
  if (opts.runtimeLazyZNZCounters && !opts.runtimeLazyZNZ) {
    throw new Error('--runtime-lazy-znz-counters requires --runtime-lazy-znz');
  }
  const hasRuntimeLogicComposite32Identity = opts.runtimeLogicComposite32RuntimeStem !== null || opts.runtimeLogicComposite32RawManifestSha256 !== null;
  if (hasRuntimeLogicComposite32Identity &&
      (!opts.runtimeLogicComposite32 || opts.runtimeLogicComposite32RuntimeStem === null || opts.runtimeLogicComposite32RawManifestSha256 === null ||
       opts.runtimeLogicComposite32RuntimeStem !== 'boxedwine.upstreamjit64fn6inlineabi2logiccomposite321' ||
       !/^[0-9a-f]{64}$/.test(opts.runtimeLogicComposite32RawManifestSha256))) {
    throw new Error('--runtime-logic-composite32 requires its private stem and raw manifest SHA-256');
  }
  if (opts.runtimeLogicComposite32 && !hasRuntimeLogicComposite32Identity) {
    throw new Error('--runtime-logic-composite32 requires runtime stem and raw manifest SHA-256');
  }
  if (opts.runtimeLogicComposite32) {
    for (const [name, value] of [['--mt-cpu-result-offset', opts.mtCpuResultOffset], ['--mt-cpu-lazy-flag-type-offset', opts.mtCpuLazyFlagTypeOffset]]) {
      if (!Number.isSafeInteger(value) || value < 0) throw new Error(`${name} is required with --runtime-logic-composite32 and must be a non-negative integer`);
    }
  }
  const hasMrgIdentity = opts.mrg1RuntimeStem !== null || opts.mrg1RawManifestSha256 !== null;
  if (hasMrgIdentity &&
      (opts.mrg1RuntimeStem === null || opts.mrg1RawManifestSha256 === null ||
       !/^boxedwine\.upstreamjit64fn6inlineabi2movsdruntimezero[0-9]+$/.test(opts.mrg1RuntimeStem) ||
       !/^[0-9a-f]{64}$/.test(opts.mrg1RawManifestSha256) || opts.directCalls)) {
    throw new Error('--mrg1-runtime-stem and --mrg1-raw-manifest-sha256 require an MRG stem, a SHA-256, and --no-direct-calls');
  }
  const hasParityCurrentIdentity = opts.parityCurrentRuntimeStem !== null || opts.parityCurrentRawManifestSha256 !== null;
  if (hasParityCurrentIdentity &&
      (opts.parityCurrentRuntimeStem === null || opts.parityCurrentRawManifestSha256 === null ||
       !/^boxedwine\.upstreamjit64fn6inlineabi2paritycurrent[0-9]+$/.test(opts.parityCurrentRuntimeStem) ||
       !/^[0-9a-f]{64}$/.test(opts.parityCurrentRawManifestSha256))) {
    throw new Error('--parity-current-runtime-stem and --parity-current-raw-manifest-sha256 require the private parity stem and a SHA-256');
  }
  const hasInt9ASpecializedIdentity = opts.int9aSpecializedRuntimeStem !== null || opts.int9aSpecializedRawManifestSha256 !== null;
  if (hasInt9ASpecializedIdentity &&
      (opts.int9aSpecializedRuntimeStem !== 'boxedwine.upstreamjit64fn6inlineabi2int9aspecialized1' ||
       opts.int9aSpecializedRawManifestSha256 === null || !/^[0-9a-f]{64}$/.test(opts.int9aSpecializedRawManifestSha256))) {
    throw new Error('--int9a-specialized-runtime-stem and --int9a-specialized-raw-manifest-sha256 require the exact private stem and a SHA-256');
  }
  if ((hasMrgIdentity ? 1 : 0) + (hasParityCurrentIdentity ? 1 : 0) + (hasRuntimeLogicComposite32Identity ? 1 : 0) + (hasInt9ASpecializedIdentity ? 1 : 0) > 1) {
    throw new Error('MRG, parity, INT9A and runtime logical-composite32 identities are mutually exclusive');
  }
  if (opts.threadedTailCensusHelperIndices !== null &&
      (opts.threadedTailCensusHelperIndices.length !== 4 ||
       opts.threadedTailCensusHelperIndices.some(value => !Number.isSafeInteger(value) || value < 0) ||
       new Set(opts.threadedTailCensusHelperIndices).size !== 4)) {
    throw new Error('--threaded-tail-census-helper-indices requires four distinct non-negative integer indices');
  }
  if (!Number.isSafeInteger(opts.profileSplitThreshold) || opts.profileSplitThreshold < 1) throw new Error('--profile-split-threshold must be a positive integer');
  if (!Number.isSafeInteger(opts.profileSplitLimit) || opts.profileSplitLimit < 0) throw new Error('--profile-split-limit must be a non-negative integer');
  if (!Number.isSafeInteger(opts.profileSplitRejectionSamples) || opts.profileSplitRejectionSamples < 0) throw new Error('--profile-split-rejection-samples must be a non-negative integer');
  if (!Number.isInteger(opts.compressionLevel) || opts.compressionLevel < 0 || opts.compressionLevel > 9) {
    throw new Error('--compression-level must be an integer from 0 to 9');
  }
  return { ...opts, inputZip: opts.positional[0], outputZip: opts.positional[1] };
}

function parseDirectCallKinds(value) {
  const allowed = new Set(['next1', 'next2', 'jump']);
  const kinds = new Set(String(value || '').split(',').filter(Boolean));
  if (!kinds.size || [...kinds].some((kind) => !allowed.has(kind))) {
    throw new Error(`Invalid --direct-call-kinds value: ${value}`);
  }
  return kinds;
}

function readU16(buf, off) {
  if (off + 2 > buf.length) throw new Error(`Unexpected EOF at ${off}`);
  return buf.readUInt16LE(off);
}

function readU32(buf, off) {
  if (off + 4 > buf.length) throw new Error(`Unexpected EOF at ${off}`);
  return buf.readUInt32LE(off);
}

function writeU16(value) {
  const buf = Buffer.alloc(2);
  buf.writeUInt16LE(value);
  return buf;
}

function writeU32(value) {
  const buf = Buffer.alloc(4);
  buf.writeUInt32LE(value >>> 0);
  return buf;
}

function writeWasmUleb(value) {
  const bytes = [];
  let v = value >>> 0;
  do {
    let byte = v & 0x7f;
    v >>>= 7;
    if (v) byte |= 0x80;
    bytes.push(byte);
  } while (v);
  return Buffer.from(bytes);
}

function writeWasmSleb(value) {
  const bytes = [];
  let v = value | 0;
  let more = true;
  while (more) {
    let byte = v & 0x7f;
    v >>= 7;
    const signBitSet = (byte & 0x40) !== 0;
    if ((v === 0 && !signBitSet) || (v === -1 && signBitSet)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    bytes.push(byte);
  }
  return Buffer.from(bytes);
}

function wasmNameBytes(name) {
  const bytes = Buffer.from(name, 'utf8');
  return Buffer.concat([writeWasmUleb(bytes.length), bytes]);
}

function readWasmUleb(buf, off) {
  let value = 0;
  let shift = 0;
  let pos = off;
  while (pos < buf.length) {
    const byte = buf[pos++];
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) return [value >>> 0, pos];
    shift += 7;
  }
  throw new Error(`Unexpected EOF in LEB at ${off}`);
}

function readWasmSleb(buf, off, bits = 32) {
  let value = 0;
  let shift = 0;
  let pos = off;
  let byte = 0;
  while (pos < buf.length) {
    byte = buf[pos++];
    value |= (byte & 0x7f) << shift;
    shift += 7;
    if ((byte & 0x80) === 0) break;
  }
  if ((shift < bits) && (byte & 0x40)) value |= (~0 << shift);
  return [value | 0, pos];
}

function wasmName(buf, off) {
  const [len, p] = readWasmUleb(buf, off);
  const end = p + len;
  if (end > buf.length) throw new Error('Wasm name extends outside buffer');
  return [buf.subarray(p, end).toString('utf8'), end];
}

function findEndOfCentralDirectory(buf) {
  const min = Math.max(0, buf.length - 0xffff - 22);
  for (let off = buf.length - 22; off >= min; off--) {
    if (readU32(buf, off) === 0x06054b50) return off;
  }
  throw new Error('Could not find ZIP end-of-central-directory record');
}

function decodeName(buf, flags) {
  return buf.toString((flags & (1 << 11)) ? 'utf8' : 'latin1');
}

function parseZip(buf) {
  const eocd = findEndOfCentralDirectory(buf);
  const totalEntries = readU16(buf, eocd + 10);
  const centralSize = readU32(buf, eocd + 12);
  const centralOffset = readU32(buf, eocd + 16);
  if (totalEntries === 0xffff || centralSize === 0xffffffff || centralOffset === 0xffffffff) {
    throw new Error('Zip64 archives are not supported');
  }
  const entries = [];
  let off = centralOffset;
  for (let i = 0; i < totalEntries; i++) {
    if (readU32(buf, off) !== 0x02014b50) throw new Error(`Bad central-directory header at ${off}`);
    const flags = readU16(buf, off + 8);
    const method = readU16(buf, off + 10);
    const compressedSize = readU32(buf, off + 20);
    const uncompressedSize = readU32(buf, off + 24);
    const nameLen = readU16(buf, off + 28);
    const extraLen = readU16(buf, off + 30);
    const commentLen = readU16(buf, off + 32);
    const localOffset = readU32(buf, off + 42);
    const name = decodeName(buf.subarray(off + 46, off + 46 + nameLen), flags);
    if (readU32(buf, localOffset) !== 0x04034b50) throw new Error(`Bad local header for ${name}`);
    const localMethod = readU16(buf, localOffset + 8);
    const localNameLen = readU16(buf, localOffset + 26);
    const localExtraLen = readU16(buf, localOffset + 28);
    const dataStart = localOffset + 30 + localNameLen + localExtraLen;
    const compressed = buf.subarray(dataStart, dataStart + compressedSize);
    let data;
    if (localMethod === 0) data = Buffer.from(compressed);
    else if (localMethod === 8) data = inflateRawSync(compressed);
    else throw new Error(`Unsupported ZIP method ${localMethod} for ${name}`);
    if (data.length !== uncompressedSize) throw new Error(`Bad uncompressed size for ${name}`);
    entries.push({ name, method, localMethod, compressedSize, uncompressedSize, data });
    off += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < CRC_TABLE.length; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  CRC_TABLE[n] = c >>> 0;
}

function crc32(buf) {
  let c = 0xffffffff;
  for (const byte of buf) c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function buildZip(entries, level) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const entry of entries) {
    const nameBuf = Buffer.from(entry.name, 'utf8');
    const data = Buffer.from(entry.data);
    const compressed = level === 0 ? data : deflateRawSync(data, { level });
    const method = level === 0 ? 0 : 8;
    const crc = crc32(data);
    const local = Buffer.concat([
      writeU32(0x04034b50), writeU16(20), writeU16(1 << 11), writeU16(method),
      writeU16(0), writeU16(0), writeU32(crc), writeU32(compressed.length),
      writeU32(data.length), writeU16(nameBuf.length), writeU16(0), nameBuf, compressed,
    ]);
    locals.push(local);
    const central = Buffer.concat([
      writeU32(0x02014b50), writeU16(0x031e), writeU16(20), writeU16(1 << 11),
      writeU16(method), writeU16(0), writeU16(0), writeU32(crc), writeU32(compressed.length),
      writeU32(data.length), writeU16(nameBuf.length), writeU16(0), writeU16(0),
      writeU16(0), writeU16(0), writeU32(0), writeU32(offset), nameBuf,
    ]);
    centrals.push(central);
    offset += local.length;
  }
  const centralOffset = offset;
  const centralDir = Buffer.concat(centrals);
  const localData = Buffer.concat(locals);
  const eocd = Buffer.concat([
    writeU32(0x06054b50), writeU16(0), writeU16(0), writeU16(entries.length),
    writeU16(entries.length), writeU32(centralDir.length), writeU32(centralOffset), writeU16(0),
  ]);
  return Buffer.concat([localData, centralDir, eocd]);
}

function isWasm(data) {
  return data.length >= 4 && data.subarray(0, 4).equals(WASM_MAGIC);
}

function parseImportShape(wasm) {
  const imports = [];
  let pos = 8;
  while (pos < wasm.length) {
    const id = wasm[pos++];
    const [size, p] = readWasmUleb(wasm, pos);
    pos = p;
    const sectionEnd = pos + size;
    if (id === 2) {
      let q = pos;
      const [count, q2] = readWasmUleb(wasm, q);
      q = q2;
      for (let i = 0; i < count; i++) {
        let moduleName; [moduleName, q] = wasmName(wasm, q);
        let fieldName; [fieldName, q] = wasmName(wasm, q);
        const kind = wasm[q++];
        const imp = { module: moduleName, field: fieldName, kind };
        if (kind === 0) [imp.typeIndex, q] = readWasmUleb(wasm, q);
        else if (kind === 1) {
          imp.refType = wasm[q++];
          let flags; [flags, q] = readWasmUleb(wasm, q);
          [, q] = readWasmUleb(wasm, q);
          if (flags & 1) [, q] = readWasmUleb(wasm, q);
        }
        else if (kind === 2) {
          const flags = wasm[q++];
          [, q] = readWasmUleb(wasm, q);
          if (flags & 1) [, q] = readWasmUleb(wasm, q);
        } else if (kind === 3) {
          q += 2;
        } else {
          throw new Error(`Unsupported import kind ${kind}`);
        }
        imports.push(imp);
      }
      break;
    }
    pos = sectionEnd;
  }
  return {
    helperFields: new Set(imports.filter((i) => i.kind === 0 && i.module === 'helpers').map((i) => i.field)),
    memories: imports.filter((i) => i.kind === 2).length,
    totalFunctions: imports.filter((i) => i.kind === 0).length,
  };
}

function section(id, payload) {
  return Buffer.concat([Buffer.from([id]), writeWasmUleb(payload.length), payload]);
}

function splitCodeBody(body) {
  const [bodySize, payloadStart] = readWasmUleb(body, 0);
  const payloadEnd = payloadStart + bodySize;
  if (payloadEnd !== body.length) throw new Error('Code body size does not match payload');
  let pos = payloadStart;
  const [localDeclCount, localDeclCountEnd] = readWasmUleb(body, pos);
  pos = localDeclCountEnd;
  for (let i = 0; i < localDeclCount; i++) {
    [, pos] = readWasmUleb(body, pos);
    pos++;
    if (pos > payloadEnd) throw new Error('Code body local declarations extend outside payload');
  }
  return {
    localDecls: Buffer.from(body.subarray(payloadStart, pos)),
    instructions: Buffer.from(body.subarray(pos, payloadEnd)),
  };
}

function buildCodeBody(localDecls, instructions) {
  const payload = Buffer.concat([localDecls, instructions]);
  return Buffer.concat([writeWasmUleb(payload.length), payload]);
}

function parseExactTargetKeys(value) {
  const raw = String(value || '').split(',')
    .map((key) => key.trim().toLowerCase()).filter(Boolean);
  const keys = new Set(raw);
  if (!keys.size || keys.size !== raw.length ||
      [...keys].some((key) => !/^v5-[0-9a-f]{8}-[0-9a-f]{8}$/i.test(key))) {
    throw new Error(`Invalid --mt-internal-clone-target-keys value: ${value}`);
  }
  return keys;
}

function parseInternalCloneExpectedBaseStats(value) {
  const parts = String(value || '').split(',').map((part) => Number(part));
  if (parts.length !== 4 || parts.some((part) => !Number.isSafeInteger(part)) ||
      parts[0] <= 0 || parts[1] <= 0 || parts[2] < 0 || parts[3] < 0) {
    throw new Error(`Invalid --mt-internal-clone-expected-base-stats value: ${value}`);
  }
  return {
    entries: parts[0],
    groups: parts[1],
    directCallPatches: parts[2],
    baseDirectCallsRewritten: parts[3],
  };
}

function validateInternalCloneTargets(entries, requestedKeys) {
  if (!requestedKeys.size) return [];
  const counts = new Map();
  const byKey = new Map();
  for (const entry of entries) {
    counts.set(entry.key, (counts.get(entry.key) || 0) + 1);
    byKey.set(entry.key, entry);
  }
  const targets = [];
  for (const key of [...requestedKeys].sort()) {
    const entry = byKey.get(key);
    if (!entry || counts.get(key) !== 1) {
      throw new Error(`Internal clone target must resolve to one raw manifest entry: ${key}`);
    }
    if (entry.opCount !== 2 || entry.emulatedLen !== 4 || entry.relocCount !== 0) {
      throw new Error(
        `Internal clone target shape mismatch for ${key}: ` +
        `opCount=${entry.opCount} emulatedLen=${entry.emulatedLen} relocCount=${entry.relocCount}`);
    }
    const keyMatch = /^v5-([0-9a-f]{8})-([0-9a-f]{8})$/.exec(key);
    if (!keyMatch || (entry.eip >>> 0) !== (Number.parseInt(keyMatch[1], 16) >>> 0) ||
        (entry.blockHash >>> 0) !== (Number.parseInt(keyMatch[2], 16) >>> 0)) {
      throw new Error(
        `Internal clone target key/EIP/hash identity mismatch for ${key}`);
    }
    for (const kind of ['next1', 'next2', 'jump']) {
      const exit = entry.exits?.[kind];
      if (!exit || Number(exit.count) !== 0 || Number(exit.firstTarget) !== 0) {
        throw new Error(
          `Internal clone target ${key} requires zero-profile dynamic exits; ` +
          `${kind}=${JSON.stringify(exit)}`);
      }
    }
    targets.push(entry);
  }
  return targets;
}

function internalCloneKey(sourceKey, groupId) {
  return `internal-clone:${sourceKey}:group-${String(groupId).padStart(4, '0')}`;
}

function countInternalCloneDirectSites(groupEntries, incomingEdges, sourceEntry, runtime) {
  const edgesByCaller = new Map();
  for (const edge of incomingEdges) {
    if (edge.to !== sourceEntry.key) continue;
    let kinds = edgesByCaller.get(edge.from);
    if (!kinds) {
      kinds = new Set();
      edgesByCaller.set(edge.from, kinds);
    }
    kinds.add(edge.kind);
  }
  let count = 0;
  for (const entry of groupEntries) {
    const kinds = edgesByCaller.get(entry.key);
    if (!kinds) continue;
    const parsed = parseWasmForMerge(entry.data);
    if (parsed.functionTypes.length !== 1) {
      throw new Error('Internal clone preflight requires legacy single-entry modules');
    }
    const targets = new Map([[
      sourceEntry.eip >>> 0,
      {
        index: 0,
        key: sourceEntry.key,
        relocCount: 0,
        internalCloneOnly: true,
        guarded: false,
        kinds,
      },
    ]]);
    const rewritten = rewriteDirectCallsInBody(parsed.codeBodies[0], targets, {
      hotFastPathOnly: true,
      hotNext1: true,
      boundedCyclicFastPaths: false,
      directChainLimit: 0,
      runtime,
      allocRelocArg() {
        throw new Error('Internal clone preflight unexpectedly requested a reloc patch');
      },
    });
    count += rewritten.internalCloneDirectCallsRewritten || 0;
  }
  return count;
}

function validateOriginalRegionEntries(entries, rawManifestSha256) {
  const checked = validateOriginalRegionSpec();
  const spec = NTCURRENTTEB_ORIGINAL_REGION_SPEC;
  if (rawManifestSha256 !== spec.rawManifestSha256) {
    throw new Error(
      `Original-region raw manifest SHA-256 mismatch: got ${rawManifestSha256}, ` +
      `expected ${spec.rawManifestSha256}`);
  }
  if (entries.length !== spec.expected.registeredEntries) {
    throw new Error(
      `Original-region registered-entry mismatch: got ${entries.length}, ` +
      `expected ${spec.expected.registeredEntries}`);
  }
  const counts = new Map();
  const entryByKey = new Map();
  for (const entry of entries) {
    counts.set(entry.key, (counts.get(entry.key) || 0) + 1);
    entryByKey.set(entry.key, entry);
  }
  for (const contract of spec.entries) {
    const entry = entryByKey.get(contract.key);
    if (!entry || counts.get(contract.key) !== 1) {
      throw new Error(
        `Original-region key must resolve to exactly one raw entry: ${contract.key}`);
    }
    const identity = originalRegionKeyIdentity(contract.key);
    if ((entry.eip >>> 0) !== identity.eip ||
        (entry.blockHash >>> 0) !== identity.blockHash ||
        (entry.relocCount || 0) !== contract.relocCount) {
      throw new Error(
        `Original-region key/EIP/hash/reloc mismatch for ${contract.key}: ` +
        `eip=${hex32(entry.eip)} hash=${hex32(entry.blockHash)} ` +
        `reloc=${entry.relocCount || 0}`);
    }
  }
  return {...checked, entryByKey};
}

function applyOriginalRegionLayout(groups, entries) {
  const spec = NTCURRENTTEB_ORIGINAL_REGION_SPEC;
  if (groups.length !== spec.expected.groups) {
    throw new Error(
      `Original-region group-count mismatch before layout: got ${groups.length}, ` +
      `expected ${spec.expected.groups}`);
  }
  const initialGroupByKey = new Map();
  for (let groupId = 0; groupId < groups.length; groupId++) {
    for (const entry of groups[groupId].entries) {
      if (initialGroupByKey.has(entry.key)) {
        throw new Error(`Original-region duplicate grouped key: ${entry.key}`);
      }
      initialGroupByKey.set(entry.key, groupId);
    }
  }
  if (initialGroupByKey.size !== entries.length) {
    throw new Error(
      `Original-region grouped entry population mismatch: ` +
      `${initialGroupByKey.size}/${entries.length}`);
  }
  for (const contract of spec.entries) {
    const actualGroup = initialGroupByKey.get(contract.key);
    if (actualGroup !== contract.originalGroup) {
      throw new Error(
        `Original-region initial group mismatch for ${contract.key}: ` +
        `got ${actualGroup}, expected ${contract.originalGroup}`);
    }
  }

  const hostGroup = groups[spec.expected.hostGroup];
  if (!hostGroup || initialGroupByKey.get(spec.hostKey) !== spec.expected.hostGroup ||
      hostGroup.wasmBytes !== spec.expected.hostGroupBytesBefore) {
    throw new Error(
      `Original-region host group identity/size mismatch: group=${spec.expected.hostGroup} ` +
      `bytes=${hostGroup?.wasmBytes}`);
  }
  let movedEntryBytes = 0;
  for (const contract of spec.entries) {
    if (contract.key === spec.hostKey) continue;
    const sourceGroup = groups[contract.originalGroup];
    const index = sourceGroup.entries.findIndex((entry) => entry.key === contract.key);
    if (index < 0) {
      throw new Error(`Original-region layout source entry missing: ${contract.key}`);
    }
    const [entry] = sourceGroup.entries.splice(index, 1);
    sourceGroup.wasmBytes -= entry.wasmBytes || 0;
    hostGroup.entries.push(entry);
    hostGroup.wasmBytes += entry.wasmBytes || 0;
    movedEntryBytes += entry.wasmBytes || 0;
  }
  if (movedEntryBytes !== spec.expected.movedEntryBytes ||
      hostGroup.wasmBytes !== spec.expected.hostGroupBytesAfter) {
    throw new Error(
      `Original-region pinned layout size mismatch: moved=${movedEntryBytes} ` +
      `hostAfter=${hostGroup.wasmBytes}`);
  }
  if (hostGroup.wasmBytes > spec.expected.budgetBytes ||
      groups.some((group) => group.wasmBytes > spec.expected.budgetBytes)) {
    throw new Error(
      `Original-region pinned layout exceeds ${spec.expected.budgetBytes}-byte budget`);
  }
  const finalKeys = new Set(hostGroup.entries.map((entry) => entry.key));
  for (const contract of spec.entries) {
    if (!finalKeys.has(contract.key)) {
      throw new Error(`Original-region host group lost key: ${contract.key}`);
    }
  }
  const finalPopulation = groups.reduce((sum, group) => sum + group.entries.length, 0);
  if (finalPopulation !== spec.expected.registeredEntries ||
      groups.some((group) => !group.entries.length)) {
    throw new Error(
      `Original-region layout changed registered population/groups: entries=${finalPopulation}`);
  }
  return {
    hostGroup: spec.expected.hostGroup,
    hostGroupBytesBefore: spec.expected.hostGroupBytesBefore,
    movedEntryBytes,
    hostGroupBytesAfter: hostGroup.wasmBytes,
    budgetBytes: spec.expected.budgetBytes,
  };
}

function genericDispatcherPattern(dispatcherIndex, needsFetch) {
  return genericDispatcherTailCall(dispatcherIndex, needsFetch);
}

function rewriteOriginalRegionDispatcherCalls(body, dispatcherIndex, routerIndex,
    targetEip, mode, route) {
  if (mode !== 'control' && mode !== 'candidate' && mode !== 'selective') {
    throw new Error(`Original-region dispatcher rewrite has invalid mode: ${mode}`);
  }
  const split = splitCodeBody(body);
  const instructions = split.instructions;
  const out = [];
  let pos = 0;
  let dispatcherSites = 0;
  let observedStaticEipSites = 0;
  let redirectedSites = 0;
  while (pos < instructions.length) {
    let matched = null;
    let needsFetch = false;
    for (const value of [false, true]) {
      const pattern = genericDispatcherPattern(dispatcherIndex, value);
      if (pos + pattern.length <= instructions.length &&
          instructions.compare(pattern, 0, pattern.length, pos,
            pos + pattern.length) === 0) {
        matched = pattern;
        needsFetch = value;
        break;
      }
    }
    if (!matched) {
      out.push(Buffer.from([instructions[pos++]]));
      continue;
    }
    dispatcherSites++;
    const staticEip = findStaticEipStoreBefore(instructions, pos, 2048);
    const exactStaticTarget = staticEip?.eip === (targetEip >>> 0);
    if (exactStaticTarget) observedStaticEipSites++;
    const redirect = mode === 'candidate' ||
      (mode === 'selective' &&
       (route.dynamicNormalTargetSites > 0 || exactStaticTarget));
    out.push(redirect ? genericDispatcherTailCall(routerIndex, needsFetch) : matched);
    if (redirect) redirectedSites++;
    pos += matched.length;
  }
  return {
    body: redirectedSites ? buildCodeBody(split.localDecls, Buffer.concat(out)) : body,
    dispatcherSites,
    observedStaticEipSites,
    redirectedSites,
    retainedDispatcherSites: dispatcherSites - redirectedSites,
  };
}

export function buildOriginalRegionRouterBody({
  runtime,
  targetIndex,
  dispatcherIndex,
  targetEip,
  targetRelocCount,
  targetRelocArg,
  fetchNextImportIndex,
  directChainLimit,
}) {
  for (const [name, value] of [
    ['targetIndex', targetIndex],
    ['dispatcherIndex', dispatcherIndex],
    ['targetEip', targetEip],
    ['targetRelocCount', targetRelocCount],
    ['fetchNextImportIndex', fetchNextImportIndex],
    ['directChainLimit', directChainLimit],
  ]) {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new Error(`Original-region router invalid ${name}: ${value}`);
    }
  }
  if (!Buffer.isBuffer(targetRelocArg) || !targetRelocArg.length) {
    throw new Error('Original-region router requires one target reloc-base argument');
  }
  const required = [
    ['nextOpOffset', runtime?.cpu?.nextOpOffset],
    ['eipOffset', runtime?.cpu?.eipOffset],
    ['csAddressOffset', runtime?.cpu?.csAddressOffset],
    ['bigOffset', runtime?.cpu?.bigOffset],
    ['yieldOffset', runtime?.cpu?.yieldOffset],
    ['bailoutOffset', runtime?.cpu?.bailoutOffset],
    ['directChainDepthOffset', runtime?.cpu?.directChainDepthOffset],
    ['pfnJitCodeOffset', runtime?.decodedOp?.pfnJitCodeOffset],
  ];
  for (const [name, value] of required) {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new Error(`Original-region router runtime ABI missing ${name}`);
    }
  }
  const locals = appendI32Locals(Buffer.from([0x00]), 4);
  const targetOpLocal = locals.firstIndex;
  const linearEipLocal = locals.firstIndex + 1;
  const relocBaseLocal = locals.firstIndex + 2;
  const pfnLocal = locals.firstIndex + 3;
  const cpu = runtime.cpu;
  const pfnOffset = runtime.decodedOp.pfnJitCodeOffset >>> 0;
  const tokenOffset = (targetRelocCount >>> 0) * 4;
  return buildCodeBody(locals.localDecls, Buffer.concat([
    // Preserve the exact common-dispatch fetch contract before deciding
    // whether the pinned local route is still live.
    inlineFetchNextOpPrelude(runtime, targetOpLocal, fetchNextImportIndex),

    // One loader patch supplies the registered target's reloc-array base.
    // With zero-reloc-token ABI v1 even a relocCount=0 original entry owns
    // U32[1], whose first word is its wrapper publication token.
    targetRelocArg,
    Buffer.from([0x21]), writeWasmUleb(relocBaseLocal),

    // Recompute the same actual linear EIP used by the inline op-cache lookup.
    Buffer.from([0x20, 0x00]), wasmI32Load(cpu.eipOffset >>> 0),
    Buffer.from([0x21]), writeWasmUleb(linearEipLocal),
    Buffer.from([0x20, 0x00]), wasmI32Load(cpu.bigOffset >>> 0),
    Buffer.from([0x45, 0x04, 0x40]),
    Buffer.from([0x20]), writeWasmUleb(linearEipLocal),
    Buffer.from([0x41]), writeWasmSleb(0xffff),
    Buffer.from([0x71, 0x21]), writeWasmUleb(linearEipLocal),
    Buffer.from([0x0b]),
    Buffer.from([0x20]), writeWasmUleb(linearEipLocal),
    Buffer.from([0x20, 0x00]), wasmI32Load(cpu.csAddressOffset >>> 0),
    Buffer.from([0x6a, 0x21]), writeWasmUleb(linearEipLocal),

    // exact linear EIP
    Buffer.from([0x20]), writeWasmUleb(linearEipLocal),
    Buffer.from([0x41]), writeWasmSleb(targetEip | 0),
    Buffer.from([0x46]),
    // live nextOp != null
    Buffer.from([0x20, 0x00]), wasmI32Load(cpu.nextOpOffset >>> 0),
    Buffer.from([0x22]), writeWasmUleb(targetOpLocal),
    Buffer.from([0x45, 0x45, 0x71]),
    // registered target reloc-array != null
    Buffer.from([0x20]), writeWasmUleb(relocBaseLocal),
    Buffer.from([0x45, 0x45, 0x71]),
    // live target publication is nonzero
    Buffer.from([0x20]), writeWasmUleb(targetOpLocal),
    wasmI32AtomicLoad(pfnOffset),
    Buffer.from([0x22]), writeWasmUleb(pfnLocal),
    Buffer.from([0x45, 0x45, 0x71]),
    // target token == live nextOp->pfnJitCode
    Buffer.from([0x20]), writeWasmUleb(relocBaseLocal),
    wasmI32AtomicLoad(tokenOffset),
    Buffer.from([0x20]), writeWasmUleb(pfnLocal),
    Buffer.from([0x46, 0x71]),
    // reloc>0 targets additionally publish exact DecodedOp* at word 0
    ...(targetRelocCount > 0 ? [
      Buffer.from([0x20]), writeWasmUleb(relocBaseLocal),
      wasmI32Load(0),
      Buffer.from([0x20]), writeWasmUleb(targetOpLocal),
      Buffer.from([0x46, 0x71]),
    ] : []),
    // scheduler / invalidation boundaries
    Buffer.from([0x20, 0x00]), wasmI32Load8U(cpu.yieldOffset >>> 0),
    Buffer.from([0x45, 0x71]),
    Buffer.from([0x20, 0x00]), wasmI32Load(cpu.bailoutOffset >>> 0),
    Buffer.from([0x45, 0x71]),
    // Preserve the enclosing generic-indirect chain budget. The static route
    // DAG is only nine edges, but it may be entered after prior dynamic tails.
    Buffer.from([0x20, 0x00]),
    wasmI32Load(cpu.directChainDepthOffset >>> 0),
    Buffer.from([0x41]), writeWasmSleb(directChainLimit),
    Buffer.from([0x49, 0x71, 0x04, 0x40]),

    Buffer.from([0x20, 0x00, 0x20, 0x00]),
    wasmI32Load(cpu.directChainDepthOffset >>> 0),
    Buffer.from([0x41, 0x01, 0x6a]),
    wasmI32Store(cpu.directChainDepthOffset >>> 0),

    Buffer.from([0x20, 0x00]),
    Buffer.from([0x20]), writeWasmUleb(relocBaseLocal),
    Buffer.from([0x12]), writeWasmUleb(targetIndex),
    Buffer.from([0x0b]),

    // Fetch has already run (or was not requested), so mismatch falls back to
    // the exact existing common dispatcher without repeating the fetch.
    Buffer.from([0x20, 0x00, 0x41, 0x00, 0x12]),
    writeWasmUleb(dispatcherIndex),
    Buffer.from([0x0b]),
  ]));
}

function rewriteSingleCallIndexInBody(body, oldIndex, newIndex) {
  const { localDecls, instructions } = splitCodeBody(body);
  let matchStart = -1;
  let matchEnd = -1;
  let matches = 0;
  for (let pos = 0; pos < instructions.length; pos++) {
    if (instructions[pos] !== 0x10) continue;
    try {
      const [index, end] = readWasmUleb(instructions, pos + 1);
      if (index === oldIndex) {
        matchStart = pos;
        matchEnd = end;
        matches++;
      }
    } catch {
      // The exact generated wrapper must contain one valid direct call.
    }
  }
  if (matches !== 1) {
    throw new Error(
      `Dual-entry wrapper call relocation expected 1 match, found ${matches}`);
  }
  return buildCodeBody(localDecls, Buffer.concat([
    instructions.subarray(0, matchStart),
    Buffer.from([0x10]), writeWasmUleb(newIndex),
    instructions.subarray(matchEnd),
  ]));
}

function appendI32Locals(localDecls, count, parameterCount = 2) {
  const [declCount, declStart] = readWasmUleb(localDecls, 0);
  let q = declStart;
  let existingLocals = 0;
  for (let i = 0; i < declCount; i++) {
    let n; [n, q] = readWasmUleb(localDecls, q);
    existingLocals += n;
    q++; // value type
  }
  if (q !== localDecls.length) throw new Error('Unexpected bytes after local declarations');
  return {
    firstIndex: parameterCount + existingLocals,
    localDecls: Buffer.concat([
      writeWasmUleb(declCount + 1),
      localDecls.subarray(declStart),
      writeWasmUleb(count), Buffer.from([0x7f]),
    ]),
  };
}

function appendWorkerTableImport(importPayload) {
  const [count, itemsStart] = readWasmUleb(importPayload, 0);
  return Buffer.concat([
    writeWasmUleb(count + 1),
    importPayload.subarray(itemsStart),
    wasmNameBytes('env'), wasmNameBytes('table'),
    Buffer.from([0x01, 0x70, 0x00, 0x00]), // table, funcref, min=0
  ]);
}

// Globals for a merged group module: just the mutable direct-call depth
// counter (when directCallDepth > 0). Reloc values flow through each block
// function's second parameter, never through module state — a module-level
// register is not re-entrancy safe (signals and blocking syscalls nest other
// group functions mid-block; see Wasm-JIT-Grouped-Reloc-Parity.md).
function mergedGlobalsSection(directCallDepth, genericLastSlotCache = false) {
  const count = (directCallDepth > 0 ? 1 : 0) + (genericLastSlotCache ? 1 : 0);
  if (!count) return [];
  const item = Buffer.from([
    0x7f, 0x01, // i32 mutable
    0x41, 0x00, // i32.const 0
    0x0b,       // end
  ]);
  return [section(6, Buffer.concat([
    writeWasmUleb(count),
    ...(directCallDepth > 0 ? [item] : []),
    ...(genericLastSlotCache ? [item] : []),
  ]))];
}

// 5-byte padded SLEB128 of a 32-bit value (the encoding i32.const placeholders
// use so the loader can patch them in place without shifting offsets).
function paddedSleb32(v) {
  return Buffer.from([
    (v & 0x7f) | 0x80,
    ((v >>> 7) & 0x7f) | 0x80,
    ((v >>> 14) & 0x7f) | 0x80,
    ((v >>> 21) & 0x7f) | 0x80,
    ((v >>> 28) & 0x0f) | ((v & 0x80000000) ? 0x70 : 0),
  ]);
}

// Second argument of a rewritten direct call: the *target* block's reloc
// slot array, as an i32.const marker placeholder the loader patches with the
// real address (0 when the target has no array — the callee's guards then
// take the slow paths). Per call argument = per activation, so nested guest
// execution can never alias another block's array.
function relocArgPlaceholder(marker) {
  return Buffer.concat([Buffer.from([0x41]), paddedSleb32(marker)]);
}

function findStaticEipStoreBefore(instructions, end, maxLookback = 128) {
  const start = Math.max(0, end - maxLookback);
  let best = null;
  for (let pos = start; pos < end; pos++) {
    if (instructions[pos] !== 0x20 || instructions[pos + 1] !== 0x00 || instructions[pos + 2] !== 0x41) continue;
    let value;
    let p;
    try {
      [value, p] = readWasmSleb(instructions, pos + 3);
    } catch {
      continue;
    }
    if (p >= end || instructions[p++] !== 0x36) continue; // i32.store
    let align;
    let offset;
    let q;
    try {
      [align, q] = readWasmUleb(instructions, p);
      [offset, q] = readWasmUleb(instructions, q);
    } catch {
      continue;
    }
    if (align <= 2 && offset === 112 && q <= end) {
      best = { eip: value >>> 0, end: q };
    }
  }
  return best;
}

function directCallTail(targetIndex, relocArg) {
  return Buffer.concat([
    Buffer.from([0x20, 0x00]),
    relocArg,
    Buffer.from([0x10]),
    writeWasmUleb(targetIndex),
    Buffer.from([0x0f]),
  ]);
}

function threadedReturnCallTail(targetIndex, relocArg) {
  return Buffer.concat([
    Buffer.from([0x20, 0x00]),
    relocArg,
    ...Array.from({ length: 8 }, (_, i) => Buffer.from([0x20, 0x02 + i])),
    Buffer.from([0x12]),
    writeWasmUleb(targetIndex),
  ]);
}

function threadedTailCensusCall(helperIndex) {
  return Buffer.concat([
    Buffer.from([0x20, 0x00, 0x10]), // local.get cpu; call helper
    writeWasmUleb(helperIndex),
  ]);
}

const THREADED_GP_CHAIN_LIMIT = 64;

function guardedThreadedReturnCallTail(
    targetIndex, targetRelocCount, runtime, fallbackBytes,
    relocTargetNonzeroArg, relocWrapperArg, relocTargetPfnArg,
    relocTypedNonzeroArg, relocTypedExactArg, relocWrapperExactArg,
    relocNextOpArg, relocCallArg, census = null) {
  const depthOffset = runtime.cpu.directChainDepthOffset >>> 0;
  const bailoutOffset = runtime.cpu.bailoutOffset >>> 0;
  const yieldOffset = runtime.cpu.yieldOffset >>> 0;
  const wrapperTokenOffset = (targetRelocCount >>> 0) * 4;
  const typedTokenOffset = ((targetRelocCount >>> 0) + 1) * 4;
  return Buffer.concat([
    // A grouped direct edge is valid only while the target's exact two-slot
    // publication remains live.  The target op pointer itself must be present;
    // its atomic pfnJitCode must still match the wrapper token; and the typed
    // token must be the non-zero adjacent slot published by the pair loader.
    // Retirement clears pfnJitCode before quiescent table-slot reaping, so a
    // stale reloc array always falls through to the original dispatcher path.
    relocTargetNonzeroArg, wasmI32Load(0),
    Buffer.from([0x45, 0x45]), // i32.eqz; i32.eqz
    relocWrapperArg, wasmI32AtomicLoad(wrapperTokenOffset),
    relocTargetPfnArg, wasmI32Load(0),
    wasmI32AtomicLoad(runtime.decodedOp.pfnJitCodeOffset >>> 0),
    Buffer.from([0x46, 0x71]), // i32.eq; i32.and
    relocTypedNonzeroArg, wasmI32AtomicLoad(typedTokenOffset),
    Buffer.from([0x45, 0x45, 0x71]), // nonzero; i32.and
    relocTypedExactArg, wasmI32AtomicLoad(typedTokenOffset),
    relocWrapperExactArg, wasmI32AtomicLoad(wrapperTokenOffset),
    Buffer.from([0x41, 0x01, 0x6a, 0x46, 0x71]), // wrapper+1; eq; and

    // Do not cross an observable dispatcher boundary while a bailout/yield is
    // pending, and cap the delay before asynchronous termination is observed.
    Buffer.from([0x20, 0x00]), wasmI32Load(bailoutOffset),
    Buffer.from([0x45, 0x71]), // i32.eqz; i32.and publication readiness
    Buffer.from([0x20, 0x00]), wasmI32Load8U(yieldOffset),
    Buffer.from([0x45, 0x71]), // i32.eqz; i32.and
    Buffer.from([0x20, 0x00]), wasmI32Load(depthOffset),
    Buffer.from([0x41]), writeWasmSleb(THREADED_GP_CHAIN_LIMIT),
    Buffer.from([0x49, 0x71]), // i32.lt_u; i32.and
    Buffer.from([0x04, 0x40]), // if void
    ...(census ? [threadedTailCensusCall(census.admitted)] : []),
    Buffer.from([0x20, 0x00, 0x20, 0x00]),
    wasmI32Load(depthOffset),
    Buffer.from([0x41, 0x01, 0x6a]),
    wasmI32Store(depthOffset),
    // Preserve the original terminal state update without re-entering the
    // decoder: the exact target DecodedOp* was already publication-checked
    // above, so carry it directly into cpu->nextOp before the typed tail.
    // The fallback retains its original GP stores + helpers.fn_6 path.
    Buffer.from([0x20, 0x00]),
    relocNextOpArg, wasmI32Load(0),
    wasmI32Store(runtime.cpu.nextOpOffset >>> 0),
    threadedReturnCallTail(targetIndex, relocCallArg),
    Buffer.from([0x0b]), // end
    fallbackBytes,
  ]);
}

function matchThreadedFetchReturn(instructions, pos) {
  if (instructions[pos] !== 0x20 || instructions[pos + 1] !== 0x00 ||
      instructions[pos + 2] !== 0x10 || instructions[pos + 3] !== 0x06) {
    return 0;
  }
  let q = pos + 4;
  for (let i = 0; i < 8; i++) {
    if (instructions[q++] !== 0x20 || instructions[q++] !== 0x02 + i) return 0;
  }
  if (instructions[q] === 0x0f) return q + 1 - pos;
  // Binaryen removes a redundant explicit return when this tuple is the
  // function's terminal expression. Consume through the tuple but leave the
  // structural function-end opcode after the replacement return_call.
  if (instructions[q] === 0x0b) return q - pos;
  return 0;
}

// The threaded internal body starts with all eight GP parameters dirty.  For
// helper-free blocks, the legacy terminal path therefore emits one contiguous
// architectural writeback for eax..edi immediately before helpers.fn_6.  A
// direct typed tail carries those same locals to the target, so keeping this
// writeback would defeat the ABI.  Match only the complete, strictly
// contiguous eight-register shape; partial or ambiguous suffixes remain on
// the dispatcher path unchanged.
function matchThreadedGpStoreSuffix(
    instructions, end, expectedFirstOffset, maxLookback = 128) {
  const startLimit = Math.max(0, end - maxLookback);
  for (let start = startLimit; start < end; start++) {
    let q = start;
    let firstOffset = null;
    let valid = true;
    for (let i = 0; i < 8; i++) {
      if (instructions[q++] !== 0x20 || instructions[q++] !== 0x00 ||
          instructions[q++] !== 0x20 || instructions[q++] !== 0x02 + i ||
          instructions[q++] !== 0x36) {
        valid = false;
        break;
      }
      let align;
      let offset;
      try {
        [align, q] = readWasmUleb(instructions, q);
        [offset, q] = readWasmUleb(instructions, q);
      } catch {
        valid = false;
        break;
      }
      if (align !== 2) {
        valid = false;
        break;
      }
      if (firstOffset === null) firstOffset = offset >>> 0;
      if ((offset >>> 0) !== ((firstOffset + i * 4) >>> 0)) {
        valid = false;
        break;
      }
    }
    if (valid && q === end && firstOffset === (expectedFirstOffset >>> 0)) {
      return { start, length: end - start, firstOffset };
    }
  }
  return null;
}

function findThreadedWrapperGpLoadBase(body, internalIndex) {
  const {instructions} = splitCodeBody(body);
  const matches = [];
  for (let start = 0; start < instructions.length; start++) {
    let q = start;
    if (instructions[q++] !== 0x20 || instructions[q++] !== 0x00 ||
        instructions[q++] !== 0x20 || instructions[q++] !== 0x01) {
      continue;
    }
    let firstOffset = null;
    let valid = true;
    for (let i = 0; i < 8; i++) {
      if (instructions[q++] !== 0x20 || instructions[q++] !== 0x00 ||
          instructions[q++] !== 0x28) {
        valid = false;
        break;
      }
      let align;
      let offset;
      try {
        [align, q] = readWasmUleb(instructions, q);
        [offset, q] = readWasmUleb(instructions, q);
      } catch {
        valid = false;
        break;
      }
      if (align !== 2) {
        valid = false;
        break;
      }
      if (firstOffset === null) firstOffset = offset >>> 0;
      if ((offset >>> 0) !== ((firstOffset + i * 4) >>> 0)) {
        valid = false;
        break;
      }
    }
    if (!valid || instructions[q++] !== 0x10) continue;
    let target;
    try {
      [target, q] = readWasmUleb(instructions, q);
    } catch {
      continue;
    }
    if (target === (internalIndex >>> 0)) matches.push(firstOffset);
  }
  if (matches.length !== 1) {
    throw new Error(
      `Threaded wrapper GP load layout is ${matches.length === 0 ? 'missing' : 'ambiguous'}`);
  }
  return matches[0] >>> 0;
}

function trimOutputTail(out, byteCount) {
  let remaining = byteCount;
  while (remaining > 0 && out.length) {
    const last = out[out.length - 1];
    if (last.length <= remaining) {
      remaining -= last.length;
      out.pop();
    } else {
      out[out.length - 1] = last.subarray(0, last.length - remaining);
      remaining = 0;
    }
  }
  if (remaining !== 0) {
    throw new Error(`Cannot sink ${byteCount} threaded GP-store bytes from emitted prefix`);
  }
}

function returnCallTail(targetIndex, relocArg) {
  return Buffer.concat([
    Buffer.from([0x20, 0x00]),
    relocArg,
    Buffer.from([0x12]),
    writeWasmUleb(targetIndex),
  ]);
}

function wasmI32Load(offset, align = 2) {
  return Buffer.concat([Buffer.from([0x28]), writeWasmUleb(align), writeWasmUleb(offset)]);
}

function wasmI32Store(offset, align = 2) {
  return Buffer.concat([Buffer.from([0x36]), writeWasmUleb(align), writeWasmUleb(offset)]);
}

function wasmI32Load8U(offset) {
  return Buffer.concat([Buffer.from([0x2d]), writeWasmUleb(0), writeWasmUleb(offset)]);
}

function wasmI32AtomicLoad(offset) {
  // 0xfe 0x10 = i32.atomic.load; natural i32 alignment is required.
  return Buffer.concat([Buffer.from([0xfe, 0x10]), writeWasmUleb(2), writeWasmUleb(offset)]);
}

function wasmI32Store8(offset) {
  return Buffer.concat([Buffer.from([0x3a]), writeWasmUleb(0), writeWasmUleb(offset)]);
}

function readIndexedInstruction(instructions, pos, opcode) {
  if (instructions[pos] !== opcode) return null;
  try {
    const [index, end] = readWasmUleb(instructions, pos + 1);
    return { index, end };
  } catch {
    return null;
  }
}

function readMemoryInstruction(instructions, pos, opcode) {
  if (instructions[pos] !== opcode) return null;
  try {
    let align;
    let offset;
    let end;
    [align, end] = readWasmUleb(instructions, pos + 1);
    [offset, end] = readWasmUleb(instructions, end);
    return { align, offset, end };
  } catch {
    return null;
  }
}

// Match the exact structured fast-success arm emitted by persistence-mode
// blockNext2 after the local Binaryen passes:
//
//   cpu->eip = staticTarget;
//   if ((target = relocBase) &&
//       (target = target[slot]) &&
//       (target = target->next)) {
//     cpu->nextOp = target;
//     return;
//   }
//
// The returned targetLocal is still live after the nextOp store.  Anchoring
// the match at the static EIP store and checking all three nested if headers
// keeps unrelated `store; return` sequences out of the rewrite.
function matchNext2FastSuccessArm(instructions, storeStart, targetIndexByEip,
    allowGuarded = false, structureOnlyNextOpOffset = null) {
  let q = storeStart;
  const cpuGet = readIndexedInstruction(instructions, q, 0x20); // local.get
  if (!cpuGet || cpuGet.index !== 0) return null;
  q = cpuGet.end;
  const targetGet = readIndexedInstruction(instructions, q, 0x20);
  if (!targetGet) return null;
  const targetLocal = targetGet.index;
  q = targetGet.end;
  const nextOpStore = readMemoryInstruction(instructions, q, 0x36); // i32.store
  if (!nextOpStore || instructions[nextOpStore.end] !== 0x0f ||
      (structureOnlyNextOpOffset !== null &&
       nextOpStore.offset !== (structureOnlyNextOpOffset >>> 0))) return null; // return
  const returnPos = nextOpStore.end;

  const eipStore = findStaticEipStoreBefore(instructions, storeStart, 192);
  if (!eipStore) return null;
  const target = structureOnlyNextOpOffset === null
    ? targetIndexByEip.get(eipStore.eip) : null;
  if (structureOnlyNextOpOffset === null &&
      (!target || !target.kinds?.has('next2') ||
       (target.guarded && !allowGuarded) ||
       (!(target.relocCount > 0) && !target.internalCloneOnly))) return null;

  // Validate the nested relocBase -> slot -> live op->next chain between the
  // EIP store and this success arm. Binaryen may coalesce the scratch local,
  // so read its index instead of assuming the emitter's original local.
  q = eipStore.end;
  const relocGet = readIndexedInstruction(instructions, q, 0x20);
  if (!relocGet || relocGet.index !== 1) return null;
  q = relocGet.end;
  let tee = readIndexedInstruction(instructions, q, 0x22); // local.tee
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null; // if void

  let localGet = readIndexedInstruction(instructions, q, 0x20);
  if (!localGet || localGet.index !== targetLocal) return null;
  q = localGet.end;
  let load = readMemoryInstruction(instructions, q, 0x28); // i32.load reloc slot
  if (!load) return null;
  q = load.end;
  tee = readIndexedInstruction(instructions, q, 0x22);
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;

  localGet = readIndexedInstruction(instructions, q, 0x20);
  if (!localGet || localGet.index !== targetLocal) return null;
  q = localGet.end;
  load = readMemoryInstruction(instructions, q, 0x28); // i32.load op->next
  if (!load || load.offset !== 0) return null;
  q = load.end;
  tee = readIndexedInstruction(instructions, q, 0x22);
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40 || q !== storeStart) return null;

  return { target, targetLocal, returnPos };
}

// blockNext1 has the same three semantic guards as next2, but its middle
// value is a nextJump cache-slot pointer and the final load obtains the live
// target from that slot. Binaryen emits two exact shapes in the Azrok corpus:
// it either keeps the initial local.tee, or tests relocBase directly and uses
// parameter local 1 as the base of the first load. Keep this matcher separate
// from next2 so a future optimizer change cannot silently broaden both paths.
function matchNext1FastSuccessArm(instructions, storeStart, targetIndexByEip,
    allowGuarded = false, structureOnlyNextOpOffset = null) {
  let q = storeStart;
  const cpuGet = readIndexedInstruction(instructions, q, 0x20);
  if (!cpuGet || cpuGet.index !== 0) return null;
  q = cpuGet.end;
  const targetGet = readIndexedInstruction(instructions, q, 0x20);
  if (!targetGet) return null;
  const targetLocal = targetGet.index;
  q = targetGet.end;
  const nextOpStore = readMemoryInstruction(instructions, q, 0x36);
  if (!nextOpStore || instructions[nextOpStore.end] !== 0x0f ||
      (structureOnlyNextOpOffset !== null &&
       nextOpStore.offset !== (structureOnlyNextOpOffset >>> 0))) return null;
  const returnPos = nextOpStore.end;

  const eipStore = findStaticEipStoreBefore(instructions, storeStart, 192);
  if (!eipStore) return null;
  const target = structureOnlyNextOpOffset === null
    ? targetIndexByEip.get(eipStore.eip) : null;
  if (structureOnlyNextOpOffset === null &&
      (!target || !target.kinds?.has('next1') ||
       (target.guarded && !allowGuarded) ||
       (!(target.relocCount > 0) && !target.internalCloneOnly))) return null;

  q = eipStore.end;
  const relocGet = readIndexedInstruction(instructions, q, 0x20);
  if (!relocGet || relocGet.index !== 1) return null;
  q = relocGet.end;

  let firstLoadBaseLocal;
  let shape;
  let tee = readIndexedInstruction(instructions, q, 0x22);
  if (tee && tee.index === targetLocal) {
    // Shape A: local.get 1; local.tee target; if; load(target, slot)
    q = tee.end;
    if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;
    firstLoadBaseLocal = targetLocal;
    shape = 'tee-base';
  } else {
    // Shape B: local.get 1; if; load(local 1, slot); local.tee target
    if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;
    firstLoadBaseLocal = 1;
    shape = 'direct-base';
  }

  let localGet = readIndexedInstruction(instructions, q, 0x20);
  if (!localGet || localGet.index !== firstLoadBaseLocal) return null;
  q = localGet.end;
  let load = readMemoryInstruction(instructions, q, 0x28); // reloc slot -> nextJump cache-slot*
  if (!load) return null;
  q = load.end;
  tee = readIndexedInstruction(instructions, q, 0x22);
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;

  localGet = readIndexedInstruction(instructions, q, 0x20);
  if (!localGet || localGet.index !== targetLocal) return null;
  q = localGet.end;
  load = readMemoryInstruction(instructions, q, 0x28); // *nextJump cache-slot -> live target
  if (!load || load.offset !== 0) return null;
  q = load.end;
  tee = readIndexedInstruction(instructions, q, 0x22);
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40 || q !== storeStart) return null;

  return { target, targetLocal, returnPos, shape };
}

// Match only the persistence-mode success arm emitted by
// JitWasmCodeGen::JumpIfCondition. Unlike the legacy generic jump scan, this
// proves the complete relocBase -> target slot -> cpu->nextOp structure and
// never infers a destination by scanning across structured-control arms.
function matchJumpFastSuccessArm(instructions, storeStart, targetIndexByEip,
    allowGuarded = false, structureOnlyNextOpOffset = null) {
  let q = storeStart;
  const cpuGet = readIndexedInstruction(instructions, q, 0x20);
  if (!cpuGet || cpuGet.index !== 0) return null;
  q = cpuGet.end;
  const targetGet = readIndexedInstruction(instructions, q, 0x20);
  if (!targetGet) return null;
  const targetLocal = targetGet.index;
  q = targetGet.end;
  const nextOpStore = readMemoryInstruction(instructions, q, 0x36);
  if (!nextOpStore || instructions[nextOpStore.end] !== 0x0f ||
      (structureOnlyNextOpOffset !== null &&
       nextOpStore.offset !== (structureOnlyNextOpOffset >>> 0))) return null;
  const returnPos = nextOpStore.end;

  const eipStore = findStaticEipStoreBefore(instructions, storeStart, 192);
  if (!eipStore) return null;
  const target = structureOnlyNextOpOffset === null
    ? targetIndexByEip.get(eipStore.eip) : null;
  if (structureOnlyNextOpOffset === null &&
      (!target || !target.kinds?.has('jump') ||
       (target.guarded && !allowGuarded) || !(target.relocCount > 0))) return null;

  q = eipStore.end;
  const relocGet = readIndexedInstruction(instructions, q, 0x20);
  if (!relocGet || relocGet.index !== 1) return null;
  q = relocGet.end;

  let firstLoadBaseLocal;
  let shape;
  let tee = readIndexedInstruction(instructions, q, 0x22);
  if (tee && tee.index === targetLocal) {
    q = tee.end;
    if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;
    firstLoadBaseLocal = targetLocal;
    shape = 'tee-base';
  } else {
    if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40) return null;
    firstLoadBaseLocal = 1;
    shape = 'direct-base';
  }

  const localGet = readIndexedInstruction(instructions, q, 0x20);
  if (!localGet || localGet.index !== firstLoadBaseLocal) return null;
  q = localGet.end;
  const load = readMemoryInstruction(instructions, q, 0x28);
  if (!load) return null;
  q = load.end;
  tee = readIndexedInstruction(instructions, q, 0x22);
  if (!tee || tee.index !== targetLocal) return null;
  q = tee.end;
  if (instructions[q++] !== 0x04 || instructions[q++] !== 0x40 || q !== storeStart) return null;

  return { target, targetLocal, returnPos, shape };
}

function guardedFastDirectTail(targetIndex, targetLocal, targetRelocCount, runtime,
    relocIdentityArg, relocPublicationArg, relocCallArg,
    boundedCyclic = false, directChainLimit = 0) {
  const guards = [
    // The target array is allocated because the pipeline only admits targets
    // with relocCount > 0. Slot 0 is the target block-start DecodedOp. It must
    // still equal the live branch target accepted by the original fast arm.
    relocIdentityArg,
    wasmI32Load(0),
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    Buffer.from([0x46]), // i32.eq

    // Pointer identity alone survives invalidation until quiescent reaping.
    // The imported MT loader appends the exact table publication token after
    // the ordinary relocation words. Compare it against the target op's
    // atomic pfnJitCode immediately before the direct call. Invalidation
    // clears pfnJitCode, and a different block hash/version republishes a
    // different table slot, so both stale cases fall back to the dispatcher.
    relocPublicationArg,
    wasmI32AtomicLoad((targetRelocCount >>> 0) * 4),
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    wasmI32AtomicLoad(runtime.decodedOp.pfnJitCodeOffset >>> 0),
    Buffer.from([0x46, 0x71]), // i32.eq; i32.and

    // MT direct chains are finite (cyclic-touching edges are excluded), but
    // honor an already-raised scheduler yield before extending the chain.
    Buffer.from([0x20, 0x00]),
    wasmI32Load8U(runtime.cpu.yieldOffset >>> 0),
    Buffer.from([0x45, 0x71]), // i32.eqz; i32.and
  ];
  const bounded = boundedCyclic ? [
    // A self-modifying-code helper may have invalidated the active block even
    // though the live target slot still matches. Do not extend that frame.
    Buffer.from([0x20, 0x00]),
    wasmI32Load(runtime.cpu.bailoutOffset >>> 0),
    Buffer.from([0x45, 0x71]), // i32.eqz; i32.and

    // The depth is per CPU and initialized/restored by the C++ call wrapper.
    // Increment only on the taken direct edge; a failed guard preserves the
    // original dispatcher return immediately below.
    Buffer.from([0x20, 0x00]),
    wasmI32Load(runtime.cpu.directChainDepthOffset >>> 0),
    Buffer.from([0x41]), writeWasmSleb(directChainLimit),
    Buffer.from([0x49, 0x71]), // i32.lt_u; i32.and
    Buffer.from([0x04, 0x40]), // if void

    Buffer.from([0x20, 0x00, 0x20, 0x00]),
    wasmI32Load(runtime.cpu.directChainDepthOffset >>> 0),
    Buffer.from([0x41, 0x01, 0x6a]),
    wasmI32Store(runtime.cpu.directChainDepthOffset >>> 0),
  ] : [
    Buffer.from([0x04, 0x40]), // if void
  ];
  return Buffer.concat([
    ...guards,
    ...bounded,
    Buffer.from([0x20, 0x00]),
    relocCallArg,
    Buffer.from([0x12]), writeWasmUleb(targetIndex), // return_call
    Buffer.from([0x0b, 0x0f]), // end; original dispatcher return fallback
  ]);
}

// An internal clone deliberately has no export, loader registration, reloc
// array, or publication token of its own. The opt-in allowlist pins the exact
// raw cache key and a zero-reloc dynamic-return shape. The original fast arm
// immediately above this tail has still resolved a live DecodedOp for the
// exact static EIP and stored it in CPU::nextOp. Requiring a nonzero published
// pfn, no bailout and no yield keeps pre-publication/invalidation/scheduler
// boundaries on the original dispatcher return without adding a cross-group
// reloc patch that the loader cannot resolve.
function internalCloneFastTail(targetIndex, targetLocal, runtime) {
  return Buffer.concat([
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    wasmI32AtomicLoad(runtime.decodedOp.pfnJitCodeOffset >>> 0),
    Buffer.from([0x45, 0x45]), // !!target->pfnJitCode

    Buffer.from([0x20, 0x00]),
    wasmI32Load(runtime.cpu.bailoutOffset >>> 0),
    Buffer.from([0x45, 0x71]), // !bailout; and

    Buffer.from([0x20, 0x00]),
    wasmI32Load8U(runtime.cpu.yieldOffset >>> 0),
    Buffer.from([0x45, 0x71]), // !yield; and
    Buffer.from([0x04, 0x40]), // if void

    Buffer.from([0x20, 0x00, 0x41, 0x00]), // cpu, relocBase=0
    Buffer.from([0x12]), writeWasmUleb(targetIndex), // return_call clone
    Buffer.from([0x0b, 0x0f]), // end; original dispatcher return fallback
  ]);
}

function yieldAwareReturnCallTail(targetIndex, opCount, runtime, relocArg) {
  const blockInstructionCountOffset = runtime.cpu.blockInstructionCountOffset >>> 0;
  const yieldOffset = runtime.cpu.yieldOffset >>> 0;
  const contextTimeRemainingPtr = runtime.scheduler.contextTimeRemainingPtr >>> 0;
  const count = Math.max(1, opCount >>> 0);
  return Buffer.concat([
    // cpu->blockInstructionCount += opCount
    Buffer.from([0x20, 0x00, 0x20, 0x00]),
    wasmI32Load(blockInstructionCountOffset),
    Buffer.from([0x41]), writeWasmSleb(count),
    Buffer.from([0x6a]),
    wasmI32Store(blockInstructionCountOffset),

    // if ((int)cpu->blockInstructionCount >= contextTimeRemaining || cpu->yield)
    Buffer.from([0x20, 0x00]),
    wasmI32Load(blockInstructionCountOffset),
    Buffer.from([0x41]), writeWasmSleb(contextTimeRemainingPtr | 0),
    wasmI32Load(0),
    Buffer.from([0x4e]), // i32.ge_s
    Buffer.from([0x20, 0x00]),
    wasmI32Load8U(yieldOffset),
    Buffer.from([0x72]), // i32.or
    Buffer.from([0x04, 0x40]), // if
    Buffer.from([0x20, 0x00, 0x41, 0x01]),
    wasmI32Store8(yieldOffset),
    Buffer.from([0x20, 0x00, 0x10, 0x06, 0x0f]), // call fetchNextOp; return
    Buffer.from([0x0b]), // end

    Buffer.from([0x20, 0x00]),
    relocArg,
    Buffer.from([0x12]), writeWasmUleb(targetIndex),
  ]);
}

function guardedDirectCallTail(targetIndex, depthLimit, relocArg) {
  return Buffer.concat([
    Buffer.from([0x23, 0x00]),               // global.get depth
    Buffer.from([0x41]), writeWasmSleb(depthLimit),
    Buffer.from([0x49]),                     // i32.lt_u
    Buffer.from([0x04, 0x40]),               // if
    Buffer.from([0x23, 0x00, 0x41]), writeWasmSleb(1), Buffer.from([0x6a, 0x24, 0x00]), // depth++
    Buffer.from([0x20, 0x00]),
    relocArg,
    Buffer.from([0x10]), writeWasmUleb(targetIndex),
    Buffer.from([0x23, 0x00, 0x41]), writeWasmSleb(1), Buffer.from([0x6b, 0x24, 0x00]), // depth--
    Buffer.from([0x0f, 0x0b]),               // return; end
    Buffer.from([0x20, 0x00, 0x10, 0x06, 0x0f]), // original helper fallback
  ]);
}

function genericIndirectTail(runtime, blockTypeIndex, targetLocal, slotLocal, depthLocal,
    depthLimit, plainRelocMapLoad = false, depthSnapshotLocal = false,
    tablePregrown = false, workerReadyBitmapOffset = 0, workerReadyBitmapLocal = 0,
    callRef = false, typedRefLocal = 0, lastSlotCache = false,
    lastSlotGlobalIndex = 0) {
  const nextOpOffset = runtime.cpu.nextOpOffset >>> 0;
  const pfnOffset = runtime.decodedOp.pfnJitCodeOffset >>> 0;
  const depthOffset = runtime.cpu.directChainDepthOffset >>> 0;
  const bailoutOffset = runtime.cpu.bailoutOffset >>> 0;
  const yieldOffset = runtime.cpu.yieldOffset >>> 0;
  const mapBase = runtime.jit.indirectRelocMapBase >>> 0;
  const mapCapacity = runtime.jit.indirectRelocMapCapacity >>> 0;
  return Buffer.concat([
    // fetchNextOp has just refreshed cpu->nextOp from the live op cache. A
    // null target (including thread termination) keeps the original return.
    Buffer.from([0x20, 0x00]), wasmI32Load(nextOpOffset),
    Buffer.from([0x22]), writeWasmUleb(targetLocal), // local.tee targetOp
    Buffer.from([0x45, 0x45, 0x04, 0x40]),          // !!target; if

    // Load the exact published table slot once. Generic mode makes every
    // pfn publication atomic and sets the owner-wide relocation hazard.
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    wasmI32AtomicLoad(pfnOffset),
    Buffer.from([0x22]), writeWasmUleb(slotLocal),
    Buffer.from([0x45, 0x45]),                       // slot != 0
    Buffer.from([0x20]), writeWasmUleb(slotLocal),
    Buffer.from([0x41]), writeWasmSleb(mapCapacity | 0),
    Buffer.from([0x49, 0x71]),                       // slot < capacity; and
    Buffer.from([0x20, 0x00]), wasmI32Load8U(yieldOffset),
    Buffer.from([0x45, 0x71]),                       // !yield; and
    Buffer.from([0x20, 0x00]), wasmI32Load(bailoutOffset),
    Buffer.from([0x45, 0x71]),                       // !bailout; and
    Buffer.from([0x20, 0x00]), wasmI32Load(depthOffset),
    ...(depthSnapshotLocal ? [Buffer.from([0x22]), writeWasmUleb(depthLocal)] : []),
    Buffer.from([0x41]), writeWasmSleb(depthLimit),
    Buffer.from([0x49, 0x71, 0x04, 0x40]),           // depth < limit; if

    // wasmTable is worker-local. The default path proves bounds/non-null with
    // reference-table operations. Bitmap mode instead first rejects a zero
    // worker-row pointer, then requires the post-table.set byte for this exact
    // slot. A miss returns to C++ for the existing lazy install/backfill path.
    ...(lastSlotCache ? [
      // `$fallback` encloses `$ready`. A same-instance slot hit exits only
      // `$ready` and reaches the call. A slow-path bounds/null miss exits
      // `$fallback`, skipping the call and returning to the C++ installer.
      Buffer.from([0x02, 0x40, 0x02, 0x40]), // block fallback; block ready
      Buffer.from([0x20]), writeWasmUleb(slotLocal),
      Buffer.from([0x23]), writeWasmUleb(lastSlotGlobalIndex),
      Buffer.from([0x46, 0x0d, 0x00]), // eq; br_if ready
      ...(tablePregrown ? [] : [
        Buffer.from([0x20]), writeWasmUleb(slotLocal),
        Buffer.from([0xfc, 0x10, 0x00, 0x4f, 0x0d, 0x01]), // table.size; ge_u; br_if fallback
      ]),
      Buffer.from([0x20]), writeWasmUleb(slotLocal),
      Buffer.from([0x25, 0x00, 0xd1, 0x0d, 0x01]), // table.get; ref.is_null; br_if fallback
      Buffer.from([0x20]), writeWasmUleb(slotLocal),
      Buffer.from([0x24]), writeWasmUleb(lastSlotGlobalIndex), // global.set after proof
      Buffer.from([0x0b]), // end ready
    ] : workerReadyBitmapOffset ? [
      ...(tablePregrown ? [] : [
        Buffer.from([0x20]), writeWasmUleb(slotLocal),
        Buffer.from([0xfc, 0x10, 0x00, 0x49, 0x04, 0x40]), // table.size 0; lt_u; if
      ]),
      Buffer.from([0x20, 0x00]), wasmI32Load(workerReadyBitmapOffset),
      Buffer.from([0x22]), writeWasmUleb(workerReadyBitmapLocal),
      Buffer.from([0x04, 0x40]), // if worker row pointer != 0
      Buffer.from([0x20]), writeWasmUleb(workerReadyBitmapLocal),
      Buffer.from([0x20]), writeWasmUleb(slotLocal), Buffer.from([0x6a]),
      wasmI32Load8U(0), Buffer.from([0x04, 0x40]), // if installed byte != 0
    ] : [
      ...(tablePregrown ? [] : [
        Buffer.from([0x20]), writeWasmUleb(slotLocal),
        Buffer.from([0xfc, 0x10, 0x00, 0x49, 0x04, 0x40]), // table.size 0; lt_u; if
      ]),
      Buffer.from([0x20]), writeWasmUleb(slotLocal),
      ...(callRef ? [
        Buffer.from([0x25, 0x00, 0xfb, 0x17]), writeWasmSleb(blockTypeIndex), // table.get; ref.cast (ref null type)
        Buffer.from([0x22]), writeWasmUleb(typedRefLocal),
        Buffer.from([0xd1, 0x45, 0x04, 0x40]), // ref.is_null; eqz; if
      ] : [
        Buffer.from([0x25, 0x00, 0xd1, 0x45, 0x04, 0x40]), // table.get 0; ref.is_null; eqz; if
      ]),
    ]),

    // Increment only for a taken tail. The outer C++ activation owns the
    // save/restore scope, so exceptions and scheduler returns restore depth.
    Buffer.from([0x20, 0x00]),
    ...(depthSnapshotLocal ?
      [Buffer.from([0x20]), writeWasmUleb(depthLocal)] :
      [Buffer.from([0x20, 0x00]), wasmI32Load(depthOffset)]),
    Buffer.from([0x41, 0x01, 0x6a]), wasmI32Store(depthOffset),

    // Signature is exactly (cpu, relocBase) -> void. The baseline indirect
    // call consumes the dynamic slot after both arguments; typed-ref mode
    // consumes the exact reference saved by the readiness table.get.
    Buffer.from([0x20, 0x00]),
    Buffer.from([0x41]), writeWasmSleb(mapBase | 0),
    Buffer.from([0x20]), writeWasmUleb(slotLocal),
    Buffer.from([0x41, 0x02, 0x74, 0x6a]),
    // pfnJitCode above is the acquire publication. The runtime initializes
    // this owner-wide slot->relocBase entry before publishing pfnJitCode and
    // does not mutate it while published, so the post-acquire payload read
    // can be plain. Keep the atomic form as the default for old runtimes.
    plainRelocMapLoad ? wasmI32Load(0) : wasmI32AtomicLoad(0),
    ...(callRef ? [
      Buffer.from([0x20]), writeWasmUleb(typedRefLocal),
      Buffer.from([0x15]), writeWasmUleb(blockTypeIndex),
    ] : [
      Buffer.from([0x20]), writeWasmUleb(slotLocal),
      Buffer.from([0x13]), writeWasmUleb(blockTypeIndex), Buffer.from([0x00]),
    ]),

    Buffer.from(lastSlotCache ?
      [0x0b, 0x0b, 0x0b, 0x0f] : // fallback block/depth/target; return
      workerReadyBitmapOffset ?
      (tablePregrown ?
        [0x0b, 0x0b, 0x0b, 0x0b, 0x0f] : // target/depth/pointer/byte; return
        [0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0f]) : // plus table bounds
      (tablePregrown ?
        [0x0b, 0x0b, 0x0b, 0x0f] : // target/depth/table-ref; return
        [0x0b, 0x0b, 0x0b, 0x0b, 0x0f])), // plus table bounds
  ]);
}

function genericDispatcherTailCall(dispatcherIndex, needsFetch = false) {
  // The common dispatcher reloads cpu->nextOp. Its second standard i32
  // parameter is therefore available as a fail-closed needsFetch flag without
  // adding a new function type.
  return Buffer.concat([
    Buffer.from([0x20, 0x00, 0x41, needsFetch ? 0x01 : 0x00, 0x12]),
    writeWasmUleb(dispatcherIndex),
  ]);
}

function inlineFetchNextOpPrelude(runtime, targetLocal, fetchNextImportIndex) {
  const nextOpOffset = runtime.cpu.nextOpOffset >>> 0;
  const threadOffset = runtime.cpu.threadOffset >>> 0;
  const terminatingOffset = runtime.cpu.kthreadTerminatingOffset >>> 0;
  const processOffset = runtime.cpu.kthreadProcessOffset >>> 0;
  const eipOffset = runtime.cpu.eipOffset >>> 0;
  const csAddressOffset = runtime.cpu.csAddressOffset >>> 0;
  const bigOffset = runtime.cpu.bigOffset >>> 0;
  const opCacheOffset = runtime.cpu.opCacheOffset >>> 0;
  return Buffer.concat([
    Buffer.from([0x20, 0x01, 0x04, 0x40]), // if (needsFetch)
    Buffer.from([0x02, 0x40, 0x02, 0x40]), // block done; block fallback

    // Default to null. A terminating thread exits without touching the cache;
    // every structural miss falls through to the exact imported helper.
    Buffer.from([0x20, 0x00, 0x41, 0x00]), wasmI32Store(nextOpOffset),
    Buffer.from([0x20, 0x00]), wasmI32Load(threadOffset),
    Buffer.from([0x22]), writeWasmUleb(targetLocal),
    Buffer.from([0x45, 0x0d, 0x00]), // null thread -> fallback
    Buffer.from([0x20]), writeWasmUleb(targetLocal), wasmI32Load8U(terminatingOffset),
    Buffer.from([0x04, 0x40, 0x0c, 0x02, 0x0b]), // terminating -> done
    Buffer.from([0x20]), writeWasmUleb(targetLocal), wasmI32Load(processOffset),
    Buffer.from([0x45, 0x0d, 0x01]), // null process -> done

    // param 1 is dead after the entry predicate; reuse it for the exact
    // linear EIP: (big ? eip : (eip & 0xffff)) + CS.address.
    Buffer.from([0x20, 0x00]), wasmI32Load(eipOffset),
    Buffer.from([0x21, 0x01]),
    Buffer.from([0x20, 0x00]), wasmI32Load(bigOffset),
    Buffer.from([0x45, 0x04, 0x40]),
    Buffer.from([0x20, 0x01, 0x41]), writeWasmSleb(0xffff),
    Buffer.from([0x71, 0x21, 0x01, 0x0b]),
    Buffer.from([0x20, 0x01, 0x20, 0x00]), wasmI32Load(csAddressOffset),
    Buffer.from([0x6a, 0x21, 0x01]),

    // DecodedOpCache 10/10/12 lookup. targetLocal is scratch here and is
    // overwritten by the existing cpu->nextOp local.tee immediately below.
    Buffer.from([0x20, 0x00]), wasmI32Load(opCacheOffset),
    Buffer.from([0x22]), writeWasmUleb(targetLocal),
    Buffer.from([0x45, 0x0d, 0x00]),
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    Buffer.from([0x20, 0x01, 0x41, 0x16, 0x76, 0x41, 0x02, 0x74, 0x6a]),
    wasmI32Load(0),
    Buffer.from([0x22]), writeWasmUleb(targetLocal),
    Buffer.from([0x45, 0x0d, 0x00]),
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    Buffer.from([0x20, 0x01, 0x41, 0x0c, 0x76, 0x41]), writeWasmSleb(0x3ff),
    Buffer.from([0x71, 0x41, 0x02, 0x74, 0x6a]), wasmI32Load(0),
    Buffer.from([0x22]), writeWasmUleb(targetLocal),
    Buffer.from([0x45, 0x0d, 0x00]),
    Buffer.from([0x20]), writeWasmUleb(targetLocal),
    Buffer.from([0x20, 0x01, 0x41]), writeWasmSleb(0xfff),
    Buffer.from([0x71, 0x41, 0x02, 0x74, 0x6a]), wasmI32Load(0),
    Buffer.from([0x22]), writeWasmUleb(targetLocal),
    Buffer.from([0x45, 0x0d, 0x00]),
    Buffer.from([0x20, 0x00, 0x20]), writeWasmUleb(targetLocal),
    wasmI32Store(nextOpOffset),
    Buffer.from([0x0c, 0x01, 0x0b]), // hit -> done; end fallback
    Buffer.from([0x20, 0x00, 0x10]), writeWasmUleb(fetchNextImportIndex),
    Buffer.from([0x0b, 0x0b]), // end done; end needsFetch
  ]);
}

function buildGenericIndirectDispatcherBody(runtime, blockTypeIndex, depthLimit,
    plainRelocMapLoad = false, depthSnapshotLocal = false,
    tablePregrown = false, workerReadyBitmapOffset = 0, callRef = false,
    lastSlotCache = false, lastSlotGlobalIndex = 0) {
  const i32LocalCount = 2 + (depthSnapshotLocal ? 1 : 0) + (workerReadyBitmapOffset ? 1 : 0);
  // A typed reference local uses the multi-byte `(ref null typeidx)` value
  // type. Build this fresh dispatcher declaration directly: the generic
  // split/append helpers intentionally only parse the legacy one-byte value
  // types present in recorded input bodies.
  const locals = callRef ? {
    firstIndex: 2,
    localDecls: Buffer.concat([
      writeWasmUleb(2),
      writeWasmUleb(i32LocalCount), Buffer.from([0x7f]),
      writeWasmUleb(1), Buffer.from([0x63]), writeWasmSleb(blockTypeIndex),
    ]),
  } : appendI32Locals(Buffer.from([0x00]), i32LocalCount);
  const workerReadyBitmapLocal = locals.firstIndex + 2 + (depthSnapshotLocal ? 1 : 0);
  const typedRefLocal = locals.firstIndex + i32LocalCount;
  return buildCodeBody(locals.localDecls, Buffer.concat([
    ...(runtime.cpu.inlineFetch ? [inlineFetchNextOpPrelude(runtime,
      locals.firstIndex, runtime.cpu.inlineFetch.fetchNextImportIndex)] : []),
    genericIndirectTail(runtime, blockTypeIndex, locals.firstIndex,
      locals.firstIndex + 1, locals.firstIndex + 2, depthLimit,
      plainRelocMapLoad, depthSnapshotLocal, tablePregrown,
      workerReadyBitmapOffset, workerReadyBitmapLocal, callRef, typedRefLocal,
      lastSlotCache, lastSlotGlobalIndex),
    Buffer.from([0x0b]), // function end
  ]));
}

function rewriteGenericIndirectTailsInBody(body, options) {
  const split = splitCodeBody(body);
  const instructions = split.instructions;
  const out = [];
  let pos = 0;
  let fallbackRewritten = 0;
  let fastSuccessRewritten = 0;
  let fastSuccessThreeLoadRewritten = 0;
  let fastSuccessJumpRewritten = 0;
  while (pos < instructions.length) {
    // The persistence fast-success arms have already refreshed cpu->nextOp and
    // normally return without calling fetchNextOp. Match only the exact nested
    // reloc shapes and require the runtime's precise CPU::nextOp offset.
    const fastThreeLoad =
      matchNext2FastSuccessArm(instructions, pos, null, false,
        options.runtime.cpu.nextOpOffset) ||
      matchNext1FastSuccessArm(instructions, pos, null, false,
        options.runtime.cpu.nextOpOffset);
    const fast = fastThreeLoad ||
      matchJumpFastSuccessArm(instructions, pos, null, false,
        options.runtime.cpu.nextOpOffset);
    if (fast) {
      out.push(Buffer.from(instructions.subarray(pos, fast.returnPos)));
      out.push(genericDispatcherTailCall(options.dispatcherIndex, false));
      pos = fast.returnPos + 1;
      fastSuccessRewritten++;
      if (fastThreeLoad) fastSuccessThreeLoadRewritten++;
      else fastSuccessJumpRewritten++;
      continue;
    }

    const cpu = readIndexedInstruction(instructions, pos, 0x20);
    const call = cpu && cpu.index === 0
      ? readIndexedInstruction(instructions, cpu.end, 0x10) : null;
    if (call && call.index === options.fetchNextImportIndex &&
        instructions[call.end] === 0x0f) {
      if (options.inlineFetch) {
        // Move the helper into the common dispatcher: cache hits stay inside
        // grouped Wasm, while structural misses still call this exact import.
        out.push(genericDispatcherTailCall(options.dispatcherIndex, true));
      } else {
        // Baseline remains byte-for-byte compatible when the flag is absent.
        out.push(Buffer.from(instructions.subarray(pos, call.end)));
        out.push(genericDispatcherTailCall(options.dispatcherIndex, false));
      }
      pos = call.end + 1;
      fallbackRewritten++;
      continue;
    }
    out.push(Buffer.from([instructions[pos++]]));
  }
  const rewritten = fallbackRewritten + fastSuccessRewritten;
  if (!rewritten) return {
    body, rewritten: 0, fallbackRewritten: 0, fastSuccessRewritten: 0,
    fastSuccessThreeLoadRewritten: 0, fastSuccessJumpRewritten: 0,
  };
  return {
    body: buildCodeBody(split.localDecls, Buffer.concat(out)),
    rewritten,
    fallbackRewritten,
    fastSuccessRewritten,
    fastSuccessThreeLoadRewritten,
    fastSuccessJumpRewritten,
  };
}

function rewriteDirectCallsInBody(body, targetIndexByEip, options = {}) {
  if (!targetIndexByEip.size) return { body, rewritten: 0 };
  const { localDecls, instructions } = splitCodeBody(body);
  const out = [];
  let pos = 0;
  let rewritten = 0;
  let guarded = 0;
  let hotNext2Rewritten = 0;
  let hotNext1Rewritten = 0;
  let hotNext1TeeBaseRewritten = 0;
  let hotNext1DirectBaseRewritten = 0;
  let hotJumpRewritten = 0;
  let hotJumpTeeBaseRewritten = 0;
  let hotJumpDirectBaseRewritten = 0;
  let fallbackRewritten = 0;
  let threadedStoreTailsSunk = 0;
  let threadedStoreBytesSunk = 0;
  let threadedStorePatternMisses = 0;
  let threadedTailZeroRelocFallbacksInjected = 0;
  let internalCloneDirectCallsRewritten = 0;
  const internalCloneRewrittenByKey = new Map();
  const recordInternalCloneRewrite = (key) => {
    internalCloneDirectCallsRewritten++;
    internalCloneRewrittenByKey.set(
      key, (internalCloneRewrittenByKey.get(key) || 0) + 1);
  };
  while (pos < instructions.length) {
    if (options.hotFastPathOnly) {
      const hot = matchNext2FastSuccessArm(instructions, pos, targetIndexByEip,
        Boolean(options.boundedCyclicFastPaths));
      if (hot) {
        // Keep the original cpu->nextOp store, then either enter the verified
        // target directly or execute the original return to the dispatcher.
        out.push(Buffer.from(instructions.subarray(pos, hot.returnPos)));
        if (hot.target.internalCloneOnly) {
          out.push(internalCloneFastTail(
            hot.target.index, hot.targetLocal, options.runtime));
          recordInternalCloneRewrite(hot.target.key);
        } else {
          const relocIdentityArg = options.allocRelocArg(hot.target.key);
          const relocPublicationArg = options.allocRelocArg(hot.target.key);
          const relocCallArg = options.allocRelocArg(hot.target.key);
          out.push(guardedFastDirectTail(
            hot.target.index, hot.targetLocal, hot.target.relocCount, options.runtime,
            relocIdentityArg, relocPublicationArg, relocCallArg,
            Boolean(hot.target.guarded), options.directChainLimit || 0));
        }
        pos = hot.returnPos + 1;
        rewritten++;
        if (hot.target.guarded) guarded++;
        hotNext2Rewritten++;
        continue;
      }
    }
    if (options.hotNext1) {
      const hot = matchNext1FastSuccessArm(instructions, pos, targetIndexByEip,
        Boolean(options.boundedCyclicFastPaths));
      if (hot) {
        out.push(Buffer.from(instructions.subarray(pos, hot.returnPos)));
        if (hot.target.internalCloneOnly) {
          out.push(internalCloneFastTail(
            hot.target.index, hot.targetLocal, options.runtime));
          recordInternalCloneRewrite(hot.target.key);
        } else {
          const relocIdentityArg = options.allocRelocArg(hot.target.key);
          const relocPublicationArg = options.allocRelocArg(hot.target.key);
          const relocCallArg = options.allocRelocArg(hot.target.key);
          out.push(guardedFastDirectTail(
            hot.target.index, hot.targetLocal, hot.target.relocCount, options.runtime,
            relocIdentityArg, relocPublicationArg, relocCallArg,
            Boolean(hot.target.guarded), options.directChainLimit || 0));
        }
        pos = hot.returnPos + 1;
        rewritten++;
        if (hot.target.guarded) guarded++;
        hotNext1Rewritten++;
        if (hot.shape === 'tee-base') hotNext1TeeBaseRewritten++;
        else hotNext1DirectBaseRewritten++;
        continue;
      }
    }
    if (options.hotJump) {
      const hot = matchJumpFastSuccessArm(instructions, pos, targetIndexByEip,
        Boolean(options.boundedCyclicFastPaths));
      if (hot) {
        out.push(Buffer.from(instructions.subarray(pos, hot.returnPos)));
        const relocIdentityArg = options.allocRelocArg(hot.target.key);
        const relocPublicationArg = options.allocRelocArg(hot.target.key);
        const relocCallArg = options.allocRelocArg(hot.target.key);
        out.push(guardedFastDirectTail(
          hot.target.index, hot.targetLocal, hot.target.relocCount, options.runtime,
          relocIdentityArg, relocPublicationArg, relocCallArg,
          Boolean(hot.target.guarded), options.directChainLimit || 0));
        pos = hot.returnPos + 1;
        rewritten++;
        if (hot.target.guarded) guarded++;
        hotJumpRewritten++;
        if (hot.shape === 'tee-base') hotJumpTeeBaseRewritten++;
        else hotJumpDirectBaseRewritten++;
        continue;
      }
    }
    const threadedFetchReturnLength = options.threadedGpAbi
      ? matchThreadedFetchReturn(instructions, pos)
      : 0;
    const legacyFetchReturn = !options.threadedGpAbi &&
      instructions[pos] === 0x20 && instructions[pos + 1] === 0x00 &&
      instructions[pos + 2] === 0x10 && instructions[pos + 3] === 0x06 &&
      instructions[pos + 4] === 0x0f;
    if (!options.hotFastPathOnly &&
        (threadedFetchReturnLength !== 0 || legacyFetchReturn)) {
      const eipStore = findStaticEipStoreBefore(instructions, pos);
      const target = eipStore ? targetIndexByEip.get(eipStore.eip) : undefined;
      if (target !== undefined) {
        const threadedStoreSuffix = options.threadedGpAbi
          ? matchThreadedGpStoreSuffix(
              instructions, pos, options.threadedGpFirstOffset)
          : null;
        if (options.threadedGpAbi && !threadedStoreSuffix) {
          // A partial or non-canonical writeback cannot be deleted safely.
          // Preserve the original helper exit byte-for-byte.
          threadedStorePatternMisses++;
          out.push(Buffer.from([instructions[pos++]]));
          continue;
        }
        // Every direct call passes the target's reloc slot array as the
        // second argument — a marker placeholder here, patched by the
        // loader once the arrays are allocated.
        if (options.threadedGpAbi) {
          if (target.yieldAwareTailCall || target.guarded) {
            throw new Error('Threaded direct tails currently require an unguarded exact edge');
          }
          trimOutputTail(out, threadedStoreSuffix.length);
          threadedStoreTailsSunk++;
          threadedStoreBytesSunk += threadedStoreSuffix.length;
          out.push(guardedThreadedReturnCallTail(
            target.index, target.relocCount, options.runtime,
            Buffer.from(instructions.subarray(
              threadedStoreSuffix.start,
              pos + threadedFetchReturnLength)),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.allocRelocArg(target.key),
            options.threadedTailCensus));
        } else if (target.yieldAwareTailCall) {
          const relocArg = options.allocRelocArg(target.key);
          out.push(yieldAwareReturnCallTail(target.index, options.opCount, options.runtime, relocArg));
          guarded++;
        } else if (target.tailCall) {
          const relocArg = options.allocRelocArg(target.key);
          out.push(returnCallTail(target.index, relocArg));
        } else if (target.guarded) {
          const relocArg = options.allocRelocArg(target.key);
          out.push(guardedDirectCallTail(target.index, options.directCallDepth, relocArg));
          guarded++;
        } else {
          const relocArg = options.allocRelocArg(target.key);
          out.push(directCallTail(target.index, relocArg));
        }
        pos += threadedFetchReturnLength || 5;
        rewritten++;
        fallbackRewritten++;
        continue;
      }
      if (options.threadedGpAbi && options.threadedTailCensus && eipStore &&
          options.zeroRelocTargets?.has(eipStore.eip >>> 0)) {
        const suffix = matchThreadedGpStoreSuffix(
          instructions, pos, options.threadedGpFirstOffset);
        if (suffix) {
          out.push(threadedTailCensusCall(
            options.threadedTailCensus.zeroRelocFallback));
          threadedTailZeroRelocFallbacksInjected++;
        }
      }
    }
    out.push(Buffer.from([instructions[pos++]]));
  }
  return {
    body: buildCodeBody(localDecls, Buffer.concat(out)),
    rewritten,
    guarded,
    hotNext2Rewritten,
    hotNext1Rewritten,
    hotNext1TeeBaseRewritten,
    hotNext1DirectBaseRewritten,
    hotJumpRewritten,
    hotJumpTeeBaseRewritten,
    hotJumpDirectBaseRewritten,
    fallbackRewritten,
    threadedStoreTailsSunk,
    threadedStoreBytesSunk,
    threadedStorePatternMisses,
    threadedTailZeroRelocFallbacksInjected,
    internalCloneDirectCallsRewritten,
    internalCloneRewrittenByKey: Object.fromEntries(internalCloneRewrittenByKey),
  };
}

function parseWasmForMerge(wasm) {
  if (!isWasm(wasm)) throw new Error('Entry is not a wasm module');
  const parsed = {
    typePayload: null,
    importPayload: null,
    importedFunctions: 0,
    imports: [],
    types: [],
    functionTypes: [],
    codeBodies: [],
  };
  let pos = 8;
  while (pos < wasm.length) {
    const id = wasm[pos++];
    const [size, p] = readWasmUleb(wasm, pos);
    pos = p;
    const sectionStart = pos;
    const sectionEnd = pos + size;
    if (sectionEnd > wasm.length) throw new Error('Wasm section extends outside module');
    if (id === 1) {
      parsed.typePayload = Buffer.from(wasm.subarray(sectionStart, sectionEnd));
      let q = sectionStart;
      const [count, q2] = readWasmUleb(wasm, q);
      q = q2;
      for (let i = 0; i < count; i++) {
        if (wasm[q++] !== 0x60) throw new Error('Expected a function type');
        let paramCount; [paramCount, q] = readWasmUleb(wasm, q);
        const params = [];
        for (let j = 0; j < paramCount; j++) params.push(wasm[q++]);
        let resultCount; [resultCount, q] = readWasmUleb(wasm, q);
        const results = [];
        for (let j = 0; j < resultCount; j++) results.push(wasm[q++]);
        parsed.types.push({ params, results });
      }
      if (q !== sectionEnd) throw new Error('Function type section has trailing bytes');
    } else if (id === 2) {
      parsed.importPayload = Buffer.from(wasm.subarray(sectionStart, sectionEnd));
      let q = sectionStart;
      const [count, q2] = readWasmUleb(wasm, q);
      q = q2;
      for (let i = 0; i < count; i++) {
        let moduleName; [moduleName, q] = wasmName(wasm, q);
        let fieldName; [fieldName, q] = wasmName(wasm, q);
        const kind = wasm[q++];
        const imp = { module: moduleName, field: fieldName, kind };
        if (kind === 0) {
          [imp.typeIndex, q] = readWasmUleb(wasm, q);
          imp.functionIndex = parsed.importedFunctions;
          parsed.importedFunctions++;
        } else if (kind === 1) {
          imp.refType = wasm[q++];
          let flags; [flags, q] = readWasmUleb(wasm, q); imp.flags = flags;
          [, q] = readWasmUleb(wasm, q);
          if (flags & 1) [, q] = readWasmUleb(wasm, q);
        } else if (kind === 2) {
          const flags = wasm[q++];
          [, q] = readWasmUleb(wasm, q);
          if (flags & 1) [, q] = readWasmUleb(wasm, q);
        } else if (kind === 3) {
          q += 2;
        } else {
          throw new Error(`Unsupported import kind ${kind}`);
        }
        parsed.imports.push(imp);
      }
    } else if (id === 3) {
      let q = sectionStart;
      const [count, q2] = readWasmUleb(wasm, q);
      q = q2;
      for (let i = 0; i < count; i++) {
        let typeIndex; [typeIndex, q] = readWasmUleb(wasm, q);
        parsed.functionTypes.push(typeIndex);
      }
    } else if (id === 10) {
      let q = sectionStart;
      const [count, q2] = readWasmUleb(wasm, q);
      q = q2;
      for (let i = 0; i < count; i++) {
        const bodyStart = q;
        const [bodySize, bodyPayloadStart] = readWasmUleb(wasm, q);
        q = bodyPayloadStart + bodySize;
        parsed.codeBodies.push(Buffer.from(wasm.subarray(bodyStart, q)));
      }
    }
    pos = sectionEnd;
  }
  if (!parsed.typePayload || !parsed.importPayload ||
      parsed.functionTypes.length !== parsed.codeBodies.length ||
      (parsed.functionTypes.length !== 1 && parsed.functionTypes.length !== 2)) {
    throw new Error('Expected one legacy function or one dual-entry function pair');
  }
  const isI32Vector = (values, count) =>
    values.length === count && values.every((value) => value === 0x7f);
  const wrapperType = parsed.types[parsed.functionTypes[0]];
  if (!wrapperType || !isI32Vector(wrapperType.params, 2) || wrapperType.results.length !== 0) {
    throw new Error('Wrapper function must have signature (i32,i32)->()');
  }
  if (parsed.functionTypes.length === 2) {
    const threadedType = parsed.types[parsed.functionTypes[1]];
    if (!threadedType ||
        !isI32Vector(threadedType.params, 10) ||
        !isI32Vector(threadedType.results, 8)) {
      throw new Error('Threaded function must have signature (10xi32)->(8xi32)');
    }
  }
  return parsed;
}

function buildMergedGroupWasm(sortedEntries, options = {}) {
  if (!sortedEntries.length) throw new Error('Cannot merge an empty group');
  const parsedEntries = sortedEntries.map((entry) => parseWasmForMerge(entry.data));
  const first = parsedEntries[0];
  const functionsPerEntry = first.functionTypes.length;
  const threadedGpAbi = functionsPerEntry === 2;
  const internalClones = options.internalClones || [];
  const originalRegionRoutes = options.originalRegionRoutes || [];
  if (internalClones.length && threadedGpAbi) {
    throw new Error('Internal clone-only targets currently require legacy single-entry modules');
  }
  const cloneKeySet = new Set();
  const entryKeySet = new Set(sortedEntries.map((entry) => entry.key));
  for (const clone of internalClones) {
    if (!clone?.cloneKey || !clone?.sourceEntry ||
        cloneKeySet.has(clone.cloneKey) || entryKeySet.has(clone.cloneKey)) {
      throw new Error(`Internal clone key collision: ${clone?.cloneKey || '<missing>'}`);
    }
    cloneKeySet.add(clone.cloneKey);
  }
  if (threadedGpAbi &&
      (!Number.isInteger(options.runtime?.cpu?.directChainDepthOffset) ||
       options.runtime.cpu.directChainDepthOffset <= 0 ||
       !Number.isInteger(options.runtime?.cpu?.bailoutOffset) ||
       options.runtime.cpu.bailoutOffset <= 0 ||
       !Number.isInteger(options.runtime?.cpu?.yieldOffset) ||
       options.runtime.cpu.yieldOffset <= 0 ||
       !Number.isInteger(options.runtime?.cpu?.nextOpOffset) ||
       options.runtime.cpu.nextOpOffset <= 0 ||
       (options.runtime.cpu.nextOpOffset & 3) !== 0 ||
       !Number.isInteger(options.runtime?.decodedOp?.pfnJitCodeOffset) ||
       options.runtime.decodedOp.pfnJitCodeOffset <= 0)) {
    throw new Error(
      'Threaded GP tails require exact aligned nextOp, directChainDepth, bailout, yield, and pfnJitCode offsets');
  }
  const genericIndirectEnabled = options.mtGenericIndirectLimit > 0;
  if (threadedGpAbi && genericIndirectEnabled) {
    throw new Error(
      'Dual-entry modules require the exact typed common-dispatch rewriter');
  }
  if (originalRegionRoutes.length &&
      (threadedGpAbi || !genericIndirectEnabled || internalClones.length ||
       (options.originalRegionMode !== 'control' &&
        options.originalRegionMode !== 'candidate' &&
        options.originalRegionMode !== 'selective'))) {
    throw new Error(
      'Original-region routers require legacy original entries, generic indirect tails, no clones, and an exact mode');
  }
  if (options.threadedTailCensus) {
    if (!threadedGpAbi) {
      throw new Error('Threaded-tail census requires paired threaded-body modules');
    }
    for (const helperIndex of Object.values(options.threadedTailCensus)) {
      const helper = first.imports.find((imp) => imp.kind === 0 &&
        imp.module === 'helpers' && imp.field === `fn_${helperIndex}` &&
        imp.functionIndex === helperIndex);
      if (!helper) {
        throw new Error(`Threaded-tail census helper import fn_${helperIndex} missing`);
      }
    }
  }
  const fetchNextImport = first.imports.find((imp) =>
    imp.kind === 0 && imp.module === 'helpers' && imp.field === 'fn_6');
  if (genericIndirectEnabled && !fetchNextImport) {
    throw new Error('Generic indirect tails require the exact helpers.fn_6 fetchNextOp import');
  }
  if (genericIndirectEnabled && first.imports.some((imp) => imp.kind === 1)) {
    throw new Error('Generic indirect tails require raw modules with no existing table import');
  }
  const sortedKeys = new Set(sortedEntries.map((entry) => entry.key));
  const entryByKey = new Map(sortedEntries.map((entry) => [entry.key, entry]));
  const functionIndexByKey = new Map(sortedEntries.map((entry, i) => [
    entry.key,
    first.importedFunctions + i * functionsPerEntry +
      (threadedGpAbi ? 1 : 0),
  ]));
  const internalCloneBySourceKey = new Map();
  for (let i = 0; i < internalClones.length; i++) {
    const clone = internalClones[i];
    if (internalCloneBySourceKey.has(clone.sourceEntry.key)) {
      throw new Error(`Duplicate internal clone source in one group: ${clone.sourceEntry.key}`);
    }
    internalCloneBySourceKey.set(clone.sourceEntry.key, {
      ...clone,
      index: first.importedFunctions + sortedEntries.length * functionsPerEntry + i,
    });
  }
  const genericDispatcherIndex =
    first.importedFunctions + sortedEntries.length * functionsPerEntry + internalClones.length;
  const originalRegionRouterBase = genericDispatcherIndex +
    (genericIndirectEnabled ? 1 : 0);
  const originalRegionRouterBySource = new Map();
  for (let i = 0; i < originalRegionRoutes.length; i++) {
    const route = originalRegionRoutes[i];
    if (!entryByKey.has(route.sourceKey) || !entryByKey.has(route.targetKey) ||
        originalRegionRouterBySource.has(route.sourceKey)) {
      throw new Error(
        `Original-region route is not group-local/unique: ` +
        `${route.sourceKey} -> ${route.targetKey}`);
    }
    originalRegionRouterBySource.set(route.sourceKey, {
      ...route,
      functionIndex: originalRegionRouterBase + i,
    });
  }
  const directTargetsByKey = new Map();
  const zeroRelocTargetsByKey = new Map();
  if (options.directCalls) {
    for (const edge of options.edges || []) {
      if (!edge.resolved || !sortedKeys.has(edge.from) ||
          (!sortedKeys.has(edge.to) && !edge.internalCloneOnly)) continue;
      const clone = edge.internalCloneOnly
        ? internalCloneBySourceKey.get(edge.to) : null;
      if (edge.internalCloneOnly && !clone) {
        throw new Error(`Missing internal clone for edge ${edge.from} -> ${edge.to}`);
      }
      const targetEntry = clone?.sourceEntry || entryByKey.get(edge.to);
      // Typed store-sunk tails use targetRelocBase[0] both as the exact
      // published DecodedOp* guard and as the CPU::nextOp value. A zero-slot
      // target has only the pair publication tokens, so admit neither a
      // direct edge nor a marker; retain its original fetchNextOp fallback.
      if (threadedGpAbi && (targetEntry?.relocCount || 0) < 1) {
        if (options.threadedTailCensus) {
          let targets = zeroRelocTargetsByKey.get(edge.from);
          if (!targets) {
            targets = new Set();
            zeroRelocTargetsByKey.set(edge.from, targets);
          }
          targets.add(edge.targetEip >>> 0);
        }
        continue;
      }
      let targets = directTargetsByKey.get(edge.from);
      if (!targets) {
        targets = new Map();
        directTargetsByKey.set(edge.from, targets);
      }
      const targetEip = edge.targetEip >>> 0;
      const existing = targets.get(targetEip);
      if (existing) {
        if (existing.internalCloneOnly !== Boolean(clone)) {
          throw new Error(`Mixed original/clone direct target at ${hex32(targetEip)}`);
        }
        existing.kinds.add(edge.kind);
        existing.guarded ||= Boolean(edge.guarded);
        existing.yieldAwareTailCall ||= Boolean(options.yieldAwareTailCalls && edge.guarded);
      } else {
        targets.set(targetEip, {
          index: clone ? clone.index : functionIndexByKey.get(edge.to),
          key: edge.to,
          tailCall: Boolean(options.tailCalls),
          yieldAwareTailCall: Boolean(options.yieldAwareTailCalls && edge.guarded),
          guarded: Boolean(edge.guarded),
          kinds: new Set([edge.kind]),
          relocCount: targetEntry?.relocCount || 0,
          internalCloneOnly: Boolean(clone),
        });
      }
    }
  }
  const functionTypes = [];
  const codeBodies = [];
  let directCallsRewritten = 0;
  let guardedDirectCallsRewritten = 0;
  let hotNext2DirectCallsRewritten = 0;
  let hotNext1DirectCallsRewritten = 0;
  let hotNext1TeeBaseDirectCallsRewritten = 0;
  let hotNext1DirectBaseDirectCallsRewritten = 0;
  let hotJumpDirectCallsRewritten = 0;
  let hotJumpTeeBaseDirectCallsRewritten = 0;
  let hotJumpDirectBaseDirectCallsRewritten = 0;
  let fallbackDirectCallsRewritten = 0;
  let threadedStoreTailsSunk = 0;
  let threadedStoreBytesSunk = 0;
  let threadedStorePatternMisses = 0;
  let genericIndirectTailsRewritten = 0;
  let genericIndirectFallbackTailsRewritten = 0;
  let genericIndirectFastSuccessTailsRewritten = 0;
  let genericIndirectFastSuccessThreeLoadTailsRewritten = 0;
  let genericIndirectFastSuccessJumpTailsRewritten = 0;
  let threadedTailZeroRelocFallbacksInjected = 0;
  let internalCloneDirectCallsRewritten = 0;
  let internalCloneGenericIndirectTailsRewritten = 0;
  let originalRegionDispatcherSites = 0;
  let originalRegionStaticTargetSites = 0;
  let originalRegionDynamicNormalTargetSites = 0;
  let originalRegionNormalTargetSites = 0;
  let originalRegionRedirectedSites = 0;
  let originalRegionRetainedDispatcherSites = 0;
  let originalRegionRouterResidualFallbackSites = 0;
  const originalRegionRouteStats = [];
  const internalCloneRewrittenByKey = new Map();
  // Per-site reloc-argument markers: { marker, targetKey } recorded by the
  // direct-call rewriter, located after assembly so the loader can patch
  // each site with its target's array address.
  const relocSiteMarkers = [];
  const allocRelocArg = (targetKey, metadata = {}) => {
    const marker = 0x1bace000 + relocSiteMarkers.length;
    relocSiteMarkers.push({ marker, targetKey, ...metadata });
    return relocArgPlaceholder(marker);
  };
  for (let entryIndex = 0; entryIndex < sortedEntries.length; entryIndex++) {
    const entry = sortedEntries[entryIndex];
    const parsed = parsedEntries[entryIndex];
    if (!parsed.typePayload.equals(first.typePayload) || !parsed.importPayload.equals(first.importPayload)) {
      throw new Error(`Module ABI mismatch while merging ${entry.key}`);
    }
    if (parsed.functionTypes.length !== functionsPerEntry ||
        parsed.functionTypes.some((type, i) => type !== first.functionTypes[i])) {
      throw new Error(`Module local-function shape mismatch while merging ${entry.key}`);
    }
    functionTypes.push(...parsed.functionTypes);
    // Legacy modules rewrite their only body. Paired modules retain the cold
    // wrapper as body 0 and reserve body 1 for the exact threaded rewriter.
    let body = parsed.codeBodies[threadedGpAbi ? 1 : 0];
    const threadedGpFirstOffset = threadedGpAbi
      ? findThreadedWrapperGpLoadBase(
          parsed.codeBodies[0], first.importedFunctions + 1)
      : undefined;
    const directTargets = directTargetsByKey.get(entry.key);
    const zeroRelocTargets = zeroRelocTargetsByKey.get(entry.key);
    if (directTargets || zeroRelocTargets) {
      if (threadedGpAbi &&
          [...(directTargets || new Map()).values()].some((target) =>
            target.guarded || target.yieldAwareTailCall)) {
        throw new Error(
          `Threaded entry ${entry.key} contains a guarded direct edge; typed guard support is required`);
      }
      const rewritten = rewriteDirectCallsInBody(body, directTargets || new Map(), {
        ...options,
        threadedGpAbi,
        threadedGpFirstOffset,
        ...(threadedGpAbi ? {
          hotFastPathOnly: false,
          hotNext1: false,
          hotNext2: false,
          hotJump: false,
          directCallDepth: 0,
        } : {}),
        opCount: entry.opCount,
        allocRelocArg,
        threadedTailCensus: options.threadedTailCensus,
        zeroRelocTargets,
      });
      body = rewritten.body;
      directCallsRewritten += rewritten.rewritten;
      guardedDirectCallsRewritten += rewritten.guarded;
      hotNext2DirectCallsRewritten += rewritten.hotNext2Rewritten;
      hotNext1DirectCallsRewritten += rewritten.hotNext1Rewritten;
      hotNext1TeeBaseDirectCallsRewritten += rewritten.hotNext1TeeBaseRewritten;
      hotNext1DirectBaseDirectCallsRewritten += rewritten.hotNext1DirectBaseRewritten;
      hotJumpDirectCallsRewritten += rewritten.hotJumpRewritten;
      hotJumpTeeBaseDirectCallsRewritten += rewritten.hotJumpTeeBaseRewritten;
      hotJumpDirectBaseDirectCallsRewritten += rewritten.hotJumpDirectBaseRewritten;
      fallbackDirectCallsRewritten += rewritten.fallbackRewritten;
      threadedStoreTailsSunk += rewritten.threadedStoreTailsSunk;
      threadedStoreBytesSunk += rewritten.threadedStoreBytesSunk;
      threadedStorePatternMisses += rewritten.threadedStorePatternMisses;
      threadedTailZeroRelocFallbacksInjected += rewritten.threadedTailZeroRelocFallbacksInjected;
      internalCloneDirectCallsRewritten += rewritten.internalCloneDirectCallsRewritten || 0;
      for (const [key, count] of Object.entries(
        rewritten.internalCloneRewrittenByKey || {})) {
        internalCloneRewrittenByKey.set(
          key, (internalCloneRewrittenByKey.get(key) || 0) + count);
      }
    }
    if (genericIndirectEnabled) {
      if (options.mtGenericInlineFetch) {
        options.runtime.cpu.inlineFetch = {
          fetchNextImportIndex: fetchNextImport.functionIndex,
        };
      }
      const rewritten = rewriteGenericIndirectTailsInBody(body, {
        runtime: options.runtime,
        mtGenericIndirectLimit: options.mtGenericIndirectLimit,
        fetchNextImportIndex: fetchNextImport.functionIndex,
        blockTypeIndex: parsed.functionTypes[0],
        dispatcherIndex: genericDispatcherIndex,
        inlineFetch: options.mtGenericInlineFetch,
      });
      body = rewritten.body;
      genericIndirectTailsRewritten += rewritten.rewritten;
      genericIndirectFallbackTailsRewritten += rewritten.fallbackRewritten;
      genericIndirectFastSuccessTailsRewritten += rewritten.fastSuccessRewritten;
      genericIndirectFastSuccessThreeLoadTailsRewritten += rewritten.fastSuccessThreeLoadRewritten;
      genericIndirectFastSuccessJumpTailsRewritten += rewritten.fastSuccessJumpRewritten;
    }
    const originalRegionRoute = originalRegionRouterBySource.get(entry.key);
    if (originalRegionRoute) {
      const target = entryByKey.get(originalRegionRoute.targetKey);
      const analyzed = rewriteOriginalRegionDispatcherCalls(
        body, genericDispatcherIndex, originalRegionRoute.functionIndex,
        target.eip >>> 0, options.originalRegionMode, originalRegionRoute);
      if (analyzed.dispatcherSites !== originalRegionRoute.dispatcherSites ||
          analyzed.observedStaticEipSites !==
            originalRegionRoute.staticTargetSites) {
        throw new Error(
          `Original-region source-site mismatch for ${entry.key}: ` +
          `dispatcher=${analyzed.dispatcherSites}/${originalRegionRoute.dispatcherSites} ` +
          `static=${analyzed.observedStaticEipSites}/` +
          `${originalRegionRoute.staticTargetSites}`);
      }
      body = analyzed.body;
      originalRegionDispatcherSites += analyzed.dispatcherSites;
      // The 13 exact physical target sites include dynamic popped-EIP/GOT
      // shapes that cannot all be proved by the narrow static-store matcher.
      // Keep the independently disassembled per-source census declarative;
      // every emitted router still recomputes and checks the actual live
      // linear EIP before its local return_call.
      originalRegionStaticTargetSites += originalRegionRoute.staticTargetSites;
      originalRegionDynamicNormalTargetSites +=
        originalRegionRoute.dynamicNormalTargetSites;
      originalRegionNormalTargetSites +=
        originalRegionRoute.staticTargetSites +
        originalRegionRoute.dynamicNormalTargetSites;
      const normalSites = originalRegionRoute.staticTargetSites +
        originalRegionRoute.dynamicNormalTargetSites;
      const expectedRedirectedSites = options.originalRegionMode === 'candidate' ?
        originalRegionRoute.dispatcherSites :
        options.originalRegionMode === 'selective' ?
          (originalRegionRoute.dynamicNormalTargetSites > 0 ?
            originalRegionRoute.dispatcherSites :
            originalRegionRoute.staticTargetSites) : 0;
      if (analyzed.redirectedSites !== expectedRedirectedSites) {
        throw new Error(
          `Original-region redirect mismatch for ${entry.key}: ` +
          `${analyzed.redirectedSites}/${expectedRedirectedSites}`);
      }
      originalRegionRedirectedSites += analyzed.redirectedSites;
      originalRegionRetainedDispatcherSites += analyzed.retainedDispatcherSites;
      if (options.originalRegionMode === 'selective') {
        originalRegionRouterResidualFallbackSites +=
          analyzed.redirectedSites - normalSites;
      }
      originalRegionRouteStats.push({
        sourceKey: originalRegionRoute.sourceKey,
        targetKey: originalRegionRoute.targetKey,
        functionIndex: originalRegionRoute.functionIndex,
        dispatcherSites: analyzed.dispatcherSites,
        staticTargetSites: originalRegionRoute.staticTargetSites,
        dynamicNormalTargetSites: originalRegionRoute.dynamicNormalTargetSites,
        normalTargetSites: originalRegionRoute.staticTargetSites +
          originalRegionRoute.dynamicNormalTargetSites,
        observedStaticEipSites: analyzed.observedStaticEipSites,
        residualFallbackSites:
          options.originalRegionMode === 'selective' ?
            analyzed.redirectedSites - normalSites :
            analyzed.dispatcherSites - normalSites,
        ...(options.originalRegionMode === 'selective' ? {
          redirectedSites: analyzed.redirectedSites,
          retainedDispatcherSites: analyzed.retainedDispatcherSites,
        } : {}),
      });
    }
    if (threadedGpAbi) {
      const originalInternalIndex = first.importedFunctions + 1;
      const mergedInternalIndex =
        first.importedFunctions + entryIndex * functionsPerEntry + 1;
      const wrapper = rewriteSingleCallIndexInBody(
        parsed.codeBodies[0], originalInternalIndex, mergedInternalIndex);
      codeBodies.push(wrapper, body);
    } else {
      codeBodies.push(body);
    }
  }
  const internalCloneFunctions = [];
  for (const clone of internalCloneBySourceKey.values()) {
    const parsed = parseWasmForMerge(clone.sourceEntry.data);
    if (!parsed.typePayload.equals(first.typePayload) ||
        !parsed.importPayload.equals(first.importPayload) ||
        parsed.functionTypes.length !== 1 || parsed.functionTypes[0] !== first.functionTypes[0]) {
      throw new Error(`Internal clone ABI mismatch for ${clone.sourceEntry.key}`);
    }
    let body = parsed.codeBodies[0];
    let genericRewritten = 0;
    if (genericIndirectEnabled) {
      const rewritten = rewriteGenericIndirectTailsInBody(body, {
        runtime: options.runtime,
        mtGenericIndirectLimit: options.mtGenericIndirectLimit,
        fetchNextImportIndex: fetchNextImport.functionIndex,
        blockTypeIndex: parsed.functionTypes[0],
        dispatcherIndex: genericDispatcherIndex,
        inlineFetch: options.mtGenericInlineFetch,
      });
      body = rewritten.body;
      genericRewritten = rewritten.rewritten;
      internalCloneGenericIndirectTailsRewritten += genericRewritten;
    }
    functionTypes.push(parsed.functionTypes[0]);
    codeBodies.push(body);
    internalCloneFunctions.push({
      cloneKey: clone.cloneKey,
      sourceKey: clone.sourceEntry.key,
      functionIndex: clone.index,
      incomingEdges: clone.incomingEdges || 0,
      directCallsRewritten: internalCloneRewrittenByKey.get(clone.sourceEntry.key) || 0,
      genericIndirectTailsRewritten: genericRewritten,
    });
  }
  if (genericIndirectEnabled) {
    functionTypes.push(first.functionTypes[0]);
    codeBodies.push(buildGenericIndirectDispatcherBody(options.runtime,
      first.functionTypes[0], options.mtGenericIndirectLimit,
      options.mtIndirectRelocMapPlainLoad, options.mtGenericDepthLocal,
      options.mtGenericTablePregrown,
      options.mtGenericWorkerReadyBitmapOffset,
      options.mtGenericCallRef,
      options.mtGenericLastSlotCache,
      options.directCallDepth > 0 ? 1 : 0));
  }
  const originalRegionRouters = [];
  for (const route of originalRegionRoutes) {
    const router = originalRegionRouterBySource.get(route.sourceKey);
    const target = entryByKey.get(route.targetKey);
    const targetRelocArg = allocRelocArg(route.targetKey, {
      kind: 'original-region-target-reloc-base-v1',
      tokenOffsetWords: target.relocCount || 0,
      zeroRelocTokenAbiVersion: 1,
    });
    functionTypes.push(first.functionTypes[0]);
    codeBodies.push(buildOriginalRegionRouterBody({
      runtime: options.runtime,
      targetIndex: functionIndexByKey.get(route.targetKey),
      dispatcherIndex: genericDispatcherIndex,
      targetEip: target.eip >>> 0,
      targetRelocCount: target.relocCount || 0,
      targetRelocArg,
      fetchNextImportIndex: fetchNextImport.functionIndex,
      directChainLimit: options.mtGenericIndirectLimit,
    }));
    originalRegionRouters.push({
      sourceKey: route.sourceKey,
      targetKey: route.targetKey,
      functionIndex: router.functionIndex,
      targetFunctionIndex: functionIndexByKey.get(route.targetKey),
      targetRelocCount: target.relocCount || 0,
      dispatcherSites: router.dispatcherSites,
      staticTargetSites: router.staticTargetSites,
      dynamicNormalTargetSites: router.dynamicNormalTargetSites,
      normalTargetSites: router.staticTargetSites + router.dynamicNormalTargetSites,
      residualFallbackSites: router.dispatcherSites - router.staticTargetSites -
        router.dynamicNormalTargetSites,
      publicationPatchKind: 'original-region-target-reloc-base-v1',
    });
  }
  const functionPayload = Buffer.concat([
    writeWasmUleb(functionTypes.length),
    ...functionTypes.map((typeIndex) => writeWasmUleb(typeIndex)),
  ]);
  const exportItems = [];
  for (let i = 0; i < sortedEntries.length; i++) {
    exportItems.push(Buffer.concat([
      wasmNameBytes(sortedEntries[i].key),
      Buffer.from([0]),
      writeWasmUleb(first.importedFunctions + i * functionsPerEntry),
    ]));
    if (threadedGpAbi) {
      exportItems.push(Buffer.concat([
        wasmNameBytes(`t_${sortedEntries[i].key}`),
        Buffer.from([0]),
        writeWasmUleb(first.importedFunctions + i * functionsPerEntry + 1),
      ]));
    }
  }
  const exportPayload = Buffer.concat([writeWasmUleb(exportItems.length), ...exportItems]);
  const codePayload = Buffer.concat([writeWasmUleb(codeBodies.length), ...codeBodies]);
  const wasm = Buffer.concat([
    WASM_MAGIC,
    Buffer.from([0x01, 0x00, 0x00, 0x00]),
    section(1, first.typePayload),
    section(2, genericIndirectEnabled ?
      appendWorkerTableImport(first.importPayload) : first.importPayload),
    section(3, functionPayload),
    ...mergedGlobalsSection(options.directCallDepth, options.mtGenericLastSlotCache),
    section(7, exportPayload),
    section(10, codePayload),
  ]);
  // Locate each direct-call reloc-argument placeholder in the final bytes.
  // A miss or duplicate would leave a garbage marker live in the compiled
  // module (the callee's guards would treat it as a valid pointer), so fail
  // the build instead.
  // Scan the final module once instead of rescanning all bytes for every
  // marker. Accept only the exact i32.const + fixed-width padded SLEB form
  // emitted by relocArgPlaceholder; misses and duplicates remain fatal.
  const markerSites = new Map(relocSiteMarkers.map(({ marker, targetKey, ...metadata }) => [
    marker >>> 0,
    { marker: marker >>> 0, targetKey, ...metadata, offset: -1, count: 0 },
  ]));
  for (let i = 0; i <= wasm.length - 6; i++) {
    if (wasm[i] !== 0x41 ||
        (wasm[i + 1] & 0x80) === 0 ||
        (wasm[i + 2] & 0x80) === 0 ||
        (wasm[i + 3] & 0x80) === 0 ||
        (wasm[i + 4] & 0x80) === 0 ||
        (wasm[i + 5] & 0x80) !== 0) continue;
    const marker = (
      (wasm[i + 1] & 0x7f) |
      ((wasm[i + 2] & 0x7f) << 7) |
      ((wasm[i + 3] & 0x7f) << 14) |
      ((wasm[i + 4] & 0x7f) << 21) |
      ((wasm[i + 5] & 0x0f) << 28)
    ) >>> 0;
    const site = markerSites.get(marker);
    if (!site) continue;
    const pat = relocArgPlaceholder(site.marker);
    if (wasm.compare(pat, 0, pat.length, i, i + pat.length) !== 0) continue;
    site.offset = i;
    site.count++;
    i += pat.length - 1;
  }
  const directCallPatches = [];
  for (const { marker, targetKey, ...metadata } of relocSiteMarkers) {
    const site = markerSites.get(marker >>> 0);
    if (!site || site.count === 0) throw new Error(`Reloc-arg marker not found for site -> ${targetKey}`);
    if (site.count > 1) throw new Error(`Ambiguous reloc-arg marker for site -> ${targetKey}`);
    directCallPatches.push({ offset: site.offset, targetKey, ...metadata }); // offset of the i32.const opcode
  }
  return {
    wasm,
    threadedGpAbi,
    directCallsRewritten,
    guardedDirectCallsRewritten,
    hotNext2DirectCallsRewritten,
    hotNext1DirectCallsRewritten,
    hotNext1TeeBaseDirectCallsRewritten,
    hotNext1DirectBaseDirectCallsRewritten,
    hotJumpDirectCallsRewritten,
    hotJumpTeeBaseDirectCallsRewritten,
    hotJumpDirectBaseDirectCallsRewritten,
    fallbackDirectCallsRewritten,
    threadedStoreTailsSunk,
    threadedStoreBytesSunk,
    threadedStorePatternMisses,
    threadedTailZeroRelocFallbacksInjected,
    internalCloneDirectCallsRewritten,
    internalCloneGenericIndirectTailsRewritten,
    internalCloneFunctions,
    originalRegionRouters,
    originalRegionDispatcherSites,
    originalRegionStaticTargetSites,
    originalRegionDynamicNormalTargetSites,
    originalRegionNormalTargetSites,
    originalRegionRedirectedSites,
    originalRegionRetainedDispatcherSites,
    originalRegionRouterResidualFallbackSites,
    originalRegionRouteStats,
    genericIndirectTailsRewritten,
    genericIndirectFallbackTailsRewritten,
    genericIndirectFastSuccessTailsRewritten,
    genericIndirectFastSuccessThreeLoadTailsRewritten,
    genericIndirectFastSuccessJumpTailsRewritten,
    directCallPatches,
  };
}

function hex32(value) {
  return `0x${(value >>> 0).toString(16).padStart(8, '0')}`;
}

function parseHex32(text) {
  return Number.parseInt(text.replace(/^0x/i, ''), 16) >>> 0;
}

function parseInteriorProfileText(text) {
  const pairs = new Map();
  const topRe = /interiorTop\[([^\]]*)\]/g;
  let topMatch;
  while ((topMatch = topRe.exec(text)) !== null) {
    const body = topMatch[1].trim();
    if (!body) continue;
    for (const rawPart of body.split(',')) {
      const part = rawPart.trim();
      const match = /^([0-9a-f]{8})@([0-9a-f]{8})=(\d+)(?:\/(\d+))?$/i.exec(part);
      if (!match) continue;
      const target = parseHex32(match[1]);
      const blockStart = parseHex32(match[2]);
      const approxCount = Number(match[3]);
      const exactCount = Number(match[4] ?? match[3]);
      if (!Number.isSafeInteger(approxCount) || !Number.isSafeInteger(exactCount)) continue;
      const key = `${match[2].toLowerCase()}:${match[1].toLowerCase()}`;
      const existing = pairs.get(key);
      if (!existing || exactCount > existing.exactCount || approxCount > existing.approxCount) {
        pairs.set(key, { blockStart, target, approxCount, exactCount });
      }
    }
  }
  return Array.from(pairs.values());
}

async function loadProfileGuidedSplits(opts, entries, embeddedProfileText = null) {
  if (!opts.interiorProfile || opts.profileSplitLimit === 0) {
    if (!embeddedProfileText || opts.profileSplitLimit === 0) {
      return { source: opts.interiorProfile || null, threshold: opts.profileSplitThreshold, limit: opts.profileSplitLimit, parsed: 0, belowThreshold: 0, rejected: 0, rejectedByReason: {}, rejectedSamples: [], targets: [] };
    }
  }

  const text = opts.interiorProfile ? await readFile(opts.interiorProfile, 'utf8') : embeddedProfileText;
  const profiled = parseInteriorProfileText(text);
  const entriesByStart = new Map();
  for (const entry of entries) {
    const start = entry.eip >>> 0;
    const len = entry.emulatedLen >>> 0;
    if (!len || !entry.key) continue;
    if (!entriesByStart.has(start)) entriesByStart.set(start, []);
    entriesByStart.get(start).push(entry);
  }

  let rejected = 0;
  let belowThreshold = 0;
  const rejectedByReason = {
    noBlockStart: 0,
    targetAtBlockStart: 0,
    targetAtBlockEnd: 0,
    targetNotInterior: 0,
  };
  const rejectedSamples = [];
  const selectedByBlock = new Map();
  for (const item of profiled) {
    if (item.exactCount < opts.profileSplitThreshold) {
      belowThreshold++;
      continue;
    }
    const candidates = entriesByStart.get(item.blockStart) || [];
    const owner = candidates.find((entry) => {
      const start = entry.eip >>> 0;
      const end = (start + (entry.emulatedLen >>> 0)) >>> 0;
      return item.target > start && item.target < end;
    });
    if (!owner) {
      rejected++;
      let reason = 'targetNotInterior';
      if (candidates.length === 0) {
        reason = 'noBlockStart';
      } else if (candidates.some((entry) => item.target === (entry.eip >>> 0))) {
        reason = 'targetAtBlockStart';
      } else if (candidates.some((entry) => item.target === (((entry.eip >>> 0) + ((entry.emulatedLen || 0) >>> 0)) >>> 0))) {
        reason = 'targetAtBlockEnd';
      }
      rejectedByReason[reason]++;
      if (rejectedSamples.length < opts.profileSplitRejectionSamples) {
        rejectedSamples.push({
          reason,
          blockStart: item.blockStart,
          target: item.target,
          approxCount: item.approxCount,
          exactCount: item.exactCount,
          candidates: candidates.slice(0, 3).map((entry) => ({
            key: entry.key,
            start: entry.eip >>> 0,
            end: ((entry.eip >>> 0) + ((entry.emulatedLen || 0) >>> 0)) >>> 0,
            emulatedLen: (entry.emulatedLen || 0) >>> 0,
          })),
        });
      }
      continue;
    }
    const existing = selectedByBlock.get(item.blockStart);
    if (!existing || item.exactCount > existing.exactCount || item.approxCount > existing.approxCount) {
      selectedByBlock.set(item.blockStart, {
        blockStart: item.blockStart,
        target: item.target,
        byteDistance: (item.target - item.blockStart) >>> 0,
        approxCount: item.approxCount,
        exactCount: item.exactCount,
        containingKey: owner.key,
      });
    }
  }

  const targets = Array.from(selectedByBlock.values())
    .sort((a, b) => b.exactCount - a.exactCount || b.approxCount - a.approxCount)
    .slice(0, opts.profileSplitLimit);
  return {
    source: opts.interiorProfile || PROFILE_NAME,
    threshold: opts.profileSplitThreshold,
    limit: opts.profileSplitLimit,
    parsed: profiled.length,
    belowThreshold,
    rejected,
    rejectedByReason,
    rejectedSamples,
    targets,
  };
}

function formatBytes(n) {
  return `${n.toLocaleString()} B`;
}

function pct(n, d) {
  return d ? (100 * n / d).toFixed(2) : '0.00';
}

function buildGraph(entries) {
  const byKey = new Map(entries.map((entry) => [entry.key, entry]));
  const byEip = new Map();
  for (const entry of entries) {
    const eip = entry.eip >>> 0;
    if (!byEip.has(eip)) byEip.set(eip, []);
    byEip.get(eip).push(entry);
  }
  const findInteriorTargets = (targetEip) => entries.filter((entry) => {
    const start = entry.eip >>> 0;
    const len = entry.emulatedLen >>> 0;
    if (!len || targetEip === start) return false;
    return targetEip > start && targetEip < ((start + len) >>> 0);
  });
  const adjacency = new Map();
  const reverse = new Map();
  const edges = [];
  for (const entry of entries) {
    const from = entry.key;
    const neighbors = new Set();
    for (const kind of ['next1', 'next2', 'jump']) {
      const exit = entry.exits?.[kind];
      if (!exit?.count || !exit.firstTarget) continue;
      const to = exit.firstTarget >>> 0;
      const candidates = byEip.get(to) || [];
      const resolved = candidates.length === 1;
      const interiorCandidates = resolved ? [] : findInteriorTargets(to);
      edges.push({
        from,
        to: resolved ? candidates[0].key : null,
        interiorTo: interiorCandidates.length === 1 ? interiorCandidates[0].key : null,
        targetEip: to,
        kind,
        count: exit.count,
        resolved,
        ambiguous: candidates.length > 1,
        interior: !resolved && interiorCandidates.length === 1,
        ambiguousInterior: !resolved && interiorCandidates.length > 1,
      });
      if (resolved) {
        const targetKey = candidates[0].key;
        neighbors.add(targetKey);
        if (!reverse.has(targetKey)) reverse.set(targetKey, new Set());
        reverse.get(targetKey).add(from);
      }
    }
    adjacency.set(from, neighbors);
  }
  return { byKey, byEip, adjacency, reverse, edges };
}

function findCyclicInfo(entries, edges) {
  const adjacency = new Map(entries.map((entry) => [entry.key, []]));
  for (const edge of edges) {
    if (edge.resolved && edge.to) adjacency.get(edge.from)?.push(edge.to);
  }

  let nextIndex = 0;
  const indexByKey = new Map();
  const lowByKey = new Map();
  const stack = [];
  const onStack = new Set();
  const cyclic = new Set();
  const componentByKey = new Map();
  let nextComponent = 0;

  function visit(key) {
    indexByKey.set(key, nextIndex);
    lowByKey.set(key, nextIndex);
    nextIndex++;
    stack.push(key);
    onStack.add(key);

    for (const next of adjacency.get(key) || []) {
      if (!indexByKey.has(next)) {
        visit(next);
        lowByKey.set(key, Math.min(lowByKey.get(key), lowByKey.get(next)));
      } else if (onStack.has(next)) {
        lowByKey.set(key, Math.min(lowByKey.get(key), indexByKey.get(next)));
      }
    }

    if (lowByKey.get(key) === indexByKey.get(key)) {
      const component = [];
      let item;
      do {
        item = stack.pop();
        onStack.delete(item);
        component.push(item);
      } while (item !== key);

      const componentId = nextComponent++;
      for (const member of component) componentByKey.set(member, componentId);

      if (component.length > 1) {
        for (const member of component) cyclic.add(member);
      } else {
        const only = component[0];
        if ((adjacency.get(only) || []).includes(only)) cyclic.add(only);
      }
    }
  }

  for (const entry of entries) {
    if (!indexByKey.has(entry.key)) visit(entry.key);
  }
  return { cyclic, componentByKey };
}

function findComponents(entries, graph) {
  const seen = new Set();
  const components = [];
  for (const entry of entries) {
    const start = entry.key;
    if (seen.has(start)) continue;
    const stack = [start];
    const keys = [];
    seen.add(start);
    while (stack.length) {
      const key = stack.pop();
      keys.push(key);
      for (const next of graph.adjacency.get(key) ?? []) {
        if (!seen.has(next)) {
          seen.add(next);
          stack.push(next);
        }
      }
      for (const prev of graph.reverse.get(key) ?? []) {
        if (!seen.has(prev)) {
          seen.add(prev);
          stack.push(prev);
        }
      }
    }
    const componentEntries = keys.map((key) => graph.byKey.get(key));
    const wasmBytes = componentEntries.reduce((sum, item) => sum + (item.wasmBytes || 0), 0);
    components.push({ entries: componentEntries, wasmBytes });
  }
  return components.sort((a, b) => b.wasmBytes - a.wasmBytes);
}

function assertAcyclicEdges(edges) {
  const adjacency = new Map();
  const indegree = new Map();
  for (const edge of edges) {
    if (!adjacency.has(edge.from)) adjacency.set(edge.from, []);
    adjacency.get(edge.from).push(edge.to);
    if (!indegree.has(edge.from)) indegree.set(edge.from, 0);
    indegree.set(edge.to, (indegree.get(edge.to) || 0) + 1);
  }
  const ready = [];
  for (const [key, degree] of indegree) {
    if (degree === 0) ready.push(key);
  }
  let visited = 0;
  while (ready.length) {
    const key = ready.pop();
    visited++;
    for (const next of adjacency.get(key) || []) {
      const degree = indegree.get(next) - 1;
      indegree.set(next, degree);
      if (degree === 0) ready.push(next);
    }
  }
  if (visited !== indegree.size) {
    throw new Error(`Selected direct-tail graph remains cyclic: visited ${visited}/${indegree.size} nodes`);
  }
}

function splitOversizedComponent(component, budget) {
  if (component.wasmBytes <= budget) return [component];
  const chunks = [];
  let current = [];
  let currentBytes = 0;
  for (const entry of [...component.entries].sort((a, b) => (b.wasmBytes || 0) - (a.wasmBytes || 0))) {
    const bytes = entry.wasmBytes || 0;
    if (current.length && currentBytes + bytes > budget) {
      chunks.push({ entries: current, wasmBytes: currentBytes });
      current = [];
      currentBytes = 0;
    }
    current.push(entry);
    currentBytes += bytes;
  }
  if (current.length) chunks.push({ entries: current, wasmBytes: currentBytes });
  return chunks;
}

function packComponents(components, budget) {
  const items = components.flatMap((component) => splitOversizedComponent(component, budget));
  items.sort((a, b) => b.wasmBytes - a.wasmBytes);
  const groups = [];
  for (const item of items) {
    let best = null;
    let bestRemainder = Infinity;
    for (const group of groups) {
      const remainder = budget - group.wasmBytes - item.wasmBytes;
      if (remainder >= 0 && remainder < bestRemainder) {
        best = group;
        bestRemainder = remainder;
      }
    }
    if (!best) {
      best = { entries: [], wasmBytes: 0 };
      groups.push(best);
    }
    best.entries.push(...item.entries);
    best.wasmBytes += item.wasmBytes;
  }
  return groups.sort((a, b) => b.wasmBytes - a.wasmBytes);
}

function findDefaultBinaryenJs() {
  for (const candidate of ['./binaryen_js.js', './binaryen.js', './bin/binaryen_js.js', './bin/binaryen.js']) {
    const resolved = resolve(candidate);
    if (existsSync(resolved)) return resolved;
  }
  return null;
}

async function loadBinaryen(path) {
  const binaryenPath = resolve(path);
  const imported = await import(pathToFileURL(binaryenPath).href);
  const mod = imported.default ?? imported.Binaryen ?? imported;
  if (typeof mod === 'function') {
    return await mod({ locateFile(file) { return resolve(dirname(binaryenPath), file); } });
  }
  return mod;
}

function configureLocalPreset(binaryen) {
  binaryen.setOptimizeLevel?.(2);
  binaryen.setShrinkLevel?.(0);
  return LOCAL_PASSES;
}

function optimizeWasm(binaryen, data) {
  // The generated block modules use post-MVP features (sign-extension ops;
  // shared memory in MT builds). Older binaryen.js exposed
  // readBinaryWithFeatures; newer versions dropped it, so fall back to
  // readBinary + Module.setFeatures before validating/optimizing.
  const features = binaryen.Features?.All ??
    ((binaryen.Features?.SignExt ?? 0) | (binaryen.Features?.Atomics ?? 0));
  const mod = binaryen.readBinaryWithFeatures
    ? binaryen.readBinaryWithFeatures(new Uint8Array(data), features)
    : binaryen.readBinary(new Uint8Array(data));
  try {
    mod.setFeatures?.(features);
    mod.runPasses(configureLocalPreset(binaryen));
    if (!mod.validate()) throw new Error('Binaryen validation failed');
    return Buffer.from(mod.emitBinary());
  } finally {
    mod.dispose();
  }
}

async function loadCacheZip(path) {
  const zipBuf = await readFile(path);
  const zipEntries = parseZip(zipBuf);
  const rawManifest = zipEntries.find((entry) => entry.name === 'boxedwine-jit-manifest.json');
  if (!rawManifest) throw new Error(`${path} must contain boxedwine-jit-manifest.json`);
  const manifest = JSON.parse(rawManifest.data.toString('utf8'));
  if (manifest.version !== 1 || !Array.isArray(manifest.entries)) {
    throw new Error(`Unsupported or missing boxedwine-jit-manifest.json entries in ${path}`);
  }
  const rawProfile = zipEntries.find((entry) => entry.name === PROFILE_NAME);
  const wasmByKey = new Map();
  for (const entry of zipEntries) {
    if (!isWasm(entry.data)) continue;
    const match = CACHE_NAME_RE.exec(entry.name);
    if (!match) continue;
    const key = entry.name.replace(/\.wasm$/i, '');
    wasmByKey.set(key, {
      key,
      eip: parseInt(match[1], 16) >>> 0,
      blockHash: parseInt(match[2], 16) >>> 0,
      data: Buffer.from(entry.data),
    });
  }

  const missingManifestKeys = [];
  const entries = manifest.entries.flatMap((entry) => {
    const wasm = wasmByKey.get(entry.key);
    if (!wasm) {
      missingManifestKeys.push(entry.key);
      return [];
    }
    return [{
      key: entry.key,
      eip: entry.eip >>> 0,
      blockHash: entry.blockHash >>> 0,
      wasmBytes: entry.wasmBytes || wasm.data.length,
      opCount: entry.opCount || 0,
      emulatedLen: entry.emulatedLen || 0,
      relocCount: entry.relocCount || 0,
      exits: entry.exits || {},
      data: wasm.data,
    }];
  });
  if (missingManifestKeys.length) {
    gWarnings.push(`${path}: ignored ${missingManifestKeys.length} manifest entries with no wasm entry: ${missingManifestKeys.slice(0, 8).join(', ')}${missingManifestKeys.length > 8 ? ', ...' : ''}`);
  }

  return {
    path,
    zipBuf,
    rawManifest: rawManifest.data,
    manifest,
    rawProfile,
    profileText: rawProfile ? rawProfile.data.toString('utf8') : null,
    entries,
    missingManifestKeys,
  };
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const inputCache = await loadCacheZip(opts.inputZip);
  const manifest = inputCache.manifest;
  const rawManifestSha256 = createHash('sha256').update(inputCache.rawManifest).digest('hex');
  const originalRegionRuntimeIdentity = opts.mtOriginalRegionMode === null ? null : {
    kind: 'azrok-ntcurrentteb-original-region-v1',
    runtimeStem: NTCURRENTTEB_ORIGINAL_REGION_SPEC.runtimeStem,
    rawManifestSha256,
    requiredZeroRelocPublicationTokenAbiVersion: 1,
  };
  const mrgRuntimeIdentity = opts.mrg1RuntimeStem === null ? null : {
    kind: 'azrok-mrg1-movsd-runtime-zero-v1',
    runtimeStem: opts.mrg1RuntimeStem,
    rawManifestSha256,
    requiredGuardAbiVersion: 1,
  };
  if (mrgRuntimeIdentity && rawManifestSha256 !== opts.mrg1RawManifestSha256) {
    throw new Error(`MRG raw manifest SHA-256 mismatch: got ${rawManifestSha256}, expected ${opts.mrg1RawManifestSha256}`);
  }
  const parityCurrentRuntimeIdentity = opts.parityCurrentRuntimeStem === null ? null : {
    kind: 'azrok-parity-current1-v1',
    runtimeStem: opts.parityCurrentRuntimeStem,
    rawManifestSha256,
    requiredInlineParityAbiVersion: 1,
  };
  if (parityCurrentRuntimeIdentity && rawManifestSha256 !== opts.parityCurrentRawManifestSha256) {
    throw new Error(`Parity raw manifest SHA-256 mismatch: got ${rawManifestSha256}, expected ${opts.parityCurrentRawManifestSha256}`);
  }
  const int9aSpecializedRuntimeIdentity = opts.int9aSpecializedRuntimeStem === null ? null : {
    kind: 'azrok-int9a-specialized-v1',
    runtimeStem: opts.int9aSpecializedRuntimeStem,
    rawManifestSha256,
    requiredInt9ASpecializedAbiVersion: 1,
  };
  if (int9aSpecializedRuntimeIdentity) {
    if (manifest.privateRecord?.kind !== 'int9a-specialized' ||
        manifest.privateRecord?.stem !== opts.int9aSpecializedRuntimeStem ||
        manifest.privateRecord?.abi !== 1) {
      throw new Error('INT9A raw manifest private-record identity mismatch');
    }
    if (rawManifestSha256 !== opts.int9aSpecializedRawManifestSha256) {
      throw new Error(`INT9A raw manifest SHA-256 mismatch: got ${rawManifestSha256}, expected ${opts.int9aSpecializedRawManifestSha256}`);
    }
  }
  const runtimeLogicComposite32Identity = opts.runtimeLogicComposite32RuntimeStem === null ? null : {
    kind: 'azrok-runtime-logic-composite32-v1',
    runtimeStem: opts.runtimeLogicComposite32RuntimeStem,
    rawManifestSha256,
    requiredRuntimeLogicComposite32AbiVersion: 1,
  };
  if (runtimeLogicComposite32Identity && rawManifestSha256 !== opts.runtimeLogicComposite32RawManifestSha256) {
    throw new Error(`Runtime logical-composite32 raw manifest SHA-256 mismatch: got ${rawManifestSha256}, expected ${opts.runtimeLogicComposite32RawManifestSha256}`);
  }
  const entries = inputCache.entries;
  const originalRegionContract = opts.mtOriginalRegionMode === null ? null :
    validateOriginalRegionEntries(entries, rawManifestSha256);
  const internalCloneTargets = validateInternalCloneTargets(
    entries, opts.mtInternalCloneTargetKeys);
  const embeddedProfileText = inputCache.profileText;
  if (manifest.cacheVersion !== 'v5') {
    throw new Error(`Input cache version ${manifest.cacheVersion || 'unknown'} is not v5; record a new cache with this build`);
  }
  // Copy runtime constants so explicit, verified ABI overrides can be carried
  // into the grouped output without mutating the parsed source manifest.
  const runtime = JSON.parse(JSON.stringify(manifest.runtime || null));
  if (runtime?.cpu && opts.mtDirectChainDepthOffset !== null) {
    runtime.cpu.directChainDepthOffset = opts.mtDirectChainDepthOffset;
  }
  if (runtime?.cpu && opts.mtBailoutOffset !== null) {
    runtime.cpu.bailoutOffset = opts.mtBailoutOffset;
  }
  if (opts.mtPfnJitCodeOffset !== null) {
    if (!runtime.decodedOp) runtime.decodedOp = {};
    runtime.decodedOp.pfnJitCodeOffset = opts.mtPfnJitCodeOffset;
  }
  if (runtime?.cpu && opts.mtNextOpOffset !== null) {
    runtime.cpu.nextOpOffset = opts.mtNextOpOffset;
  }
  if (runtime?.cpu && opts.mtGenericWorkerReadyBitmapOffset !== null) {
    runtime.cpu.workerReadyBitmapOffset = opts.mtGenericWorkerReadyBitmapOffset;
  }
  if (runtime?.cpu && opts.mtCpuFlagsOffset !== null) runtime.cpu.flagsOffset = opts.mtCpuFlagsOffset;
  if (runtime?.cpu && opts.mtCpuResultOffset !== null) runtime.cpu.resultOffset = opts.mtCpuResultOffset;
  if (runtime?.cpu && opts.mtCpuLazyFlagTypeOffset !== null) runtime.cpu.lazyFlagTypeOffset = opts.mtCpuLazyFlagTypeOffset;
  if (runtime?.cpu && opts.mtCpuLazyFlagTypePrevOffset !== null) runtime.cpu.lazyFlagTypePrevOffset = opts.mtCpuLazyFlagTypePrevOffset;
  if (opts.runtimeLazyZNZ && (!runtime?.cpu ||
      !Number.isInteger(runtime.cpu.flagsOffset) || runtime.cpu.flagsOffset < 0 || (runtime.cpu.flagsOffset & 3) !== 0 ||
      !Number.isInteger(runtime.cpu.resultOffset) || runtime.cpu.resultOffset < 0 || (runtime.cpu.resultOffset & 3) !== 0 ||
      !Number.isInteger(runtime.cpu.lazyFlagTypeOffset) || runtime.cpu.lazyFlagTypeOffset < 0 ||
      !Number.isInteger(runtime.cpu.lazyFlagTypePrevOffset) || runtime.cpu.lazyFlagTypePrevOffset < 0)) {
    throw new Error('runtime-lazy-znz CPU ABI is missing or invalid');
  }
  if (runtime?.cpu) {
    if (opts.mtCpuThreadOffset !== null) runtime.cpu.threadOffset = opts.mtCpuThreadOffset;
    if (opts.mtKthreadTerminatingOffset !== null) runtime.cpu.kthreadTerminatingOffset = opts.mtKthreadTerminatingOffset;
    if (opts.mtKthreadProcessOffset !== null) runtime.cpu.kthreadProcessOffset = opts.mtKthreadProcessOffset;
    if (opts.mtCpuEipOffset !== null) runtime.cpu.eipOffset = opts.mtCpuEipOffset;
    if (opts.mtCpuCsAddressOffset !== null) runtime.cpu.csAddressOffset = opts.mtCpuCsAddressOffset;
    if (opts.mtCpuBigOffset !== null) runtime.cpu.bigOffset = opts.mtCpuBigOffset;
    if (opts.mtCpuOpCacheOffset !== null) runtime.cpu.opCacheOffset = opts.mtCpuOpCacheOffset;
  }
  if (opts.mtIndirectRelocMapBase !== null || opts.mtIndirectRelocMapCapacity !== null) {
    if (!runtime.jit) runtime.jit = {};
    if (opts.mtIndirectRelocMapBase !== null) runtime.jit.indirectRelocMapBase = opts.mtIndirectRelocMapBase;
    if (opts.mtIndirectRelocMapCapacity !== null) runtime.jit.indirectRelocMapCapacity = opts.mtIndirectRelocMapCapacity;
  }
  if (!runtime?.cpu || !runtime?.scheduler ||
      !Number.isInteger(runtime.cpu.blockInstructionCountOffset) ||
      !Number.isInteger(runtime.cpu.yieldOffset) ||
      !Number.isInteger(runtime.scheduler.contextTimeRemainingPtr)) {
    throw new Error('Input manifest is missing runtime CPU/scheduler constants; regenerate the JIT cache zip with the updated BoxedWine build');
  }
  // Multi-threaded builds have no scheduler slice-budget global, so their
  // manifests carry contextTimeRemainingPtr=0. The ST yield-aware tail bakes
  // that address into rewritten sites, so MT groups use plain return_call
  // tails instead — and edges touching cyclic blocks are not rewritten at
  // all (in ST the budget check is what bounds those cycles; in MT an
  // unguarded cyclic tail chain would never return to the dispatcher).
  // The output manifest carries mt:true so the loader matches build shapes.
  const isMtInput = runtime.scheduler.contextTimeRemainingPtr === 0;
  const mtCyclicTailEnabled = isMtInput && opts.mtCyclicLimit > 0;
  const mtCycleBreakForwardEnabled = isMtInput && opts.mtCycleBreakForward;
  const mtGenericIndirectEnabled = isMtInput && opts.mtGenericIndirectLimit > 0;
  if (opts.mtCyclicLimit > 0 && !isMtInput) {
    throw new Error('--mt-cyclic-limit is only valid for an MT cache manifest');
  }
  if (opts.mtCycleBreakForward && !isMtInput) {
    throw new Error('--mt-cycle-break-forward is only valid for an MT cache manifest');
  }
  if (opts.mtGenericIndirectLimit > 0 && !isMtInput) {
    throw new Error('--mt-generic-indirect-limit is only valid for an MT cache manifest');
  }
  if (internalCloneTargets.length && !isMtInput) {
    throw new Error('--mt-internal-clone-target-keys is only valid for an MT cache manifest');
  }
  if (originalRegionContract && !isMtInput) {
    throw new Error('--mt-original-region-mode is only valid for an MT cache manifest');
  }
  if (originalRegionContract && opts.budget !==
      NTCURRENTTEB_ORIGINAL_REGION_SPEC.expected.budgetBytes) {
    throw new Error(
      `Original-region requires exact ${NTCURRENTTEB_ORIGINAL_REGION_SPEC.expected.budgetBytes}-byte budget`);
  }
  if (mtCyclicTailEnabled &&
      (!Number.isInteger(runtime.cpu.directChainDepthOffset) || runtime.cpu.directChainDepthOffset <= 0 ||
       !Number.isInteger(runtime.cpu.bailoutOffset) || runtime.cpu.bailoutOffset <= 0)) {
    throw new Error('MT cyclic stitching requires directChainDepthOffset and bailoutOffset from the exact replay runtime manifest (or explicit verified overrides)');
  }
  if (opts.directCalls && isMtInput &&
      (!Number.isInteger(runtime.decodedOp?.pfnJitCodeOffset) ||
       runtime.decodedOp.pfnJitCodeOffset <= 0 ||
       (runtime.decodedOp.pfnJitCodeOffset & 3) !== 0 ||
       !Number.isInteger(runtime.cpu?.nextOpOffset) ||
       runtime.cpu.nextOpOffset <= 0 ||
       (runtime.cpu.nextOpOffset & 3) !== 0)) {
    throw new Error('MT direct stitching requires aligned DecodedOp::pfnJitCode and CPU::nextOp offsets from the exact replay runtime manifest (or verified overrides)');
  }
  if (mtGenericIndirectEnabled &&
      (!Number.isInteger(runtime.cpu.nextOpOffset) || runtime.cpu.nextOpOffset <= 0 ||
       (runtime.cpu.nextOpOffset & 3) !== 0 ||
       !Number.isInteger(runtime.cpu.directChainDepthOffset) || runtime.cpu.directChainDepthOffset <= 0 ||
       !Number.isInteger(runtime.cpu.bailoutOffset) || runtime.cpu.bailoutOffset <= 0 ||
       !Number.isInteger(runtime.decodedOp?.pfnJitCodeOffset) || runtime.decodedOp.pfnJitCodeOffset <= 0 ||
       !Number.isInteger(runtime.jit?.indirectRelocMapBase) || runtime.jit.indirectRelocMapBase <= 0 ||
       (runtime.jit.indirectRelocMapBase & 3) !== 0 ||
       !Number.isInteger(runtime.jit?.indirectRelocMapCapacity) || runtime.jit.indirectRelocMapCapacity <= 0)) {
    throw new Error('MT generic indirect stitching requires exact nextOp/depth/bailout/pfn and aligned reloc-map ABI constants');
  }
  if (opts.mtGenericWorkerReadyBitmapOffset !== null &&
      (!Number.isInteger(runtime.cpu.workerReadyBitmapOffset) ||
       runtime.cpu.workerReadyBitmapOffset <= 0 ||
       (runtime.cpu.workerReadyBitmapOffset & 3) !== 0)) {
    throw new Error('MT generic worker-ready bitmap requires an aligned exact CPU pointer offset');
  }
  if (opts.mtGenericInlineFetch) {
    const fields = [
      ['threadOffset', runtime.cpu.threadOffset],
      ['eipOffset', runtime.cpu.eipOffset],
      ['csAddressOffset', runtime.cpu.csAddressOffset],
      ['bigOffset', runtime.cpu.bigOffset],
      ['opCacheOffset', runtime.cpu.opCacheOffset],
    ];
    for (const [name, value] of fields) {
      if (!Number.isInteger(value) || value < 0 || (value & 3) !== 0) {
        throw new Error(`MT inline fetch requires aligned exact CPU ${name}`);
      }
    }
    if (!Number.isInteger(runtime.cpu.kthreadTerminatingOffset) ||
        runtime.cpu.kthreadTerminatingOffset < 0) {
      throw new Error('MT inline fetch requires exact KThread::terminating offset');
    }
    if (!Number.isInteger(runtime.cpu.kthreadProcessOffset) ||
        runtime.cpu.kthreadProcessOffset < 0 ||
        (runtime.cpu.kthreadProcessOffset & 3) !== 0) {
      throw new Error('MT inline fetch requires aligned exact KThread::process offset');
    }
  }
  // Flat mode carries no grouped manifest, so there is nowhere to put split
  // hints; skip the profile parse entirely.
  const profileGuidedSplits = opts.flat
    ? { targets: [] }
    : await loadProfileGuidedSplits(opts, entries, embeddedProfileText);

  const binaryenJs = opts.binaryenJs ? resolve(opts.binaryenJs) : findDefaultBinaryenJs();
  if (!binaryenJs) throw new Error('Could not find binaryen_js.js; pass --binaryen-js PATH');
  const binaryen = await loadBinaryen(binaryenJs);

  let optimized = 0;
  let wasmBefore = 0;
  let wasmAfter = 0;
  for (const entry of entries) {
    wasmBefore += entry.data.length;
    try {
      entry.data = optimizeWasm(binaryen, entry.data);
    } catch (error) {
      throw new Error(`Binaryen optimize failed for ${entry.key}: ${error.message}`);
    }
    optimized++;
    entry.wasmBytes = entry.data.length;
    wasmAfter += entry.data.length;
  }

  if (opts.flat) {
    // Re-emit the optimized per-block modules in the original flat layout.
    // The runtime consumes this exactly like a recorded zip (flat lookup in
    // ST, raw-bytes preference in MT); the refreshed manifest keeps the zip
    // analyzable and re-pipeable.
    const outEntries = entries.map((entry) => ({
      name: `${entry.key}.wasm`,
      data: entry.data,
    }));
    const flatManifest = {
      version: 1,
      generatedAt: new Date().toISOString(),
      entryCount: entries.length,
      runtime,
      source: {
        inputZip: opts.inputZip,
        inputManifestVersion: manifest.version,
        optimized: true,
        preset: 'local',
        flat: true,
      },
      entries: entries.map((entry) => ({
        key: entry.key,
        eip: entry.eip,
        blockHash: entry.blockHash,
        wasmBytes: entry.data.length,
        opCount: entry.opCount,
        emulatedLen: entry.emulatedLen,
        relocCount: entry.relocCount || 0,
        exits: entry.exits,
      })),
    };
    outEntries.push({
      name: 'boxedwine-jit-manifest.json',
      data: Buffer.from(`${JSON.stringify(flatManifest, null, 2)}\n`, 'utf8'),
    });
    if (embeddedProfileText) {
      outEntries.push({ name: PROFILE_NAME, data: Buffer.from(embeddedProfileText, 'utf8') });
    }
    const out = buildZip(outEntries, opts.compressionLevel);
    await writeFile(opts.outputZip, out);

    console.log(`Input:              ${opts.inputZip}`);
    console.log(`Output:             ${opts.outputZip}`);
    console.log(`Mode:               flat (Binaryen optimize only; no grouping/direct calls/splits)`);
    console.log(`Optimized:          ${optimized}/${entries.length}`);
    console.log(`Wasm bytes:         ${formatBytes(wasmBefore)} -> ${formatBytes(wasmAfter)} (${pct(wasmAfter - wasmBefore, wasmBefore)}%)`);
    console.log(`Zip bytes:          ${formatBytes(inputCache.zipBuf.length)} -> ${formatBytes(out.length)}`);
    for (const warning of gWarnings) {
      console.log(`Warning:            ${warning}`);
    }
    return;
  }

  const shape = parseImportShape(entries[0].data);
  const graph = buildGraph(entries);
  const cyclicInfo = findCyclicInfo(entries, graph.edges);
  const cyclicKeys = cyclicInfo.cyclic;
  const components = findComponents(entries, graph);
  const groups = packComponents(components, opts.budget);
  const originalRegionLayout = originalRegionContract ?
    applyOriginalRegionLayout(groups, entries) : null;
  const groupByKey = new Map();
  for (let i = 0; i < groups.length; i++) {
    for (const entry of groups[i].entries) groupByKey.set(entry.key, i);
  }
  let resolvedEdges = 0;
  let unresolvedEdges = 0;
  let ambiguousEdges = 0;
  let interiorEdges = 0;
  let ambiguousInteriorEdges = 0;
  let inGroupEdges = 0;
  const directEdgesByGroup = new Array(groups.length).fill(0);
  const directCallsRewrittenByGroup = new Array(groups.length).fill(0);
  const guardedDirectCallsRewrittenByGroup = new Array(groups.length).fill(0);
  const hotNext2EligibleByGroup = new Array(groups.length).fill(0);
  const hotNext2NoRelocByGroup = new Array(groups.length).fill(0);
  const hotNext2RewrittenByGroup = new Array(groups.length).fill(0);
  const hotNext2PatternMissesByGroup = new Array(groups.length).fill(0);
  const hotNext1EligibleByGroup = new Array(groups.length).fill(0);
  const hotNext1NoRelocByGroup = new Array(groups.length).fill(0);
  const hotNext1RewrittenByGroup = new Array(groups.length).fill(0);
  const hotNext1PatternMissesByGroup = new Array(groups.length).fill(0);
  const hotNext1TeeBaseByGroup = new Array(groups.length).fill(0);
  const hotNext1DirectBaseByGroup = new Array(groups.length).fill(0);
  const hotJumpEligibleByGroup = new Array(groups.length).fill(0);
  const hotJumpNoRelocByGroup = new Array(groups.length).fill(0);
  const hotJumpRewrittenByGroup = new Array(groups.length).fill(0);
  const hotJumpPatternMissesByGroup = new Array(groups.length).fill(0);
  const hotJumpTeeBaseByGroup = new Array(groups.length).fill(0);
  const hotJumpDirectBaseByGroup = new Array(groups.length).fill(0);
  const fallbackDirectCallsRewrittenByGroup = new Array(groups.length).fill(0);
  const threadedStoreTailsSunkByGroup = new Array(groups.length).fill(0);
  const threadedStoreBytesSunkByGroup = new Array(groups.length).fill(0);
  const threadedStorePatternMissesByGroup = new Array(groups.length).fill(0);
  const genericIndirectTailsByGroup = new Array(groups.length).fill(0);
  const genericIndirectFallbackTailsByGroup = new Array(groups.length).fill(0);
  const genericIndirectFastSuccessTailsByGroup = new Array(groups.length).fill(0);
  const genericIndirectFastSuccessThreeLoadTailsByGroup = new Array(groups.length).fill(0);
  const genericIndirectFastSuccessJumpTailsByGroup = new Array(groups.length).fill(0);
  const internalCloneDirectCallsByGroup = new Array(groups.length).fill(0);
  const internalCloneFunctionsByGroup = new Array(groups.length).fill(0);
  const internalCloneGenericIndirectTailsByGroup = new Array(groups.length).fill(0);
  const internalCloneTargetByKey = new Map(
    internalCloneTargets.map((entry) => [entry.key, entry]));
  const originalRegionRoutePairs = new Set(originalRegionContract ?
    NTCURRENTTEB_ORIGINAL_REGION_SPEC.routes.map((route) =>
      `${route.sourceKey}->${route.targetKey}`) : []);
  const internalCloneCandidateEdges = internalCloneTargets.length ? graph.edges
    .filter((edge) => edge.resolved && internalCloneTargetByKey.has(edge.to))
    .filter((edge) => (edge.kind === 'next1' || edge.kind === 'next2') &&
      opts.directCallKinds.has(edge.kind)) : [];
  const internalCloneCrossGroupEdges = internalCloneCandidateEdges.filter((edge) =>
    groupByKey.get(edge.from) !== groupByKey.get(edge.to)).length;
  const allManifestKeys = new Set(entries.map((entry) => entry.key));
  const allInternalCloneKeys = new Set();
  let internalClonePatternRejectedEdges = 0;
  let internalClonePatternRejectedGroups = 0;
  let cycleBreakEdgesAdmitted = 0;
  const selectedDirectEdges = [];
  for (const edge of graph.edges) {
    if (!edge.resolved) {
      if (edge.ambiguous) ambiguousEdges++;
      if (edge.interior) interiorEdges++;
      if (edge.ambiguousInterior) ambiguousInteriorEdges++;
      unresolvedEdges++;
      continue;
    }
    resolvedEdges++;
    const groupId = groupByKey.get(edge.from);
    if (groupId === groupByKey.get(edge.to)) {
      inGroupEdges++;
      directEdgesByGroup[groupId]++;
    }
  }

  const outEntries = [];
  const manifestGroups = groups.map((group, index) => {
    const groupName = `group-${String(index).padStart(4, '0')}`;
    const sorted = [...group.entries].sort((a, b) => a.eip - b.eip);
    const groupPath = `groups/${groupName}.wasm`;
    const directEdges = graph.edges
      .filter((edge) => edge.resolved)
      .filter((edge) => groupByKey.get(edge.from) === index &&
        groupByKey.get(edge.to) === index)
      .filter((edge) => !internalCloneTargetByKey.has(edge.to))
      // The pinned region owns these nine exact boundaries in both control
      // and candidate layouts. Do not let newly co-located raw-profile edges
      // turn into ordinary direct tails and contaminate the A/B control.
      .filter((edge) => !originalRegionRoutePairs.has(`${edge.from}->${edge.to}`))
      // Generic jump tails are deliberately excluded by default. Their
      // destination is currently inferred by a raw backward byte scan, which
      // can cross a structured-control arm and execute the wrong guest block.
      .filter((edge) => opts.directCallKinds.has(edge.kind))
      .map((edge) => {
        const touchesCyclic = cyclicKeys.has(edge.from) || cyclicKeys.has(edge.to);
        const sameCyclicComponent = cyclicKeys.has(edge.from) && cyclicKeys.has(edge.to) &&
          cyclicInfo.componentByKey.get(edge.from) === cyclicInfo.componentByKey.get(edge.to);
        const fromEip = graph.byKey.get(edge.from)?.eip >>> 0;
        const toEip = graph.byKey.get(edge.to)?.eip >>> 0;
        // Condensation-graph edges cannot participate in a cycle. Inside one
        // cyclic SCC, strictly increasing guest EIP gives every admitted edge
        // the same total order, so at least the backward loop edge still exits
        // to the dispatcher and the generated tail graph remains acyclic.
        const cycleBreakSafe = mtCycleBreakForwardEnabled && touchesCyclic &&
          (!sameCyclicComponent || toEip > fromEip);
        if (cycleBreakSafe) cycleBreakEdgesAdmitted++;
        return {
          ...edge,
          guarded: touchesCyclic && !cycleBreakSafe,
          cycleBreakSafe,
        };
      })
      // Cyclic MT tails are admitted only with the explicit finite CPU budget.
      // In cycle-break-forward mode, next1/next2 back-edges keep the dispatcher
      // exit because cyclic8 already measured neutral/negative; only the exact
      // JumpIfCondition matcher gets the bounded residual self/back edges.
      .filter((edge) => !isMtInput || !edge.guarded ||
        (mtCyclicTailEnabled && (!mtCycleBreakForwardEnabled || edge.kind === 'jump')));
    const rawInternalCloneEdges = internalCloneTargets.length ? graph.edges
      .filter((edge) => edge.resolved && internalCloneTargetByKey.has(edge.to))
      .filter((edge) => groupByKey.get(edge.from) === index)
      .filter((edge) => (edge.kind === 'next1' || edge.kind === 'next2') &&
        opts.directCallKinds.has(edge.kind))
      .map((edge) => ({
        ...edge,
        guarded: false,
        cycleBreakSafe: true,
        internalCloneOnly: true,
      })) : [];
    const admittedCloneSources = new Set();
    for (const sourceKey of new Set(rawInternalCloneEdges.map((edge) => edge.to))) {
      const sourceEntry = internalCloneTargetByKey.get(sourceKey);
      const matched = countInternalCloneDirectSites(
        sorted, rawInternalCloneEdges, sourceEntry, runtime);
      if (matched > 0) {
        admittedCloneSources.add(sourceKey);
      } else {
        internalClonePatternRejectedGroups++;
        internalClonePatternRejectedEdges += rawInternalCloneEdges.filter(
          (edge) => edge.to === sourceKey).length;
      }
    }
    const internalCloneEdges = rawInternalCloneEdges.filter((edge) =>
      admittedCloneSources.has(edge.to));
    const internalClones = [...admittedCloneSources]
      .sort()
      .map((sourceKey) => {
        const cloneKey = internalCloneKey(sourceKey, index);
        if (allManifestKeys.has(cloneKey) || allInternalCloneKeys.has(cloneKey)) {
          throw new Error(`Internal clone key collision: ${cloneKey}`);
        }
        allInternalCloneKeys.add(cloneKey);
        return {
          cloneKey,
          sourceEntry: internalCloneTargetByKey.get(sourceKey),
          incomingEdges: internalCloneEdges.filter((edge) => edge.to === sourceKey).length,
        };
      });
    const selectedEdges = [...directEdges, ...internalCloneEdges];
    directEdgesByGroup[index] += internalCloneEdges.length;
    selectedDirectEdges.push(...selectedEdges);
    const hotNext2Candidates = selectedEdges.filter((edge) => edge.kind === 'next2');
    const hotNext2Eligible = hotNext2Candidates.filter((edge) =>
      edge.internalCloneOnly || (graph.byKey.get(edge.to)?.relocCount || 0) > 0).length;
    hotNext2EligibleByGroup[index] = hotNext2Eligible;
    hotNext2NoRelocByGroup[index] = hotNext2Candidates.length - hotNext2Eligible;
    const hotNext1Candidates = selectedEdges.filter((edge) => edge.kind === 'next1');
    const hotNext1Eligible = hotNext1Candidates.filter((edge) =>
      edge.internalCloneOnly || (graph.byKey.get(edge.to)?.relocCount || 0) > 0).length;
    hotNext1EligibleByGroup[index] = hotNext1Eligible;
    hotNext1NoRelocByGroup[index] = hotNext1Candidates.length - hotNext1Eligible;
    const hotJumpCandidates = selectedEdges.filter((edge) => edge.kind === 'jump');
    const hotJumpEligible = hotJumpCandidates.filter((edge) =>
      (graph.byKey.get(edge.to)?.relocCount || 0) > 0).length;
    hotJumpEligibleByGroup[index] = hotJumpEligible;
    hotJumpNoRelocByGroup[index] = hotJumpCandidates.length - hotJumpEligible;
    const originalRegionRoutes = originalRegionContract &&
        index === originalRegionLayout.hostGroup ?
      NTCURRENTTEB_ORIGINAL_REGION_SPEC.routes : [];
    const merged = buildMergedGroupWasm(sorted, {
      directCalls: opts.directCalls,
      tailCalls: opts.directCalls,
      yieldAwareTailCalls: opts.directCalls && !isMtInput,
      // MT uses guarded persistence fast-success stitches for resolved,
      // in-group next1/next2 arms. Cyclic arms additionally require the
      // per-CPU bounded mode; legacy helper fallbacks remain
      // byte-for-byte dispatcher exits.
      hotFastPathOnly: opts.directCalls && isMtInput,
      hotNext1: opts.directCalls && isMtInput,
      hotJump: opts.directCalls && isMtInput && opts.directCallKinds.has('jump'),
      boundedCyclicFastPaths: mtCyclicTailEnabled,
      runtime,
      directChainLimit: mtCyclicTailEnabled ? opts.mtCyclicLimit : 0,
      mtGenericIndirectLimit: mtGenericIndirectEnabled ? opts.mtGenericIndirectLimit : 0,
      mtIndirectRelocMapPlainLoad: mtGenericIndirectEnabled && opts.mtIndirectRelocMapPlainLoad,
      mtGenericDepthLocal: mtGenericIndirectEnabled && opts.mtGenericDepthLocal,
      mtGenericCallRef: mtGenericIndirectEnabled && opts.mtGenericCallRef,
      mtGenericLastSlotCache: mtGenericIndirectEnabled && opts.mtGenericLastSlotCache,
      mtGenericInlineFetch: mtGenericIndirectEnabled && opts.mtGenericInlineFetch,
      mtGenericTablePregrown: mtGenericIndirectEnabled && opts.mtGenericTablePregrown,
      mtGenericWorkerReadyBitmapOffset: mtGenericIndirectEnabled ?
        (opts.mtGenericWorkerReadyBitmapOffset || 0) : 0,
      directCallDepth: 0,
      edges: selectedEdges,
      internalClones,
      originalRegionRoutes,
      originalRegionMode: opts.mtOriginalRegionMode,
      threadedTailCensus: opts.threadedTailCensusHelperIndices ? {
        wrapper: opts.threadedTailCensusHelperIndices[0],
        typedBody: opts.threadedTailCensusHelperIndices[1],
        admitted: opts.threadedTailCensusHelperIndices[2],
        zeroRelocFallback: opts.threadedTailCensusHelperIndices[3],
      } : null,
    });
    const mergedWasm = merged.wasm;
    if ((internalCloneTargets.length || originalRegionContract) &&
        !WebAssembly.validate(mergedWasm)) {
      throw new Error(`Opt-in candidate grouped Wasm validation failed for ${groupPath}`);
    }
    if (internalClones.length) {
      const registered = new Map(sorted.map((entry) => [entry.key, entry]));
      for (const patch of merged.directCallPatches) {
        const target = registered.get(patch.targetKey);
        if (!target || !(target.relocCount > 0)) {
          throw new Error(
            `Internal-clone direct-call patch is not group-local registered reloc work: ` +
            `${groupPath} -> ${patch.targetKey}`);
        }
      }
    }
    if (originalRegionRoutes.length) {
      const registered = new Map(sorted.map((entry) => [entry.key, entry]));
      const regionPatches = merged.directCallPatches.filter((patch) =>
        patch.kind === 'original-region-target-reloc-base-v1');
      if (regionPatches.length !== originalRegionRoutes.length) {
        throw new Error(
          `Original-region publication patch count mismatch in ${groupPath}: ` +
          `${regionPatches.length}/${originalRegionRoutes.length}`);
      }
      for (const patch of regionPatches) {
        const target = registered.get(patch.targetKey);
        if (!target || patch.tokenOffsetWords !== (target.relocCount || 0) ||
            patch.zeroRelocTokenAbiVersion !== 1) {
          throw new Error(
            `Original-region publication patch is not exact group-local target work: ` +
            `${groupPath} -> ${patch.targetKey}`);
        }
      }
    }
    directCallsRewrittenByGroup[index] = merged.directCallsRewritten;
    guardedDirectCallsRewrittenByGroup[index] = merged.guardedDirectCallsRewritten;
    hotNext2RewrittenByGroup[index] = merged.hotNext2DirectCallsRewritten;
    hotNext2PatternMissesByGroup[index] = Math.max(
      0, hotNext2Eligible - merged.hotNext2DirectCallsRewritten);
    hotNext1RewrittenByGroup[index] = merged.hotNext1DirectCallsRewritten;
    hotNext1PatternMissesByGroup[index] = Math.max(
      0, hotNext1Eligible - merged.hotNext1DirectCallsRewritten);
    hotNext1TeeBaseByGroup[index] = merged.hotNext1TeeBaseDirectCallsRewritten;
    hotNext1DirectBaseByGroup[index] = merged.hotNext1DirectBaseDirectCallsRewritten;
    hotJumpRewrittenByGroup[index] = merged.hotJumpDirectCallsRewritten;
    hotJumpPatternMissesByGroup[index] = Math.max(
      0, hotJumpEligible - merged.hotJumpDirectCallsRewritten);
    hotJumpTeeBaseByGroup[index] = merged.hotJumpTeeBaseDirectCallsRewritten;
    hotJumpDirectBaseByGroup[index] = merged.hotJumpDirectBaseDirectCallsRewritten;
    fallbackDirectCallsRewrittenByGroup[index] = merged.fallbackDirectCallsRewritten;
    threadedStoreTailsSunkByGroup[index] = merged.threadedStoreTailsSunk;
    threadedStoreBytesSunkByGroup[index] = merged.threadedStoreBytesSunk;
    threadedStorePatternMissesByGroup[index] = merged.threadedStorePatternMisses;
    genericIndirectTailsByGroup[index] = merged.genericIndirectTailsRewritten;
    genericIndirectFallbackTailsByGroup[index] = merged.genericIndirectFallbackTailsRewritten;
    genericIndirectFastSuccessTailsByGroup[index] = merged.genericIndirectFastSuccessTailsRewritten;
    genericIndirectFastSuccessThreeLoadTailsByGroup[index] = merged.genericIndirectFastSuccessThreeLoadTailsRewritten;
    genericIndirectFastSuccessJumpTailsByGroup[index] = merged.genericIndirectFastSuccessJumpTailsRewritten;
    internalCloneDirectCallsByGroup[index] = merged.internalCloneDirectCallsRewritten;
    internalCloneFunctionsByGroup[index] = merged.internalCloneFunctions.length;
    internalCloneGenericIndirectTailsByGroup[index] =
      merged.internalCloneGenericIndirectTailsRewritten;
    const groupWasmBytes = mergedWasm.length;
    outEntries.push({
      name: groupPath,
      data: mergedWasm,
    });
    return {
      id: index,
      name: groupName,
      path: groupPath,
      wasmBytes: groupWasmBytes,
      directCallCandidateEdges: directEdgesByGroup[index],
      directCallsRewritten: directCallsRewrittenByGroup[index],
      guardedDirectCallsRewritten: guardedDirectCallsRewrittenByGroup[index],
      hotNext2Eligible,
      hotNext2NoReloc: hotNext2NoRelocByGroup[index],
      hotNext2Rewritten: hotNext2RewrittenByGroup[index],
      hotNext2PatternMisses: hotNext2PatternMissesByGroup[index],
      hotNext1Eligible,
      hotNext1NoReloc: hotNext1NoRelocByGroup[index],
      hotNext1Rewritten: hotNext1RewrittenByGroup[index],
      hotNext1PatternMisses: hotNext1PatternMissesByGroup[index],
      hotNext1TeeBaseRewritten: hotNext1TeeBaseByGroup[index],
      hotNext1DirectBaseRewritten: hotNext1DirectBaseByGroup[index],
      hotJumpEligible,
      hotJumpNoReloc: hotJumpNoRelocByGroup[index],
      hotJumpRewritten: hotJumpRewrittenByGroup[index],
      hotJumpPatternMisses: hotJumpPatternMissesByGroup[index],
      hotJumpTeeBaseRewritten: hotJumpTeeBaseByGroup[index],
      hotJumpDirectBaseRewritten: hotJumpDirectBaseByGroup[index],
      fallbackDirectCallsRewritten: fallbackDirectCallsRewrittenByGroup[index],
      threadedStoreTailsSunk: threadedStoreTailsSunkByGroup[index],
      threadedStoreBytesSunk: threadedStoreBytesSunkByGroup[index],
      threadedStorePatternMisses: threadedStorePatternMissesByGroup[index],
      threadedTailZeroRelocFallbacksInjected: merged.threadedTailZeroRelocFallbacksInjected,
      genericIndirectTailsRewritten: genericIndirectTailsByGroup[index],
      genericIndirectFallbackTailsRewritten: genericIndirectFallbackTailsByGroup[index],
      genericIndirectFastSuccessTailsRewritten: genericIndirectFastSuccessTailsByGroup[index],
      genericIndirectFastSuccessThreeLoadTailsRewritten: genericIndirectFastSuccessThreeLoadTailsByGroup[index],
      genericIndirectFastSuccessJumpTailsRewritten: genericIndirectFastSuccessJumpTailsByGroup[index],
      ...(internalClones.length ? {
        internalCloneDirectCallsRewritten: internalCloneDirectCallsByGroup[index],
        internalCloneGenericIndirectTailsRewritten:
          internalCloneGenericIndirectTailsByGroup[index],
        internalClones: merged.internalCloneFunctions,
      } : {}),
      ...(originalRegionRoutes.length ? {
        originalRegionMode: opts.mtOriginalRegionMode,
        originalRegionRouters: merged.originalRegionRouters,
        originalRegionRouteStats: merged.originalRegionRouteStats,
        originalRegionDispatcherSites: merged.originalRegionDispatcherSites,
        originalRegionStaticTargetSites: merged.originalRegionStaticTargetSites,
        originalRegionDynamicNormalTargetSites:
          merged.originalRegionDynamicNormalTargetSites,
        originalRegionNormalTargetSites: merged.originalRegionNormalTargetSites,
        originalRegionRedirectedSites: merged.originalRegionRedirectedSites,
        ...(opts.mtOriginalRegionMode === 'selective' ? {
          originalRegionRetainedDispatcherSites:
            merged.originalRegionRetainedDispatcherSites,
          originalRegionRouterResidualFallbackSites:
            merged.originalRegionRouterResidualFallbackSites,
        } : {}),
      } : {}),
      directCallPatches: merged.directCallPatches,
      threadedGpAbi: merged.threadedGpAbi,
      entries: sorted.map((entry) => ({
        key: entry.key,
        path: groupPath,
        exportName: entry.key,
        eip: hex32(entry.eip),
        blockHash: hex32(entry.blockHash),
        wasmBytes: entry.wasmBytes,
        opCount: entry.opCount,
        emulatedLen: entry.emulatedLen,
        relocCount: entry.relocCount || 0,
        typedExportName: merged.threadedGpAbi ? `t_${entry.key}` : undefined,
      })),
    };
  });

  if (mtCycleBreakForwardEnabled) {
    // Matcher failures only remove candidates, so proving the complete
    // unguarded candidate graph acyclic also proves its emitted tail subset.
    // A combined --mt-cyclic-limit build may additionally carry guarded
    // residual back/self edges; those are bounded dynamically instead.
    assertAcyclicEdges(selectedDirectEdges.filter((edge) => !edge.guarded));
  }

  const threadedGpAbiEnabled = manifestGroups.some((group) => group.threadedGpAbi);
  if (threadedGpAbiEnabled &&
      manifestGroups.some((group) => !group.threadedGpAbi)) {
    throw new Error('Mixed legacy/dual-entry groups are forbidden');
  }
  if (internalCloneTargets.length) {
    const clonedSources = new Set(manifestGroups.flatMap((group) =>
      (group.internalClones || []).map((clone) => clone.sourceKey)));
    for (const target of internalCloneTargets) {
      if (!clonedSources.has(target.key)) {
        throw new Error(`Internal clone target has no eligible incoming next1/next2 edge: ${target.key}`);
      }
    }
    for (const group of manifestGroups) {
      for (const clone of group.internalClones || []) {
        if (!(clone.incomingEdges > 0) || !(clone.directCallsRewritten > 0)) {
          throw new Error(
            `Internal clone did not produce a direct tail in caller group ${group.path}: ` +
            `${clone.sourceKey} incoming=${clone.incomingEdges} rewritten=${clone.directCallsRewritten}`);
        }
      }
    }
    const expected = opts.mtInternalCloneExpectedBaseStats;
    const directCallPatches = manifestGroups.reduce(
      (sum, group) => sum + group.directCallPatches.length, 0);
    const totalDirectCalls = directCallsRewrittenByGroup.reduce(
      (sum, count) => sum + count, 0);
    const cloneDirectCalls = internalCloneDirectCallsByGroup.reduce(
      (sum, count) => sum + count, 0);
    const actual = {
      entries: entries.length,
      groups: manifestGroups.length,
      directCallPatches,
      baseDirectCallsRewritten: totalDirectCalls - cloneDirectCalls,
    };
    for (const key of Object.keys(expected)) {
      if (actual[key] !== expected[key]) {
        throw new Error(
          `Internal clone base-stat mismatch for ${key}: ` +
          `got ${actual[key]}, expected ${expected[key]}`);
      }
    }
  }
  if (originalRegionContract) {
    const spec = NTCURRENTTEB_ORIGINAL_REGION_SPEC;
    const registrationRows = manifestGroups.flatMap((group) => group.entries);
    const regionGroups = manifestGroups.filter((group) =>
      (group.originalRegionRouters || []).length);
    if (manifestGroups.length !== spec.expected.groups ||
        registrationRows.length !== spec.expected.registeredEntries ||
        new Set(registrationRows.map((entry) => entry.key)).size !==
          spec.expected.registeredEntries ||
        regionGroups.length !== 1 || regionGroups[0].id !== spec.expected.hostGroup) {
      throw new Error(
        `Original-region registered layout mismatch: groups=${manifestGroups.length} ` +
        `entries=${registrationRows.length} regionGroups=${regionGroups.length}`);
    }
    const regionGroup = regionGroups[0];
    const regionKeys = new Set(regionGroup.entries.map((entry) => entry.key));
    for (const contract of spec.entries) {
      if (!regionKeys.has(contract.key)) {
        throw new Error(`Original-region key not co-located in host group: ${contract.key}`);
      }
    }
    for (const contract of spec.entries) {
      if (contract.originalGroup === spec.expected.hostGroup) continue;
      if (manifestGroups[contract.originalGroup].entries.some((entry) =>
          entry.key === contract.key)) {
        throw new Error(
          `Original-region moved key remained in old group ${contract.originalGroup}: ${contract.key}`);
      }
    }
    const routers = regionGroup.originalRegionRouters;
    const routeStats = regionGroup.originalRegionRouteStats;
    const redirected = regionGroup.originalRegionRedirectedSites;
    const expectedRedirected = opts.mtOriginalRegionMode === 'candidate' ?
      spec.expected.routedDispatcherSites :
      opts.mtOriginalRegionMode === 'selective' ?
        spec.expected.selectiveRedirectedSites : 0;
    if (routers.length !== spec.expected.routerFunctions ||
        routeStats.length !== spec.routes.length ||
        regionGroup.originalRegionDispatcherSites !==
          spec.expected.routedDispatcherSites ||
        regionGroup.originalRegionStaticTargetSites !==
          spec.expected.staticTargetSites ||
        regionGroup.originalRegionDynamicNormalTargetSites !==
          spec.expected.dynamicNormalTargetSites ||
        regionGroup.originalRegionNormalTargetSites !==
          spec.expected.normalTargetSites ||
        redirected !== expectedRedirected ||
        (opts.mtOriginalRegionMode === 'selective' &&
         (regionGroup.originalRegionRetainedDispatcherSites !==
            spec.expected.selectiveRetainedDispatcherSites ||
          regionGroup.originalRegionRouterResidualFallbackSites !==
            spec.expected.selectiveRouterResidualFallbackSites)) ||
        routers.some((router) => router.sourceKey === spec.terminalKey)) {
      throw new Error(
        `Original-region router/site mismatch: routers=${routers.length} ` +
        `sites=${regionGroup.originalRegionDispatcherSites} ` +
        `static=${regionGroup.originalRegionStaticTargetSites} ` +
        `dynamic=${regionGroup.originalRegionDynamicNormalTargetSites} ` +
        `normal=${regionGroup.originalRegionNormalTargetSites} redirected=${redirected}`);
    }
    const regionPatches = regionGroup.directCallPatches.filter((patch) =>
      patch.kind === 'original-region-target-reloc-base-v1');
    if (regionPatches.length !== spec.routes.length ||
        regionPatches.some((patch) => !regionKeys.has(patch.targetKey))) {
      throw new Error('Original-region publication patch target escaped the host group');
    }
    // Every ordinary direct-call patch is also regenerated from this final
    // layout. Prove no stale function/reloc target survived moving the ten
    // entries out of their old groups.
    for (const group of manifestGroups) {
      const local = new Set(group.entries.map((entry) => entry.key));
      for (const patch of group.directCallPatches) {
        if (!local.has(patch.targetKey)) {
          throw new Error(
            `Original-region layout left a dangling/non-local direct patch: ` +
            `${group.path} -> ${patch.targetKey}`);
        }
      }
    }
    const zeroRelocEntries = registrationRows.filter((entry) =>
      (entry.relocCount || 0) === 0).length;
    if (zeroRelocEntries !== 2965 ||
        registrationRows.length - zeroRelocEntries !== 27585) {
      throw new Error(
        `Original-region zero-reloc population mismatch: zero=${zeroRelocEntries} ` +
        `positive=${registrationRows.length - zeroRelocEntries}`);
    }
  }
  if (opts.threadedTailCensusHelperIndices) {
    // This diagnostic is tied to the already rejected R2 shape. Do not emit
    // a cache if source drift changes its static population: it would no
    // longer answer the 5,299 excluded operands versus wrapper question.
    if (!threadedGpAbiEnabled) {
      throw new Error('Threaded-tail census requires the dual-entry ABI');
    }
    const wrapperSites = entries.length;
    const admittedSites = manifestGroups.reduce(
      (sum, group) => sum + group.threadedStoreTailsSunk, 0);
    const zeroRelocSites = manifestGroups.reduce(
      (sum, group) => sum + group.threadedTailZeroRelocFallbacksInjected, 0);
    const directPatches = manifestGroups.reduce(
      (sum, group) => sum + group.directCallPatches.length, 0);
    if (wrapperSites !== 34322 || admittedSites !== 2715 ||
        zeroRelocSites !== 757 || directPatches !== admittedSites * 8 ||
        admittedSites + zeroRelocSites !== 3472) {
      throw new Error(
        `Threaded-tail census static population mismatch: wrappers=${wrapperSites}, ` +
        `admitted=${admittedSites}, zeroReloc=${zeroRelocSites}, patches=${directPatches}`);
    }
  }
  const groupedManifest = {
    version: threadedGpAbiEnabled ? 3 : 2,
    format: 'boxedwine-wasm-jit-grouped-cache',
    cacheVersion: 'v5',
    mt: isMtInput,
    source: {
      inputZip: opts.inputZip,
      inputManifestVersion: manifest.version,
      optimized: true,
      preset: 'local',
      budgetBytes: opts.budget,
      mergedGroups: true,
      runtimeIdentity: originalRegionRuntimeIdentity || mrgRuntimeIdentity || parityCurrentRuntimeIdentity || int9aSpecializedRuntimeIdentity || runtimeLogicComposite32Identity || undefined,
      directCalls: opts.directCalls,
      directCallMode: opts.directCalls ? (isMtInput ?
        (mtCyclicTailEnabled ? `mt-hot-next12-fast-success-${mtCycleBreakForwardEnabled ? 'cycle-break-forward-' : ''}cyclic${opts.mtCyclicLimit}` :
          (mtCycleBreakForwardEnabled ? 'mt-hot-next12-fast-success-cycle-break-forward' : 'mt-hot-next12-fast-success')) :
        'legacy-fallback') : 'none',
      tailCalls: opts.directCalls,
      yieldAwareTailCalls: opts.directCalls && !isMtInput,
      directCallDepth: 0,
      mtCyclicLimit: mtCyclicTailEnabled ? opts.mtCyclicLimit : 0,
      mtCycleBreakForward: mtCycleBreakForwardEnabled,
      mtGenericIndirectLimit: mtGenericIndirectEnabled ? opts.mtGenericIndirectLimit : 0,
      ...(internalCloneTargets.length ? {
        internalCloneOnly: {
          kind: 'exact-zero-reloc-dynamic-return-caller-group-clone-v1',
          rawManifestSha256,
          targetKeys: internalCloneTargets.map((entry) => entry.key),
          targetShape: {
            opCount: 2,
            emulatedLen: 4,
            relocCount: 0,
            profiledExitCounts: 0,
          },
          cloneVisibility: 'internal-no-export-no-registration',
          incomingKinds: ['next1', 'next2'],
          callerAdmission: 'exact-next1-next2-fast-success-shape-only',
          liveGuards: ['target-pfn-nonzero', 'cpu-bailout-zero', 'cpu-yield-zero'],
          trustBoundary:
            'operator-asserted-exact-key-hash-eip-and-immutable-guest-text',
          expectedBaseStats: opts.mtInternalCloneExpectedBaseStats,
        },
      } : {}),
      ...(originalRegionContract ? {
        originalRegionRouter: {
          kind: NTCURRENTTEB_ORIGINAL_REGION_SPEC.kind,
          mode: opts.mtOriginalRegionMode,
          rawManifestSha256,
          runtimeStem: NTCURRENTTEB_ORIGINAL_REGION_SPEC.runtimeStem,
          requiredZeroRelocPublicationTokenAbiVersion: 1,
          hostKey: NTCURRENTTEB_ORIGINAL_REGION_SPEC.hostKey,
          terminalKey: NTCURRENTTEB_ORIGINAL_REGION_SPEC.terminalKey,
          targetKeys: NTCURRENTTEB_ORIGINAL_REGION_SPEC.entries.map((entry) => ({
            key: entry.key,
            relocCount: entry.relocCount,
            originalGroup: entry.originalGroup,
          })),
          routes: NTCURRENTTEB_ORIGINAL_REGION_SPEC.routes.map((route) => ({...route})),
          layout: originalRegionLayout,
          registration: 'original-export-only-no-clones',
          routerVisibility: 'internal-no-export-no-registration',
          routerCall: 'group-local-return_call-original-entry',
          fallback: 'existing-generic-indirect-dispatcher',
          liveGuards: [
            'actual-linear-eip-equals-target',
            'cpu-nextOp-nonzero',
            'target-publication-token-equals-nextOp-pfnJitCode',
            'positive-reloc-word0-equals-nextOp',
            'cpu-yield-zero',
            'cpu-bailout-zero',
            'cpu-direct-chain-depth-below-generic-limit',
          ],
          publicationPatch: {
            kind: 'original-region-target-reloc-base-v1',
            tokenOffsetWords: 'target-relocCount',
            zeroRelocTokenAbiVersion: 1,
          },
          routeGraph: {
            acyclic: true,
            maxDepth: originalRegionContract.maxRouteDepth,
            dynamicDepthOffset: runtime.cpu.directChainDepthOffset,
            dynamicDepthLimit: opts.mtGenericIndirectLimit,
            takenEdgeDepthIncrement: 1,
            terminalEpilogueRoute: false,
          },
          staticSites: {
            routedDispatcher: originalRegionContract.dispatcherSites,
            staticTarget: originalRegionContract.staticTargetSites,
            dynamicNormalTarget: originalRegionContract.dynamicNormalTargetSites,
            normalTarget: originalRegionContract.normalTargetSites,
            residualFallback: opts.mtOriginalRegionMode === 'selective' ?
              originalRegionContract.selectiveRouterResidualFallbackSites :
              originalRegionContract.residualFallbackSites,
          },
          ...(opts.mtOriginalRegionMode === 'selective' ? {
            selectivePolicy: {
              staticSources:
                'redirect-only-findStaticEipStoreBefore-equals-targetEip',
              dynamicNormalSources:
                'redirect-all-two-sites-runtime-linear-eip-guard',
              redirectedSites: originalRegionContract.selectiveRedirectedSites,
              retainedDispatcherSites:
                originalRegionContract.selectiveRetainedDispatcherSites,
              routerNormalSites: originalRegionContract.normalTargetSites,
              routerResidualFallbackSites:
                originalRegionContract.selectiveRouterResidualFallbackSites,
            },
          } : {}),
        },
      } : {}),
      runtimeCpu: mtCyclicTailEnabled ? {
        directChainDepthOffset: runtime.cpu.directChainDepthOffset,
        bailoutOffset: runtime.cpu.bailoutOffset,
      } : undefined,
      // MRG legacy-v2 retains one-token publication even with direct calls
      // disabled: it is still registered through the MT group-entry API, and
      // the page requires this explicit ABI statement before any registration.
      runtimePublication: isMtInput &&
          (opts.directCalls || threadedGpAbiEnabled || mrgRuntimeIdentity !== null || parityCurrentRuntimeIdentity !== null || int9aSpecializedRuntimeIdentity !== null) ? {
        // MRG deliberately has no direct tail rewrite, so its one-token
        // publication contract does not dereference DecodedOp::pfnJitCode.
        // Keep a numeric field for the shared manifest shape without asking a
        // legacy raw record to prove an unused direct-publication offset.
        pfnJitCodeOffset: mrgRuntimeIdentity !== null ? 0 : runtime.decodedOp.pfnJitCodeOffset,
        tokenWords: threadedGpAbiEnabled ? 2 : 1,
        zeroRelocTokenAbiVersion: originalRegionContract ? 1 : undefined,
      } : undefined,
      runtimeRegisterThreading: threadedGpAbiEnabled ? {
        abiVersion: 1,
        publicationTokenWords: 2,
        wrapperSignature: 'vii',
        typedSignature: 'iiiiiiiiii_iiiiiiii',
      } : undefined,
      // Store-sunk typed tails write the live target identity directly into
      // CPU::nextOp. Keep that ABI in the manifest independently from the
      // generic-indirect feature so the page rejects a stale runtime before
      // it can register any pair-replay module.
      runtimeThreadedTail: threadedGpAbiEnabled && opts.directCalls ? {
        nextOpOffset: runtime.cpu.nextOpOffset,
        pfnJitCodeOffset: runtime.decodedOp.pfnJitCodeOffset,
        publicationTokenWords: 2,
      } : undefined,
      threadedTailCensus: opts.threadedTailCensusHelperIndices ? {
        kind: 'r2-threaded-tail-coverage-census-v1',
        abiVersion: 1,
        snapshotBytes: 32,
        helperIndices: opts.threadedTailCensusHelperIndices,
        staticSites: {
          wrapper: entries.length,
          admitted: 2715,
          zeroRelocFallback: 757,
        },
        semantics: 'exact-host-helper-invocations-since-process-start',
      } : undefined,
      runtimeLazyZNZ: opts.runtimeLazyZNZ ? {
        kind: 'cpu-runtime-lazy-znz-v1',
        abiVersion: 1,
        cpuFlagsOffset: runtime.cpu.flagsOffset,
        cpuResultOffset: runtime.cpu.resultOffset,
        cpuLazyFlagTypeOffset: runtime.cpu.lazyFlagTypeOffset,
        cpuLazyFlagTypePrevOffset: runtime.cpu.lazyFlagTypePrevOffset,
        zfMask: 64,
        noneType: 0,
        cfofType: 50,
        resultTypeRange: [1, 49],
        fallback: 'helpers.cond-z-nz',
        runtimeLazyZNZCounters: opts.runtimeLazyZNZCounters ? {
          kind: 'cpu-runtime-lazy-znz-helper-counters-v1',
          abiVersion: 1,
          snapshotBytes: 16,
          helperIndices: [16, 17],
          semantics: 'exact-host-helper-invocations-since-process-start',
        } : undefined,
      } : undefined,
      runtimeLogicComposite32: opts.runtimeLogicComposite32 ? {
        kind: 'cpu-runtime-logical-composite32-v1',
        abiVersion: 1,
        cpuResultOffset: runtime.cpu.resultOffset,
        cpuLazyFlagTypeOffset: runtime.cpu.lazyFlagTypeOffset,
        logicalTypes: [6, 15, 21, 42],
        conditions: [6, 7, 12, 13, 14, 15],
        compileTimeState: 'FLAGS_NULL_ONLY',
        fallback: 'helpers.cond-composite',
      } : undefined,
      threadedGpAbi: threadedGpAbiEnabled ? {
        version: 1,
        gpCount: 8,
        wrapperParams: 2,
        internalParams: 10,
        internalResults: 8,
        wrapperExport: 'entry-key',
        internalExportPrefix: 't_',
        // Every rewritten threaded edge is admitted only after the exact
        // contiguous eax..edi terminal writeback was removed. Non-matching
        // exits remain byte-for-byte helper fallbacks.
        storeSinking: true,
        chainLimit: THREADED_GP_CHAIN_LIMIT,
        boundaryGuards: ['bailout', 'yield', 'depth'],
      } : undefined,
      runtimeGenericIndirect: mtGenericIndirectEnabled ? {
        nextOpOffset: runtime.cpu.nextOpOffset,
        directChainDepthOffset: runtime.cpu.directChainDepthOffset,
        bailoutOffset: runtime.cpu.bailoutOffset,
        pfnJitCodeOffset: runtime.decodedOp.pfnJitCodeOffset,
        relocMapBase: runtime.jit.indirectRelocMapBase,
        relocMapCapacity: runtime.jit.indirectRelocMapCapacity,
        relocMapLoad: opts.mtIndirectRelocMapPlainLoad ? 'plain-after-pfn-acquire' : 'atomic',
        depthLoad: opts.mtGenericDepthLocal ? 'snapshot-local' : 'double-memory',
        dispatchCall: opts.mtGenericCallRef ? 'typed-ref-v1' : undefined,
        readinessCache: opts.mtGenericLastSlotCache ? 'instance-last-slot-v1' : undefined,
        inlineFetch: opts.mtGenericInlineFetch ? {
          kind: 'cpu-opcache-hit-v1',
          abiVersion: 2,
          cpuThreadOffset: runtime.cpu.threadOffset,
          kthreadTerminatingOffset: runtime.cpu.kthreadTerminatingOffset,
          kthreadProcessOffset: runtime.cpu.kthreadProcessOffset,
          cpuEipOffset: runtime.cpu.eipOffset,
          cpuCsAddressOffset: runtime.cpu.csAddressOffset,
          cpuBigOffset: runtime.cpu.bigOffset,
          cpuOpCacheOffset: runtime.cpu.opCacheOffset,
          pointerBytes: 4,
          pageShift: 12,
          levelBits: [10, 10],
          pageOpsOffset: 0,
          missHelper: 'helpers.fn_6',
        } : undefined,
        tableBounds: opts.mtGenericTablePregrown ? 'runtime-pregrown' : 'dispatcher-checked',
        slotVisibility: opts.mtGenericWorkerReadyBitmapOffset !== null ?
          'worker-byte-map-v1' : 'table-ref-v1',
        workerReadyBitmapOffset: opts.mtGenericWorkerReadyBitmapOffset !== null ?
          runtime.cpu.workerReadyBitmapOffset : undefined,
        workerReadyBitmapCapacity: opts.mtGenericWorkerReadyBitmapOffset !== null ?
          runtime.jit.indirectRelocMapCapacity : undefined,
        tableImport: 'env.table',
      } : undefined,
      interiorProfile: opts.interiorProfile,
      directCallKinds: opts.directCalls ? [...opts.directCallKinds] : [],
      profileSplitThreshold: opts.profileSplitThreshold,
      profileSplitLimit: opts.profileSplitLimit,
    },
    stats: {
      modules: entries.length,
      groups: groups.length,
      components: components.length,
      wasmBytesBefore: wasmBefore,
      wasmBytesAfter: wasmAfter,
      mergedWasmBytes: manifestGroups.reduce((sum, group) => sum + group.wasmBytes, 0),
      graphEdges: {
        total: graph.edges.length,
        resolved: resolvedEdges,
        unresolved: unresolvedEdges,
        ambiguous: ambiguousEdges,
        interior: interiorEdges,
        ambiguousInterior: ambiguousInteriorEdges,
        inGroup: inGroupEdges,
      },
      directCalls: {
        candidateEdges: inGroupEdges,
        rewritten: directCallsRewrittenByGroup.reduce((sum, count) => sum + count, 0),
        guardedRewritten: guardedDirectCallsRewrittenByGroup.reduce((sum, count) => sum + count, 0),
        mode: opts.directCalls ? (isMtInput ?
          (mtCyclicTailEnabled ? `mt-hot-next12-fast-success-${mtCycleBreakForwardEnabled ? 'cycle-break-forward-' : ''}cyclic${opts.mtCyclicLimit}` :
            (mtCycleBreakForwardEnabled ? 'mt-hot-next12-fast-success-cycle-break-forward' : 'mt-hot-next12-fast-success')) :
          'legacy-fallback') : 'none',
        hotNext2Eligible: hotNext2EligibleByGroup.reduce((sum, count) => sum + count, 0),
        hotNext2NoReloc: hotNext2NoRelocByGroup.reduce((sum, count) => sum + count, 0),
        hotNext2Rewritten: hotNext2RewrittenByGroup.reduce((sum, count) => sum + count, 0),
        hotNext2PatternMisses: hotNext2PatternMissesByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1Eligible: hotNext1EligibleByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1NoReloc: hotNext1NoRelocByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1Rewritten: hotNext1RewrittenByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1PatternMisses: hotNext1PatternMissesByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1TeeBaseRewritten: hotNext1TeeBaseByGroup.reduce((sum, count) => sum + count, 0),
        hotNext1DirectBaseRewritten: hotNext1DirectBaseByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpEligible: hotJumpEligibleByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpNoReloc: hotJumpNoRelocByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpRewritten: hotJumpRewrittenByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpPatternMisses: hotJumpPatternMissesByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpTeeBaseRewritten: hotJumpTeeBaseByGroup.reduce((sum, count) => sum + count, 0),
        hotJumpDirectBaseRewritten: hotJumpDirectBaseByGroup.reduce((sum, count) => sum + count, 0),
        fallbackRewritten: fallbackDirectCallsRewrittenByGroup.reduce((sum, count) => sum + count, 0),
        threadedStoreTailsSunk: threadedStoreTailsSunkByGroup.reduce((sum, count) => sum + count, 0),
        threadedStoreBytesSunk: threadedStoreBytesSunkByGroup.reduce((sum, count) => sum + count, 0),
        threadedStorePatternMisses: threadedStorePatternMissesByGroup.reduce((sum, count) => sum + count, 0),
        genericIndirectTailsRewritten: genericIndirectTailsByGroup.reduce((sum, count) => sum + count, 0),
        genericIndirectFallbackTailsRewritten: genericIndirectFallbackTailsByGroup.reduce((sum, count) => sum + count, 0),
        genericIndirectFastSuccessTailsRewritten: genericIndirectFastSuccessTailsByGroup.reduce((sum, count) => sum + count, 0),
        genericIndirectFastSuccessThreeLoadTailsRewritten: genericIndirectFastSuccessThreeLoadTailsByGroup.reduce((sum, count) => sum + count, 0),
        genericIndirectFastSuccessJumpTailsRewritten: genericIndirectFastSuccessJumpTailsByGroup.reduce((sum, count) => sum + count, 0),
        tailCalls: opts.directCalls,
        yieldAwareTailCalls: opts.directCalls && !isMtInput,
        cyclicBlocksPresent: cyclicKeys.size,
        cyclicBlocksExcluded: mtCyclicTailEnabled ? 0 : cyclicKeys.size,
        cycleBreakEdgesAdmitted,
        depthLimit: mtCyclicTailEnabled ? opts.mtCyclicLimit : 0,
        ...(internalCloneTargets.length ? {
          internalCloneTargetKeys: internalCloneTargets.length,
          internalCloneCandidateEdges: internalCloneCandidateEdges.length,
          internalCloneCrossGroupEdges,
          internalClonePatternRejectedEdges,
          internalClonePatternRejectedGroups,
          internalCloneAdmittedEdges:
            internalCloneCandidateEdges.length - internalClonePatternRejectedEdges,
          internalCloneFunctions: internalCloneFunctionsByGroup.reduce(
            (sum, count) => sum + count, 0),
          internalCloneDirectCallsRewritten: internalCloneDirectCallsByGroup.reduce(
            (sum, count) => sum + count, 0),
          internalCloneGenericIndirectTailsRewritten:
            internalCloneGenericIndirectTailsByGroup.reduce((sum, count) => sum + count, 0),
        } : {}),
        ...(originalRegionContract ? {
          originalRegionMode: opts.mtOriginalRegionMode,
          originalRegionRouterFunctions:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionRouters || []).length, 0),
          originalRegionDispatcherSites:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionDispatcherSites || 0), 0),
          originalRegionStaticTargetSites:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionStaticTargetSites || 0), 0),
          originalRegionDynamicNormalTargetSites:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionDynamicNormalTargetSites || 0), 0),
          originalRegionNormalTargetSites:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionNormalTargetSites || 0), 0),
          originalRegionResidualFallbackSites:
            opts.mtOriginalRegionMode === 'selective' ?
              NTCURRENTTEB_ORIGINAL_REGION_SPEC.expected
                .selectiveRouterResidualFallbackSites :
              NTCURRENTTEB_ORIGINAL_REGION_SPEC.expected.residualFallbackSites,
          originalRegionRedirectedSites:
            manifestGroups.reduce((sum, group) =>
              sum + (group.originalRegionRedirectedSites || 0), 0),
          originalRegionPublicationPatches:
            manifestGroups.reduce((sum, group) =>
              sum + group.directCallPatches.filter((patch) =>
                patch.kind === 'original-region-target-reloc-base-v1').length, 0),
          ...(opts.mtOriginalRegionMode === 'selective' ? {
            originalRegionRetainedDispatcherSites:
              manifestGroups.reduce((sum, group) =>
                sum + (group.originalRegionRetainedDispatcherSites || 0), 0),
            originalRegionRouterResidualFallbackSites:
              manifestGroups.reduce((sum, group) =>
                sum + (group.originalRegionRouterResidualFallbackSites || 0), 0),
          } : {}),
        } : {}),
      },
      reloc: {
        entriesWithSlots: manifestGroups.reduce((sum, group) =>
          sum + group.entries.filter((entry) => (entry.relocCount || 0) > 0).length, 0),
        directCallSitePatches: manifestGroups.reduce((sum, group) =>
          sum + group.directCallPatches.length, 0),
      },
      imports: {
        helperFieldsPerModule: shape.helperFields.size,
        functionImportsBefore: shape.totalFunctions * entries.length,
        functionImportsAfterGroupedEstimate: shape.totalFunctions * groups.length,
        memoryImportsBefore: shape.memories * entries.length,
        memoryImportsAfterGroupedEstimate: shape.memories * groups.length,
      },
    },
    profileGuidedSplits,
    groups: manifestGroups,
  };
  outEntries.push({
    name: GROUPED_MANIFEST,
    data: Buffer.from(`${JSON.stringify(groupedManifest, null, 2)}\n`, 'utf8'),
  });

  const out = buildZip(outEntries, opts.compressionLevel);
  await writeFile(opts.outputZip, out);

  console.log(`Input:              ${opts.inputZip}`);
  console.log(`Output:             ${opts.outputZip}`);
  console.log(`Optimized:          ${optimized}/${entries.length}`);
  console.log(`Budget:             ${formatBytes(opts.budget)}`);
  console.log(`Modules:            ${entries.length}`);
  console.log(`Groups:             ${groups.length}`);
  console.log(`Merged groups:      yes`);
  const directStats = groupedManifest.stats.directCalls;
  console.log(`Direct calls:       ${directStats.rewritten} tail sites rewritten, candidateEdges=${directStats.candidateEdges}, kinds=${groupedManifest.source.directCallKinds.join(',') || 'none'}, tailCalls=${directStats.tailCalls ? 'yes' : 'no'}, yieldAware=${directStats.yieldAwareTailCalls ? 'yes' : 'no'}, guarded=${directStats.guardedRewritten}, cyclicBlocks=${directStats.cyclicBlocksExcluded}`);
  if (isMtInput && opts.directCalls) {
    console.log(`Hot next2 stitch:   ${directStats.hotNext2Rewritten}/${directStats.hotNext2Eligible} eligible fast-success arms rewritten, patternMisses=${directStats.hotNext2PatternMisses}, targetNoReloc=${directStats.hotNext2NoReloc}, fallbackRewritten=${directStats.fallbackRewritten}`);
    console.log(`Hot next1 stitch:   ${directStats.hotNext1Rewritten}/${directStats.hotNext1Eligible} eligible fast-success arms rewritten, patternMisses=${directStats.hotNext1PatternMisses}, targetNoReloc=${directStats.hotNext1NoReloc}, shapes[teeBase=${directStats.hotNext1TeeBaseRewritten},directBase=${directStats.hotNext1DirectBaseRewritten}]`);
    if (opts.directCallKinds.has('jump')) {
      console.log(`Hot jump stitch:    ${directStats.hotJumpRewritten}/${directStats.hotJumpEligible} eligible exact JumpIfCondition arms rewritten, patternMisses=${directStats.hotJumpPatternMisses}, targetNoReloc=${directStats.hotJumpNoReloc}, shapes[teeBase=${directStats.hotJumpTeeBaseRewritten},directBase=${directStats.hotJumpDirectBaseRewritten}]`);
    }
    console.log(`MT publication:     pfnJitCodeOffset=${runtime.decodedOp.pfnJitCodeOffset}, tokenWords=${threadedGpAbiEnabled ? 2 : 1}`);
    if (mtCyclicTailEnabled) {
      console.log(`MT cyclic bound:    limit=${opts.mtCyclicLimit}, guardedRewritten=${directStats.guardedRewritten}, depthOffset=${runtime.cpu.directChainDepthOffset}, bailoutOffset=${runtime.cpu.bailoutOffset}`);
    }
    if (mtCycleBreakForwardEnabled) {
      console.log(`MT cycle break:     admitted=${directStats.cycleBreakEdgesAdmitted} unguarded cross-SCC/forward edges; residual back/self edges=${mtCyclicTailEnabled ? 'bounded' : 'dispatcher'}`);
    }
    if (mtGenericIndirectEnabled) {
      console.log(`MT generic indirect: ${directStats.genericIndirectTailsRewritten} common-dispatch tails rewritten [fallback=${directStats.genericIndirectFallbackTailsRewritten}, fast=${directStats.genericIndirectFastSuccessTailsRewritten}, threeLoad=${directStats.genericIndirectFastSuccessThreeLoadTailsRewritten}, jump=${directStats.genericIndirectFastSuccessJumpTailsRewritten}], dispatchers=${groups.length}, limit=${opts.mtGenericIndirectLimit}, nextOpOffset=${runtime.cpu.nextOpOffset}, relocMap=${runtime.jit.indirectRelocMapBase}/${runtime.jit.indirectRelocMapCapacity}`);
    }
    if (internalCloneTargets.length) {
      console.log(`MT internal clones: ${directStats.internalCloneDirectCallsRewritten}/${directStats.internalCloneAdmittedEdges} admitted incoming tails rewritten, targets=${directStats.internalCloneTargetKeys}, functions=${directStats.internalCloneFunctions}, rawCandidates=${directStats.internalCloneCandidateEdges}, patternRejected=${directStats.internalClonePatternRejectedEdges}/${directStats.internalClonePatternRejectedGroups}groups, crossGroup=${directStats.internalCloneCrossGroupEdges}, cloneGeneric=${directStats.internalCloneGenericIndirectTailsRewritten}`);
    }
    if (originalRegionContract) {
      console.log(`MT original region: mode=${directStats.originalRegionMode}, routers=${directStats.originalRegionRouterFunctions}, dispatcherSites=${directStats.originalRegionDispatcherSites}, staticTargetSites=${directStats.originalRegionStaticTargetSites}, dynamicTargetSites=${directStats.originalRegionDynamicNormalTargetSites}, normalTargetSites=${directStats.originalRegionNormalTargetSites}, residual=${directStats.originalRegionResidualFallbackSites}, redirected=${directStats.originalRegionRedirectedSites}, publicationPatches=${directStats.originalRegionPublicationPatches}, hostGroup=${originalRegionLayout.hostGroup}, bytes=${originalRegionLayout.hostGroupBytesAfter}/${originalRegionLayout.budgetBytes}`);
    }
  }
  const relocStats = groupedManifest.stats.reloc;
  console.log(`Reloc:              ${relocStats.entriesWithSlots} entries with slot arrays, ${relocStats.directCallSitePatches} direct-call sites patched with target arrays`);
  console.log(`Components:         ${components.length}`);
  console.log(`Wasm bytes:         ${formatBytes(wasmBefore)} -> ${formatBytes(wasmAfter)} (${pct(wasmAfter - wasmBefore, wasmBefore)}%)`);
  console.log(`Graph edges:        ${inGroupEdges}/${resolvedEdges} resolved intra-group; unresolved=${unresolvedEdges} interior=${interiorEdges} ambiguousInterior=${ambiguousInteriorEdges}`);
  console.log(`Profile splits:     ${profileGuidedSplits.targets.length}` +
              (profileGuidedSplits.source ? ` selected from ${profileGuidedSplits.parsed || 0} profile pairs, belowThreshold=${profileGuidedSplits.belowThreshold || 0}, rejected=${profileGuidedSplits.rejected || 0}` : ' (no profile)'));
  if (profileGuidedSplits.rejected) {
    const rejectedByReason = profileGuidedSplits.rejectedByReason || {};
    const reasonText = Object.entries(rejectedByReason)
      .filter(([, count]) => count)
      .map(([reason, count]) => `${reason}=${count}`)
      .join(' ');
    if (reasonText) console.log(`Profile split rejects: ${reasonText}`);
    for (const sample of (profileGuidedSplits.rejectedSamples || [])) {
      const candidates = (sample.candidates || [])
        .map((entry) => `${hex32(entry.start)}-${hex32(entry.end)} ${entry.key}`)
        .join('; ');
      console.log(`  rejected ${hex32(sample.blockStart)} -> ${hex32(sample.target)} count=${sample.exactCount} reason=${sample.reason}${candidates ? ` candidates[${candidates}]` : ''}`);
    }
  }
  for (const split of profileGuidedSplits.targets.slice(0, 8)) {
    console.log(`  ${hex32(split.blockStart)} -> ${hex32(split.target)} count=${split.exactCount} key=${split.containingKey}`);
  }
  console.log(`Function imports:   ${groupedManifest.stats.imports.functionImportsBefore} -> ${groupedManifest.stats.imports.functionImportsAfterGroupedEstimate} estimated`);
  console.log(`Memory imports:     ${groupedManifest.stats.imports.memoryImportsBefore} -> ${groupedManifest.stats.imports.memoryImportsAfterGroupedEstimate} estimated`);
  console.log(`Zip bytes:          ${formatBytes(inputCache.zipBuf.length)} -> ${formatBytes(out.length)}`);
  for (const warning of gWarnings) {
    console.log(`Warning:            ${warning}`);
  }
}

if (process.argv[1] &&
    pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  main().catch((err) => {
    console.error(`error: ${err.message}`);
    process.exit(1);
  });
}

export {
  rewriteOriginalRegionDispatcherCalls,
};
