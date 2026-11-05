/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"

#ifdef __TEST

#include "testCPU.h"
#include "ksignal.h"
#include "knativeinput_math.h"
#include "../../emulation/cpu/normal/normalCPU.h"
#if defined(BOXEDWINE_JIT_ARMV8)
#include "../../emulation/cpu/armv8/jitArmV8CodeGen.h"
#endif
#if defined(BOXEDWINE_JIT_X86) || defined(BOXEDWINE_JIT_X64)
#include "../../emulation/cpu/x32/jitX86CodeGen.h"
#endif
#if defined(BOXEDWINE_WASM_JIT) && !defined(BOXEDWINE_MULTI_THREADED)
#include "../../emulation/cpu/wasm/jitWasmCodeGen.h"
#endif
#ifdef BOXEDWINE_WASM_JIT
#include "../../emulation/cpu/jit/jitCodeLifecycle.h"
#endif
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern void failed(const char* msg, ...);

namespace {

constexpr U32 TEST_PAGES_PER_SEG = 32;

std::atomic<bool> fastMode(false);
std::mutex testContextCreateMutex;
std::unique_ptr<TestContext> serialContext;
std::vector<std::unique_ptr<TestContext>> parallelContexts;
thread_local TestContext* currentContext = nullptr;
thread_local bool runningParallelTest = false;

#if defined(BOXEDWINE_JIT) && !defined(BOXEDWINE_WASM_JIT)
void OPCALL testJitRunCountCallback(CPU* cpu, DecodedOp* op) {
    (void)cpu;
    (void)op;
}
#endif

void setupSegments(TestContext& context) {
    CPU* cpu = context.cpu;
    KProcess* process = context.process.get();

    for (int i = 0; i < 6; i++) {
        cpu->seg[i].address = 0;
        cpu->seg[i].value = 0;
        process->hasSetSeg[i] = false;
    }
    process->hasSetStackMask = false;

    struct user_desc* ldt = process->getLDT(TEST_HEAP_SEG >> 3);
    ldt->entry_number = TEST_HEAP_SEG >> 3;
    ldt->base_addr = TEST_HEAP_ADDRESS;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_STACK_SEG >> 3);
    ldt->entry_number = TEST_STACK_SEG >> 3;
    ldt->base_addr = TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_CODE_SEG >> 3);
    ldt->entry_number = TEST_CODE_SEG >> 3;
    ldt->base_addr = TEST_CODE_ADDRESS;
    ldt->seg_32bit = 1;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    ldt = process->getLDT(TEST_CODE_SEG_16 >> 3);
    ldt->entry_number = TEST_CODE_SEG_16 >> 3;
    ldt->base_addr = TEST_CODE_ADDRESS;
    ldt->seg_32bit = 0;
    ldt->read_exec_only = 0;
    ldt->seg_not_present = 0;

    cpu->seg[CS].address = TEST_CODE_ADDRESS;
    cpu->seg[CS].value = TEST_CODE_SEG;
    cpu->seg[DS].address = TEST_HEAP_ADDRESS;
    cpu->seg[DS].value = TEST_HEAP_SEG;
    cpu->seg[SS].address = TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG;
    cpu->seg[SS].value = TEST_STACK_SEG;
    cpu->stackMask = 0xffffffff;
    cpu->stackNotMask = 0;
    cpu->seg[ES].address = 0;
    cpu->seg[ES].value = 0;
    cpu->seg[GS].address = 0;
    cpu->seg[GS].value = 0;
    cpu->seg[FS].address = 0;
    cpu->seg[FS].value = 0;
    process->hasSetSeg[CS] = true;
    process->hasSetSeg[DS] = true;
    process->hasSetSeg[SS] = true;
}

