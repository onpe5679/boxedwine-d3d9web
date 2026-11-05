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

#include "testString.h"
#include "testCPU.h"
#include "testX86Util.h"
#include "testAsmJit.h"
#include "../../emulation/cpu/normal/normalCPU.h"
#include "ksignal.h"

#define cpu (testContext().cpu)
#define memory (testContext().memory)
#define pushCode8 testPushCode8
#define newInstruction testNewInstruction
#define runTestCPU testRunCPU
#define failed testFail

namespace {

using namespace TestX86;

constexpr U32 REG_GUARD = 0xA55A0000;
constexpr U32 SRC_BASE = 0x1200;
constexpr U32 DST_BASE = 0x2200;
constexpr U32 OVERLAP_BASE = 0x3200;
constexpr U32 PAGE_SRC_BASE = 0x3fff;
constexpr U32 PAGE_DST_BASE = 0x5fff;
constexpr size_t OVERLAP_SIZE = 96;
constexpr size_t PAGE_TEST_SIZE = 32;
constexpr U8 PREFIX_REPNE = 0xf2;
constexpr U8 PREFIX_REPE = 0xf3;
constexpr U8 GUARD_BEFORE = 0x5a;
constexpr U8 GUARD_AFTER = 0xa5;
constexpr U32 FLAG_MASK = CF | PF | AF | ZF | SF | OF | DF;
constexpr U32 ARITH_FLAG_MASK = CF | PF | AF | ZF | SF | OF;

enum RegId {
    R_AX,
    R_CX,
    R_DX,
    R_BX,
    R_SP,
    R_BP,
    R_SI,
    R_DI
};

enum StringOp {
    STRING_MOVS,
    STRING_CMPS,
    STRING_STOS,
    STRING_LODS,
    STRING_SCAS
};

struct StringCase {
    U8 prefix;
    U32 flags;
    U32 count;
    bool backward;
    U32 eax;
    U8 src[20];
    U8 dst[20];
    size_t dataSize;
    const char* name;
};

void initCode(asmjit::CodeHolder& code) {
    asmjit::Environment env(asmjit::Arch::kX86);
    if (code.init(env) != asmjit::Error::kOk) {
        failed("asmjit string code init failed");
    }
}

void pushGeneratedCode(const asmjit::CodeHolder& code, bool stripOperandSizePrefix) {
    const asmjit::CodeBuffer& buffer = code.text_section()->buffer();
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (stripOperandSizePrefix && buffer.data()[i] == 0x66) {
            continue;
        }
        pushCode8(buffer.data()[i]);
    }
}

asmjit::Error emitStringInstruction(asmjit::x86::Assembler& a, StringOp op, int width) {
    if (op == STRING_MOVS) {
        if (width == 1) return a.movsb();
        if (width == 2) return a.movsw();
        return a.movsd();
    }
    if (op == STRING_CMPS) {
        if (width == 1) return a.cmpsb();
        if (width == 2) return a.cmpsw();
        return a.cmpsd();
    }
    if (op == STRING_STOS) {
        if (width == 1) return a.stosb();
        if (width == 2) return a.stosw();
        return a.stosd();
    }
    if (op == STRING_LODS) {
        if (width == 1) return a.lodsb();
        if (width == 2) return a.lodsw();
        return a.lodsd();
    }
    if (width == 1) return a.scasb();
    if (width == 2) return a.scasw();
    return a.scasd();
}

void emitCode(StringOp op, int width, U8 prefix) {
    if (prefix) {
        pushCode8(prefix);
    }
    asmjit::CodeHolder code;
    initCode(code);
    asmjit::x86::Assembler a(&code);
    if (emitStringInstruction(a, op, width) != asmjit::Error::kOk) {
        failed("asmjit string emit failed");
    }
    pushGeneratedCode(code, width == 2);
}

void initRegisters(U32* expectedRegs, U32 eax, U32 esi, U32 edi, U32 ecx) {
    for (int i = 0; i < 8; ++i) {
        cpu->reg[i].u32 = REG_GUARD | (0x0100 + i);
        expectedRegs[i] = cpu->reg[i].u32;
    }
    cpu->reg[R_AX].u32 = eax;
    cpu->reg[R_CX].u32 = ecx;
    cpu->reg[R_SI].u32 = esi;
    cpu->reg[R_DI].u32 = edi;
    expectedRegs[R_AX] = eax;
    expectedRegs[R_CX] = ecx;
    expectedRegs[R_SI] = esi;
    expectedRegs[R_DI] = edi;
}

U32 addressMask(bool address32) {
    return address32 ? 0xffffffff : 0xffff;
}

U32 indexOffset(U32 value, bool address32) {
    return value & addressMask(address32);
}

U32 updateIndex(U32 value, S32 delta, bool address32) {
    if (address32) {
        return value + delta;
    }
    return (value & 0xffff0000) | ((value + delta) & 0xffff);
}

U32 countValue(U32 ecx, bool address32) {
    return address32 ? ecx : (ecx & 0xffff);
}

void setCountValue(U32& ecx, U32 count, bool address32) {
    if (address32) {
        ecx = count;
    } else {
        ecx = (ecx & 0xffff0000) | (count & 0xffff);
    }
}

void writeBytes(U32 base, const U8* values, size_t count) {
    memory->writeb(base - 1, GUARD_BEFORE);
    for (size_t i = 0; i < count; ++i) {
        memory->writeb(base + (U32)i, values[i]);
    }
    memory->writeb(base + (U32)count, GUARD_AFTER);
}

void verifyBytes(U32 base, const U8* expected, size_t count, const char* name) {
    if (memory->readb(base - 1) != GUARD_BEFORE || memory->readb(base + (U32)count) != GUARD_AFTER) {
        failed("%s memory guard", name);
    }
    for (size_t i = 0; i < count; ++i) {
        if (memory->readb(base + (U32)i) != expected[i]) {
            failed("%s memory byte", name);
        }
    }
}

bool evenParity(U32 value) {
    value &= 0xff;
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1) == 0;
}

U32 subFlags(U32 lhs, U32 rhs, U32 result, int bits) {
    U32 mask = widthMask(bits);
    U32 sign = signBit(bits);
    lhs &= mask;
    rhs &= mask;
    result &= mask;
    U32 flags = 0;
    if (lhs < rhs) flags |= CF;
    if (((lhs ^ rhs) & (lhs ^ result) & sign) != 0) flags |= OF;
    if (((lhs ^ rhs ^ result) & 0x10) != 0) flags |= AF;
    if (result == 0) flags |= ZF;
    if ((result & sign) != 0) flags |= SF;
    if (evenParity(result)) flags |= PF;
    return flags;
}

U32 accumulatorForWidth(U32 eax, int width) {
    if (width == 1) return eax & 0xff;
    if (width == 2) return eax & 0xffff;
    return eax;
}

void setAccumulator(U32& eax, int width, U32 value) {
    if (width == 1) {
        eax = (eax & 0xffffff00) | (value & 0xff);
    } else if (width == 2) {
        eax = (eax & 0xffff0000) | (value & 0xffff);
    } else {
        eax = value;
    }
}

U32 readCaseValue(const U8* values, U32 offset, int width) {
    U32 result = values[offset];
    if (width >= 2) {
        result |= ((U32)values[offset + 1]) << 8;
    }
    if (width == 4) {
        result |= ((U32)values[offset + 2]) << 16;
        result |= ((U32)values[offset + 3]) << 24;
    }
    return result;
}

void writeCaseValue(U8* values, U32 offset, int width, U32 value) {
    values[offset] = (U8)value;
    if (width >= 2) {
        values[offset + 1] = (U8)(value >> 8);
    }
    if (width == 4) {
        values[offset + 2] = (U8)(value >> 16);
        values[offset + 3] = (U8)(value >> 24);
    }
}

U32 readMemoryValue(U32 linear, int width) {
    if (width == 1) return memory->readb(linear);
    if (width == 2) return memory->readw(linear);
    return memory->readd(linear);
}

void writeMemoryValue(U32 linear, int width, U32 value) {
    if (width == 1) {
        memory->writeb(linear, value);
    } else if (width == 2) {
        memory->writew(linear, value);
    } else {
        memory->writed(linear, value);
    }
}

void copyBytes(U8* dst, const U8* src, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

void simulate(StringOp op, int width, bool address32, U8 prefix, U32& expectedFlags, U32* expectedRegs, const U8* expectedSrc, U8* expectedDst) {
    S32 delta = (expectedFlags & DF) ? -width : width;
    bool compareOp = op == STRING_CMPS || op == STRING_SCAS;
    bool repeat = prefix == PREFIX_REPNE || prefix == PREFIX_REPE;
    U32 remaining = repeat ? countValue(expectedRegs[R_CX], address32) : 1;

    while (remaining) {
        U32 esi = expectedRegs[R_SI];
        U32 edi = expectedRegs[R_DI];
        U32 srcOffset = indexOffset(esi, address32) - SRC_BASE;
        U32 dstOffset = indexOffset(edi, address32) - DST_BASE;
        U32 srcValue = readCaseValue(expectedSrc, srcOffset, width);
        U32 dstValue = readCaseValue(expectedDst, dstOffset, width);

        if (op == STRING_MOVS) {
            writeCaseValue(expectedDst, dstOffset, width, srcValue);
        } else if (op == STRING_CMPS) {
            U32 result = (srcValue - dstValue) & widthMask(width * 8);
            expectedFlags = (expectedFlags & ~ARITH_FLAG_MASK) | subFlags(srcValue, dstValue, result, width * 8);
        } else if (op == STRING_STOS) {
            writeCaseValue(expectedDst, dstOffset, width, accumulatorForWidth(expectedRegs[R_AX], width));
        } else if (op == STRING_LODS) {
            setAccumulator(expectedRegs[R_AX], width, srcValue);
        } else {
            U32 acc = accumulatorForWidth(expectedRegs[R_AX], width);
            U32 result = (acc - dstValue) & widthMask(width * 8);
            expectedFlags = (expectedFlags & ~ARITH_FLAG_MASK) | subFlags(acc, dstValue, result, width * 8);
        }

        if (op == STRING_MOVS || op == STRING_CMPS || op == STRING_LODS) {
            expectedRegs[R_SI] = updateIndex(expectedRegs[R_SI], delta, address32);
        }
        if (op == STRING_MOVS || op == STRING_CMPS || op == STRING_STOS || op == STRING_SCAS) {
            expectedRegs[R_DI] = updateIndex(expectedRegs[R_DI], delta, address32);
        }

        if (repeat) {
            --remaining;
            setCountValue(expectedRegs[R_CX], remaining, address32);
            if (compareOp) {
                bool zf = (expectedFlags & ZF) != 0;
                if ((prefix == PREFIX_REPE && !zf) || (prefix == PREFIX_REPNE && zf)) {
                    break;
                }
            }
        } else {
            break;
        }
    }

}

void runStringCase(StringOp op, int width, bool address32, const StringCase& data) {
    newInstruction(data.flags);
    cpu->big = address32 ? 1 : 0;
    cpu->seg[ES].address = TEST_HEAP_ADDRESS;
    cpu->seg[ES].value = TEST_HEAP_SEG;
    cpu->thread->process->hasSetSeg[ES] = true;

    U32 startOffset = data.backward && data.count ? (data.count - 1) * width : 0;
    U32 esi = SRC_BASE + startOffset;
    U32 edi = DST_BASE + startOffset;
    U32 ecx = (address32 || data.prefix) ? data.count : (REG_GUARD | data.count);
    U32 expectedRegs[8];
    U8 expectedDst[20];
    U32 expectedFlags = data.flags;
    size_t dataSize = data.dataSize;
    size_t requiredSize = data.count ? data.count * width : width;
    if (requiredSize > dataSize) {
        dataSize = requiredSize;
    }
    initRegisters(expectedRegs, data.eax, esi, edi, ecx);
    writeBytes(TEST_HEAP_ADDRESS + SRC_BASE, data.src, dataSize);
    writeBytes(TEST_HEAP_ADDRESS + DST_BASE, data.dst, dataSize);
    copyBytes(expectedDst, data.dst, dataSize);

    emitCode(op, width, data.prefix);
    simulate(op, width, address32, data.prefix, expectedFlags, expectedRegs, data.src, expectedDst);
    runTestCPU();

    verifyRegisters(cpu, expectedRegs, data.name);
    verifyBytes(TEST_HEAP_ADDRESS + SRC_BASE, data.src, dataSize, data.name);
    verifyBytes(TEST_HEAP_ADDRESS + DST_BASE, expectedDst, dataSize, data.name);
    if ((actualFlags(cpu, true) & FLAG_MASK) != (expectedFlags & FLAG_MASK)) {
        if (op != STRING_CMPS && op != STRING_SCAS) {
            failed("%s flags changed", data.name);
        }
    }
    if (op == STRING_CMPS || op == STRING_SCAS) {
        if ((actualFlags(cpu, true) & FLAG_MASK) != (expectedFlags & FLAG_MASK)) {
            failed("%s flags", data.name);
        }
    }
}

void runStringCases(StringOp op, int width, bool address32, const StringCase* cases, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        runStringCase(op, width, address32, cases[i]);
    }
}

