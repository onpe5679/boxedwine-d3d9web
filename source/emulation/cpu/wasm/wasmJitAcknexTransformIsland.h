/*
 *  Copyright (C) 2012-2026 The BoxedWine Team
 *
 *  Experimental, content-bound Acknex transform-island HLE.  This surface is
 *  deliberately absent from ordinary runtimes; the dedicated build target
 *  defines BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND.
 */

#ifndef __WASM_JIT_ACKNEX_TRANSFORM_ISLAND_H__
#define __WASM_JIT_ACKNEX_TRANSFORM_ISLAND_H__

#if defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND)

class CPU;
class KMemory;

enum class WasmJitAcknexTransformCounter : U32 {
    CandidateQueries = 0,
    CandidateAccepted,
    CandidateWrongMapping,
    CandidateContentMismatch,
    CandidateBlockHashMismatch,
    HelperEntries,
    HelperSuccess,
    FallbackIdentity,
    FallbackCodeBytes,
    FallbackLiveTarget,
    FallbackMxcsr,
    FallbackSegment,
    FallbackStack,
    FallbackAlias,
    FallbackUnaligned,
    FallbackMemory,
    FallbackNegativeIndex,
    FallbackNullMatrix,
    FallbackType,
    Transform3x3Runs,
    TransformCoordRuns,
    // ABI v2 diagnostics.  Component counters count full validation failures,
    // not hits served by the fail-closed negative cache.
    CodeMismatchEntry,
    CodeMismatchThunkCoord,
    CodeMismatchThunk3x3,
    CodeMismatchTransform3x3,
    CodeMismatchTransformCoord,
    FallbackCodeNegativeCached,
    // The following ten U64 words are a single first-failure record.  State
    // is 0=empty, 1=writer active, 2=complete.  They intentionally share the
    // snapshot ABI so a browser can retrieve one coherent read-only record.
    FirstMismatchState,
    FirstMismatchComponent,
    FirstMismatchOffset,
    FirstMismatchAddress,
    FirstMismatchExpected,
    FirstMismatchActual,
    FirstMismatchBase,
    FirstMismatchEntry,
    FirstMismatchProcessId,
    FirstMismatchMappingGeneration,
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    // ABI v3 diagnostics.  These are appended only in the isolated outer-loop
    // runtime; the ABI2 whole-FUN_10046760 runtime retains its exact counter
    // layout when the outer-loop macro is absent.
    OuterCandidateQueries,
    OuterCandidateAccepted,
    OuterHelperEntries,
    OuterSafeFallback,
    OuterPartialResume,
    OuterStaleCodeExit,
    OuterCompletedLoops,
    OuterChunks,
    OuterElements,
    OuterChunkLimitStops,
    OuterYieldStops,
    OuterBailoutStops,
    OuterTerminationStops,
    OuterEpochStops,
#endif
    Count
};

static constexpr U32 WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT =
    (U32)WasmJitAcknexTransformCounter::Count;

// Compile-time admission.  The caller still has to verify the exact decoded
// block hash before emitting the helper arm.
bool wasmJitAcknexTransformIslandIsCandidate(CPU* cpu, U32 entryAddress);
void wasmJitAcknexTransformIslandRecordBlockHashMismatch();
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
bool wasmJitAcknexTransformIslandIsOuterEntry(CPU* cpu, U32 entryAddress);
#endif

// Imported by generated WASM.  tmpReg is 1 only after a complete successful
// replacement; on every rejection it is 0 and architectural state is left for
// the original generated guest body.
void wasmJitAcknexTransformIslandHelper(CPU* cpu);

#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
enum class WasmJitAcknexOuterOutcome : U32 {
    SafeFallback = 0,
    PartialResume = 1,
    StaleCodeExit = 2,
};
static constexpr U32 WASM_JIT_ACKNEX_OUTER_CHUNK_ELEMENTS = 64;
#endif

extern "C" U32 wasm_jit_get_acknex_transform_island_abi_version();
extern "C" U32 wasm_jit_get_acknex_transform_island_counter_count();
extern "C" U32 wasm_jit_get_acknex_transform_island_snapshot_bytes();
extern "C" void wasm_jit_copy_acknex_transform_island_snapshot(U64* output, U32 outputBytes);
extern "C" void wasm_jit_reset_acknex_transform_island_counters();

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
void wasmJitAcknexTransformIslandFixtureSetIdentity(bool valid);
void wasmJitAcknexTransformIslandFixtureSetModuleBase(U32 moduleBase);
void wasmJitAcknexTransformIslandFixtureInstallCode(KMemory* memory);
bool wasmJitAcknexTransformIslandRunForTest(CPU* cpu);
bool wasmJitAcknexTransformIslandSha256SelfTest();
#endif

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE)
enum class WasmJitAcknexOuterFixtureStop : U32 {
    None = 0,
    Yield,
    Bailout,
    MappingEpoch,
    Termination,
};
U32 wasmJitAcknexOuterLoopRunForTest(CPU* cpu);
void wasmJitAcknexOuterLoopFixtureSetStop(U32 completedElements,
    WasmJitAcknexOuterFixtureStop stop);
#endif

#endif

#endif