void resetMemory(TestContext& context) {
    context.memory->memset(TEST_CODE_ADDRESS, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
    context.memory->memset(TEST_STACK_ADDRESS - K_PAGE_SIZE * TEST_PAGES_PER_SEG, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
    context.memory->memset(TEST_HEAP_ADDRESS, 0, K_PAGE_SIZE * TEST_PAGES_PER_SEG);
}

void createContext(TestContext& context) {
    context.process = KProcess::create();
    context.memory = KMemory::create(context.process.get());
    context.process->memory = context.memory;
    context.thread = context.process->createThread();
    context.cpu = context.thread->cpu;
    KThread::setCurrentThread(context.thread);

    context.memory->mmap(context.thread, ((TEST_STACK_ADDRESS >> K_PAGE_SHIFT) - TEST_PAGES_PER_SEG) << K_PAGE_SHIFT, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_WRITE | K_PROT_READ | K_PROT_READ, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    context.memory->mmap(context.thread, TEST_CODE_ADDRESS, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_WRITE | K_PROT_READ | K_PROT_READ | K_PROT_EXEC, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    context.memory->mmap(context.thread, TEST_HEAP_ADDRESS, TEST_PAGES_PER_SEG << K_PAGE_SHIFT, K_PROT_READ | K_PROT_WRITE, K_MAP_FIXED | K_MAP_PRIVATE, -1, 0);
    setupSegments(context);
    resetMemory(context);
}

bool intInList(int value, const int* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

} // namespace

TestContext& testContext() {
    if (!currentContext) {
        std::lock_guard<std::mutex> lock(testContextCreateMutex);
        if (!serialContext) {
            serialContext = std::make_unique<TestContext>();
            createContext(*serialContext);
        }
        currentContext = serialContext.get();
    }
    KThread::setCurrentThread(currentContext->thread);
    return *currentContext;
}

void ensureParallelContexts(U32 workerCount) {
    std::lock_guard<std::mutex> lock(testContextCreateMutex);
    while (parallelContexts.size() < workerCount) {
        std::unique_ptr<TestContext> context = std::make_unique<TestContext>();
        createContext(*context);
        parallelContexts.push_back(std::move(context));
    }
}

void bindParallelContext(U32 index) {
    currentContext = parallelContexts[index].get();
    KThread::setCurrentThread(currentContext->thread);
}

void testNewInstruction(int flags) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    setupSegments(context);
    resetMemory(context);
    context.codeIp = TEST_CODE_ADDRESS;
    cpu->lazyFlagType = FLAGS_NONE;
    cpu->setFlags(flags, FMASK_ALL);
    cpu->big = 1;
    cpu->reg[0].u32 = 0;
    cpu->reg[1].u32 = 0;
    cpu->reg[2].u32 = 0;
    cpu->reg[3].u32 = 0;
    cpu->reg[4].u32 = 4096;
    cpu->reg[5].u32 = 0;
    cpu->reg[6].u32 = 0;
    cpu->reg[7].u32 = 0;
    cpu->eip.u32 = 0;
    context.memory->clearOpCache();
    cpu->mxcsr = 0x1F80;
}

void testFloatingPointStateCopy() {
    TestContext& context = testContext();
    CPU* source = context.cpu;
    std::unique_ptr<CPU> destination(CPU::allocCPU(context.memory));

    FPU savedFpu = source->fpu;
    SSE savedXmm[8];
    for (int i = 0; i < 8; i++) {
        savedXmm[i] = source->xmm[i];
        source->xmm[i].pi.u64[0] = 0x1111111100000000ULL + (U64)i;
        source->xmm[i].pi.u64[1] = 0xAAAAAAAA00000000ULL + (U64)i;
    }
    U32 savedMxcsr = source->mxcsr;
    source->fpu.cw = 0x027f;
    source->fpu.sw = 0x4500;
    source->fpu.top = 5;
    source->fpu.getMMX(3)->q = 0x0123456789ABCDEFULL;
    source->mxcsr = 0x3f80;

    destination->reg[0].u32 = 0x13572468;
    destination->flags = 0x24681357;
    destination->copyFloatingPointStateFrom(source);

    if (destination->fpu.cw != source->fpu.cw ||
            destination->fpu.sw != source->fpu.sw ||
            destination->fpu.top != source->fpu.top ||
            destination->fpu.getMMX(3)->q != source->fpu.getMMX(3)->q) {
        testFail("floating-point state copy did not preserve x87/MMX state");
    }
    for (int i = 0; i < 8; i++) {
        if (destination->xmm[i].pi.u64[0] != source->xmm[i].pi.u64[0] ||
                destination->xmm[i].pi.u64[1] != source->xmm[i].pi.u64[1]) {
            testFail("floating-point state copy did not preserve XMM%d", i);
            break;
        }
    }
    if (destination->mxcsr != source->mxcsr) {
        testFail("floating-point state copy did not preserve MXCSR");
    }
    if (destination->reg[0].u32 != 0x13572468 || destination->flags != 0x24681357) {
        testFail("floating-point state copy changed non-FP CPU state");
    }

    source->fpu = savedFpu;
    source->mxcsr = savedMxcsr;
    for (int i = 0; i < 8; i++) {
        source->xmm[i] = savedXmm[i];
    }
}

void testD3D9WebFixedInputCoordinates() {
    if (KNativeInputMath::convertCoordinate(320, 640, 1024) != 512 ||
            KNativeInputMath::convertCoordinate(240, 480, 768) != 384 ||
            KNativeInputMath::convertCoordinate(512, 1024, 640) != 320 ||
            KNativeInputMath::convertCoordinate(384, 768, 480) != 240) {
        testFail("D3D9Web 640x480 to 1024x768 input conversion failed");
    }
    if (KNativeInputMath::convertCoordinate(7, 0, 1024) != 7 ||
            KNativeInputMath::convertCoordinate(9, 640, 0) != 9) {
        testFail("input conversion zero-extent fallback failed");
    }
}

void testPushCode8(int value) {
    TestContext& context = testContext();
    context.memory->writeb(context.codeIp, value);
    context.memory->clearPageWriteCounts(context.codeIp >> K_PAGE_SHIFT);
    context.codeIp++;
}

void testPushCode16(int value) {
    TestContext& context = testContext();
    U32 startPage = context.codeIp >> K_PAGE_SHIFT;
    context.memory->writew(context.codeIp, value);
    context.codeIp += 2;
    context.memory->clearPageWriteCounts(startPage);
    context.memory->clearPageWriteCounts((context.codeIp - 1) >> K_PAGE_SHIFT);
}

void testPushCode32(int value) {
    TestContext& context = testContext();
    U32 startPage = context.codeIp >> K_PAGE_SHIFT;
    context.memory->writed(context.codeIp, value);
    context.codeIp += 4;
    context.memory->clearPageWriteCounts(startPage);
    context.memory->clearPageWriteCounts((context.codeIp - 1) >> K_PAGE_SHIFT);
}

void testRunCPU() {
#if defined(BOXEDWINE_JIT_ARMV8)
    ensureArmV8HardwareTSOForThread();
#endif
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testPushCode8(0xcd);
    testPushCode8(0x97);

    cpu->nextOp = cpu->getNextOp();
    do {
        cpu->run();
    } while (!cpu->nextOp || cpu->nextOp->inst != TestEnd);
}

void testFail(const char* msg, ...) {
    TestContext& context = testContext();
    context.failed = true;

    char text[1024];
    va_list args;
    va_start(args, msg);
    vsnprintf(text, sizeof(text), msg, args);
    va_end(args);

    context.failures.push_back(text);
    if (!runningParallelTest) {
        failed("%s", text);
    }
}

bool testIsFastMode() {
    return fastMode.load();
}

void testSetFastMode(bool fast) {
    fastMode.store(fast);
}

void testWasmJitOnlyBlockEntryIsCallable() {
#ifdef BOXEDWINE_WASM_JIT
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testNewInstruction(0);
    testPushCode8(0x40); // inc eax
    testPushCode8(0x41); // inc ecx
    testPushCode8(0x42); // inc edx
    testRunCPU();

    if (jitUsesCodeMemory()) {
        testFail("wasm jit entries are not backed by native code memory");
    }

    DecodedOp* first = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
    DecodedOp* second = first ? first->next : nullptr;
    DecodedOp* third = second ? second->next : nullptr;

    if (!first || !second || !third) {
        testFail("wasm jit block metadata decode");
        return;
    }
    if (first->pfn != cpu->thread->process->startJITOp || !first->pfnJitCode) {
        testFail("wasm jit first op callable entry");
    }
    if (second->pfn == cpu->thread->process->startJITOp || second->pfnJitCode) {
        testFail("wasm jit second op interior is not callable entry");
    }
    if (third->pfn == cpu->thread->process->startJITOp || third->pfnJitCode) {
        testFail("wasm jit third op interior is not callable entry");
    }
    if (second->blockStart != first || third->blockStart != first) {
        testFail("wasm jit interior ops keep owner block");
    }

    cpu->eip.u32 = first->len;
    cpu->nextOp = second;
    cpu->run();

    if (second->pfn != cpu->thread->process->startJITOp || !second->pfnJitCode) {
        testFail("wasm jit fallthrough interior op can compile as subblock entry");
    }
    if (second->blockStart != first) {
        testFail("wasm jit fallthrough interior keeps longer owner block");
    }
    if (third->pfn == cpu->thread->process->startJITOp || third->pfnJitCode) {
        testFail("wasm jit fallthrough subblock interior is not callable entry");
    }

    if (first->pfnJitCode == second->pfnJitCode) {
        testFail("wasm jit parent and fallthrough subblock have distinct entries");
    }

    cpu->wasmJitActiveBlock = first;
    cpu->wasmJitBailout = 0;
    context.memory->removeCodeBlock(TEST_CODE_ADDRESS, first, false);

    if (cpu->wasmJitBailout != 1) {
        testFail("wasm jit active invalidation requests bailout");
    }
    if (first->pfnJitCode || second->pfnJitCode || third->pfnJitCode) {
        testFail("wasm jit parent invalidation clears subblock entries");
    }
    if (first->blockStart || second->blockStart || third->blockStart) {
        testFail("wasm jit parent invalidation clears owner metadata");
    }

    testNewInstruction(0);
    cpu->reg[0].u32 = 0x100; // eax
    cpu->reg[2].u32 = 0x200; // edx
    cpu->reg[6].u32 = 0x103FEB4C; // stale esi value should be overwritten
    cpu->reg[7].u32 = 0x11223344; // stale edi value should be overwritten
    context.memory->writed(TEST_HEAP_ADDRESS + 0x100, 0x12345678);
    context.memory->writed(TEST_HEAP_ADDRESS + 0x104, 3);

    testPushCode8(0x8b); // mov esi,[eax+4]
    testPushCode8(0x70);
    testPushCode8(0x04);
    testPushCode8(0x8b); // mov edi,[eax]
    testPushCode8(0x38);
    testPushCode8(0x66); // mov [edx+esi*8+6],di
    testPushCode8(0x89);
    testPushCode8(0x7c);
    testPushCode8(0xf2);
    testPushCode8(0x06);
    testRunCPU();

    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x200 + 3 * 8 + 6) != 0x5678) {
        testFail("wasm jit scaled-index word store uses loaded esi/edi");
    }

    testNewInstruction(0);
    testPushCode8(0x75); // jnz to the ret, keeping ret inside a forward-branch range
    testPushCode8(0x00);
    testPushCode8(0xc3); // ret
    testPushCode8(0x43); // inc ebx, a decoded next function/op that must not join the ret block
    testPushCode8(0xcd);
    testPushCode8(0x97);

    context.memory->writed(cpu->seg[SS].address + cpu->reg[4].u32, 4);
    cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    cpu->nextOp = cpu->getNextOp();
    do {
        cpu->run();
    } while (!cpu->nextOp || cpu->nextOp->inst != TestEnd);

    DecodedOp* branch = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
    DecodedOp* ret = branch && branch->next ? branch->next : nullptr;
    DecodedOp* afterRet = ret && ret->next ? ret->next : context.memory->getDecodedOp(TEST_CODE_ADDRESS + 3);

    if (!branch || !ret || !afterRet) {
        testFail("wasm jit ret boundary metadata decode");
        return;
    }
    if (branch->pfn != cpu->thread->process->startJITOp || !branch->pfnJitCode) {
        testFail("wasm jit ret boundary block compiled");
    }
    if (branch->blockLen != 3 || branch->blockOpCount != 2) {
        testFail("wasm jit ret boundary stops at computed exit");
    }
    if (ret->blockStart != branch) {
        testFail("wasm jit ret stays in owner block");
    }
    if (afterRet->blockStart == branch) {
        testFail("wasm jit ret boundary does not absorb following decoded op");
    }

    testNewInstruction(0);
    testPushCode8(0xbe); // mov esi,3
    testPushCode32(3);
    testPushCode8(0xe8); // call helper
    testPushCode32(17);
    testPushCode8(0xbf); // mov edi,0x12345678
    testPushCode32(0x12345678);
    testPushCode8(0xba); // mov edx,0x200
    testPushCode32(0x200);
    testPushCode8(0x66); // mov [edx+esi*8+6],di
    testPushCode8(0x89);
    testPushCode8(0x7c);
    testPushCode8(0xf2);
    testPushCode8(0x06);
    testPushCode8(0xcd);
    testPushCode8(0x97);
    testPushCode8(0x56); // helper: push esi
    testPushCode8(0xbe); // mov esi,7
    testPushCode32(7);
    testPushCode8(0x5e); // pop esi
    testPushCode8(0xc3); // ret
    testRunCPU();

    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x200 + 3 * 8 + 6) != 0x5678) {
        testFail("wasm jit call preserves restored esi for caller");
    }
    if (context.memory->readw(TEST_HEAP_ADDRESS + 0x200 + 7 * 8 + 6) == 0x5678) {
        testFail("wasm jit call must not use callee-clobbered esi");
    }
#endif
}

void testWasmJitDiv32FaultArms() {
#ifdef BOXEDWINE_WASM_JIT
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    KSigAction savedFpe = context.process->sigActions[K_SIGFPE];
    struct RestoreFpeAction {
        KProcessPtr process;
        KSigAction action;
        ~RestoreFpeAction() { process->sigActions[K_SIGFPE] = action; }
    } restore{context.process, savedFpe};

    auto runFault = [&](bool isSigned, U32 eax, U32 edx, U32 divisor,
                        U32 expectedCode, const char* name) {
        testNewInstruction(0);
        constexpr U32 handler = TEST_CODE_ADDRESS + 0x300;
        context.memory->writeb(handler, 0xcd); // TestEnd interrupt
        context.memory->writeb(handler + 1, 0x97);
        context.process->sigActions[K_SIGFPE] = savedFpe;
        context.process->sigActions[K_SIGFPE].handlerAndSigAction = handler;

        cpu->reg[0].u32 = eax;
        cpu->reg[2].u32 = edx;
        cpu->reg[3].u32 = divisor;
        testPushCode8(0xf7);
        testPushCode8(isSigned ? 0xfb : 0xf3); // idiv/div ebx
        testRunCPU();

        const KSigAction& action = context.process->sigActions[K_SIGFPE];
        if (action.sigInfo[0] != K_SIGFPE || action.sigInfo[2] != expectedCode) {
            testFail("%s expected SIGFPE code %u got signal %u code %u",
                name, expectedCode, action.sigInfo[0], action.sigInfo[2]);
        }
    };

    runFault(false, 0x11111111, 0x22222222, 0, K_FPE_INTDIV,
        "wasm jit div32 zero");
    runFault(false, 0, 1, 1, K_FPE_INTOVF,
        "wasm jit div32 quotient overflow");
    runFault(true, 0x11111111, 0x22222222, 0, K_FPE_INTDIV,
        "wasm jit idiv32 zero");
    runFault(true, 0, 0x80000000u, 0xffffffffu, K_FPE_INTOVF,
        "wasm jit idiv32 int64 min overflow");
    runFault(true, 0, 1, 1, K_FPE_INTOVF,
        "wasm jit idiv32 quotient overflow");
#endif
}

