/*
 *  Copyright (C) 2012-2026 The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "testCPU.h"
#include "testWasmJitMultiDirectLoops.h"

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT) && \
        defined(BOXEDWINE_MULTI_THREADED) && defined(__EMSCRIPTEN__) && \
        defined(BOXEDWINE_WASM_JIT_MULTI_DIRECT_LOOPS) && \
        defined(BOXEDWINE_WASM_JIT_MULTI_DIRECT_LOOPS_FIXTURE)

#include "../../emulation/cpu/normal/normalCPU.h"
#include "../../emulation/cpu/wasm/jitWasmCodeGen.h"
#include "ksignal.h"

namespace {

void runToTestEnd(CPU* cpu, U32 maxSteps, const char* name) {
    try {
        for (U32 step = 0;
                step < maxSteps &&
                (!cpu->nextOp || cpu->nextOp->inst != TestEnd);
                ++step) {
            cpu->run();
        }
    } catch (...) {
        testFail("%s unexpected exception", name);
    }
    if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
        testFail("%s did not reach TestEnd", name);
    }
}

DecodedOp* requireCompiledRoot(const char* name) {
    TestContext& context = testContext();
    DecodedOp* root = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
    if (!root || root->pfn != context.cpu->thread->process->startJITOp ||
            !root->pfnJitCode || !(root->flags & OP_FLAG_JIT) ||
            root->blockStart != root) {
        testFail("%s compiled root", name);
        return nullptr;
    }
    return root;
}

void runDisjointSharedBudgetFixture() {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    wasmJitMultiDirectLoopFixtureReset();
    testNewInstruction(0);

    testPushCode8(0xb9); testPushCode32(40); // mov ecx,40
    testPushCode8(0x40);                     // loop1: inc eax
    testPushCode8(0x49);                     // dec ecx
    testPushCode8(0x75); testPushCode8(0xfc); // jnz loop1
    testPushCode8(0xb9); testPushCode32(40); // mov ecx,40
    testPushCode8(0x42);                     // loop2: inc edx
    testPushCode8(0x49);                     // dec ecx
    testPushCode8(0x75); testPushCode8(0xfc); // jnz loop2
    testPushCode8(0x0f); testPushCode8(0x94); testPushCode8(0xc3); // setz bl
    testRunCPU();
    DecodedOp* root = requireCompiledRoot("multi-loop disjoint");
    if (!root) return;

    cpu->reg[0].u32 = 0;
    cpu->reg[1].u32 = 0;
    cpu->reg[2].u32 = 0;
    cpu->reg[3].u32 = 0;
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->wasmJitBailout = 0;
    wasmJitMultiDirectLoopFixtureResetRuntime();
    runToTestEnd(cpu, 8, "multi-loop disjoint generated");

    WasmJitMultiDirectLoopFixtureStats stats =
        wasmJitMultiDirectLoopFixtureGet();
    if (cpu->reg[0].u32 != 40 || cpu->reg[1].u32 != 0 ||
            cpu->reg[2].u32 != 40 || (cpu->reg[3].u32 & 0xff) != 1 ||
            stats.discoveredEdges < 2 || stats.admittedEdges < 2 ||
            stats.openedHeaders < 2 || stats.localBackedges < 63 ||
            stats.fallbackBackedges == 0 || cpu->wasmJitBailout != 0) {
        testFail("multi-loop disjoint/shared-budget state eax=%u ecx=%u edx=%u ebx=%u discovered=%u admitted=%u opened=%u local=%u fallback=%u bailout=%u",
            cpu->reg[0].u32, cpu->reg[1].u32, cpu->reg[2].u32,
            cpu->reg[3].u32 & 0xff, stats.discoveredEdges,
            stats.admittedEdges, stats.openedHeaders,
            stats.localBackedges, stats.fallbackBackedges,
            cpu->wasmJitBailout);
    }
}

void runNestedFixture() {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    wasmJitMultiDirectLoopFixtureReset();
    testNewInstruction(0);

    testPushCode8(0xb9); testPushCode32(3);  // mov ecx,3
    testPushCode8(0xba); testPushCode32(4);  // outer: mov edx,4
    testPushCode8(0x40);                     // inner: inc eax
    testPushCode8(0x4a);                     // dec edx
    testPushCode8(0x75); testPushCode8(0xfc); // jnz inner
    testPushCode8(0x49);                     // dec ecx
    testPushCode8(0x75); testPushCode8(0xf4); // jnz outer
    testPushCode8(0x0f); testPushCode8(0x94); testPushCode8(0xc3); // setz bl
    testRunCPU();
    DecodedOp* root = requireCompiledRoot("multi-loop nested");
    if (!root) return;

    cpu->reg[0].u32 = 0;
    cpu->reg[1].u32 = 0;
    cpu->reg[2].u32 = 0;
    cpu->reg[3].u32 = 0;
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->wasmJitBailout = 0;
    wasmJitMultiDirectLoopFixtureResetRuntime();
    runToTestEnd(cpu, 4, "multi-loop nested generated");

    WasmJitMultiDirectLoopFixtureStats stats =
        wasmJitMultiDirectLoopFixtureGet();
    if (cpu->reg[0].u32 != 12 || cpu->reg[1].u32 != 0 ||
            cpu->reg[2].u32 != 0 || (cpu->reg[3].u32 & 0xff) != 1 ||
            stats.admittedEdges < 2 || stats.localBackedges != 11 ||
            stats.fallbackBackedges != 0 || cpu->wasmJitBailout != 0) {
        testFail("multi-loop nested/flags state eax=%u ecx=%u edx=%u ebx=%u admitted=%u local=%u fallback=%u bailout=%u",
            cpu->reg[0].u32, cpu->reg[1].u32, cpu->reg[2].u32,
            cpu->reg[3].u32 & 0xff, stats.admittedEdges,
            stats.localBackedges, stats.fallbackBackedges,
            cpu->wasmJitBailout);
    }
}

void runSmcFixture() {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    constexpr U32 armOffset = 0x100;
    constexpr U32 loopStart = 5;
    constexpr U32 branchDisplacementOffset = 25;
    wasmJitMultiDirectLoopFixtureReset();
    testNewInstruction(0);
    context.memory->writed(TEST_HEAP_ADDRESS + armOffset, 0);

    testPushCode8(0xb9); testPushCode32(5); // mov ecx,5
    testPushCode8(0x83); testPushCode8(0x3d); testPushCode32(armOffset);
    testPushCode8(0x00);                    // cmp dword [arm],0
    testPushCode8(0x74); testPushCode8(0x08); // je no-write
    testPushCode8(0x2e); testPushCode8(0xc6); testPushCode8(0x05);
    testPushCode32(branchDisplacementOffset); testPushCode8(0x00);
                                               // mov byte cs:[jnz+1],0
    testPushCode8(0x40);                     // no-write: inc eax
    testPushCode8(0x49);                     // dec ecx
    testPushCode8(0x75);
    testPushCode8((U8)(loopStart - (branchDisplacementOffset + 1)));
    testRunCPU();
    DecodedOp* root = requireCompiledRoot("multi-loop SMC");
    if (!root) return;

    context.memory->writed(TEST_HEAP_ADDRESS + armOffset, 1);
    cpu->reg[0].u32 = 0;
    cpu->reg[1].u32 = 0;
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->wasmJitBailout = 0;
    wasmJitMultiDirectLoopFixtureResetRuntime();
    runToTestEnd(cpu, 8, "multi-loop SMC generated");

    WasmJitMultiDirectLoopFixtureStats stats =
        wasmJitMultiDirectLoopFixtureGet();
    if (context.memory->readb(TEST_CODE_ADDRESS + branchDisplacementOffset) != 0 ||
            cpu->reg[0].u32 != 1 || cpu->reg[1].u32 != 4 ||
            stats.localBackedges != 0 || cpu->wasmJitActiveBlock ||
            cpu->wasmJitBailout != 0) {
        testFail("multi-loop SMC fresh-decode state byte=%u eax=%u ecx=%u local=%u active=%p bailout=%u",
            context.memory->readb(TEST_CODE_ADDRESS + branchDisplacementOffset),
            cpu->reg[0].u32, cpu->reg[1].u32, stats.localBackedges,
            cpu->wasmJitActiveBlock, cpu->wasmJitBailout);
    }
}

void runFaultFixture() {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    constexpr U32 loopStart = 5;
    constexpr U32 handler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 protectedPage = TEST_HEAP_ADDRESS + K_PAGE_SIZE;
    wasmJitMultiDirectLoopFixtureReset();
    testNewInstruction(0);

    testPushCode8(0xb9); testPushCode32(3); // mov ecx,3
    testPushCode8(0x8b); testPushCode8(0x06); // loop: mov eax,[esi]
    testPushCode8(0x83); testPushCode8(0xc6); testPushCode8(0x04); // add esi,4
    testPushCode8(0x49);                     // dec ecx
    testPushCode8(0x75); testPushCode8(0xf8); // jnz loop
    context.memory->writeb(handler, 0xcd);
    context.memory->writeb(handler + 1, 0x97);
    testRunCPU();
    DecodedOp* root = requireCompiledRoot("multi-loop fault");
    if (!root) return;

    const KSigAction savedSegv = context.process->sigActions[K_SIGSEGV];
    context.process->sigActions[K_SIGSEGV].handlerAndSigAction = handler;
    context.process->sigActions[K_SIGSEGV].flags = K_SA_SIGINFO;
    ::memset(context.process->sigActions[K_SIGSEGV].sigInfo, 0,
        sizeof(context.process->sigActions[K_SIGSEGV].sigInfo));
    context.memory->writed(protectedPage - 4, 0x12345678);
    if (context.memory->mprotect(cpu->thread, protectedPage, K_PAGE_SIZE,
            K_PROT_NONE)) {
        testFail("multi-loop fault protect setup");
        context.process->sigActions[K_SIGSEGV] = savedSegv;
        return;
    }

    const U32 initialEsp = 4096;
    cpu->reg[4].u32 = initialEsp;
    cpu->reg[6].u32 = K_PAGE_SIZE - 4;
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->wasmJitBailout = 0;
    wasmJitMultiDirectLoopFixtureResetRuntime();
    bool faulted = false;
    try {
        for (U32 step = 0;
                step < 4 &&
                (!cpu->nextOp || cpu->nextOp->inst != TestEnd);
                ++step) {
            cpu->run();
        }
    } catch (...) {
        faulted = true;
    }
    // Execute the delivered handler after the normal C++ fault envelope.
    if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
        try {
            cpu->nextOp = cpu->getNextOp();
            if (cpu->nextOp) {
                cpu->nextOp->pfn = NormalCPU::getFunctionForOp(cpu->nextOp);
                cpu->run();
            }
        } catch (...) {
            faulted = true;
        }
    }
    const U32 signalContext =
        TEST_STACK_ADDRESS - K_PAGE_SIZE * 32 + initialEsp - 768;
    const U32 signalInfo = (signalContext & ~15u) - 128;
    const U32 savedEip = context.memory->readd(signalContext + 0x4c);
    const U32 savedSignal = context.memory->readd(signalInfo);
    const U32 savedCode = context.memory->readd(signalInfo + 8);
    const U32 savedAddress = context.memory->readd(signalInfo + 12);
    const bool restored = context.memory->mprotect(cpu->thread,
        protectedPage, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE) == 0;
    context.process->sigActions[K_SIGSEGV] = savedSegv;
    WasmJitMultiDirectLoopFixtureStats stats =
        wasmJitMultiDirectLoopFixtureGet();
    if (!restored || savedSignal != K_SIGSEGV || savedCode != 2 ||
            savedAddress != protectedPage || savedEip != loopStart ||
            stats.localBackedges != 1 || cpu->wasmJitActiveBlock ||
            cpu->wasmJitBailout != 0 || !faulted ||
            !cpu->nextOp || cpu->nextOp->inst != TestEnd) {
        testFail("multi-loop fault equivalence signal=%u code=%u address=%08x eip=%08x local=%u active=%p bailout=%u restored=%u",
            savedSignal, savedCode, savedAddress, savedEip,
            stats.localBackedges, cpu->wasmJitActiveBlock,
            cpu->wasmJitBailout, restored ? 1 : 0);
    }
}

void runIntervalFilterFixture() {
    const U32 disjointTargets[] = {10, 30};
    const U32 disjointSources[] = {20, 40};
    const U32 nestedTargets[] = {10, 20};
    const U32 nestedSources[] = {50, 30};
    const U32 crossingTargets[] = {10, 20, 50};
    const U32 crossingSources[] = {30, 40, 60};
    if (wasmJitMultiDirectLoopFixtureFilterIntervals(
            disjointTargets, disjointSources, 2) != 2 ||
        wasmJitMultiDirectLoopFixtureFilterIntervals(
            nestedTargets, nestedSources, 2) != 2 ||
        wasmJitMultiDirectLoopFixtureFilterIntervals(
            crossingTargets, crossingSources, 3) != 1) {
        testFail("multi-loop laminar/crossing filter");
    }
}

} // namespace

void testWasmJitMultiDirectLoops() {
    runIntervalFilterFixture();
    runDisjointSharedBudgetFixture();
    runNestedFixture();
    runSmcFixture();
    runFaultFixture();
    printf("Multi-direct-loop disjoint/nested/shared-budget/crossing/SMC/fault OK\n");
}

#else

void testWasmJitMultiDirectLoops() {
}

#endif