void initOverlapBytes(U8* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        values[i] = (U8)(0x40 + ((i * 17 + 3) & 0x3f));
    }
}

void writeOverlapBytes(const U8* values, size_t count) {
    memory->writeb(TEST_HEAP_ADDRESS + OVERLAP_BASE - 1, GUARD_BEFORE);
    for (size_t i = 0; i < count; ++i) {
        memory->writeb(TEST_HEAP_ADDRESS + OVERLAP_BASE + (U32)i, values[i]);
    }
    memory->writeb(TEST_HEAP_ADDRESS + OVERLAP_BASE + (U32)count, GUARD_AFTER);
}

void verifyOverlapBytes(const U8* expected, size_t count, const char* name) {
    if (memory->readb(TEST_HEAP_ADDRESS + OVERLAP_BASE - 1) != GUARD_BEFORE ||
            memory->readb(TEST_HEAP_ADDRESS + OVERLAP_BASE + (U32)count) != GUARD_AFTER) {
        failed("%s overlap guard", name);
    }
    for (size_t i = 0; i < count; ++i) {
        if (memory->readb(TEST_HEAP_ADDRESS + OVERLAP_BASE + (U32)i) != expected[i]) {
            failed("%s overlap byte", name);
        }
    }
}

void runHotMovsCase(int width, bool address32, U32 count, bool backward, const char* name) {
    newInstruction(backward ? DF : 0);
    cpu->big = address32 ? 1 : 0;
    Seg savedDs = cpu->seg[DS];
    Seg savedEs = cpu->seg[ES];
    bool savedHasDs = cpu->thread->process->hasSetSeg[DS];
    bool savedHasEs = cpu->thread->process->hasSetSeg[ES];
    if (address32) {
        cpu->seg[DS].address = 0;
        cpu->seg[DS].value = 0;
        cpu->seg[ES].address = 0;
        cpu->seg[ES].value = 0;
        cpu->thread->process->hasSetSeg[DS] = false;
        cpu->thread->process->hasSetSeg[ES] = false;
    } else {
        cpu->seg[ES].address = TEST_HEAP_ADDRESS;
        cpu->seg[ES].value = TEST_HEAP_SEG;
        cpu->thread->process->hasSetSeg[ES] = true;
    }

    U32 startOffset = backward ? (count - 1) * width : 0;
    U32 srcBase = address32 ? TEST_HEAP_ADDRESS + SRC_BASE : SRC_BASE;
    U32 dstBase = address32 ? TEST_HEAP_ADDRESS + DST_BASE : DST_BASE;
    U32 srcStart = srcBase + startOffset;
    U32 dstStart = dstBase + startOffset;
    size_t dataSize = count * width;
    U8 initialSrc[256];
    U8 initialDst[256];
    U8 expectedDst[256];
    if (dataSize > sizeof(initialSrc)) {
        failed("%s hot data size", name);
        cpu->seg[DS] = savedDs;
        cpu->seg[ES] = savedEs;
        cpu->thread->process->hasSetSeg[DS] = savedHasDs;
        cpu->thread->process->hasSetSeg[ES] = savedHasEs;
        return;
    }
    for (size_t i = 0; i < dataSize; ++i) {
        initialSrc[i] = (U8)(0x20 + ((i * 13 + 7) & 0x7f));
        initialDst[i] = (U8)(0x90 + ((i * 13 + 7) & 0x3f));
    }
    copyBytes(expectedDst, initialSrc, dataSize);
    emitCode(STRING_MOVS, width, PREFIX_REPE);

    for (int pass = 0; pass < 2; ++pass) {
        cpu->eip.u32 = 0;
        cpu->big = address32 ? 1 : 0;
        cpu->setFlags(backward ? DF : 0, FMASK_ALL);
        cpu->reg[R_SI].u32 = srcStart;
        cpu->reg[R_DI].u32 = dstStart;
        cpu->reg[R_CX].u32 = count;
        writeBytes(TEST_HEAP_ADDRESS + SRC_BASE, initialSrc, dataSize);
        writeBytes(TEST_HEAP_ADDRESS + DST_BASE, initialDst, dataSize);
        runTestCPU();
    }

    verifyBytes(TEST_HEAP_ADDRESS + SRC_BASE, initialSrc, dataSize, name);
    verifyBytes(TEST_HEAP_ADDRESS + DST_BASE, expectedDst, dataSize, name);
    S32 totalDelta = backward ? -(S32)(count * width) : (S32)(count * width);
    if (cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != updateIndex(srcStart, totalDelta, address32) ||
            cpu->reg[R_DI].u32 != updateIndex(dstStart, totalDelta, address32)) {
        failed("%s hot registers", name);
    }
    if ((actualFlags(cpu, true) & FLAG_MASK) != (backward ? DF : 0)) {
        failed("%s hot flags", name);
    }
    cpu->seg[DS] = savedDs;
    cpu->seg[ES] = savedEs;
    cpu->thread->process->hasSetSeg[DS] = savedHasDs;
    cpu->thread->process->hasSetSeg[ES] = savedHasEs;
}

void simulateOverlapMovs(U8* expected, int width, bool address32, U32 count, bool backward, U32& esi, U32& edi, U32& ecx) {
    S32 delta = backward ? -width : width;
    U32 remaining = countValue(ecx, address32);
    while (remaining) {
        U32 srcOffset = indexOffset(esi, address32) - OVERLAP_BASE;
        U32 dstOffset = indexOffset(edi, address32) - OVERLAP_BASE;
        U32 value = readCaseValue(expected, srcOffset, width);
        writeCaseValue(expected, dstOffset, width, value);
        esi = updateIndex(esi, delta, address32);
        edi = updateIndex(edi, delta, address32);
        --remaining;
        setCountValue(ecx, remaining, address32);
    }
}

void runOverlapMovsCase(int width, bool address32, U32 count, bool backward, const char* name) {
    newInstruction(backward ? DF : 0);
    cpu->big = address32 ? 1 : 0;
    cpu->seg[ES].address = TEST_HEAP_ADDRESS;
    cpu->seg[ES].value = TEST_HEAP_SEG;
    cpu->thread->process->hasSetSeg[ES] = true;

    U32 overlapDelta = width;
    U32 srcStart = OVERLAP_BASE + (backward ? overlapDelta + (count - 1) * width : 0);
    U32 dstStart = OVERLAP_BASE + (backward ? (count - 1) * width : overlapDelta);
    U32 ecx = count;
    size_t dataSize = count * width + overlapDelta;
    if (dataSize > OVERLAP_SIZE) {
        failed("%s overlap data size", name);
        return;
    }

    U8 initial[OVERLAP_SIZE];
    U8 expected[OVERLAP_SIZE];
    initOverlapBytes(initial, dataSize);
    copyBytes(expected, initial, dataSize);
    writeOverlapBytes(initial, dataSize);

    U32 expectedRegs[8];
    initRegisters(expectedRegs, 0x89abcdef, srcStart, dstStart, ecx);
    simulateOverlapMovs(expected, width, address32, count, backward, expectedRegs[R_SI], expectedRegs[R_DI], expectedRegs[R_CX]);

    emitCode(STRING_MOVS, width, PREFIX_REPE);
    runTestCPU();

    verifyRegisters(cpu, expectedRegs, name);
    verifyOverlapBytes(expected, dataSize, name);
    if ((actualFlags(cpu, true) & FLAG_MASK) != (backward ? DF : 0)) {
        failed("%s overlap flags", name);
    }
}

void runOverlapMovsCases(int width, bool address32) {
    static const U32 counts[] = {15, 16, 17};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        runOverlapMovsCase(width, address32, counts[i], false, "string move overlap forward");
        runOverlapMovsCase(width, address32, counts[i], true, "string move overlap backward");
    }
}

void runHotMovsCases(int width, bool address32) {
    runHotMovsCase(width, address32, 48, false, "string move hot forward");
    runHotMovsCase(width, address32, 48, true, "string move hot backward");
}

// This deliberately keeps Process::hasSetSeg sticky while mutating the live
// CPU-local bases.  It is the direct regression shape for the guarded MRG
// route: the generated block must remain installed and choose emulate for a
// nonzero base, then return to movsr when both bases become zero again.
struct MovsdRuntimeZeroGuardProfile {
    U32 dsBase;
    U32 esBase;
    const char* name;
};

constexpr U32 MOVSD_GUARD_REGION = 0x6800;
// The boundary matrix reaches 300 dwords (1200 bytes) in both distinct
// source/destination ranges, while keeping room for overlap rows and guards.
constexpr U32 MOVSD_GUARD_REGION_SIZE = 4096;
constexpr U32 MOVSD_GUARD_SOURCE = MOVSD_GUARD_REGION + 96;
constexpr U32 MOVSD_GUARD_DESTINATION = MOVSD_GUARD_REGION + 2048;
constexpr U32 MOVSD_GUARD_COUNT = 30;

void initPageBytes(U8* values, size_t count, U8 seed);
void writePageBytes(U32 offset, const U8* values, size_t count);
void verifyPageBytes(U32 offset, const U8* expected, size_t count, const char* name);

void writeMovsdGuardRegion(const U8* values) {
    for (U32 i = 0; i < MOVSD_GUARD_REGION_SIZE; ++i) {
        memory->writeb(TEST_HEAP_ADDRESS + MOVSD_GUARD_REGION + i, values[i]);
    }
}

void verifyMovsdGuardRegion(const U8* expected, const char* name) {
    for (U32 i = 0; i < MOVSD_GUARD_REGION_SIZE; ++i) {
        if (memory->readb(TEST_HEAP_ADDRESS + MOVSD_GUARD_REGION + i) != expected[i]) {
            failed("%s memory at %u", name, i);
            return;
        }
    }
}

