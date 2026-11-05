/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#if defined(__TEST) && defined(BOXEDWINE_MULTI_THREADED) && defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)

#include "cpu/testCPU.h"
#include "../emulation/cpu/wasm/wasmPgoQuiesce.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

static bool wasmPgoQuiesceLifecycleWaitFor(U32 parked, U32 arrived, U32 timeoutMs = 1000) {
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

static bool wasmPgoQuiesceLifecycleCapture(void* opaque) {
    ((std::atomic<U32>*)opaque)->fetch_add(1, std::memory_order_relaxed);
    return true;
}

static U32 wasmPgoQuiesceLifecycleEmitProgram() {
    U32 first = testContext().codeIp;
    testPushCode8(0x40); // inc eax
    testPushCode8(0xe9); // jmp rel32 0: return at the nonterminal second block
    testPushCode32(0);
    testPushCode8(0x42); // inc edx after the released Poll
    testPushCode8(0xcd);
    testPushCode8(0x97); // TestEnd
    return first;
}

static KThread* wasmPgoQuiesceLifecycleStart(U32 address) {
    KThread* child = testContext().process->createThread();
    child->cpu->clone(testContext().cpu);
    child->cpu->reg[0].u32 = 0; // EAX
    child->cpu->reg[2].u32 = 0; // EDX
    child->cpu->eip.u32 = address - child->cpu->seg[CS].address;
    child->cpu->nextOp = nullptr;
    scheduleThread(child);
    return child;
}

} // namespace

void testWasmPgoQuiesceCpuLifecycle() {
    wasmPgoQuiesceResetForTest();
    testNewInstruction(0);
    U32 first = wasmPgoQuiesceLifecycleEmitProgram();
    U32 second = wasmPgoQuiesceLifecycleEmitProgram();
    KThread* children[2] = {nullptr, nullptr};
    bool joined[2] = {false, false};
    U64 epoch = 0;
    bool released = false;
    bool ok = wasmPgoQuiesceTestArmCpuPostRunProbe(2);
    if (!ok) {
        testFail("PGO CPU lifecycle fixture cannot arm its post-run probe");
    }
    if (ok) {
        children[0] = wasmPgoQuiesceLifecycleStart(first);
        children[1] = wasmPgoQuiesceLifecycleStart(second);
        ok = wasmPgoQuiesceTestWaitCpuPostRunProbe(2, 1000);
        WasmPgoQuiesceSnapshot before = wasmPgoQuiesceSnapshot();
        ok = ok && before.registered == 2 && before.active == 0 &&
            before.arrived == 0 && before.parked == 0;
        if (!ok) {
            testFail("PGO CPU lifecycle fixture did not reach two post-run safe points");
        }
    }
    if (ok) {
        epoch = wasmPgoQuiesceBegin();
        ok = epoch && wasmPgoQuiesceTestReleaseCpuPostRunProbe(1) &&
            wasmPgoQuiesceLifecycleWaitFor(1, 1) && !wasmPgoQuiesceReady(epoch);
        std::atomic<U32> captures = 0;
        ok = ok && !wasmPgoQuiesceCapture(epoch, wasmPgoQuiesceLifecycleCapture, &captures) &&
            captures.load(std::memory_order_relaxed) == 0;
        ok = ok && wasmPgoQuiesceTestReleaseCpuPostRunProbe(1) &&
            wasmPgoQuiesceWaitReady(epoch, 1000) &&
            wasmPgoQuiesceCapture(epoch, wasmPgoQuiesceLifecycleCapture, &captures) &&
            captures.load(std::memory_order_relaxed) == 1 && wasmPgoQuiesceRelease(epoch);
        released = ok;
        if (!ok) {
            testFail("PGO CPU lifecycle fixture violated 1/2 or 2/2 quorum capture");
        }
    }

    if (epoch && !released) {
        wasmPgoQuiesceCancel(epoch);
    }
    wasmPgoQuiesceTestReleaseAllCpuPostRunProbes();
    for (U32 i = 0; i < 2; ++i) {
        if (children[i]) {
            joinThread(children[i]);
            joined[i] = true;
        }
    }
    KThread::setCurrentThread(testContext().thread);

    WasmPgoQuiesceSnapshot after = wasmPgoQuiesceSnapshot();
    if (!released || !joined[0] || !joined[1] || after.registered || after.parked ||
            after.releasePending || children[0]->cpu->reg[0].u32 != 1 ||
            children[1]->cpu->reg[0].u32 != 1 || children[0]->cpu->reg[2].u32 != 1 ||
            children[1]->cpu->reg[2].u32 != 1 || !children[0]->cpu->nextOp ||
            !children[1]->cpu->nextOp || children[0]->cpu->nextOp->inst != TestEnd ||
            children[1]->cpu->nextOp->inst != TestEnd) {
        testFail("PGO CPU lifecycle fixture did not release, unregister, and finish both guest CPUs");
    }
    for (KThread* child : children) {
        if (child) {
            testContext().process->deleteThread(child);
        }
    }
    wasmPgoQuiesceResetForTest();
}

#endif