void testWasmJitInt9ADirectFixture() {
#ifdef BOXEDWINE_WASM_JIT_INT9A_DIRECT_FIXTURE
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    constexpr U32 d3dBase = 0xd3900000u;
    auto runIndex = [&](U32 rawIndex, U32 expectedEax, const char* name) {
        testNewInstruction(0);
        cpu->reg[0].u32 = 0xa5a50000u;
        cpu->reg[1].u32 = 0x11111111u;
        cpu->reg[2].u32 = 0x22222222u;
        cpu->reg[3].u32 = 0x33333333u;
        cpu->reg[5].u32 = 0x55555555u;
        cpu->reg[6].u32 = 0x66666666u;
        cpu->reg[7].u32 = 0x77777777u;
        const U32 stack = cpu->seg[SS].address + cpu->reg[4].u32;
        context.memory->writed(stack + 0, rawIndex);
        context.memory->writed(stack + 4, 0);
        context.memory->writed(stack + 8, 0);
        testPushCode8(0xcd); testPushCode8(0x9a);
        testPushCode8(0xcd); testPushCode8(0x97);
        wasmJitInt9ADirectFixtureReset();
        testRunCPU();
        U64 direct = 0, fallback = 0, fault = 0;
        wasmJitInt9ADirectFixtureGet(&direct, &fallback, &fault);
        if (direct != 1 || fallback != 0 || fault != 0 || cpu->reg[0].u32 != expectedEax ||
                cpu->reg[1].u32 != 0x11111111u || cpu->reg[2].u32 != 0x22222222u ||
                cpu->reg[3].u32 != 0x33333333u || cpu->reg[5].u32 != 0x55555555u ||
                cpu->reg[6].u32 != 0x66666666u || cpu->reg[7].u32 != 0x77777777u) {
            testFail("%s direct fixture state", name);
        }
    };
    runIndex(d3dBase + 0, 0x1234u, "int9a index0");
    runIndex(d3dBase + 1, 0x3001u, "int9a index1");
    // callD3D9Web must reject both sides of its six-entry dispatch range
    // before reading arg1/arg2 or changing EAX.  These still travel through
    // the generated direct INT9A boundary, rather than testing the helper in
    // isolation.
    runIndex(d3dBase - 1, 0xa5a50000u, "int9a below-range");
    runIndex(d3dBase + 6, 0xa5a50000u, "int9a above-range");

    // Change DS before the direct boundary, leave a lazy ZF-producing INC
    // pending, then consume both state classes after it.  normal_int9A is
    // not allowed to erase either state; the generated boundary must flush
    // ECX before the helper and invalidate its GP/segment/lazy caches before
    // compiling the following moffs8/PUSHFD body.
    testNewInstruction(0);
    cpu->reg[1].u32 = 0xffffffffu; // INC ECX -> zero, lazy ZF=1
    const U32 stack = cpu->seg[SS].address + cpu->reg[4].u32;
    context.memory->writed(stack + 0, d3dBase);
    context.memory->writed(stack + 4, 0);
    context.memory->writed(stack + 8, 0);
    context.memory->writeb(cpu->seg[SS].address + 0x20, 0x5au);
    testPushCode8(0x66); testPushCode8(0xb8); testPushCode16(TEST_STACK_SEG); // mov ax,stack selector
    testPushCode8(0x8e); testPushCode8(0xd8); // mov ds,ax
    testPushCode8(0x41);                     // inc ecx
    testPushCode8(0xcd); testPushCode8(0x9a);
    testPushCode8(0xa0); testPushCode32(0x20); // mov al,[ds:0x20]
    testPushCode8(0x9c); testPushCode8(0x5e);  // pushfd; pop esi
    testPushCode8(0xcd); testPushCode8(0x97);
    wasmJitInt9ADirectFixtureReset();
    testRunCPU();
    U64 direct = 0, fallback = 0, fault = 0;
    wasmJitInt9ADirectFixtureGet(&direct, &fallback, &fault);
    if (direct != 1 || fallback != 0 || fault != 0 ||
            cpu->seg[DS].value != TEST_STACK_SEG ||
            cpu->seg[DS].address != cpu->seg[SS].address ||
            cpu->reg[1].u32 != 0 || cpu->reg[0].u32 != 0x125au ||
            (cpu->reg[6].u32 & ZF) == 0) {
        testFail("int9a direct fixture segment/lazy-state successor");
    }

    // First publish a real generated direct-INT9A root.  Do not retain or
    // dereference root after the armed CodePage write: the write invalidates
    // this block by design.
    testNewInstruction(0);
    const U32 mismatchStack = cpu->seg[SS].address + cpu->reg[4].u32;
    context.memory->writed(mismatchStack + 0, d3dBase);
    context.memory->writed(mismatchStack + 4, 0);
    context.memory->writed(mismatchStack + 8, 0);
    testPushCode8(0xcd); testPushCode8(0x9a);
    testPushCode8(0xcd); testPushCode8(0x97);
    DecodedOp* root = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!root) {
        testFail("int9a direct fixture re-decode root");
        return;
    }
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        cpu->eip.u32 = 0;
        cpu->nextOp = root;
        cpu->run();
        if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
            testFail("int9a direct fixture re-decode warm flow");
            return;
        }
    }
    if (root->pfn != cpu->thread->process->startJITOp || !root->pfnJitCode || root->blockStart != root) {
        testFail("int9a direct fixture re-decode warm root");
        return;
    }
    wasmJitInt9ADirectFixtureReset();
    wasmJitInt9ADirectFixtureArmRedecodeMismatch();
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->run();
    // root was invalidated by the fixture write.  Fetch only a fresh decoded
    // object and verify the exact same-length non-INT replacement.
    DecodedOp* fresh = cpu->getOp(TEST_CODE_ADDRESS, 0);
    wasmJitInt9ADirectFixtureGet(&direct, &fallback, &fault);
    if (!fresh || fresh->inst == Int9A || fresh->len != 2 ||
            context.memory->readb(TEST_CODE_ADDRESS) != 0x89 ||
            context.memory->readb(TEST_CODE_ADDRESS + 1) != 0xc0 ||
            direct != 0 || fallback != 1 || fault != 0 ||
            cpu->reg[0].u32 != 0x1234u || cpu->wasmJitActiveBlock || cpu->wasmJitBailout != 0) {
        testFail("int9a direct fixture SMC re-decode fallback");
    }

    struct StackFaultObservation {
        U64 direct = 0, fallback = 0, fault = 0;
        U32 savedEip = 0, signal = 0, signalCode = 0, signalAddress = 0, eax = 0;
        bool handlerEnded = false;
    };
    constexpr U32 faultHandler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 faultMarker = 0x19aaf00d;
    const U32 stackBase = cpu->seg[SS].address;
    const U32 protectedStackPage = stackBase + K_PAGE_SIZE;
    auto runStackFault = [&](U32 faultWord, bool generated) {
        StackFaultObservation observed;
        // word 0 begins on the protected page; words 1/2 begin there after
        // preceding stack words remain readable at the prior-page tail.
        const U32 esp = faultWord == 0 ? K_PAGE_SIZE : K_PAGE_SIZE - 4 * faultWord;
        testNewInstruction(0);
        cpu->reg[4].u32 = esp;
        context.memory->writed(stackBase + esp + 0, d3dBase);
        context.memory->writed(stackBase + esp + 4, 0);
        context.memory->writed(stackBase + esp + 8, 0);
        testPushCode8(0xcd); testPushCode8(0x9a);
        testPushCode8(0xcd); testPushCode8(0x97);
        context.memory->writeb(faultHandler, 0xb8); // mov eax,faultMarker
        context.memory->writed(faultHandler + 1, faultMarker);
        context.memory->writeb(faultHandler + 5, 0xcd);
        context.memory->writeb(faultHandler + 6, 0x97);
        DecodedOp* faultRoot = cpu->getOp(TEST_CODE_ADDRESS, 0);
        if (!faultRoot) {
            testFail("int9a stack-fault root decode");
            return observed;
        }
        if (generated) {
            // Publish the same direct boundary before protection; this warm
            // run is not part of the fault observation.
            testRunCPU();
            faultRoot = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
            if (!faultRoot || faultRoot->pfn != cpu->thread->process->startJITOp || !faultRoot->pfnJitCode) {
                testFail("int9a stack-fault generated root");
                return observed;
            }
        } else {
            faultRoot->pfn = NormalCPU::getFunctionForOp(faultRoot);
        }
        const KSigAction savedSegv = context.process->sigActions[K_SIGSEGV];
        context.process->sigActions[K_SIGSEGV].handlerAndSigAction = faultHandler;
        context.process->sigActions[K_SIGSEGV].flags = K_SA_SIGINFO;
        ::memset(context.process->sigActions[K_SIGSEGV].sigInfo, 0,
            sizeof(context.process->sigActions[K_SIGSEGV].sigInfo));
        if (context.memory->mprotect(cpu->thread, protectedStackPage, K_PAGE_SIZE, K_PROT_NONE)) {
            testFail("int9a stack-fault protect setup");
            context.process->sigActions[K_SIGSEGV] = savedSegv;
            return observed;
        }
        wasmJitInt9ADirectFixtureReset();
        cpu->eip.u32 = 0;
        cpu->nextOp = faultRoot;
        try {
            for (U32 step = 0; step < 4 && (!cpu->nextOp || cpu->nextOp->inst != TestEnd); ++step) cpu->run();
        } catch (...) {
            // NormalCPU throws after delivery. Resume at the live handler
            // decode, matching the outer dispatcher behavior.
            cpu->nextOp = cpu->getNextOp();
            if (cpu->nextOp) cpu->nextOp->pfn = NormalCPU::getFunctionForOp(cpu->nextOp);
            try {
                for (U32 step = 0; step < 2 && (!cpu->nextOp || cpu->nextOp->inst != TestEnd); ++step) cpu->run();
            } catch (...) {
                testFail("int9a stack-fault handler execution");
            }
        }
        const bool restored = context.memory->mprotect(cpu->thread, protectedStackPage, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE) == 0;
        context.process->sigActions[K_SIGSEGV] = savedSegv;
        wasmJitInt9ADirectFixtureGet(&observed.direct, &observed.fallback, &observed.fault);
        const U32 signalContext = stackBase + esp - 768;
        const U32 signalInfo = (signalContext & ~15) - 128;
        observed.savedEip = context.memory->readd(signalContext + 0x4c);
        observed.signal = context.memory->readd(signalInfo);
        observed.signalCode = context.memory->readd(signalInfo + 8);
        observed.signalAddress = context.memory->readd(signalInfo + 12);
        observed.eax = cpu->reg[0].u32;
        observed.handlerEnded = cpu->nextOp && cpu->nextOp->inst == TestEnd;
        if (!restored) testFail("int9a stack-fault protect restore");
        return observed;
    };
    for (U32 word = 0; word < 3; ++word) {
        const StackFaultObservation oracle = runStackFault(word, false);
        const StackFaultObservation directFault = runStackFault(word, true);
        const U32 expectedAddress = stackBase + K_PAGE_SIZE;
        if (!oracle.handlerEnded || !directFault.handlerEnded || oracle.signal != K_SIGSEGV ||
                oracle.signalCode != 2 || oracle.signalAddress != expectedAddress ||
                directFault.direct != 0 || directFault.fallback != 0 || directFault.fault != 1 ||
                directFault.savedEip != oracle.savedEip || directFault.signal != oracle.signal ||
                directFault.signalCode != oracle.signalCode || directFault.signalAddress != oracle.signalAddress ||
                directFault.eax != oracle.eax || directFault.eax != faultMarker) {
            testFail("int9a direct fixture stack-word fault equivalence");
            break;
        }
    }
    printf("INT9A direct fixture index/range/segment-lazy OK\n");