void runMovsdRuntimeZeroGuardCase(DecodedOp* op, void* compiledEntry,
                                  const MovsdRuntimeZeroGuardProfile& profile,
                                  bool backward, bool overlap, U32 count = MOVSD_GUARD_COUNT) {
    U8 initial[MOVSD_GUARD_REGION_SIZE];
    U8 expected[MOVSD_GUARD_REGION_SIZE];
    for (U32 i = 0; i < MOVSD_GUARD_REGION_SIZE; ++i) {
        initial[i] = (U8)(0x21 + ((i * 29 + 11) & 0x7f));
        expected[i] = initial[i];
    }

    U32 srcOffset = MOVSD_GUARD_SOURCE;
    U32 dstOffset = MOVSD_GUARD_DESTINATION;
    if (overlap) {
        dstOffset = backward ? srcOffset - 4 : srcOffset + 4;
    }
    // REP with ECX=0 does not evaluate the first element. Keep the DF=1
    // fixture address arithmetic equally inert rather than underflowing.
    U32 startOffset = backward && count ? (count - 1) * 4 : 0;
    S32 delta = backward ? -4 : 4;
    U32 expectedEsi = (TEST_HEAP_ADDRESS + srcOffset - profile.dsBase) + startOffset;
    U32 expectedEdi = (TEST_HEAP_ADDRESS + dstOffset - profile.esBase) + startOffset;
    U32 simulateEsi = expectedEsi;
    U32 simulateEdi = expectedEdi;
    for (U32 i = 0; i < count; ++i) {
        U32 srcIndex = (simulateEsi + profile.dsBase) - (TEST_HEAP_ADDRESS + MOVSD_GUARD_REGION);
        U32 dstIndex = (simulateEdi + profile.esBase) - (TEST_HEAP_ADDRESS + MOVSD_GUARD_REGION);
        U32 value = readCaseValue(expected, srcIndex, 4);
        writeCaseValue(expected, dstIndex, 4, value);
        simulateEsi += delta;
        simulateEdi += delta;
    }

    cpu->setSeg(DS, profile.dsBase, profile.dsBase ? TEST_HEAP_SEG : 0);
    cpu->setSeg(ES, profile.esBase, profile.esBase ? TEST_HEAP_SEG : 0);
    if (!cpu->thread->process->hasSetSeg[DS]) {
        failed("%s lost sticky DS route", profile.name);
        return;
    }
    if (op->pfn != cpu->thread->process->startJITOp || op->pfnJitCode != compiledEntry ||
            op->blockStart != op || !(op->flags & OP_FLAG_JIT)) {
        failed("%s pre-run compiled block identity", profile.name);
        return;
    }
    writeMovsdGuardRegion(initial);
    U32 expectedRegs[8];
    initRegisters(expectedRegs, 0x89abcdef, expectedEsi, expectedEdi, count);
    expectedRegs[R_SI] = simulateEsi;
    expectedRegs[R_DI] = simulateEdi;
    expectedRegs[R_CX] = 0;
    cpu->setFlags(backward ? DF : 0, FMASK_ALL);
    cpu->eip.u32 = 0;
    cpu->nextOp = op;
    cpu->wasmJitBailout = 0;
    cpu->run();

    verifyRegisters(cpu, expectedRegs, profile.name);
    verifyMovsdGuardRegion(expected, profile.name);
    if ((actualFlags(cpu, true) & FLAG_MASK) != (backward ? DF : 0)) {
        failed("%s flags", profile.name);
    }
    if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
        failed("%s nextOp", profile.name);
    }
    if (cpu->eip.u32 != op->len) {
        failed("%s eip", profile.name);
    }
    if (cpu->wasmJitBailout != 0 || op->pfn != cpu->thread->process->startJITOp ||
            op->pfnJitCode != compiledEntry || op->blockStart != op) {
        failed("%s compiled block identity", profile.name);
    }
}

// The normal page-boundary helper creates a new instruction, which would not
// prove the MRG block that was already published. Keep this on the same B5
// entry and cross both source and destination page boundaries within the REP
// in each direction.
void runMovsdRuntimeZeroGuardPageCrossCase(DecodedOp* op, void* compiledEntry, bool backward) {
    constexpr U32 count = 3;
    constexpr U32 byteCount = count * 4;
    U8 source[PAGE_TEST_SIZE];
    U8 destination[PAGE_TEST_SIZE];
    U8 expectedDestination[PAGE_TEST_SIZE];
    initPageBytes(source, byteCount, 0x19);
    initPageBytes(destination, byteCount, 0x61);
    copyBytes(expectedDestination, destination, byteCount);
    for (U32 i = 0; i < count; ++i) {
        U32 value = readCaseValue(source, i * 4, 4);
        writeCaseValue(expectedDestination, i * 4, 4, value);
    }

    cpu->setSeg(DS, 0, 0);
    cpu->setSeg(ES, 0, 0);
    if (!cpu->thread->process->hasSetSeg[DS] || cpu->thread->process->hasSetSeg[ES]) {
        failed("movsd runtime-zero page-cross source-only setup");
        return;
    }
    if (op->pfn != cpu->thread->process->startJITOp || op->pfnJitCode != compiledEntry ||
            op->blockStart != op || !(op->flags & OP_FLAG_JIT)) {
        failed("movsd runtime-zero page-cross pre-run compiled block identity");
        return;
    }
    writePageBytes(PAGE_SRC_BASE, source, byteCount);
    writePageBytes(PAGE_DST_BASE, destination, byteCount);
    U32 startOffset = backward ? (count - 1) * 4 : 0;
    S32 delta = backward ? -4 : 4;
    U32 expectedRegs[8];
    initRegisters(expectedRegs, 0x89abcdef,
                  TEST_HEAP_ADDRESS + PAGE_SRC_BASE + startOffset,
                  TEST_HEAP_ADDRESS + PAGE_DST_BASE + startOffset, count);
    expectedRegs[R_SI] += delta * (S32)count;
    expectedRegs[R_DI] += delta * (S32)count;
    expectedRegs[R_CX] = 0;
    cpu->setFlags(backward ? DF : 0, FMASK_ALL);
    cpu->eip.u32 = 0;
    cpu->nextOp = op;
    cpu->wasmJitBailout = 0;
    cpu->run();

    const char* name = backward ? "movsd runtime-zero page-cross backward" : "movsd runtime-zero page-cross forward";
    verifyRegisters(cpu, expectedRegs, name);
    verifyPageBytes(PAGE_SRC_BASE, source, byteCount, name);
    verifyPageBytes(PAGE_DST_BASE, expectedDestination, byteCount, name);
    if ((actualFlags(cpu, true) & FLAG_MASK) != (backward ? DF : 0)) {
        failed("%s flags", name);
    }
    if (!cpu->nextOp || cpu->nextOp->inst != TestEnd || cpu->eip.u32 != op->len) {
        failed("%s control flow", name);
    }
    if (cpu->wasmJitBailout != 0 || op->pfn != cpu->thread->process->startJITOp ||
            op->pfnJitCode != compiledEntry || op->blockStart != op) {
        failed("%s compiled block identity", name);
    }
}

#if defined(BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_FAULT_ORACLE) || defined(BOXEDWINE_WASM_JIT_MOVSD_RUNTIME_ZERO_GUARD)

// This binary is deliberately NormalCPU-only.  Do not copy its expected
// partial-fault values into the MRG arm until a browser run proves them.
struct MovsdPartialFaultRestore {
    KProcessPtr process;
    KMemory* memoryPtr;
    KThread* thread;
    KSigAction oldSegv;
    U32 protectedPage;
    bool pageProtected = false;

    ~MovsdPartialFaultRestore() {
        if (pageProtected) {
            memoryPtr->mprotect(thread, protectedPage, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE);
        }
        process->sigActions[K_SIGSEGV] = oldSegv;
    }
};

