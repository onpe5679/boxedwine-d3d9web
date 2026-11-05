/*
 *  Copyright (C) 2012-2026 The BoxedWine Team
 *
 *  Differential and fail-closed fixture for the exact Acknex transform island.
 */

#include "boxedwine.h"
#include "testCPU.h"
#include "testWasmJitAcknexTransformIsland.h"

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT) && \
        defined(BOXEDWINE_MULTI_THREADED) && defined(__EMSCRIPTEN__) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND) && \
        defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)

#include "../../emulation/cpu/normal/normalCPU.h"
#include "../../emulation/cpu/wasm/wasmJitAcknexTransformIsland.h"
#include <array>
#include <cstring>
#include <functional>

namespace {

constexpr U32 PREFERRED_BASE = 0x10000000u;
U32 activeBase = PREFERRED_BASE;
constexpr U32 RETURN_EIP = 0x10104f00u;
constexpr U32 DATA = 0x20000000u;
constexpr U32 STACK = 0x30000000u;
constexpr U32 ENGINE = DATA + 0x0800u;
constexpr U32 SOURCE_DEFAULT = DATA + 0x3000u;
constexpr U32 DEST_DEFAULT = DATA + 0x5000u;
constexpr U32 MATRIX_RECORD_ALIGNED = DATA + 0x1fecu; // +0x14 is 16-byte aligned
constexpr U32 MATRIX_RECORD_MOD4 = DATA + 0x2200u;    // +0x14 is 4 mod 16
constexpr U32 SNAPSHOT_BYTES = 0x7000u;

struct CaseSpec {
    U32 source;
    U32 destination;
    U32 matrixRecord;
    U32 type;
    U32 seed;
    const char* name;
};

struct Snapshot {
    U32 reg[8];
    U32 flags;
    U32 eip;
    U32 lazyType;
    U32 lazyDst;
    U32 lazySrc;
    U32 lazyResult;
    U32 mxcsr;
    U64 xmm[8][2];
    std::array<U8,sizeof(FPU)> fpu;
    std::array<U8, SNAPSHOT_BYTES> data;
    std::array<U8, 0x200> stack;
};

U32 randomNext(U32& value) {
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return value;
}

void writeFinite(KMemory* memory, U32 address, U32 value) {
    const U32 sign = value & 0x80000000u;
    const U32 exponent = (((value >> 24) % 40u) + 100u) << 23;
    memory->writed(address, sign | exponent | (value & 0x007fffffu));
}

U32 activeEntry() { return activeBase+0x00046760u; }

void mapFixture(U32 moduleBase) {
    TestContext& context = testContext();
    activeBase=moduleBase;
    wasmJitAcknexTransformIslandFixtureSetModuleBase(activeBase);
    struct Map { U32 address; U32 pages; U32 prot; };
    const Map maps[] = {
        {activeBase+0x00046000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00084000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00104000u,1,K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC},
        {activeBase+0x00252000u,1,K_PROT_READ|K_PROT_WRITE},
        {activeBase+0x00304000u,1,K_PROT_READ|K_PROT_WRITE},
        {DATA,8,K_PROT_READ|K_PROT_WRITE},
        {STACK,2,K_PROT_READ|K_PROT_WRITE},
    };
    for (const Map& map : maps) {
        context.memory->mmap(context.thread,map.address,map.pages*K_PAGE_SIZE,map.prot,
            K_MAP_FIXED|K_MAP_PRIVATE,-1,0);
    }
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE
    // The shared strict-code installer also writes the isolated +49650 body
    // when the outer fixture is enabled.  Keep the ABI2-only fixture mapping
    // byte-for-byte unchanged when that macro is absent.
    context.memory->mmap(context.thread,activeBase+0x00049000u,K_PAGE_SIZE,
        K_PROT_READ|K_PROT_WRITE|K_PROT_EXEC,K_MAP_FIXED|K_MAP_PRIVATE,-1,0);
#endif
    wasmJitAcknexTransformIslandFixtureInstallCode(context.memory);
    context.memory->writeb(RETURN_EIP,0xcd);
    context.memory->writeb(RETURN_EIP+1,0x97);
    context.memory->clearPageWriteCounts(activeEntry() >> K_PAGE_SHIFT);
    context.memory->clearPageWriteCounts((activeBase+0x84364u) >> K_PAGE_SHIFT);
    context.memory->clearPageWriteCounts((activeBase+0x1041d7u) >> K_PAGE_SHIFT);
}

void prepare(const CaseSpec& spec) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    testNewInstruction(CF|AF|OF|DF);
    wasmJitAcknexTransformIslandFixtureInstallCode(context.memory);
    context.memory->writeb(RETURN_EIP,0xcd);
    context.memory->writeb(RETURN_EIP+1,0x97);
    context.memory->clearPageWriteCounts(activeEntry() >> K_PAGE_SHIFT);
    context.memory->clearPageWriteCounts((activeBase+0x84364u) >> K_PAGE_SHIFT);
    context.memory->clearPageWriteCounts((activeBase+0x1041d7u) >> K_PAGE_SHIFT);
    cpu->seg[CS].address=0; cpu->seg[DS].address=0; cpu->seg[ES].address=0;
    cpu->seg[FS].address=0; cpu->seg[GS].address=0; cpu->seg[SS].address=0;
    context.memory->memset(DATA,0,SNAPSHOT_BYTES);
    context.memory->memset(STACK,0,K_PAGE_SIZE*2);
    U32 random = spec.seed;
    for (U32 offset=0;offset<SNAPSHOT_BYTES;offset+=4)
        context.memory->writed(DATA+offset,randomNext(random));
    for (U32 i=0;i<6;++i) writeFinite(context.memory,spec.source+i*4,randomNext(random));
    const U32 matrix = spec.matrixRecord + 0x14;
    for (U32 i=0;i<16;++i) writeFinite(context.memory,matrix+i*4,randomNext(random));
    context.memory->writed(spec.source+0x20,0);
    context.memory->writed(spec.matrixRecord+4,spec.type);
    context.memory->writed(activeBase+0x304c40u,ENGINE);
    context.memory->writed(ENGINE+0x4f8,spec.matrixRecord);
    context.memory->writed(activeBase+0x252150u,activeBase+0x1042a3u);
    context.memory->writed(activeBase+0x252140u,activeBase+0x1041d7u);
    for (U32 i=0;i<8;++i) {
        cpu->reg[i].u32=0x71000000u+spec.seed+i*0x01010101u;
        cpu->xmm[i].pi.u64[0]=0x1111111100000000ull^((U64)spec.seed<<16)^i;
        cpu->xmm[i].pi.u64[1]=0xeeeeeeeeffffffffull^((U64)spec.seed<<24)^(i*17u);
    }
    cpu->reg[regSP].u32=STACK+0x1800u;
    cpu->reg[regCX].u32=spec.destination;
    cpu->reg[regDX].u32=spec.source;
    cpu->mxcsr=0x1f80u;
    cpu->fpu.reset();
    cpu->push32(RETURN_EIP);
    cpu->eip.u32=activeEntry();
    cpu->nextOp=nullptr;
    context.memory->clearOpCache();
}