#elif defined(BOXEDWINE_WASM_JIT_INT9A_FN8_DIRECT_DISPATCH)
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    constexpr U32 d3dBase = 0xd3900000u;

    auto readCounts = [&]() {
        struct Counts { U64 direct = 0, generic = 0, fault = 0; } result;
        wasmJitInt9AFn8DirectFixtureGet(&result.direct, &result.generic, &result.fault);
        return result;
    };

    // The generated module must retain its ordinary helpers.fn_8 boundary.
    // Only CPU::runNextSingleOp's live decoded dispatch becomes the direct
    // normal_int9A call, so this witness must be direct=1/generic=0.
    testNewInstruction(0);
    cpu->reg[0].u32 = 0xa5a50000u;
    const U32 directStack = cpu->seg[SS].address + cpu->reg[4].u32;
    context.memory->writed(directStack + 0, d3dBase);
    context.memory->writed(directStack + 4, 0);
    context.memory->writed(directStack + 8, 0);
    testPushCode8(0xcd); testPushCode8(0x9a);
    wasmJitInt9AFn8DirectFixtureReset();
    testRunCPU();
    auto counts = readCounts();
    if (counts.direct != 1 || counts.generic != 0 || counts.fault != 0 ||
            cpu->reg[0].u32 != 0x1234u) {
        testFail("int9a fn8 direct dispatch state");
    }

    // AAD is an ordinary non-Int9A emulateSingleOp user.  It must retain the
    // exact NormalCPU::getFunctionForOp route and prove that the candidate did
    // not turn helpers.fn_8 into an Int9A-only helper.
    testNewInstruction(0);
    cpu->reg[0].u32 = 0x00000102u; // AH=1, AL=2; AAD 10 -> AX=0x000c
    testPushCode8(0xd5); testPushCode8(0x0a);
    wasmJitInt9AFn8DirectFixtureReset();
    testRunCPU();
    counts = readCounts();
    if (counts.direct != 0 || counts.generic != 1 || counts.fault != 0 ||
            cpu->reg[0].u16 != 0x000cu) {
        testFail("int9a fn8 non-int generic dispatch state");
    }

    // Flush dirty GP and segment state into the existing fn_8 envelope, then
    // consume both classes plus lazy ZF after the direct Int9A call.
    testNewInstruction(0);
    cpu->reg[1].u32 = 0xffffffffu;
    const U32 successorStack = cpu->seg[SS].address + cpu->reg[4].u32;
    context.memory->writed(successorStack + 0, d3dBase);
    context.memory->writed(successorStack + 4, 0);
    context.memory->writed(successorStack + 8, 0);
    context.memory->writeb(cpu->seg[SS].address + 0x20, 0x5au);
    testPushCode8(0x66); testPushCode8(0xb8); testPushCode16(TEST_STACK_SEG);
    testPushCode8(0x8e); testPushCode8(0xd8); // mov ds,ax
    testPushCode8(0x41);                     // inc ecx
    testPushCode8(0xcd); testPushCode8(0x9a);
    testPushCode8(0xa0); testPushCode32(0x20); // mov al,[ds:0x20]
    testPushCode8(0x9c); testPushCode8(0x5e);  // pushfd; pop esi
    wasmJitInt9AFn8DirectFixtureReset();
    testRunCPU();
    counts = readCounts();
    // MOV DS itself is the one expected non-Int9A fn_8 call in this block.
    if (counts.direct != 1 || counts.generic != 1 || counts.fault != 0 ||
            cpu->seg[DS].value != TEST_STACK_SEG ||
            cpu->seg[DS].address != cpu->seg[SS].address ||
            cpu->reg[1].u32 != 0 || cpu->reg[0].u32 != 0x125au ||
            (cpu->reg[6].u32 & ZF) == 0) {
        testFail("int9a fn8 direct segment/lazy successor counts=%llu/%llu/%llu ds=%x/%x ss=%x ecx=%x eax=%x esi=%x",
            counts.direct, counts.generic, counts.fault, cpu->seg[DS].value,
            cpu->seg[DS].address, cpu->seg[SS].address, cpu->reg[1].u32,
            cpu->reg[0].u32, cpu->reg[6].u32);
    }

    // Warm and publish an ordinary generated fn_8 root, then mutate CD 9A to
    // same-length MOV EAX,EAX inside the test-only helper boundary.  The live
    // decode must take the generic branch and the existing bailout envelope
    // must retire the invalidated root without stale direct execution.
    testNewInstruction(0);
    cpu->reg[0].u32 = 0x1234u;
    const U32 mismatchStack = cpu->seg[SS].address + cpu->reg[4].u32;
    context.memory->writed(mismatchStack + 0, d3dBase);
    context.memory->writed(mismatchStack + 4, 0);
    context.memory->writed(mismatchStack + 8, 0);
    testPushCode8(0xcd); testPushCode8(0x9a);
    testPushCode8(0xcd); testPushCode8(0x97);
    DecodedOp* root = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!root) {
        testFail("int9a fn8 re-decode root");
        return;
    }
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        cpu->eip.u32 = 0;
        cpu->nextOp = root;
        cpu->run();
        if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
            testFail("int9a fn8 re-decode warm flow");
            return;
        }
    }
    if (root->pfn != cpu->thread->process->startJITOp || !root->pfnJitCode || root->blockStart != root) {
        testFail("int9a fn8 re-decode warm root");
        return;
    }
    wasmJitInt9AFn8DirectFixtureReset();
    wasmJitInt9AFn8DirectFixtureArmRedecodeMismatch();
    cpu->eip.u32 = 0;
    cpu->nextOp = root;
    cpu->run();
    DecodedOp* fresh = cpu->getOp(TEST_CODE_ADDRESS, 0);
    counts = readCounts();
    if (!fresh || fresh->inst == Int9A || fresh->len != 2 ||
            context.memory->readb(TEST_CODE_ADDRESS) != 0x89 ||
            context.memory->readb(TEST_CODE_ADDRESS + 1) != 0xc0 ||
            counts.direct != 0 || counts.generic != 1 || counts.fault != 0 ||
            cpu->reg[0].u32 != 0x1234u || cpu->wasmJitActiveBlock || cpu->wasmJitBailout != 0) {
        testFail("int9a fn8 SMC re-decode generic fallback");
    }

    // Compare each protected stack-word fault with the NormalCPU oracle.  The
    // candidate deliberately uses the existing fn_8 exception envelope: no
    // historical marker-2 arm is present or expected.
    struct StackFaultObservation {
        U64 direct = 0, generic = 0, fault = 0;
        U32 savedEip = 0, signal = 0, signalCode = 0, signalAddress = 0, eax = 0;
        bool handlerEnded = false;
    };
    constexpr U32 faultHandler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 faultMarker = 0x19a8f00d;
    const U32 stackBase = cpu->seg[SS].address;
    const U32 protectedStackPage = stackBase + K_PAGE_SIZE;
    auto runStackFault = [&](U32 faultWord, bool generated) {
        StackFaultObservation observed;
        const U32 esp = faultWord == 0 ? K_PAGE_SIZE : K_PAGE_SIZE - 4 * faultWord;
        testNewInstruction(0);
        cpu->reg[4].u32 = esp;
        context.memory->writed(stackBase + esp + 0, d3dBase);
        context.memory->writed(stackBase + esp + 4, 0);
        context.memory->writed(stackBase + esp + 8, 0);
        testPushCode8(0xcd); testPushCode8(0x9a);
        context.memory->writeb(faultHandler, 0xb8); // mov eax,faultMarker
        context.memory->writed(faultHandler + 1, faultMarker);
        context.memory->writeb(faultHandler + 5, 0xcd);
        context.memory->writeb(faultHandler + 6, 0x97);
        DecodedOp* faultRoot = cpu->getOp(TEST_CODE_ADDRESS, 0);
        if (!faultRoot) {
            testFail("int9a fn8 stack-fault root decode");
            return observed;
        }
        if (generated) {
            testRunCPU();
            faultRoot = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
            if (!faultRoot || faultRoot->pfn != cpu->thread->process->startJITOp || !faultRoot->pfnJitCode) {
                testFail("int9a fn8 stack-fault generated root");
                return observed;
            }
        } else {
            faultRoot->pfn = NormalCPU::getFunctionForOp(faultRoot);
        }
        const KSigAction savedSegv = context.process->sigActions[K_SIGSEGV];
        context.process->sigActions[K_SIGSEGV].handlerAndSigAction = faultHandler;
        context.process->sigActions[K_SIGSEGV].flags = K_SA_SIGINFO;
        ::memset(context.process->sigActions[K_SIGSEGV].sigInfo, 0,
            sizeof(context.process->sigActions[K_SIGSEGV].sigInfo));
        if (context.memory->mprotect(cpu->thread, protectedStackPage, K_PAGE_SIZE, K_PROT_NONE)) {
            testFail("int9a fn8 stack-fault protect setup");
            context.process->sigActions[K_SIGSEGV] = savedSegv;
            return observed;
        }
        wasmJitInt9AFn8DirectFixtureReset();
        cpu->eip.u32 = 0;
        cpu->nextOp = faultRoot;
        try {
            for (U32 step = 0; step < 4 && (!cpu->nextOp || cpu->nextOp->inst != TestEnd); ++step) cpu->run();
        } catch (...) {
            cpu->nextOp = cpu->getNextOp();
            if (cpu->nextOp) cpu->nextOp->pfn = NormalCPU::getFunctionForOp(cpu->nextOp);
            try {
                for (U32 step = 0; step < 2 && (!cpu->nextOp || cpu->nextOp->inst != TestEnd); ++step) cpu->run();
            } catch (...) {
                testFail("int9a fn8 stack-fault handler execution");
            }
        }
        const bool restored = context.memory->mprotect(cpu->thread, protectedStackPage, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE) == 0;
        context.process->sigActions[K_SIGSEGV] = savedSegv;
        wasmJitInt9AFn8DirectFixtureGet(&observed.direct, &observed.generic, &observed.fault);
        const U32 signalContext = stackBase + esp - 768;
        const U32 signalInfo = (signalContext & ~15) - 128;
        observed.savedEip = context.memory->readd(signalContext + 0x4c);
        observed.signal = context.memory->readd(signalInfo);
        observed.signalCode = context.memory->readd(signalInfo + 8);
        observed.signalAddress = context.memory->readd(signalInfo + 12);
        observed.eax = cpu->reg[0].u32;
        observed.handlerEnded = cpu->nextOp && cpu->nextOp->inst == TestEnd;
        if (!restored) testFail("int9a fn8 stack-fault protect restore");
        return observed;
    };
    for (U32 word = 0; word < 3; ++word) {
        const StackFaultObservation oracle = runStackFault(word, false);
        const StackFaultObservation directFault = runStackFault(word, true);
        const U32 expectedAddress = stackBase + K_PAGE_SIZE;
        if (!oracle.handlerEnded || !directFault.handlerEnded || oracle.signal != K_SIGSEGV ||
                oracle.signalCode != 2 || oracle.signalAddress != expectedAddress ||
                directFault.direct != 0 || directFault.generic != 0 || directFault.fault != 1 ||
                directFault.savedEip != oracle.savedEip || directFault.signal != oracle.signal ||
                directFault.signalCode != oracle.signalCode || directFault.signalAddress != oracle.signalAddress ||
                directFault.eax != oracle.eax || directFault.eax != faultMarker) {
            testFail("int9a fn8 stack-word fault equivalence");
            break;
        }
    }

    printf("INT9A fn8 direct/generic/successor/SMC/fault OK\n");
