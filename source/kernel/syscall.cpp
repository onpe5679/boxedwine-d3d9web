/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#include "log.h"
#include "kscheduler.h"
#include "ksignal.h"
#include "ksocket.h"
#include "kepoll.h"
#include "kevent.h"
#include "kwaitframeprofile.h"

#include <random>
#include <new>
#include <thread>
#if defined(BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE) && defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

U64 sysCallTime;
extern struct Block emptyBlock;

#define ARG1 EBX
#define ARG2 ECX
#define ARG3 EDX
#define ARG4 ESI
#define ARG5 EDI
#define ARG6 EBP

#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
namespace {
constexpr U64 AZROK_BLOCKING_READ_MIN_US = 1000;
constexpr U32 AZROK_WAIT_PROFILE_PRESENTS = 120;
constexpr U32 AZROK_WINE_REQUEST_LIMIT = 306;
#if defined(BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN) && !defined(BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE)
#error "WIS handle join requires BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE"
#endif
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
constexpr U32 AZROK_WINE_CREATE_FILE_REQUEST = 44;
constexpr U32 AZROK_WINE_GET_HANDLE_FD_REQUEST = 48;
constexpr U32 AZROK_WINE_CLOSE_HANDLE_REQUEST = 21;
constexpr U32 AZROK_WINE_FIRST_USER_HANDLE = 0x0020;
constexpr U32 AZROK_WINE_LAST_USER_HANDLE = 0xffef;
constexpr U32 AZROK_WIS_LOAD_READ_CALLS = 6;
constexpr U64 AZROK_WIS_LOAD_READ_BYTES = 130580;
constexpr U64 AZROK_WIS_HANDLE_JOIN_TIMEOUT_US = 2000000;
constexpr U32 AZROK_WIS_HANDLE_JOIN_HISTORY = 16;
#endif
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
constexpr U32 AZROK_WAIT_PUBLISH_ABI_VERSION = 1;
constexpr U32 AZROK_WAIT_PUBLISH_CAPACITY = 65536;
#endif

struct AzrokWaitBucket {
    U64 calls = 0;
    U64 requestedKnownCalls = 0;
    U64 requestedMicros = 0;
    U64 actualMicros = 0;
    U64 timeoutCalls = 0;
    U64 oversleepMicros = 0;
    U64 maxActualMicros = 0;
    U64 maxOversleepMicros = 0;
};

struct AzrokWaitFrameProfile {
    AzrokWaitBucket buckets[K_WAIT_FRAME_KIND_COUNT] = {};
    U64 blockingReadCalls = 0;
    U64 blockingReadMicros = 0;
    U64 maxBlockingReadMicros = 0;
    U64 markedWaitCalls = 0;
    U64 markedWaitCompleted = 0;
    U64 markedWaitRequestedKnown = 0;
    U64 markedWaitRequestedMicros = 0;
    U64 markedWaitReadCalls = 0;
    U64 markedWaitReadMicros = 0;
    U64 markedWaitMaxReadMicros = 0;
    U64 markedWaitTimeoutResults = 0;
    U64 markedWaitSignaledResults = 0;
    U64 markedWaitFailedResults = 0;
    struct Caller {
        U32 address = 0;
        U64 calls = 0;
        U64 requestedKnown = 0;
        U64 requestedMicros = 0;
        U64 readCalls = 0;
        U64 readMicros = 0;
        U64 maxReadMicros = 0;
    } callers[8] = {};
    struct ReadSource {
        U32 fd = 0xffffffffu;
        U32 syscallEip = 0;
        U32 objectType = 0xffffffffu;
        BString identity;
        U64 blockingCalls = 0;
        U64 blockingMicros = 0;
        U64 maxBlockingMicros = 0;
        U64 requestedBytes = 0;
        U64 positiveBytes = 0;
        U64 zeroCalls = 0;
        U64 errorCalls = 0;
        U64 len12Calls = 0;
        U64 len12Micros = 0;
        U64 len64Calls = 0;
        U64 len64Micros = 0;
        U64 otherLenCalls = 0;
        U64 otherLenMicros = 0;
    } readSources[16] = {};
    struct WineRequest {
        U32 requestFd = 0xffffffffu;
        U64 blockingCalls = 0;
        U64 blockingMicros = 0;
        U64 maxBlockingMicros = 0;
    } wineRequests[AZROK_WINE_REQUEST_LIMIT] = {};
    U64 readSourceOverflowCalls = 0;
    U64 readSourceOverflowMicros = 0;
    U64 readSourceOverflowMaxMicros = 0;
    bool readSourceOverflow = false;
    bool markedWaitActive = false;
    U32 markedWaitCaller = 0;
    U32 markedWaitTimeoutMillis = 0xffffffffu;
    U32 presents = 0;
    bool armed = false;
    U32 startJitBlockDispatches = 0;
    U32 startWarmupBlockDispatches = 0;
    U32 startPendingBlockDispatches = 0;
    U32 startNoJitBlockDispatches = 0;
    U64 startWallMicros = 0;
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    struct CausalWis {
        U64 calls = 0;
        U64 bytes = 0;
        U64 syscallMicros = 0;
        U64 stages[K_AZROK_CAUSAL_WIS_STAGE_COUNT] = {};
        U64 backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_COUNT] = {};
        U64 cacheLookupHits = 0;
        U64 cacheLookupMisses = 0;
        U64 leafAccountedMicros = 0;
        U64 residualMicros = 0;
        U64 leafOverrunMicros = 0;
    } causalWis;
    struct CausalWine {
        U64 calls = 0;
        U64 publishToClientReturnMicros = 0;
        U64 maxPublishToClientReturnMicros = 0;
    } causalWine[AZROK_WINE_REQUEST_LIMIT] = {};
    U64 causalWisContextOverlap = 0;
    U64 causalWisFinishUnmatched = 0;
    U64 causalWisCrossWindow = 0;
    U64 causalWinePublishOverflow = 0;
    U64 causalWinePublishMissing = 0;
    U64 causalWinePublishPartial = 0;
    U64 causalWineReplyUnmatched = 0;
    U64 causalWineRequestMismatch = 0;
    U64 causalWineCrossWindow = 0;
    U64 causalWineOutstandingAtBoundary = 0;
    U64 causalStageClockCalls = 0;
    U64 causalTimedIntervals = 0;
    U64 causalClockProbePairs = 0;
    U64 causalClockProbeMicros = 0;
    U64 causalClockProbeMinMicros = 0xffffffffffffffffULL;
    U64 causalClockProbeMaxMicros = 0;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    struct CausalWisHandleJoin {
        U64 loads = 0;
        U64 createCalls = 0;
        U64 createMicros = 0;
        U64 getFdCalls = 0;
        U64 getFdMicros = 0;
        U64 closeCalls = 0;
        U64 closeMicros = 0;
        U64 unionMicros = 0;
        U64 ambiguous = 0;
        U64 unmatched = 0;
        U64 timeouts = 0;
        U64 interleaved = 0;
        U64 handleParseFailures = 0;
        U64 shapeMismatches = 0;
        U64 crossWindow = 0;
    } causalWisHandleJoin;
#endif
#endif
};

thread_local AzrokWaitFrameProfile azrokWaitFrameProfile;
thread_local U32 azrokLastWineServerRequest = 0xffffffffu;
thread_local U32 azrokLastWineServerRequestFd = 0xffffffffu;
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
struct AzrokCausalWisContext {
    bool active = false;
    U64 windowSerial = 0;
    U64 syscallStartMicros = 0;
    U64 stages[K_AZROK_CAUSAL_WIS_STAGE_COUNT] = {};
    U64 backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_COUNT] = {};
    U64 cacheLookupHits = 0;
    U64 cacheLookupMisses = 0;
};

struct AzrokCausalWineContext {
    bool prepared = false;
    bool published = false;
    U32 requestId = 0xffffffffu;
    U32 requestFd = 0xffffffffu;
    U32 expectedBytes = 0;
    U64 publishMicros = 0;
    U64 windowSerial = 0;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    KThread* clientThread = nullptr;
    U32 requestHandle = 0;
    bool requestHandleKnown = false;
#endif
};

thread_local AzrokCausalWisContext azrokCausalWisContext;
thread_local AzrokCausalWineContext azrokCausalWineContext;
thread_local U64 azrokCausalWindowSerial = 0;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
struct AzrokCausalWineTransaction {
    bool valid = false;
    bool success = false;
    bool handleKnown = false;
    U32 requestId = 0xffffffffu;
    U32 handle = 0;
    U64 elapsedMicros = 0;
    U64 ordinal = 0;
    U64 completedMicros = 0;
    U64 windowSerial = 0;
    KThread* clientThread = nullptr;
};

struct AzrokCausalWisHandleLoad {
    bool active = false;
    bool matched = false;
    bool fdClosed = false;
    bool closeSeen = false;
    U32 fd = 0xffffffffu;
    const KObject* object = nullptr;
    BString path;
    U32 reads = 0;
    U64 bytes = 0;
    U64 windowSerial = 0;
    U64 firstReadMicros = 0;
    U64 fdCloseMicros = 0;
    KThread* clientThread = nullptr;
    AzrokCausalWineTransaction create;
    AzrokCausalWineTransaction getFd;
    AzrokCausalWineTransaction close;
};

struct AzrokCausalWisHandleTracker {
    AzrokCausalWineTransaction recent[AZROK_WIS_HANDLE_JOIN_HISTORY] = {};
    U32 nextRecent = 0;
    U32 recentCount = 0;
    U64 nextOrdinal = 0;
    AzrokCausalWisHandleLoad load;
};

thread_local AzrokCausalWisHandleTracker azrokCausalWisHandleTracker;
#endif
#endif

#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
struct alignas(4) AzrokWaitProfilePublishedText {
    std::atomic<U32> sequence{0};
    std::atomic<U32> bytes{0};
    alignas(4) char data[AZROK_WAIT_PUBLISH_CAPACITY] = {};
};

static_assert(sizeof(std::atomic<U32>) == sizeof(U32),
    "wait-profile seqlock must be one shared U32");
static_assert(alignof(AzrokWaitProfilePublishedText) >= alignof(U32),
    "wait-profile publication must be U32 aligned");

std::atomic<AzrokWaitProfilePublishedText*> azrokWaitProfilePublishedText{nullptr};

AzrokWaitProfilePublishedText* ensureAzrokWaitProfilePublishedText() {
    AzrokWaitProfilePublishedText* published =
        azrokWaitProfilePublishedText.load(std::memory_order_acquire);
    if (published) return published;
    AzrokWaitProfilePublishedText* candidate =
        new (std::nothrow) AzrokWaitProfilePublishedText();
    if (!candidate) return nullptr;
    if (!azrokWaitProfilePublishedText.compare_exchange_strong(published, candidate,
            std::memory_order_release, std::memory_order_acquire)) {
        delete candidate;
    } else {
        published = candidate;
    }
    return published;
}

class AzrokWaitProfileTextWriter {
public:
    AzrokWaitProfileTextWriter() {
        published = azrokWaitProfilePublishedText.load(std::memory_order_acquire);
        if (!published) return;
        U32 prior = published->sequence.load(std::memory_order_relaxed);
        if (prior & 1u) ++prior;
        evenSequence = prior + 2u;
        published->sequence.store(prior + 1u, std::memory_order_release);
        appendLine("AZROK_WAIT_SNAPSHOT_META abi=1 truncated=0");
        const char* marker = strstr(published->data, "truncated=0");
        if (marker) truncatedDigitOffset = (U32)(marker - published->data) + 10u;
    }

    template <typename... Args>
    void appendLine(const char* format, Args... args) {
        if (!published) return;
        if (truncated || position >= AZROK_WAIT_PUBLISH_CAPACITY - 1u) {
            truncated = true;
            return;
        }
        const U32 remaining = AZROK_WAIT_PUBLISH_CAPACITY - position;
        const int written = snprintf(published->data + position,
            remaining, format, args...);
        if (written < 0 || (U32)written >= remaining) {
            truncated = true;
            position = AZROK_WAIT_PUBLISH_CAPACITY - 1u;
            published->data[position] = 0;
            return;
        }
        position += (U32)written;
        if (position >= AZROK_WAIT_PUBLISH_CAPACITY - 1u) {
            truncated = true;
            return;
        }
        published->data[position++] = '\n';
        published->data[position] = 0;
    }

    void publish() {
        if (!published) return;
        if (truncatedDigitOffset < AZROK_WAIT_PUBLISH_CAPACITY) {
            published->data[truncatedDigitOffset] = truncated ? '1' : '0';
        }
        published->bytes.store(position, std::memory_order_relaxed);
        published->sequence.store(evenSequence, std::memory_order_release);
    }

private:
    AzrokWaitProfilePublishedText* published = nullptr;
    U32 position = 0;
    U32 truncatedDigitOffset = 0xffffffffu;
    U32 evenSequence = 0;
    bool truncated = false;
};
#else
struct alignas(4) AzrokWaitProfilePublishedText {
    std::atomic<U32> sequence{0};
    U32 bytes = 0;
    alignas(4) char data[AZROK_WAIT_PUBLISH_CAPACITY] = {};
};

static_assert(sizeof(std::atomic<U32>) == sizeof(U32),
    "wait-profile seqlock must be one shared U32");
static_assert(alignof(AzrokWaitProfilePublishedText) >= alignof(U32),
    "wait-profile publication must be U32 aligned");

AzrokWaitProfilePublishedText azrokWaitProfilePublishedText;

class AzrokWaitProfileTextWriter {
public:
    AzrokWaitProfileTextWriter() {
        U32 prior = azrokWaitProfilePublishedText.sequence.load(std::memory_order_relaxed);
        if (prior & 1u) ++prior;
        evenSequence = prior + 2u;
        azrokWaitProfilePublishedText.sequence.store(prior + 1u, std::memory_order_release);
        appendLine("AZROK_WAIT_SNAPSHOT_META abi=1 truncated=0");
        const char* marker = strstr(azrokWaitProfilePublishedText.data, "truncated=0");
        if (marker) truncatedDigitOffset = (U32)(marker - azrokWaitProfilePublishedText.data) + 10u;
    }

    template <typename... Args>
    void appendLine(const char* format, Args... args) {
        if (truncated || position >= AZROK_WAIT_PUBLISH_CAPACITY - 1u) {
            truncated = true;
            return;
        }
        const U32 remaining = AZROK_WAIT_PUBLISH_CAPACITY - position;
        const int written = snprintf(azrokWaitProfilePublishedText.data + position,
            remaining, format, args...);
        if (written < 0 || (U32)written >= remaining) {
            truncated = true;
            position = AZROK_WAIT_PUBLISH_CAPACITY - 1u;
            azrokWaitProfilePublishedText.data[position] = 0;
            return;
        }
        position += (U32)written;
        if (position >= AZROK_WAIT_PUBLISH_CAPACITY - 1u) {
            truncated = true;
            return;
        }
        azrokWaitProfilePublishedText.data[position++] = '\n';
        azrokWaitProfilePublishedText.data[position] = 0;
    }

    void publish() {
        if (truncatedDigitOffset < AZROK_WAIT_PUBLISH_CAPACITY) {
            azrokWaitProfilePublishedText.data[truncatedDigitOffset] = truncated ? '1' : '0';
        }
        azrokWaitProfilePublishedText.bytes = position;
        azrokWaitProfilePublishedText.sequence.store(evenSequence, std::memory_order_release);
    }

private:
    U32 position = 0;
    U32 truncatedDigitOffset = 0xffffffffu;
    U32 evenSequence = 0;
    bool truncated = false;
};
#endif
#endif

static bool canSafelyReadGuest(CPU* cpu, U32 address, U32 length) {
    if (!length) return true;
    if (address > 0xffffffffu - (length - 1)) return false;
    return cpu->memory->canRead(address, length);
}

#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
static bool isAzrokWisPath(const BString& path) {
    const char* value = path.c_str();
    const size_t length = strlen(value);
    if (length < 4) return false;
    const char* suffix = value + length - 4;
    return suffix[0] == '.' &&
        (suffix[1] == 'w' || suffix[1] == 'W') &&
        (suffix[2] == 'i' || suffix[2] == 'I') &&
        (suffix[3] == 's' || suffix[3] == 'S');
}

#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
static bool azrokCausalWisJoinTimedOut(U64 earlier, U64 later) {
    return later < earlier || later - earlier > AZROK_WIS_HANDLE_JOIN_TIMEOUT_US;
}

static bool isAzrokCausalWineUserHandle(U32 handle) {
    return handle >= AZROK_WINE_FIRST_USER_HANDLE &&
        handle <= AZROK_WINE_LAST_USER_HANDLE;
}

static void resetAzrokCausalWisHandleTracker() {
    azrokCausalWisHandleTracker = AzrokCausalWisHandleTracker{};
}

static const AzrokCausalWineTransaction* azrokCausalWisJoinRecent(U32 back) {
    const AzrokCausalWisHandleTracker& tracker = azrokCausalWisHandleTracker;
    if (back >= tracker.recentCount) return nullptr;
    const U32 index = (tracker.nextRecent + AZROK_WIS_HANDLE_JOIN_HISTORY - 1u - back) %
        AZROK_WIS_HANDLE_JOIN_HISTORY;
    return &tracker.recent[index];
}

static void rememberAzrokCausalWineTransaction(AzrokCausalWineTransaction transaction) {
    AzrokCausalWisHandleTracker& tracker = azrokCausalWisHandleTracker;
    transaction.ordinal = ++tracker.nextOrdinal;
    tracker.recent[tracker.nextRecent] = transaction;
    tracker.nextRecent = (tracker.nextRecent + 1u) % AZROK_WIS_HANDLE_JOIN_HISTORY;
    tracker.recentCount = std::min<U32>(tracker.recentCount + 1u,
        AZROK_WIS_HANDLE_JOIN_HISTORY);
}

static void rejectAzrokCausalWisHandleLoad(bool timeout, bool interleaved) {
    AzrokCausalWisHandleLoad& load = azrokCausalWisHandleTracker.load;
    if (!load.active || !load.matched) return;
    AzrokWaitFrameProfile::CausalWisHandleJoin& totals =
        azrokWaitFrameProfile.causalWisHandleJoin;
    totals.unmatched++;
    if (timeout) totals.timeouts++;
    if (interleaved) {
        totals.ambiguous++;
        totals.interleaved++;
    }
    load.matched = false;
}

