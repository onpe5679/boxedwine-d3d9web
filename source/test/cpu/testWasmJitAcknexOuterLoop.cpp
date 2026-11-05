/*
 *  Copyright (C) 2012-2026 The BoxedWine Team
 *
 *  Differential/fault-boundary fixture for the isolated Acknex +49650
 *  bounded outer-loop fusion.
 */

#include "boxedwine.h"
#include "testCPU.h"
#include "testWasmJitAcknexOuterLoop.h"

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT) && \
        defined(BOXEDWINE_MULTI_THREADED) && defined(__EMSCRIPTEN__) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE)

#include "../../emulation/cpu/normal/normalCPU.h"
#include "../../emulation/cpu/wasm/wasmJitAcknexTransformIsland.h"
#include <array>
#include <cstring>
#include <vector>

namespace {

extern "C" U32 wasm_jit_get_acknex_outer_fixture_normalized_hash();

constexpr U32 PREFERRED_BASE=0x10000000u;
constexpr U32 HISTORICAL_BASE=0x7f360000u;
constexpr U32 CURRENT_BASE=0x7f3a0000u;
U32 activeBase=PREFERRED_BASE;
constexpr U32 RETURN_EIP=0x10104f00u;
constexpr U32 DATA=0x20000000u;
constexpr U32 STACK=0x30000000u;
constexpr U32 ENGINE=DATA+0x0800u;
constexpr U32 OUTER_STATE=DATA+0x1800u;
constexpr U32 MATRIX_RECORD=DATA+0x4000u;
constexpr U32 SOURCE=DATA+0x10000u;
constexpr U32 DESTINATION=DATA+0x30000u;
constexpr U32 OUTER_ESP=STACK+0x1800u;
constexpr U32 DATA_BYTES=0x80000u;
constexpr U32 STACK_CAPTURE_START=STACK+0x1500u;
constexpr U32 STACK_CAPTURE_BYTES=0x700u;

struct Snapshot {
    U32 reg[8]{};
    U32 flags=0;
    U32 eip=0;
    U32 lazyType=0;
    U32 lazyTypePrev=0;
    U32 lazyDst=0;
    U32 lazySrc=0;
    U32 lazyResult=0;
    U32 mxcsr=0;
    U32 fpuDirtyFlags=0;
    U64 xmm[8][2]{};
    std::array<U8,sizeof(FPU)> fpu{};
    std::vector<U8> data;
    std::vector<U8> stack;
};

U32 randomNext(U32& value) {
    value^=value<<13; value^=value>>17; value^=value<<5;
    return value;
}

void writeFinite(KMemory* memory,U32 address,U32 value) {
    const U32 sign=value&0x80000000u;
    const U32 exponent=(((value>>24)%40u)+100u)<<23;
    memory->writed(address,sign|exponent|(value&0x007fffffu));
}

U32 outerEntry() { return activeBase+0x00049650u; }
U32 outerEpilogue() { return activeBase+0x0004966du; }

void mapFixture(U32 moduleBase) {
    TestContext& context=testContext();
    activeBase=moduleBase;
    wasmJitAcknexTransformIslandFixtureSetModuleBase(activeBase);
    struct Map { U32 address; U32 pages; U32 prot; };
    const Map maps[]={
        {activeBase+0x00046000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00049000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00084000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00104000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00252000u,1,K_PROT_READ|K_PROT_WRITE},
        {activeBase+0x002ff000u,1,K_PROT_READ|K_PROT_WRITE},
        {activeBase+0x00304000u,1,K_PROT_READ|K_PROT_WRITE},
        {DATA,DATA_BYTES/K_PAGE_SIZE,K_PROT_READ|K_PROT_WRITE},
        {STACK,2,K_PROT_READ|K_PROT_WRITE},
    };
    for (const Map& map:maps)
        context.memory->mmap(context.thread,map.address,map.pages*K_PAGE_SIZE,map.prot,
            K_MAP_FIXED|K_MAP_PRIVATE,-1,0);
    wasmJitAcknexTransformIslandFixtureInstallCode(context.memory);
    context.memory->writeb(RETURN_EIP,0xcd);
    context.memory->writeb(RETURN_EIP+1,0x97);
}

void prepare(U32 count,U32 mismatchElement=0xffffffffu) {
    TestContext& context=testContext();
    CPU* cpu=context.cpu;
    testNewInstruction(CF|AF|OF|DF);
    // TestContext is reused across the suite and earlier segment fixtures may
    // leave non-flat bases behind.  The real admitted outer helper requires a
    // flat CS/DS/SS contract, and the NormalCPU oracle must execute the same
    // linear addresses.  Without this reset 0x10049650/0x200xxxxx are decoded
    // as 0xE0049650/0xE200xxxx and the fixture faults before exercising AOL.
    cpu->seg[CS].address=0; cpu->seg[DS].address=0; cpu->seg[ES].address=0;
    cpu->seg[FS].address=0; cpu->seg[GS].address=0; cpu->seg[SS].address=0;
    wasmJitAcknexTransformIslandFixtureInstallCode(context.memory);
    wasmJitAcknexOuterLoopFixtureSetStop(0xffffffffu,WasmJitAcknexOuterFixtureStop::None);
    context.memory->memset(DATA,0,DATA_BYTES);
    context.memory->memset(STACK,0,K_PAGE_SIZE*2);
    context.memory->writeb(RETURN_EIP,0xcd);
    context.memory->writeb(RETURN_EIP+1,0x97);

    U32 random=0x49650000u^count^(mismatchElement*17u);
    const U32 records=count+2u;
    for (U32 i=0;i<records;++i) {
        const U32 source=SOURCE+i*0x2cu;
        for (U32 lane=0;lane<6;++lane)
            writeFinite(context.memory,source+lane*4u,randomNext(random));
        context.memory->writed(source+0x20,(i==mismatchElement) ? 1u : 0u);
        for (U32 dword=0;dword<11;++dword)
            context.memory->writed(DESTINATION+i*0x2cu+dword*4u,randomNext(random));
    }
    for (U32 record=0;record<2;++record) {
        const U32 matrixRecord=MATRIX_RECORD+record*0x94u;
        context.memory->writed(matrixRecord+4,record ? 2u : 3u);
        for (U32 lane=0;lane<16;++lane)
            writeFinite(context.memory,matrixRecord+0x14+lane*4u,randomNext(random));
    }
    context.memory->writed(activeBase+0x00304c40u,ENGINE);
    context.memory->writed(ENGINE+0x4f8,MATRIX_RECORD);
    context.memory->writed(activeBase+0x002ff4ccu,OUTER_STATE);
    context.memory->writed(OUTER_STATE+0x28,count);
    context.memory->writed(activeBase+0x00252150u,activeBase+0x001042a3u);
    context.memory->writed(activeBase+0x00252140u,activeBase+0x001041d7u);

    for (U32 i=0;i<8;++i) {
        cpu->reg[i].u32=0x71000000u+i*0x01010101u;
        cpu->xmm[i].pi.u64[0]=0x1111111100000000ull^((U64)count<<20)^i;
        cpu->xmm[i].pi.u64[1]=0xeeeeeeeeffffffffull^((U64)count<<28)^(i*17u);
    }
    const U32 savedEbx=cpu->reg[regBX].u32;
    const U32 savedEsi=cpu->reg[regSI].u32;
    const U32 savedEdi=cpu->reg[regDI].u32;
    cpu->reg[regBX].u32=SOURCE;
    cpu->reg[regSI].u32=0;
    cpu->reg[regDI].u32=DESTINATION;
    cpu->reg[regSP].u32=OUTER_ESP;
    context.memory->writed(OUTER_ESP,savedEdi);
    context.memory->writed(OUTER_ESP+4,savedEsi);
    context.memory->writed(OUTER_ESP+8,savedEbx);
    context.memory->writed(OUTER_ESP+12,RETURN_EIP);
    cpu->mxcsr=0x1f80u;
    cpu->stackMask=0xffffffffu;
    cpu->stackNotMask=0;
    cpu->yield=false;
    cpu->wasmJitBailout=0;
    cpu->fpuDirtyFlags=0;
    cpu->fpu.reset();
    cpu->eip.u32=outerEntry();
    cpu->nextOp=nullptr;
    context.memory->clearOpCache();
}

bool runGuest(const char* name,bool forceNormal) {
    TestContext& context=testContext();
    CPU* cpu=context.cpu;
    cpu->nextOp=cpu->getNextOp();
    bool exception=false;
    try {
        for (U32 steps=0;steps<500000 && (!cpu->nextOp || cpu->nextOp->inst!=TestEnd);++steps) {
            if (forceNormal) {
                OpCallback callback=NormalCPU::getFunctionForOp(cpu->nextOp);
                if (!callback) break;
                callback(cpu,cpu->nextOp);
            } else {
                cpu->run();
            }
        }
    } catch (...) { exception=true; }
    if (exception || !cpu->nextOp || cpu->nextOp->inst!=TestEnd) {
        testFail("Acknex outer %s did not reach TestEnd",name);
        return false;
    }
    return true;
}

Snapshot capture() {
    TestContext& context=testContext();
    Snapshot result;
    result.lazyType=(U32)context.cpu->lazyFlagType;
    result.lazyTypePrev=(U32)context.cpu->lazyFlagTypePrev;
    result.lazyDst=context.cpu->dst.u32;
    result.lazySrc=context.cpu->src.u32;
    result.lazyResult=context.cpu->result.u32;
    result.mxcsr=context.cpu->mxcsr;
    result.fpuDirtyFlags=context.cpu->fpuDirtyFlags;
    std::memcpy(result.fpu.data(),&context.cpu->fpu,sizeof(FPU));
    context.cpu->fillFlags();
    for (U32 i=0;i<8;++i) {
        result.reg[i]=context.cpu->reg[i].u32;
        result.xmm[i][0]=context.cpu->xmm[i].pi.u64[0];
        result.xmm[i][1]=context.cpu->xmm[i].pi.u64[1];
    }
    result.flags=context.cpu->flags;
    result.eip=context.cpu->eip.u32;
    result.data.resize(DATA_BYTES);
    result.stack.resize(STACK_CAPTURE_BYTES);
    context.memory->memcpy(result.data.data(),DATA,DATA_BYTES);
    context.memory->memcpy(result.stack.data(),STACK_CAPTURE_START,STACK_CAPTURE_BYTES);
    return result;
}

void compare(const char* name,const Snapshot& reference,const Snapshot& candidate) {
    for (U32 i=0;i<8;++i) {
        if (reference.reg[i]!=candidate.reg[i])
            testFail("Acknex outer %s GPR%u ref=%08x fused=%08x",name,i,reference.reg[i],candidate.reg[i]);
        if (reference.xmm[i][0]!=candidate.xmm[i][0] ||
                reference.xmm[i][1]!=candidate.xmm[i][1])
            testFail("Acknex outer %s XMM%u mismatch",name,i);
    }
    if (reference.flags!=candidate.flags || reference.eip!=candidate.eip)
        testFail("Acknex outer %s flags/eip ref=%08x/%08x fused=%08x/%08x",name,
            reference.flags,reference.eip,candidate.flags,candidate.eip);
    if (reference.lazyType!=candidate.lazyType || reference.lazyTypePrev!=candidate.lazyTypePrev ||
            reference.lazyDst!=candidate.lazyDst || reference.lazySrc!=candidate.lazySrc ||
            reference.lazyResult!=candidate.lazyResult)
        testFail("Acknex outer %s lazy tuple mismatch",name);
    if (reference.mxcsr!=candidate.mxcsr || reference.fpuDirtyFlags!=candidate.fpuDirtyFlags ||
            reference.fpu!=candidate.fpu)
        testFail("Acknex outer %s MXCSR/x87 mismatch",name);
    if (reference.data!=candidate.data)
        testFail("Acknex outer %s data memory mismatch",name);
    if (reference.stack!=candidate.stack)
        testFail("Acknex outer %s residual stack mismatch",name);
}

std::array<U64,WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT> counterSnapshot() {
    std::array<U64,WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT> result{};
    wasm_jit_copy_acknex_transform_island_snapshot(result.data(),sizeof(result));
    return result;
}

void finishOriginalFromCurrent(const char* name) {
    testContext().cpu->yield=false;
    testContext().cpu->wasmJitBailout=0;
    testContext().thread->terminating=false;
    testContext().thread->process->terminated=false;
    testContext().cpu->nextOp=nullptr;
    runGuest(name,true);
}

void runCompleteDifferential(U32 count,const char* name) {
    wasmJitAcknexTransformIslandFixtureSetIdentity(false);
    prepare(count);
    if (!runGuest(name,true)) return;
    const Snapshot reference=capture();

    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(count);
    wasm_jit_reset_acknex_transform_island_counters();
    U32 calls=0;
    while (testContext().cpu->eip.u32==outerEntry() && calls<128) {
        const U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
        if (outcome!=(U32)WasmJitAcknexOuterOutcome::PartialResume) {
            testFail("Acknex outer %s outcome=%u",name,outcome);
            return;
        }
        ++calls;
    }
    if (testContext().cpu->eip.u32!=outerEpilogue()) {
        testFail("Acknex outer %s did not reach epilogue",name);
        return;
    }
    finishOriginalFromCurrent(name);
    compare(name,reference,capture());
    const U32 expectedElements=count ? count : 1u;
    const U32 expectedCalls=(expectedElements+WASM_JIT_ACKNEX_OUTER_CHUNK_ELEMENTS-1u)/
        WASM_JIT_ACKNEX_OUTER_CHUNK_ELEMENTS;
    const auto counters=counterSnapshot();
    if (calls!=expectedCalls ||
            counters[(U32)WasmJitAcknexTransformCounter::OuterElements]!=expectedElements ||
            counters[(U32)WasmJitAcknexTransformCounter::OuterChunks]!=expectedCalls ||
            counters[(U32)WasmJitAcknexTransformCounter::OuterCompletedLoops]!=1 ||
            counters[(U32)WasmJitAcknexTransformCounter::OuterChunkLimitStops]!=(expectedCalls-1u))
        testFail("Acknex outer %s chunk counters calls=%u expected=%u elements=%llu",name,
            calls,expectedCalls,(unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::OuterElements]);
}

void runPartialFallbackCase() {
    constexpr U32 count=65;
    constexpr U32 rejectAt=17;
    wasmJitAcknexTransformIslandFixtureSetIdentity(false);
    prepare(count,rejectAt);
    runGuest("partial-type-oracle",true);
    const Snapshot reference=capture();

    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(count,rejectAt);
    wasm_jit_reset_acknex_transform_island_counters();
    const U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    if (outcome!=(U32)WasmJitAcknexOuterOutcome::PartialResume ||
            testContext().cpu->reg[regSI].u32!=rejectAt ||
            testContext().cpu->eip.u32!=outerEntry())
        testFail("Acknex outer partial type resume outcome=%u esi=%u eip=%08x",outcome,
            testContext().cpu->reg[regSI].u32,testContext().cpu->eip.u32);
    finishOriginalFromCurrent("partial-type-resume");
    compare("partial-type-resume",reference,capture());
}

void runSafeFallbackCase() {
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(1,0);
    const Snapshot before=capture();
    // capture materializes flags, so re-prepare before the helper and compare a
    // second capture of the unchanged state.
    prepare(1,0);
    wasm_jit_reset_acknex_transform_island_counters();
    const U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    if (outcome!=(U32)WasmJitAcknexOuterOutcome::SafeFallback)
        testFail("Acknex outer first-element fallback outcome=%u",outcome);
    compare("safe-fallback-no-progress",before,capture());
}

void runInjectedStopCase(WasmJitAcknexOuterFixtureStop stop,
        WasmJitAcknexOuterOutcome expected,const char* name) {
    constexpr U32 count=65;
    constexpr U32 stopAfter=17;
    wasmJitAcknexTransformIslandFixtureSetIdentity(false);
    prepare(count);
    runGuest(name,true);
    const Snapshot reference=capture();

    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(count);
    wasmJitAcknexOuterLoopFixtureSetStop(stopAfter,stop);
    const U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    if (outcome!=(U32)expected || testContext().cpu->reg[regSI].u32!=stopAfter ||
            testContext().cpu->eip.u32!=outerEntry())
        testFail("Acknex outer %s stop outcome=%u esi=%u",name,outcome,
            testContext().cpu->reg[regSI].u32);
    wasmJitAcknexOuterLoopFixtureSetStop(0xffffffffu,WasmJitAcknexOuterFixtureStop::None);
    finishOriginalFromCurrent(name);
    compare(name,reference,capture());
}

void runStaleCodeCase() {
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(1);
    const U8 original=testContext().memory->readb(outerEntry());
    testContext().memory->writeb(outerEntry(),original^1u);
    const U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    if (outcome!=(U32)WasmJitAcknexOuterOutcome::StaleCodeExit ||
            testContext().cpu->eip.u32!=outerEntry() || !testContext().cpu->wasmJitBailout)
        testFail("Acknex outer stale-code outcome=%u eip=%08x bailout=%u",outcome,
            testContext().cpu->eip.u32,testContext().cpu->wasmJitBailout);
    wasmJitAcknexTransformIslandFixtureInstallCode(testContext().memory);
}

void runSignedJlCases() {
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(1);
    testContext().cpu->reg[regSI].u32=0x7ffffffeu;
    testContext().memory->writed(OUTER_STATE+0x28,0x80000000u);
    U32 outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    if (outcome!=(U32)WasmJitAcknexOuterOutcome::PartialResume ||
            testContext().cpu->eip.u32!=outerEpilogue() ||
            testContext().cpu->lazyFlagType!=FLAGS_CMP32)
        testFail("Acknex outer signed-JL false boundary outcome=%u eip=%08x",outcome,
            testContext().cpu->eip.u32);

    prepare(1);
    testContext().cpu->reg[regSI].u32=0x7fffffffu;
    testContext().memory->writed(OUTER_STATE+0x28,0x7fffffffu);
    wasmJitAcknexOuterLoopFixtureSetStop(1,WasmJitAcknexOuterFixtureStop::Yield);
    outcome=wasmJitAcknexOuterLoopRunForTest(testContext().cpu);
    wasmJitAcknexOuterLoopFixtureSetStop(0xffffffffu,WasmJitAcknexOuterFixtureStop::None);
    if (outcome!=(U32)WasmJitAcknexOuterOutcome::PartialResume ||
            testContext().cpu->eip.u32!=outerEntry() ||
            testContext().cpu->reg[regSI].u32!=0x80000000u)
        testFail("Acknex outer signed-JL true boundary outcome=%u eip=%08x esi=%08x",outcome,
            testContext().cpu->eip.u32,testContext().cpu->reg[regSI].u32);
}

void runMprotectEpochCase() {
    KProcess* process=testContext().thread->process.get();
    const U64 before=process->getAcknexTransformMappingGeneration();
    testContext().memory->mprotect(testContext().thread,DATA,K_PAGE_SIZE,K_PROT_READ);
    const U64 after=process->getAcknexTransformMappingGeneration();
    testContext().memory->mprotect(testContext().thread,DATA,K_PAGE_SIZE,
        K_PROT_READ|K_PROT_WRITE);
    if (after<=before)
        testFail("Acknex outer mprotect did not advance mapping epoch");
}

void runGeneratedAdmissionCase(const char* name) {
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(1);
    wasm_jit_reset_acknex_transform_island_counters();
    runGuest(name,false);
    const auto counters=counterSnapshot();
    if (!counters[(U32)WasmJitAcknexTransformCounter::OuterCandidateAccepted] ||
            !counters[(U32)WasmJitAcknexTransformCounter::OuterHelperEntries] ||
            !counters[(U32)WasmJitAcknexTransformCounter::OuterCompletedLoops] ||
            counters[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch])
        testFail("Acknex outer generated admission accepted=%llu helper=%llu complete=%llu hash=%llu normalized=%08x", 
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::OuterCandidateAccepted],
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::OuterHelperEntries],
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::OuterCompletedLoops],
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch],
            wasm_jit_get_acknex_outer_fixture_normalized_hash());
}

} // namespace

