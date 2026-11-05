/*
 * Azrok D3D9 command transport for the Emscripten WebGL2 host runtime.
 *
 * INT 0x9a is unused when Vulkan is disabled.  The guest d3d9 proxy uses the
 * private 0xd3900000 index range so ordinary Vulkan thunks retain their old
 * no-op behaviour.  Commands are ordered in one batch and completed at
 * Present; resource lifetime/query commands remain synchronous boundaries.
 */
#include "boxedwine.h"
#include "kwaitframeprofile.h"

#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB)
#include <emscripten.h>
#ifdef BOXEDWINE_D3D9WEB_ASYNC_RING
#include <emscripten/threading_primitives.h>
#endif

#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
#if !defined(BOXEDWINE_MULTI_THREADED) || !defined(BOXEDWINE_WASM_JIT) || !defined(BOXEDWINE_D3D9WEB_ASYNC_RING)
#error "INT9A sequence census requires the production MT Wasm-JIT D3D9Web async-ring topology"
#endif
#if defined(__TEST) || defined(BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS) || defined(BOXEDWINE_WASM_JIT_INT9A_DIRECT_FIXTURE)
#error "INT9A sequence census is private production telemetry and cannot mix with test/joined/direct-fixture modes"
#endif
#endif

namespace {
constexpr U32 D3D9WEB_INDEX_BASE = 0xd3900000u;
constexpr U32 D3D9WEB_OK = 0x6001u;
constexpr U32 D3D9WEB_GUEST_BATCH_REJECT = 0x6002u;
constexpr U32 MAX_COMMAND_BYTES = 32u * 1024u * 1024u;
constexpr U32 MAX_BATCH_BYTES = 4u * 1024u * 1024u;
constexpr U32 GUEST_BATCH_MAGIC = 0x31423944u; // "D9B1"
constexpr U32 GUEST_BATCH_VERSION = 1u;
constexpr U32 GUEST_BATCH_HEADER_BYTES = 6u * sizeof(U32);

std::vector<U8> commandBytes;
std::vector<U8> batchedCommandBytes;

#if defined(BOXEDWINE_D3D9WEB_ASYNC_RING) && defined(BOXEDWINE_MULTI_THREADED)
constexpr U32 ASYNC_RING_SLOTS = 3u;
constexpr U32 ASYNC_SLOT_FREE = 0u;
constexpr U32 ASYNC_SLOT_PUBLISHED = 1u;
constexpr U32 ASYNC_SLOT_EXECUTING = 2u;
constexpr U32 ASYNC_SLOT_COMPLETED = 3u;

struct alignas(64) AsyncBatchSlot {
    alignas(4) volatile U32 state = ASYNC_SLOT_FREE;
    alignas(4) volatile U32 result = D3D9WEB_OK;
    alignas(4) volatile U32 sequence = 0;
    alignas(4) volatile U32 bytes = 0;
    alignas(4) volatile U32 terminalOpcode = 0;
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    // Captured before publication and consumed only after this exact slot is
    // COMPLETED and reaped.  It is not a cross-frame aggregate.
    alignas(4) volatile U32 routeMask = 0;
#endif
    alignas(16) U8 payload[MAX_BATCH_BYTES] = {};
};

struct AsyncRingWindow {
    U32 submitted = 0;
    U32 reaped = 0;
    U32 ringFullWaits = 0;
    U32 synchronousDrains = 0;
    U32 errors = 0;
    U32 maxInflight = 0;
};

#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
// One sealed record is copied from the main thread only after its final async
// Present slot has been reaped.  The page never reads the live worker-local
// window.  All integer fields are U64 so the JS reader has one stable ABI.
struct Int9AJoinedCensusSnapshot {
    U64 abi = 0, words = 0, epoch = 0, state = 0, owner = 0;
    U64 submitted = 0, firstSequence = 0, lastSequence = 0, reapedSequence = 0;
    U64 fn8Total = 0, fn8Int9A = 0, fn8Invalid = 0;
    U64 indexCalls[6] = {}, indexBytes[6] = {};
    U64 bridgeCalls[6] = {}, bridgeMicros[6] = {};
    U64 genericCalls = 0, genericBytes = 0, batchCalls = 0, batchBytes = 0;
    U64 genericRuns = 0, guestBatchRecords = 0, guestBatchBytes = 0;
    U64 genericOpcodeCalls[25] = {};
};
static_assert(sizeof(Int9AJoinedCensusSnapshot) == 68u * sizeof(U64),
              "INT9A joined census ABI layout changed");
constexpr U64 INT9A_JOINED_ABI = 1u;
constexpr U64 INT9A_JOINED_VALID = 1u;
constexpr U64 INT9A_JOINED_OWNER_MISMATCH = 2u;
constexpr U64 INT9A_JOINED_SUBMIT_MISMATCH = 4u;
constexpr U64 INT9A_JOINED_ASYNC_ERROR = 8u;

struct Int9AJoinedWindow {
    CPU* owner = nullptr;
    U64 startFn8Total = 0, startFn8Int9A = 0, startFn8Invalid = 0;
    U64 fn8Total = 0, fn8Int9A = 0, fn8Invalid = 0;
    U64 state = 0;
    U32 submitted = 0, firstSequence = 0, lastSequence = 0;
    U64 indexCalls[6] = {}, indexBytes[6] = {};
    U64 bridgeCalls[6] = {}, bridgeMicros[6] = {};
    U64 genericCalls = 0, genericBytes = 0, batchCalls = 0, batchBytes = 0;
    U64 genericRuns = 0, guestBatchRecords = 0, guestBatchBytes = 0;
    U64 genericOpcodeCalls[25] = {};
};

thread_local Int9AJoinedWindow int9AJoinedWindow;
static std::mutex int9AJoinedSealedMutex;
static Int9AJoinedCensusSnapshot int9AJoinedSealed;
static U64 int9AJoinedNextEpoch = 0;

static void int9AJoinedEnsureOwner(CPU* cpu) {
    if (!int9AJoinedWindow.owner) {
        int9AJoinedWindow.owner = cpu;
    } else if (int9AJoinedWindow.owner != cpu) {
        int9AJoinedWindow.state |= INT9A_JOINED_OWNER_MISMATCH;
    }
}

static void int9AJoinedResetWindow() {
    const U64 fn8Total = int9AJoinedWindow.fn8Total;
    const U64 fn8Int9A = int9AJoinedWindow.fn8Int9A;
    const U64 fn8Invalid = int9AJoinedWindow.fn8Invalid;
    int9AJoinedWindow = Int9AJoinedWindow{};
    // fn_8 counters are accumulated by the generated worker path between
    // D3D calls.  Preserve their monotonic coordinate across sealed windows;
    // the snapshot takes the difference from these start values.
    int9AJoinedWindow.fn8Total = fn8Total;
    int9AJoinedWindow.fn8Int9A = fn8Int9A;
    int9AJoinedWindow.fn8Invalid = fn8Invalid;
    int9AJoinedWindow.startFn8Total = fn8Total;
    int9AJoinedWindow.startFn8Int9A = fn8Int9A;
    int9AJoinedWindow.startFn8Invalid = fn8Invalid;
}
#endif

#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
constexpr U32 INT9A_SEQUENCE_ROUTE_MASK = (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 5u);
constexpr U64 INT9A_SEQUENCE_ABI = 1u;
constexpr U64 INT9A_SEQUENCE_VALID = 1u;
constexpr U64 INT9A_SEQUENCE_ORDER_ERROR = 2u;
constexpr U64 INT9A_SEQUENCE_ASYNC_ERROR = 4u;

struct Int9ASequenceCensusSnapshot {
    U64 abi = 0, words = 0, epoch = 0, state = 0;
    U64 submitted = 0, firstSequence = 0, lastSequence = 0, reapedSequence = 0;
    U64 reapedSlots = 0, requiredRouteMask = 0, qualifyingReaped = 0;
    U64 firstQualifiedSequence = 0, lastQualifiedSequence = 0, lastQualifiedRouteMask = 0;
    U64 lastTerminalOpcode = 0;
};
static_assert(sizeof(Int9ASequenceCensusSnapshot) == 15u * sizeof(U64),
              "INT9A sequence census ABI layout changed");

struct Int9ASequenceCensusWindow {
    U64 state = 0;
    U32 submitted = 0, firstSequence = 0, lastSequence = 0;
    U32 reapedSlots = 0, qualifyingReaped = 0;
    U32 firstQualifiedSequence = 0, lastQualifiedSequence = 0;
    U32 lastQualifiedRouteMask = 0, lastTerminalOpcode = 0;
};

thread_local U32 int9ASequencePendingRouteMask = 0;
thread_local Int9ASequenceCensusWindow int9ASequenceWindow;
static std::mutex int9ASequenceSealedMutex;
static Int9ASequenceCensusSnapshot int9ASequenceSealed;
static U64 int9ASequenceNextEpoch = 0;

static void int9ASequenceOnReaped(U32 expectedSequence, U32 actualSequence, U32 result,
                                  U32 terminalOpcode, U32 routeMask);
static void int9ASequenceOnPresentSubmitted(U32 sequence);

static void int9ASequenceResetWindow() {
    int9ASequenceWindow = Int9ASequenceCensusWindow{};
}

static void int9ASequenceOnSynchronousFlush() {
    // A non-Present flush drains on the worker.  Its prior routes cannot be
    // attributed to a later terminal async slot.
    int9ASequencePendingRouteMask = 0;
}

static void int9ASequenceOnSuccessfulRoute(U32 index) {
    if (index >= 3u && index <= 5u) {
        int9ASequencePendingRouteMask |= 1u << index;
    }
}
#endif

AsyncBatchSlot asyncBatchSlots[ASYNC_RING_SLOTS];
thread_local U32 asyncSubmittedSequence = 0;
thread_local U32 asyncReapedSequence = 0;
thread_local U32 asyncStickyError = D3D9WEB_OK;
thread_local AsyncRingWindow asyncRingWindow;

static U32 asyncLoad(const volatile U32* value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void asyncStore(volatile U32* value, U32 replacement) {
    __atomic_store_n(value, replacement, __ATOMIC_RELEASE);
}

static AsyncBatchSlot& asyncSlotForSequence(U32 sequence) {
    return asyncBatchSlots[(sequence - 1u) % ASYNC_RING_SLOTS];
}

static void updateAsyncMaxInflight() {
    const U32 inflight = asyncSubmittedSequence - asyncReapedSequence;
    if (inflight > asyncRingWindow.maxInflight) {
        asyncRingWindow.maxInflight = inflight;
    }
}

static U32 reapNextAsyncBatch(bool waitForCompletion) {
    if (asyncReapedSequence == asyncSubmittedSequence) {
        return asyncStickyError;
    }
    const U32 expectedSequence = asyncReapedSequence + 1u;
    AsyncBatchSlot& slot = asyncSlotForSequence(expectedSequence);
    U32 state = asyncLoad(&slot.state);
    while (state != ASYNC_SLOT_COMPLETED) {
        if (!waitForCompletion) {
            return asyncStickyError;
        }
        emscripten_futex_wait(&slot.state, state, 1000.0);
        state = asyncLoad(&slot.state);
    }

    const U32 actualSequence = asyncLoad(&slot.sequence);
    const U32 result = asyncLoad(&slot.result);
    if (actualSequence != expectedSequence || result != D3D9WEB_OK) {
        asyncStickyError = result != D3D9WEB_OK ? result : 0u;
        asyncRingWindow.errors++;
    }
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    int9ASequenceOnReaped(expectedSequence, actualSequence, result,
                           asyncLoad(&slot.terminalOpcode), asyncLoad(&slot.routeMask));
#endif
    asyncStore(&slot.state, ASYNC_SLOT_FREE);
    asyncReapedSequence = expectedSequence;
    asyncRingWindow.reaped++;
    return asyncStickyError;
}

static void reapCompletedAsyncBatches() {
    while (asyncReapedSequence != asyncSubmittedSequence) {
        AsyncBatchSlot& slot = asyncSlotForSequence(asyncReapedSequence + 1u);
        if (asyncLoad(&slot.state) != ASYNC_SLOT_COMPLETED) {
            return;
        }
        reapNextAsyncBatch(false);
    }
}

static U32 drainAsyncBatches() {
    if (asyncReapedSequence != asyncSubmittedSequence) {
        asyncRingWindow.synchronousDrains++;
    }
    while (asyncReapedSequence != asyncSubmittedSequence) {
        reapNextAsyncBatch(true);
    }
    return asyncStickyError;
}

#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
static void int9ASequenceSealCompletedWindow() {
    const bool contiguous = int9ASequenceWindow.firstSequence != 0u &&
        int9ASequenceWindow.lastSequence - int9ASequenceWindow.firstSequence == 119u &&
        asyncReapedSequence >= int9ASequenceWindow.lastSequence &&
        int9ASequenceWindow.reapedSlots == 120u;
    if (!contiguous) {
        int9ASequenceWindow.state |= INT9A_SEQUENCE_ORDER_ERROR;
    }
    if (asyncStickyError != D3D9WEB_OK) {
        int9ASequenceWindow.state |= INT9A_SEQUENCE_ASYNC_ERROR;
    }
    Int9ASequenceCensusSnapshot snapshot;
    snapshot.abi = INT9A_SEQUENCE_ABI;
    snapshot.words = sizeof(snapshot) / sizeof(U64);
    snapshot.state = int9ASequenceWindow.state ? int9ASequenceWindow.state : INT9A_SEQUENCE_VALID;
    snapshot.submitted = int9ASequenceWindow.submitted;
    snapshot.firstSequence = int9ASequenceWindow.firstSequence;
    snapshot.lastSequence = int9ASequenceWindow.lastSequence;
    snapshot.reapedSequence = asyncReapedSequence;
    snapshot.reapedSlots = int9ASequenceWindow.reapedSlots;
    snapshot.requiredRouteMask = INT9A_SEQUENCE_ROUTE_MASK;
    snapshot.qualifyingReaped = int9ASequenceWindow.qualifyingReaped;
    snapshot.firstQualifiedSequence = int9ASequenceWindow.firstQualifiedSequence;
    snapshot.lastQualifiedSequence = int9ASequenceWindow.lastQualifiedSequence;
    snapshot.lastQualifiedRouteMask = int9ASequenceWindow.lastQualifiedRouteMask;
    snapshot.lastTerminalOpcode = int9ASequenceWindow.lastTerminalOpcode;
    {
        std::lock_guard<std::mutex> lock(int9ASequenceSealedMutex);
        snapshot.epoch = ++int9ASequenceNextEpoch;
        int9ASequenceSealed = snapshot;
    }
    int9ASequenceResetWindow();
}

static void int9ASequenceOnPresentSubmitted(U32 sequence) {
    if (!int9ASequenceWindow.submitted) {
        int9ASequenceWindow.firstSequence = sequence;
    } else if (sequence != int9ASequenceWindow.lastSequence + 1u) {
        int9ASequenceWindow.state |= INT9A_SEQUENCE_ORDER_ERROR;
    }
    int9ASequenceWindow.lastSequence = sequence;
    ++int9ASequenceWindow.submitted;
    if (int9ASequenceWindow.submitted == 120u) {
        // This waits for the exact main-thread callback completion path; no
        // sequence is sealed from an un-reaped slot.
        drainAsyncBatches();
        int9ASequenceSealCompletedWindow();
    }
}

static void int9ASequenceOnReaped(U32 expectedSequence, U32 actualSequence, U32 result,
                                  U32 terminalOpcode, U32 routeMask) {
    ++int9ASequenceWindow.reapedSlots;
    int9ASequenceWindow.lastTerminalOpcode = terminalOpcode;
    if (actualSequence != expectedSequence || result != D3D9WEB_OK || terminalOpcode != 11u) {
        int9ASequenceWindow.state |= INT9A_SEQUENCE_ASYNC_ERROR;
        return;
    }
    if ((routeMask & INT9A_SEQUENCE_ROUTE_MASK) != INT9A_SEQUENCE_ROUTE_MASK) {
        return;
    }
    if (!int9ASequenceWindow.qualifyingReaped) {
        int9ASequenceWindow.firstQualifiedSequence = actualSequence;
    }
    ++int9ASequenceWindow.qualifyingReaped;
    int9ASequenceWindow.lastQualifiedSequence = actualSequence;
    int9ASequenceWindow.lastQualifiedRouteMask = routeMask;
}
#endif

#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
static void int9AJoinedSealCompletedWindow() {
    const bool sequencesAreContiguous = int9AJoinedWindow.firstSequence != 0 &&
        int9AJoinedWindow.lastSequence - int9AJoinedWindow.firstSequence == 119u &&
        asyncReapedSequence >= int9AJoinedWindow.lastSequence;
    if (!sequencesAreContiguous) {
        int9AJoinedWindow.state |= INT9A_JOINED_SUBMIT_MISMATCH;
    }
    if (asyncStickyError != D3D9WEB_OK) {
        int9AJoinedWindow.state |= INT9A_JOINED_ASYNC_ERROR;
    }

    Int9AJoinedCensusSnapshot snapshot;
    snapshot.abi = INT9A_JOINED_ABI;
    snapshot.words = sizeof(snapshot) / sizeof(U64);
    snapshot.state = int9AJoinedWindow.state ? int9AJoinedWindow.state : INT9A_JOINED_VALID;
    snapshot.owner = (U64)(uintptr_t)int9AJoinedWindow.owner;
    snapshot.submitted = int9AJoinedWindow.submitted;
    snapshot.firstSequence = int9AJoinedWindow.firstSequence;
    snapshot.lastSequence = int9AJoinedWindow.lastSequence;
    snapshot.reapedSequence = asyncReapedSequence;
    snapshot.fn8Total = int9AJoinedWindow.fn8Total - int9AJoinedWindow.startFn8Total;
    snapshot.fn8Int9A = int9AJoinedWindow.fn8Int9A - int9AJoinedWindow.startFn8Int9A;
    snapshot.fn8Invalid = int9AJoinedWindow.fn8Invalid - int9AJoinedWindow.startFn8Invalid;
    memcpy(snapshot.indexCalls, int9AJoinedWindow.indexCalls, sizeof(snapshot.indexCalls));
    memcpy(snapshot.indexBytes, int9AJoinedWindow.indexBytes, sizeof(snapshot.indexBytes));
    memcpy(snapshot.bridgeCalls, int9AJoinedWindow.bridgeCalls, sizeof(snapshot.bridgeCalls));
    memcpy(snapshot.bridgeMicros, int9AJoinedWindow.bridgeMicros, sizeof(snapshot.bridgeMicros));
    snapshot.genericCalls = int9AJoinedWindow.genericCalls;
    snapshot.genericBytes = int9AJoinedWindow.genericBytes;
    snapshot.batchCalls = int9AJoinedWindow.batchCalls;
    snapshot.batchBytes = int9AJoinedWindow.batchBytes;
    snapshot.genericRuns = int9AJoinedWindow.genericRuns;
    snapshot.guestBatchRecords = int9AJoinedWindow.guestBatchRecords;
    snapshot.guestBatchBytes = int9AJoinedWindow.guestBatchBytes;
    memcpy(snapshot.genericOpcodeCalls, int9AJoinedWindow.genericOpcodeCalls,
           sizeof(snapshot.genericOpcodeCalls));
    {
        std::lock_guard<std::mutex> lock(int9AJoinedSealedMutex);
        snapshot.epoch = ++int9AJoinedNextEpoch;
        int9AJoinedSealed = snapshot;
    }
    int9AJoinedResetWindow();
}

static void int9AJoinedOnPresentSubmitted(CPU* cpu, U32 sequence) {
    int9AJoinedEnsureOwner(cpu);
    if (!int9AJoinedWindow.submitted) {
        int9AJoinedWindow.firstSequence = sequence;
    } else if (sequence != int9AJoinedWindow.lastSequence + 1u) {
        int9AJoinedWindow.state |= INT9A_JOINED_SUBMIT_MISMATCH;
    }
    int9AJoinedWindow.lastSequence = sequence;
    ++int9AJoinedWindow.submitted;
    if (int9AJoinedWindow.submitted != 120u) {
        return;
    }
    // Completion state is written only after main-thread dispatchBatch returns.
    // Waiting here is diagnostic-only and prevents the next window from being
    // mixed into a still-unreaped final slot.
    drainAsyncBatches();
    int9AJoinedSealCompletedWindow();
}
#endif

static U32 submitAsyncPresentBatch(const U8* bytes, U32 byteCount, U32 terminalOpcode) {
    if (!bytes || !byteCount || byteCount > MAX_BATCH_BYTES || terminalOpcode != 11u) {
        return 0u;
    }
    reapCompletedAsyncBatches();
    if (asyncStickyError != D3D9WEB_OK) {
        return asyncStickyError;
    }

    const U32 sequence = asyncSubmittedSequence + 1u;
    AsyncBatchSlot& slot = asyncSlotForSequence(sequence);
    if (asyncLoad(&slot.state) != ASYNC_SLOT_FREE) {
        asyncRingWindow.ringFullWaits++;
        const U32 waitResult = reapNextAsyncBatch(true);
        if (waitResult != D3D9WEB_OK) {
            return waitResult;
        }
    }
    if (asyncLoad(&slot.state) != ASYNC_SLOT_FREE) {
        asyncStickyError = 0u;
        asyncRingWindow.errors++;
        return 0u;
    }

    memcpy(slot.payload, bytes, byteCount);
    asyncStore(&slot.result, D3D9WEB_OK);
    asyncStore(&slot.sequence, sequence);
    asyncStore(&slot.bytes, byteCount);
    asyncStore(&slot.terminalOpcode, terminalOpcode);
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    asyncStore(&slot.routeMask, int9ASequencePendingRouteMask | (1u << 2u));
    int9ASequencePendingRouteMask = 0;
#endif
    asyncStore(&slot.state, ASYNC_SLOT_PUBLISHED);

    MAIN_THREAD_ASYNC_EM_ASM({
        const stateIndex = $0 >>> 2;
        const resultIndex = $1 >>> 2;
        const sequenceIndex = $2 >>> 2;
        const payload = $3 >>> 0;
        const byteCount = $4 >>> 0;
        const expectedSequence = $5 >>> 0;
        const stats = globalThis.D3D9WebAsyncRingDebug ||
            (globalThis.D3D9WebAsyncRingDebug = {
                callbacks: 0,
                errors: 0,
                lastSequence: 0,
                nextSequence: 1,
                firstSequence: 0,
                gaps: 0,
                reorders: 0
            });
        let result = 0;
        let ringError = 0;
        if (expectedSequence !== (stats.nextSequence >>> 0)) {
            if (expectedSequence > (stats.nextSequence >>> 0)) {
                stats.gaps += expectedSequence - (stats.nextSequence >>> 0);
            } else {
                stats.reorders++;
            }
            ringError = 4;
            console.error(`D3D9WEB_ASYNC_RING_ERROR order expected=${stats.nextSequence >>> 0} actual=${expectedSequence}`);
        }
        stats.nextSequence = (expectedSequence + 1) >>> 0;
        if (!ringError &&
            ((Atomics.load(HEAPU32, sequenceIndex) >>> 0) !== expectedSequence ||
             Atomics.compareExchange(HEAPU32, stateIndex, 1, 2) !== 1)) {
            ringError = 1;
        }
        if (!ringError) {
            try {
                if (typeof FS !== 'undefined') globalThis.BoxedWineFS = FS;
                const runtime = globalThis.D3D9WebRuntime;
                if (runtime && typeof runtime.dispatchBatch === 'function') {
                    result = runtime.dispatchBatch(
                        HEAPU8.subarray(payload, (payload + byteCount) >>> 0)) >>> 0;
                } else {
                    ringError = 2;
                    console.error('D3D9WEB_ASYNC_RING_ERROR runtime-not-ready');
                }
            } catch (error) {
                ringError = 3;
                console.error('D3D9WEB_ASYNC_RING_ERROR dispatch-exception', error);
            }
        }
        if (ringError) result = 0;
        Atomics.store(HEAPU32, resultIndex, result >>> 0);
        Atomics.store(HEAPU32, stateIndex, 3);
        Atomics.notify(HEAP32, stateIndex, 1);
        if ((stats.callbacks % 120) === 0) stats.firstSequence = expectedSequence;
        stats.callbacks++;
        if (ringError) stats.errors++;
        stats.lastSequence = expectedSequence;
        if ((stats.callbacks % 120) === 0) {
            console.log(`D3D9WEB_ASYNC_RING_MAIN callbacks=120 first_seq=${stats.firstSequence} last_seq=${stats.lastSequence} next_seq=${stats.nextSequence >>> 0} gaps=${stats.gaps} reorders=${stats.reorders} errors=${stats.errors}`);
            stats.errors = 0;
            stats.gaps = 0;
            stats.reorders = 0;
            stats.firstSequence = 0;
        }
    }, (U32)&slot.state, (U32)&slot.result, (U32)&slot.sequence,
       (U32)slot.payload, byteCount, sequence);

    asyncSubmittedSequence = sequence;
    asyncRingWindow.submitted++;
    updateAsyncMaxInflight();
    if (asyncRingWindow.submitted == 120u) {
        reapCompletedAsyncBatches();
        klog_fmt("D3D9WEB_ASYNC_RING_FRAME presents=120 submitted=%u reaped=%u first_seq=%u last_seq=%u inflight=%u max_inflight=%u ring_full=%u sync_drains=%u errors=%u",
            asyncRingWindow.submitted,
            asyncRingWindow.reaped,
            asyncSubmittedSequence - asyncRingWindow.submitted + 1u,
            asyncSubmittedSequence,
            asyncSubmittedSequence - asyncReapedSequence,
            asyncRingWindow.maxInflight,
            asyncRingWindow.ringFullWaits,
            asyncRingWindow.synchronousDrains,
            asyncRingWindow.errors);
        asyncRingWindow = AsyncRingWindow{};
    }
    return D3D9WEB_OK;
}
#endif

#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
struct IntCensus {
    U64 indexCalls[6] = {};
    U64 indexBytes[6] = {};
    U64 opcodeCalls[25] = {};
    U64 base2Calls = 0;
    U64 base2Bytes = 0;
    U64 batchCalls = 0;
    U64 batchBytes = 0;
    U64 directWithPending[2] = {};
    U64 terminalWithPending = 0;
    U64 terminalPendingByOpcode[25] = {};
    U64 genericRuns = 0;
    U64 genericPendingBytes = 0;
    U64 maxGenericPendingBytes = 0;
    U64 guestBatchRecords = 0;
    U64 guestBatchRecordBytes = 0;
    U32 presents = 0;
    bool genericPending = false;
};

thread_local IntCensus intCensus;
#endif

#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
struct AsyncGapProfile {
    double presentReturnMs = 0.0;
    U64 lastPresentWaitMicros = 0;
    U64 presentWaitMicros = 0;
    U64 presentToTailMicros = 0;
    U64 pairedOverlapCeilingMicros = 0;
    U64 minPresentToTailMicros = ~0ull;
    U64 maxPresentToTailMicros = 0;
    U64 tailOpcodes[32] = {};
    U32 overwrittenPresents = 0;
    U32 samples = 0;
};

thread_local AsyncGapProfile asyncGapProfile;

static U64 d3d9webElapsedMicros(double startMs, double endMs) {
    const double elapsed = endMs - startMs;
    return elapsed > 0.0 ? (U64)(elapsed * 1000.0 + 0.5) : 0;
}

static U64 beginNextSynchronousTail() {
    if (asyncGapProfile.presentReturnMs == 0.0) return ~0ull;
    const double nowMs = emscripten_get_now();
    const U64 gapMicros = d3d9webElapsedMicros(asyncGapProfile.presentReturnMs, nowMs);
    asyncGapProfile.presentReturnMs = 0.0;
    return gapMicros;
}

static void commitNextSynchronousTail(U32 opcode, U64 gapMicros) {
    if (gapMicros == ~0ull) return;
    const U64 waitMicros = asyncGapProfile.lastPresentWaitMicros;
    asyncGapProfile.lastPresentWaitMicros = 0;
    asyncGapProfile.presentWaitMicros += waitMicros;
    asyncGapProfile.presentToTailMicros += gapMicros;
    asyncGapProfile.pairedOverlapCeilingMicros +=
        waitMicros < gapMicros ? waitMicros : gapMicros;
    if (gapMicros < asyncGapProfile.minPresentToTailMicros) {
        asyncGapProfile.minPresentToTailMicros = gapMicros;
    }
    if (gapMicros > asyncGapProfile.maxPresentToTailMicros) {
        asyncGapProfile.maxPresentToTailMicros = gapMicros;
    }
    asyncGapProfile.tailOpcodes[opcode < 32u ? opcode : 31u]++;
    asyncGapProfile.samples++;
    if (asyncGapProfile.samples == 120u) {
        klog_fmt("D3D9WEB_ASYNC_GAP_PROFILE samples=120 present_wait_us=%llu present_to_tail_us=%llu paired_overlap_ceiling_us=%llu min_tail_us=%llu max_tail_us=%llu overwritten_presents=%u",
            (unsigned long long)asyncGapProfile.presentWaitMicros,
            (unsigned long long)asyncGapProfile.presentToTailMicros,
            (unsigned long long)asyncGapProfile.pairedOverlapCeilingMicros,
            (unsigned long long)asyncGapProfile.minPresentToTailMicros,
            (unsigned long long)asyncGapProfile.maxPresentToTailMicros,
            asyncGapProfile.overwrittenPresents);
        for (U32 reason = 0; reason < 32u; ++reason) {
            if (asyncGapProfile.tailOpcodes[reason]) {
                klog_fmt("D3D9WEB_ASYNC_GAP_TAIL samples=120 opcode=%u calls=%llu",
                    reason, (unsigned long long)asyncGapProfile.tailOpcodes[reason]);
            }
        }
        asyncGapProfile = AsyncGapProfile{};
    }
}

static void recordPresentWait(double startMs, double endMs) {
    if (asyncGapProfile.presentReturnMs != 0.0) {
        asyncGapProfile.overwrittenPresents++;
    }
    asyncGapProfile.lastPresentWaitMicros = d3d9webElapsedMicros(startMs, endMs);
    asyncGapProfile.presentReturnMs = endMs;
}
#endif

#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
struct BridgeFrameProfile {
    U64 calls[6] = {};
    U64 callMicros[6] = {};
    U64 mainRoundTrips = 0;
    U64 mainWaitMicros = 0;
    U64 mainReasonCalls[2][32] = {};
    U64 mainReasonWaitMicros[2][32] = {};
    U64 kernelMicros = 0;
    U64 lastKernelMicros = 0;
    U64 frameWallMicros = 0;
    U32 frames = 0;
    double lastPresentMs = 0.0;
};

thread_local BridgeFrameProfile bridgeFrameProfile;

static U64 elapsedMicros(double startMs) {
    const double elapsed = emscripten_get_now() - startMs;
    return elapsed > 0.0 ? (U64)(elapsed * 1000.0 + 0.5) : 0;
}

static void resetBridgeWindow(bool preserveFrameBoundary) {
    const double lastPresentMs = preserveFrameBoundary ? bridgeFrameProfile.lastPresentMs : 0.0;
    const U64 lastKernelMicros = preserveFrameBoundary ? bridgeFrameProfile.lastKernelMicros : 0;
    bridgeFrameProfile = BridgeFrameProfile{};
    bridgeFrameProfile.lastPresentMs = lastPresentMs;
    bridgeFrameProfile.lastKernelMicros = lastKernelMicros;
}
#endif

static U32 dispatchCommandOnMainThread(U32 address, U32 bytes, U32 opcode) {
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const double waitStartMs = emscripten_get_now();
#endif
    U32 result = 0;
#ifdef BOXEDWINE_MULTI_THREADED
    result = MAIN_THREAD_EM_ASM_INT({
        if (typeof FS !== 'undefined') globalThis.BoxedWineFS = FS;
        const runtime = globalThis.D3D9WebRuntime;
        if (!runtime || typeof runtime.dispatch !== 'function') {
            console.error('D3D9WEB_PHASE6_ERROR runtime-not-ready');
            return 0;
        }
        return runtime.dispatch(HEAPU8.subarray($0 >>> 0, ($0 + $1) >>> 0)) >>> 0;
    }, address, bytes);
#else
    result = EM_ASM_INT({
        if (typeof FS !== 'undefined') globalThis.BoxedWineFS = FS;
        const runtime = globalThis.D3D9WebRuntime;
        if (!runtime || typeof runtime.dispatch !== 'function') {
            console.error('D3D9WEB_PHASE6_ERROR runtime-not-ready');
            return 0;
        }
        return runtime.dispatch(HEAPU8.subarray($0 >>> 0, ($0 + $1) >>> 0)) >>> 0;
    }, address, bytes);
#endif
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const U64 waitMicros = elapsedMicros(waitStartMs);
    const U32 reason = opcode < 31u ? opcode : 31u;
    bridgeFrameProfile.mainRoundTrips += 1;
    bridgeFrameProfile.mainWaitMicros += waitMicros;
    bridgeFrameProfile.mainReasonCalls[0][reason] += 1;
    bridgeFrameProfile.mainReasonWaitMicros[0][reason] += waitMicros;
#endif
    return result;
}

static U32 dispatchBatchOnMainThread(U32 address, U32 bytes, U32 flushReason) {
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const double waitStartMs = emscripten_get_now();
#endif
    U32 result = 0;
#ifdef BOXEDWINE_MULTI_THREADED
    result = MAIN_THREAD_EM_ASM_INT({
        if (typeof FS !== 'undefined') globalThis.BoxedWineFS = FS;
        const runtime = globalThis.D3D9WebRuntime;
        if (!runtime || typeof runtime.dispatchBatch !== 'function') {
            console.error('D3D9WEB_PHASE6_ERROR batch-runtime-not-ready');
            return 0;
        }
        return runtime.dispatchBatch(HEAPU8.subarray($0 >>> 0, ($0 + $1) >>> 0)) >>> 0;
    }, address, bytes);
#else
    result = EM_ASM_INT({
        if (typeof FS !== 'undefined') globalThis.BoxedWineFS = FS;
        const runtime = globalThis.D3D9WebRuntime;
        if (!runtime || typeof runtime.dispatchBatch !== 'function') {
            console.error('D3D9WEB_PHASE6_ERROR batch-runtime-not-ready');
            return 0;
        }
        return runtime.dispatchBatch(HEAPU8.subarray($0 >>> 0, ($0 + $1) >>> 0)) >>> 0;
    }, address, bytes);
#endif
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const U64 waitMicros = elapsedMicros(waitStartMs);
    const U32 reason = flushReason < 31u ? flushReason : 31u;
    bridgeFrameProfile.mainRoundTrips += 1;
    bridgeFrameProfile.mainWaitMicros += waitMicros;
    bridgeFrameProfile.mainReasonCalls[1][reason] += 1;
    bridgeFrameProfile.mainReasonWaitMicros[1][reason] += waitMicros;
#endif
    return result;
}

static U32 flushBatch(U32 reason) {
    if (batchedCommandBytes.empty()) {
#if defined(BOXEDWINE_D3D9WEB_ASYNC_RING) && defined(BOXEDWINE_MULTI_THREADED)
        if (reason != 11u) {
            const U32 result = drainAsyncBatches();
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
            int9ASequenceOnSynchronousFlush();
#endif
            return result;
        }
#endif
        return D3D9WEB_OK;
    }
#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
    intCensus.batchCalls++;
    intCensus.batchBytes += batchedCommandBytes.size();
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    int9AJoinedWindow.batchCalls++;
    int9AJoinedWindow.batchBytes += batchedCommandBytes.size();
#endif
#if defined(BOXEDWINE_D3D9WEB_ASYNC_RING) && defined(BOXEDWINE_MULTI_THREADED)
    if (reason == 11u) {
        const U32 result = submitAsyncPresentBatch(
            batchedCommandBytes.data(), (U32)batchedCommandBytes.size(), reason);
        batchedCommandBytes.clear();
        return result;
    }
    const U32 drainResult = drainAsyncBatches();
    if (drainResult != D3D9WEB_OK) {
        return drainResult;
    }
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    int9ASequenceOnSynchronousFlush();
#endif
#endif
    U32 result = dispatchBatchOnMainThread(
        (U32)batchedCommandBytes.data(), (U32)batchedCommandBytes.size(), reason);
    batchedCommandBytes.clear();
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    int9ASequenceOnSynchronousFlush();
#endif
    return result;
}

static bool reserveBatchPacket(U32 packetBytes, U32& packetOffset) {
    const U64 required = (U64)batchedCommandBytes.size() + sizeof(U32) + packetBytes;
    if (!batchedCommandBytes.empty() && required > MAX_BATCH_BYTES) {
        if (flushBatch(31u) != D3D9WEB_OK) {
            return false;
        }
    }
    packetOffset = (U32)batchedCommandBytes.size();
    batchedCommandBytes.resize((size_t)packetOffset + sizeof(U32) + packetBytes);
    memcpy(batchedCommandBytes.data() + packetOffset, &packetBytes, sizeof(packetBytes));
    return true;
}

static bool canBatch(U32 opcode) {
    switch (opcode) {
    case 2:  // Clear
    case 3:  // SetTransform
    case 4:  // SetViewport
    case 5:  // SetRenderState
    case 6:  // SetTextureStageState
    case 7:  // SetSamplerState
    case 8:  // SetScissorRect
    case 9:  // SetFVF
    case 10: // DrawPrimitiveUP
    case 11: // Present
    case 14: // UploadResource
    case 15: // DestroyResource (guest Release ignores the bridge result)
    case 16: // BindTexture
    case 17: // BindStream
    case 18: // BindIndices
    case 19: // DrawIndexedPrimitive
    case 21: // BindDeclaration
    case 22: // SetMaterial
    case 23: // SetLight
    case 24: // LightEnable
        return true;
    default:
        return false;
    }
}

static bool isSynchronousTail(U32 opcode) {
    switch (opcode) {
    case 1:  // Init
    case 12: // CreateTexture
    case 13: // CreateBuffer
    case 20: // CreateDeclaration
        return true;
    default:
        return false;
    }
}

static U32 enqueueDirectUpload(CPU* cpu, U32 descriptor, U32 descriptorBytes) {
    constexpr U32 DESCRIPTOR_BYTES = 10u * sizeof(U32);
    constexpr U32 HEADER_BYTES = 10u * sizeof(U32);
    if (!descriptor || descriptorBytes != DESCRIPTOR_BYTES) {
        return 0;
    }

    const U32 handle = cpu->memory->readd(descriptor);
    const U32 kind = cpu->memory->readd(descriptor + 4u);
    const U32 data = cpu->memory->readd(descriptor + 8u);
    const U32 offset = cpu->memory->readd(descriptor + 12u);
    const U32 resourceBytes = cpu->memory->readd(descriptor + 16u);
    const U32 uploadBytes = cpu->memory->readd(descriptor + 20u);
    const U32 finalChunk = cpu->memory->readd(descriptor + 24u);
    const U32 lockBytes = cpu->memory->readd(descriptor + 28u);
    const U32 lockFlags = cpu->memory->readd(descriptor + 32u);
    const U32 lockHash = cpu->memory->readd(descriptor + 36u);

    if (!data || !uploadBytes || data > 0xffffffffu - uploadBytes ||
        uploadBytes > MAX_COMMAND_BYTES - HEADER_BYTES ||
        offset > resourceBytes || uploadBytes > resourceBytes - offset || finalChunk > 1u) {
        return 0;
    }

    const U32 packetBytes = HEADER_BYTES + uploadBytes;
    U32 packetOffset = 0;
    if (!reserveBatchPacket(packetBytes, packetOffset)) {
        return 0;
    }
    U8* destination = batchedCommandBytes.data() + packetOffset + sizeof(U32);
    const U32 header[10] = {
        14u, handle, kind, offset, resourceBytes, uploadBytes,
        finalChunk, lockBytes, lockFlags, lockHash};
    memcpy(destination, header, sizeof(header));
    cpu->memory->memcpy(destination + sizeof(header), data, uploadBytes);
    return D3D9WEB_OK;
}

static U32 enqueueDirectDrawPrimitiveUP(CPU* cpu, U32 descriptor, U32 descriptorBytes) {
    constexpr U32 DESCRIPTOR_BYTES = 6u * sizeof(U32);
    constexpr U32 HEADER_BYTES = 6u * sizeof(U32);
    constexpr U32 MAX_DRAW_PACKET_BYTES = 2u * 1024u * 1024u;
    if (!descriptor || descriptorBytes != DESCRIPTOR_BYTES) {
        return 0;
    }

    const U32 primitiveType = cpu->memory->readd(descriptor);
    const U32 primitiveCount = cpu->memory->readd(descriptor + 4u);
    const U32 stride = cpu->memory->readd(descriptor + 8u);
    const U32 fvf = cpu->memory->readd(descriptor + 12u);
    const U32 data = cpu->memory->readd(descriptor + 16u);
    const U32 vertexBytes = cpu->memory->readd(descriptor + 20u);

    U64 vertexCount = 0;
    switch (primitiveType) {
    case 1u: vertexCount = primitiveCount; break;
    case 2u: vertexCount = (U64)primitiveCount * 2u; break;
    case 3u: vertexCount = (U64)primitiveCount + 1u; break;
    case 4u: vertexCount = (U64)primitiveCount * 3u; break;
    case 5u:
    case 6u: vertexCount = (U64)primitiveCount + 2u; break;
    default: return 0;
    }

    const U64 expectedBytes = vertexCount * stride;
    if (!data || !stride || expectedBytes != vertexBytes ||
        vertexBytes > MAX_DRAW_PACKET_BYTES - HEADER_BYTES ||
        data > 0xffffffffu - vertexBytes) {
        return 0;
    }

    const U32 packetBytes = HEADER_BYTES + vertexBytes;
    U32 packetOffset = 0;
    if (!reserveBatchPacket(packetBytes, packetOffset)) {
        return 0;
    }
    U8* destination = batchedCommandBytes.data() + packetOffset + sizeof(U32);
    const U32 header[6] = {
        10u, primitiveType, primitiveCount, stride, fvf, vertexBytes};
    memcpy(destination, header, sizeof(header));
    cpu->memory->memcpy(destination + sizeof(header), data, vertexBytes);
    return D3D9WEB_OK;
}

static U32 enqueueGuestBatch(CPU* cpu, U32 source, U32 bytes) {
    if (!source || bytes < GUEST_BATCH_HEADER_BYTES || bytes > MAX_BATCH_BYTES ||
        source > 0xffffffffu - bytes) {
        return D3D9WEB_GUEST_BATCH_REJECT;
    }

    commandBytes.resize(bytes);
    cpu->memory->memcpy(commandBytes.data(), source, bytes);
    U32 header[6] = {};
    memcpy(header, commandBytes.data(), sizeof(header));
    const U32 magic = header[0];
    const U32 version = header[1];
    const U32 flags = header[2];
    const U32 recordCount = header[3];
    const U32 recordBytes = header[4];
    const U32 reserved = header[5];
    if (magic != GUEST_BATCH_MAGIC || version != GUEST_BATCH_VERSION || flags || reserved ||
        recordBytes != bytes - GUEST_BATCH_HEADER_BYTES || !recordCount) {
        return D3D9WEB_GUEST_BATCH_REJECT;
    }

    U32 offset = GUEST_BATCH_HEADER_BYTES;
    for (U32 record = 0; record < recordCount; ++record) {
        if (offset > bytes || bytes - offset < sizeof(U32)) return D3D9WEB_GUEST_BATCH_REJECT;
        U32 packetBytes = 0;
        memcpy(&packetBytes, commandBytes.data() + offset, sizeof(packetBytes));
        offset += sizeof(packetBytes);
        if (packetBytes < sizeof(U32) || packetBytes > MAX_COMMAND_BYTES ||
            offset > bytes || packetBytes > bytes - offset) {
            return D3D9WEB_GUEST_BATCH_REJECT;
        }
        U32 opcode = 0;
        memcpy(&opcode, commandBytes.data() + offset, sizeof(opcode));
        if (!canBatch(opcode) || opcode == 11u || isSynchronousTail(opcode)) {
            return D3D9WEB_GUEST_BATCH_REJECT;
        }
        offset += packetBytes;
    }
    if (offset != bytes || recordBytes > MAX_BATCH_BYTES) return D3D9WEB_GUEST_BATCH_REJECT;

    if (!batchedCommandBytes.empty() &&
        (U64)batchedCommandBytes.size() + recordBytes > MAX_BATCH_BYTES) {
        if (flushBatch(30u) != D3D9WEB_OK) return D3D9WEB_GUEST_BATCH_REJECT;
    }
    if ((U64)batchedCommandBytes.size() + recordBytes > MAX_BATCH_BYTES) {
        return D3D9WEB_GUEST_BATCH_REJECT;
    }
    const size_t destination = batchedCommandBytes.size();
    batchedCommandBytes.resize(destination + recordBytes);
    memcpy(batchedCommandBytes.data() + destination,
           commandBytes.data() + GUEST_BATCH_HEADER_BYTES, recordBytes);
#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
    intCensus.guestBatchRecords += recordCount;
    intCensus.guestBatchRecordBytes += recordBytes;
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    int9AJoinedWindow.guestBatchRecords += recordCount;
    int9AJoinedWindow.guestBatchBytes += recordBytes;
#endif
    return D3D9WEB_OK;
}

static U32 dispatchGeneric(CPU* cpu, U32 source, U32 bytes) {
    if (!source || !bytes || bytes > MAX_COMMAND_BYTES || source > 0xffffffffu - bytes) {
        return 0;
    }
    commandBytes.resize(bytes);
    cpu->memory->memcpy(commandBytes.data(), source, bytes);

    U32 opcode = 0;
    if (bytes >= sizeof(opcode)) {
        memcpy(&opcode, commandBytes.data(), sizeof(opcode));
    }
#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
    intCensus.base2Calls++;
    intCensus.base2Bytes += bytes;
    if (opcode < 25u) intCensus.opcodeCalls[opcode]++;
    const bool hadGenericPending = intCensus.genericPending;
    if (isSynchronousTail(opcode) || opcode == 11u || !canBatch(opcode)) {
        if (hadGenericPending) {
            intCensus.terminalWithPending++;
            if (opcode < 25u) intCensus.terminalPendingByOpcode[opcode]++;
        }
        intCensus.genericPending = false;
        intCensus.genericPendingBytes = 0;
    } else {
        if (!hadGenericPending) intCensus.genericRuns++;
        intCensus.genericPending = true;
        intCensus.genericPendingBytes += sizeof(U32) + bytes;
        if (intCensus.genericPendingBytes > intCensus.maxGenericPendingBytes) {
            intCensus.maxGenericPendingBytes = intCensus.genericPendingBytes;
        }
    }
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    int9AJoinedWindow.genericCalls++;
    int9AJoinedWindow.genericBytes += bytes;
    if (opcode < 25u) {
        int9AJoinedWindow.genericOpcodeCalls[opcode]++;
    } else {
        int9AJoinedWindow.state |= INT9A_JOINED_SUBMIT_MISMATCH;
    }
    int9AJoinedWindow.genericRuns++;
#endif
    if (isSynchronousTail(opcode)) {
#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
        U64 presentToTailMicros = ~0ull;
#endif
        U32 packetOffset = 0;
        if (!reserveBatchPacket(bytes, packetOffset)) {
            return 0;
        }
        memcpy(batchedCommandBytes.data() + packetOffset + sizeof(U32), commandBytes.data(), bytes);
#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
        presentToTailMicros = beginNextSynchronousTail();
#endif
        const U32 result = flushBatch(opcode);
#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
        if (result == D3D9WEB_OK) {
            commitNextSynchronousTail(opcode, presentToTailMicros);
        }
#endif
        return result;
    }
    if (!canBatch(opcode)) {
        U32 result = flushBatch(opcode);
        return result == D3D9WEB_OK
            ? dispatchCommandOnMainThread((U32)commandBytes.data(), bytes, opcode)
            : result;
    }

    U32 packetOffset = 0;
    if (!reserveBatchPacket(bytes, packetOffset)) {
        return 0;
    }
    memcpy(batchedCommandBytes.data() + packetOffset + sizeof(U32), commandBytes.data(), bytes);
    if (opcode == 11u) {
#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
        const double presentStartMs = emscripten_get_now();
#endif
        const U32 result = flushBatch(opcode);
#ifdef BOXEDWINE_D3D9WEB_ASYNC_GAP_PROFILE
        const double presentEndMs = emscripten_get_now();
        if (result == D3D9WEB_OK) {
            recordPresentWait(presentStartMs, presentEndMs);
        }
#endif
#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
        intCensus.presents++;
        if (intCensus.presents == 120u) {
            klog_fmt("D3D9WEB_INT_CENSUS presents=120 base2_calls=%llu base2_bytes=%llu batch_calls=%llu batch_bytes=%llu",
                (unsigned long long)intCensus.base2Calls,
                (unsigned long long)intCensus.base2Bytes,
                (unsigned long long)intCensus.batchCalls,
                (unsigned long long)intCensus.batchBytes);
            klog_fmt("D3D9WEB_INT_INDEX presents=120 i0=%llu/%llu i1=%llu/%llu i2=%llu/%llu i3=%llu/%llu i4=%llu/%llu i5=%llu/%llu direct3_pending=%llu direct4_pending=%llu terminal_pending=%llu",
                (unsigned long long)intCensus.indexCalls[0], (unsigned long long)intCensus.indexBytes[0],
                (unsigned long long)intCensus.indexCalls[1], (unsigned long long)intCensus.indexBytes[1],
                (unsigned long long)intCensus.indexCalls[2], (unsigned long long)intCensus.indexBytes[2],
                (unsigned long long)intCensus.indexCalls[3], (unsigned long long)intCensus.indexBytes[3],
                (unsigned long long)intCensus.indexCalls[4], (unsigned long long)intCensus.indexBytes[4],
                (unsigned long long)intCensus.indexCalls[5], (unsigned long long)intCensus.indexBytes[5],
                (unsigned long long)intCensus.directWithPending[0],
                (unsigned long long)intCensus.directWithPending[1],
                (unsigned long long)intCensus.terminalWithPending);
            klog_fmt("D3D9WEB_INT_RUNS presents=120 generic_runs=%llu max_pending_bytes=%llu term1=%llu term11=%llu term12=%llu term13=%llu term20=%llu",
                (unsigned long long)intCensus.genericRuns,
                (unsigned long long)intCensus.maxGenericPendingBytes,
                (unsigned long long)intCensus.terminalPendingByOpcode[1],
                (unsigned long long)intCensus.terminalPendingByOpcode[11],
                (unsigned long long)intCensus.terminalPendingByOpcode[12],
                (unsigned long long)intCensus.terminalPendingByOpcode[13],
                (unsigned long long)intCensus.terminalPendingByOpcode[20]);
            klog_fmt("D3D9WEB_INT_GUEST_BATCH presents=120 records=%llu record_bytes=%llu",
                (unsigned long long)intCensus.guestBatchRecords,
                (unsigned long long)intCensus.guestBatchRecordBytes);
            klog_fmt("D3D9WEB_INT_OPCODES presents=120 op1=%llu op2=%llu op3=%llu op4=%llu op5=%llu op6=%llu op7=%llu op8=%llu op9=%llu op10=%llu op11=%llu op12=%llu op13=%llu op14=%llu op15=%llu op16=%llu op17=%llu op18=%llu op19=%llu op20=%llu op21=%llu op22=%llu op23=%llu op24=%llu",
                (unsigned long long)intCensus.opcodeCalls[1], (unsigned long long)intCensus.opcodeCalls[2],
                (unsigned long long)intCensus.opcodeCalls[3], (unsigned long long)intCensus.opcodeCalls[4],
                (unsigned long long)intCensus.opcodeCalls[5], (unsigned long long)intCensus.opcodeCalls[6],
                (unsigned long long)intCensus.opcodeCalls[7], (unsigned long long)intCensus.opcodeCalls[8],
                (unsigned long long)intCensus.opcodeCalls[9], (unsigned long long)intCensus.opcodeCalls[10],
                (unsigned long long)intCensus.opcodeCalls[11], (unsigned long long)intCensus.opcodeCalls[12],
                (unsigned long long)intCensus.opcodeCalls[13], (unsigned long long)intCensus.opcodeCalls[14],
                (unsigned long long)intCensus.opcodeCalls[15], (unsigned long long)intCensus.opcodeCalls[16],
                (unsigned long long)intCensus.opcodeCalls[17], (unsigned long long)intCensus.opcodeCalls[18],
                (unsigned long long)intCensus.opcodeCalls[19], (unsigned long long)intCensus.opcodeCalls[20],
                (unsigned long long)intCensus.opcodeCalls[21], (unsigned long long)intCensus.opcodeCalls[22],
                (unsigned long long)intCensus.opcodeCalls[23], (unsigned long long)intCensus.opcodeCalls[24]);
            intCensus = IntCensus{};
        }
#endif
        return result;
    }
    return D3D9WEB_OK;
}
} // namespace
#endif

#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB) && defined(BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS)
extern "C" void d3d9WebInt9AJoinedCensusRecordGenerated(CPU* cpu, U32 opcode) {
    if (!cpu) {
        return;
    }
    ++int9AJoinedWindow.fn8Total;
    if (opcode == Int9A) {
        ++int9AJoinedWindow.fn8Int9A;
    } else if (opcode >= InstructionCount) {
        ++int9AJoinedWindow.fn8Invalid;
    }
    if (int9AJoinedWindow.owner && int9AJoinedWindow.owner != cpu) {
        int9AJoinedWindow.state |= INT9A_JOINED_OWNER_MISMATCH;
    }
}

extern "C" U32 d3d9WebInt9AJoinedCensusGetAbiVersion() {
    return INT9A_JOINED_ABI;
}

extern "C" U32 d3d9WebInt9AJoinedCensusGetSnapshotBytes() {
    return sizeof(Int9AJoinedCensusSnapshot);
}

extern "C" void d3d9WebInt9AJoinedCensusCopySnapshot(Int9AJoinedCensusSnapshot* snapshot) {
    if (!snapshot) {
        return;
    }
    std::lock_guard<std::mutex> lock(int9AJoinedSealedMutex);
    *snapshot = int9AJoinedSealed;
}
#endif

#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB) && defined(BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS)
extern "C" U32 d3d9WebInt9ASequenceCensusGetAbiVersion() {
    return INT9A_SEQUENCE_ABI;
}

extern "C" U32 d3d9WebInt9ASequenceCensusGetSnapshotBytes() {
    return sizeof(Int9ASequenceCensusSnapshot);
}

extern "C" void d3d9WebInt9ASequenceCensusCopySnapshot(Int9ASequenceCensusSnapshot* snapshot) {
    if (!snapshot) {
        return;
    }
    std::lock_guard<std::mutex> lock(int9ASequenceSealedMutex);
    *snapshot = int9ASequenceSealed;
}
#endif

void callD3D9Web(CPU* cpu, U32 index) {
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_D3D9WEB)
    if (index < D3D9WEB_INDEX_BASE || index > D3D9WEB_INDEX_BASE + 5u) {
        return;
    }
    index -= D3D9WEB_INDEX_BASE;
    const U32 arg1 = cpu->peek32(1);
    const U32 arg2 = cpu->peek32(2);
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    int9AJoinedEnsureOwner(cpu);
    int9AJoinedWindow.indexCalls[index]++;
    int9AJoinedWindow.indexBytes[index] += arg2;
#endif
#ifdef BOXEDWINE_D3D9WEB_INT_CENSUS
    intCensus.indexCalls[index]++;
    intCensus.indexBytes[index] += arg2;
    if ((index == 3u || index == 4u) && intCensus.genericPending) {
        intCensus.directWithPending[index - 3u]++;
        intCensus.genericPending = false;
        intCensus.genericPendingBytes = 0;
    }
#endif
#if defined(BOXEDWINE_D3D9WEB_BRIDGE_PROFILE) || defined(BOXEDWINE_AZROK_WAIT_FRAME_PROFILE) || defined(BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS) || defined(BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS)
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const double callStartMs = emscripten_get_now();
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    const double int9AJoinedCallStartMs = emscripten_get_now();
#endif
    const bool isPresent = index == 2u && arg1 && arg2 >= sizeof(U32) &&
        cpu->memory->readd(arg1) == 11u;
#endif

