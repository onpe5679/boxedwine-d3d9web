/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "wasmPgoQuiesce.h"

#if defined(__TEST) || defined(BOXEDWINE_WASM_PGO_QUIESCE) || defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)

#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <vector>

namespace {

struct WasmPgoQuiesceMember {
    U64 token;
    WasmPgoQuiesceDomain domain;
    bool registered;
    bool active;
    U64 arrivedEpoch;
};

std::mutex g_wasmPgoQuiesceMutex;
std::condition_variable g_wasmPgoQuiesceCondition;
std::vector<WasmPgoQuiesceMember> g_wasmPgoQuiesceMembers;
U64 g_wasmPgoQuiesceNextToken = 1;
U64 g_wasmPgoQuiesceLastEpoch = 0;
U64 g_wasmPgoQuiesceCurrentEpoch = 0;
U64 g_wasmPgoQuiesceCapturedEpoch = 0;
U32 g_wasmPgoQuiesceExpected = 0;
U32 g_wasmPgoQuiesceArrived = 0;
U32 g_wasmPgoQuiesceParked = 0;
bool g_wasmPgoQuiesceFreezeActive = false;
bool g_wasmPgoQuiesceReleasePending = false;
bool g_wasmPgoQuiesceFailed = false;
U64 g_wasmPgoQuiesceFailedEpoch = 0;

#if defined(__TEST) && defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
bool g_wasmPgoQuiesceTestProbeArmed = false;
bool g_wasmPgoQuiesceTestProbeFailed = false;
U32 g_wasmPgoQuiesceTestProbeExpected = 0;
U32 g_wasmPgoQuiesceTestProbePermits = 0;
U32 g_wasmPgoQuiesceTestProbeReleased = 0;
std::vector<U64> g_wasmPgoQuiesceTestProbeTokens;
#endif

static WasmPgoQuiesceMember* wasmPgoQuiesceFindMember(U64 token) {
    for (WasmPgoQuiesceMember& member : g_wasmPgoQuiesceMembers) {
        if (member.token == token) {
            return &member;
        }
    }
    return nullptr;
}

static U32 wasmPgoQuiesceRegisteredCount() {
    U32 count = 0;
    for (const WasmPgoQuiesceMember& member : g_wasmPgoQuiesceMembers) {
        count += member.registered ? 1 : 0;
    }
    return count;
}

static U32 wasmPgoQuiesceActiveCount() {
    U32 count = 0;
    for (const WasmPgoQuiesceMember& member : g_wasmPgoQuiesceMembers) {
        count += member.registered && member.active ? 1 : 0;
    }
    return count;
}

static void wasmPgoQuiesceFinishReleaseIfIdle() {
    if (g_wasmPgoQuiesceReleasePending && !g_wasmPgoQuiesceParked) {
        g_wasmPgoQuiesceReleasePending = false;
        g_wasmPgoQuiesceExpected = 0;
        g_wasmPgoQuiesceArrived = 0;
    }
}

static void wasmPgoQuiesceFailLocked() {
    // A successful release has already completed the capture under this lock.
    // Subsequent lifecycle changes may delay the next epoch, but must never
    // retroactively turn the released participants into a failed capture.
    if (!g_wasmPgoQuiesceFreezeActive) {
        return;
    }
    g_wasmPgoQuiesceFailed = true;
    g_wasmPgoQuiesceFailedEpoch = g_wasmPgoQuiesceCurrentEpoch;
    g_wasmPgoQuiesceFreezeActive = false;
    g_wasmPgoQuiesceReleasePending = true;
    wasmPgoQuiesceFinishReleaseIfIdle();
    g_wasmPgoQuiesceCondition.notify_all();
}

static bool wasmPgoQuiesceReadyLocked(U64 epoch) {
    return epoch && g_wasmPgoQuiesceFreezeActive &&
        epoch == g_wasmPgoQuiesceCurrentEpoch && !g_wasmPgoQuiesceFailed &&
        g_wasmPgoQuiesceExpected == g_wasmPgoQuiesceArrived &&
        g_wasmPgoQuiesceExpected == g_wasmPgoQuiesceParked &&
        !wasmPgoQuiesceActiveCount();
}

} // namespace