bool runGuest(const char* name,bool forceNormal=false) {
    TestContext& context = testContext();
    CPU* cpu = context.cpu;
    cpu->nextOp=cpu->getNextOp();
    bool exception=false;
    try {
        for (U32 steps=0;steps<4096 && (!cpu->nextOp || cpu->nextOp->inst!=TestEnd);++steps) {
            if(forceNormal) {
                OpCallback callback=NormalCPU::getFunctionForOp(cpu->nextOp);
                if(!callback) break;
                callback(cpu,cpu->nextOp);
            } else {
                cpu->run();
            }
        }
    } catch (...) { exception=true; }
    if (exception || !cpu->nextOp || cpu->nextOp->inst!=TestEnd) {
        testFail("Acknex transform %s did not reach TestEnd",name);
        return false;
    }
    return true;
}

Snapshot capture() {
    TestContext& context = testContext();
    Snapshot result{};
    result.lazyType=(U32)context.cpu->lazyFlagType;
    result.lazyDst=context.cpu->dst.u32;
    result.lazySrc=context.cpu->src.u32;
    result.lazyResult=context.cpu->result.u32;
    result.mxcsr=context.cpu->mxcsr;
    std::memcpy(result.fpu.data(),&context.cpu->fpu,sizeof(FPU));
    context.cpu->fillFlags();
    for (U32 i=0;i<8;++i) {
        result.reg[i]=context.cpu->reg[i].u32;
        result.xmm[i][0]=context.cpu->xmm[i].pi.u64[0];
        result.xmm[i][1]=context.cpu->xmm[i].pi.u64[1];
    }
    result.flags=context.cpu->flags;
    result.eip=context.cpu->eip.u32;
    context.memory->memcpy(result.data.data(),DATA,SNAPSHOT_BYTES);
    context.memory->memcpy(result.stack.data(),STACK+0x1600u,(U32)result.stack.size());
    return result;
}

