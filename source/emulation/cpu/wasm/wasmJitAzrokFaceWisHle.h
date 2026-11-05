/*
 * Experimental, exact-content Azrok HP_*.wis loader bypass.  The helper is
 * absent unless BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE is selected together
 * with the isolated two-boundary grouped cache.
 */

#ifndef __WASM_JIT_AZROK_FACE_WIS_HLE_H__
#define __WASM_JIT_AZROK_FACE_WIS_HLE_H__

#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE)

class CPU;
class KProcess;

enum class WasmJitAzrokFaceWisCounter : U32 {
    HelperEntries = 0,
    LookupEntries,
    LookupHits,
    LookupMisses,
    LookupProbeMisses,
    CaptureEntries,
    CaptureFirstSnapshots,
    CaptureStableAdmissions,
    CaptureRefreshes,
    FallbackProcessIdentity,
    FallbackEpoch,
    FallbackCodeIdentity,
    FallbackPath,
    FallbackManifest,
    FallbackVfsIdentity,
    FallbackDescriptor,
    FallbackPayloadIdentity,
    FallbackAllocation,
    FallbackBudget,
    TailComparisons,
    TailStable,
    TailChanged,
    EntriesDisabledTail,
    SlabsAllocated,
    SlabBytesAllocated,
    ProcessResets,
    UnknownBoundary,
    Count,
};

static constexpr U32 WASM_JIT_AZROK_FACE_WIS_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisCounter::Count;

// Append-only generated-Wasm import.  The exact module/base/signature is
// helpers.fn_36 : (i32 cpu_ptr) -> ().  At 0x00551198 tmpReg is the hit bit;
// at 0x005511b1 the call is capture-only and leaves architectural state intact.
void wasmJitAzrokFaceWisHleHelper(CPU* cpu);

// KProcess calls this before exec/cleanup while its KMemory is still valid.
// It unmaps every process-owned synthetic descriptor and frees the host slabs.
void wasmJitAzrokFaceWisHleProcessReset(KProcess* process);

extern "C" U32 wasm_jit_get_azrok_face_wis_hle_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_helper_index();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_tmp_reg_offset();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_lookup_eip();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_capture_eip();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_manifest_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_hle_snapshot(U64* output,
    U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_hle_counters();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_entry_snapshot_bytes();
extern "C" U32 wasm_jit_copy_azrok_face_wis_hle_entry_snapshot(U32 index,
    U8* output, U32 outputBytes);

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
// Retry diagnostics are intentionally separate from the stable r5 counter
// array, so compiling without RETRY_V1 preserves the r5 ABI and bytes.
enum class WasmJitAzrokFaceWisRetryCounter : U32 {
    DisabledHelperEntries = 0,
    Attempts,
    Successes,
    BusySkips,
    NotDue,
    ProcessEpochTriggers,
    ZipGenerationTriggers,
    BoundedTriggers,
    TransientFailures,
    PermanentFailures,
    Count,
};
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_RETRY_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisRetryCounter::Count;
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_interval();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_hle_retry_snapshot(U64* output,
    U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_hle_retry_counters();
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
enum class WasmJitAzrokFaceWisCwdDiscoveryCounter : U32 {
    BoundarySamples = 0,
    FixedCFiles,
    NonFixedOrUnreadable,
    HookResolved,
    HookUnresolved,
    SetterObserved,
    RelativeFallbacks,
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_CENSUS_V1
    FindNull,
    HashReadFailure,
    HashMismatch,
    CodeMismatch,
    ConfirmFailure,
#endif
    Count,
};
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisCwdDiscoveryCounter::Count;
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_cwd_discovery_snapshot(
    U64* output, U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_cwd_discovery_counters();
void wasmJitAzrokFaceWisCwdDiscoveryNoteHook(bool resolved);
void wasmJitAzrokFaceWisCwdDiscoveryNoteSetter();
void wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback();
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
enum class WasmJitAzrokFaceWisScopedGenerationCounter : U32 {
    ProcessEpochMismatches = 0,
    RootGenerationMismatches,
    AppGenerationMismatches,
    Count,
};
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_SCOPED_GENERATION_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisScopedGenerationCounter::Count;
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_hle_scoped_generation_snapshot(
    U64* output, U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_hle_scoped_generation_counters();
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
enum class WasmJitAzrokFaceWisWarmupCounter : U32 {
    RemainingGauge = 0,
    Arms,
    BoundarySkips,
    Completions,
    Resets,
    ProcessEpochResets,
    RootGenerationResets,
    AppGenerationResets,
    Count,
};
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisWarmupCounter::Count;
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_boundary_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_hle_warmup_snapshot(
    U64* output, U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_hle_warmup_counters();
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
enum class WasmJitAzrokFaceWisCwdEpochCounter : U32 {
    ChdirBumpsSkipped = 0,
    FchdirBumpsSkipped,
    NonFixedBoundaryFallbacks,
    FixedBoundaryPasses,
    Count,
};
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_CWD_EPOCH_COUNTER_COUNT =
    (U32)WasmJitAzrokFaceWisCwdEpochCounter::Count;
void wasmJitAzrokFaceWisHleNoteCwdEpochSkip(bool fchdir);
void wasmJitAzrokFaceWisHleNoteCwdBoundary(bool fixed);
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_abi_version();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_counter_count();
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_snapshot_bytes();
extern "C" void wasm_jit_copy_azrok_face_wis_hle_cwd_epoch_snapshot(
    U64* output, U32 outputBytes);
extern "C" void wasm_jit_reset_azrok_face_wis_hle_cwd_epoch_counters();
#endif

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_FIXTURE)
bool wasmJitAzrokFaceWisHleManifestSelfTest();
bool wasmJitAzrokFaceWisHleSha256SelfTest();
#endif

#endif
#endif