static bool findAzrokCausalWisHandleChain(KThread* clientThread, U64 now,
        AzrokCausalWineTransaction& create, AzrokCausalWineTransaction& getFd) {
    const AzrokCausalWineTransaction* latest = azrokCausalWisJoinRecent(0);
    const AzrokCausalWineTransaction* previous = azrokCausalWisJoinRecent(1);
    if (!latest || !previous) return false;
    if (!latest->valid || !previous->valid || !latest->success || !previous->success ||
            latest->requestId != AZROK_WINE_GET_HANDLE_FD_REQUEST ||
            previous->requestId != AZROK_WINE_CREATE_FILE_REQUEST ||
            !latest->handleKnown || !previous->handleKnown ||
            latest->handle != previous->handle ||
            latest->clientThread != clientThread || previous->clientThread != clientThread ||
            latest->windowSerial != azrokCausalWindowSerial ||
            previous->windowSerial != azrokCausalWindowSerial ||
            latest->ordinal != previous->ordinal + 1u ||
            azrokCausalWisJoinTimedOut(previous->completedMicros, latest->completedMicros) ||
            azrokCausalWisJoinTimedOut(latest->completedMicros, now)) {
        return false;
    }
    create = *previous;
    getFd = *latest;
    return true;
}

static bool sameAzrokCausalWisLoad(const AzrokCausalWisHandleLoad& load,
        KThread* clientThread, U32 fd, const std::shared_ptr<KObject>& object) {
    return load.active && object && load.clientThread == clientThread && load.fd == fd &&
        load.object == object.get() && strcmp(load.path.c_str(), object->selfFd().c_str()) == 0;
}

static void finalizeAzrokCausalWisHandleLoad() {
    AzrokCausalWisHandleLoad& load = azrokCausalWisHandleTracker.load;
    if (!load.active || !load.matched || !load.fdClosed || !load.closeSeen ||
            load.reads != AZROK_WIS_LOAD_READ_CALLS || load.bytes != AZROK_WIS_LOAD_READ_BYTES) {
        return;
    }
    AzrokWaitFrameProfile::CausalWisHandleJoin& totals =
        azrokWaitFrameProfile.causalWisHandleJoin;
    totals.loads++;
    totals.createCalls++;
    totals.createMicros += load.create.elapsedMicros;
    totals.getFdCalls++;
    totals.getFdMicros += load.getFd.elapsedMicros;
    totals.closeCalls++;
    totals.closeMicros += load.close.elapsedMicros;
    totals.unionMicros += load.create.elapsedMicros + load.getFd.elapsedMicros +
        load.close.elapsedMicros;
    load = AzrokCausalWisHandleLoad{};
}

static void recordAzrokCausalWisHandleRead(KThread* clientThread, U32 fd,
        const std::shared_ptr<KObject>& object, U32 result, U64 now) {
    if (!azrokWaitFrameProfile.armed || !clientThread || !object ||
            object->type != KTYPE_FILE || !isAzrokWisPath(object->selfFd())) {
        return;
    }
    AzrokCausalWisHandleTracker& tracker = azrokCausalWisHandleTracker;
    AzrokCausalWisHandleLoad& current = tracker.load;
    if (current.active && !sameAzrokCausalWisLoad(current, clientThread, fd, object)) {
        AzrokWaitFrameProfile::CausalWisHandleJoin& totals =
            azrokWaitFrameProfile.causalWisHandleJoin;
        if (current.matched) totals.unmatched++;
        totals.ambiguous++;
        totals.interleaved++;
        current = AzrokCausalWisHandleLoad{};
    }
    if (!current.active) {
        current.active = true;
        current.fd = fd;
        current.object = object.get();
        current.path = object->selfFd();
        current.windowSerial = azrokCausalWindowSerial;
        current.firstReadMicros = now;
        current.clientThread = clientThread;
        if (findAzrokCausalWisHandleChain(clientThread, now, current.create, current.getFd)) {
            current.matched = true;
        } else {
            azrokWaitFrameProfile.causalWisHandleJoin.unmatched++;
        }
    }
    current.reads++;
    if ((S32)result > 0) current.bytes += result;
    if (current.matched && ((S32)result <= 0 ||
            current.reads > AZROK_WIS_LOAD_READ_CALLS ||
            current.bytes > AZROK_WIS_LOAD_READ_BYTES)) {
        azrokWaitFrameProfile.causalWisHandleJoin.shapeMismatches++;
        rejectAzrokCausalWisHandleLoad(false, false);
    }
}

static void recordAzrokCausalWisFdClose(KThread* clientThread, U32 fd,
        const std::shared_ptr<KObject>& object, bool success, U64 now) {
    if (!success || !clientThread || !object || object->type != KTYPE_FILE ||
            !isAzrokWisPath(object->selfFd())) {
        return;
    }
    AzrokCausalWisHandleLoad& load = azrokCausalWisHandleTracker.load;
    if (!sameAzrokCausalWisLoad(load, clientThread, fd, object)) {
        azrokWaitFrameProfile.causalWisHandleJoin.unmatched++;
        return;
    }
    if (!load.matched) {
        load = AzrokCausalWisHandleLoad{};
        return;
    }
    if (load.fdClosed) {
        rejectAzrokCausalWisHandleLoad(false, true);
        load = AzrokCausalWisHandleLoad{};
        return;
    }
    load.fdClosed = true;
    load.fdCloseMicros = now;
    if (load.reads != AZROK_WIS_LOAD_READ_CALLS || load.bytes != AZROK_WIS_LOAD_READ_BYTES) {
        azrokWaitFrameProfile.causalWisHandleJoin.shapeMismatches++;
        rejectAzrokCausalWisHandleLoad(false, false);
        load = AzrokCausalWisHandleLoad{};
        return;
    }
    if (azrokCausalWisJoinTimedOut(load.getFd.completedMicros, now)) {
        rejectAzrokCausalWisHandleLoad(true, false);
        load = AzrokCausalWisHandleLoad{};
        return;
    }
    finalizeAzrokCausalWisHandleLoad();
}

static void recordAzrokCausalWisWineTransaction(AzrokCausalWineTransaction transaction) {
    rememberAzrokCausalWineTransaction(transaction);
    const AzrokCausalWineTransaction* remembered = azrokCausalWisJoinRecent(0);
    if (!remembered) return;
    transaction = *remembered;
    if (!transaction.valid || (transaction.success && !transaction.handleKnown)) {
        azrokWaitFrameProfile.causalWisHandleJoin.handleParseFailures++;
    }
    AzrokCausalWisHandleLoad& load = azrokCausalWisHandleTracker.load;
    if (!load.active || !load.matched) return;
    if (transaction.windowSerial != load.windowSerial ||
            transaction.clientThread != load.clientThread) {
        rejectAzrokCausalWisHandleLoad(false, true);
        return;
    }
    if (transaction.requestId == AZROK_WINE_CLOSE_HANDLE_REQUEST &&
            transaction.success && transaction.handleKnown &&
            transaction.handle == load.getFd.handle && !load.closeSeen &&
            transaction.ordinal == load.getFd.ordinal + 1u &&
            !azrokCausalWisJoinTimedOut(load.getFd.completedMicros,
                transaction.completedMicros)) {
        load.closeSeen = true;
        load.close = transaction;
        if (load.fdClosed && azrokCausalWisJoinTimedOut(load.fdCloseMicros,
                transaction.completedMicros)) {
            rejectAzrokCausalWisHandleLoad(true, false);
            return;
        }
        finalizeAzrokCausalWisHandleLoad();
        return;
    }
    // Relevant transaction completion is the ordinal boundary. Any other
    // create/getfd/close while a WIS descriptor is live makes the association
    // non-unique, even when its handle differs.
    rejectAzrokCausalWisHandleLoad(false, true);
}

static void finishAzrokCausalWisHandleJoinWindow() {
    AzrokCausalWisHandleLoad& load = azrokCausalWisHandleTracker.load;
    if (load.active) {
        if (load.matched) azrokWaitFrameProfile.causalWisHandleJoin.unmatched++;
        azrokWaitFrameProfile.causalWisHandleJoin.crossWindow++;
    }
    resetAzrokCausalWisHandleTracker();
}
#endif

static bool beginAzrokCausalWis(const std::shared_ptr<KObject>& object) {
    if (!azrokWaitFrameProfile.armed || !object || object->type != KTYPE_FILE ||
            !isAzrokWisPath(object->selfFd())) {
        return false;
    }
    if (azrokCausalWisContext.active) {
        azrokWaitFrameProfile.causalWisContextOverlap++;
    }
    azrokCausalWisContext = AzrokCausalWisContext{};
    azrokCausalWisContext.active = true;
    azrokCausalWisContext.windowSerial = azrokCausalWindowSerial;
    azrokCausalWisContext.syscallStartMicros = azrokCausalProfileNow();
    return true;
}

static void prepareAzrokCausalWine(U32 requestId, U32 requestFd, U32 expectedBytes
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        , KThread* clientThread, bool requestHandleKnown, U32 requestHandle
#endif
        ) {
    if (!azrokWaitFrameProfile.armed) return;
    if (azrokCausalWineContext.prepared || azrokCausalWineContext.published) {
        azrokWaitFrameProfile.causalWinePublishOverflow++;
        azrokCausalWineContext = AzrokCausalWineContext{};
    }
    azrokCausalWineContext.prepared = true;
    azrokCausalWineContext.requestId = requestId;
    azrokCausalWineContext.requestFd = requestFd;
    azrokCausalWineContext.expectedBytes = expectedBytes;
    azrokCausalWineContext.windowSerial = azrokCausalWindowSerial;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    azrokCausalWineContext.clientThread = clientThread;
    azrokCausalWineContext.requestHandleKnown = requestHandleKnown;
    azrokCausalWineContext.requestHandle = requestHandle;
#endif
}
#endif

static bool captureWineRequestHeader(CPU* cpu, U32 fd, U32 address, U32 length,
                                     U32 expectedVariableBytes,
                                     KWaitFrameProfilePending& pending) {
    // Wine 11 sends the fixed generic request union as the first 64 bytes.
    // Validate its three-word header so unrelated 64-byte writes cannot
    // poison the following reply attribution.
    if (fd == 4 || !address || length != 64 || !canSafelyReadGuest(cpu, address, 12)) return false;
    KFileDescriptorPtr descriptor = cpu->thread->process->getFileDescriptor(fd);
    if (!descriptor || !descriptor->kobject || descriptor->kobject->type != KTYPE_UNIX_SOCKET) {
        return false;
    }
    const U32 requestId = cpu->memory->readd(address);
    const U32 requestSize = cpu->memory->readd(address + 4);
    const U32 replySize = cpu->memory->readd(address + 8);
    if (requestId >= AZROK_WINE_REQUEST_LIMIT || requestSize != expectedVariableBytes ||
            requestSize > 16 * 1024 * 1024 || replySize > 16 * 1024 * 1024) {
        return false;
    }
    pending.active = true;
    pending.wineRequestWrite = true;
    pending.wineRequestId = requestId;
    pending.wineRequestFd = fd;
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    bool requestHandleKnown = false;
    U32 requestHandle = 0;
    if ((requestId == AZROK_WINE_GET_HANDLE_FD_REQUEST ||
            requestId == AZROK_WINE_CLOSE_HANDLE_REQUEST) &&
            canSafelyReadGuest(cpu, address + 12u, 4u)) {
        requestHandle = cpu->memory->readd(address + 12u);
        requestHandleKnown = isAzrokCausalWineUserHandle(requestHandle);
    }
#endif
    prepareAzrokCausalWine(requestId, fd, length + expectedVariableBytes
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        , cpu->thread, requestHandleKnown, requestHandle
#endif
        );
#endif
    return true;
}

static U64 timespec32ToMicros(CPU* cpu, U32 address) {
    const U64 seconds = cpu->memory->readd(address);
    const U64 nanos = cpu->memory->readd(address + 4);
    return seconds * 1000000ULL + (nanos + 999ULL) / 1000ULL;
}

static U64 timespec64ToMicros(CPU* cpu, U32 address) {
    const U64 seconds = cpu->memory->readq(address);
    const U64 nanos = cpu->memory->readd(address + 8);
    return seconds * 1000000ULL + (nanos + 999ULL) / 1000ULL;
}

static bool waitCompletedByTimeout(U32 kind, U32 result) {
    switch (kind) {
    case K_WAIT_FRAME_SELECT:
    case K_WAIT_FRAME_POLL:
    case K_WAIT_FRAME_NANOSLEEP:
    case K_WAIT_FRAME_CLOCK_NANOSLEEP:
        return result == 0;
    case K_WAIT_FRAME_FUTEX:
        return result == (U32)-K_ETIMEDOUT;
    default:
        return false;
    }
}

template <typename Emit>
static void logAzrokWaitBucket(const char* name, const AzrokWaitBucket& bucket, Emit&& emit) {
    emit("AZROK_WAIT_FRAME kind=%s calls=%llu req_known=%llu req_us=%llu actual_us=%llu timeout_calls=%llu oversleep_us=%llu max_actual_us=%llu max_oversleep_us=%llu",
        name,
        (unsigned long long)bucket.calls,
        (unsigned long long)bucket.requestedKnownCalls,
        (unsigned long long)bucket.requestedMicros,
        (unsigned long long)bucket.actualMicros,
        (unsigned long long)bucket.timeoutCalls,
        (unsigned long long)bucket.oversleepMicros,
        (unsigned long long)bucket.maxActualMicros,
        (unsigned long long)bucket.maxOversleepMicros);
}

static AzrokWaitFrameProfile::Caller* getMarkedWaitCaller(U32 address) {
    AzrokWaitFrameProfile::Caller* empty = nullptr;
    for (auto& caller : azrokWaitFrameProfile.callers) {
        if (caller.address == address) return &caller;
        if (!caller.address && !empty) empty = &caller;
    }
    if (empty) {
        empty->address = address;
        return empty;
    }
    return nullptr;
}

static AzrokWaitFrameProfile::ReadSource* getReadSource(
        U32 fd, U32 syscallEip, U32 objectType, const std::shared_ptr<KObject>& object) {
    AzrokWaitFrameProfile::ReadSource* empty = nullptr;
    for (auto& source : azrokWaitFrameProfile.readSources) {
        if (source.fd == fd && source.syscallEip == syscallEip) return &source;
        if (source.fd == 0xffffffffu && !empty) empty = &source;
    }
    if (empty) {
        empty->fd = fd;
        empty->syscallEip = syscallEip;
        empty->objectType = objectType;
        if (object) empty->identity = object->selfFd();
        return empty;
    }
    azrokWaitFrameProfile.readSourceOverflow = true;
    return nullptr;
}
}

#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
bool azrokCausalWisActive() {
    return azrokCausalWisContext.active;
}

static U64 azrokCausalRawNow() {
#ifdef __EMSCRIPTEN__
    return (U64)(emscripten_get_now() * 1000.0 + 0.5);
#else
    return KSystem::getMicroCounter();
#endif
}

U64 azrokCausalProfileNow() {
    if (azrokWaitFrameProfile.armed) {
        azrokWaitFrameProfile.causalStageClockCalls++;
    }
    return azrokCausalRawNow();
}

void azrokCausalWisRecordStage(U32 stage, U64 startMicros, U64 endMicros) {
    if (!azrokCausalWisContext.active || stage >= K_AZROK_CAUSAL_WIS_STAGE_COUNT) return;
    if (endMicros >= startMicros) {
        azrokCausalWisContext.stages[stage] += endMicros - startMicros;
        azrokWaitFrameProfile.causalTimedIntervals++;
    }
}

void azrokCausalWisRecordBackend(U32 backend) {
    if (!azrokCausalWisContext.active || backend >= K_AZROK_CAUSAL_WIS_BACKEND_COUNT) return;
    azrokCausalWisContext.backendCalls[backend]++;
}

void azrokCausalWisRecordCacheLookup(bool hit) {
    if (!azrokCausalWisContext.active) return;
    if (hit) azrokCausalWisContext.cacheLookupHits++;
    else azrokCausalWisContext.cacheLookupMisses++;
}

void azrokCausalWineSocketPublished(U32 bytes) {
    if (!azrokCausalWineContext.prepared) return;
    if (bytes != azrokCausalWineContext.expectedBytes) {
        azrokWaitFrameProfile.causalWinePublishPartial++;
        azrokCausalWineContext = AzrokCausalWineContext{};
        return;
    }
    azrokCausalWineContext.publishMicros = azrokCausalProfileNow();
    azrokCausalWineContext.published = true;
    azrokCausalWineContext.prepared = false;
}

static void finishAzrokCausalWis(U32 result, U64 syscallMicros, U64 syscallEndMicros) {
    if (!azrokCausalWisContext.active) {
        azrokWaitFrameProfile.causalWisFinishUnmatched++;
        return;
    }
    if (azrokCausalWisContext.windowSerial != azrokCausalWindowSerial) {
        azrokWaitFrameProfile.causalWisCrossWindow++;
        azrokCausalWisContext = AzrokCausalWisContext{};
        return;
    }
    if (syscallEndMicros >= azrokCausalWisContext.syscallStartMicros) {
        syscallMicros = syscallEndMicros - azrokCausalWisContext.syscallStartMicros;
        azrokWaitFrameProfile.causalTimedIntervals++;
    }
    AzrokWaitFrameProfile::CausalWis& total = azrokWaitFrameProfile.causalWis;
    total.calls++;
    if ((S32)result > 0) total.bytes += result;
    total.syscallMicros += syscallMicros;
    U64 leafMicros = 0;
    for (U32 i = 0; i < K_AZROK_CAUSAL_WIS_STAGE_COUNT; ++i) {
        total.stages[i] += azrokCausalWisContext.stages[i];
        if (i != K_AZROK_CAUSAL_WIS_OUTER_HELD) {
            leafMicros += azrokCausalWisContext.stages[i];
        }
    }
    for (U32 i = 0; i < K_AZROK_CAUSAL_WIS_BACKEND_COUNT; ++i) {
        total.backendCalls[i] += azrokCausalWisContext.backendCalls[i];
    }
    total.cacheLookupHits += azrokCausalWisContext.cacheLookupHits;
    total.cacheLookupMisses += azrokCausalWisContext.cacheLookupMisses;
    total.leafAccountedMicros += std::min<U64>(leafMicros, syscallMicros);
    if (syscallMicros >= leafMicros) total.residualMicros += syscallMicros - leafMicros;
    else total.leafOverrunMicros += leafMicros - syscallMicros;
    azrokCausalWisContext = AzrokCausalWisContext{};
}

static void finishAzrokCausalWineWrite(bool success) {
    if (!azrokCausalWineContext.prepared) return;
    if (success) azrokWaitFrameProfile.causalWinePublishMissing++;
    azrokCausalWineContext = AzrokCausalWineContext{};
}