U64 wasmPgoQuiesceRegister(WasmPgoQuiesceDomain domain) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (g_wasmPgoQuiesceFreezeActive) {
        // Registration is allowed to complete so the caller has a lifetime
        // token, but it always destroys the in-flight capture epoch.
        wasmPgoQuiesceFailLocked();
    }
    U64 token = g_wasmPgoQuiesceNextToken++;
    if (!token) {
        token = g_wasmPgoQuiesceNextToken++;
    }
    g_wasmPgoQuiesceMembers.push_back({token, domain, true, false, 0});
    return token;
}

bool wasmPgoQuiesceUnregister(U64 token) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    WasmPgoQuiesceMember* member = wasmPgoQuiesceFindMember(token);
    if (!member || !member->registered) {
        return false;
    }
    member->registered = false;
    member->active = false;
    if (g_wasmPgoQuiesceFreezeActive) {
        wasmPgoQuiesceFailLocked();
    }
    return true;
}

bool wasmPgoQuiesceSetActive(U64 token, bool active) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    WasmPgoQuiesceMember* member = wasmPgoQuiesceFindMember(token);
    if (!member || !member->registered) {
        return false;
    }
    member->active = active;
    g_wasmPgoQuiesceCondition.notify_all();
    return true;
}

U64 wasmPgoQuiesceBegin() {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (g_wasmPgoQuiesceFreezeActive || g_wasmPgoQuiesceReleasePending) {
        return 0;
    }
    U32 expected = wasmPgoQuiesceRegisteredCount();
    if (!expected) {
        return 0;
    }
    ++g_wasmPgoQuiesceLastEpoch;
    if (!g_wasmPgoQuiesceLastEpoch) {
        ++g_wasmPgoQuiesceLastEpoch;
    }
    g_wasmPgoQuiesceCurrentEpoch = g_wasmPgoQuiesceLastEpoch;
    g_wasmPgoQuiesceCapturedEpoch = 0;
    g_wasmPgoQuiesceExpected = expected;
    g_wasmPgoQuiesceArrived = 0;
    g_wasmPgoQuiesceParked = 0;
    g_wasmPgoQuiesceFreezeActive = true;
    g_wasmPgoQuiesceReleasePending = false;
    g_wasmPgoQuiesceFailed = false;
    for (WasmPgoQuiesceMember& member : g_wasmPgoQuiesceMembers) {
        member.arrivedEpoch = 0;
    }
    return g_wasmPgoQuiesceCurrentEpoch;
}

bool wasmPgoQuiesceReady(U64 epoch) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    return wasmPgoQuiesceReadyLocked(epoch);
}

bool wasmPgoQuiesceWaitReady(U64 epoch, U32 timeoutMs) {
    std::unique_lock<std::mutex> lock(g_wasmPgoQuiesceMutex);
    const bool ready = g_wasmPgoQuiesceCondition.wait_for(lock,
        std::chrono::milliseconds(timeoutMs), [epoch] {
            return wasmPgoQuiesceReadyLocked(epoch) ||
                !g_wasmPgoQuiesceFreezeActive ||
                g_wasmPgoQuiesceCurrentEpoch != epoch;
        }) && wasmPgoQuiesceReadyLocked(epoch);
    if (!ready && g_wasmPgoQuiesceFreezeActive &&
            g_wasmPgoQuiesceCurrentEpoch == epoch) {
        wasmPgoQuiesceFailLocked();
    }
    return ready;
}

