/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#if defined(__TEST) && defined(BOXEDWINE_MULTI_THREADED)

#include "cpu/testCPU.h"
#include "../emulation/cpu/wasm/wasmPgoQuiesce.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

struct WasmPgoQuiescePollArgs {
    U64 token;
    std::atomic<bool> entered = false;
    std::atomic<bool> returned = false;
    bool result = false;
};

static void wasmPgoQuiescePollWorker(WasmPgoQuiescePollArgs* args) {
    args->entered.store(true, std::memory_order_release);
    args->result = wasmPgoQuiescePoll(args->token);
    args->returned.store(true, std::memory_order_release);
}

static bool wasmPgoQuiesceWaitFor(U32 parked, U32 arrived, U32 timeoutMs = 1000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        WasmPgoQuiesceSnapshot snapshot = wasmPgoQuiesceSnapshot();
        if (snapshot.parked == parked && snapshot.arrived == arrived) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

static bool wasmPgoQuiesceWaitForIdle(U32 timeoutMs = 1000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        WasmPgoQuiesceSnapshot snapshot = wasmPgoQuiesceSnapshot();
        if (!snapshot.freezeActive && !snapshot.releasePending && !snapshot.parked) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

static bool wasmPgoQuiesceCountCapture(void* opaque) {
    std::atomic<U32>* count = (std::atomic<U32>*)opaque;
    count->fetch_add(1, std::memory_order_relaxed);
    return true;
}

static bool wasmPgoQuiesceFailCapture(void*) {
    return false;
}

} // namespace

void testWasmPgoQuiesceCoordinator() {
    wasmPgoQuiesceResetForTest();
    const U64 cpu = wasmPgoQuiesceRegister(WasmPgoQuiesceDomain::GuestCpu);
    const U64 main = wasmPgoQuiesceRegister(WasmPgoQuiesceDomain::MainLoop);
    if (!cpu || !main) {
        testFail("PGO quiesce coordinator cannot register two members");
        wasmPgoQuiesceResetForTest();
        return;
    }
    const U64 epoch = wasmPgoQuiesceBegin();
    if (!epoch) {
        testFail("PGO quiesce coordinator cannot register and freeze a two-member epoch");
        wasmPgoQuiesceResetForTest();
        return;
    }
    std::atomic<U32> captures = 0;
    if (wasmPgoQuiesceCapture(epoch, wasmPgoQuiesceCountCapture, &captures) ||
            captures.load(std::memory_order_relaxed) != 0) {
        testFail("PGO quiesce coordinator allowed capture before any participant parked");
        wasmPgoQuiesceCancel(epoch);
        wasmPgoQuiesceResetForTest();
        return;
    }
    WasmPgoQuiescePollArgs cpuArgs{cpu};
    WasmPgoQuiescePollArgs mainArgs{main};
    std::thread cpuThread(wasmPgoQuiescePollWorker, &cpuArgs);
    bool partialRejected = wasmPgoQuiesceWaitFor(1, 1) && !wasmPgoQuiesceReady(epoch);
    bool duplicateRejected = !wasmPgoQuiescePoll(cpu) && !wasmPgoQuiesceReady(epoch - 1);
    std::thread mainThread(wasmPgoQuiescePollWorker, &mainArgs);
    bool completeReady = wasmPgoQuiesceWaitReady(epoch, 1000) &&
        wasmPgoQuiesceSnapshot().parked == 2;
    bool uncapturedReleaseRejected = !wasmPgoQuiesceRelease(epoch);
    bool captured = wasmPgoQuiesceCapture(epoch, wasmPgoQuiesceCountCapture, &captures);
    bool exactRelease = completeReady && !wasmPgoQuiesceRelease(epoch - 1) &&
        wasmPgoQuiesceRelease(epoch);
    if (!exactRelease) {
        wasmPgoQuiesceCancel(epoch);
    }
    cpuThread.join();
    mainThread.join();
    if (!partialRejected || !duplicateRejected || !completeReady || !uncapturedReleaseRejected ||
            !captured || captures.load() != 1 || !exactRelease || !cpuArgs.result ||
            !mainArgs.result || !wasmPgoQuiesceWaitForIdle() ||
            wasmPgoQuiesceRelease(epoch)) {
        testFail("PGO quiesce coordinator quorum/release invariant failed");
        wasmPgoQuiesceResetForTest();
        return;
    }

    const U64 timeoutEpoch = wasmPgoQuiesceBegin();
    WasmPgoQuiescePollArgs timeoutArgs{cpu};
    std::thread timeoutThread(wasmPgoQuiescePollWorker, &timeoutArgs);
    bool timeoutRejected = timeoutEpoch && wasmPgoQuiesceWaitFor(1, 1) &&
        !wasmPgoQuiesceWaitReady(timeoutEpoch, 200);
    timeoutThread.join();
    if (!timeoutRejected || timeoutArgs.result || !wasmPgoQuiesceWaitForIdle()) {
        testFail("PGO quiesce coordinator exposed success after cancelled quorum");
        wasmPgoQuiesceResetForTest();
        return;
    }

    const U64 failedCaptureEpoch = wasmPgoQuiesceBegin();
    WasmPgoQuiescePollArgs failedCpuArgs{cpu};
    WasmPgoQuiescePollArgs failedMainArgs{main};
    std::thread failedCpuThread(wasmPgoQuiescePollWorker, &failedCpuArgs);
    std::thread failedMainThread(wasmPgoQuiescePollWorker, &failedMainArgs);
    bool failedCaptureRejected = failedCaptureEpoch &&
        wasmPgoQuiesceWaitReady(failedCaptureEpoch, 1000) &&
        !wasmPgoQuiesceCapture(failedCaptureEpoch, wasmPgoQuiesceFailCapture, nullptr);
    if (!failedCaptureRejected) {
        wasmPgoQuiesceCancel(failedCaptureEpoch);
    }
    failedCpuThread.join();
    failedMainThread.join();
    if (!failedCaptureRejected || failedCpuArgs.result || failedMainArgs.result ||
            !wasmPgoQuiesceWaitForIdle()) {
        testFail("PGO quiesce coordinator did not fail-close a failed raw copy");
        wasmPgoQuiesceResetForTest();
        return;
    }

    const U64 membershipEpoch = wasmPgoQuiesceBegin();
    WasmPgoQuiescePollArgs membershipArgs{cpu};
    std::thread membershipThread(wasmPgoQuiescePollWorker, &membershipArgs);
    bool membershipSetup = membershipEpoch && wasmPgoQuiesceWaitFor(1, 1);
    const U64 socket = membershipSetup ?
        wasmPgoQuiesceRegister(WasmPgoQuiesceDomain::NativeSocket) : 0;
    if (!membershipSetup) {
        wasmPgoQuiesceCancel(membershipEpoch);
    }
    membershipThread.join();
    if (!membershipSetup || !socket || membershipArgs.result || !wasmPgoQuiesceWaitForIdle()) {
        testFail("PGO quiesce coordinator did not reject registration during freeze");
        wasmPgoQuiesceResetForTest();
        return;
    }

    const U64 unregisterEpoch = wasmPgoQuiesceBegin();
    WasmPgoQuiescePollArgs unregisterArgs{cpu};
    std::thread unregisterThread(wasmPgoQuiescePollWorker, &unregisterArgs);
    bool unregisterRejected = unregisterEpoch && wasmPgoQuiesceWaitFor(1, 1) &&
        wasmPgoQuiesceUnregister(main);
    if (!unregisterRejected) {
        wasmPgoQuiesceCancel(unregisterEpoch);
    }
    unregisterThread.join();
    if (!unregisterRejected || unregisterArgs.result || !wasmPgoQuiesceWaitForIdle()) {
        testFail("PGO quiesce coordinator did not reject unregister during freeze");
        wasmPgoQuiesceResetForTest();
        return;
    }

    if (!wasmPgoQuiesceSetActive(cpu, true)) {
        testFail("PGO quiesce coordinator cannot mark an active participant");
    }
    const U64 activeEpoch = wasmPgoQuiesceBegin();
    if (!activeEpoch || wasmPgoQuiesceReady(activeEpoch)) {
        testFail("PGO quiesce coordinator accepted an active guest block");
    }
    if (!wasmPgoQuiesceCancel(activeEpoch) || !wasmPgoQuiesceSetActive(cpu, false)) {
        testFail("PGO quiesce coordinator could not cancel active-block epoch");
    }
    if (!wasmPgoQuiesceUnregister(cpu) || wasmPgoQuiesceUnregister(main) ||
            !wasmPgoQuiesceUnregister(socket) || wasmPgoQuiesceBegin() != 0) {
        testFail("PGO quiesce coordinator unregister/empty-set gate failed");
    }
    wasmPgoQuiesceResetForTest();
}

#endif