void compare(const CaseSpec& spec,const Snapshot& reference,const Snapshot& candidate,bool strictLazy=false) {
    for (U32 i=0;i<8;++i) {
        if (reference.reg[i]!=candidate.reg[i])
            testFail("Acknex transform %s GPR%u ref=%08x hle=%08x",spec.name,i,reference.reg[i],candidate.reg[i]);
        if (reference.xmm[i][0]!=candidate.xmm[i][0] || reference.xmm[i][1]!=candidate.xmm[i][1])
            testFail("Acknex transform %s XMM%u mismatch",spec.name,i);
    }
    if (reference.flags!=candidate.flags || reference.eip!=candidate.eip)
        testFail("Acknex transform %s flags/eip ref=%08x/%08x hle=%08x/%08x",spec.name,
            reference.flags,reference.eip,candidate.flags,candidate.eip);
    if(strictLazy && (reference.lazyType!=candidate.lazyType ||
       reference.lazyDst!=candidate.lazyDst || reference.lazySrc!=candidate.lazySrc ||
       reference.lazyResult!=candidate.lazyResult))
        testFail("Acknex transform %s NormalCPU lazy tuple mismatch",spec.name);
    // Raw lazy tuples are intentionally not a generated-Wasm ABI: it lowers TEST8
    // through FLAGS_AND8 while this helper uses canonical FLAGS_TEST8.  The
    // materialized flags above, all GPR/XMM state and a real successor decode
    // are the semantic contract.
    if(reference.mxcsr!=candidate.mxcsr || reference.fpu!=candidate.fpu)
        testFail("Acknex transform %s MXCSR/x87 state mismatch",spec.name);
    if (std::memcmp(reference.data.data(),candidate.data.data(),SNAPSHOT_BYTES)!=0)
        testFail("Acknex transform %s memory mismatch",spec.name);
    if (reference.stack!=candidate.stack)
        testFail("Acknex transform %s residual stack-memory mismatch",spec.name);
}

std::array<U64,WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT> counterSnapshot() {
    std::array<U64,WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT> result{};
    wasm_jit_copy_acknex_transform_island_snapshot(result.data(),sizeof(result));
    return result;
}