void runMovsdRuntimeZeroGuardPartialFaultOracleCase(bool backward) {
    constexpr U32 handler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 source = TEST_HEAP_ADDRESS + 0x8000;
    constexpr U32 protectedPage = TEST_HEAP_ADDRESS + 0xa000;
    constexpr U32 initialFirstDestination = 0xa1b2c3d4;
    constexpr U32 initialSecondDestination = 0x55667788;
    constexpr U32 initialFlags = CF | PF | AF | ZF | SF | OF;
    const U32 firstSource = source + (backward ? 4 : 0);
    const U32 secondSource = source + (backward ? 0 : 4);
    const U32 firstDestination = backward ? protectedPage + K_PAGE_SIZE : protectedPage - 4;
    const U32 secondDestination = backward ? protectedPage + K_PAGE_SIZE - 4 : protectedPage;
    const U32 flags = initialFlags | (backward ? DF : 0);

    newInstruction(flags);
    // MOC3's runtime-zero arm uses absolute guest offsets under zero live
    // DS/ES bases. Keep the oracle's linear addresses identical.
    cpu->setSeg(DS, 0, 0);
    cpu->setSeg(ES, 0, 0);
    // Write the signal endpoint before emitting/decoding the B5 block.  The
    // matching MRG fixture must retain this order to avoid code-page mutation
    // after publication.
    memory->writeb(handler, 0xcd);
    memory->writeb(handler + 1, 0x97);
    memory->clearPageWriteCounts(handler >> K_PAGE_SHIFT);

    U32 initialRegs[8];
    for (U32 i = 0; i < 8; ++i) {
        initialRegs[i] = 0x11000000 + i;
        cpu->reg[i].u32 = initialRegs[i];
    }
    cpu->reg[R_SP].u32 = 4096;
    cpu->reg[R_SI].u32 = firstSource;
    cpu->reg[R_DI].u32 = firstDestination;
    cpu->reg[R_CX].u32 = 2;
    initialRegs[R_SP] = cpu->reg[R_SP].u32;
    initialRegs[R_SI] = firstSource;
    initialRegs[R_DI] = firstDestination;
    initialRegs[R_CX] = 2;

    memory->writed(source, 0x10203040);
    memory->writed(source + 4, 0x50607080);
    memory->writed(source - 4, 0x21222324);
    memory->writed(source + 8, 0x31323334);
    memory->writed(firstDestination, initialFirstDestination);
    memory->writed(secondDestination, initialSecondDestination);
    memory->writed(backward ? firstDestination + 4 : firstDestination - 4, 0x6a6b6c6d);
    memory->writed(backward ? secondDestination - 4 : secondDestination + 4, 0x7a7b7c7d);

    MovsdPartialFaultRestore restore{cpu->thread->process, memory, cpu->thread,
        cpu->thread->process->sigActions[K_SIGSEGV], protectedPage};
    if (memory->mprotect(cpu->thread, protectedPage, K_PAGE_SIZE, K_PROT_READ)) {
        failed("movsd partial-fault oracle protect setup");
        return;
    }
    restore.pageProtected = true;
    cpu->thread->process->sigActions[K_SIGSEGV] = restore.oldSegv;
    cpu->thread->process->sigActions[K_SIGSEGV].handlerAndSigAction = handler;
    // The action-global sigInfo can be overwritten by a later fetch fault.
    // Preserve the first delivery's siginfo beside its architectural context.
    cpu->thread->process->sigActions[K_SIGSEGV].flags = K_SA_SIGINFO;

    pushCode8(PREFIX_REPE);
    pushCode8(0xa5); // REP MOVSD
    DecodedOp* normalOp = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!normalOp) {
        failed("movsd partial-fault oracle decode");
        return;
    }
    // The oracle binary contains the Wasm-JIT layout solely so it can share
    // the test harness. Route this clean op explicitly through NormalCPU;
    // never let its run count reach the JIT threshold.
    normalOp->pfn = NormalCPU::getFunctionForOp(normalOp);
    cpu->nextOp = normalOp;
    ::memset(cpu->thread->process->sigActions[K_SIGSEGV].sigInfo, 0,
        sizeof(cpu->thread->process->sigActions[K_SIGSEGV].sigInfo));
    bool faulted = false;
    int faultException = -1;
    try {
        cpu->run();
    } catch (int value) {
        faulted = true;
        faultException = value;
    } catch (...) {
        faulted = true;
    }
    const KSigAction observedSegv = cpu->thread->process->sigActions[K_SIGSEGV];
    const U32 context = TEST_STACK_ADDRESS - K_PAGE_SIZE * 32 + initialRegs[R_SP] - 768;
    const U32 signalInfo = (context & ~15) - 128;
    U32 savedSignalInfo[4] = {
        memory->readd(signalInfo), memory->readd(signalInfo + 4),
        memory->readd(signalInfo + 8), memory->readd(signalInfo + 12)
    };
    U32 savedRegs[8] = {};
    savedRegs[R_AX] = memory->readd(context + 0x40);
    savedRegs[R_CX] = memory->readd(context + 0x3c);
    savedRegs[R_DX] = memory->readd(context + 0x38);
    savedRegs[R_BX] = memory->readd(context + 0x34);
    savedRegs[R_SP] = memory->readd(context + 0x30);
    savedRegs[R_BP] = memory->readd(context + 0x2c);
    savedRegs[R_SI] = memory->readd(context + 0x28);
    savedRegs[R_DI] = memory->readd(context + 0x24);
    const U32 savedEip = memory->readd(context + 0x4c);
    const U32 savedFlags = memory->readd(context + 0x54);

    bool handlerEnded = false;
    if (faulted) {
        cpu->nextOp = cpu->getNextOp();
        if (!cpu->nextOp) {
            failed("movsd partial-fault oracle handler decode");
        } else {
            cpu->nextOp->pfn = NormalCPU::getFunctionForOp(cpu->nextOp);
        }
        try {
            if (cpu->nextOp) cpu->run();
            handlerEnded = cpu->nextOp && cpu->nextOp->inst == TestEnd;
        } catch (...) {
            failed("movsd partial-fault oracle handler execution");
        }
    }
    // Restore R|W before any post-fault destination read, then disarm the
    // scope guard so every exit still restores exactly once.
    const bool protectionRestored = memory->mprotect(cpu->thread, protectedPage, K_PAGE_SIZE,
        K_PROT_READ | K_PROT_WRITE) == 0;
    if (!protectionRestored) failed("movsd partial-fault oracle protect restore");
    if (protectionRestored) restore.pageProtected = false;
    cpu->thread->process->sigActions[K_SIGSEGV] = restore.oldSegv;

    const char* name = backward ? "movsd partial-fault oracle backward" : "movsd partial-fault oracle forward";
    if (!faulted || faultException != 1 || !handlerEnded) {
        failed("%s signal delivery", name);
    }
    if (savedSignalInfo[0] != K_SIGSEGV || savedSignalInfo[2] != 2 ||
            savedSignalInfo[3] != secondDestination) {
        failed("%s siginfo", name);
    }
    if (memory->readd(firstDestination) != memory->readd(firstSource) ||
            memory->readd(secondDestination) != initialSecondDestination) {
        failed("%s completed/faulting destination", name);
    }
    if (memory->readd(source - 4) != 0x21222324 || memory->readd(source + 8) != 0x31323334 ||
            memory->readd(backward ? firstDestination + 4 : firstDestination - 4) != 0x6a6b6c6d ||
            memory->readd(backward ? secondDestination - 4 : secondDestination + 4) != 0x7a7b7c7d) {
        failed("%s source/destination guard", name);
    }
    for (U32 i = 0; i < 8; ++i) {
        U32 expected = initialRegs[i];
        if (i == R_SI) expected = firstSource + (backward ? -4 : 4);
        if (i == R_DI) expected = firstDestination + (backward ? -4 : 4);
        if (i == R_CX) expected = 1;
        if (savedRegs[i] != expected) {
            failed("%s saved register %u", name, i);
        }
    }
    if (savedEip != 0 || (savedFlags & FLAG_MASK) != flags) {
        failed("%s saved eip/flags", name);
    }
    printf("Movsd runtime-zero partial-fault NormalCPU %s frame-sig=%u/%u/%08X global-sig=%u/%u/%08X first=%08X second=%08X eip=%08X flags=%08X ecx=%08X esi=%08X edi=%08X throw=%d handler=%u\n",
        backward ? "DF1" : "DF0", savedSignalInfo[0], savedSignalInfo[2], savedSignalInfo[3],
        observedSegv.sigInfo[0], observedSegv.sigInfo[2], observedSegv.sigInfo[3],
        memory->readd(firstDestination), memory->readd(secondDestination), savedEip, savedFlags,
        savedRegs[R_CX], savedRegs[R_SI], savedRegs[R_DI], faultException, handlerEnded ? 1 : 0);
}

#ifdef BOXEDWINE_WASM_JIT_MOVSD_RUNTIME_ZERO_GUARD

void runMovsdRuntimeZeroGuardPartialFaultMrgCase(DecodedOp* op, void* compiledEntry,
                                                 MovsdPartialFaultRestore& restore, bool backward) {
    constexpr U32 handler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 source = TEST_HEAP_ADDRESS + 0x8000;
    constexpr U32 protectedPage = TEST_HEAP_ADDRESS + 0xa000;
    constexpr U32 initialFirstDestination = 0xa1b2c3d4;
    constexpr U32 initialSecondDestination = 0x55667788;
    constexpr U32 initialFlags = CF | PF | AF | ZF | SF | OF;
    const U32 firstSource = source + (backward ? 4 : 0);
    const U32 firstDestination = backward ? protectedPage + K_PAGE_SIZE : protectedPage - 4;
    const U32 secondDestination = backward ? protectedPage + K_PAGE_SIZE - 4 : protectedPage;
    const U32 flags = initialFlags | (backward ? DF : 0);
    const char* name = backward ? "movsd runtime-zero partial-fault MRG DF1" : "movsd runtime-zero partial-fault MRG DF0";

    cpu->setSeg(DS, 0, 0);
    cpu->setSeg(ES, 0, 0);
    if (!cpu->thread->process->hasSetSeg[DS] || cpu->thread->process->hasSetSeg[ES]) {
        failed("%s source-only setup", name);
        return;
    }
    if (op->pfn != cpu->thread->process->startJITOp || op->pfnJitCode != compiledEntry ||
            op->blockStart != op || !(op->flags & OP_FLAG_JIT)) {
        failed("%s pre-run compiled block identity", name);
        return;
    }
    U32 initialRegs[8];
    for (U32 i = 0; i < 8; ++i) {
        initialRegs[i] = 0x11000000 + i;
        cpu->reg[i].u32 = initialRegs[i];
    }
    cpu->reg[R_SP].u32 = 4096;
    cpu->reg[R_SI].u32 = firstSource;
    cpu->reg[R_DI].u32 = firstDestination;
    cpu->reg[R_CX].u32 = 2;
    initialRegs[R_SP] = cpu->reg[R_SP].u32;
    initialRegs[R_SI] = firstSource;
    initialRegs[R_DI] = firstDestination;
    initialRegs[R_CX] = 2;
    memory->writed(source, 0x10203040);
    memory->writed(source + 4, 0x50607080);
    memory->writed(source - 4, 0x21222324);
    memory->writed(source + 8, 0x31323334);
    memory->writed(firstDestination, initialFirstDestination);
    memory->writed(secondDestination, initialSecondDestination);
    memory->writed(backward ? firstDestination + 4 : firstDestination - 4, 0x6a6b6c6d);
    memory->writed(backward ? secondDestination - 4 : secondDestination + 4, 0x7a7b7c7d);
    if (memory->mprotect(cpu->thread, protectedPage, K_PAGE_SIZE, K_PROT_READ)) {
        failed("%s protect setup", name);
        return;
    }
    restore.pageProtected = true;
    cpu->setFlags(flags, FMASK_ALL);
    cpu->eip.u32 = 0;
    cpu->nextOp = op;
    cpu->wasmJitBailout = 0;
    const Seg savedCs = cpu->seg[CS];
    const Seg savedSs = cpu->seg[SS];
    const Seg savedDs = cpu->seg[DS];
    const Seg savedEs = cpu->seg[ES];
    const U32 savedStackMask = cpu->stackMask;
    const U32 savedStackNotMask = cpu->stackNotMask;
    const U32 savedEipBeforeFault = cpu->eip.u32;
    const U32 savedFlagsBeforeFault = cpu->flags;
    const bool savedBig = cpu->big != 0;
    const U64 savedInSigMask = cpu->thread->inSigMask;
    const U32 savedInSignal = cpu->thread->inSignal;
    ::memset(cpu->thread->process->sigActions[K_SIGSEGV].sigInfo, 0,
        sizeof(cpu->thread->process->sigActions[K_SIGSEGV].sigInfo));
#ifdef BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS
    cpu->wasmJitMovsdMrgFixtureSlowWriteCount = 0;
#endif
    bool faulted = false;
    int faultException = -1;
    try {
        cpu->run();
    } catch (int value) {
        faulted = true;
        faultException = value;
    } catch (...) {
        faulted = true;
    }
#ifdef BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS
    const U32 generatedSlowWriteCount = cpu->wasmJitMovsdMrgFixtureSlowWriteCount;
#endif
    const U32 context = TEST_STACK_ADDRESS - K_PAGE_SIZE * 32 + initialRegs[R_SP] - 768;
    const U32 signalInfo = (context & ~15) - 128;
    U32 savedSignalInfo[4] = {memory->readd(signalInfo), memory->readd(signalInfo + 4),
        memory->readd(signalInfo + 8), memory->readd(signalInfo + 12)};
    U32 savedRegs[8] = {};
    savedRegs[R_AX] = memory->readd(context + 0x40);
    savedRegs[R_CX] = memory->readd(context + 0x3c);
    savedRegs[R_DX] = memory->readd(context + 0x38);
    savedRegs[R_BX] = memory->readd(context + 0x34);
    savedRegs[R_SP] = memory->readd(context + 0x30);
    savedRegs[R_BP] = memory->readd(context + 0x2c);
    savedRegs[R_SI] = memory->readd(context + 0x28);
    savedRegs[R_DI] = memory->readd(context + 0x24);
    const U32 savedEip = memory->readd(context + 0x4c);
    const U32 savedFlags = memory->readd(context + 0x54);
    // The Wasm helper's runNextSingleOp boundary may consume the host throw
    // after runSignal has installed the guest handler. Guest EIP, not C++
    // exception transport, is the durable delivery proof here.
    const bool handlerReached = cpu->eip.u32 == handler;
    bool handlerEnded = false;
    if (handlerReached) {
        cpu->nextOp = cpu->getNextOp();
        if (cpu->nextOp) {
            cpu->nextOp->pfn = NormalCPU::getFunctionForOp(cpu->nextOp);
            try {
                cpu->run();
                handlerEnded = cpu->nextOp && cpu->nextOp->inst == TestEnd;
            } catch (...) {
                failed("%s handler execution", name);
            }
        } else {
            failed("%s handler decode", name);
        }
    }
    const bool protectionRestored = memory->mprotect(cpu->thread, protectedPage, K_PAGE_SIZE,
        K_PROT_READ | K_PROT_WRITE) == 0;
    if (!protectionRestored) failed("%s protect restore", name);
    if (protectionRestored) restore.pageProtected = false;
    // TestEnd is not sigreturn. Restore the exact non-signal execution state
    // without calling newInstruction (which would clear the published B5).
    cpu->setSegment(CS, savedCs.value);
    cpu->setSegment(SS, savedSs.value);
    cpu->setSegment(DS, savedDs.value);
    cpu->setSegment(ES, savedEs.value);
    cpu->stackMask = savedStackMask;
    cpu->stackNotMask = savedStackNotMask;
    cpu->setIsBig(savedBig ? 1 : 0);
    cpu->eip.u32 = savedEipBeforeFault;
    cpu->setFlags(savedFlagsBeforeFault, FMASK_ALL);
    cpu->thread->inSigMask = savedInSigMask;
    cpu->thread->inSignal = savedInSignal;
    cpu->nextOp = op;
    if (!protectionRestored) return;
    printf("Movsd runtime-zero partial-fault MRG %s frame-sig=%u/%u/%08X eip=%08X flags=%08X ecx=%08X esi=%08X edi=%08X throw=%d reached=%u handler=%u bailout=%u\n",
        backward ? "DF1" : "DF0", savedSignalInfo[0], savedSignalInfo[2], savedSignalInfo[3],
        savedEip, savedFlags, savedRegs[R_CX], savedRegs[R_SI], savedRegs[R_DI],
        faultException, handlerReached ? 1 : 0, handlerEnded ? 1 : 0, cpu->wasmJitBailout);
#ifdef BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS
    printf("Movsd runtime-zero partial-fault MRG %s generated-slow-write=%u\n",
        backward ? "DF1" : "DF0", generatedSlowWriteCount);
    if (generatedSlowWriteCount != 1) {
        failed("%s generated slow-write", name);
    }
#endif
    if ((faulted && faultException != 1) || !handlerReached || !handlerEnded || savedSignalInfo[0] != K_SIGSEGV ||
            savedSignalInfo[2] != 2 || savedSignalInfo[3] != secondDestination) {
        failed("%s signal frame", name);
    }
    if (memory->readd(firstDestination) != memory->readd(firstSource) ||
            memory->readd(secondDestination) != initialSecondDestination ||
            memory->readd(source - 4) != 0x21222324 || memory->readd(source + 8) != 0x31323334 ||
            memory->readd(backward ? firstDestination + 4 : firstDestination - 4) != 0x6a6b6c6d ||
            memory->readd(backward ? secondDestination - 4 : secondDestination + 4) != 0x7a7b7c7d) {
        failed("%s bytes/guards", name);
    }
    for (U32 i = 0; i < 8; ++i) {
        U32 expected = initialRegs[i];
        if (i == R_SI) expected = firstSource + (backward ? -4 : 4);
        if (i == R_DI) expected = firstDestination + (backward ? -4 : 4);
        if (i == R_CX) expected = 1;
        if (savedRegs[i] != expected) failed("%s saved register %u", name, i);
    }
    if (savedEip != 0 || (savedFlags & FLAG_MASK) != flags) failed("%s saved eip/flags", name);
    if (cpu->wasmJitBailout != 0 || op->pfn != cpu->thread->process->startJITOp ||
            op->pfnJitCode != compiledEntry || op->blockStart != op) {
        failed("%s compiled block identity", name);
    }
}