#endif
}

void testWasmJitOomRetryAfterRelease() {
#if defined(BOXEDWINE_WASM_JIT) && !defined(BOXEDWINE_MULTI_THREADED)
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testNewInstruction(0);
    wasmJitTestResetRuntimeBatching();
    boxedwine_wasm_test_reset_oom_state();
    struct ResetOomState {
        ~ResetOomState() {
            wasmJitTestResetRuntimeBatching();
            boxedwine_wasm_test_reset_oom_state();
        }
    } resetOomState;

    U32 addresses[5];
    for (U32& address : addresses) {
        address = context.codeIp;
        testPushCode8(0x40); // inc eax
        testPushCode8(0xcd);
        testPushCode8(0x97); // TestEnd
    }

    auto compileBlock = [&](U32 address) -> DecodedOp* {
        DecodedOp* op = cpu->getOp(address, 0);
        if (!op) {
            testFail("wasm jit OOM recovery decode at %x", address);
            return nullptr;
        }
        startNewJIT(cpu, address, op);
        return op;
    };
    auto isJitBlock = [&](DecodedOp* op) {
        return op && op->pfn == cpu->thread->process->startJITOp && op->pfnJitCode && (op->flags & OP_FLAG_JIT);
    };

    DecodedOp* releasable = compileBlock(addresses[0]);
    if (!isJitBlock(releasable)) {
        testFail("wasm jit OOM recovery setup block compiled");
        return;
    }
    WasmJitRuntimeStatsSnapshot stats = wasmJitTestGetRuntimeStats();
    U64 rawBytesPerBlock = stats.rawInputBytes;
    if (stats.translatedAnonymous != 1 || stats.translatedFileBacked != 0 || stats.standaloneModules != 1 || rawBytesPerBlock == 0) {
        testFail("wasm jit standalone OOM statistics setup");
    }

    boxedwine_wasm_test_force_next_module_oom();
    DecodedOp* oomBlock = compileBlock(addresses[1]);
    if (isJitBlock(oomBlock)) {
        testFail("wasm jit forced OOM falls back to interpreter");
    }
    stats = wasmJitTestGetRuntimeStats();
    if (stats.translatedAnonymous != 2 || stats.rawInputBytes != rawBytesPerBlock * 2 || stats.oomRetries != 0 || stats.oomResumptions != 0) {
        testFail("wasm jit standalone OOM translation statistics");
    }

    DecodedOp* blockedBlock = compileBlock(addresses[2]);
    if (isJitBlock(blockedBlock)) {
        testFail("wasm jit OOM blocks compilation until release");
    }
    stats = wasmJitTestGetRuntimeStats();
    if (stats.translatedAnonymous != 3 || stats.rawInputBytes != rawBytesPerBlock * 3 || stats.oomRetries != 0 || stats.oomResumptions != 0) {
        testFail("wasm jit blocked standalone translation statistics");
    }

    context.memory->removeCodeBlock(addresses[0], releasable, false);

    DecodedOp* retryBlock = compileBlock(addresses[3]);
    if (!isJitBlock(retryBlock)) {
        testFail("wasm jit release permits one recovery probe");
    }
    stats = wasmJitTestGetRuntimeStats();
    if (stats.translatedAnonymous != 4 || stats.rawInputBytes != rawBytesPerBlock * 4 || stats.oomRetries != 1 || stats.oomResumptions != 1) {
        testFail("wasm jit standalone OOM retry statistics");
    }

    DecodedOp* continuedBlock = compileBlock(addresses[4]);
    if (!isJitBlock(continuedBlock)) {
        testFail("wasm jit successful OOM recovery resumes compilation");
    }
    stats = wasmJitTestGetRuntimeStats();
    if (stats.translatedAnonymous != 5 || stats.rawInputBytes != rawBytesPerBlock * 5 || stats.oomRetries != 1 || stats.oomResumptions != 1) {
        testFail("wasm jit resumed standalone statistics");
    }
#endif
}

namespace {

constexpr U32 CROSS_BLOCK_FLAGS_TARGET_OFFSET = 0x100;
constexpr U32 CROSS_BLOCK_FLAGS_MASK = CF | PF | AF | ZF | SF | OF;

void padCrossBlockFlagsCodeToTarget() {
    TestContext& context = testContext();
    while (context.codeIp < TEST_CODE_ADDRESS + CROSS_BLOCK_FLAGS_TARGET_OFFSET) {
        testPushCode8(0x90); // nop
    }
}

void verifyCrossBlockJitEntries(const char* name) {
#ifdef BOXEDWINE_JIT
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    DecodedOp* producer = context.memory->getDecodedOp(TEST_CODE_ADDRESS);
    DecodedOp* consumer = context.memory->getDecodedOp(TEST_CODE_ADDRESS + CROSS_BLOCK_FLAGS_TARGET_OFFSET);

    if (!producer || !consumer) {
        testFail("%s metadata decode", name);
        return;
    }
    if (producer->pfn != cpu->thread->process->startJITOp || !producer->pfnJitCode) {
        testFail("%s producer compiled entry", name);
    }
    if (consumer->pfn != cpu->thread->process->startJITOp || !consumer->pfnJitCode) {
        testFail("%s consumer compiled entry", name);
    }
    if (producer->blockStart != producer) {
        testFail("%s producer owns block", name);
    }
    if (consumer->blockStart != consumer) {
        testFail("%s consumer owns block", name);
    }
    if (producer->pfnJitCode && producer->pfnJitCode == consumer->pfnJitCode) {
        testFail("%s producer and consumer use distinct compiled entries", name);
    }
#else
    (void)name;
#endif
}

void runCrossBlockFlagsCase(const U8* producerCode, U32 producerCodeLen, U32 initialFlags,
                            U32 esi, U32 edi, U32 expectedEsi, U32 expectedFlags,
                            const char* name) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testNewInstruction((int)initialFlags);
    cpu->reg[0].u32 = CROSS_BLOCK_FLAGS_TARGET_OFFSET; // eax: indirect jump target
    cpu->reg[6].u32 = esi;
    cpu->reg[7].u32 = edi;