bool wasmPgoQuiesceCapture(U64 epoch, WasmPgoQuiesceCaptureFn capture, void* opaque) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    // The caller supplies the private LLVM buffer-copy operation. Keeping it
    // under this coordinator lock prevents a release, membership mutation, or
    // second capture from racing profile bytes between Ready and the copy.
    if (!capture || !wasmPgoQuiesceReadyLocked(epoch) ||
            g_wasmPgoQuiesceCapturedEpoch == epoch) {
        return false;
    }
    try {
        if (capture(opaque)) {
            g_wasmPgoQuiesceCapturedEpoch = epoch;
            return true;
        }
    } catch (...) {
        wasmPgoQuiesceFailLocked();
        throw;
    }
    // A failed copy is indistinguishable from an unsafe raw capture. Do not
    // leave a successful-looking epoch with its writers parked for a caller
    // that forgot to issue a separate cancel.
    wasmPgoQuiesceFailLocked();
    return false;
}

bool wasmPgoQuiesceRelease(U64 epoch) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!wasmPgoQuiesceReadyLocked(epoch) ||
            g_wasmPgoQuiesceCapturedEpoch != epoch) {
        return false;
    }
    g_wasmPgoQuiesceFreezeActive = false;
    g_wasmPgoQuiesceReleasePending = true;
    wasmPgoQuiesceFinishReleaseIfIdle();
    g_wasmPgoQuiesceCondition.notify_all();
    return true;
}

bool wasmPgoQuiesceCancel(U64 epoch) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!epoch || epoch != g_wasmPgoQuiesceCurrentEpoch ||
            !g_wasmPgoQuiesceFreezeActive) {
        return false;
    }
    wasmPgoQuiesceFailLocked();
    return true;
}

bool wasmPgoQuiescePoll(U64 token) {
    std::unique_lock<std::mutex> lock(g_wasmPgoQuiesceMutex);
    WasmPgoQuiesceMember* member = wasmPgoQuiesceFindMember(token);
    if (!member || !member->registered || !g_wasmPgoQuiesceFreezeActive ||
            member->active || member->arrivedEpoch == g_wasmPgoQuiesceCurrentEpoch) {
        return false;
    }
    const U64 epoch = g_wasmPgoQuiesceCurrentEpoch;
    member->arrivedEpoch = epoch;
    ++g_wasmPgoQuiesceArrived;
    ++g_wasmPgoQuiesceParked;
    g_wasmPgoQuiesceCondition.notify_all();
    g_wasmPgoQuiesceCondition.wait(lock, [epoch] {
        return !g_wasmPgoQuiesceFreezeActive ||
            g_wasmPgoQuiesceCurrentEpoch != epoch;
    });
    const bool success = g_wasmPgoQuiesceFailedEpoch != epoch;
    --g_wasmPgoQuiesceParked;
    wasmPgoQuiesceFinishReleaseIfIdle();
    g_wasmPgoQuiesceCondition.notify_all();
    return success;
}

WasmPgoQuiesceSnapshot wasmPgoQuiesceSnapshot() {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    return {g_wasmPgoQuiesceLastEpoch, wasmPgoQuiesceRegisteredCount(),
        g_wasmPgoQuiesceExpected, g_wasmPgoQuiesceArrived,
        g_wasmPgoQuiesceParked, wasmPgoQuiesceActiveCount(),
        g_wasmPgoQuiesceFreezeActive, g_wasmPgoQuiesceReleasePending,
        g_wasmPgoQuiesceFailed};
}

#ifdef __TEST
void wasmPgoQuiesceResetForTest() {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (g_wasmPgoQuiesceFreezeActive || g_wasmPgoQuiesceReleasePending ||
            g_wasmPgoQuiesceParked) {
        kpanic("wasmPgoQuiesceResetForTest with a live epoch");
    }
    g_wasmPgoQuiesceMembers.clear();
    g_wasmPgoQuiesceNextToken = 1;
    g_wasmPgoQuiesceLastEpoch = 0;
    g_wasmPgoQuiesceCurrentEpoch = 0;
    g_wasmPgoQuiesceCapturedEpoch = 0;
    g_wasmPgoQuiesceExpected = 0;
    g_wasmPgoQuiesceArrived = 0;
    g_wasmPgoQuiesceParked = 0;
    g_wasmPgoQuiesceFailed = false;
    g_wasmPgoQuiesceFailedEpoch = 0;
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
    g_wasmPgoQuiesceTestProbeArmed = false;
    g_wasmPgoQuiesceTestProbeFailed = false;
    g_wasmPgoQuiesceTestProbeExpected = 0;
    g_wasmPgoQuiesceTestProbePermits = 0;
    g_wasmPgoQuiesceTestProbeReleased = 0;
    g_wasmPgoQuiesceTestProbeTokens.clear();
#endif
}