static void finishAzrokCausalWineReply(const KWaitFrameProfilePending& pending,
                                       U32 result, U64 endMicros) {
    if (!azrokCausalWineContext.published) {
        azrokWaitFrameProfile.causalWineReplyUnmatched++;
        return;
    }
    if (azrokCausalWineContext.windowSerial != azrokCausalWindowSerial) {
        azrokWaitFrameProfile.causalWineCrossWindow++;
    } else if (pending.wineRequestId != azrokCausalWineContext.requestId ||
            pending.wineRequestFd != azrokCausalWineContext.requestFd
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
            || pending.causalThread != azrokCausalWineContext.clientThread
#endif
            ) {
        azrokWaitFrameProfile.causalWineRequestMismatch++;
    } else if (pending.wineRequestId < AZROK_WINE_REQUEST_LIMIT) {
        endMicros = azrokCausalProfileNow();
        if (endMicros < azrokCausalWineContext.publishMicros) {
            azrokWaitFrameProfile.causalWineReplyUnmatched++;
            azrokCausalWineContext = AzrokCausalWineContext{};
            return;
        }
        const U64 elapsed = endMicros - azrokCausalWineContext.publishMicros;
        azrokWaitFrameProfile.causalTimedIntervals++;
        AzrokWaitFrameProfile::CausalWine& total =
            azrokWaitFrameProfile.causalWine[pending.wineRequestId];
        total.calls++;
        total.publishToClientReturnMicros += elapsed;
        total.maxPublishToClientReturnMicros =
            std::max<U64>(total.maxPublishToClientReturnMicros, elapsed);
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        if (pending.wineRequestId == AZROK_WINE_CREATE_FILE_REQUEST ||
                pending.wineRequestId == AZROK_WINE_GET_HANDLE_FD_REQUEST ||
                pending.wineRequestId == AZROK_WINE_CLOSE_HANDLE_REQUEST) {
            AzrokCausalWineTransaction transaction;
            transaction.requestId = pending.wineRequestId;
            transaction.elapsedMicros = elapsed;
            transaction.completedMicros = endMicros;
            transaction.windowSerial = azrokCausalWineContext.windowSerial;
            transaction.clientThread = azrokCausalWineContext.clientThread;
            if (pending.causalMemory && pending.causalReplyAddress && result >= 8u &&
                    pending.causalMemory->canRead(pending.causalReplyAddress, result)) {
                transaction.valid = true;
                transaction.success = pending.causalMemory->readd(
                    pending.causalReplyAddress) == 0;
                if (pending.wineRequestId == AZROK_WINE_CREATE_FILE_REQUEST) {
                    if (transaction.success && result >= 12u) {
                        transaction.handle = pending.causalMemory->readd(
                            pending.causalReplyAddress + 8u);
                        transaction.handleKnown = isAzrokCausalWineUserHandle(
                            transaction.handle);
                    }
                } else {
                    transaction.handle = azrokCausalWineContext.requestHandle;
                    transaction.handleKnown = azrokCausalWineContext.requestHandleKnown;
                }
            }
            recordAzrokCausalWisWineTransaction(transaction);
        }
#endif
    } else {
        azrokWaitFrameProfile.causalWineReplyUnmatched++;
    }
    azrokCausalWineContext = AzrokCausalWineContext{};
}

static void probeAzrokCausalClock() {
    constexpr U32 PAIRS = 32;
    for (U32 i = 0; i < PAIRS; ++i) {
        const U64 start = azrokCausalRawNow();
        const U64 end = azrokCausalRawNow();
        const U64 elapsed = end >= start ? end - start : 0;
        azrokWaitFrameProfile.causalClockProbePairs++;
        azrokWaitFrameProfile.causalClockProbeMicros += elapsed;
        azrokWaitFrameProfile.causalClockProbeMinMicros =
            std::min<U64>(azrokWaitFrameProfile.causalClockProbeMinMicros, elapsed);
        azrokWaitFrameProfile.causalClockProbeMaxMicros =
            std::max<U64>(azrokWaitFrameProfile.causalClockProbeMaxMicros, elapsed);
    }
}
#endif

#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
extern "C" U32 wasm_wait_profile_publish_abi_version() {
    return AZROK_WAIT_PUBLISH_ABI_VERSION;
}

extern "C" U32 wasm_wait_profile_publish_seq_address() {
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
    AzrokWaitProfilePublishedText* published = ensureAzrokWaitProfilePublishedText();
    return published ? (U32)(uintptr_t)&published->sequence : 0;
#else
    return (U32)(uintptr_t)&azrokWaitProfilePublishedText.sequence;
#endif
}

extern "C" U32 wasm_wait_profile_publish_bytes_address() {
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
    AzrokWaitProfilePublishedText* published = ensureAzrokWaitProfilePublishedText();
    return published ? (U32)(uintptr_t)&published->bytes : 0;
#else
    return (U32)(uintptr_t)&azrokWaitProfilePublishedText.bytes;
#endif
}

extern "C" U32 wasm_wait_profile_publish_data_address() {
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
    AzrokWaitProfilePublishedText* published = ensureAzrokWaitProfilePublishedText();
    return published ? (U32)(uintptr_t)&published->data[0] : 0;
#else
    return (U32)(uintptr_t)&azrokWaitProfilePublishedText.data[0];
#endif
}

extern "C" U32 wasm_wait_profile_publish_capacity() {
    return AZROK_WAIT_PUBLISH_CAPACITY;
}
#endif

KWaitFrameProfilePending prepareAzrokWaitFrameProfile(CPU* cpu, U32 syscallNo) {
    KWaitFrameProfilePending pending;
    switch (syscallNo) {
    case 3: // read; Wine WaitForSingleObject blocks on wait_fd with no timeout in this syscall.
        pending.active = true;
        pending.kind = K_WAIT_FRAME_READ;
        pending.fd = ARG1;
        pending.requestedLen = ARG3;
        pending.syscallEip = cpu->eip.u32;
        pending.wineRequestId = azrokLastWineServerRequest;
        pending.wineRequestFd = azrokLastWineServerRequestFd;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        pending.causalMemory = cpu->memory;
        pending.causalThread = cpu->thread;
        pending.causalReplyAddress = ARG2;
#endif
        if (azrokWaitFrameProfile.armed) {
            KFileDescriptorPtr fd = cpu->thread->process->getFileDescriptor(ARG1);
            if (!fd) break;
            if (fd->kobject) {
                pending.objectType = fd->kobject->type;
                pending.object = fd->kobject;
            }
        }
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        pending.causalWis = beginAzrokCausalWis(pending.object);
#endif
        break;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    case 6: { // close; capture the exact WIS KFile before descriptor removal.
        if (!azrokWaitFrameProfile.armed) break;
        KFileDescriptorPtr fd = cpu->thread->process->getFileDescriptor(ARG1);
        if (!fd || !fd->kobject || fd->kobject->type != KTYPE_FILE ||
                !isAzrokWisPath(fd->kobject->selfFd())) {
            break;
        }
        pending.active = true;
        pending.fd = ARG1;
        pending.objectType = fd->kobject->type;
        pending.object = fd->kobject;
        pending.causalThread = cpu->thread;
        pending.causalWisFdClose = true;
        break;
    }
#endif
    case 4: // write; Wine fixed-size server request
        captureWineRequestHeader(cpu, ARG1, ARG2, ARG3, 0, pending);
        break;
    case 142: // _newselect, timeval32
        pending.active = true;
        pending.kind = K_WAIT_FRAME_SELECT;
        if (ARG5) {
            pending.requestedKnown = true;
            pending.requestedMicros = (U64)cpu->memory->readd(ARG5) * 1000000ULL +
                cpu->memory->readd(ARG5 + 4);
        }
        break;
    case 146: // writev; Wine request with appended variable data
        if ((S32)ARG3 >= 2 && ARG3 <= 6 && ARG2 &&
                canSafelyReadGuest(cpu, ARG2, ARG3 * 8)) {
            const U32 firstBase = cpu->memory->readd(ARG2);
            const U32 firstLength = cpu->memory->readd(ARG2 + 4);
            U64 variableBytes = 0;
            for (U32 i = 1; i < ARG3; ++i) {
                variableBytes += cpu->memory->readd(ARG2 + i * 8 + 4);
            }
            if (variableBytes > 0 && variableBytes <= 16 * 1024 * 1024) {
                captureWineRequestHeader(cpu, ARG1, firstBase, firstLength,
                    (U32)variableBytes, pending);
            }
        }
        break;
    case 162: // nanosleep, timespec32
        pending.active = true;
        pending.kind = K_WAIT_FRAME_NANOSLEEP;
        if (ARG1) {
            pending.requestedKnown = true;
            pending.requestedMicros = timespec32ToMicros(cpu, ARG1);
        }
        break;
    case 168: // poll, timeout milliseconds
        pending.active = true;
        pending.kind = K_WAIT_FRAME_POLL;
        if ((S32)ARG3 >= 0) {
            pending.requestedKnown = true;
            pending.requestedMicros = (U64)ARG3 * 1000ULL;
        }
        break;
    case 240: // futex, timespec32
    case 422: { // futex_time64
        const U32 operation = ARG2 & 0x7f;
        if (operation != 0 && operation != 9) break; // FUTEX_WAIT / FUTEX_WAIT_BITSET only
        pending.active = true;
        pending.kind = K_WAIT_FRAME_FUTEX;
        if (operation == 0 && ARG4) { // relative timeout; WAIT_BITSET is absolute
            pending.requestedKnown = true;
            pending.requestedMicros = syscallNo == 422 ?
                timespec64ToMicros(cpu, ARG4) : timespec32ToMicros(cpu, ARG4);
        }
        break;
    }
    case 267: // clock_nanosleep, timespec32
    case 407: // clock_nanosleep_time64
        pending.active = true;
        pending.kind = K_WAIT_FRAME_CLOCK_NANOSLEEP;
        if (!(ARG2 & 1) && ARG3) { // TIMER_ABSTIME cannot be normalized without another clock read.
            pending.requestedKnown = true;
            pending.requestedMicros = syscallNo == 407 ?
                timespec64ToMicros(cpu, ARG3) : timespec32ToMicros(cpu, ARG3);
        }
        break;
    case 308: // pselect6, timespec32
    case 413: // pselect6_time64
        pending.active = true;
        pending.kind = K_WAIT_FRAME_SELECT;
        if (ARG5) {
            pending.requestedKnown = true;
            pending.requestedMicros = syscallNo == 413 ?
                timespec64ToMicros(cpu, ARG5) : timespec32ToMicros(cpu, ARG5);
        }
        break;
    default:
        break;
    }
    return pending;
}

void recordAzrokWaitFrameProfile(const KWaitFrameProfilePending& pending, U32 result,
                                 U64 startMicros, U64 endMicros) {
    if (!pending.active) return;
    const U64 actualMicros = endMicros >= startMicros ? endMicros - startMicros : 0;
    if (pending.wineRequestWrite) {
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        finishAzrokCausalWineWrite((S32)result > 0);
#endif
        if ((S32)result > 0) {
            azrokLastWineServerRequest = pending.wineRequestId;
            azrokLastWineServerRequestFd = pending.wineRequestFd;
        }
        return;
    }
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    if (pending.causalWisFdClose) {
        recordAzrokCausalWisFdClose(pending.causalThread, pending.fd, pending.object,
            result == 0, azrokCausalProfileNow());
        return;
    }
#endif
    const bool consumedWineReply = pending.kind == K_WAIT_FRAME_READ &&
        pending.fd == 4 && pending.requestedLen == 64 && (S32)result > 0;
    if (consumedWineReply) {
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        finishAzrokCausalWineReply(pending, result, endMicros);
#endif
        azrokLastWineServerRequest = 0xffffffffu;
        azrokLastWineServerRequestFd = 0xffffffffu;
    }
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    if (pending.causalWis) {
        const U64 causalEndMicros = azrokCausalProfileNow();
        finishAzrokCausalWis(result, actualMicros, causalEndMicros);
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        recordAzrokCausalWisHandleRead(pending.causalThread, pending.fd,
            pending.object, result, causalEndMicros);
#endif
    }
#endif
    if (!azrokWaitFrameProfile.armed) return;
    AzrokWaitBucket& bucket = azrokWaitFrameProfile.buckets[pending.kind];
    bucket.calls++;
    bucket.actualMicros += actualMicros;
    if (actualMicros > bucket.maxActualMicros) bucket.maxActualMicros = actualMicros;
    if (pending.requestedKnown) {
        bucket.requestedKnownCalls++;
        bucket.requestedMicros += pending.requestedMicros;
    }
    if (pending.requestedKnown && waitCompletedByTimeout(pending.kind, result)) {
        bucket.timeoutCalls++;
        if (actualMicros > pending.requestedMicros) {
            const U64 oversleepMicros = actualMicros - pending.requestedMicros;
            bucket.oversleepMicros += oversleepMicros;
            if (oversleepMicros > bucket.maxOversleepMicros) bucket.maxOversleepMicros = oversleepMicros;
        }
    }
    if (pending.kind == K_WAIT_FRAME_READ && actualMicros >= AZROK_BLOCKING_READ_MIN_US) {
        azrokWaitFrameProfile.blockingReadCalls++;
        azrokWaitFrameProfile.blockingReadMicros += actualMicros;
        if (actualMicros > azrokWaitFrameProfile.maxBlockingReadMicros) {
            azrokWaitFrameProfile.maxBlockingReadMicros = actualMicros;
        }
        AzrokWaitFrameProfile::ReadSource* readSource = getReadSource(
            pending.fd, pending.syscallEip, pending.objectType, pending.object);
        if (readSource) {
            readSource->blockingCalls++;
            readSource->blockingMicros += actualMicros;
            if (actualMicros > readSource->maxBlockingMicros) {
                readSource->maxBlockingMicros = actualMicros;
            }
            readSource->requestedBytes += pending.requestedLen;
            if ((S32)result > 0) readSource->positiveBytes += result;
            else if (!result) readSource->zeroCalls++;
            else readSource->errorCalls++;
            if (pending.requestedLen == 12) {
                readSource->len12Calls++;
                readSource->len12Micros += actualMicros;
            } else if (pending.requestedLen == 64) {
                readSource->len64Calls++;
                readSource->len64Micros += actualMicros;
            } else {
                readSource->otherLenCalls++;
                readSource->otherLenMicros += actualMicros;
            }
        } else {
            azrokWaitFrameProfile.readSourceOverflowCalls++;
            azrokWaitFrameProfile.readSourceOverflowMicros += actualMicros;
            if (actualMicros > azrokWaitFrameProfile.readSourceOverflowMaxMicros) {
                azrokWaitFrameProfile.readSourceOverflowMaxMicros = actualMicros;
            }
        }
        if (pending.fd == 4 && pending.requestedLen == 64 &&
                pending.wineRequestId < AZROK_WINE_REQUEST_LIMIT) {
            AzrokWaitFrameProfile::WineRequest& request =
                azrokWaitFrameProfile.wineRequests[pending.wineRequestId];
            request.requestFd = pending.wineRequestFd;
            request.blockingCalls++;
            request.blockingMicros += actualMicros;
            if (actualMicros > request.maxBlockingMicros) {
                request.maxBlockingMicros = actualMicros;
            }
        }
        if (azrokWaitFrameProfile.markedWaitActive) {
            azrokWaitFrameProfile.markedWaitReadCalls++;
            azrokWaitFrameProfile.markedWaitReadMicros += actualMicros;
            if (actualMicros > azrokWaitFrameProfile.markedWaitMaxReadMicros) {
                azrokWaitFrameProfile.markedWaitMaxReadMicros = actualMicros;
            }
            if (AzrokWaitFrameProfile::Caller* caller = getMarkedWaitCaller(azrokWaitFrameProfile.markedWaitCaller)) {
                caller->readCalls++;
                caller->readMicros += actualMicros;
                if (actualMicros > caller->maxReadMicros) caller->maxReadMicros = actualMicros;
            }
        }
    }
}

void markAzrokWaitFrameProfile(U32 phase, U32 timeoutMillis, U32 callerAddress, U32 value) {
    if (!azrokWaitFrameProfile.armed) return;
    if (phase == 1) {
        azrokWaitFrameProfile.markedWaitActive = true;
        azrokWaitFrameProfile.markedWaitCaller = callerAddress;
        azrokWaitFrameProfile.markedWaitTimeoutMillis = timeoutMillis;
        azrokWaitFrameProfile.markedWaitCalls++;
        AzrokWaitFrameProfile::Caller* caller = getMarkedWaitCaller(callerAddress);
        if (caller) caller->calls++;
        if (timeoutMillis != 0xffffffffu) {
            const U64 requestedMicros = (U64)timeoutMillis * 1000ULL;
            azrokWaitFrameProfile.markedWaitRequestedKnown++;
            azrokWaitFrameProfile.markedWaitRequestedMicros += requestedMicros;
            if (caller) {
                caller->requestedKnown++;
                caller->requestedMicros += requestedMicros;
            }
        }
    } else if (phase == 2) {
        if (!azrokWaitFrameProfile.markedWaitActive) return;
        azrokWaitFrameProfile.markedWaitCompleted++;
        if (value == 0x102u) azrokWaitFrameProfile.markedWaitTimeoutResults++;
        else if (value == 0xffffffffu) azrokWaitFrameProfile.markedWaitFailedResults++;
        else azrokWaitFrameProfile.markedWaitSignaledResults++;
        azrokWaitFrameProfile.markedWaitActive = false;
    }
}