    for (U32 i = 0; i < producerCodeLen; ++i) {
        testPushCode8(producerCode[i]);
    }
    testPushCode8(0xff); // jmp eax
    testPushCode8(0xe0);
    padCrossBlockFlagsCodeToTarget();
    testPushCode8(0x9c); // pushfd
    testPushCode8(0x5b); // pop ebx

    testRunCPU();

    if (cpu->reg[6].u32 != expectedEsi) {
        testFail("%s result", name);
    }
    if (((cpu->reg[3].u32 ^ expectedFlags) & CROSS_BLOCK_FLAGS_MASK) != 0) {
        testFail("%s flags", name);
    }
    verifyCrossBlockJitEntries(name);
}

}

void testFlagsAcrossIndirectJitBlockBoundary() {
    static const U8 incEsi[] = {0x46};
    runCrossBlockFlagsCase(incEsi, sizeof(incEsi), CROSS_BLOCK_FLAGS_MASK,
                           0, 0, 1, CF, "cross-block inc/pushfd");

    static const U8 subEsiEdi[] = {0x29, 0xfe};
    runCrossBlockFlagsCase(subEsiEdi, sizeof(subEsiEdi), ZF | OF,
                           0, 1, 0xffffffff, CF | PF | AF | SF,
                           "cross-block sub/pushfd");
}

// The producer and Jcc deliberately live in distinct JIT blocks.  At the
// consumer, Jit's compile-time currentLazyFlags is FLAGS_NULL while CPU keeps
// the exact producer in lazyFlagType.  This is the contract used by the
// runtime-lazy Z/NZ specialization; ordinary same-block Jcc tests do not
// exercise it.
void runCrossBlockRuntimeZNZCase(const U8* producerCode, U32 producerCodeLen,
                                 U32 initialFlags, U32 ecx, U32 edx,
                                 U32 esi, U32 edi, U8 jcc,
                                 U32 expectedEbx, const char* name) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;

    testNewInstruction((int)initialFlags);
    cpu->reg[0].u32 = CROSS_BLOCK_FLAGS_TARGET_OFFSET; // eax: indirect target
    cpu->reg[1].u32 = ecx;
    cpu->reg[2].u32 = edx;
    cpu->reg[6].u32 = esi;
    cpu->reg[7].u32 = edi;

    for (U32 i = 0; i < producerCodeLen; ++i) {
        testPushCode8(producerCode[i]);
    }
    testPushCode8(0xff); // jmp eax: force a separately compiled consumer
    testPushCode8(0xe0);
    padCrossBlockFlagsCodeToTarget();
    testPushCode8(jcc);  // JZ/JNZ +7, over false-path mov + jmp
    testPushCode8(0x07);
    testPushCode8(0xbb); // false path: mov ebx, 0x22222222
    testPushCode32(0x22222222);
    testPushCode8(0xeb); // skip true path
    testPushCode8(0x05);
    testPushCode8(0xbb); // true path: mov ebx, 0x11111111
    testPushCode32(0x11111111);

    testRunCPU();

    if (cpu->reg[3].u32 != expectedEbx) {
        testFail("%s condition result", name);
    }
    verifyCrossBlockJitEntries(name);
}

void runRuntimeZNZBinaryResultZero(U8 opcode8, const char* name) {
    const U8 b8[] = {opcode8, 0xd1};
    const U8 b16[] = {0x66, (U8)(opcode8 + 1), 0xd1};
    const U8 b32[] = {(U8)(opcode8 + 1), 0xd1};
    // b8/b16 deliberately retain non-zero high result-union bytes.
    runCrossBlockRuntimeZNZCase(b8, sizeof(b8), 0,
                                 0xa5a50000, 0x5a5a0000, 0, 0,
                                 0x74, 0x11111111, name);
    runCrossBlockRuntimeZNZCase(b16, sizeof(b16), 0,
                                 0xa5a50000, 0x5a5a0000, 0, 0,
                                 0x74, 0x11111111, name);
    runCrossBlockRuntimeZNZCase(b32, sizeof(b32), 0,
                                 0, 0, 0, 0,
                                 0x74, 0x11111111, name);
}

void runRuntimeZNZUnaryResultZero(U8 opcode8, U8 opcodeWide, U8 modrm,
                                  U32 b8Input, U32 b16Input, U32 b32Input,
                                  const char* name) {
    const U8 b8[] = {opcode8, modrm};
    const U8 b16[] = {0x66, opcodeWide, modrm};
    const U8 b32[] = {opcodeWide, modrm};
    runCrossBlockRuntimeZNZCase(b8, sizeof(b8), 0,
                                 b8Input, 0, 0, 0,
                                 0x74, 0x11111111, name);
    runCrossBlockRuntimeZNZCase(b16, sizeof(b16), 0,
                                 b16Input, 0, 0, 0,
                                 0x74, 0x11111111, name);
    runCrossBlockRuntimeZNZCase(b32, sizeof(b32), 0,
                                 b32Input, 0, 0, 0,
                                 0x74, 0x11111111, name);
}

void testRuntimeLazyZNZAcrossIndirectJitBlockBoundary() {
    static const U8 pushfPopf[] = {0x9c, 0x9d};     // FLAGS_NONE, architectural ZF
    static const U8 subThenRol[] = {0x29, 0xfe, 0xd1, 0xc1};
                                                   // FLAGS_CFOF, prev=FLAGS_SUB32
    static const U8 subThenTwoRotates[] = {0x29, 0xfe, 0xd1, 0xc1, 0xd1, 0xc9};
                                                   // second CFOF must retain the same prev producer
    static const U8 dshl16[] = {0x66, 0x0f, 0xa4, 0xd1, 0x01};
    static const U8 dshl32[] = {0x0f, 0xa4, 0xd1, 0x01};
    static const U8 dshr16[] = {0x66, 0x0f, 0xac, 0xd1, 0x01};
    static const U8 dshr32[] = {0x0f, 0xac, 0xd1, 0x01};

    // Enum 1..42: the first fourteen producers are repeating b8/b16/b32
    // triples.  Every b8/b16 case leaves upper result bytes non-zero, so
    // the test fails if the runtime path omits its width mask.
    runRuntimeZNZBinaryResultZero(0x00, "runtime-z add");
    runRuntimeZNZBinaryResultZero(0x08, "runtime-z or");
    runRuntimeZNZBinaryResultZero(0x10, "runtime-z adc");
    runRuntimeZNZBinaryResultZero(0x18, "runtime-z sbb");
    runRuntimeZNZBinaryResultZero(0x20, "runtime-z and");
    runRuntimeZNZBinaryResultZero(0x28, "runtime-z sub");
    runRuntimeZNZBinaryResultZero(0x30, "runtime-z xor");
    runRuntimeZNZUnaryResultZero(0xfe, 0xff, 0xc1,
                                 0xa5a500ff, 0xa5a5ffff, 0xffffffff,
                                 "runtime-z inc");
    runRuntimeZNZUnaryResultZero(0xfe, 0xff, 0xc9,
                                 0xa5a50001, 0xa5a50001, 0x00000001,
                                 "runtime-z dec");
    runRuntimeZNZUnaryResultZero(0xd0, 0xd1, 0xe1,
                                 0xa5a50000, 0xa5a50000, 0x00000000,
                                 "runtime-z shl");
    runRuntimeZNZUnaryResultZero(0xd0, 0xd1, 0xe9,
                                 0xa5a50000, 0xa5a50000, 0x00000000,
                                 "runtime-z shr");
    runRuntimeZNZUnaryResultZero(0xd0, 0xd1, 0xf9,
                                 0xa5a50000, 0xa5a50000, 0x00000000,
                                 "runtime-z sar");
    runRuntimeZNZBinaryResultZero(0x38, "runtime-z cmp");
    runRuntimeZNZBinaryResultZero(0x84, "runtime-z test");

    // Enum 43..49 has the non-triple DSHL/DSHR/NEG width mapping.
    runCrossBlockRuntimeZNZCase(dshl16, sizeof(dshl16), 0,
                                 0xa5a50000, 0x5a5a0000, 0, 0,
                                 0x74, 0x11111111, "runtime-z dshl16");
    runCrossBlockRuntimeZNZCase(dshl32, sizeof(dshl32), 0,
                                 0, 0, 0, 0,
                                 0x74, 0x11111111, "runtime-z dshl32");
    runCrossBlockRuntimeZNZCase(dshr16, sizeof(dshr16), 0,
                                 0xa5a50000, 0x5a5a0000, 0, 0,
                                 0x74, 0x11111111, "runtime-z dshr16");
    runCrossBlockRuntimeZNZCase(dshr32, sizeof(dshr32), 0,
                                 0, 0, 0, 0,
                                 0x74, 0x11111111, "runtime-z dshr32");
    runRuntimeZNZUnaryResultZero(0xf6, 0xf7, 0xd9,
                                 0xa5a50000, 0xa5a50000, 0x00000000,
                                 "runtime-z neg");

    runCrossBlockRuntimeZNZCase(pushfPopf, sizeof(pushfPopf), ZF,
                                 0, 0, 0, 0,
                                 0x75, 0x22222222, "runtime-z nz flags-none");
    runCrossBlockRuntimeZNZCase(subThenRol, sizeof(subThenRol), 0,
                                 1, 0, 1, 1,
                                 0x74, 0x11111111, "runtime-z jz cfof-prev");
    runCrossBlockRuntimeZNZCase(subThenTwoRotates, sizeof(subThenTwoRotates), 0,
                                 1, 0, 1, 1,
                                 0x74, 0x11111111, "runtime-z jz repeated-cfof-prev");

    static const U8 subCxDx[] = {0x66, 0x29, 0xd1};
    runCrossBlockRuntimeZNZCase(subCxDx, sizeof(subCxDx), 0,
                                 0xa5a50002, 0x5a5a0001, 0, 0,
                                 0x75, 0x11111111, "runtime-z nz nonzero");
}