void runDifferentialCases() {
    const CaseSpec cases[] = {
        {SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,0x10203040u,"aligned-separated"},
        {SOURCE_DEFAULT,SOURCE_DEFAULT,MATRIX_RECORD_MOD4,3,0x20304050u,"source-destination-alias"},
        {SOURCE_DEFAULT,SOURCE_DEFAULT+0x0c,MATRIX_RECORD_ALIGNED,3,0x30405060u,"first-write-feeds-second-source"},
    };
    for (const CaseSpec& spec:cases) {
        wasmJitAcknexTransformIslandFixtureSetIdentity(false);
        prepare(spec);
        if (!runGuest(spec.name,true)) continue;
        const Snapshot reference=capture();

        wasmJitAcknexTransformIslandFixtureSetIdentity(true);
        prepare(spec);
        if (!wasmJitAcknexTransformIslandRunForTest(testContext().cpu)) {
            testFail("Acknex transform %s HLE rejected",spec.name);
            continue;
        }
        testContext().cpu->nextOp=testContext().cpu->getNextOp();
        const Snapshot candidate=capture();
        compare(spec,reference,candidate,true);
    }
}

void runGeneratedAndFallbackCases() {
    const CaseSpec aligned={SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,0x40506070u,"generated-aligned"};
    wasm_jit_reset_acknex_transform_island_counters();
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(aligned);
    runGuest(aligned.name);
    auto counters=counterSnapshot();
    if (!counters[(U32)WasmJitAcknexTransformCounter::CandidateAccepted] ||
        !counters[(U32)WasmJitAcknexTransformCounter::HelperSuccess] ||
        counters[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch]) {
        testFail("Acknex generated boundary admission accepted=%llu success=%llu hashMismatch=%llu",
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::CandidateAccepted],
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::HelperSuccess],
            (unsigned long long)counters[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch]);
    }

    const CaseSpec fallbackCases[] = {
        {SOURCE_DEFAULT+1,DEST_DEFAULT+1,MATRIX_RECORD_ALIGNED,3,0x50607080u,"generated-unaligned"},
        {SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,2,0x60708090u,"generated-type2"},
    };
    for (const CaseSpec& spec:fallbackCases) {
        wasmJitAcknexTransformIslandFixtureSetIdentity(false); prepare(spec); runGuest(spec.name);
        const Snapshot reference=capture();
        wasmJitAcknexTransformIslandFixtureSetIdentity(true); prepare(spec); runGuest(spec.name);
        compare(spec,reference,capture());
    }
}

void expectDirectFallback(const CaseSpec& spec,WasmJitAcknexTransformCounter reason,const char* name,
        const std::function<void()>& mutate) {
    wasm_jit_reset_acknex_transform_island_counters();
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(spec);
    mutate();
    CPU* cpu=testContext().cpu;
    U32 regs[8]; U64 xmm[8][2];
    for(U32 i=0;i<8;++i){regs[i]=cpu->reg[i].u32;xmm[i][0]=cpu->xmm[i].pi.u64[0];xmm[i][1]=cpu->xmm[i].pi.u64[1];}
    const U32 eip=cpu->eip.u32,esp=cpu->reg[regSP].u32,lazy=(U32)cpu->lazyFlagType;
    if (wasmJitAcknexTransformIslandRunForTest(cpu)) testFail("Acknex %s unexpectedly admitted",name);
    auto values=counterSnapshot();
    if (!values[(U32)reason]) testFail("Acknex %s fallback counter missing",name);
    for(U32 i=0;i<8;++i) if(regs[i]!=cpu->reg[i].u32 || xmm[i][0]!=cpu->xmm[i].pi.u64[0] || xmm[i][1]!=cpu->xmm[i].pi.u64[1])
        testFail("Acknex %s changed architectural state",name);
    if(eip!=cpu->eip.u32 || esp!=cpu->reg[regSP].u32 || lazy!=(U32)cpu->lazyFlagType)
        testFail("Acknex %s changed eip/esp/lazy state",name);
}