    switch (index) {
    case 0u:
        EAX = 0x1234u;
        break;
    case 1u:
        EAX = 0x3001u;
        break;
    case 2u:
        EAX = dispatchGeneric(cpu, arg1, arg2);
        break;
    case 3u:
        EAX = enqueueDirectUpload(cpu, arg1, arg2);
        break;
    case 4u:
        EAX = enqueueDirectDrawPrimitiveUP(cpu, arg1, arg2);
        break;
    case 5u:
        EAX = enqueueGuestBatch(cpu, arg1, arg2);
        break;
    }
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    if (EAX == D3D9WEB_OK) {
        int9ASequenceOnSuccessfulRoute(index);
    }
#endif
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
    if (isPresent) presentAzrokWaitFrameProfile(cpu);
#endif
#ifdef BOXEDWINE_D3D9WEB_BRIDGE_PROFILE
    const double callEndMs = emscripten_get_now();
    bridgeFrameProfile.calls[index] += 1;
    bridgeFrameProfile.callMicros[index] +=
        callEndMs > callStartMs ? (U64)((callEndMs - callStartMs) * 1000.0 + 0.5) : 0;
    if (isPresent) {
        const U64 currentKernelMicros = cpu->thread->kernelTime;
        if (bridgeFrameProfile.lastPresentMs == 0.0) {
            bridgeFrameProfile.lastPresentMs = callEndMs;
            bridgeFrameProfile.lastKernelMicros = currentKernelMicros;
            resetBridgeWindow(true);
        } else {
            const double frameMs = callEndMs - bridgeFrameProfile.lastPresentMs;
            bridgeFrameProfile.lastPresentMs = callEndMs;
            if (frameMs > 0.0) {
                bridgeFrameProfile.frameWallMicros += (U64)(frameMs * 1000.0 + 0.5);
            }
            if (currentKernelMicros >= bridgeFrameProfile.lastKernelMicros) {
                bridgeFrameProfile.kernelMicros += currentKernelMicros - bridgeFrameProfile.lastKernelMicros;
            }
            bridgeFrameProfile.lastKernelMicros = currentKernelMicros;
            bridgeFrameProfile.frames += 1;
            if (bridgeFrameProfile.frames == 120u) {
                const U64 bridgeCalls = bridgeFrameProfile.calls[0] + bridgeFrameProfile.calls[1] +
                    bridgeFrameProfile.calls[2] + bridgeFrameProfile.calls[3] + bridgeFrameProfile.calls[4] +
                    bridgeFrameProfile.calls[5];
                const U64 bridgeMicros = bridgeFrameProfile.callMicros[0] + bridgeFrameProfile.callMicros[1] +
                    bridgeFrameProfile.callMicros[2] + bridgeFrameProfile.callMicros[3] + bridgeFrameProfile.callMicros[4] +
                    bridgeFrameProfile.callMicros[5];
                klog_fmt("D3D9WEB_BRIDGE_FRAME_PROFILE frames=120 wall_us=%llu kernel_us=%llu bridge_calls=%llu bridge_us=%llu main_roundtrips=%llu main_wait_us=%llu probe_calls=%llu probe_us=%llu generic_calls=%llu generic_us=%llu upload_calls=%llu upload_us=%llu drawup_calls=%llu drawup_us=%llu",
                    (unsigned long long)bridgeFrameProfile.frameWallMicros,
                    (unsigned long long)bridgeFrameProfile.kernelMicros,
                    (unsigned long long)bridgeCalls,
                    (unsigned long long)bridgeMicros,
                    (unsigned long long)bridgeFrameProfile.mainRoundTrips,
                    (unsigned long long)bridgeFrameProfile.mainWaitMicros,
                    (unsigned long long)(bridgeFrameProfile.calls[0] + bridgeFrameProfile.calls[1]),
                    (unsigned long long)(bridgeFrameProfile.callMicros[0] + bridgeFrameProfile.callMicros[1]),
                    (unsigned long long)bridgeFrameProfile.calls[2],
                    (unsigned long long)bridgeFrameProfile.callMicros[2],
                    (unsigned long long)bridgeFrameProfile.calls[3],
                    (unsigned long long)bridgeFrameProfile.callMicros[3],
                    (unsigned long long)bridgeFrameProfile.calls[4],
                    (unsigned long long)bridgeFrameProfile.callMicros[4]);
                U64 accountedCalls = 0;
                U64 accountedWaitMicros = 0;
                for (U32 mode = 0; mode < 2u; ++mode) {
                    for (U32 reason = 0; reason < 32u; ++reason) {
                        const U64 calls = bridgeFrameProfile.mainReasonCalls[mode][reason];
                        if (!calls) {
                            continue;
                        }
                        const U64 waitMicros = bridgeFrameProfile.mainReasonWaitMicros[mode][reason];
                        accountedCalls += calls;
                        accountedWaitMicros += waitMicros;
                        klog_fmt("D3D9WEB_MAIN_REASON frames=120 mode=%s reason_id=%u calls=%llu wait_us=%llu",
                            mode == 0u ? "direct" : "batch",
                            reason,
                            (unsigned long long)calls,
                            (unsigned long long)waitMicros);
                    }
                }
                klog_fmt("D3D9WEB_MAIN_REASON_TOTAL accounted_calls=%llu accounted_wait_us=%llu expected_calls=%llu expected_wait_us=%llu",
                    (unsigned long long)accountedCalls,
                    (unsigned long long)accountedWaitMicros,
                    (unsigned long long)bridgeFrameProfile.mainRoundTrips,
                    (unsigned long long)bridgeFrameProfile.mainWaitMicros);
                resetBridgeWindow(true);
            }
        }
    }
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_JOINED_CENSUS
    const double int9AJoinedCallEndMs = emscripten_get_now();
    int9AJoinedWindow.bridgeCalls[index]++;
    int9AJoinedWindow.bridgeMicros[index] += int9AJoinedCallEndMs > int9AJoinedCallStartMs ?
        (U64)((int9AJoinedCallEndMs - int9AJoinedCallStartMs) * 1000.0 + 0.5) : 0;
    if (isPresent) {
        if (EAX != D3D9WEB_OK || asyncSubmittedSequence == 0u) {
            int9AJoinedWindow.state |= INT9A_JOINED_ASYNC_ERROR;
        } else {
            int9AJoinedOnPresentSubmitted(cpu, asyncSubmittedSequence);
        }
    }
#endif
#ifdef BOXEDWINE_WASM_JIT_INT9A_SEQUENCE_CENSUS
    if (isPresent) {
        if (EAX != D3D9WEB_OK || asyncSubmittedSequence == 0u) {
            int9ASequenceWindow.state |= INT9A_SEQUENCE_ASYNC_ERROR;
        } else {
            int9ASequenceOnPresentSubmitted(asyncSubmittedSequence);
        }
    }
#endif
#else
    (void)cpu;
    (void)index;
#endif
}