void presentAzrokWaitFrameProfile(CPU* cpu) {
    if (!azrokWaitFrameProfile.armed) {
        azrokWaitFrameProfile = AzrokWaitFrameProfile{};
        azrokWaitFrameProfile.armed = true;
        azrokWaitFrameProfile.startJitBlockDispatches = cpu->azrokJitBlockDispatches;
        azrokWaitFrameProfile.startWarmupBlockDispatches = cpu->azrokWarmupBlockDispatches;
        azrokWaitFrameProfile.startPendingBlockDispatches = cpu->azrokPendingBlockDispatches;
        azrokWaitFrameProfile.startNoJitBlockDispatches = cpu->azrokNoJitBlockDispatches;
        azrokWaitFrameProfile.startWallMicros = KSystem::getMicroCounter();
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
        ++azrokCausalWindowSerial;
        probeAzrokCausalClock();
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
        resetAzrokCausalWisHandleTracker();
#endif
#endif
        return;
    }
    if (++azrokWaitFrameProfile.presents < AZROK_WAIT_PROFILE_PRESENTS) return;

    // Close the measured window before formatting or publishing any rows.
    // The shared diagnostic starts the next window only after publication.
    const U64 coverageEndMicros = KSystem::getMicroCounter();
    const U64 coverageWallMicros = coverageEndMicros >= azrokWaitFrameProfile.startWallMicros ?
        coverageEndMicros - azrokWaitFrameProfile.startWallMicros : 0;
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    if (azrokCausalWineContext.published) {
        azrokWaitFrameProfile.causalWineOutstandingAtBoundary++;
    }
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    finishAzrokCausalWisHandleJoinWindow();
#endif
#endif
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
    AzrokWaitProfileTextWriter writer;
    auto emit = [&writer](const char* format, auto... values) {
        writer.appendLine(format, values...);
    };
#else
    auto emit = [](const char* format, auto... values) {
        klog_fmt(format, values...);
    };
#endif

    emit("AZROK_WAIT_FRAME window_presents=%u read_req=N/A blocking_read_threshold_us=%llu blocking_read_calls=%llu blocking_read_us=%llu max_blocking_read_us=%llu",
        azrokWaitFrameProfile.presents,
        (unsigned long long)AZROK_BLOCKING_READ_MIN_US,
        (unsigned long long)azrokWaitFrameProfile.blockingReadCalls,
        (unsigned long long)azrokWaitFrameProfile.blockingReadMicros,
        (unsigned long long)azrokWaitFrameProfile.maxBlockingReadMicros);
    logAzrokWaitBucket("select", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_SELECT], emit);
    logAzrokWaitBucket("nanosleep", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_NANOSLEEP], emit);
    logAzrokWaitBucket("clock_nanosleep", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_CLOCK_NANOSLEEP], emit);
    logAzrokWaitBucket("poll", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_POLL], emit);
    logAzrokWaitBucket("futex", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_FUTEX], emit);
    logAzrokWaitBucket("read_req_na", azrokWaitFrameProfile.buckets[K_WAIT_FRAME_READ], emit);
    for (const auto& source : azrokWaitFrameProfile.readSources) {
        if (source.fd == 0xffffffffu) continue;
        emit("AZROK_WAIT_READ_SOURCE fd=%u eip=%08x object_type=%u identity=%s blocking_calls=%llu blocking_us=%llu max_blocking_us=%llu requested_bytes=%llu positive_bytes=%llu zero=%llu errors=%llu len12_calls=%llu len12_us=%llu len64_calls=%llu len64_us=%llu other_calls=%llu other_us=%llu",
            source.fd,
            source.syscallEip,
            source.objectType,
            source.identity.c_str(),
            (unsigned long long)source.blockingCalls,
            (unsigned long long)source.blockingMicros,
            (unsigned long long)source.maxBlockingMicros,
            (unsigned long long)source.requestedBytes,
            (unsigned long long)source.positiveBytes,
            (unsigned long long)source.zeroCalls,
            (unsigned long long)source.errorCalls,
            (unsigned long long)source.len12Calls,
            (unsigned long long)source.len12Micros,
            (unsigned long long)source.len64Calls,
            (unsigned long long)source.len64Micros,
            (unsigned long long)source.otherLenCalls,
            (unsigned long long)source.otherLenMicros);
    }
    if (azrokWaitFrameProfile.readSourceOverflow) {
        emit("AZROK_WAIT_READ_SOURCE overflow=1 calls=%llu us=%llu max_us=%llu",
            (unsigned long long)azrokWaitFrameProfile.readSourceOverflowCalls,
            (unsigned long long)azrokWaitFrameProfile.readSourceOverflowMicros,
            (unsigned long long)azrokWaitFrameProfile.readSourceOverflowMaxMicros);
    }
    for (U32 requestId = 0; requestId < AZROK_WINE_REQUEST_LIMIT; ++requestId) {
        const AzrokWaitFrameProfile::WineRequest& request =
            azrokWaitFrameProfile.wineRequests[requestId];
        if (!request.blockingCalls) continue;
        emit("AZROK_WINE_SERVER_REQUEST id=%u request_fd=%u blocking_calls=%llu blocking_us=%llu max_blocking_us=%llu",
            requestId,
            request.requestFd,
            (unsigned long long)request.blockingCalls,
            (unsigned long long)request.blockingMicros,
            (unsigned long long)request.maxBlockingMicros);
    }
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    const AzrokWaitFrameProfile::CausalWis& wis = azrokWaitFrameProfile.causalWis;
    emit("AZROK_CAUSAL_WIS calls=%llu bytes=%llu syscall_us=%llu filepos_wait_us=%llu outer_wait_us=%llu outer_held_us=%llu page_prepare_us=%llu cache_lookup_us=%llu cache_memcpy_us=%llu host_read_us=%llu zip_setup_us=%llu zip_inflate_us=%llu leaf_accounted_us=%llu residual_us=%llu leaf_overrun_us=%llu cache_backend_calls=%llu direct_backend_calls=%llu zip_backend_calls=%llu host_backend_calls=%llu cache_lookup_hits=%llu cache_lookup_misses=%llu",
        (unsigned long long)wis.calls,
        (unsigned long long)wis.bytes,
        (unsigned long long)wis.syscallMicros,
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_FILE_POS_WAIT],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_OUTER_WAIT],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_OUTER_HELD],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_PAGE_PREPARE],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_CACHE_LOOKUP],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_CACHE_MEMCPY],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_HOST_READ],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_ZIP_SETUP],
        (unsigned long long)wis.stages[K_AZROK_CAUSAL_WIS_ZIP_INFLATE],
        (unsigned long long)wis.leafAccountedMicros,
        (unsigned long long)wis.residualMicros,
        (unsigned long long)wis.leafOverrunMicros,
        (unsigned long long)wis.backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_CACHE],
        (unsigned long long)wis.backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_DIRECT],
        (unsigned long long)wis.backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_ZIP],
        (unsigned long long)wis.backendCalls[K_AZROK_CAUSAL_WIS_BACKEND_HOST],
        (unsigned long long)wis.cacheLookupHits,
        (unsigned long long)wis.cacheLookupMisses);
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    const AzrokWaitFrameProfile::CausalWisHandleJoin& join =
        azrokWaitFrameProfile.causalWisHandleJoin;
    emit("AZROK_CAUSAL_WIS_HANDLE_JOIN loads=%llu create_calls=%llu create_us=%llu getfd_calls=%llu getfd_us=%llu close_calls=%llu close_us=%llu union_us=%llu ambiguous=%llu unmatched=%llu timeouts=%llu interleaved=%llu handle_parse_failures=%llu shape_mismatches=%llu cross_window=%llu abi=1 expected_reads=6 expected_bytes=130580 timeout_us=2000000",
        (unsigned long long)join.loads,
        (unsigned long long)join.createCalls,
        (unsigned long long)join.createMicros,
        (unsigned long long)join.getFdCalls,
        (unsigned long long)join.getFdMicros,
        (unsigned long long)join.closeCalls,
        (unsigned long long)join.closeMicros,
        (unsigned long long)join.unionMicros,
        (unsigned long long)join.ambiguous,
        (unsigned long long)join.unmatched,
        (unsigned long long)join.timeouts,
        (unsigned long long)join.interleaved,
        (unsigned long long)join.handleParseFailures,
        (unsigned long long)join.shapeMismatches,
        (unsigned long long)join.crossWindow);
#endif
    for (U32 requestId = 0; requestId < AZROK_WINE_REQUEST_LIMIT; ++requestId) {
        const AzrokWaitFrameProfile::CausalWine& request =
            azrokWaitFrameProfile.causalWine[requestId];
        if (!request.calls) continue;
        emit("AZROK_CAUSAL_WINE id=%u calls=%llu publish_to_client_return_us=%llu max_publish_to_client_return_us=%llu",
            requestId,
            (unsigned long long)request.calls,
            (unsigned long long)request.publishToClientReturnMicros,
            (unsigned long long)request.maxPublishToClientReturnMicros);
    }
    const U64 clockProbeMin = azrokWaitFrameProfile.causalClockProbePairs ?
        azrokWaitFrameProfile.causalClockProbeMinMicros : 0;
    emit("AZROK_CAUSAL_META abi=1 wine_mode=request_publish_to_client_read_return wis_context_overlap=%llu wis_finish_unmatched=%llu wis_cross_window=%llu wine_publish_overflow=%llu wine_publish_missing=%llu wine_publish_partial=%llu wine_reply_unmatched=%llu wine_request_mismatch=%llu wine_cross_window=%llu wine_outstanding_at_boundary=%llu stage_clock_calls=%llu timed_intervals=%llu clock_probe_pairs=%llu clock_probe_us=%llu clock_probe_min_us=%llu clock_probe_max_us=%llu",
        (unsigned long long)azrokWaitFrameProfile.causalWisContextOverlap,
        (unsigned long long)azrokWaitFrameProfile.causalWisFinishUnmatched,
        (unsigned long long)azrokWaitFrameProfile.causalWisCrossWindow,
        (unsigned long long)azrokWaitFrameProfile.causalWinePublishOverflow,
        (unsigned long long)azrokWaitFrameProfile.causalWinePublishMissing,
        (unsigned long long)azrokWaitFrameProfile.causalWinePublishPartial,
        (unsigned long long)azrokWaitFrameProfile.causalWineReplyUnmatched,
        (unsigned long long)azrokWaitFrameProfile.causalWineRequestMismatch,
        (unsigned long long)azrokWaitFrameProfile.causalWineCrossWindow,
        (unsigned long long)azrokWaitFrameProfile.causalWineOutstandingAtBoundary,
        (unsigned long long)azrokWaitFrameProfile.causalStageClockCalls,
        (unsigned long long)azrokWaitFrameProfile.causalTimedIntervals,
        (unsigned long long)azrokWaitFrameProfile.causalClockProbePairs,
        (unsigned long long)azrokWaitFrameProfile.causalClockProbeMicros,
        (unsigned long long)clockProbeMin,
        (unsigned long long)azrokWaitFrameProfile.causalClockProbeMaxMicros);
#endif
    emit("AZROK_JIT_COVERAGE presents=%u wall_us=%llu jit_blocks=%u warmup_blocks=%u pending_blocks=%u nojit_blocks=%u",
        azrokWaitFrameProfile.presents,
        (unsigned long long)coverageWallMicros,
        cpu->azrokJitBlockDispatches - azrokWaitFrameProfile.startJitBlockDispatches,
        cpu->azrokWarmupBlockDispatches - azrokWaitFrameProfile.startWarmupBlockDispatches,
        cpu->azrokPendingBlockDispatches - azrokWaitFrameProfile.startPendingBlockDispatches,
        cpu->azrokNoJitBlockDispatches - azrokWaitFrameProfile.startNoJitBlockDispatches);
    emit("AZROK_WAIT_MARKER api=WaitForSingleObject calls=%llu completed=%llu req_known=%llu req_us=%llu blocking_read_calls=%llu blocking_read_us=%llu max_blocking_read_us=%llu signaled=%llu timeout=%llu failed=%llu caller_overflow=%s",
        (unsigned long long)azrokWaitFrameProfile.markedWaitCalls,
        (unsigned long long)azrokWaitFrameProfile.markedWaitCompleted,
        (unsigned long long)azrokWaitFrameProfile.markedWaitRequestedKnown,
        (unsigned long long)azrokWaitFrameProfile.markedWaitRequestedMicros,
        (unsigned long long)azrokWaitFrameProfile.markedWaitReadCalls,
        (unsigned long long)azrokWaitFrameProfile.markedWaitReadMicros,
        (unsigned long long)azrokWaitFrameProfile.markedWaitMaxReadMicros,
        (unsigned long long)azrokWaitFrameProfile.markedWaitSignaledResults,
        (unsigned long long)azrokWaitFrameProfile.markedWaitTimeoutResults,
        (unsigned long long)azrokWaitFrameProfile.markedWaitFailedResults,
        azrokWaitFrameProfile.callers[7].address ? "possible" : "no");
    for (const auto& caller : azrokWaitFrameProfile.callers) {
        if (!caller.address) continue;
        emit("AZROK_WAIT_MARKER_CALLER caller=%08x calls=%llu req_known=%llu req_us=%llu blocking_read_calls=%llu blocking_read_us=%llu max_blocking_read_us=%llu",
            caller.address,
            (unsigned long long)caller.calls,
            (unsigned long long)caller.requestedKnown,
            (unsigned long long)caller.requestedMicros,
            (unsigned long long)caller.readCalls,
            (unsigned long long)caller.readMicros,
            (unsigned long long)caller.maxReadMicros);
    }
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
    writer.publish();
#endif

    azrokWaitFrameProfile = AzrokWaitFrameProfile{};
    azrokWaitFrameProfile.armed = true;
    azrokWaitFrameProfile.startJitBlockDispatches = cpu->azrokJitBlockDispatches;
    azrokWaitFrameProfile.startWarmupBlockDispatches = cpu->azrokWarmupBlockDispatches;
    azrokWaitFrameProfile.startPendingBlockDispatches = cpu->azrokPendingBlockDispatches;
    azrokWaitFrameProfile.startNoJitBlockDispatches = cpu->azrokNoJitBlockDispatches;
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
    azrokWaitFrameProfile.startWallMicros = KSystem::getMicroCounter();
#else
    azrokWaitFrameProfile.startWallMicros = coverageEndMicros;
#endif
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    ++azrokCausalWindowSerial;
    probeAzrokCausalClock();
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    resetAzrokCausalWisHandleTracker();
#endif
#endif
}
#endif

#define SARG2 memory->readd(ARG2)
#define SARG3 memory->readd(ARG2+4)
#define SARG4 memory->readd(ARG2+8)
#define SARG5 memory->readd(ARG2+12)
#define SARG6 memory->readd(ARG2+16)
#define SARG7 memory->readd(ARG2+20)

typedef U32 (*SyscallFunc)(CPU* cpu, U32 eipCount);

#define SYSCALL_PROCESS     0x01
#define SYSCALL_THREAD      0x02
#define SYSCALL_FILE        0x04
#define SYSCALL_READ        0x08
#define SYSCALL_WRITE       0x10
#define SYSCALL_SYSTEM      0x20
#define SYSCALL_SIGNAL      0x40
#define SYSCALL_MEMORY      0x80
#define SYSCALL_SOCKET      0x100
#define SYSCALL_FUTEX       0x200

#ifdef _DEBUG
static U32 syscallMask = 0;
#else
static U32 syscallMask = 0;
#endif

template<typename ... Args>
void sysLog(U32 type, CPU* cpu, const char* msg, Args ... args) {
    if (type & syscallMask) {
        std::printf(msg, args ...);
    }
}

template<typename ... Args>
void sysLog1(U32 type, CPU* cpu, const char* msg, Args ... args) {
    if (type & syscallMask) {
        printf("%.4X/%.4X %s ", cpu->thread->process->id, cpu->thread->id, cpu->thread->process->name.c_str());
        std::printf(msg, args ...);
    }
}

void sysLog1_nofmt(U32 type, CPU* cpu, const char* msg) {
    if (type & syscallMask) {
        printf("%.4X/%.4X %s ", cpu->thread->process->id, cpu->thread->id, cpu->thread->process->name.c_str());
        klog_nonewline(msg);
    }
}

#define SYS_LOG if (syscallMask) sysLog
#define SYS_LOG1 if (syscallMask) sysLog1
#define SYS_LOG1_NO_FMT if (syscallMask) sysLog1_nofmt