#ifdef BOXEDWINE_WASM_JIT_RUNTIME_LOGIC_COMPOSITE32_FIXTURE
// Each consumer performs SETcc, CMOVcc, then Jcc after an indirect block
// boundary.  The three reads must all take the private runtime-b32 candidate
// arm (or all take the retained helper for a negative producer).
static bool runtimeLogicComposite32Expected(U8 jcc, U32 value) {
    bool z = value == 0;
    bool s = (value & 0x80000000u) != 0;
    switch (jcc) {
    case 0x76: return z;          // BE; logical CF is zero
    case 0x77: return !z;         // NBE
    case 0x7c: return s;          // L; logical OF is zero
    case 0x7d: return !s;         // NL
    case 0x7e: return z || s;     // LE
    case 0x7f: return !z && !s;   // NLE
    default: return false;
    }
}

static U32 runtimeLogicComposite32ConditionIndex(U8 jcc) {
    switch (jcc) {
    case 0x76: return 0; // BE
    case 0x77: return 1; // NBE
    case 0x7c: return 2; // L
    case 0x7d: return 3; // NL
    case 0x7e: return 4; // LE
    case 0x7f: return 5; // NLE
    default: return 6;
    }
}

static void runRuntimeLogicComposite32Case(const U8* producer, U32 producerLen,
                                           U32 ecx, U32 edx, U32 expectedResult,
                                           LazyFlagType expectedType, U8 jcc,
                                           bool expectedCondition, bool expectCandidate,
                                           const char* name) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    U32 initialEsp;
    const U32 conditionIndex = runtimeLogicComposite32ConditionIndex(jcc);
    if (conditionIndex >= 6) { testFail("%s fixture condition index", name); return; }
    U32 candidateBefore = cpu->wasmJitRuntimeLogicComposite32FixtureCandidate[conditionIndex];
    U32 fallbackBefore = cpu->wasmJitRuntimeLogicComposite32FixtureFallback[conditionIndex];

    testNewInstruction(0);
    cpu->reg[0].u32 = CROSS_BLOCK_FLAGS_TARGET_OFFSET;
    cpu->reg[1].u32 = ecx;
    cpu->reg[2].u32 = edx;
    cpu->reg[3].u32 = 0xa5a50000;
    cpu->reg[5].u32 = 0x22222222;
    cpu->reg[6].u32 = 0x11111111;
    cpu->reg[7].u32 = 0x33333333;
    initialEsp = cpu->reg[4].u32;
    for (U32 i = 0; i < producerLen; ++i) testPushCode8(producer[i]);
    testPushCode8(0xff); testPushCode8(0xe0); // jmp eax: distinct consumer
    padCrossBlockFlagsCodeToTarget();
    testPushCode8(0x0f); testPushCode8((U8)(jcc + 0x20)); testPushCode8(0xc3); // setcc bl
    testPushCode8(0x0f); testPushCode8((U8)(jcc - 0x30)); testPushCode8(0xee); // cmovcc ebp,esi
    testPushCode8(jcc); testPushCode8(0x07); // jcc true-path
    testPushCode8(0xbf); testPushCode32(0x22222222);
    testPushCode8(0xeb); testPushCode8(0x05);
    testPushCode8(0xbf); testPushCode32(0x11111111);
    testRunCPU();

    U32 expectedBool = expectedCondition ? 1 : 0;
    if (cpu->reg[0].u32 != CROSS_BLOCK_FLAGS_TARGET_OFFSET ||
        cpu->reg[1].u32 != ecx || cpu->reg[2].u32 != edx ||
        cpu->reg[3].u32 != (0xa5a50000 | expectedBool) ||
        cpu->reg[4].u32 != initialEsp ||
        cpu->reg[5].u32 != (expectedCondition ? 0x11111111 : 0x22222222) ||
        cpu->reg[6].u32 != 0x11111111 ||
        cpu->reg[7].u32 != (expectedCondition ? 0x11111111 : 0x22222222)) {
        testFail("%s all-GP surface", name);
    }
    if (cpu->lazyFlagType != expectedType || cpu->result.u32 != expectedResult) {
        testFail("%s lazy tuple actualType=%u expectedType=%u actualResult=%08x expectedResult=%08x",
                 name, (U32)cpu->lazyFlagType, (U32)expectedType,
                 cpu->result.u32, expectedResult);
    }
    if ((cpu->getCF() != 0) || (cpu->getOF() != 0) ||
        ((cpu->getZF() != 0) != (expectedResult == 0)) ||
        ((cpu->getSF() != 0) != ((expectedResult & 0x80000000u) != 0))) {
        testFail("%s logical flags", name);
    }
    if (cpu->wasmJitRuntimeLogicComposite32FixtureCandidate[conditionIndex] != candidateBefore + (expectCandidate ? 3 : 0) ||
        cpu->wasmJitRuntimeLogicComposite32FixtureFallback[conditionIndex] != fallbackBefore + (expectCandidate ? 0 : 3)) {
        testFail("%s candidate/fallback witness", name);
    }
    verifyCrossBlockJitEntries(name);
}

static void runRuntimeLogicComposite32Negative(const U8* producer, U32 producerLen,
                                               U32 ecx, U32 edx, U8 jcc, bool expectedCondition,
                                               const char* name) {
    // Negative rows use JIT-vs-interpreter observable results but cannot use
    // the b32 logical formula as their oracle.  They still require all three
    // surfaces to reach the retained helper (witness asserted after run).
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    const U32 conditionIndex = runtimeLogicComposite32ConditionIndex(jcc);
    if (conditionIndex >= 6) { testFail("%s fixture condition index", name); return; }
    U32 candidateBefore = cpu->wasmJitRuntimeLogicComposite32FixtureCandidate[conditionIndex];
    U32 fallbackBefore = cpu->wasmJitRuntimeLogicComposite32FixtureFallback[conditionIndex];
    testNewInstruction(0);
    cpu->reg[0].u32 = CROSS_BLOCK_FLAGS_TARGET_OFFSET;
    cpu->reg[1].u32 = ecx; cpu->reg[2].u32 = edx;
    cpu->reg[3].u32 = 0xa5a50000; cpu->reg[5].u32 = 0x22222222;
    cpu->reg[6].u32 = 0x11111111; cpu->reg[7].u32 = 0x33333333;
    for (U32 i = 0; i < producerLen; ++i) testPushCode8(producer[i]);
    testPushCode8(0xff); testPushCode8(0xe0);
    padCrossBlockFlagsCodeToTarget();
    testPushCode8(0x0f); testPushCode8((U8)(jcc + 0x20)); testPushCode8(0xc3);
    testPushCode8(0x0f); testPushCode8((U8)(jcc - 0x30)); testPushCode8(0xee);
    testPushCode8(jcc); testPushCode8(0x07);
    testPushCode8(0xbf); testPushCode32(0x22222222);
    testPushCode8(0xeb); testPushCode8(0x05);
    testPushCode8(0xbf); testPushCode32(0x11111111);
    testRunCPU();
    U32 expectedBool = expectedCondition ? 1 : 0;
    if (cpu->reg[3].u32 != (0xa5a50000 | expectedBool) ||
        cpu->reg[5].u32 != (expectedCondition ? 0x11111111 : 0x22222222) ||
        cpu->reg[7].u32 != (expectedCondition ? 0x11111111 : 0x22222222)) {
        testFail("%s helper result surfaces", name);
    }
    if (cpu->wasmJitRuntimeLogicComposite32FixtureCandidate[conditionIndex] != candidateBefore ||
        cpu->wasmJitRuntimeLogicComposite32FixtureFallback[conditionIndex] != fallbackBefore + 3) {
        testFail("%s retained-helper witness", name);
    }
    verifyCrossBlockJitEntries(name);
}