#endif

#endif

void initPageBytes(U8* values, size_t count, U8 seed) {
    for (size_t i = 0; i < count; ++i) {
        values[i] = (U8)(seed + ((i * 13 + 7) & 0x7f));
    }
}

void writePageBytes(U32 offset, const U8* values, size_t count) {
    U32 base = TEST_HEAP_ADDRESS + offset;
    memory->writeb(base - 1, GUARD_BEFORE);
    for (size_t i = 0; i < count; ++i) {
        memory->writeb(base + (U32)i, values[i]);
    }
    memory->writeb(base + (U32)count, GUARD_AFTER);
}

void verifyPageBytes(U32 offset, const U8* expected, size_t count, const char* name) {
    U32 base = TEST_HEAP_ADDRESS + offset;
    if (memory->readb(base - 1) != GUARD_BEFORE || memory->readb(base + (U32)count) != GUARD_AFTER) {
        failed("%s page guard", name);
    }
    for (size_t i = 0; i < count; ++i) {
        if (memory->readb(base + (U32)i) != expected[i]) {
            failed("%s page byte", name);
        }
    }
}

U32 pageLinear(U32 value, bool address32) {
    return TEST_HEAP_ADDRESS + indexOffset(value, address32);
}

void setPageCompareData(StringOp op, int width, U8 prefix, U32 eax, U8* src, U8* dst, U32 count) {
    for (U32 i = 0; i < count; ++i) {
        U32 offset = i * width;
        if (op == STRING_SCAS) {
            writeCaseValue(dst, offset, width, accumulatorForWidth(eax, width));
            if (prefix == PREFIX_REPNE || prefix == 0) {
                dst[offset] = (U8)(dst[offset] + 1);
            }
        } else {
            for (int j = 0; j < width; ++j) {
                dst[offset + j] = src[offset + j];
            }
            if (prefix == PREFIX_REPNE || prefix == 0) {
                dst[offset] = (U8)(dst[offset] + 1);
            }
        }
    }
}

void simulatePageBoundary(StringOp op, int width, bool address32, U8 prefix, U32& expectedFlags, U32* expectedRegs) {
    bool compareOp = op == STRING_CMPS || op == STRING_SCAS;
    bool repeat = prefix == PREFIX_REPNE || prefix == PREFIX_REPE;
    U32 remaining = repeat ? countValue(expectedRegs[R_CX], address32) : 1;

    while (remaining) {
        U32 srcValue = readMemoryValue(pageLinear(expectedRegs[R_SI], address32), width);
        U32 dstValue = readMemoryValue(pageLinear(expectedRegs[R_DI], address32), width);
        if (op == STRING_MOVS) {
            writeMemoryValue(pageLinear(expectedRegs[R_DI], address32), width, srcValue);
        } else if (op == STRING_CMPS) {
            U32 result = (srcValue - dstValue) & widthMask(width * 8);
            expectedFlags = (expectedFlags & ~ARITH_FLAG_MASK) | subFlags(srcValue, dstValue, result, width * 8);
        } else if (op == STRING_STOS) {
            writeMemoryValue(pageLinear(expectedRegs[R_DI], address32), width, accumulatorForWidth(expectedRegs[R_AX], width));
        } else if (op == STRING_LODS) {
            setAccumulator(expectedRegs[R_AX], width, srcValue);
        } else {
            U32 acc = accumulatorForWidth(expectedRegs[R_AX], width);
            U32 result = (acc - dstValue) & widthMask(width * 8);
            expectedFlags = (expectedFlags & ~ARITH_FLAG_MASK) | subFlags(acc, dstValue, result, width * 8);
        }

        if (op == STRING_MOVS || op == STRING_CMPS || op == STRING_LODS) {
            expectedRegs[R_SI] = updateIndex(expectedRegs[R_SI], width, address32);
        }
        if (op == STRING_MOVS || op == STRING_CMPS || op == STRING_STOS || op == STRING_SCAS) {
            expectedRegs[R_DI] = updateIndex(expectedRegs[R_DI], width, address32);
        }

        if (repeat) {
            --remaining;
            setCountValue(expectedRegs[R_CX], remaining, address32);
            if (compareOp) {
                bool zf = (expectedFlags & ZF) != 0;
                if ((prefix == PREFIX_REPE && !zf) || (prefix == PREFIX_REPNE && zf)) {
                    break;
                }
            }
        } else {
            break;
        }
    }
}

void runPageBoundaryCase(StringOp op, int width, bool address32, U8 prefix, const char* name) {
    constexpr U32 initialFlags = CF | PF | AF | ZF | SF | OF;
    newInstruction(initialFlags);
    cpu->big = address32 ? 1 : 0;
    cpu->seg[ES].address = TEST_HEAP_ADDRESS;
    cpu->seg[ES].value = TEST_HEAP_SEG;
    cpu->thread->process->hasSetSeg[ES] = true;

    U32 count = prefix ? 3 : 1;
    size_t dataSize = count * width;
    U32 eax = 0x6d5c3b2a;
    U8 src[PAGE_TEST_SIZE];
    U8 dst[PAGE_TEST_SIZE];
    U8 expectedSrc[PAGE_TEST_SIZE];
    U8 expectedDst[PAGE_TEST_SIZE];
    initPageBytes(src, dataSize, 0x10);
    initPageBytes(dst, dataSize, 0x70);
    if (op == STRING_CMPS || op == STRING_SCAS) {
        setPageCompareData(op, width, prefix, eax, src, dst, count);
    }
    copyBytes(expectedSrc, src, dataSize);
    copyBytes(expectedDst, dst, dataSize);

    writePageBytes(PAGE_SRC_BASE, src, dataSize);
    writePageBytes(PAGE_DST_BASE, dst, dataSize);

    U32 expectedRegs[8];
    initRegisters(expectedRegs, eax, PAGE_SRC_BASE, PAGE_DST_BASE, prefix ? count : REG_GUARD);
    U32 expectedFlags = initialFlags;
    emitCode(op, width, prefix);
    simulatePageBoundary(op, width, address32, prefix, expectedFlags, expectedRegs);
    for (size_t i = 0; i < dataSize; ++i) {
        expectedDst[i] = memory->readb(TEST_HEAP_ADDRESS + PAGE_DST_BASE + (U32)i);
    }
    for (size_t i = 0; i < dataSize; ++i) {
        memory->writeb(TEST_HEAP_ADDRESS + PAGE_SRC_BASE + (U32)i, src[i]);
        memory->writeb(TEST_HEAP_ADDRESS + PAGE_DST_BASE + (U32)i, dst[i]);
    }

    runTestCPU();

    verifyRegisters(cpu, expectedRegs, name);
    verifyPageBytes(PAGE_SRC_BASE, expectedSrc, dataSize, name);
    verifyPageBytes(PAGE_DST_BASE, expectedDst, dataSize, name);
    if ((actualFlags(cpu, true) & FLAG_MASK) != (expectedFlags & FLAG_MASK)) {
        failed("%s page flags", name);
    }
}

void runPageBoundaryCases(StringOp op, int width, bool address32) {
    static const U8 prefixes[] = {0, PREFIX_REPE, PREFIX_REPNE};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        runPageBoundaryCase(op, width, address32, prefixes[i], "string page boundary");
    }
}