void runDirectFallbackMatrix() {
    const CaseSpec base={SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,0x708090a0u,"fallback"};
    expectDirectFallback({SOURCE_DEFAULT+1,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,base.seed,"unaligned"},
        WasmJitAcknexTransformCounter::FallbackUnaligned,"unaligned",[](){});
    expectDirectFallback({SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,2,base.seed,"type"},
        WasmJitAcknexTransformCounter::FallbackType,"type!=3",[](){});
    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackLiveTarget,"live-target",[](){
        testContext().memory->writed(activeBase+0x252150u,activeBase+0x1041d7u);
    });
    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackCodeBytes,"smc",[](){
        testContext().memory->writeb(activeEntry(),testContext().memory->readb(activeEntry())^1);
    });
    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackMxcsr,"mxcsr",[](){
        testContext().cpu->mxcsr=0x3f80u;
    });
    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackSegment,"segmented-ds",[](){
        testContext().cpu->seg[DS].address=0x1000u;
    });
    expectDirectFallback({SOURCE_DEFAULT,STACK+0x17d0u,MATRIX_RECORD_ALIGNED,3,base.seed,"stack-alias"},
        WasmJitAcknexTransformCounter::FallbackAlias,"stack-alias",[](){});
    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackStack,"stack-guard",[](){
        testContext().memory->mprotect(testContext().thread,STACK+0x1000u,K_PAGE_SIZE,K_PROT_READ);
    });
    testContext().memory->mprotect(testContext().thread,STACK+0x1000u,K_PAGE_SIZE,K_PROT_READ|K_PROT_WRITE);
    wasm_jit_reset_acknex_transform_island_counters();
    wasmJitAcknexTransformIslandFixtureSetIdentity(false); prepare(base);
    if (wasmJitAcknexTransformIslandRunForTest(testContext().cpu) ||
        !counterSnapshot()[(U32)WasmJitAcknexTransformCounter::FallbackIdentity])
        testFail("Acknex content-identity fallback");

    expectDirectFallback(base,WasmJitAcknexTransformCounter::FallbackMemory,"fault-preflight",[](){
        testContext().memory->mprotect(testContext().thread,DEST_DEFAULT,K_PAGE_SIZE,K_PROT_READ);
    });
    testContext().memory->mprotect(testContext().thread,DEST_DEFAULT,K_PAGE_SIZE,K_PROT_READ|K_PROT_WRITE);
    wasmJitAcknexTransformIslandFixtureInstallCode(testContext().memory);
}

void runCodeMismatchDiagnosticsAndNegativeCache() {
    const CaseSpec spec={SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,
        0x78899aabu,"code-mismatch-diagnostic"};
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(spec);
    wasm_jit_reset_acknex_transform_island_counters();
    KMemory* memory=testContext().memory;
    const U8 expected=memory->readb(activeEntry());
    memory->writeb(activeEntry(),expected^1u);
    if (wasmJitAcknexTransformIslandRunForTest(testContext().cpu) ||
        wasmJitAcknexTransformIslandRunForTest(testContext().cpu))
        testFail("Acknex code mismatch unexpectedly admitted");
    const auto values=counterSnapshot();
    if (values[(U32)WasmJitAcknexTransformCounter::FallbackCodeBytes]!=2 ||
        values[(U32)WasmJitAcknexTransformCounter::CodeMismatchEntry]!=1 ||
        values[(U32)WasmJitAcknexTransformCounter::FallbackCodeNegativeCached]!=1 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchState]!=2 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchComponent]!=1 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchOffset]!=0 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchAddress]!=activeEntry() ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchExpected]!=expected ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchActual]!=(U8)(expected^1u) ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchBase]!=activeBase ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchEntry]!=activeEntry() ||
        !values[(U32)WasmJitAcknexTransformCounter::FirstMismatchMappingGeneration]) {
        testFail("Acknex code mismatch ABI/cache evidence invalid");
    }

    // The fixture install models a new trusted mapping.  Production negative
    // verdicts intentionally stay sticky until the real mapping generation
    // changes, so a later byte repair can never turn a rejected process into
    // an HLE-admitted one silently.
    wasmJitAcknexTransformIslandFixtureInstallCode(memory);
    prepare(spec);
    if (!wasmJitAcknexTransformIslandRunForTest(testContext().cpu))
        testFail("Acknex remapped code remained negative-cached");
}