void testRuntimeLogicComposite32AcrossIndirectJitBlockBoundary() {
    static const U8 orCxDx[]   = {0x09, 0xd1};
    static const U8 andCxDx[]  = {0x21, 0xd1};
    static const U8 xorCxDx[]  = {0x31, 0xd1};
    static const U8 testCxDx[] = {0x85, 0xd1};
    static const U8 orClDl[]   = {0x08, 0xd1};
    static const U8 orCwDw[]   = {0x66, 0x09, 0xd1};
    static const U8 pushfPopf[] = {0x9c, 0x9d};
    static const U8 subCxDx[] = {0x29, 0xd1};
    static const U8 shlCx[] = {0xd1, 0xe1};
    static const U8 orThenRol[] = {0x09, 0xd1, 0xd1, 0xc1};
    const U8 conditions[] = {0x76, 0x77, 0x7c, 0x7d, 0x7e, 0x7f};
    const U32 values[] = {0, 1, 0x80000000u};
    struct Producer { const U8* code; U32 len; LazyFlagType type; const char* name; U32 mask; };
    const Producer producers[] = {
        {orCxDx, sizeof(orCxDx), FLAGS_OR32, "or", 0},
        {andCxDx, sizeof(andCxDx), FLAGS_AND32, "and", 0xffffffffu},
        {xorCxDx, sizeof(xorCxDx), FLAGS_XOR32, "xor", 0},
        // The decoded TEST opcode's cross-block dynamic path canonicalizes to
        // FLAGS_AND32 while retaining the same result = left & right tuple.
        {testCxDx, sizeof(testCxDx), FLAGS_AND32, "test", 0xffffffffu},
    };
    for (U32 i = 0; i < 6; ++i) {
        testContext().cpu->wasmJitRuntimeLogicComposite32FixtureCandidate[i] = 0;
        testContext().cpu->wasmJitRuntimeLogicComposite32FixtureFallback[i] = 0;
    }
    for (const Producer& producer : producers) for (U32 value : values) for (U8 jcc : conditions) {
        char name[96];
        snprintf(name, sizeof(name), "runtime-logic32 %s value=%08x cc=%02x", producer.name, value, jcc);
        runRuntimeLogicComposite32Case(producer.code, producer.len, value, producer.mask, value,
                                       producer.type, jcc, runtimeLogicComposite32Expected(jcc, value), true, name);
    }
    // Negative fallback rows: b8/b16 (upper result bytes intentionally stale),
    // FLAGS_NONE, arithmetic, shift, and CFOF with a logical predecessor.
    runRuntimeLogicComposite32Negative(orClDl, sizeof(orClDl), 0x7f000080, 0, 0x7c, true, "runtime-logic32 fallback b8");
    runRuntimeLogicComposite32Negative(orCwDw, sizeof(orCwDw), 0x7f008000, 0, 0x7c, true, "runtime-logic32 fallback b16");
    runRuntimeLogicComposite32Negative(pushfPopf, sizeof(pushfPopf), 0, 0, 0x76, false, "runtime-logic32 fallback none");
    runRuntimeLogicComposite32Negative(subCxDx, sizeof(subCxDx), 0, 1, 0x76, true, "runtime-logic32 fallback sub");
    runRuntimeLogicComposite32Negative(shlCx, sizeof(shlCx), 0x80000001u, 0, 0x76, true, "runtime-logic32 fallback shl");
    runRuntimeLogicComposite32Negative(orThenRol, sizeof(orThenRol), 0x80000000u, 0, 0x76, true, "runtime-logic32 fallback cfof-logical-prev");
}
#endif

void testJitOverlappingDirectJumpTarget() {
#ifdef BOXEDWINE_JIT
    CPU* cpu = testContext().cpu;

    testNewInstruction(0);
    cpu->reg[0].u32 = 0x12340000;

    testPushCode8(0x39); // cmp eax,eax
    testPushCode8(0xc0);
    testPushCode8(0x74); // jz +1 into the immediate bytes of the fallthrough mov
    testPushCode8(0x01);
    testPushCode8(0xb8); // fallthrough: mov eax,0x02eb07b0
    testPushCode8(0xb0); // target: mov al,7
    testPushCode8(0x07);
    testPushCode8(0xeb); // jmp over mov al,3
    testPushCode8(0x02);
    testPushCode8(0xb0); // fallthrough stream: mov al,3
    testPushCode8(0x03);
    testRunCPU();

    if ((cpu->reg[0].u32 & 0xff) != 7) {
        testFail("jit overlapping direct jump target");
    }
#endif
}

void testNativeJitRunCountWraps() {
#if defined(BOXEDWINE_JIT) && !defined(BOXEDWINE_WASM_JIT)
    DecodedOp op;
    op.runCount = 0xff;
    op.pfn = testJitRunCountCallback;

    firstDynamicOp(testContext().cpu, &op);

    if (op.runCount != 0) {
        testFail("native JIT runCount wraps after a failed compile threshold");
    }
#endif
}

bool testShouldRunRegister(bool fast, int reg) {
    if (!fast) {
        return true;
    }
    static const int fastRegs[] = {0, 1, 3, 4, 5, 7};
    return intInList(reg, fastRegs, sizeof(fastRegs) / sizeof(fastRegs[0]));
}

bool testShouldRunRegisterPair(bool fast, int dst, int src) {
    if (!fast) {
        return true;
    }
    static const int fastPairs[][2] = {
        {0, 0},
        {0, 1},
        {1, 0},
        {2, 3},
        {3, 2},
        {4, 5},
        {5, 4},
        {6, 7},
        {7, 6},
        {4, 0},
        {0, 4}
    };
    for (size_t i = 0; i < sizeof(fastPairs) / sizeof(fastPairs[0]); ++i) {
        if (fastPairs[i][0] == dst && fastPairs[i][1] == src) {
            return true;
        }
    }
    return false;
}

bool testShouldRunMemoryBase(bool fast, int base) {
    if (!fast) {
        return true;
    }
    static const int fastBases[] = {0, 3, 4, 5, 6};
    return intInList(base, fastBases, sizeof(fastBases) / sizeof(fastBases[0]));
}

bool testShouldRunMemoryBaseDisplacement(bool fast, int base, int displacementIndex) {
    if (!fast) {
        return true;
    }
    if (!testShouldRunMemoryBase(true, base)) {
        return false;
    }
    if (displacementIndex == 0) {
        return base == 0 || base == 4;
    }
    if (displacementIndex == 1) {
        return base == 3 || base == 5;
    }
    return base == 6;
}

bool testShouldRunMemorySib(bool fast, int base, int index, int shift) {
    if (!fast) {
        return true;
    }
    static const int fastSibCases[][3] = {
        {0, 1, 0},
        {0, 6, 2},
        {1, 3, 2},
        {2, 7, 0},
        {3, 2, 1},
        {4, 1, 3},
        {4, 7, 2},
        {5, 0, 3},
        {5, 4, 0},
        {5, 6, 1},
        {6, 0, 3},
        {6, 5, 0},
        {7, 3, 1}
    };
    for (size_t i = 0; i < sizeof(fastSibCases) / sizeof(fastSibCases[0]); ++i) {
        if (fastSibCases[i][0] == base && fastSibCases[i][1] == index && fastSibCases[i][2] == shift) {
            return true;
        }
    }
    return false;
}

bool testRunRegister(int reg) {
    return testShouldRunRegister(testIsFastMode(), reg);
}

bool testRunRegisterPair(int dst, int src) {
    return testShouldRunRegisterPair(testIsFastMode(), dst, src);
}

bool testRunMemoryBase(int base) {
    return testShouldRunMemoryBase(testIsFastMode(), base);
}

bool testRunMemoryBaseDisplacement(int base, int displacementIndex) {
    return testShouldRunMemoryBaseDisplacement(testIsFastMode(), base, displacementIndex);
}

bool testRunMemorySib(int base, int index, int shift) {
    return testShouldRunMemorySib(testIsFastMode(), base, index, shift);
}

void testFastModeSelectionHelpers() {
    if (!testShouldRunRegisterPair(false, 2, 6) ||
            !testShouldRunMemorySib(false, 2, 4, 3) ||
            !testShouldRunMemoryBaseDisplacement(false, 7, 2)) {
        testFail("full mode should keep exhaustive combinations");
    }
    if (!testShouldRunRegisterPair(true, 4, 0) ||
            !testShouldRunRegisterPair(true, 6, 7) ||
            testShouldRunRegisterPair(true, 2, 6)) {
        testFail("fast mode register pair sampling");
    }
    if (!testShouldRunMemoryBaseDisplacement(true, 0, 0) ||
            !testShouldRunMemoryBaseDisplacement(true, 5, 1) ||
            !testShouldRunMemoryBaseDisplacement(true, 6, 2) ||
            testShouldRunMemoryBaseDisplacement(true, 7, 2)) {
        testFail("fast mode base/displacement sampling");
    }
    if (!testShouldRunMemorySib(true, 4, 7, 2) ||
            !testShouldRunMemorySib(true, 5, 0, 3) ||
            testShouldRunMemorySib(true, 2, 4, 3)) {
        testFail("fast mode sib sampling");
    }
}

#ifdef BOXEDWINE_HOST_EXCEPTIONS
void platformInitExceptionHandling();
#endif

void testRunParallel(const TestEntry* entries, size_t entryCount, U32 workerCount) {
    if (!entryCount) {
        return;
    }

    ensureParallelContexts(workerCount);

    std::atomic<size_t> nextEntry(0);
    std::mutex printMutex;

    auto runEntry = [&](U32 contextIndex, size_t entryIndex) {
        bindParallelContext(contextIndex);
        TestContext& context = *currentContext;
        runningParallelTest = true;

        context.failed = false;
        context.failures.clear();
        entries[entryIndex].function();
        {
            std::lock_guard<std::mutex> lock(printMutex);
            printf("%s", entries[entryIndex].name);
            printf(" ... ");
            if (context.failed) {
                printf("FAILED\n");
                for (const std::string& failure : context.failures) {
                    printf("  %s\n", failure.c_str());
                }
                failed("%s", entries[entryIndex].name);
            } else {
                printf("OK\n");
            }
            fflush(stdout);
        }
    };

    if (workerCount == 1) {
#if defined(BOXEDWINE_JIT_ARMV8)
        ensureArmV8HardwareTSOForThread();
#endif
        for (size_t i = 0; i < entryCount; ++i) {
            runEntry(0, i);
        }
        runningParallelTest = false;
        currentContext = nullptr;
        return;
    }

    std::vector<std::thread> workers;

    for (U32 i = 0; i < workerCount; ++i) {
        workers.push_back(std::thread([&, i]() {
#ifdef BOXEDWINE_HOST_EXCEPTIONS
            platformInitExceptionHandling();
#endif
#if defined(BOXEDWINE_JIT_ARMV8)
            ensureArmV8HardwareTSOForThread();
#endif
            while (true) {
                size_t entryIndex = nextEntry.fetch_add(1);
                if (entryIndex >= entryCount) {
                    break;
                }
                runEntry(i, entryIndex);
            }
            runningParallelTest = false;
            currentContext = nullptr;
        }));
    }

    for (auto& worker : workers) {
        worker.join();
    }
}

#endif