const StringCase MOVE_CASES[] = {
    {0, 0, 1, false, 0x89abcdef, {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string move once"},
    {0, DF, 1, true, 0x89abcdef, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string move backward"},
    {PREFIX_REPE, CF | OF, 4, false, 0x89abcdef, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0}, {0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0, 0, 0, 0, 0, 0, 0, 0}, 16, "string move rep"},
    {PREFIX_REPNE, DF | SF, 0, false, 0x89abcdef, {0x71, 0x72, 0x73, 0x74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0xd1, 0xd2, 0xd3, 0xd4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string move zero count"}
};

const StringCase COMPARE_CASES[] = {
    {0, 0, 1, false, 0x89abcdef, {0x20, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x10, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string compare greater"},
    {0, DF, 1, true, 0x89abcdef, {0x10, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x20, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string compare less backward"},
    {PREFIX_REPE, OF, 4, false, 0x89abcdef, {0x31, 0x00, 0x32, 0x00, 0x33, 0x00, 0x34, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x31, 0x00, 0x32, 0x00, 0x30, 0x00, 0x34, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string compare repe stops"},
    {PREFIX_REPNE, CF, 4, false, 0x89abcdef, {0x01, 0x00, 0x02, 0x00, 0x44, 0x00, 0x05, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x10, 0x00, 0x20, 0x00, 0x44, 0x00, 0x50, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string compare repne stops"},
    {PREFIX_REPE, CF | PF | DF, 0, false, 0x89abcdef, {0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string compare zero count"}
};

const StringCase STORE_CASES[] = {
    {0, 0, 1, false, 0x12345678, {0x10, 0x20, 0x30, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string store once"},
    {0, DF, 1, true, 0x87654321, {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string store backward"},
    {PREFIX_REPE, SF | OF, 4, false, 0xa1b2c3d4, {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0, 0, 0, 0, 0, 0, 0, 0}, 16, "string store rep"},
    {PREFIX_REPNE, DF | AF, 0, false, 0xaabbccdd, {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x90, 0x91, 0x92, 0x93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string store zero count"}
};

const StringCase LOAD_CASES[] = {
    {0, 0, 1, false, 0x12345678, {0xaa, 0xbb, 0xcc, 0xdd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x10, 0x11, 0x12, 0x13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string load once"},
    {0, DF, 1, true, 0x87654321, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x20, 0x21, 0x22, 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string load backward"},
    {PREFIX_REPE, PF | AF, 4, false, 0xaabbccdd, {0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44, 0x00, 0x45, 0x00, 0x46, 0x00, 0, 0, 0, 0, 0, 0, 0, 0}, {0x30, 0x31, 0x32, 0x33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16, "string load rep"},
    {PREFIX_REPNE, CF | OF | DF, 0, false, 0x55667788, {0x51, 0x52, 0x53, 0x54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x60, 0x61, 0x62, 0x63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string load zero count"}
};

const StringCase SCAN_CASES[] = {
    {0, 0, 1, false, 0x00000030, {0x20, 0x21, 0x22, 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x20, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string scan greater"},
    {0, DF, 1, true, 0x00000010, {0x10, 0x11, 0x12, 0x13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x20, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string scan less backward"},
    {PREFIX_REPE, OF, 4, false, 0x00000044, {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x44, 0x00, 0x44, 0x00, 0x45, 0x00, 0x44, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string scan repe stops"},
    {PREFIX_REPNE, CF, 4, false, 0x00000055, {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x10, 0x00, 0x20, 0x00, 0x55, 0x00, 0x60, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 12, "string scan repne stops"},
    {PREFIX_REPE, CF | PF | DF, 0, false, 0x00000011, {0x01, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 8, "string scan zero count"}
};

template <typename T, size_t N>
size_t caseCount(const T(&)[N]) {
    return N;
}

} // namespace

void testMovsb_0x0a4() {
    runStringCases(STRING_MOVS, 1, false, MOVE_CASES, caseCount(MOVE_CASES));
    runOverlapMovsCases(1, false);
    runHotMovsCases(1, false);
    runPageBoundaryCases(STRING_MOVS, 1, false);
}

void testMovsb_0x2a4() {
    runStringCases(STRING_MOVS, 1, true, MOVE_CASES, caseCount(MOVE_CASES));
    runOverlapMovsCases(1, true);
    runHotMovsCases(1, true);
    runPageBoundaryCases(STRING_MOVS, 1, true);
}

void testMovsw_0x0a5() {
    runStringCases(STRING_MOVS, 2, false, MOVE_CASES, caseCount(MOVE_CASES));
    runOverlapMovsCases(2, false);
    runHotMovsCases(2, false);
    runPageBoundaryCases(STRING_MOVS, 2, false);
}

void testMovsd_0x2a5() {
    runStringCases(STRING_MOVS, 4, true, MOVE_CASES, caseCount(MOVE_CASES));
    runOverlapMovsCases(4, true);
    runHotMovsCases(4, true);
    runPageBoundaryCases(STRING_MOVS, 4, true);
}

void testMovsdRuntimeZeroGuard() {
#ifdef BOXEDWINE_WASM_JIT_MOVSD_RUNTIME_ZERO_GUARD
    newInstruction(0);
    constexpr U32 partialFaultHandler = TEST_CODE_ADDRESS + 0x300;
    constexpr U32 partialFaultProtectedPage = TEST_HEAP_ADDRESS + 0xa000;
    // Install the endpoint and action before the B5 block can be decoded or
    // published. No code-page writes are permitted after publication.
    memory->writeb(partialFaultHandler, 0xcd);
    memory->writeb(partialFaultHandler + 1, 0x97);
    memory->clearPageWriteCounts(partialFaultHandler >> K_PAGE_SHIFT);
    MovsdPartialFaultRestore partialFaultRestore{cpu->thread->process, memory, cpu->thread,
        cpu->thread->process->sigActions[K_SIGSEGV], partialFaultProtectedPage};
    cpu->thread->process->sigActions[K_SIGSEGV] = partialFaultRestore.oldSegv;
    cpu->thread->process->sigActions[K_SIGSEGV].handlerAndSigAction = partialFaultHandler;
    cpu->thread->process->sigActions[K_SIGSEGV].flags = K_SA_SIGINFO;
    // Preserve the observed source-only B5 route: DS is sticky, ES is not.
    // CPU::setSeg intentionally leaves hasSetSeg sticky after the live base is
    // cleared, so do not seed ES before this block is compiled.
    cpu->setSeg(DS, TEST_HEAP_ADDRESS, TEST_HEAP_SEG);
    cpu->setSeg(DS, 0, 0);
    if (!cpu->thread->process->hasSetSeg[DS] || cpu->thread->process->hasSetSeg[ES]) {
        failed("movsd runtime-zero guard source-only setup");
        return;
    }
    pushCode8(PREFIX_REPE);
    pushCode8(0xa5); // MOVSD, source DS and destination ES
    runTestCPU();

    DecodedOp* op = memory->getDecodedOp(TEST_CODE_ADDRESS);
    if (!op || op->base != DS || op->pfn != cpu->thread->process->startJITOp ||
            !op->pfnJitCode || !(op->flags & OP_FLAG_JIT) || op->blockStart != op) {
        failed("movsd runtime-zero guard initial compiled block");
        return;
    }
    void* compiledEntry = op->pfnJitCode;
    static const MovsdRuntimeZeroGuardProfile profiles[] = {
        {0, 0, "movsd runtime-zero source-only both-zero"},
        {TEST_HEAP_ADDRESS, 0, "movsd runtime-zero DS-nonzero"},
        {0, 0, "movsd runtime-zero source-only return-both-zero"},
        {0, TEST_HEAP_ADDRESS, "movsd runtime-zero ES-nonzero"},
        {TEST_HEAP_ADDRESS, TEST_HEAP_ADDRESS, "movsd runtime-zero both-nonzero"},
        {0, 0, "movsd runtime-zero post-ES return-both-zero"},
    };
    // __TEST publishes on the first hit, unlike production. Exercise the same
    // installed block past the production threshold before changing bases.
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profiles[0], false, false);
    }
    // Both rows run before any ES mutation, retaining the measured source-only
    // DS-sticky B5 route on this exact compiled entry.
    runMovsdRuntimeZeroGuardPartialFaultMrgCase(op, compiledEntry, partialFaultRestore, false);
    runMovsdRuntimeZeroGuardPartialFaultMrgCase(op, compiledEntry, partialFaultRestore, true);
    printf("Movsd runtime-zero source-only same-entry partial-fault matrix ... OK\n");
    // This must run before any ES-nonzero profile makes its process marker
    // sticky; the MOC3 source-only route is part of this page-cross proof.
    runMovsdRuntimeZeroGuardPageCrossCase(op, compiledEntry, false);
    runMovsdRuntimeZeroGuardPageCrossCase(op, compiledEntry, true);
    printf("Movsd runtime-zero source-only same-entry page-cross matrix ... OK\n");
    for (const auto& profile : profiles) {
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profile, false, false);
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profile, true, false);
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profile, false, true);
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profile, true, true);
    }
    // Keep the observed zero-base B5 arm on the exact compiled entry across
    // the byte-sized helper boundary and a count that exceeds it. A nonzero
    // DS representative confirms the exact-emulate branch at both extremes.
    static const U32 boundaryCounts[] = {0, 1, 255, 256, 300};
    for (U32 count : boundaryCounts) {
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profiles[0], false, false, count);
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profiles[0], true, false, count);
    }
    static const U32 nonzeroBoundaryCounts[] = {1, 300};
    for (U32 count : nonzeroBoundaryCounts) {
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profiles[1], false, false, count);
        runMovsdRuntimeZeroGuardCase(op, compiledEntry, profiles[1], true, false, count);
    }
    printf("Movsd runtime-zero ECX boundary matrix ... OK\n");
#endif
}

void testMovsdRuntimeZeroGuardFaultOracle() {
#ifdef BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_FAULT_ORACLE
    runMovsdRuntimeZeroGuardPartialFaultOracleCase(false);
    runMovsdRuntimeZeroGuardPartialFaultOracleCase(true);
#endif
}

#if defined(BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_SMC_ORACLE) && defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_SMC_FIXTURE)
#error "MRGN4 NormalCPU oracle and MRGF4 Wasm SMC fixture must be separate builds"
#endif
#if defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_SMC_FIXTURE) && defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS)
#error "MRGF4 SMC fixture and MRGF3 partial-fault census must be separate builds"
#endif
#if defined(BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_SMC_ORACLE) && defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS)
#error "MRGN4 NormalCPU oracle and MRGF3 partial-fault census must be separate builds"
#endif
#if defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_EA16_SMC_FIXTURE) && (defined(BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_SMC_ORACLE) || defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_SMC_FIXTURE) || defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_FIXTURE_CENSUS))
#error "MRGF5 EA16 helper fixture must be separate from MRGN4/MRGF3/MRGF4"
#endif

#if defined(BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_SMC_ORACLE) || defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_SMC_FIXTURE)
namespace {

constexpr U32 MOVSD_SMC_SOURCE = TEST_HEAP_ADDRESS + 0x180;
constexpr U32 MOVSD_SMC_FLAGS = CF | PF | AF | ZF | SF | OF;
// The explicit DS override makes this a real source-only-sticky-DS B5 row.
// Prefix-less A5 decodes with base ES and cannot exercise the guard at all.
constexpr U8 MOVSD_SMC_INITIAL_CODE[] = {0x3e, 0xf3, 0xa5, 0x41, 0xcd, 0x97, 0x90};
constexpr U8 MOVSD_SMC_MUTATED_CODE[] = {0x3e, 0xf3, 0xa5, 0x40, 0xcd, 0x97, 0x90};
constexpr U32 MOVSD_SMC_SOURCE_DWORD = 0x9097cd40;
constexpr U32 MOVSD_SMC_EAX = 0x10203040;

void setupMovsdSmcProgram() {
    newInstruction(MOVSD_SMC_FLAGS);
    // Make the source-only B5 selection sticky before the root is decoded;
    // ES remains untouched, with both live bases zero for the actual row.
    cpu->setSeg(DS, TEST_HEAP_ADDRESS, TEST_HEAP_SEG);
    cpu->setSeg(DS, 0, 0);
    if (!cpu->thread->process->hasSetSeg[DS] || cpu->thread->process->hasSetSeg[ES]) {
        failed("movsd SMC source-only sticky setup");
        return;
    }
    for (U8 byte : MOVSD_SMC_INITIAL_CODE) {
        pushCode8(byte);
    }
    // All bytes, including the payload which becomes the successor, are in
    // place before getDecodedOp can publish any code-page ownership.
    memory->writed(MOVSD_SMC_SOURCE, MOVSD_SMC_SOURCE_DWORD);
}

void setMovsdSmcRegisters(U32 count, bool backward = false, bool segmentedSource = false) {
    U32 ignored[8];
    initRegisters(ignored, MOVSD_SMC_EAX,
        segmentedSource ? (MOVSD_SMC_SOURCE - TEST_HEAP_ADDRESS) : MOVSD_SMC_SOURCE,
        TEST_CODE_ADDRESS + 3, count);
    cpu->setFlags(MOVSD_SMC_FLAGS | (backward ? DF : 0), FMASK_ALL);
    cpu->eip.u32 = 0;
    cpu->wasmJitActiveBlock = nullptr;
    cpu->wasmJitBailout = 0;
}

void printMovsdSmcSnapshot(const char* tag) {
    printf("Movsd SMC %s regs=%08X/%08X/%08X/%08X/%08X/%08X/%08X/%08X flags=%08X eip=%08X next=%u bases=DS:%08X/ES:%08X code=%02X%02X%02X%02X%02X%02X%02X source=%08X\n",
        tag,
        cpu->reg[0].u32, cpu->reg[1].u32, cpu->reg[2].u32, cpu->reg[3].u32,
        cpu->reg[4].u32, cpu->reg[5].u32, cpu->reg[6].u32, cpu->reg[7].u32,
        cpu->flags, cpu->eip.u32, cpu->nextOp ? (U32)cpu->nextOp->inst : 0xffffffff,
        cpu->seg[DS].address, cpu->seg[ES].address,
        memory->readb(TEST_CODE_ADDRESS), memory->readb(TEST_CODE_ADDRESS + 1),
        memory->readb(TEST_CODE_ADDRESS + 2), memory->readb(TEST_CODE_ADDRESS + 3),
        memory->readb(TEST_CODE_ADDRESS + 4), memory->readb(TEST_CODE_ADDRESS + 5),
        memory->readb(TEST_CODE_ADDRESS + 6),
        memory->readd(MOVSD_SMC_SOURCE));
}

bool verifyMovsdSmcFinal(const char* tag, U32 expectedEip, bool backward = false,
                         bool segmentedSource = false) {
    bool ok = true;
    for (U32 i = 0; i < sizeof(MOVSD_SMC_MUTATED_CODE); ++i) {
        if (memory->readb(TEST_CODE_ADDRESS + i) != MOVSD_SMC_MUTATED_CODE[i]) {
            failed("movsd SMC %s mutated code", tag);
            ok = false;
            break;
        }
    }
    if (cpu->reg[R_AX].u32 != MOVSD_SMC_EAX + 1 || cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != (segmentedSource ? (MOVSD_SMC_SOURCE - TEST_HEAP_ADDRESS) : MOVSD_SMC_SOURCE) + (backward ? -4 : 4) ||
            cpu->reg[R_DI].u32 != TEST_CODE_ADDRESS + (backward ? -1 : 7) || cpu->eip.u32 != expectedEip ||
            (cpu->flags & FMASK_ALL) != (MOVSD_SMC_FLAGS | (backward ? DF : 0)) ||
            !cpu->nextOp || cpu->nextOp->inst != TestEnd ||
            memory->readd(MOVSD_SMC_SOURCE) != MOVSD_SMC_SOURCE_DWORD) {
        printMovsdSmcSnapshot(tag);
        failed("movsd SMC %s final architectural state", tag);
        ok = false;
    }
    return ok;
}

} // namespace
#endif

#ifdef BOXEDWINE_WASM_JIT_MOVSD_MRG_EA16_SMC_FIXTURE
namespace {
constexpr U32 MOVSD_EA16_SMC_SOURCE = TEST_HEAP_ADDRESS + 0x180;
constexpr U32 MOVSD_EA16_SMC_FLAGS = CF | PF | AF | ZF | SF | OF;
constexpr U8 MOVSD_EA16_SMC_INITIAL[] = {0x3e, 0x67, 0xf3, 0xa5, 0x41, 0xcd, 0x97, 0x90};
constexpr U8 MOVSD_EA16_SMC_MUTATED[] = {0x3e, 0x67, 0xf3, 0xa5, 0x40, 0xcd, 0x97, 0x90};

void setupMovsdEa16Smc() {
    newInstruction(MOVSD_EA16_SMC_FLAGS);
    cpu->setSeg(DS, TEST_HEAP_ADDRESS, TEST_HEAP_SEG);
    cpu->setSeg(ES, TEST_CODE_ADDRESS, TEST_HEAP_SEG);
    for (U8 byte : MOVSD_EA16_SMC_INITIAL) pushCode8(byte);
    memory->writed(MOVSD_EA16_SMC_SOURCE, 0x9097cd40);
}

void setMovsdEa16Regs(U16 cx) {
    U32 ignored[8];
    initRegisters(ignored, 0x10203040, 0xa55a0180, 0xd0000004, 0xa55a0000 | cx);
    cpu->setFlags(MOVSD_EA16_SMC_FLAGS, FMASK_ALL);
    cpu->eip.u32 = 0;
    cpu->wasmJitActiveBlock = nullptr;
    cpu->wasmJitBailout = 0;
}

bool verifyMovsdEa16State(U32 eip, bool completed) {
    const U32 expectedEax = 0x10203040 + (completed ? 1 : 0);
    const U32 expectedCx = 0xa55a0000;
    const U32 expectedSi = 0xa55a0184;
    const U32 expectedDi = 0xd0000008;
    if (cpu->reg[R_AX].u32 != expectedEax || cpu->reg[R_CX].u32 != expectedCx ||
            cpu->reg[R_SI].u32 != expectedSi || cpu->reg[R_DI].u32 != expectedDi ||
            cpu->eip.u32 != eip || (cpu->flags & FMASK_ALL) != MOVSD_EA16_SMC_FLAGS) return false;
    for (U32 i = 0; i < sizeof(MOVSD_EA16_SMC_MUTATED); ++i)
        if (memory->readb(TEST_CODE_ADDRESS + i) != MOVSD_EA16_SMC_MUTATED[i]) return false;
    return memory->readd(MOVSD_EA16_SMC_SOURCE) == 0x9097cd40;
}
} // namespace
#endif

void testMovsdRuntimeZeroGuardSmcRedecode() {
#ifdef BOXEDWINE_MOVSD_RUNTIME_ZERO_GUARD_SMC_ORACLE
    for (U32 direction = 0; direction < 2; ++direction) {
    const bool backward = direction != 0;
    const char* postTag = backward ? "MRGN4 DF1 post-write" : "MRGN4 DF0 post-write";
    const char* finalTag = backward ? "MRGN4 DF1 oracle" : "MRGN4 DF0 oracle";
    setupMovsdSmcProgram();
    // getDecodedOp is lookup-only; establish this private program's root via
    // the CPU decoder before asserting the source-segment route.
    DecodedOp* root = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!root || root->base != DS) {
        failed("movsd SMC oracle root decode");
        return;
    }
    setMovsdSmcRegisters(1, backward);
    cpu->nextOp = root;
    // A normal direct-dispatch NEXT would follow root->next, but the active
    // CodePage write replaces that old tail with Done.  This single-op route
    // copies the live root and re-decodes only after MOVSD has completed.
    cpu->runNextSingleOp();
    DecodedOp* freshSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!freshSuccessor || cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != MOVSD_SMC_SOURCE + (backward ? -4 : 4) ||
            cpu->reg[R_DI].u32 != TEST_CODE_ADDRESS + (backward ? -1 : 7) || cpu->eip.u32 != 3 ||
            (cpu->flags & FMASK_ALL) != (MOVSD_SMC_FLAGS | (backward ? DF : 0))) {
        printf("Movsd SMC MRGN4 %s post-write fresh=%p ecx=%08X esi=%08X edi=%08X flags=%08X eip=%08X\n",
            backward ? "DF1" : "DF0", freshSuccessor, cpu->reg[R_CX].u32,
            cpu->reg[R_SI].u32, cpu->reg[R_DI].u32, cpu->flags, cpu->eip.u32);
        failed("movsd SMC MRGN4 mutation/redecode state");
        return;
    }
    printMovsdSmcSnapshot(postTag);
    cpu->nextOp = freshSuccessor;
    cpu->run();
    if (verifyMovsdSmcFinal(finalTag, 4, backward)) {
        printMovsdSmcSnapshot(finalTag);
        printf("Movsd SMC MRGN4 %s NormalCPU oracle ... OK\n", backward ? "DF1" : "DF0");
    }
    }
    // Publish with the zero DS base, then switch only the live base. This is
    // the exact NormalCPU oracle for MRGF4's runtime nonzero-base fallback.
    setupMovsdSmcProgram();
    DecodedOp* fallbackRoot = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!fallbackRoot || fallbackRoot->base != DS) {
        failed("movsd SMC MRGN4 fallback root decode");
        return;
    }
    cpu->setSeg(DS, TEST_HEAP_ADDRESS, TEST_HEAP_SEG);
    setMovsdSmcRegisters(1, false, true);
    cpu->nextOp = fallbackRoot;
    cpu->runNextSingleOp();
    DecodedOp* fallbackSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!fallbackSuccessor || cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != MOVSD_SMC_SOURCE - TEST_HEAP_ADDRESS + 4 ||
            cpu->reg[R_DI].u32 != TEST_CODE_ADDRESS + 7 || cpu->eip.u32 != 3 ||
            (cpu->flags & FMASK_ALL) != MOVSD_SMC_FLAGS) {
        failed("movsd SMC MRGN4 fallback mutation/redecode state");
        return;
    }
    printMovsdSmcSnapshot("MRGN4 fallback post-write");
    cpu->nextOp = fallbackSuccessor;
    cpu->run();
    if (verifyMovsdSmcFinal("MRGN4 fallback oracle", 4, false, true)) {
        printMovsdSmcSnapshot("MRGN4 fallback oracle");
        printf("Movsd SMC MRGN4 nonzero-base NormalCPU oracle ... OK\n");
    }
#elif defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_SMC_FIXTURE)
    for (U32 direction = 0; direction < 2; ++direction) {
    const bool backward = direction != 0;
    const char* postTag = backward ? "MRGF4 DF1 post-write" : "MRGF4 DF0 post-write";
    const char* finalTag = backward ? "MRGF4 DF1 fixture" : "MRGF4 DF0 fixture";
    setupMovsdSmcProgram();
    // getDecodedOp is lookup-only; establish this private program's root via
    // the CPU decoder before warming its generated block.
    DecodedOp* root = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!root || root->base != DS) {
        failed("movsd SMC MRGF4 root decode");
        return;
    }
    // Warm only ECX=0: it publishes the exact B5 root without changing code
    // or executing the generated CodePage write that this row is measuring.
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        setMovsdSmcRegisters(0, backward);
        cpu->nextOp = root;
        cpu->run();
        if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
            failed("movsd SMC MRGF4 warm control flow");
            return;
        }
    }
    if (root->blockOpCount < 2 || root->pfn != cpu->thread->process->startJITOp ||
            !root->pfnJitCode || root->blockStart != root) {
        failed("movsd SMC MRGF4 warmed compiled root");
        return;
    }
    DecodedOp* originalSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!originalSuccessor || root->next != originalSuccessor ||
            originalSuccessor->blockStart != root) {
        failed("movsd SMC MRGF4 original successor ownership");
        return;
    }
    // Do not dereference originalSuccessor after this point: MOVSD's checked
    // write invalidates the active root and frees every old decoded op.
    setMovsdSmcRegisters(1, backward);
    cpu->wasmJitMovsdSmcBailoutTakenCount = 0;
    cpu->wasmJitMovsdSmcCheckedWriteCount = 0;
    cpu->nextOp = root;
    cpu->run();
    DecodedOp* freshSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!freshSuccessor || cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != MOVSD_SMC_SOURCE + (backward ? -4 : 4) ||
            cpu->reg[R_DI].u32 != TEST_CODE_ADDRESS + (backward ? -1 : 7) || cpu->eip.u32 != 3 ||
            (cpu->flags & FMASK_ALL) != (MOVSD_SMC_FLAGS | (backward ? DF : 0)) ||
            cpu->wasmJitActiveBlock || cpu->wasmJitBailout != 0 ||
            cpu->wasmJitMovsdSmcBailoutTakenCount != 1 ||
            cpu->wasmJitMovsdSmcCheckedWriteCount != 1) {
        printf("Movsd SMC MRGF4 post-write fresh=%p ecx=%08X esi=%08X edi=%08X eip=%08X active=%p bailout=%u checked=%u taken=%u\n",
            freshSuccessor, cpu->reg[R_CX].u32, cpu->reg[R_SI].u32, cpu->reg[R_DI].u32,
            cpu->eip.u32, cpu->wasmJitActiveBlock, cpu->wasmJitBailout,
            cpu->wasmJitMovsdSmcCheckedWriteCount, cpu->wasmJitMovsdSmcBailoutTakenCount);
        failed("movsd SMC MRGF4 generated bailout/redecode state");
        return;
    }
    for (U32 i = 0; i < sizeof(MOVSD_SMC_MUTATED_CODE); ++i) {
        if (memory->readb(TEST_CODE_ADDRESS + i) != MOVSD_SMC_MUTATED_CODE[i]) {
            failed("movsd SMC MRGF4 generated checked write");
            return;
        }
    }
    printMovsdSmcSnapshot(postTag);
    cpu->nextOp = freshSuccessor;
    cpu->run();
    if (verifyMovsdSmcFinal(finalTag, 4, backward)) {
        printMovsdSmcSnapshot(finalTag);
        printf("Movsd SMC MRGF4 generated checked-write=%u bailout-taken=%u\n",
            cpu->wasmJitMovsdSmcCheckedWriteCount, cpu->wasmJitMovsdSmcBailoutTakenCount);
        printf("Movsd SMC MRGF4 %s warmed bailout/re-decode ... OK\n", backward ? "DF1" : "DF0");
    }
    }
    // Compile the guarded route with DS=0, then change only its live base.
    // The generated body must select emulateSingleOp(), never its inline
    // MOVSR checked-write arm, while retaining exact SMC re-decode behavior.
    setupMovsdSmcProgram();
    DecodedOp* fallbackRoot = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!fallbackRoot || fallbackRoot->base != DS) {
        failed("movsd SMC MRGF4 fallback root decode");
        return;
    }
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        setMovsdSmcRegisters(0);
        cpu->nextOp = fallbackRoot;
        cpu->run();
        if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) {
            failed("movsd SMC MRGF4 fallback warm control flow");
            return;
        }
    }
    if (fallbackRoot->blockOpCount < 2 ||
            fallbackRoot->pfn != cpu->thread->process->startJITOp ||
            !fallbackRoot->pfnJitCode || fallbackRoot->blockStart != fallbackRoot) {
        failed("movsd SMC MRGF4 fallback warmed compiled root");
        return;
    }
    DecodedOp* fallbackOriginalSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!fallbackOriginalSuccessor || fallbackRoot->next != fallbackOriginalSuccessor ||
            fallbackOriginalSuccessor->blockStart != fallbackRoot) {
        failed("movsd SMC MRGF4 fallback original successor ownership");
        return;
    }
    cpu->setSeg(DS, TEST_HEAP_ADDRESS, TEST_HEAP_SEG);
    setMovsdSmcRegisters(1, false, true);
    cpu->wasmJitMovsdSmcBailoutTakenCount = 0;
    cpu->wasmJitMovsdSmcCheckedWriteCount = 0;
    cpu->nextOp = fallbackRoot;
    cpu->run();
    DecodedOp* fallbackSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 3, 0);
    if (!fallbackSuccessor || cpu->reg[R_CX].u32 != 0 ||
            cpu->reg[R_SI].u32 != MOVSD_SMC_SOURCE - TEST_HEAP_ADDRESS + 4 ||
            cpu->reg[R_DI].u32 != TEST_CODE_ADDRESS + 7 || cpu->eip.u32 != 3 ||
            (cpu->flags & FMASK_ALL) != MOVSD_SMC_FLAGS ||
            cpu->wasmJitActiveBlock || cpu->wasmJitBailout != 0 ||
            cpu->wasmJitMovsdSmcCheckedWriteCount != 0 ||
            cpu->wasmJitMovsdSmcBailoutTakenCount != 1) {
        printf("Movsd SMC MRGF4 fallback fresh=%p ecx=%08X esi=%08X edi=%08X flags=%08X eip=%08X active=%p bailout=%u checked=%u taken=%u\n",
            fallbackSuccessor, cpu->reg[R_CX].u32, cpu->reg[R_SI].u32,
            cpu->reg[R_DI].u32, cpu->flags, cpu->eip.u32,
            cpu->wasmJitActiveBlock, cpu->wasmJitBailout,
            cpu->wasmJitMovsdSmcCheckedWriteCount, cpu->wasmJitMovsdSmcBailoutTakenCount);
        failed("movsd SMC MRGF4 nonzero-base fallback state");
        return;
    }
    for (U32 i = 0; i < sizeof(MOVSD_SMC_MUTATED_CODE); ++i) {
        if (memory->readb(TEST_CODE_ADDRESS + i) != MOVSD_SMC_MUTATED_CODE[i]) {
            failed("movsd SMC MRGF4 fallback checked write");
            return;
        }
    }
    printMovsdSmcSnapshot("MRGF4 fallback post-write");
    cpu->nextOp = fallbackSuccessor;
    cpu->run();
    if (verifyMovsdSmcFinal("MRGF4 fallback fixture", 4, false, true)) {
        printMovsdSmcSnapshot("MRGF4 fallback fixture");
        printf("Movsd SMC MRGF4 fallback generated checked-write=%u bailout-taken=%u\n",
            cpu->wasmJitMovsdSmcCheckedWriteCount, cpu->wasmJitMovsdSmcBailoutTakenCount);
        printf("Movsd SMC MRGF4 nonzero-base fallback ... OK\n");
    }
#elif defined(BOXEDWINE_WASM_JIT_MOVSD_MRG_EA16_SMC_FIXTURE)
    setupMovsdEa16Smc();
    DecodedOp* root = cpu->getOp(TEST_CODE_ADDRESS, 0);
    if (!root || root->inst != Movsd || !root->ea16 || root->base != DS || !root->repZero || root->len != 4) {
        failed("movsd SMC MRGF5 EA16 root decode");
        return;
    }
    for (U32 i = 0; i <= JIT_RUN_COUNT; ++i) {
        setMovsdEa16Regs(0); cpu->nextOp = root; cpu->run();
        if (!cpu->nextOp || cpu->nextOp->inst != TestEnd) { failed("movsd SMC MRGF5 EA16 warm"); return; }
    }
    if (root->pfn != cpu->thread->process->startJITOp || !root->pfnJitCode || root->blockStart != root) {
        failed("movsd SMC MRGF5 EA16 compiled root"); return;
    }
    DecodedOp* oldSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 4, 0);
    if (!oldSuccessor || root->next != oldSuccessor || oldSuccessor->blockStart != root) { failed("movsd SMC MRGF5 EA16 ownership"); return; }
    setMovsdEa16Regs(1);
    cpu->wasmJitMovsdSmcBailoutTakenCount = 0;
    cpu->wasmJitMovsdSmcCheckedWriteCount = 0;
    cpu->nextOp = root; cpu->run();
    DecodedOp* freshSuccessor = cpu->getOp(TEST_CODE_ADDRESS + 4, 0);
    if (!freshSuccessor || !verifyMovsdEa16State(4, false) || cpu->wasmJitActiveBlock || cpu->wasmJitBailout ||
            cpu->wasmJitMovsdSmcCheckedWriteCount != 0 || cpu->wasmJitMovsdSmcBailoutTakenCount != 1) {
        failed("movsd SMC MRGF5 EA16 helper mutation/redecode"); return;
    }
    cpu->nextOp = freshSuccessor; cpu->run();
    if (!verifyMovsdEa16State(5, true) || !cpu->nextOp || cpu->nextOp->inst != TestEnd) { failed("movsd SMC MRGF5 EA16 final"); return; }
    printf("Movsd SMC MRGF5 EA16 helper checked-write=%u bailout-taken=%u ... OK\n", cpu->wasmJitMovsdSmcCheckedWriteCount, cpu->wasmJitMovsdSmcBailoutTakenCount);
#endif
}

void testCmpsb_0x0a6() {
    runStringCases(STRING_CMPS, 1, false, COMPARE_CASES, caseCount(COMPARE_CASES));
    runPageBoundaryCases(STRING_CMPS, 1, false);
}

void testCmpsb_0x2a6() {
    runStringCases(STRING_CMPS, 1, true, COMPARE_CASES, caseCount(COMPARE_CASES));
    runPageBoundaryCases(STRING_CMPS, 1, true);
}

void testCmpsw_0x0a7() {
    runStringCases(STRING_CMPS, 2, false, COMPARE_CASES, caseCount(COMPARE_CASES));
    runPageBoundaryCases(STRING_CMPS, 2, false);
}

void testCmpsd_0x2a7() {
    runStringCases(STRING_CMPS, 4, true, COMPARE_CASES, caseCount(COMPARE_CASES));
    runPageBoundaryCases(STRING_CMPS, 4, true);
}

void testStosb_0x0aa() {
    runStringCases(STRING_STOS, 1, false, STORE_CASES, caseCount(STORE_CASES));
    runPageBoundaryCases(STRING_STOS, 1, false);
}

void testStosb_0x2aa() {
    runStringCases(STRING_STOS, 1, true, STORE_CASES, caseCount(STORE_CASES));
    runPageBoundaryCases(STRING_STOS, 1, true);
}

void testStosw_0x0ab() {
    runStringCases(STRING_STOS, 2, false, STORE_CASES, caseCount(STORE_CASES));
    runPageBoundaryCases(STRING_STOS, 2, false);
}

void testStosd_0x2ab() {
    runStringCases(STRING_STOS, 4, true, STORE_CASES, caseCount(STORE_CASES));
    runPageBoundaryCases(STRING_STOS, 4, true);
}

void testLodsb_0x0ac() {
    runStringCases(STRING_LODS, 1, false, LOAD_CASES, caseCount(LOAD_CASES));
    runPageBoundaryCases(STRING_LODS, 1, false);
}

void testLodsb_0x2ac() {
    runStringCases(STRING_LODS, 1, true, LOAD_CASES, caseCount(LOAD_CASES));
    runPageBoundaryCases(STRING_LODS, 1, true);
}

void testLodsw_0x0ad() {
    runStringCases(STRING_LODS, 2, false, LOAD_CASES, caseCount(LOAD_CASES));
    runPageBoundaryCases(STRING_LODS, 2, false);
}

void testLodsd_0x2ad() {
    runStringCases(STRING_LODS, 4, true, LOAD_CASES, caseCount(LOAD_CASES));
    runPageBoundaryCases(STRING_LODS, 4, true);
}

void testScasb_0x0ae() {
    runStringCases(STRING_SCAS, 1, false, SCAN_CASES, caseCount(SCAN_CASES));
    runPageBoundaryCases(STRING_SCAS, 1, false);
}

void testScasb_0x2ae() {
    runStringCases(STRING_SCAS, 1, true, SCAN_CASES, caseCount(SCAN_CASES));
    runPageBoundaryCases(STRING_SCAS, 1, true);
}

void testScasw_0x0af() {
    runStringCases(STRING_SCAS, 2, false, SCAN_CASES, caseCount(SCAN_CASES));
    runPageBoundaryCases(STRING_SCAS, 2, false);
}

void testScasd_0x2af() {
    runStringCases(STRING_SCAS, 4, true, SCAN_CASES, caseCount(SCAN_CASES));
    runPageBoundaryCases(STRING_SCAS, 4, true);
}

#endif