#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
bool wasmPgoQuiesceTestArmCpuPostRunProbe(U32 expected) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!expected || g_wasmPgoQuiesceTestProbeArmed) {
        return false;
    }
    g_wasmPgoQuiesceTestProbeArmed = true;
    g_wasmPgoQuiesceTestProbeFailed = false;
    g_wasmPgoQuiesceTestProbeExpected = expected;
    g_wasmPgoQuiesceTestProbePermits = 0;
    g_wasmPgoQuiesceTestProbeReleased = 0;
    g_wasmPgoQuiesceTestProbeTokens.clear();
    return true;
}

bool wasmPgoQuiesceTestWaitCpuPostRunProbe(U32 expected, U32 timeoutMs) {
    std::unique_lock<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!g_wasmPgoQuiesceTestProbeArmed || expected != g_wasmPgoQuiesceTestProbeExpected) {
        return false;
    }
    return g_wasmPgoQuiesceCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs), [expected] {
        return g_wasmPgoQuiesceTestProbeFailed ||
            g_wasmPgoQuiesceTestProbeTokens.size() >= expected;
    }) && !g_wasmPgoQuiesceTestProbeFailed &&
        g_wasmPgoQuiesceTestProbeTokens.size() == expected;
}

bool wasmPgoQuiesceTestReleaseCpuPostRunProbe(U32 count) {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!g_wasmPgoQuiesceTestProbeArmed || g_wasmPgoQuiesceTestProbeFailed || !count ||
            g_wasmPgoQuiesceTestProbeReleased + g_wasmPgoQuiesceTestProbePermits + count >
                g_wasmPgoQuiesceTestProbeTokens.size()) {
        return false;
    }
    g_wasmPgoQuiesceTestProbePermits += count;
    g_wasmPgoQuiesceCondition.notify_all();
    return true;
}

void wasmPgoQuiesceTestReleaseAllCpuPostRunProbes() {
    std::lock_guard<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!g_wasmPgoQuiesceTestProbeArmed) {
        return;
    }
    g_wasmPgoQuiesceTestProbeFailed = true;
    g_wasmPgoQuiesceCondition.notify_all();
}

bool wasmPgoQuiesceTestCpuPostRunProbe(U64 token) {
    std::unique_lock<std::mutex> lock(g_wasmPgoQuiesceMutex);
    if (!g_wasmPgoQuiesceTestProbeArmed) {
        return true;
    }
    if (!token || g_wasmPgoQuiesceTestProbeTokens.size() >= g_wasmPgoQuiesceTestProbeExpected ||
            std::find(g_wasmPgoQuiesceTestProbeTokens.begin(),
                g_wasmPgoQuiesceTestProbeTokens.end(), token) !=
                g_wasmPgoQuiesceTestProbeTokens.end()) {
        g_wasmPgoQuiesceTestProbeFailed = true;
        g_wasmPgoQuiesceCondition.notify_all();
        return false;
    }
    g_wasmPgoQuiesceTestProbeTokens.push_back(token);
    g_wasmPgoQuiesceCondition.notify_all();
    g_wasmPgoQuiesceCondition.wait(lock, [] {
        return g_wasmPgoQuiesceTestProbeFailed || g_wasmPgoQuiesceTestProbePermits;
    });
    if (g_wasmPgoQuiesceTestProbeFailed) {
        return false;
    }
    --g_wasmPgoQuiesceTestProbePermits;
    ++g_wasmPgoQuiesceTestProbeReleased;
    return true;
}
#endif
#endif

#endif