static U32 syscall_exit(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "exit: status=%d", ARG1);
    U32 result = cpu->thread->process->exit(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_read(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_READ, cpu, "read: fd=%d buf=0x%X len=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->read(cpu->thread, (FD)ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_READ, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_write(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_WRITE, cpu, "write: fd=%d buf=0x%X len=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->write(cpu->thread, (FD)ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_WRITE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_open(CPU* cpu, U32 eipCount) {
    BString name = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "open: name=%s flags=%x", name.c_str(), ARG2);
    U32 result = cpu->thread->process->open(name, ARG2);
#ifdef _DEBUG
    if (result>1000) {
        printf("open: name=%s flags=%x result=%X\n", name.c_str(), ARG2, result);
    }
#endif
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_close(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "close: fd=%d", ARG1);
    U32 result = cpu->thread->process->close(ARG1);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_waitpid(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "waitpid: pid=%d status=%d options=%x", ARG1, ARG2, ARG3);
    U32 result = KSystem::waitpid(cpu->thread, (S32)ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_link(CPU* cpu, U32 eipCount) {
    BString path1 = cpu->memory->readString(ARG1);
    BString path2 = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "link: path1=%X(%s) path2=%X(%s)", ARG1, path1.c_str(), ARG2, path2.c_str());
    U32 result = cpu->thread->process->link(path1, path2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_unlink(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "unlink: path=%X(%s)", ARG1, path.c_str());
    U32 result = cpu->thread->process->unlinkFile(path);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static void readStringArray(KMemory* memory, U32 address, std::vector<BString>& results) {
    while (true) {
        U32 p = memory->readd(address);
        if (!p)
            break;
        address += 4;
        results.push_back(memory->readString(p));
    }
}

static U32 syscall_execve(CPU* cpu, U32 eipCount) {
    std::vector<BString> args;
    std::vector<BString> envs;
    BString path = cpu->memory->readString(ARG1);

    readStringArray(cpu->memory, ARG2, args);
    readStringArray(cpu->memory, ARG3, envs);

    SYS_LOG1(SYSCALL_PROCESS, cpu, "execve: path=%X(%s) argv=%X envp=%X", ARG1, path.c_str(), ARG2, ARG3);
    U32 result = cpu->thread->process->execve(cpu->thread, path, args, envs);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_chdir(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_PROCESS, cpu, "chdir: path=%X(%s)", ARG1, path.c_str());
    U32 result = cpu->thread->process->chdir(path);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_time(CPU* cpu, U32 eipCount) {
    U32 result = (U32)(KSystem::getSystemTimeAsMicroSeconds() / 1000000l);
    if (ARG1)
        cpu->memory->writed(ARG1, result);
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "time: tloc=%X result=%d(0x%X)\n", ARG1, result, result);
    return result;
}

static U32 syscall_chmod(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "chmod: path=%X (%s) mode=%o", ARG1, path.c_str(), ARG2);
    U32 result = cpu->thread->process->chmod(path, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_lseek(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "lseek: fildes=%d offset=%d whence=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->lseek((FD)ARG1, (S32)ARG2, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getpid(CPU* cpu, U32 eipCount) {    
    U32 result = cpu->thread->process->id;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getpid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mount(CPU* cpu, U32 eipCount) {
    BString dev_name = cpu->memory->readString(ARG1);
    BString dir_name = cpu->memory->readString(ARG2);
    BString type = cpu->memory->readString(ARG3);
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "mount: dev_name=%s dir_name=%s type=%s flags=%x data=%x\n", dev_name.c_str(), dir_name.c_str(), type.c_str(), ARG4, ARG5);
    return 0;
}

static U32 syscall_getuid(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->userId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getuid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ptrace(CPU* cpu, U32 eipCount) {
    return -K_EPERM;
}

static U32 syscall_alarm(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "alarm: seconds=%d", ARG1);
    U32 result = cpu->thread->process->alarm(ARG1);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_utime(CPU* cpu, U32 eipCount) {
    BString fileName = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "utime: filename=%s times=%X", fileName.c_str(), ARG2);
    U32 result = 0;
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X) IGNOREDED\n", result, result);
    return result;
}

static U32 syscall_access(CPU* cpu, U32 eipCount) {
    BString fileName = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "access: filename=%s flags=0x%X", fileName.c_str(), ARG2);
    U32 result = cpu->thread->process->access(fileName, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sync(CPU* cpu, U32 eipCount) {
    SYS_LOG1_NO_FMT(SYSCALL_FILE, cpu, "sync:");
    U32 result = 0; 
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X) IGNOREDED\n", result, result);
    return result;
}

static U32 syscall_kill(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL|SYSCALL_PROCESS, cpu, "kill: pid=%d signal=%d", ARG1, ARG2);
    U32 result = KSystem::kill(ARG1, ARG2);
    SYS_LOG(SYSCALL_SIGNAL|SYSCALL_PROCESS, cpu, " result=%d(0x%X) IGNOREDED\n", result, result);
    return result;
}

static U32 syscall_rename(CPU* cpu, U32 eipCount) {
    BString oldName = cpu->memory->readString(ARG1);
    BString newName = cpu->memory->readString(ARG2);

    SYS_LOG1(SYSCALL_FILE, cpu, "rename: oldName=%X(%s) newName=%X(%s)", ARG1, oldName.c_str(), ARG2, newName.c_str());
    U32 result = cpu->thread->process->rename(oldName, newName);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mkdir(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "mkdir: path=%X (%s) mode=%X", ARG1, path.c_str(), ARG2);
    U32 result = cpu->thread->process->mkdir(path);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_rmdir(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "rmdir: path=%X(%s)", ARG1, path.c_str());
    U32 result = cpu->thread->process->rmdir(path);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_dup(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "dup: fildes=%d", ARG1);
    U32 result = cpu->thread->process->dup(ARG1);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pipe(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "pipe: fildes=%X", ARG1);
    U32 result = ksocketpair(cpu->thread, K_AF_UNIX, K_SOCK_STREAM, 0, ARG1, 0);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_times(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "times: buf=%X", ARG1);
    U32 result = KSystem::times(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_brk(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY|SYSCALL_PROCESS, cpu, "brk: address=%.8X", ARG1);
    U32 result = cpu->thread->process->brk(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_MEMORY|SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getgid(CPU* cpu, U32 eipCount) {    
    U32 result = cpu->thread->process->groupId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getgid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_geteuid(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->effectiveUserId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "geteuid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getegid(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->effectiveGroupId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getegid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ioctl(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "ioctl: fd=%d request=%x", ARG1, ARG2);
    U32 result = cpu->thread->process->ioctl(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fcntl(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "syscall_fcntl: fd=%d cmd=%d", ARG1, ARG2);
    U32 result = cpu->thread->process->fcntrl(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_setpgid(CPU* cpu, U32 eipCount) {	
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setpgid: pid=%d pgid=%d", ARG1, ARG2);
    U32 result = KSystem::setpgid(ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_umask(CPU* cpu, U32 eipCount) {	
    SYS_LOG1(SYSCALL_PROCESS, cpu, "umask: cmask=%X", ARG1);
    U32 result = cpu->thread->process->umask(ARG1);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getpgid(CPU* cpu, U32 eipCount) {	
    U32 result = KSystem::getpgid(ARG1);
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getpgid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_dup2(CPU* cpu, U32 eipCount) {	
    SYS_LOG1(SYSCALL_FILE, cpu, "dup2: fildes1=%d fildes2=%d", ARG1, ARG2);
    U32 result = cpu->thread->process->dup2(ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getppid(CPU* cpu, U32 eipCount) {	
    U32 result = cpu->thread->process->parentId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getppid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getpgrp(CPU* cpu, U32 eipCount) {	
    U32 result = cpu->thread->process->groupId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getpgrp: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_setsid(CPU* cpu, U32 eipCount) {	
#ifdef _DEBUG
    klog("setsid not implemented");
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setsid: result=%d(0x%X) IGNORED\n", result, result);
    return result;
}

static U32 syscall_setrlimit(CPU* cpu, U32 eipCount) {	
#ifdef _DEBUG
    klog("setrlimit not implemented");
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setrlimit: result=%d(0x%X) IGNORED\n", result, result);
    return result;
}

static U32 syscall_getrusuage(CPU* cpu, U32 eipCount) {	
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getrusage: who=%d usuage=%X", ARG1, ARG2);
    U32 result = cpu->thread->process->getrusuage(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_gettimeofday(CPU* cpu, U32 eipCount) {	
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "gettimeofday: tv=%X tz=%X", ARG1, ARG2);
    U32 result = KSystem::gettimeofday(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_symlink(CPU* cpu, U32 eipCount) {
    BString path1 = cpu->memory->readString(ARG1);
    BString path2 = cpu->memory->readString(ARG2);

    SYS_LOG1(SYSCALL_FILE, cpu, "symlink: path1=%X(%s) path2=%X(%s)", ARG1, path1.c_str(), ARG2, path2.c_str());
    U32 result = cpu->thread->process->symlink(path1, path2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_readlink(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    U32 result = cpu->thread->process->readlink(path, ARG2, ARG3);
    if (syscallMask) {
        BString link;
        if (result > 0) {
            link = cpu->memory->readString(ARG2);
        }
        SYS_LOG1(SYSCALL_FILE, cpu, "readlink: path=%X (%s) buffer=%X (%s) bufSize=%d result=%d(0x%X)\n", ARG1, path.c_str(), ARG2, link.c_str(), ARG3, result, result);
    }
    return result;
}

static U32 syscall_mmap64(CPU* cpu, U32 eipCount) {
    KMemory* memory = cpu->memory;
    SYS_LOG1(SYSCALL_MEMORY, cpu, "mmap: address=%.8X len=%d prot=%X flags=%X fd=%d offset=%d", memory->readd(ARG1), memory->readd(ARG1+4), memory->readd(ARG1+8), memory->readd(ARG1+12), memory->readd(ARG1+16), memory->readd(ARG1+20));
    U32 result = memory->mmap(cpu->thread, memory->readd(ARG1), memory->readd(ARG1+4), memory->readd(ARG1+8), memory->readd(ARG1+12), memory->readd(ARG1+16), memory->readd(ARG1+20));
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_unmap(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "munmap: address=%X len=%d", ARG1, ARG2);
    U32 result = cpu->memory->unmap(ARG1, ARG2);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ftruncate(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "ftruncate: fd=%X len=%d", ARG1, ARG2);
    U32 result = cpu->thread->process->ftruncate64(ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fchmod(CPU* cpu, U32 eipCount) {	
#ifdef _DEBUG
    klog("fchmod not implemented");
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "fchmod: fd=%X mod=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, result, result);
    return result;
}

static U32 syscall_setpriority(CPU* cpu, U32 eipCount) {	    
#ifdef _DEBUG
    klog("setpriority not implemented");
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setpriority: which=%d, who=%d, prio=%d result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_statfs(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);

    SYS_LOG1(SYSCALL_FILE, cpu, "statfs: path=%X(%s) buf=%X", ARG1, path.c_str(), ARG2);
    U32 result = cpu->thread->process->statfs(path, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ioperm(CPU* cpu, U32 eipCount) {	    
#ifdef _DEBUG
    klog_fmt("ioperm not implemented: from=0x%X len=%d on=%d", ARG1, ARG2, ARG3);
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "ioperm: from=%X num=%d turn_on=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_socketcall(CPU* cpu, U32 eipCount) {
    U32 result = 0;
    KMemory* memory = cpu->memory;

    switch (ARG1) {
        case 1: // SYS_SOCKET
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SOCKET: domain=%d(%s) type=%d(%s) protocol=%d(%s)", SARG2, SARG2==K_AF_UNIX?"AF_UNIX":(SARG2==K_AF_INET)?"AF_INET":"", (SARG3 & 0xFF), (SARG3 & 0xFF)==K_SOCK_STREAM?"SOCK_STREAM":((SARG3 & 0xFF)==K_SOCK_DGRAM)?"AF_SOCK_DGRAM":"", SARG4, (SARG4 == 0)?"IPPROTO_IP":(SARG4==6)?"IPPROTO_TCP":(SARG4==17)?"IPPROTO_UDP":"");
            result = ksocket(SARG2, SARG3 & 0xFF, SARG4);
            break;
        case 2: // SYS_BIND
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_BIND: socket=%d address=%X(%s) len=%d", SARG2, SARG3, socketAddressName(memory, SARG3, SARG4).c_str(), SARG4);
            result = kbind(cpu->thread, SARG2, SARG3, SARG4);
            break;
        case 3: // SYS_CONNECT
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_CONNECT: socket=%d address=%X(%s) len=%d", SARG2, SARG3, socketAddressName(memory, SARG3, SARG4).c_str(), SARG4);
            result = kconnect(cpu->thread, SARG2, SARG3, SARG4);
            break;
        case 4: // SYS_LISTEN				
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_LISTEN: socket=%d backlog=%d", SARG2, SARG3);
            result = klisten(cpu->thread, SARG2, SARG3);
            break;
        case 5: // SYS_ACCEPT
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_ACCEPT: socket=%d address=%X(%s) len=%d", SARG2, SARG3, socketAddressName(memory, SARG3, SARG4).c_str(), SARG4);
            result = kaccept(cpu->thread, SARG2, SARG3, SARG4, 0);
            break;			
        case 6: // SYS_GETSOCKNAME
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_GETSOCKNAME: socket=%d address=%X len=%d", SARG2, SARG3, SARG4);
            result = kgetsockname(cpu->thread, SARG2, SARG3, SARG4);
            break;			
        case 7: // SYS_GETPEERNAME
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_GETPEERNAME: socket=%d address=%X len=%d", SARG2, SARG3, SARG4);
            result = kgetpeername(cpu->thread, SARG2, SARG3, SARG4);
            break;		
        case 8: // SYS_SOCKETPAIR
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SOCKETPAIR: af=%d(%s) type=%d(%s) socks=%X", SARG2, SARG2==K_AF_UNIX?"AF_UNIX":(SARG2==K_AF_INET)?"AF_INET":"", SARG3, SARG3==K_SOCK_STREAM?"SOCK_STREAM":(SARG3==K_SOCK_DGRAM)?"AF_SOCK_DGRAM":"", SARG5);
            result = ksocketpair(cpu->thread, SARG2, SARG3, SARG4, SARG5, 0);
            break;
        case 9: // SYS_SEND
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SEND: socket=%d buffer=%X len=%d flags=%X", SARG2, SARG3, SARG4, SARG5);
            result = ksend(cpu->thread, SARG2, SARG3, SARG4, SARG5);
            break;
        case 10: // SYS_RECV
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_RECV: socket=%d buffer=%X len=%d flags=%X", SARG2, SARG3, SARG4, SARG5);
            result = krecv(cpu->thread, SARG2, SARG3, SARG4, SARG5);
            break;
        case 11: // SYS_SENDTO
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SENDTO: socket=%d buffer=%X len=%d flags=%X dest=%s", SARG2, SARG3, SARG4, SARG5, socketAddressName(memory, SARG6, SARG7).c_str());
            result = ksendto(cpu->thread, SARG2, SARG3, SARG4, SARG5, SARG6, SARG7);
            break;
        case 12: // SYS_RECVFROM
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_RECVFROM: socket=%d buffer=%X len=%d flags=%X address=%s", SARG2, SARG3, SARG4, SARG5, socketAddressName(memory, SARG6, SARG7).c_str());
            result = krecvfrom(cpu->thread, SARG2, SARG3, SARG4, SARG5, SARG6, SARG7);
            break;
        case 13: // SYS_SHUTDOWN
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SHUTDOWN: socket=%d how=%d", SARG2, SARG3);
            result = kshutdown(cpu->thread, SARG2, SARG3);
            break;
        case 14: // SYS_SETSOCKOPT
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SETSOCKOPT: socket=%d level=%d name=%d value=%d, len=%d", SARG2, SARG3, SARG4, SARG5, SARG6);
            result = ksetsockopt(cpu->thread, SARG2, SARG3, SARG4, SARG5, SARG6);
            break;
        case 15: // SYS_GETSOCKOPT
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_GETSOCKOPT: socket=%d level=%d name=%d value=%d, len=%d", SARG2, SARG3, SARG4, SARG5, SARG6);
            result = kgetsockopt(cpu->thread, SARG2, SARG3, SARG4, SARG5, SARG6);
            break;		
        case 16: // SYS_SENDMSG
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_SENDMSG: socket=%d message=%X flags=%X", SARG2, SARG3, SARG4);
            result = ksendmsg(cpu->thread, SARG2, SARG3, SARG4);
            break;
        case 17: // SYS_RECVMSG
            SYS_LOG1(SYSCALL_SOCKET, cpu, "SYS_RECVMSG: socket=%d message=%X flags=%X", SARG2, SARG3, SARG4);
            result = krecvmsg(cpu->thread, SARG2, SARG3, SARG4);
            break;
        //case 18: // SYS_ACCEPT4
        default:
            kpanic_fmt("Unknown socket syscall: %d",ARG1);
    }
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_setitimer(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "setitimer :which=%d newValue=%d(%d.%.06d) oldValue=%d", ARG1, ARG2, (ARG2?cpu->memory->readd(ARG2+8):0), (ARG2? cpu->memory->readd(ARG2+12):0), ARG3);
    U32 result = cpu->thread->process->setitimer(ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_iopl(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "iopl: level=%1 result=%d(0x%X) IGNORED\n", ARG1, result, result);
    return result;
}

static U32 syscall_wait4(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "wait4: pid=%d status=%d options=%x rusage=%X", ARG1, ARG2, ARG3, ARG4);
#ifdef _DEBUG
        if (ARG4) {
            kwarn("__NR_wait4 rusuage not implemented");
        }
#endif
    U32 result = KSystem::waitpid(cpu->thread, (S32)ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sysinfo(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sysinfo: address=%X", ARG1);
    U32 result = KSystem::sysinfo(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ipc(CPU* cpu, U32 eipCount) {
    U32 result = 0;

    // ARG5 holds the pointer to be copied
    if (ARG1 == 21) { // IPCOP_shmat
        SYS_LOG1(SYSCALL_SYSTEM|SYSCALL_MEMORY, cpu, "ipc: IPCOP_shmat shmid=%d shmaddr=%d shmflg=%X", ARG2, ARG5, ARG3);
        result = KSystem::shmat(cpu->thread, ARG2, ARG5, ARG3, ARG4, nullptr);
    }  else if (ARG1 == 22) { // IPCOP_shmdt
        SYS_LOG1(SYSCALL_SYSTEM|SYSCALL_MEMORY, cpu, "ipc IPCOP_shmdt shmaddr=%d", ARG5);
        result = KSystem::shmdt(cpu->thread, ARG5);
    } else if (ARG1 == 23) { // IPCOP_shmget
        //result = -1; // :TODO: this crashes hsetroot
        SYS_LOG1(SYSCALL_SYSTEM|SYSCALL_MEMORY, cpu, "ipc: IPCOP_shmget key=%d size=%d flags=%X", ARG2, ARG3, ARG4);
        result = KSystem::shmget(cpu->thread, ARG2, ARG3, ARG4);
    } else if (ARG1 == 24) { // IPCOP_shmctl 
        SYS_LOG1(SYSCALL_SYSTEM|SYSCALL_MEMORY, cpu, "ipc: IPCOP_shmctl shmid=%d cmd=%d buf=%X", ARG2, ARG3, ARG5);
        result = KSystem::shmctl(cpu->thread, ARG2, ARG3, ARG5);
    } else {
        kpanic_fmt("__NR_ipc op %d not implemented", ARG1);
        result = 0;
    }
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fsync(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fsync: fd=%d result=%d(0x%X) IGNORED\n", ARG1, result, result);
    return result;
}

static U32 syscall_sigreturn(CPU* cpu, U32 eipCount) {
    SYS_LOG1_NO_FMT(SYSCALL_SIGNAL, cpu, "sigreturn:");
    U32 result = cpu->thread->sigreturn();
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_rseq(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "rseq: struct rseq *rseq=%X rseq_len=%d flags=%X sig=%X", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->rseq(ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_shmget(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM | SYSCALL_MEMORY, cpu, "ipc: shmget key=%d size=%d flags=%X", ARG1, ARG2, ARG3);
    U32 result = KSystem::shmget(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_shmctl(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM | SYSCALL_MEMORY, cpu, "shmctl shmid=%d cmd=%d buf=%X", ARG1, ARG2, ARG3);
    U32 result = KSystem::shmctl(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_shmat(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM | SYSCALL_MEMORY, cpu, "shmat shmid=%d shmaddr=%d shmflg=%X", ARG1, ARG2, ARG3);
    U32 retAddress = 0;
    U32 result = KSystem::shmat(cpu->thread, ARG1, ARG2, ARG3, 0, &retAddress);
    if (result == 0) {
        result = retAddress;
    }
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clone3(CPU* cpu, U32 eipCount) {
    return -K_ENOSYS;
    /*
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clone3: args=%X size=%X", ARG1, ARG2);
    U32 result = cpu->thread->process->clone3(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
    */
}

static U32 syscall_clone(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clone: flags=%X child_stack=%X ptid=%X tls=%X ctid=%X", ARG1, ARG2, ARG3, ARG4, ARG5);
    U32 result = cpu->thread->process->clone(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_uname(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "uname: name=%.8X", ARG1);
    U32 result = KSystem::uname(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_modify_ldt(CPU* cpu, U32 eipCount) {
    KMemory* memory = cpu->memory;
    SYS_LOG1(SYSCALL_PROCESS, cpu, "modify_ldt: func=%d ptr=%X(index=%d address=%X limit=%X flags=%X) count=%d", ARG1, ARG2, memory->readd(ARG2), memory->readd(ARG2+4), memory->readd(ARG2+8), memory->readd(ARG2+12), ARG3);
    U32 result = cpu->thread->modify_ldt(ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mprotect(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "mprotect: address=%X len=%d prot=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->memory->mprotect(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fchdir(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "fchdir: fd=%d", ARG1);
    U32 result = cpu->thread->process->fchdir(ARG1);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_llseek(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "llseek: fildes=%d offset=%.8X%.8X pResult=%X whence=%d", ARG1, ARG2, ARG3, ARG4, ARG5);
    S64 r64 = cpu->thread->process->llseek(ARG1, ((U64)ARG2)<<32|ARG3, ARG5);
    if (ARG4 && r64>=0) {
        cpu->memory->writeq(ARG4, r64);
    }
    U32 result = 0;
    if (r64<0) {
        result = (U32)r64;
    }
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getdents(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "getdents: fd=%d dir=%X count=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->getdents(ARG1, ARG2, ARG3, false);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_newselect(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "newselect: nfd=%d readfds=%X writefds=%X errorfds=%X timeout=%d", ARG1, ARG2, ARG3, ARG4, ARG5);
    U32 result = kselect(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, true);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pselect6(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "pselect6: nfd=%d readfds=%X writefds=%X errorfds=%X timeout=%d sigmask=%X", ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);
    U32 result = kselect(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, false, ARG6);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pselect6_time64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "pselect6_time64: nfd=%d readfds=%X writefds=%X errorfds=%X timeout=%d sigmask=%X", ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);
    U32 result = kselect(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, false, ARG6, true);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_flock(CPU* cpu, U32 eipCount) {    
#ifdef _DEBUG
    klog("flock not implemented");
#endif
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "flock: fd=%d operation=%d result=%d(0x%X) IGNORED\n", ARG1, ARG2, result, result);
    return result;
}

static U32 syscall_msync(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "msync addr=%X length=%d flags=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->msync(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_writev(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_WRITE, cpu, "writev: filds=%d iov=0x%X iovcn=%d", ARG1, ARG2, ARG3);    
    U32 result = cpu->thread->process->writev(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_WRITE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fdatasync(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fdatasync: fd=%d result=%d(0x%X) IGNORED\n", ARG1, result, result);
    return result;
}

static U32 syscall_mlock(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "mlock: address=0x%X len=%d", ARG1, ARG2);
    U32 result = cpu->memory->mlock(ARG1, ARG2);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sched_getparam(CPU* cpu, U32 eipCount) {    
    U32 result = -K_EPERM;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_getparam: pid=%d params=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, result, result);
    return result;
}

static U32 syscall_sched_getscheduler(CPU* cpu, U32 eipCount) {    
    U32 result = 0; // SCHED_OTHER
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_getscheduler: pid=%d params=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, result, result);
    return result;
}

static U32 syscall_sched_yield(CPU* cpu, U32 eipCount) {
#ifndef BOXEDWINE_MULTI_THREADED
    cpu->yield = true;
#endif
    U32 result = 0;
    std::this_thread::yield();
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "yield: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sched_get_priority_max(CPU* cpu, U32 eipCount) {    
    U32 result = 32;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_get_priority_max: policy=%d result=%d(0x%X)\n", ARG1, result, result);
    return result;
}

static U32 syscall_sched_get_priority_min(CPU* cpu, U32 eipCount) {
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_get_priority_min: policy=%d result=%d(0x%X)\n", ARG1, result, result);
    return result;
}

static U32 syscall_clock_nanosleep(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "clock_nanosleep: clock=%d flags=%x req=%X(%d.%.09d sec) remaining=%X", ARG1, ARG2, ARG3, cpu->memory->readd(ARG3), cpu->memory->readd(ARG3 + 4), ARG4);
    U32 result = cpu->thread->clockNanoSleep(ARG1, ARG2, ((U64)cpu->memory->readd(ARG3)) * 1000000000l + cpu->memory->readd(ARG3 + 4), ARG4);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clock_nanosleep_time64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "clock_nanosleep_time64: clock=%d flags=%x req=%X(%d.%.09d sec) remaining=%X", ARG1, ARG2, ARG3, cpu->memory->readd(ARG3), cpu->memory->readd(ARG3 + 4), ARG4);
    U32 result = cpu->thread->clockNanoSleep(ARG1, ARG2, ((U64)cpu->memory->readq(ARG3)) * 1000000000l + cpu->memory->readd(ARG3 + 8), ARG4);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_nanosleep(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "nanosleep: req=%X(%d.%.09d sec)", ARG1, cpu->memory->readd(ARG1), cpu->memory->readd(ARG1+4));
    U32 result = cpu->thread->nanoSleep(((U64)cpu->memory->readd(ARG1))*1000000000l+ cpu->memory->readd(ARG1+4));
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mremap(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "mremap: oldaddress=%x oldsize=%d newsize=%d flags=%X", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->memory->mremap(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_vm86(CPU* cpu, U32 eipCount) {
    kpanic("Application tried to enter DOS mode (vm86).  BoxedWine does not support this.");
    return 0;
}

static U32 syscall_poll(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "poll: pfds=%X nfds=%d timeout=%X", ARG1, ARG2, ARG3);
    U32 result = kpoll(cpu->thread, ARG1, ARG2, ARG3);		
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_prctl(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "prctl: options=%d", ARG1);
    U32 result = cpu->thread->process->prctl(ARG1, ARG2);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_rt_sigaction(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "rt_sigaction: sig=%d act=%X oact=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->sigaction(ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_rt_sigprocmask(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "rt_sigprocmask: how=%d set=%X(%X) oset=%X", ARG1, ARG2, ARG2?(ARG4==4?cpu->memory->readd(ARG2):cpu->memory->readq(ARG2)):0, ARG3);
    U32 result = cpu->thread->sigprocmask(ARG1, ARG2, ARG3, ARG4);
    EAX = result;
    cpu->eip.u32+=eipCount;
    cpu->thread->runSignals();
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return -K_CONTINUE;
}

static U32 syscall_rt_sigsuspend(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "rt_sigsuspend: mask=%X", ARG1);
    U32 result = cpu->thread->sigsuspend(ARG1, ARG2);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pread64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_READ, cpu, "pread64: fd=%d buf=%X len=%d offset=%d", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->pread64(cpu->thread, ARG1, ARG2, ARG3, ARG4 | ((U64)ARG5) << 32);
    SYS_LOG(SYSCALL_READ, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pwrite64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_WRITE, cpu, "pwrite64: fd=%d buf=%X len=%d offset=%d", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->pwrite64(cpu->thread, ARG1, ARG2, ARG3, ARG4 | ((U64)ARG5) << 32);
    SYS_LOG(SYSCALL_WRITE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getcwd(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "getcwd: buf=%X size=%d (%s)", ARG1, ARG2, cpu->thread->process->currentDirectory.c_str());
    U32 result = cpu->thread->process->getcwd(ARG1, ARG2);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sigaltstack(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "sigaltstack ss=%X oss=%X", ARG1, ARG2);
    U32 result = cpu->thread->signalstack(ARG1, ARG2);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_vfork(CPU* cpu, U32 eipCount) {    
    U32 result = cpu->thread->process->clone(cpu->thread, 0x01000000 |0x00200000 | 0x00004000, 0, 0, 0, 0);
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "vfork: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ugetrlimit(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "ugetrlimit: resource=%d rlim=%X", ARG1, ARG2);
    U32 result = KSystem::ugetrlimit(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mmap2(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_MEMORY, cpu, "mmap2: address=%.8X len=0x%X(%d) prot=%X flags=%X fd=%d offset=%d", ARG1, ARG2, ARG2, ARG3, ARG4, ARG5, ARG6);
    U32 result = cpu->memory->mmap(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, ((U64)ARG6)*4096l);
    SYS_LOG(SYSCALL_MEMORY, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_ftruncate64(CPU* cpu, U32 eipCount) {
    U64 len = ARG2 | ((U64)ARG3 << 32);
    SYS_LOG1(SYSCALL_FILE, cpu, "ftruncate64: fildes=%d length=%llu", ARG1, len);
    U32 result = cpu->thread->process->ftruncate64(ARG1, len);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_statx(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "statx: dirfd=%d path=%s flags=%x mask=%x statxbuf=%x", ARG1, path.c_str(), ARG3, ARG4, ARG5);
    U32 result = cpu->thread->process->statx(ARG1, path, ARG3, ARG4, ARG5);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_stat64(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "stat64: path=%s buf=%X", path.c_str(), ARG2);
    U32 result = cpu->thread->process->stat64(path, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_lstat64(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "lstat64: path=%s buf=%X", path.c_str(), ARG2);
    U32 result = cpu->thread->process->lstat64(path, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fstat64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "fstat64: fildes=%d buf=%X", ARG1, ARG2);
    U32 result = cpu->thread->process->fstat64(ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_lchown32(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "lchown32: path=%s owner=%d group=%d result=%d(0x%X) IGNORED\n", path.c_str(), ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_getuid32(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->userId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getuid32: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getgid32(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->groupId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getgid32: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_geteuid32(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->effectiveUserId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "geteuid32: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getegid32(CPU* cpu, U32 eipCount) {
    U32 result = cpu->thread->process->effectiveGroupId;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getegid32: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getgroups32(CPU* cpu, U32 eipCount) {
    if (ARG2!=0) {
        cpu->memory->writed(ARG2, cpu->thread->process->groupId);
    }
    U32 result = 1;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getgroups32: size=%d list=%X result=%d(0x%X)\n", ARG1, ARG2, result, result);
    return result;
}

static U32 syscall_setgroups32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setgroups32: size=%d list=%X", ARG1, ARG2);
    if (ARG1) {
        cpu->thread->process->groupId = cpu->memory->readd(ARG2);
    }
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fchown32(CPU* cpu, U32 eipCount) {
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fchown32: fd=%d owner=%d group=%d result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_setresuid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setresuid3: ruid=%d euid=%d suid=%d", ARG1, ARG2, ARG3);
    if (ARG1!=0xFFFFFFFF)
        cpu->thread->process->userId = ARG1;
    if (ARG2!=0xFFFFFFFF)
        cpu->thread->process->effectiveUserId = ARG2;
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getresuid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getresuid32: ruid=%X(%d) euid=%X(%d) suid=%X(%d)", ARG1, cpu->thread->process->userId, ARG2, cpu->thread->process->effectiveUserId, ARG3, cpu->thread->process->userId);
    if (ARG1)
        cpu->memory->writed(ARG1, cpu->thread->process->userId);
    if (ARG2)
        cpu->memory->writed(ARG2, cpu->thread->process->effectiveUserId);
    if (ARG3)
        cpu->memory->writed(ARG3, cpu->thread->process->userId);
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_setresgid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setresgid32: rgid=%d egid=%d sgid=%d", ARG1, ARG2, ARG3);
    if (ARG1!=0xFFFFFFFF)
        cpu->thread->process->groupId = ARG1;
    if (ARG2!=0xFFFFFFFF)
        cpu->thread->process->effectiveGroupId = ARG2;
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getresgid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getresgid32: rgid=%X(%d) egid=%X(%d) sgid=%X(%d)", ARG1, cpu->thread->process->groupId, ARG2, cpu->thread->process->groupId, ARG3, cpu->thread->process->groupId);
    if (ARG1)
        cpu->memory->writed(ARG1, cpu->thread->process->groupId);
    if (ARG2)
        cpu->memory->writed(ARG2, cpu->thread->process->groupId);
    if (ARG3)
        cpu->memory->writed(ARG3, cpu->thread->process->groupId);
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_chown32(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "chown32: path=%s owner=%d group=%d", path.c_str(), ARG2, ARG3);
    U32 result = 0;
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X) IGNORED\n", result, result);
    return result;
}

static U32 syscall_setuid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setuid32: uid=%d", ARG1);
    cpu->thread->process->effectiveUserId = ARG1;
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_setgid32(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "setgid32: gid=%d", ARG1);
    cpu->thread->process->groupId = ARG1;
    U32 result = 0;
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mincore(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "mincore: address=%X length=%d vec=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->mincore(ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_madvise(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_MEMORY, cpu, "madvise: address=%X len=%d advise=%d result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_getdents64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "getdents64: fd=%d dir=%X count=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->getdents(ARG1, ARG2, ARG3, true);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fcntl64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "fcntl64: fildes=%d cmd=%d arg=%d", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->fcntrl(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_gettid(CPU* cpu, U32 eipCount) {    
    U32 result = cpu->thread->id;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "gettid: result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fsetxattr(CPU* cpu, U32 eipCount) {    
    U32 result = -K_ENOTSUP;
    BString name = cpu->memory->readString(ARG2);

    KFileDescriptorPtr fd = cpu->thread->process->getFileDescriptor(ARG1);
    if (!fd) {
        result = -K_EBADFD;
    } else if (name == "user.DOSATTRIB") {
        BString value = cpu->memory->readString(ARG3);
        std::shared_ptr<KFile> node = std::dynamic_pointer_cast<KFile>(fd->kobject);
        if (!node) {
            result = -K_ENOTSUP;
        } else {
            Fs::setDosAttrib(node->openFile->node, value);
        }
    }
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fsetxattr: result=%x\n", result);
    return result;
}

static U32 syscall_getxattr(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    BString name = cpu->memory->readString(ARG2);

    U32 result = -K_ENOTSUP;
    if (name == "user.DOSATTRIB") {
        std::shared_ptr<FsNode> file = Fs::getNodeFromLocalPath(cpu->thread->process->currentDirectory, path, true);
        if (!file) {
            result = -K_ENOENT;
        } else {
            BString attr = Fs::getDosAttrib(file);
            if (attr.length() == 0) {
                result = -K_ENODATA;
            } else if ((U32)attr.length() < ARG4) {
                cpu->memory->strcpy(ARG3, attr.c_str());
                result = attr.length();
            } else {
                result = -K_ERANGE;
            }
        }
    }
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getxattr: path=%s name=%s result = %x\n", path.c_str(), name.c_str(), result);
    return result;
}

static U32 syscall_lgetxattr(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    BString name = cpu->memory->readString(ARG2);
    U32 result = -K_ENOTSUP;
    if (name == "user.DOSATTRIB") {
        std::shared_ptr<FsNode> file = Fs::getNodeFromLocalPath(cpu->thread->process->currentDirectory, path, false);
        if (!file) {
            result = -K_ENOENT;
        } else {
            BString attr = Fs::getDosAttrib(file);
            if (attr.length() == 0) {
                result = -K_ENODATA;
            } else if ((U32)attr.length() < ARG4) {
                cpu->memory->strcpy(ARG3, attr.c_str());
                result = attr.length();
            } else {
                result = -K_ERANGE;
            }
        }
    }
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "lgetxattr: path=%s name=%s result=%x\n", path.c_str(), name.c_str(), result);
    return result;
}

static U32 syscall_fgetxattr(CPU* cpu, U32 eipCount) {
    U32 result = -K_ENOTSUP;
    BString name = cpu->memory->readString(ARG2);

    KFileDescriptorPtr fd = cpu->thread->process->getFileDescriptor(ARG1);
    if (!fd) {
        result = -K_EBADFD;
    } else if (name == "user.DOSATTRIB") {
        std::shared_ptr<KFile> node = std::dynamic_pointer_cast<KFile>(fd->kobject);
        if (!node) {
            result = -K_ENOTSUP;
        } else {
            BString attr = Fs::getDosAttrib(node->openFile->node);
            if (attr.length() == 0) {
                result = -K_ENODATA;
            } else if ((U32)attr.length() < ARG4) {
                cpu->memory->strcpy(ARG3, attr.c_str());
                result = attr.length();
            } else {
                result = -K_ERANGE;
            }
        }
    }

    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fgetxattr: result = %x\n", result);
    return result;
}

static U32 syscall_flistxattr(CPU* cpu, U32 eipCount) {
    U32 result = -K_ENOTSUP;
    SYS_LOG1_NO_FMT(SYSCALL_SYSTEM, cpu, "flistxattr: result = ENOTSUP IGNORED\n");
    return result;
}

static U32 syscall_fremovexattr(CPU* cpu, U32 eipCount) {
    U32 result = -K_ENOTSUP;
    BString name = cpu->memory->readString(ARG2);

    KFileDescriptorPtr fd = cpu->thread->process->getFileDescriptor(ARG1);
    if (!fd) {
        result = -K_EBADFD;
    } else if (name == "user.DOSATTRIB") {
        std::shared_ptr<KFile> node = std::dynamic_pointer_cast<KFile>(fd->kobject);
        if (!node) {
            result = -K_ENOTSUP;
        } else {
            result = Fs::removeDosAttrib(node->openFile->node);            
        }
    }

    SYS_LOG1(SYSCALL_SYSTEM, cpu, "fremovexattr: fd=%d name=%s result = %x\n", ARG1, name.c_str(), result);
    return result;
}

static U32 syscall_sendfile64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sendfile64: out_fd=%d int_fd=%d offset=%d count=%d", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->sendFile(ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129
#define FUTEX_WAIT_BITSET_PRIVATE 137
#define FUTEX_WAKE_BITSET_PRIVATE 138

static const char* getFutexOp(U32 op) {
    if (op==0) return "WAIT";
    if (op==1) return "WAKE";
    if (op==128) return "WAIT PRIVATE";
    if (op==129) return "WAKE PRIVATE";
    if (op == 137) return "WAIT BITSET PRIVATE";
    if (op == 138) return "WAKE BITSET PRIVATE";
    static BString tmp;
    tmp = BString::valueOf(op);
    return tmp.c_str();
}

static U32 syscall_futex(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FUTEX, cpu, "futex start: address=%X op=%s value=%d\n", ARG1, getFutexOp(ARG2), ARG3);
    U32 result = cpu->thread->futex(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, false);
    SYS_LOG1(SYSCALL_FUTEX, cpu, "futex   end: address=%X op=%s value=%d result=%d(0x%X)\n", ARG1, getFutexOp(ARG2), ARG3, result, result);
    return result;
}

static U32 syscall_futex_time64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FUTEX, cpu, "futex time64 start: address=%X op=%s value=%d\n", ARG1, getFutexOp(ARG2), ARG3);
    U32 result = cpu->thread->futex(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, true);
    SYS_LOG1(SYSCALL_FUTEX, cpu, "futex time64   end: address=%X op=%s value=%d result=%d(0x%X)\n", ARG1, getFutexOp(ARG2), ARG3, result, result);
    return result;
}

#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
/* Private high syscall number. Only the Azrok wait-probe Wine module emits it. */
static U32 syscall_azrok_wait_marker(CPU* cpu, U32 eipCount) {
    markAzrokWaitFrameProfile(ARG1, ARG2, ARG3, ARG4);
    return 0;
}
#endif

static U32 syscall_sched_setaffinity(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_setaffinity: pid=%d cpusetsize=d cpu_set_t=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_sched_getaffinity(CPU* cpu, U32 eipCount) {    
#ifdef _DEBUG
     kwarn("__NR_sched_getaffinity not implemented");
#endif
    U32 result = -K_EPERM;
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "sched_getaffinity: pid=%d cpusetsize=%d mask=%X result=%d(0x%X) IGNORED\n", ARG1, ARG2, ARG3, result, result);
    return result;
}

static U32 syscall_set_thread_area(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "set_thread_area: u_info=%X", ARG1);
    U32 result = cpu->thread->process->set_thread_area(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_exit_group(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "exit_group: code=%d", ARG1);
    U32 result = cpu->thread->process->exitgroup(cpu->thread, ARG1);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_epoll_create(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "epoll_create: size=%d", ARG1);
    U32 result = cpu->thread->process->epollcreate(ARG1, 0);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_epoll_ctl(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "epoll_ctl: epfd=%d op=%d fd=%d events=%X", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->epollctl(ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_epoll_wait(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS|SYSCALL_THREAD, cpu, "epoll_wait: epfd=%d events=%X maxevents=%d timeout=%d", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->epollwait(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_PROCESS|SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_set_tid_address(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "set_tid_address: address=%X", ARG1);
    cpu->thread->clear_child_tid = ARG1;
    U32 result = cpu->thread->id;
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clock_gettime(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clock_gettime: clock_id=%d tp=%X", ARG1, ARG2);
    U32 result = KSystem::clock_gettime(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clock_gettime64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clock_gettime64: clock_id=%d tp=%X", ARG1, ARG2);
    U32 result = KSystem::clock_gettime64(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clock_getres(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clock_getres: clock_id=%d res=%X", ARG1, ARG2);
    U32 result = KSystem::clock_getres(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_clock_getres_time64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "clock_getres_time64: clock_id=%d res=%X", ARG1, ARG2);
    U32 result = KSystem::clock_getres64(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_statfs64(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "fstatfs64: path=%X(%s) len=%d buf=%X", ARG1, path.c_str(), ARG2, ARG3);
    U32 result = cpu->thread->process->statfs64(path, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fstatfs64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "fstatfs64: fd=%d len=%d buf=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->process->fstatfs64(ARG1, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_tgkill(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "tgkill: threadGroupId=%d threadId=%d signal=%d", ARG1, ARG2, ARG3);
    U32 result = KSystem::tgkill(ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_utimes(CPU* cpu, U32 eipCount) {
    BString fileName = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "utimes: fileName=%s times=%X", fileName.c_str(), ARG2);
    U32 result = cpu->thread->process->utimes(fileName, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fadvise64(CPU* cpu, U32 eipCount) {    
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "fadvise64_64: fd=%d result=%d(0x%X) IGNORED\n", ARG1, result, result);
    return result;
}

static U32 syscall_inotify_init(CPU* cpu, U32 eipCount) {    
    U32 result = -K_ENOSYS;
    SYS_LOG1(SYSCALL_FILE, cpu, "inotify_init: result=%d(0x%X) IGNORED\n", result, result);
    return result;
}

static U32 syscall_openat(CPU* cpu, U32 eipCount) {
    BString name = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "openat: dirfd=%d name=%s flags=%x", ARG1, name.c_str(), ARG3);
#ifdef _DEBUG    
    if (name == "c_rehash.sh") {
        // not sure why installing ca_certificats with TinyCore Linux 15 needs this
        klog("c_rehash.sh hack");
        name = "/usr/local/sbin/c_rehash.sh";
    }
#endif
    U32 result = cpu->thread->process->openat(ARG1, name, ARG3);
#ifdef _DEBUG
    if (result>1000 && !name.contains("font") && !name.startsWith("/sys")) {
        printf("openat: dirfd=%d name=%s flags=%x result=%x\n", (int)ARG1, name.c_str(), ARG3, result);
    }
#endif
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_mkdirat(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "mkdirat: dirfd=%d path=%s mode=%x", ARG1, path.c_str(), ARG3);
    U32 result = cpu->thread->process->mkdirat(ARG1, path, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_fchownat(CPU* cpu, U32 eipCount) {
    BString pathname = cpu->memory->readString(ARG2);
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "fchown32: pathname=%X(%s) owner=%d group=%d flags=%d result=%d(0x%X) IGNORED\n", ARG2, pathname.c_str(), ARG3, ARG4, ARG5, result, result);
    return result;
}

static U32 syscall_fstatat64(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "statat64: dirfd=%d path=%s buf=%X flags=%x", ARG1, path.c_str(), ARG3, ARG4);
    U32 result = cpu->thread->process->fstatat64(ARG1, path, ARG3, ARG4);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_unlinkat(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "unlinkat: dirfd=%d path=%s flags=%x", ARG1, path.c_str(), ARG3);
    U32 result = cpu->thread->process->unlinkat(ARG1, path, ARG3);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_symlinkat(CPU* cpu, U32 eipCount) {
    BString oldpath = cpu->memory->readString(ARG1);
    BString newpath = cpu->memory->readString(ARG3);
    SYS_LOG1(SYSCALL_FILE, cpu, "symlinkat: oldpath=%x(%s) dirfd=%d newpath=%X(%s)", ARG1, oldpath.c_str(), ARG2, ARG3, newpath.c_str());
    U32 result = cpu->thread->process->symlinkat(oldpath, ARG2, newpath);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_readlinkat(CPU* cpu, U32 eipCount) {
    if (!cpu->memory->canRead(ARG2 >> K_PAGE_SHIFT)) {
        return -K_EFAULT;
    }
    BString pathname = cpu->memory->readString(ARG2);    
    SYS_LOG1(SYSCALL_FILE, cpu, "readlinkat: dirfd=%d pathname=%X(%s) ", ARG1, ARG2, pathname.c_str());
    U32 result = cpu->thread->process->readlinkat(ARG1, pathname, ARG3, ARG4);
    if (syscallMask) {
        BString buf = cpu->memory->readString(ARG3);
        SYS_LOG(SYSCALL_FILE, cpu, "buf = %X(%s) bufsiz=%d result=%d(0x%X)\n", ARG3, buf.c_str(), ARG4, result, result);
    }
    return result;
}

static U32 syscall_fchmodat(CPU* cpu, U32 eipCount) {
    BString pathname = cpu->memory->readString(ARG2);
    U32 result = 0;
    SYS_LOG1(SYSCALL_FILE, cpu, "fchmodat pathname=%X(%s) mode=%X flags=%X result=%d(0x%X) IGNORED\n", ARG2, pathname.c_str(), ARG3, ARG4, result, result);
    return result;
}

static U32 syscall_faccessat(CPU* cpu, U32 eipCount) {
    BString pathname = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "faccessat dirfd=%X pathname=%X(%s) mode=%X flags=%X", ARG1, ARG2, pathname.c_str(), ARG3, ARG4);
    U32 result = cpu->thread->process->faccessat(ARG1, pathname, ARG3, ARG4);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_faccessat2(CPU* cpu, U32 eipCount) {
    BString pathname = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "faccessat2 dirfd=%X pathname=%X(%s) mode=%X flags=%X", ARG1, ARG2, pathname.c_str(), ARG3, ARG4);
    U32 result = cpu->thread->process->faccessat(ARG1, pathname, ARG3, ARG4);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_set_robust_list(CPU* cpu, U32 eipCount) {    
    SYS_LOG1(SYSCALL_THREAD, cpu, "set_robust_list: head=%X len=%X", ARG1, ARG2);
    U32 result = cpu->thread->set_robust_list(ARG1, ARG2);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

/*
static U32 syscall_get_robust_list(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_THREAD, cpu, "set_robust_list: pid=%X head_ptr=%X len_ptr=%X", ARG1, ARG2, ARG3);
    U32 result = cpu->thread->get_robust_list(ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_THREAD, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}
*/

static U32 syscall_sync_file_range(CPU* cpu, U32 eipCount) {
    U32 result = 0;
    SYS_LOG1(SYSCALL_THREAD, cpu, "sync_file_range: result=%d(0x%X) IGNORED\n", result, result);
    return result;
}

static U32 syscall_utimensat(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "utimensat dirfd=%d path=%X(%s) times=%X flags=%X", ARG1, ARG2, path.c_str(), ARG3, ARG4);
    U32 result = cpu->thread->process->utimesat(ARG1, path, ARG3, ARG4, false);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_timerfd_create(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "timerfd_create clockid=%d flags=%X", ARG1, ARG2);
    U32 result = cpu->thread->process->timerfd_create(ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_timerfd_settime(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "timerfd_settime fd=%d flags=%X newValue=%X oldValue=%x", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->process->timerfd_settime(ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_timerfd_gettime(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_FILE, cpu, "timerfd_settime fd=%d value=%x", ARG1, ARG2);
    U32 result = cpu->thread->process->timerfd_gettime(ARG1, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_utimensat_time64(CPU* cpu, U32 eipCount) {
    BString path = cpu->memory->readString(ARG2);
    SYS_LOG1(SYSCALL_FILE, cpu, "utimensat_time64 dirfd=%d path=%X(%s) times=%X flags=%X", ARG1, ARG2, path.c_str(), ARG3, ARG4);
    U32 result = cpu->thread->process->utimesat(ARG1, path, ARG3, ARG4, true);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_signalfd4(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "signalfd4 fd=%d mask=%X(0x%0.8X%0.8X) size=%d flags=%X", ARG1, ARG2, (ARG3>=8?cpu->memory->readd(ARG2+4):0), cpu->memory->readd(ARG2), ARG3, ARG4);
    U32 result = syscall_signalfd4(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_eventfd2(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SIGNAL, cpu, "eventfd2 value=%d flags=%X", ARG1, ARG2);
    U32 result = syscall_eventfd2(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SIGNAL, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_epoll_create1(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_PROCESS, cpu, "epoll_create1: falgs=%X", ARG1);
    U32 result = cpu->thread->process->epollcreate(0, ARG1);
    SYS_LOG(SYSCALL_PROCESS, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_pipe2(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "pipe2 fildes=%X", ARG1);
    U32 result = ksocketpair(cpu->thread, K_AF_UNIX, K_SOCK_STREAM, 0, ARG1, ARG2);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_prlimit64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SYSTEM, cpu, "prlimit64 pid=%d resource=%d newlimit=%X oldlimit=%X", ARG1, ARG2, ARG3, ARG4);
    U32 result = KSystem::prlimit64(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_sendmmsg(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "sendmmsg fd=%d address=%X vlen=%d flags=%X", ARG1, ARG2, ARG3, ARG4);
    U32 result = ksendmmsg(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_renameat(CPU* cpu, U32 eipCount) {
    BString oldpath = cpu->memory->readString(ARG2);
    BString newpath = cpu->memory->readString(ARG4);
    SYS_LOG1(SYSCALL_FILE, cpu, "renameat olddirfd=%d oldpath=%X(%s) newdirfd=%d newpath=%X(%s)", ARG1, ARG2, oldpath.c_str(), ARG3, ARG4, newpath.c_str());
    U32 result = cpu->thread->process->renameat(ARG1, oldpath, ARG3, newpath);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static U32 syscall_getrandom(CPU* cpu, U32 eipCount) {
    static std::mt19937 gen{ std::random_device{}() };
    static std::uniform_int_distribution<size_t> dist{ 0, 255 };

    SYS_LOG1(SYSCALL_SYSTEM, cpu, "getrandom buf=%X buflen=%d flags=%X", ARG1, ARG2, ARG3);
    U32 buf = ARG1;
    U32 count = ARG2;
    for (U32 i=0;i<count;i++) {
        cpu->memory->writeb(buf+i, (U8)dist(gen));
    }
    SYS_LOG(SYSCALL_SYSTEM, cpu, " result=%d\n", count);
    return count;
}

static U32 syscall_memfd_create(CPU* cpu, U32 eipCount) {
    BString name = cpu->memory->readString(ARG1);
    SYS_LOG1(SYSCALL_FILE, cpu, "name=%X(%s) flags=%X", ARG1, name.c_str(), ARG2);
    U32 result = cpu->thread->process->memfd_create(name, ARG2);
    SYS_LOG(SYSCALL_FILE, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_socket(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "socket domain=%d(%s) type=%d(%s) protocol=%d(%s)", ARG1, ARG1==K_AF_UNIX?"AF_UNIX":(ARG1==K_AF_INET)?"AF_INET":"", (ARG2 & 0xFF), (ARG2 & 0xFF)==K_SOCK_STREAM?"SOCK_STREAM":((ARG2 & 0xFF)==K_SOCK_DGRAM)?"AF_SOCK_DGRAM":"", ARG3, (ARG3 == 0)?"IPPROTO_IP":(ARG3==6)?"IPPROTO_TCP":(ARG3==17)?"IPPROTO_UDP":"");
    U32 result = ksocket(ARG1, ARG2 & 0xFF, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_socketpair(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "socketpair af=%d(%s) type=%d(%s) socks=%X", ARG1, ARG1==K_AF_UNIX?"AF_UNIX":(ARG1==K_AF_INET)?"AF_INET":"", ARG2, ARG2==K_SOCK_STREAM?"SOCK_STREAM":(ARG2==K_SOCK_DGRAM)?"AF_SOCK_DGRAM":"", ARG4);
    U32 result = ksocketpair(cpu->thread, ARG1, ARG2, ARG3, ARG4, 0);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_bind(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "bind socket=%d address=%X(%s) len=%d", ARG1, ARG2, socketAddressName(cpu->memory, ARG2, ARG3).c_str(), ARG3);
    U32 result = kbind(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_connect(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "connect socket=%d address=%X(%s) len=%d", ARG1, ARG2, socketAddressName(cpu->memory, ARG2, ARG3).c_str(), ARG3);
    U32 result = kconnect(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_listen(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "listen socket=%d backlog=%d", ARG1, ARG2);
    U32 result = klisten(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_accept4(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "accept4 socket=%d address=%X(%s) len=%d flags=%X", ARG1, ARG2, socketAddressName(cpu->memory, ARG2, ARG3).c_str(), ARG3, ARG4);
    U32 result = kaccept(cpu->thread, ARG1, ARG2, ARG3, ARG4);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_getsockopt(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "getsockopt socket=%d level=%d name=%d value=%d, len=%d", ARG1, ARG2, ARG3, ARG4, ARG5);
    U32 result = kgetsockopt(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_setsockopt(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "setsockopt socket=%d level=%d name=%d value=%d, len=%d", ARG1, ARG2, ARG3, ARG4, ARG5);
    U32 result = ksetsockopt(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_getsockname(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "getsockname socket=%d address=%X len=%d", ARG1, ARG2, ARG3);
    U32 result = kgetsockname(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_getpeername(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "getpeername socket=%d address=%X len=%d", ARG1, ARG2, ARG3);
    U32 result = kgetpeername(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_sendto(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "sendto socket=%d buffer=%X len=%d flags=%X dest=%s", ARG1, ARG2, ARG3, ARG4, socketAddressName(cpu->memory, ARG5, ARG6).c_str());
    U32 result = ksendto(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_sendmsg(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "sendmsg socket=%d message=%X flags=%X", ARG1, ARG2, ARG3);
    U32 result = ksendmsg(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_recvfrom(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "recvfrom socket=%d buffer=%X len=%d flags=%X address=%s", ARG1, ARG2, ARG3, ARG4, socketAddressName(cpu->memory, ARG5, ARG6).c_str());
    U32 result = krecvfrom(cpu->thread, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_recvmsg(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "recvmsg socket=%d message=%X flags=%X", ARG1, ARG2, ARG3);
    U32 result = krecvmsg(cpu->thread, ARG1, ARG2, ARG3);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_shutdown(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "shutdown socket=%d how=%d", ARG1, ARG2);
    U32 result = kshutdown(cpu->thread, ARG1, ARG2);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

U32 syscall_sigtimedwait_time64(CPU* cpu, U32 eipCount) {
    SYS_LOG1(SYSCALL_SOCKET, cpu, "sigtimedwait set=%x info=%x timeout=%x sizeofSet=%d", ARG1, ARG2, ARG3, ARG4);
    U32 result = cpu->thread->sigtimedwait(ARG1, ARG2, ARG3, ARG4, true);
    SYS_LOG(SYSCALL_SOCKET, cpu, " result=%d(0x%X)\n", result, result);
    return result;
}

static const SyscallFunc syscallFunc[] = {
    nullptr,                  // 0
    syscall_exit,       // 1 __NR_exit
    nullptr,                  // 2
    syscall_read,       // 3 __NR_read
    syscall_write,      // 4 __NR_write
    syscall_open,       // 5 __NR_open
    syscall_close,      // 6 __NR_close
    syscall_waitpid,    // 7 __NR_waitpid
    nullptr,                  // 8
    syscall_link,       // 9 __NR_link
    syscall_unlink,     // 10 __NR_unlink
    syscall_execve,     // 11 __NR_execve
    syscall_chdir,      // 12 __NR_chdir
    syscall_time,       // 13 __NR_time
    nullptr,                  // 14
    syscall_chmod,      // 15 __NR_chmod
    nullptr,                  // 16
    nullptr,                  // 17
    nullptr,                  // 18
    syscall_lseek,      // 19 __NR_lseek
    syscall_getpid,     // 20 __NR_getpid
    syscall_mount,            // 21
    nullptr,                  // 22
    nullptr,                  // 23
    syscall_getuid,     // 24 __NR_getuid
    nullptr,                  // 25
    syscall_ptrace,     // 26 __NR_ptrace
    syscall_alarm,      // 27 __NR_alarm
    nullptr,                  // 28
    nullptr,                  // 29
    syscall_utime,      // 30 __NR_utime
    nullptr,                  // 31
    nullptr,                  // 32
    syscall_access,     // 33 __NR_access
    nullptr,                  // 34
    nullptr,                  // 35
    syscall_sync,       // 36 __NR_sync
    syscall_kill,       // 37 __NR_kill
    syscall_rename,     // 38 __NR_rename
    syscall_mkdir,      // 39 __NR_mkdir
    syscall_rmdir,      // 40 __NR_rmdir
    syscall_dup,        // 41 __NR_dup
    syscall_pipe,       // 42 __NR_pipe
    syscall_times,      // 43 __NR_times
    nullptr,                  // 44
    syscall_brk,        // 45 __NR_brk
    nullptr,                  // 46
    syscall_getgid,     // 47 __NR_getgid
    nullptr,                  // 48
    syscall_geteuid,    // 49 __NR_geteuid
    syscall_getegid,    // 50 __NR_getegid
    nullptr,                  // 51
    nullptr,                  // 52
    nullptr,                  // 53
    syscall_ioctl,            // 54 __NR_ioctl
    syscall_fcntl,            // 55
    nullptr,                  // 56
    syscall_setpgid,          // 57 __NR_setpgid
    nullptr,                  // 58
    nullptr,                  // 59
    syscall_umask,      // 60 __NR_umask
    nullptr,                  // 61
    nullptr,                  // 62
    syscall_dup2,       // 63 __NR_dup2
    syscall_getppid,    // 64 __NR_getppid
    syscall_getpgrp,    // 65 __NR_getpgrp
    syscall_setsid,     // 66 __NR_setsid
    nullptr,                  // 67
    nullptr,                  // 68
    nullptr,                  // 69
    nullptr,                  // 70
    nullptr,                  // 71
    nullptr,                  // 72
    nullptr,                  // 73
    nullptr,                  // 74
    syscall_setrlimit,  // 75 __NR_setrlimit 
    nullptr,                  // 76
    syscall_getrusuage, // 77 __NR_getrusage
    syscall_gettimeofday,// 78__NR_gettimeofday
    nullptr,                  // 79
    nullptr,                  // 80
    nullptr,                  // 81
    nullptr,                  // 82
    syscall_symlink,    // 83 __NR_symlink
    nullptr,                  // 84
    syscall_readlink,   // 85 __NR_readlink
    nullptr,                  // 86
    nullptr,                  // 87
    nullptr,                  // 88
    nullptr,                  // 89
    syscall_mmap64,     // 90 __NR_mmap
    syscall_unmap,      // 91 __NR_munmap
    nullptr,                  // 92
    syscall_ftruncate,  // 93 __NR_ftruncate
    syscall_fchmod,     // 94 __NR_fchmod
    nullptr,                  // 95
    nullptr,                  // 96
    syscall_setpriority,// 97 __NR_setpriority
    nullptr,                  // 98
    syscall_statfs,     // 99 __NR_statfs
    nullptr,                  // 100
    syscall_ioperm,     // 101 __NR_ioperm
    syscall_socketcall, // 102 __NR_socketcall
    nullptr,                  // 103
    syscall_setitimer,  // 104 __NR_setitimer 
    nullptr,                  // 105
    nullptr,                  // 106
    nullptr,                  // 107
    nullptr,                  // 108
    nullptr,                  // 109
    syscall_iopl,       // 110 __NR_iopl
    nullptr,                  // 111
    nullptr,                  // 112
    nullptr,                  // 113
    syscall_wait4,      // 114 __NR_wait4
    nullptr,                  // 115
    syscall_sysinfo,    // 116 __NR_sysinfo
    syscall_ipc,        // 117 __NR_ipc
    syscall_fsync,      // 118 __NR_fsync
    syscall_sigreturn,  // 119 __NR_sigreturn
    syscall_clone,      // 120 __NR_clone
    nullptr,                  // 121
    syscall_uname,      // 122 __NR_uname
    syscall_modify_ldt, // 123 __NR_modify_ldt
    nullptr,                  // 124
    syscall_mprotect,   // 125 __NR_mprotect
    nullptr,                  // 126
    nullptr,                  // 127
    nullptr,                  // 128
    nullptr,                  // 129
    nullptr,                  // 130
    nullptr,                  // 131
    syscall_getpgid,    // 132 __NR_getpgid
    syscall_fchdir,     // 133 __NR_fchdir
    nullptr,                  // 134
    nullptr,                  // 135
    nullptr,                  // 136
    nullptr,                  // 137
    nullptr,                  // 138
    nullptr,                  // 139
    syscall_llseek,     // 140 __NR__llseek
    syscall_getdents,   // 141 __NR_getdents
    syscall_newselect,  // 142 __NR_newselect
    syscall_flock,      // 143 __NR_flock
    syscall_msync,      // 144 __NR_msync
    nullptr,                  // 145
    syscall_writev,     // 146  __NR_writev
    nullptr,                  // 147
    syscall_fdatasync,  // 148 __NR_fdatasync
    nullptr,                  // 149
    syscall_mlock,      // 150 __NR_mlock
    nullptr,                  // 151
    nullptr,                  // 152
    nullptr,                  // 153
    nullptr,                  // 154
    syscall_sched_getparam, // 155 __NR_sched_getparam
    nullptr,                  // 156
    syscall_sched_getscheduler, // 157 __NR_sched_getscheduler
    syscall_sched_yield,// 158 __NR_sched_yield
    syscall_sched_get_priority_max, // 159 __NR_sched_get_priority_max
    syscall_sched_get_priority_min, // 160 __NR_sched_get_priority_min
    nullptr,                  // 161
    syscall_nanosleep,  // 162 __NR_nanosleep
    syscall_mremap,     // 163 __NR_mremap
    nullptr,                  // 164
    nullptr,                  // 165
    syscall_vm86,       // 166 __NR_vm86
    nullptr,                  // 167
    syscall_poll,       // 168 __NR_poll
    nullptr,                  // 169
    nullptr,                  // 170
    nullptr,                  // 171
    syscall_prctl,      // 172 __NR_prctl
    nullptr,                  // 173
    syscall_rt_sigaction,// 174 __NR_rt_sigaction
    syscall_rt_sigprocmask, // 175 __NR_rt_sigprocmask
    nullptr,                  // 176
    nullptr,                  // 177
    nullptr,                  // 178
    syscall_rt_sigsuspend, // 179 __NR_rt_sigsuspend
    syscall_pread64,    // 180 __NR_pread64
    syscall_pwrite64,   // 181 __NR_pwrite64
    nullptr,                  // 182
    syscall_getcwd,     // 183 __NR_getcwd
    nullptr,                  // 184
    nullptr,                  // 185
    syscall_sigaltstack,// 186 __NR_sigaltstack
    nullptr,                  // 187
    nullptr,                  // 188
    nullptr,                  // 189
    syscall_vfork,      // 190 __NR_vfork
    syscall_ugetrlimit, // 191 __NR_ugetrlimit
    syscall_mmap2,      // 192 __NR_mmap2
    nullptr,                  // 193
    syscall_ftruncate64,// 194 __NR_ftruncate64
    syscall_stat64,     // 195 __NR_stat64
    syscall_lstat64,    // 196 __NR_lstat64
    syscall_fstat64,    // 197 __NR_fstat64
    syscall_lchown32,   // 198 __NR_lchown32
    syscall_getuid32,   // 199 __NR_getuid32
    syscall_getgid32,   // 200 __NR_getgid32
    syscall_geteuid32,  // 201 __NR_geteuid32
    syscall_getegid32,  // 202 __NR_getegid32
    nullptr,                  // 203
    nullptr,                  // 204
    syscall_getgroups32,// 205 __NR_getgroups32
    syscall_setgroups32,// 206 __NR_setgroups32
    syscall_fchown32,   // 207 __NR_fchown32
    syscall_setresuid32,// 208 __NR_setresuid32
    syscall_getresuid32,// 209 __NR_getresuid32
    syscall_setresgid32,// 210 __NR_setresgid32
    syscall_getresgid32,// 211 __NR_getresgid32
    syscall_chown32,    // 212 __NR_chown32
    syscall_setuid32,   // 213 __NR_setuid32
    syscall_setgid32,   // 214 __NR_setgid32
    nullptr,                  // 215
    nullptr,                  // 216
    nullptr,                  // 217
    syscall_mincore,    // 218 __NR_mincore
    syscall_madvise,    // 219 __NR_madvise
    syscall_getdents64, // 220 __NR_getdents64
    syscall_fcntl64,    // 221 __NR_fcntl64
    nullptr,                  // 222
    nullptr,                  // 223
    syscall_gettid,     // 224 __NR_gettid
    nullptr,                  // 225
    nullptr,                  // 226
    nullptr,                  // 227
    syscall_fsetxattr,  // 228 __NR_fsetxattr
    syscall_getxattr,   // 229
    syscall_lgetxattr,  // 230
    syscall_fgetxattr,  // 231 __NR_fgetxattr
    nullptr,                  // 232
    nullptr,                  // 233
    syscall_flistxattr, // 234 __NR_flistxattr
    nullptr,                  // 235
    nullptr,                  // 236
    syscall_fremovexattr,     // 237
    nullptr,                  // 238 __NR_tkill
    syscall_sendfile64,       // 239
    syscall_futex,      // 240 __NR_futex
    syscall_sched_setaffinity, // 241 __NR_sched_setaffinity
    syscall_sched_getaffinity, // 242 __NR_sched_getaffinity
    syscall_set_thread_area, // 243 __NR_set_thread_area
    nullptr,                  // 244
    nullptr,                  // 245
    nullptr,                  // 246
    nullptr,                  // 247
    nullptr,                  // 248
    nullptr,                  // 249
    nullptr,                  // 250
    nullptr,                  // 251
    syscall_exit_group, // 252 __NR_exit_group
    nullptr,                  // 253
    syscall_epoll_create, // 254 __NR_epoll_create
    syscall_epoll_ctl,  // 255 __NR_epoll_ctl
    syscall_epoll_wait, // 256 __NR_epoll_wait
    nullptr,                  // 257
    syscall_set_tid_address, // 258 __NR_set_tid_address
    nullptr,                  // 259
    nullptr,                  // 260
    nullptr,                  // 261
    nullptr,                  // 262
    nullptr,                  // 263
    nullptr,                  // 264
    syscall_clock_gettime, // 265 __NR_clock_gettime
    syscall_clock_getres, // 266 __NR_clock_getres
    syscall_clock_nanosleep, // 267 __NR_clock_nanosleep
    syscall_statfs64,   // 268 __NR_statfs64
    syscall_fstatfs64,  // 269 __NR_fstatfs64
    syscall_tgkill,     // 270 __NR_tgkill
    syscall_utimes,     // 271 __NR_utimes
    syscall_fadvise64,  // 272 __NR_fadvise64
    nullptr,                  // 273
    nullptr,                  // 274
    nullptr,                  // 275
    nullptr,                  // 276
    nullptr,                  // 277
    nullptr,                  // 278
    nullptr,                  // 279
    nullptr,                  // 280
    nullptr,                  // 281
    nullptr,                  // 282
    nullptr,                  // 283
    nullptr,                  // 284
    nullptr,                  // 285
    nullptr,                  // 286
    nullptr,                  // 287
    nullptr,                  // 288
    nullptr,                  // 289
    nullptr,                  // 290
    syscall_inotify_init,// 291 __NR_inotify_init
    nullptr,                  // 292 __NR_inotify_add_watch
    nullptr,                  // 293 __NR_inotify_rm_watch
    nullptr,                  // 294
    syscall_openat,     // 295 __NR_openat
    syscall_mkdirat,    // 296 __NR_mkdirat
    nullptr,                  // 297
    syscall_fchownat,   // 298 __NR_fchownat
    nullptr,                  // 299
    syscall_fstatat64,  // 300 __NR_fstatat64
    syscall_unlinkat,   // 301 __NR_unlinkat
    syscall_renameat,   // 302
    nullptr,                  // 303
    syscall_symlinkat,  // 304 __NR_symlinkat
    syscall_readlinkat, // 305 __NR_readlinkat
    syscall_fchmodat,   // 306 __NR_fchmodat
    syscall_faccessat,  // 307 __NR_faccessat
    syscall_pselect6,   // 308 __NR_pselect6
    nullptr,                  // 309
    nullptr,                  // 310
    syscall_set_robust_list, // 311 __NR_set_robust_list
    nullptr,                  // 312
    nullptr,                  // 313
    syscall_sync_file_range, // 314 __NR_sync_file_range
    nullptr,                  // 315
    nullptr,                  // 316
    nullptr,                  // 317
    nullptr,                  // 318 __NR_getcpu
    nullptr,                  // 319
    syscall_utimensat,  // 320 __NR_utimensat
    nullptr,                  // 321
    syscall_timerfd_create,   // 322
    nullptr,                  // 323 
    nullptr,                  // 324 __NR_fallocate	
    syscall_timerfd_settime,  // 325
    syscall_timerfd_gettime,  // 326
    syscall_signalfd4,  // 327 __NR_signalfd4
    syscall_eventfd2,   // 328 __NR_eventfd2
    syscall_epoll_create1, // 329 __NR_epoll_create1
    nullptr,                  // 330
    syscall_pipe2,      // 331 __NR_pipe2
    nullptr,                  // 332
    nullptr,                  // 333
    nullptr,                  // 334
    nullptr,                  // 335
    nullptr,                  // 336
    nullptr,                  // 337
    nullptr,                  // 338
    nullptr,                  // 339
    syscall_prlimit64,  // 340 __NR_prlimit64
    nullptr,                  // 341 __NR_name_to_handle_at
    nullptr,                  // 342 __NR_open_by_handle_at
    nullptr,                  // 343
    nullptr,                  // 344
    syscall_sendmmsg,   // 345 __NR_sendmmsg 
    nullptr,                  // 346
    nullptr,                  // 347
    nullptr,                  // 348
    nullptr,                  // 349
    nullptr,                  // 350
    nullptr,                  // 351
    nullptr,                  // 352
    syscall_renameat,   // 353 __NR_renameat2
    nullptr,                  // 354
    syscall_getrandom,  // 355 __NR_getrandom
    syscall_memfd_create,// 356 __NR_memfd_create
    nullptr,                  // 357 __NR_bpf
    nullptr,                  // 358 __NR_execveat
    syscall_socket,     // 359 __NR_socket
    syscall_socketpair, // 360 __NR_socketpair
    syscall_bind,       // 361 __NR_bind
    syscall_connect,    // 362 __NR_connect
    syscall_listen,     // 363 __NR_listen
    syscall_accept4,    // 364 __NR_accept4
    syscall_getsockopt, // 365 __NR_getsockopt
    syscall_setsockopt, // 366 __NR_setsockopt
    syscall_getsockname,// 367 __NR_getsockname
    syscall_getpeername,// 368 __NR_getpeername
    syscall_sendto,     // 369 __NR_sendto
    syscall_sendmsg,    // 370 __NR_sendmsg
    syscall_recvfrom,   // 371 __NR_recvfrom
    syscall_recvmsg,    // 372 __NR_recvmsg
    syscall_shutdown,   // 373 __NR_shutdown
    nullptr,                  // 374
    nullptr,                  // 375
    nullptr,                  // 376
    nullptr,                  // 377
    nullptr,                  // 378
    nullptr,                  // 379
    nullptr,                  // 380
    nullptr,                  // 381
    nullptr,                  // 382
    syscall_statx,            // 383 __NR_statx
    nullptr,                  // 384
    nullptr,                  // 385
    syscall_rseq,             // 386 __NR_rseq
    nullptr,                  // 387
    nullptr,                  // 388
    nullptr,                  // 389
    nullptr,                  // 390
    nullptr,                  // 391
    nullptr,                  // 392
    nullptr,                  // 393
    nullptr,                  // 394
    syscall_shmget,           // 395
    syscall_shmctl,           // 396
    syscall_shmat,            // 397
    nullptr,                  // 398
    nullptr,                  // 399
    nullptr,                  // 400
    nullptr,                  // 401
    nullptr,                  // 402
    syscall_clock_gettime64,     // 403
    nullptr,                  // 404
    nullptr,                  // 405
    syscall_clock_getres_time64,   // 406
    syscall_clock_nanosleep_time64, // 407
    nullptr,                  // 408
    nullptr,                  // 409
    nullptr,                  // 410
    nullptr,                  // 411
    syscall_utimensat_time64, // 412
    syscall_pselect6_time64,  // 413
    nullptr,                  // 414
    nullptr,                  // 415
    nullptr,                  // 416
    nullptr,                  // 417
    nullptr,                  // 418
    nullptr,                  // 419
    nullptr,                  // 420
    syscall_sigtimedwait_time64,   // 421
    syscall_futex_time64,     // 422
    nullptr,                  // 423
    nullptr,                  // 424
    nullptr,                  // 425
    nullptr,                  // 426
    nullptr,                  // 427
    nullptr,                  // 428
    nullptr,                  // 429
    nullptr,                  // 430
    nullptr,                  // 431
    nullptr,                  // 432
    nullptr,                  // 433
    nullptr,                  // 434
    syscall_clone3,           // 435 __NR_clone3
    nullptr,                  // 436
    nullptr,                  // 437
    nullptr,                  // 438
    syscall_faccessat2        // 439 __NR_faccessat2
};

#ifndef BOXEDWINE_MULTI_THREADED
extern S32 contextTime; // about the # instruction per 10 ms
#endif
void ksyscall(CPU* cpu, U32 eipCount) {
    U32 result = -K_ENOSYS;
    U64 startTime = KSystem::getMicroCounter();
#if defined(BOXEDWINE_MULTI_THREADED) || defined(BOXEDWINE_AZROK_WAIT_FRAME_PROFILE)
    U32 syscallNo = EAX;
#endif
    if (cpu->thread->terminating) {
        terminateCurrentThread(cpu->thread); // there is a race condition, just signal it again
		return;
    }
    if (cpu->thread->pendingSignals) {
        // I know this is a nested if statement, but it makes setting a break point easier
        if (cpu->thread->runSignals()) {
            cpu->nextOp = cpu->getNextOp();
            return;
        }
    }
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
    const KWaitFrameProfilePending waitProfilePending = prepareAzrokWaitFrameProfile(cpu, syscallNo);
#endif
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
    if (syscallNo == 0x7f00u) {
        result = syscall_azrok_wait_marker(cpu, eipCount);
    } else
#endif
    if (EAX>439) {
        result = -K_ENOSYS;
        kdebug("no syscall for %d", EAX);
    } else if (!syscallFunc[EAX]) {
        result = -K_ENOSYS;
        kdebug("no syscall for %d", EAX);
    } else {
#ifndef BOXEDWINE_MULTI_THREADED
        U64 startTime = KSystem::getMicroCounter();
#endif
        result = syscallFunc[EAX](cpu, eipCount);
#ifndef BOXEDWINE_MULTI_THREADED
        U64 diff = KSystem::getMicroCounter()-startTime;
        sysCallTime+=diff;  
        cpu->blockInstructionCount+=(U32)(contextTime*diff/10000);
#endif
    }    
#ifdef BOXEDWINE_MULTI_THREADED
    if (!cpu->thread->terminating && cpu->thread->startSignal) {
        cpu->thread->startSignal = false;
        result = -K_CONTINUE;
        // I saw this once with multi-thread normal cpu core and firefight, it was from syscall 4 (write)
        klog_fmt("syscall %d was not interrupted correctly by signal", syscallNo);
    }
#endif
    if (result==(U32)(-K_CONTINUE)) {

    } else if (result==(U32)(-K_WAIT)) {
        		
    } else {
        EAX = result;
        cpu->eip.u32+=eipCount;
    }
    const U64 endTime = KSystem::getMicroCounter();
    cpu->thread->kernelTime += endTime - startTime;
#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
    recordAzrokWaitFrameProfile(waitProfilePending, result, startTime, endTime);
#endif
}