void testWasmJitAcknexOuterLoop() {
    mapFixture(PREFERRED_BASE);
    runCompleteDifferential(0,"count-0-entry-still-executes-once");
    runCompleteDifferential(1,"count-1");
    runCompleteDifferential(63,"count-63");
    runCompleteDifferential(64,"count-64");
    runCompleteDifferential(65,"count-65");
    runCompleteDifferential(1964,"count-1964-observed-aggregate");
    runPartialFallbackCase();
    runSafeFallbackCase();
    runInjectedStopCase(WasmJitAcknexOuterFixtureStop::Yield,
        WasmJitAcknexOuterOutcome::PartialResume,"yield-after-17");
    runInjectedStopCase(WasmJitAcknexOuterFixtureStop::Bailout,
        WasmJitAcknexOuterOutcome::StaleCodeExit,"bailout-after-17");
    runInjectedStopCase(WasmJitAcknexOuterFixtureStop::MappingEpoch,
        WasmJitAcknexOuterOutcome::StaleCodeExit,"epoch-after-17");
    runInjectedStopCase(WasmJitAcknexOuterFixtureStop::Termination,
        WasmJitAcknexOuterOutcome::PartialResume,"termination-after-17");
    runStaleCodeCase();
    runSignedJlCases();
    runMprotectEpochCase();
    runGeneratedAdmissionCase("generated-preferred-base");
    mapFixture(HISTORICAL_BASE);
    runGeneratedAdmissionCase("generated-historical-base");
    mapFixture(CURRENT_BASE);
    runGeneratedAdmissionCase("generated-current-base");
    printf("Acknex +49650 batch64 outcome/flags/ESP/EIP/fallback/epoch fixture OK\n");
}

#else
void testWasmJitAcknexOuterLoop() {}
#endif