void expectThunkCoordMismatch(U32 offset, const char* name) {
    const CaseSpec spec={SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,
        0x7a8b9cadu,name};
    wasmJitAcknexTransformIslandFixtureSetIdentity(true);
    prepare(spec);
    wasm_jit_reset_acknex_transform_island_counters();
    KMemory* memory=testContext().memory;
    const U32 address=activeBase+0x00084364u+offset;
    const U8 expected=memory->readb(address);
    memory->writeb(address,expected^1u);
    if (wasmJitAcknexTransformIslandRunForTest(testContext().cpu))
        testFail("Acknex %s unexpectedly admitted",name);
    const auto values=counterSnapshot();
    if (values[(U32)WasmJitAcknexTransformCounter::CodeMismatchThunkCoord]!=1 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchComponent]!=2 ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchOffset]!=offset ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchAddress]!=address ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchExpected]!=expected ||
        values[(U32)WasmJitAcknexTransformCounter::FirstMismatchActual]!=(U8)(expected^1u))
        testFail("Acknex %s mismatch byte evidence invalid",name);
}

void runTerminalRelocationBoundaryCases() {
    // Exact 2-byte opcode + terminal 4-byte relocation is exercised by every
    // success case.  These mutations prove that the empty-suffix fix did not
    // weaken either side of that strict six-byte contract.
    expectThunkCoordMismatch(0,"thunk-prefix-byte");
    expectThunkCoordMismatch(2,"thunk-relocation-byte");
    wasmJitAcknexTransformIslandFixtureInstallCode(testContext().memory);
}

void runRelocatedGeneratedCase() {
    mapFixture(0x7f3a0000u);
    const CaseSpec spec={SOURCE_DEFAULT,DEST_DEFAULT,MATRIX_RECORD_ALIGNED,3,
        0x8090a0b0u,"generated-current-aslr-base"};
    wasmJitAcknexTransformIslandFixtureSetIdentity(false); prepare(spec); runGuest(spec.name);
    const Snapshot reference=capture();
    wasm_jit_reset_acknex_transform_island_counters();
    wasmJitAcknexTransformIslandFixtureSetIdentity(true); prepare(spec); runGuest(spec.name);
    compare(spec,reference,capture());
    const auto values=counterSnapshot();
    if(!values[(U32)WasmJitAcknexTransformCounter::HelperSuccess] ||
       values[(U32)WasmJitAcknexTransformCounter::FallbackCodeBytes] ||
       values[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch])
        testFail("Acknex relocated generated gate success=%llu code=%llu hash=%llu",
            (unsigned long long)values[(U32)WasmJitAcknexTransformCounter::HelperSuccess],
            (unsigned long long)values[(U32)WasmJitAcknexTransformCounter::FallbackCodeBytes],
            (unsigned long long)values[(U32)WasmJitAcknexTransformCounter::CandidateBlockHashMismatch]);
}

} // namespace

void testWasmJitAcknexTransformIsland() {
    if(!wasmJitAcknexTransformIslandSha256SelfTest())
        testFail("Acknex transform-island SHA-256 self-test");
    mapFixture(PREFERRED_BASE);
    runDifferentialCases();
    runGeneratedAndFallbackCases();
    runDirectFallbackMatrix();
    runCodeMismatchDiagnosticsAndNegativeCache();
    runTerminalRelocationBoundaryCases();
    runRelocatedGeneratedCase();
    printf("Acknex transform-island SHA/RVA/hash/IAT differential/fallback fixture OK\n");
}

#else
void testWasmJitAcknexTransformIsland() {}
#endif
