/*
 *  Copyright (C) 2012-2026 The BoxedWine Team
 *
 *  Fail-closed HLE for acknex.dll FUN_10046760.  The helper replaces the
 *  transform path only.  Copy/type!=3, unaligned, faulting, modified-code and
 *  live-target-mismatch cases execute the unchanged generated guest body.
 */

#include "boxedwine.h"
#include "wasmJitAcknexTransformIsland.h"

#if defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND)

#include <array>
#include <atomic>
#include <cstring>

namespace {

constexpr U32 ACKNEX_TRANSFORM_ENTRY_RVA = 0x00046760u;
constexpr U32 ACKNEX_TRANSFORM3X3_RVA = 0x001041d7u;
constexpr U32 ACKNEX_TRANSFORM_COORD_RVA = 0x001042a3u;
constexpr U32 ACKNEX_THUNK_COORD_RVA = 0x00084364u;
constexpr U32 ACKNEX_THUNK3X3_RVA = 0x00084477u;
constexpr U32 ACKNEX_IAT_COORD_RVA = 0x00252150u;
constexpr U32 ACKNEX_IAT3X3_RVA = 0x00252140u;
constexpr U32 ACKNEX_ENGINE_GLOBAL_RVA = 0x00304c40u;
constexpr U32 ACKNEX_FILE_BYTES = 2682880u;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
constexpr U32 ACKNEX_OUTER_ENTRY_RVA = 0x00049650u;
constexpr U32 ACKNEX_OUTER_CONTINUE_RVA = 0x00049659u;
constexpr U32 ACKNEX_OUTER_EPILOGUE_RVA = 0x0004966du;
constexpr U32 ACKNEX_OUTER_STATE_GLOBAL_RVA = 0x002ff4ccu;
constexpr U32 ACKNEX_OUTER_CODE_GLOBAL_OFFSET = 0x0bu;
constexpr U32 ACKNEX_OUTER_CHUNK_ELEMENTS = 64u;
static_assert(ACKNEX_OUTER_CHUNK_ELEMENTS == WASM_JIT_ACKNEX_OUTER_CHUNK_ELEMENTS);
#endif

// SHA-256 876f6870f6858c3829c0830415bf30cf7dd375590810eef231a7e6b1a68eedc6
constexpr U8 ACKNEX_FILE_SHA256[32] = {
    0x87,0x6f,0x68,0x70,0xf6,0x85,0x8c,0x38,
    0x29,0xc0,0x83,0x04,0x15,0xbf,0x30,0xcf,
    0x7d,0xd3,0x75,0x59,0x08,0x10,0xee,0xf2,
    0x31,0xa7,0xe6,0xb1,0xa6,0x8e,0xed,0xc6
};

constexpr U8 ACKNEX_TRANSFORM_CODE[] = {
    0x56,0x8b,0xf2,0x8b,0x46,0x20,0x85,0xc0,0x57,0x8b,0xf9,0x8d,0x0c,0xc0,0x8d,0x14,
    0x88,0x8b,0x0d,0x40,0x4c,0x30,0x10,0x8b,0x89,0xf8,0x04,0x00,0x00,0x8d,0x0c,0x91,
    0x7c,0x28,0x85,0xc9,0x74,0x24,0x83,0x79,0x04,0x03,0x75,0x1e,0x53,0x8d,0x59,0x14,
    0x53,0x56,0x57,0xe8,0xcc,0xdb,0x03,0x00,0x53,0x83,0xc6,0x0c,0x56,0x83,0xc7,0x0c,
    0x57,0xe8,0xd1,0xdc,0x03,0x00,0x5b,0x5f,0x5e,0xc3,0x8b,0x16,0x89,0x17,0x8b,0x46,
    0x04,0x89,0x47,0x04,0x8b,0x4e,0x08,0x89,0x4f,0x08,0x8b,0x56,0x0c,0x89,0x57,0x0c,
    0x8b,0x46,0x10,0x89,0x47,0x10,0x8b,0x4e,0x14,0x89,0x4f,0x14,0x5f,0x5e,0xc3
};
static_assert(sizeof(ACKNEX_TRANSFORM_CODE) == 111);

#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
constexpr U8 ACKNEX_OUTER_CODE[] = {
    0x8b,0xd3,0x8b,0xcf,0xe8,0x07,0xd1,0xff,0xff,0x8b,0x0d,0xcc,0xf4,0x2f,0x10,0x83,
    0xc6,0x01,0x83,0xc7,0x2c,0x83,0xc3,0x2c,0x3b,0x71,0x28,0x7c,0xe3,0x5f,0x5e,0x5b,
    0xc3
};
static_assert(sizeof(ACKNEX_OUTER_CODE) == 33);
#endif

constexpr U8 ACKNEX_THUNK_COORD_CODE[] = {0xff,0x25,0x50,0x21,0x25,0x10};
constexpr U8 ACKNEX_THUNK3X3_CODE[] = {0xff,0x25,0x40,0x21,0x25,0x10};

constexpr U8 ACKNEX_TRANSFORM3X3_CODE[] = {
    0x8b,0xff,0x53,0x8b,0xdc,0x51,0x83,0xe4,0xf0,0x83,0xc4,0x04,0x55,0x8b,0xec,0x8b,
    0x43,0x10,0x8b,0x4b,0x0c,0x83,0xec,0x20,0xa8,0x0f,0x75,0x40,0x0f,0x28,0x4d,0xe0,
    0x0f,0x12,0x09,0x8b,0x49,0x08,0x0f,0x28,0x50,0x20,0x89,0x4d,0xfc,0xf3,0x0f,0x10,
    0x45,0xfc,0x0f,0xc6,0xc0,0x00,0x0f,0x59,0xc2,0x0f,0x28,0xd0,0x0f,0x28,0x40,0x10,
    0x0f,0x28,0xd9,0x0f,0xc6,0xd9,0x55,0x0f,0x59,0xd8,0x0f,0x28,0x00,0x0f,0x28,0xe1,
    0x0f,0xc6,0xe1,0x00,0x0f,0x59,0xe0,0x0f,0x28,0xc4,0xeb,0x50,0x0f,0x28,0x45,0xe0,
    0x0f,0x12,0x01,0x8b,0x49,0x08,0x0f,0x28,0x55,0xe0,0x0f,0x12,0x50,0x20,0x0f,0x16,
    0x50,0x28,0x0f,0x28,0x5d,0xe0,0x0f,0x12,0x58,0x10,0x0f,0x16,0x58,0x18,0x89,0x4d,
    0xfc,0xf3,0x0f,0x10,0x4d,0xfc,0x0f,0xc6,0xc9,0x00,0x0f,0x59,0xd1,0x0f,0x28,0xc8,
    0x0f,0xc6,0xc8,0x55,0x0f,0x59,0xd9,0x0f,0x28,0xc8,0x0f,0xc6,0xc8,0x00,0x0f,0x28,
    0x45,0xe0,0x0f,0x12,0x00,0x0f,0x16,0x40,0x08,0x0f,0x59,0xc1,0x8b,0x43,0x08,0x0f,
    0x58,0xc3,0x0f,0x58,0xc2,0x0f,0x28,0xc8,0x0f,0x12,0xc8,0x0f,0x13,0x00,0xf3,0x0f,
    0x11,0x48,0x08,0x8b,0xe5,0x5d,0x8b,0xe3,0x5b,0xc2,0x0c,0x00
};
static_assert(sizeof(ACKNEX_TRANSFORM3X3_CODE) == 204);

constexpr U8 ACKNEX_TRANSFORM_COORD_CODE[] = {
    0x8b,0xff,0x53,0x8b,0xdc,0x51,0x83,0xe4,0xf0,0x83,0xc4,0x04,0x55,0x8b,0xec,0x8b,
    0x4b,0x0c,0x8b,0x43,0x10,0x0f,0x28,0x45,0xe0,0x0f,0x12,0x01,0x8b,0x49,0x08,0x83,
    0xec,0x20,0xa8,0x0f,0x75,0x34,0x0f,0x28,0x50,0x30,0x0f,0x28,0x58,0x20,0x89,0x4d,
    0xfc,0xf3,0x0f,0x10,0x4d,0xfc,0x0f,0xc6,0xc9,0x00,0x0f,0x59,0xd9,0x0f,0x58,0xda,
    0x0f,0x28,0x50,0x10,0x0f,0x28,0xc8,0x0f,0xc6,0xc8,0x55,0x0f,0x59,0xd1,0x0f,0x28,
    0xc8,0x0f,0xc6,0xc8,0x00,0x0f,0x28,0x00,0xeb,0x55,0x0f,0x28,0x4d,0xe0,0x0f,0x12,
    0x48,0x30,0x0f,0x16,0x48,0x38,0x0f,0x28,0x5d,0xe0,0x0f,0x12,0x58,0x20,0x0f,0x16,
    0x58,0x28,0x0f,0x28,0xd1,0x89,0x4d,0xfc,0xf3,0x0f,0x10,0x4d,0xfc,0x0f,0xc6,0xc9,
    0x00,0x0f,0x59,0xd9,0x0f,0x58,0xda,0x0f,0x28,0x55,0xe0,0x0f,0x12,0x50,0x10,0x0f,
    0x16,0x50,0x18,0x0f,0x28,0xc8,0x0f,0xc6,0xc8,0x55,0x0f,0x59,0xd1,0x0f,0x28,0xc8,
    0x0f,0xc6,0xc8,0x00,0x0f,0x28,0x45,0xe0,0x0f,0x12,0x00,0x0f,0x16,0x40,0x08,0x8b,
    0x43,0x08,0x0f,0x59,0xc1,0x0f,0x58,0xc2,0x0f,0x58,0xc3,0x0f,0x53,0xc8,0x0f,0x28,
    0xd1,0x0f,0x28,0xd9,0x0f,0x59,0xd0,0x0f,0x59,0xd1,0x0f,0x58,0xd9,0x0f,0x28,0xcb,
    0x0f,0x5c,0xca,0x0f,0x28,0xd1,0x0f,0xc6,0xd1,0xff,0x0f,0x59,0xc2,0x0f,0x28,0xc8,
    0x0f,0x12,0xc8,0x0f,0x13,0x00,0xf3,0x0f,0x11,0x48,0x08,0x8b,0xe5,0x5d,0x8b,0xe3,
    0x5b,0xc2,0x0c,0x00
};
static_assert(sizeof(ACKNEX_TRANSFORM_COORD_CODE) == 244);

std::array<std::atomic<U64>, WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT> counters = {};

inline void count(WasmJitAcknexTransformCounter counter) {
    counters[(U32)counter].fetch_add(1, std::memory_order_relaxed);
}

inline void countAdd(WasmJitAcknexTransformCounter counter, U64 value) {
    if (value) counters[(U32)counter].fetch_add(value, std::memory_order_relaxed);
}

struct Sha256 {
    U32 state[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    U8 pending[64] = {};
    U64 bytes = 0;
    U32 pendingBytes = 0;

    static U32 rotate(U32 value, U32 bits) {
        return (value >> bits) | (value << (32 - bits));
    }
    static U32 loadBe(const U8* p) {
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3];
    }
    void transform(const U8* block) {
        static constexpr U32 k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        U32 w[64];
        for (U32 i = 0; i < 16; ++i) w[i] = loadBe(block + i * 4);
        for (U32 i = 16; i < 64; ++i) {
            const U32 s0 = rotate(w[i-15],7) ^ rotate(w[i-15],18) ^ (w[i-15] >> 3);
            const U32 s1 = rotate(w[i-2],17) ^ rotate(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        U32 a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (U32 i = 0; i < 64; ++i) {
            const U32 s1 = rotate(e,6) ^ rotate(e,11) ^ rotate(e,25);
            const U32 ch = (e & f) ^ (~e & g);
            const U32 t1 = h + s1 + ch + k[i] + w[i];
            const U32 s0 = rotate(a,2) ^ rotate(a,13) ^ rotate(a,22);
            const U32 maj = (a & b) ^ (a & c) ^ (b & c);
            const U32 t2 = s0 + maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    void update(const U8* data, U32 len) {
        bytes += len;
        while (len) {
            const U32 take = std::min<U32>(len, 64 - pendingBytes);
            std::memcpy(pending + pendingBytes, data, take);
            pendingBytes += take; data += take; len -= take;
            if (pendingBytes == 64) { transform(pending); pendingBytes = 0; }
        }
    }
    void finish(U8 output[32]) {
        const U64 bitCount = bytes * 8;
        pending[pendingBytes++] = 0x80;
        if (pendingBytes > 56) {
            std::memset(pending + pendingBytes, 0, 64 - pendingBytes);
            transform(pending); pendingBytes = 0;
        }
        std::memset(pending + pendingBytes, 0, 56 - pendingBytes);
        for (U32 i = 0; i < 8; ++i) pending[63-i] = (U8)(bitCount >> (i*8));
        transform(pending);
        for (U32 i = 0; i < 8; ++i) {
            output[i*4]=(U8)(state[i]>>24); output[i*4+1]=(U8)(state[i]>>16);
            output[i*4+2]=(U8)(state[i]>>8); output[i*4+3]=(U8)state[i];
        }
    }
};

bool exactBackingIdentity(const MappedFilePtr& mapping) {
    if (!mapping || !mapping->file || mapping->file->length() != ACKNEX_FILE_BYTES) return false;
    Sha256 sha;
    U8 buffer[64 * 1024];
    U32 offset = 0;
    while (offset < ACKNEX_FILE_BYTES) {
        const U32 want = std::min<U32>((U32)sizeof(buffer), ACKNEX_FILE_BYTES - offset);
        if (mapping->file->preadNative(buffer, offset, want) != want) return false;
        sha.update(buffer, want);
        offset += want;
    }
    U8 digest[32];
    sha.finish(digest);
    return std::memcmp(digest, ACKNEX_FILE_SHA256, sizeof(digest)) == 0;
}

struct IdentityCache {
    KMemory* memory = nullptr;
    KProcess* process = nullptr;
    U32 processId = 0;
    U32 mappingKey = 0;
    U64 mappingGeneration = 0;
    U32 entry = 0;
    U32 base = 0;
    bool resolved = false;
    bool valid = false;
};
thread_local IdentityCache identityCache;

enum class CodeComponent : U32 {
    None = 0,
    Entry = 1,
    ThunkCoord = 2,
    Thunk3x3 = 3,
    Transform3x3 = 4,
    TransformCoord = 5,
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    OuterEntry = 6,
#endif
};

struct CodeMismatch {
    CodeComponent component = CodeComponent::None;
    U32 offset = 0xffffffffu;
    U32 address = 0;
    U32 expected = 0;
    // 0x100 means that the complete range was not readable.
    U32 actual = 0x100u;
};

struct NegativeCodeCache {
    KMemory* memory = nullptr;
    KProcess* process = nullptr;
    U32 processId = 0;
    U64 mappingGeneration = 0;
    U32 entry = 0;
    U32 base = 0;
    bool negative = false;
    CodeMismatch mismatch;
};
thread_local NegativeCodeCache negativeCodeCache;

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
std::atomic<bool> fixtureIdentityValid{false};
std::atomic<U32> fixtureModuleBase{0x10000000u};
#endif
#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE)
std::atomic<U32> fixtureOuterStopAfter{0xffffffffu};
std::atomic<U32> fixtureOuterStopKind{0};
#endif

U32 acknexEntryRva(U32 moduleBase, U32 entryAddress) {
    if (entryAddress == moduleBase + ACKNEX_TRANSFORM_ENTRY_RVA)
        return ACKNEX_TRANSFORM_ENTRY_RVA;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (entryAddress == moduleBase + ACKNEX_OUTER_ENTRY_RVA)
        return ACKNEX_OUTER_ENTRY_RVA;
#endif
    return 0;
}

U32 acknexEntryBytes(U32 entryRva) {
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (entryRva == ACKNEX_OUTER_ENTRY_RVA) return sizeof(ACKNEX_OUTER_CODE);
#endif
    return entryRva == ACKNEX_TRANSFORM_ENTRY_RVA ? sizeof(ACKNEX_TRANSFORM_CODE) : 0;
}

bool resolveIdentity(CPU* cpu, U32 entryAddress, U32& moduleBase) {
#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
    const U32 testBase=fixtureModuleBase.load(std::memory_order_relaxed);
    if (acknexEntryRva(testBase,entryAddress)) {
        moduleBase = testBase;
        return fixtureIdentityValid.load(std::memory_order_relaxed);
    }
#endif
    KProcess* currentProcess = cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    if (identityCache.memory == cpu->memory && identityCache.process == currentProcess &&
            identityCache.processId == (currentProcess ? currentProcess->id : 0) &&
            identityCache.entry == entryAddress && identityCache.resolved && currentProcess &&
            identityCache.mappingGeneration == currentProcess->getAcknexTransformMappingGeneration()) {
        moduleBase = identityCache.base;
        return identityCache.valid;
    }
    identityCache = {};
    identityCache.memory = cpu->memory;
    identityCache.process = currentProcess;
    identityCache.processId = currentProcess ? currentProcess->id : 0;
    identityCache.entry = entryAddress;
    identityCache.resolved = true;
    KProcess* process = currentProcess;
    if (!process) return false;
    identityCache.mappingGeneration = process->getAcknexTransformMappingGeneration();
    // The module base is not known yet.  Resolve the exact admitted RVA from
    // the mapped-file offset first; no arbitrary address in acknex.dll is a
    // candidate.
    MappedFilePtr mapping = process->getMappedFileForRange(entryAddress, 1);
    if (!mapping || !mapping->file || !mapping->file->openFile || !mapping->file->openFile->node ||
            !mapping->file->openFile->node->path.endsWith("acknex.dll", true)) return false;
    const U64 fileOffset = (U64)(entryAddress - mapping->address) + mapping->offset;
    const U32 entryRva = fileOffset <= 0xffffffffULL ? (U32)fileOffset : 0;
    const U32 entryBytes = acknexEntryBytes(entryRva);
    MappedFilePtr fullMapping=entryBytes ?
        process->getMappedFileForRange(entryAddress,entryBytes) : nullptr;
    if (!entryBytes || !fullMapping || fullMapping->key!=mapping->key ||
            !process->acknexTransformMappedFileOwnsRange(entryAddress,entryBytes,mapping->key) ||
            !exactBackingIdentity(mapping)) return false;
    identityCache.base = entryAddress - entryRva;
    identityCache.mappingKey = mapping->key;
    identityCache.valid = true;
    moduleBase = identityCache.base;
    return true;
}

bool rangeNoWrap(U32 address, U32 len) {
    return len && (U64)address + len <= 0x100000000ULL;
}

bool readable(KMemory* memory, U32 address, U32 len) {
    return rangeNoWrap(address, len) && memory->canRead(address, len);
}

bool writable(KMemory* memory, U32 address, U32 len) {
    return rangeNoWrap(address, len) && memory->canWrite(address, len);
}

bool readOnlyMemoryEquals(KMemory* memory,U32 address,const U8* bytes,U32 len) {
    if(!readable(memory,address,len)) return false;
    bool equal=true;
    const U8* cursor=bytes;
    memory->performOnMemory(address,len,true,[&](U8* ram,U32 chunk) {
        if(std::memcmp(cursor,ram,chunk)!=0) { equal=false; return false; }
        cursor+=chunk;
        return true;
    });
    return equal;
}

bool codeMatches(KMemory* memory, U32 address, const U8* bytes, U32 len) {
    return readOnlyMemoryEquals(memory,address,bytes,len);
}

bool codeMatchesRelocated(KMemory* memory, U32 address, const U8* preferredBytes,
        U32 len, U32 relocationOffset, U32 relocatedValue) {
    if (!readable(memory,address,len) || relocationOffset+4>len) return false;
    // A relocation can begin at byte zero or end exactly at len.  Do not pass
    // either empty edge to readable(), whose zero-length contract is false.
    // Both Acknex six-byte FF 25 thunks have precisely this terminal-reloc
    // shape (offset 2, width 4); the old check therefore rejected byte-exact
    // live thunks on every helper entry.
    return (!relocationOffset ||
            readOnlyMemoryEquals(memory,address,preferredBytes,relocationOffset)) &&
        memory->readd(address+relocationOffset)==relocatedValue &&
        (relocationOffset+4==len ||
            readOnlyMemoryEquals(memory,address+relocationOffset+4,
                preferredBytes+relocationOffset+4,len-relocationOffset-4));
}

WasmJitAcknexTransformCounter mismatchCounter(CodeComponent component) {
    switch (component) {
    case CodeComponent::Entry: return WasmJitAcknexTransformCounter::CodeMismatchEntry;
    case CodeComponent::ThunkCoord: return WasmJitAcknexTransformCounter::CodeMismatchThunkCoord;
    case CodeComponent::Thunk3x3: return WasmJitAcknexTransformCounter::CodeMismatchThunk3x3;
    case CodeComponent::Transform3x3: return WasmJitAcknexTransformCounter::CodeMismatchTransform3x3;
    case CodeComponent::TransformCoord: return WasmJitAcknexTransformCounter::CodeMismatchTransformCoord;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    case CodeComponent::OuterEntry: return WasmJitAcknexTransformCounter::CodeMismatchEntry;
#endif
    default: return WasmJitAcknexTransformCounter::CodeMismatchEntry;
    }
}

CodeMismatch findFirstMismatch(KMemory* memory, CodeComponent component, U32 address,
        const U8* preferredBytes, U32 len, U32 relocationOffset = 0xffffffffu,
        U32 relocatedValue = 0) {
    CodeMismatch result;
    result.component = component;
    result.address = address;
    if (!readable(memory,address,len)) return result;
    for (U32 i=0;i<len;++i) {
        U32 expected = preferredBytes[i];
        if (i>=relocationOffset && i-relocationOffset<4u)
            expected=(relocatedValue >> ((i-relocationOffset)*8u)) & 0xffu;
        const U32 actual=memory->readb(address+i);
        if (actual!=expected) {
            result.offset=i;
            result.address=address+i;
            result.expected=expected;
            result.actual=actual;
            return result;
        }
    }
    // A concurrent writer may have restored the range after the fast compare.
    // Preserve that fact rather than inventing a byte mismatch.
    result.offset=0xfffffffeu;
    result.expected=0x101u;
    result.actual=0x101u;
    return result;
}

bool captureFirstMismatch(CPU* cpu, U32 entry, U32 base, const CodeMismatch& mismatch) {
    auto& state=counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchState];
    U64 empty=0;
    if (!state.compare_exchange_strong(empty,1,std::memory_order_acq_rel)) return false;
    KProcess* process=cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchComponent].store(
        (U32)mismatch.component,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchOffset].store(
        mismatch.offset,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchAddress].store(
        mismatch.address,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchExpected].store(
        mismatch.expected,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchActual].store(
        mismatch.actual,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchBase].store(
        base,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchEntry].store(
        entry,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchProcessId].store(
        process ? process->id : 0,std::memory_order_relaxed);
    counters[(U32)WasmJitAcknexTransformCounter::FirstMismatchMappingGeneration].store(
        process ? process->getAcknexTransformMappingGeneration() : 0,
        std::memory_order_relaxed);
    state.store(2,std::memory_order_release);
    return true;
}

bool negativeCodeCacheMatches(CPU* cpu, U32 entry, U32 base) {
    KProcess* process=cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    return negativeCodeCache.negative && negativeCodeCache.memory==cpu->memory &&
        negativeCodeCache.process==process &&
        negativeCodeCache.processId==(process ? process->id : 0) &&
        negativeCodeCache.mappingGeneration==
            (process ? process->getAcknexTransformMappingGeneration() : 0) &&
        negativeCodeCache.entry==entry && negativeCodeCache.base==base;
}

void rememberNegativeCode(CPU* cpu, U32 entry, U32 base, const CodeMismatch& mismatch) {
    KProcess* process=cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    negativeCodeCache={};
    negativeCodeCache.memory=cpu->memory;
    negativeCodeCache.process=process;
    negativeCodeCache.processId=process ? process->id : 0;
    negativeCodeCache.mappingGeneration=process ?
        process->getAcknexTransformMappingGeneration() : 0;
    negativeCodeCache.entry=entry;
    negativeCodeCache.base=base;
    negativeCodeCache.negative=true;
    negativeCodeCache.mismatch=mismatch;
}

bool validateCodeContract(KMemory* memory, U32 entry, U32 base, CodeMismatch& mismatch) {
    const U32 target3x3=base+ACKNEX_TRANSFORM3X3_RVA;
    const U32 targetCoord=base+ACKNEX_TRANSFORM_COORD_RVA;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (entry == base + ACKNEX_OUTER_ENTRY_RVA &&
            !codeMatchesRelocated(memory,entry,ACKNEX_OUTER_CODE,sizeof(ACKNEX_OUTER_CODE),
                ACKNEX_OUTER_CODE_GLOBAL_OFFSET,base+ACKNEX_OUTER_STATE_GLOBAL_RVA)) {
        mismatch=findFirstMismatch(memory,CodeComponent::OuterEntry,entry,ACKNEX_OUTER_CODE,
            sizeof(ACKNEX_OUTER_CODE),ACKNEX_OUTER_CODE_GLOBAL_OFFSET,
            base+ACKNEX_OUTER_STATE_GLOBAL_RVA);
        return false;
    }
#endif
    const U32 transformEntry=base+ACKNEX_TRANSFORM_ENTRY_RVA;
    if (!codeMatchesRelocated(memory,transformEntry,ACKNEX_TRANSFORM_CODE,sizeof(ACKNEX_TRANSFORM_CODE),
            0x13,base+ACKNEX_ENGINE_GLOBAL_RVA)) {
        mismatch=findFirstMismatch(memory,CodeComponent::Entry,transformEntry,ACKNEX_TRANSFORM_CODE,
            sizeof(ACKNEX_TRANSFORM_CODE),0x13,base+ACKNEX_ENGINE_GLOBAL_RVA);
        return false;
    }
    if (!codeMatchesRelocated(memory,base+ACKNEX_THUNK_COORD_RVA,ACKNEX_THUNK_COORD_CODE,
            sizeof(ACKNEX_THUNK_COORD_CODE),2,base+ACKNEX_IAT_COORD_RVA)) {
        mismatch=findFirstMismatch(memory,CodeComponent::ThunkCoord,
            base+ACKNEX_THUNK_COORD_RVA,ACKNEX_THUNK_COORD_CODE,
            sizeof(ACKNEX_THUNK_COORD_CODE),2,base+ACKNEX_IAT_COORD_RVA);
        return false;
    }
    if (!codeMatchesRelocated(memory,base+ACKNEX_THUNK3X3_RVA,ACKNEX_THUNK3X3_CODE,
            sizeof(ACKNEX_THUNK3X3_CODE),2,base+ACKNEX_IAT3X3_RVA)) {
        mismatch=findFirstMismatch(memory,CodeComponent::Thunk3x3,
            base+ACKNEX_THUNK3X3_RVA,ACKNEX_THUNK3X3_CODE,
            sizeof(ACKNEX_THUNK3X3_CODE),2,base+ACKNEX_IAT3X3_RVA);
        return false;
    }
    if (!codeMatches(memory,target3x3,ACKNEX_TRANSFORM3X3_CODE,
            sizeof(ACKNEX_TRANSFORM3X3_CODE))) {
        mismatch=findFirstMismatch(memory,CodeComponent::Transform3x3,target3x3,
            ACKNEX_TRANSFORM3X3_CODE,sizeof(ACKNEX_TRANSFORM3X3_CODE));
        return false;
    }
    if (!codeMatches(memory,targetCoord,ACKNEX_TRANSFORM_COORD_CODE,
            sizeof(ACKNEX_TRANSFORM_COORD_CODE))) {
        mismatch=findFirstMismatch(memory,CodeComponent::TransformCoord,targetCoord,
            ACKNEX_TRANSFORM_COORD_CODE,sizeof(ACKNEX_TRANSFORM_COORD_CODE));
        return false;
    }
    return true;
}

bool rangesOverlap(U32 a, U32 aLen, U32 b, U32 bLen) {
    if (!rangeNoWrap(a,aLen) || !rangeNoWrap(b,bLen)) return true;
    return (U64)a < (U64)b+bLen && (U64)b < (U64)a+aLen;
}

union F32Bits { U32 u; float f; };

float readF32(KMemory* memory, U32 address) {
    F32Bits value;
    value.u = memory->readd(address);
    return value.f;
}

void writeF32(KMemory* memory, U32 address, float value) {
    F32Bits bits;
    bits.f = value;
    memory->writed(address, bits.u);
}

void runTransform3x3(CPU* cpu, U32 destination, U32 source, U32 matrix,
        U32 wrapperEntryEsp
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
        , bool recordCounter = true
#endif
        ) {
    KMemory* memory = cpu->memory;
    const U64 sourceXYBits=memory->readq(source);
    F32Bits sourceX{(U32)sourceXYBits}, sourceY{(U32)(sourceXYBits>>32)};
    const float x=sourceX.f, y=sourceY.f;
    F32Bits sourceZ{memory->readd(source + 8)};
    const float z = sourceZ.f;
    float rows[12];
    for (S32 row = 2; row >= 0; --row) {
        const U32 rowAddress=matrix+(U32)row*16u;
        const U64 lo=memory->readq(rowAddress),hi=memory->readq(rowAddress+8);
        F32Bits bits[4]={{(U32)lo},{(U32)(lo>>32)},{(U32)hi},{(U32)(hi>>32)}};
        for(U32 lane=0;lane<4;++lane) rows[(U32)row*4+lane]=bits[lane].f;
    }
    float px[4], py[4], pz[4], result[4];
    for (U32 lane = 0; lane < 4; ++lane) {
        volatile float xPart = x * rows[lane];
        volatile float yPart = y * rows[4 + lane];
        volatile float zPart = z * rows[8 + lane];
        px[lane]=xPart; py[lane]=yPart; pz[lane]=zPart;
        volatile float xy = xPart + yPart;
        result[lane] = xy + zPart;
    }
    F32Bits resultBits[3];
    for(U32 lane=0;lane<3;++lane) resultBits[lane].f=result[lane];
    memory->writeq(destination,(U64)resultBits[0].u|((U64)resultBits[1].u<<32));
    memory->writed(destination+8,resultBits[2].u);
    cpu->reg[regAX].u32 = destination;
    cpu->reg[regCX].u32 = sourceZ.u;
    // The final integer flag producer is TEST AL,0x0f, but its u8 writes leave
    // the upper bytes from the immediately preceding SUB ESP,0x20 tuple.
    const U32 alignedFrame = (wrapperEntryEsp - 36u) & ~0x0fu;
    cpu->dst.u32 = alignedFrame;
    cpu->src.u32 = 0x20u;
    cpu->result.u32 = alignedFrame - 0x20u;
    cpu->dst.u8 = (U8)matrix; cpu->src.u8 = 0x0f;
    cpu->result.u8 = cpu->dst.u8 & cpu->src.u8; cpu->lazyFlagType = FLAGS_TEST8;
    for (U32 lane = 0; lane < 4; ++lane) {
        cpu->xmm[0].ps.f32[lane]=result[lane]; cpu->xmm[2].ps.f32[lane]=pz[lane];
        cpu->xmm[3].ps.f32[lane]=py[lane];
        if ((matrix & 0x0f) == 0) cpu->xmm[4].ps.f32[lane]=px[lane];
    }
    cpu->xmm[1].ps.f32[0]=result[2]; cpu->xmm[1].ps.f32[1]=result[3];
    cpu->xmm[1].ps.f32[2]=result[2]; cpu->xmm[1].ps.f32[3]=result[3];
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (recordCounter)
#endif
        count(WasmJitAcknexTransformCounter::Transform3x3Runs);
}

simde__m128 loadM128(KMemory* memory, U32 address) {
    simde__m128 value;
    value.u64[0] = memory->readq(address);
    value.u64[1] = memory->readq(address + 8);
    return value;
}

void runTransformCoord(CPU* cpu, U32 destination, U32 source, U32 matrix
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
        , bool recordCounter = true
#endif
        ) {
    KMemory* memory = cpu->memory;
    simde__m128 sourceXY;
    sourceXY.u64[0] = memory->readq(source);
    const U32 sourceZBits = memory->readd(source + 8);
    const simde__m128 row3=loadM128(memory,matrix+0x30), row2=loadM128(memory,matrix+0x20);
    const simde__m128 row1=loadM128(memory,matrix+0x10), row0=loadM128(memory,matrix);
    simde__m128 z=simde_mm_castsi128_ps(simde_mm_set1_epi32((S32)sourceZBits));
    simde__m128 zPart=simde_mm_add_ps(simde_mm_mul_ps(row2,z),row3);
    simde__m128 y=simde_mm_shuffle_ps(sourceXY,sourceXY,0x55);
    simde__m128 yPart=simde_mm_mul_ps(row1,y);
    simde__m128 x=simde_mm_shuffle_ps(sourceXY,sourceXY,0x00);
    simde__m128 transformed=simde_mm_mul_ps(row0,x);
    transformed=simde_mm_add_ps(transformed,yPart);
    transformed=simde_mm_add_ps(transformed,zPart);
    simde__m128 reciprocal=simde_mm_rcp_ps(transformed);
    simde__m128 correction=simde_mm_mul_ps(simde_mm_mul_ps(reciprocal,transformed),reciprocal);
    simde__m128 twiceReciprocal=simde_mm_add_ps(reciprocal,reciprocal);
    reciprocal=simde_mm_sub_ps(twiceReciprocal,correction);
    simde__m128 reciprocalW=simde_mm_shuffle_ps(reciprocal,reciprocal,0xff);
    transformed=simde_mm_mul_ps(transformed,reciprocalW);
    memory->writeq(destination,transformed.u64[0]); memory->writed(destination+8,transformed.u32[2]);
    cpu->reg[regAX].u32=destination; cpu->reg[regCX].u32=sourceZBits;
    cpu->dst.u8=(U8)matrix; cpu->src.u8=0x0f;
    cpu->result.u8=cpu->dst.u8 & cpu->src.u8; cpu->lazyFlagType=FLAGS_TEST8;
    cpu->xmm[0].ps=transformed; cpu->xmm[1].ps=transformed;
    cpu->xmm[1].ps.u64[0]=transformed.u64[1]; cpu->xmm[2].ps=reciprocalW;
    cpu->xmm[3].ps=twiceReciprocal;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (recordCounter)
#endif
        count(WasmJitAcknexTransformCounter::TransformCoordRuns);
}

#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
struct AcknexOuterElementPlan {
    U32 destination = 0;
    U32 source = 0;
    U32 matrix = 0;
    U32 index37 = 0;
    U32 callEsp = 0;
    U32 alignedFrame = 0;
    U32 stackScratchStart = 0;
    U32 stackScratchBytes = 0;
    U32 entryEbx = 0;
    U32 entryEsi = 0;
    U32 entryEdi = 0;
    U32 entryEbp = 0;
};

void setAdd32Lazy(CPU* cpu, U32 destination, U32 source, U32 result) {
    cpu->dst.u32=destination;
    cpu->src.u32=source;
    cpu->result.u32=result;
    cpu->lazyFlagType=FLAGS_ADD32;
}

bool signedLessFromSub(U32 destination, U32 source, U32 result) {
    const U32 sign=(result>>31)&1u;
    const U32 overflow=(((destination^source)&(destination^result))>>31)&1u;
    return (sign^overflow)!=0;
}

bool prepareOuterElement(CPU* cpu, U32 base, AcknexOuterElementPlan& plan) {
    KMemory* memory=cpu->memory;
    const U32 target3x3=base+ACKNEX_TRANSFORM3X3_RVA;
    const U32 targetCoord=base+ACKNEX_TRANSFORM_COORD_RVA;
    if (!readable(memory,base+ACKNEX_IAT_COORD_RVA,4) ||
            !readable(memory,base+ACKNEX_IAT3X3_RVA,4) ||
            memory->readd(base+ACKNEX_IAT_COORD_RVA)!=targetCoord ||
            memory->readd(base+ACKNEX_IAT3X3_RVA)!=target3x3) {
        count(WasmJitAcknexTransformCounter::FallbackLiveTarget);
        return false;
    }

    const U32 outerEsp=cpu->reg[regSP].u32;
    if (outerEsp<100u) {
        count(WasmJitAcknexTransformCounter::FallbackStack);
        return false;
    }
    plan.callEsp=outerEsp-4u;
    plan.alignedFrame=(plan.callEsp-36u)&~0x0fu;
    plan.stackScratchStart=plan.alignedFrame-32u;
    plan.stackScratchBytes=plan.callEsp-plan.stackScratchStart;
    // Unlike the single-element helper, the outer helper must synthesize the
    // CALL return dword at callEsp itself, so its write preflight includes the
    // final four bytes as well.
    if (!readable(memory,plan.stackScratchStart,plan.stackScratchBytes+4u) ||
            !writable(memory,plan.stackScratchStart,plan.stackScratchBytes+4u)) {
        count(WasmJitAcknexTransformCounter::FallbackStack);
        return false;
    }

    plan.destination=cpu->reg[regDI].u32;
    plan.source=cpu->reg[regBX].u32;
    if (!readable(memory,plan.source,0x24) ||
            !writable(memory,plan.destination,0x18) ||
            !readable(memory,base+ACKNEX_ENGINE_GLOBAL_RVA,4) ||
            !readable(memory,base+ACKNEX_OUTER_STATE_GLOBAL_RVA,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return false;
    }
    const S32 matrixIndex=(S32)memory->readd(plan.source+0x20);
    if (matrixIndex<0) {
        count(WasmJitAcknexTransformCounter::FallbackNegativeIndex);
        return false;
    }
    const U32 engine=memory->readd(base+ACKNEX_ENGINE_GLOBAL_RVA);
    if (!readable(memory,engine+0x4f8,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return false;
    }
    const U32 matrixTable=memory->readd(engine+0x4f8);
    plan.index37=(U32)matrixIndex+((U32)matrixIndex+(U32)matrixIndex*8u)*4u;
    const U32 matrixRecord=matrixTable+plan.index37*4u;
    if (!matrixRecord) {
        count(WasmJitAcknexTransformCounter::FallbackNullMatrix);
        return false;
    }
    if (!readable(memory,matrixRecord+4,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return false;
    }
    if (memory->readd(matrixRecord+4)!=3) {
        count(WasmJitAcknexTransformCounter::FallbackType);
        return false;
    }
    plan.matrix=matrixRecord+0x14;
    if ((plan.destination|plan.source|plan.matrix)&3u) {
        count(WasmJitAcknexTransformCounter::FallbackUnaligned);
        return false;
    }
    if (!readable(memory,plan.matrix,0x40)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return false;
    }

    // Preflight the post-transform controller reads, but do not cache their
    // values across the element: destination writes are ordered before the
    // real MOV/CMP.  Reject the two direct aliases so that this preflight
    // cannot be invalidated by our own transform writes.
    const U32 outerState=memory->readd(base+ACKNEX_OUTER_STATE_GLOBAL_RVA);
    if (!readable(memory,outerState+0x28,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return false;
    }

    const U32 scratchLen=plan.stackScratchBytes+4u;
    const U32 transformEntry=base+ACKNEX_TRANSFORM_ENTRY_RVA;
    const U32 outerEntry=base+ACKNEX_OUTER_ENTRY_RVA;
    if (rangesOverlap(plan.stackScratchStart,scratchLen,plan.source,0x24) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,plan.destination,0x18) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,plan.matrix,0x40) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_ENGINE_GLOBAL_RVA,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,engine+0x4f8,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,matrixRecord+4,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_OUTER_STATE_GLOBAL_RVA,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,outerState+0x28,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,outerEntry,sizeof(ACKNEX_OUTER_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,transformEntry,sizeof(ACKNEX_TRANSFORM_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_THUNK_COORD_RVA,sizeof(ACKNEX_THUNK_COORD_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_THUNK3X3_RVA,sizeof(ACKNEX_THUNK3X3_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,targetCoord,sizeof(ACKNEX_TRANSFORM_COORD_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,target3x3,sizeof(ACKNEX_TRANSFORM3X3_CODE)) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_IAT_COORD_RVA,4) ||
        rangesOverlap(plan.stackScratchStart,scratchLen,base+ACKNEX_IAT3X3_RVA,4) ||
        rangesOverlap(plan.destination,0x18,base+ACKNEX_OUTER_STATE_GLOBAL_RVA,4) ||
        rangesOverlap(plan.destination,0x18,outerState+0x28,4) ||
        rangesOverlap(plan.destination,0x18,outerEntry,sizeof(ACKNEX_OUTER_CODE)) ||
        rangesOverlap(plan.destination,0x18,transformEntry,sizeof(ACKNEX_TRANSFORM_CODE)) ||
        rangesOverlap(plan.destination,0x18,base+ACKNEX_THUNK_COORD_RVA,sizeof(ACKNEX_THUNK_COORD_CODE)) ||
        rangesOverlap(plan.destination,0x18,base+ACKNEX_THUNK3X3_RVA,sizeof(ACKNEX_THUNK3X3_CODE)) ||
        rangesOverlap(plan.destination,0x18,targetCoord,sizeof(ACKNEX_TRANSFORM_COORD_CODE)) ||
        rangesOverlap(plan.destination,0x18,target3x3,sizeof(ACKNEX_TRANSFORM3X3_CODE)) ||
        rangesOverlap(plan.destination,0x18,base+ACKNEX_IAT_COORD_RVA,4) ||
        rangesOverlap(plan.destination,0x18,base+ACKNEX_IAT3X3_RVA,4)) {
        count(WasmJitAcknexTransformCounter::FallbackAlias);
        return false;
    }

    plan.entryEbx=cpu->reg[regBX].u32;
    plan.entryEsi=cpu->reg[regSI].u32;
    plan.entryEdi=cpu->reg[regDI].u32;
    plan.entryEbp=cpu->reg[regBP].u32;
    return true;
}

void commitOuterElement(CPU* cpu, U32 base, const AcknexOuterElementPlan& plan) {
    KMemory* memory=cpu->memory;
    // Execute the two register MOVs before the architectural CALL boundary.
    // CPU::push32 commits the memory write before publishing the decremented
    // ESP, so a return-slot write fault must still expose outer ESP.
    cpu->reg[regDX].u32=plan.entryEbx;
    cpu->reg[regCX].u32=plan.entryEdi;
    cpu->eip.u32=base+0x00049654u;
    // This write is architecturally the CALL at +49654.  All stack and alias
    // gates completed before it, so a rejected element never leaves a partial
    // synthetic call behind.
    memory->writed(plan.callEsp,base+ACKNEX_OUTER_CONTINUE_RVA);
    cpu->reg[regSP].u32=plan.callEsp;
    cpu->eip.u32=base+ACKNEX_TRANSFORM_COORD_RVA;
    runTransformCoord(cpu,plan.destination,plan.source,plan.matrix,false);
    const U32 firstSourceZ=cpu->reg[regCX].u32;
    cpu->eip.u32=base+ACKNEX_TRANSFORM3X3_RVA;
    runTransform3x3(cpu,plan.destination+0x0c,plan.source+0x0c,plan.matrix,
        plan.callEsp,false);
    const U32 secondSourceZ=cpu->reg[regCX].u32;

    memory->writed(plan.callEsp-4,plan.entryEsi);
    memory->writed(plan.callEsp-8,plan.entryEdi);
    memory->writed(plan.callEsp-12,plan.entryEbx);
    memory->writed(plan.callEsp-16,plan.matrix);
    memory->writed(plan.callEsp-20,plan.source+0x0c);
    memory->writed(plan.callEsp-24,plan.destination+0x0c);
    memory->writed(plan.callEsp-28,base+0x000467a6u);
    memory->writed(plan.callEsp-32,plan.matrix);
    memory->writed(plan.callEsp-36,firstSourceZ);
    memory->writed(plan.alignedFrame,plan.entryEbp);
    memory->writed(plan.alignedFrame-4,secondSourceZ);
    cpu->reg[regDX].u32=plan.index37;
    cpu->reg[regSP].u32=plan.callEsp+4u;
}

void finishOuter(CPU* cpu, WasmJitAcknexOuterOutcome outcome, U32 completed,
        bool completedLoop, bool chunkLimit) {
    countAdd(WasmJitAcknexTransformCounter::TransformCoordRuns,completed);
    countAdd(WasmJitAcknexTransformCounter::Transform3x3Runs,completed);
    countAdd(WasmJitAcknexTransformCounter::OuterElements,completed);
    if (completed) count(WasmJitAcknexTransformCounter::OuterChunks);
    if (completedLoop) count(WasmJitAcknexTransformCounter::OuterCompletedLoops);
    if (chunkLimit) count(WasmJitAcknexTransformCounter::OuterChunkLimitStops);
    switch (outcome) {
    case WasmJitAcknexOuterOutcome::SafeFallback:
        count(WasmJitAcknexTransformCounter::OuterSafeFallback);
        cpu->tmpReg=0;
        return;
    case WasmJitAcknexOuterOutcome::PartialResume:
        count(WasmJitAcknexTransformCounter::OuterPartialResume);
        count(WasmJitAcknexTransformCounter::HelperSuccess);
        cpu->tmpReg=(U32)outcome;
        return;
    case WasmJitAcknexOuterOutcome::StaleCodeExit:
        count(WasmJitAcknexTransformCounter::OuterStaleCodeExit);
        cpu->wasmJitBailout=1;
        cpu->tmpReg=(U32)outcome;
        return;
    }
}

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE)
void injectOuterFixtureStop(CPU* cpu, U32 completed) {
    if (completed!=fixtureOuterStopAfter.load(std::memory_order_relaxed)) return;
    switch ((WasmJitAcknexOuterFixtureStop)fixtureOuterStopKind.load(std::memory_order_relaxed)) {
    case WasmJitAcknexOuterFixtureStop::Yield: cpu->yield=true; break;
    case WasmJitAcknexOuterFixtureStop::Bailout: cpu->wasmJitBailout=1; break;
    case WasmJitAcknexOuterFixtureStop::MappingEpoch:
        if (cpu->thread && cpu->thread->process)
            cpu->thread->process->bumpAcknexTransformMappingGeneration();
        break;
    case WasmJitAcknexOuterFixtureStop::Termination:
        if (cpu->thread) cpu->thread->terminating=true;
        break;
    default: break;
    }
}
#else
void injectOuterFixtureStop(CPU*, U32) {}
#endif

void runAcknexOuterLoop(CPU* cpu, U32 entry, U32 base) {
    count(WasmJitAcknexTransformCounter::OuterHelperEntries);
    KProcess* process=cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    KMemory* memory=cpu->memory;
    if (!process || entry!=base+ACKNEX_OUTER_ENTRY_RVA) {
        count(WasmJitAcknexTransformCounter::FallbackIdentity);
        cpu->eip.u32=entry-cpu->seg[CS].address;
        finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,0,false,false);
        return;
    }
    if (negativeCodeCacheMatches(cpu,entry,base)) {
        if (captureFirstMismatch(cpu,entry,base,negativeCodeCache.mismatch))
            count(mismatchCounter(negativeCodeCache.mismatch.component));
        count(WasmJitAcknexTransformCounter::FallbackCodeBytes);
        count(WasmJitAcknexTransformCounter::FallbackCodeNegativeCached);
        cpu->eip.u32=entry-cpu->seg[CS].address;
        finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,0,false,false);
        return;
    }
    CodeMismatch mismatch;
    if (!validateCodeContract(memory,entry,base,mismatch)) {
        rememberNegativeCode(cpu,entry,base,mismatch);
        count(mismatchCounter(mismatch.component));
        captureFirstMismatch(cpu,entry,base,mismatch);
        count(WasmJitAcknexTransformCounter::FallbackCodeBytes);
        cpu->eip.u32=entry-cpu->seg[CS].address;
        finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,0,false,false);
        return;
    }
    if (cpu->mxcsr!=0x1f80u) {
        count(WasmJitAcknexTransformCounter::FallbackMxcsr);
        finishOuter(cpu,WasmJitAcknexOuterOutcome::SafeFallback,0,false,false);
        return;
    }
    if (cpu->seg[CS].address!=0 || cpu->seg[DS].address!=0 || cpu->seg[SS].address!=0 ||
            cpu->stackMask!=0xffffffffu || cpu->stackNotMask!=0) {
        count(WasmJitAcknexTransformCounter::FallbackSegment);
        finishOuter(cpu,WasmJitAcknexOuterOutcome::SafeFallback,0,false,false);
        return;
    }

    const U64 admittedGeneration=process->getAcknexTransformMappingGeneration();
    U32 completed=0;
    auto stopAtBoundary=[&](U32 resumeEip,bool completedLoop)->bool {
        cpu->eip.u32=resumeEip;
        if (process->getAcknexTransformMappingGeneration()!=admittedGeneration) {
            count(WasmJitAcknexTransformCounter::OuterEpochStops);
            finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,completed,completedLoop,false);
            return true;
        }
        if (cpu->wasmJitBailout) {
            count(WasmJitAcknexTransformCounter::OuterBailoutStops);
            finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,completed,completedLoop,false);
            return true;
        }
        if (cpu->yield) {
            count(WasmJitAcknexTransformCounter::OuterYieldStops);
            finishOuter(cpu,WasmJitAcknexOuterOutcome::PartialResume,completed,completedLoop,false);
            return true;
        }
        if (!cpu->thread || cpu->thread->terminating || process->terminated) {
            count(WasmJitAcknexTransformCounter::OuterTerminationStops);
            finishOuter(cpu,WasmJitAcknexOuterOutcome::PartialResume,completed,completedLoop,false);
            return true;
        }
        return false;
    };
    while (completed<ACKNEX_OUTER_CHUNK_ELEMENTS) {
        injectOuterFixtureStop(cpu,completed);
        if (stopAtBoundary(base+ACKNEX_OUTER_ENTRY_RVA,false)) return;

        AcknexOuterElementPlan plan;
        if (!prepareOuterElement(cpu,base,plan)) {
            if (!completed) {
                finishOuter(cpu,WasmJitAcknexOuterOutcome::SafeFallback,0,false,false);
            } else {
                cpu->eip.u32=base+ACKNEX_OUTER_ENTRY_RVA;
                finishOuter(cpu,WasmJitAcknexOuterOutcome::PartialResume,completed,false,false);
            }
            return;
        }
        // prepareOuterElement performs only reads.  Re-check the asynchronous
        // and content epochs immediately before the synthetic CALL becomes the
        // first architectural write of this element.
        if (stopAtBoundary(base+ACKNEX_OUTER_ENTRY_RVA,false)) return;
        commitOuterElement(cpu,base,plan);
        ++completed;

        // Reproduce +49659..+4966b at instruction boundaries.  The EIP updates
        // matter if a concurrent protection change makes either controller
        // read fault after the transform has already committed.
        cpu->eip.u32=base+ACKNEX_OUTER_CONTINUE_RVA;
        if (stopAtBoundary(base+ACKNEX_OUTER_CONTINUE_RVA,false)) return;
        const U32 outerState=memory->readd(base+ACKNEX_OUTER_STATE_GLOBAL_RVA);
        cpu->reg[regCX].u32=outerState;

        const U32 oldEsi=cpu->reg[regSI].u32;
        cpu->reg[regSI].u32=oldEsi+1u;
        setAdd32Lazy(cpu,oldEsi,1u,cpu->reg[regSI].u32);
        const U32 oldEdi=cpu->reg[regDI].u32;
        cpu->reg[regDI].u32=oldEdi+0x2cu;
        setAdd32Lazy(cpu,oldEdi,0x2cu,cpu->reg[regDI].u32);
        const U32 oldEbx=cpu->reg[regBX].u32;
        cpu->reg[regBX].u32=oldEbx+0x2cu;
        setAdd32Lazy(cpu,oldEbx,0x2cu,cpu->reg[regBX].u32);

        cpu->eip.u32=base+0x00049668u;
        const U32 loopCount=memory->readd(outerState+0x28u);
        cpu->dst.u32=cpu->reg[regSI].u32;
        cpu->src.u32=loopCount;
        cpu->result.u32=cpu->dst.u32-cpu->src.u32;
        cpu->lazyFlagType=FLAGS_CMP32;
        const bool taken=signedLessFromSub(cpu->dst.u32,cpu->src.u32,cpu->result.u32);
        const U32 resumeEip=taken ? base+ACKNEX_OUTER_ENTRY_RVA :
            base+ACKNEX_OUTER_EPILOGUE_RVA;
        // This check closes the natural-exit and 64th-element race: a code or
        // permission epoch raised during the controller may not escape through
        // a normal grouped tail with the old admission.
        if (stopAtBoundary(resumeEip,!taken)) return;
        if (!taken) {
            finishOuter(cpu,WasmJitAcknexOuterOutcome::PartialResume,completed,true,false);
            return;
        }
    }
    if (stopAtBoundary(base+ACKNEX_OUTER_ENTRY_RVA,false)) return;
    finishOuter(cpu,WasmJitAcknexOuterOutcome::PartialResume,completed,false,true);
}
#endif

} // namespace

bool wasmJitAcknexTransformIslandIsCandidate(CPU* cpu, U32 entryAddress) {
    count(WasmJitAcknexTransformCounter::CandidateQueries);
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    const bool outerQuery=wasmJitAcknexTransformIslandIsOuterEntry(cpu,entryAddress);
    if (outerQuery) count(WasmJitAcknexTransformCounter::OuterCandidateQueries);
#endif
#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
    if (entryAddress == fixtureModuleBase.load(std::memory_order_relaxed)+ACKNEX_TRANSFORM_ENTRY_RVA) {
        U32 base = 0;
        if (!resolveIdentity(cpu, entryAddress, base)) {
            count(WasmJitAcknexTransformCounter::CandidateContentMismatch);
            return false;
        }
        count(WasmJitAcknexTransformCounter::CandidateAccepted);
        return true;
    }
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (entryAddress == fixtureModuleBase.load(std::memory_order_relaxed)+ACKNEX_OUTER_ENTRY_RVA) {
        U32 base=0;
        if (!resolveIdentity(cpu,entryAddress,base)) {
            count(WasmJitAcknexTransformCounter::CandidateContentMismatch);
            return false;
        }
        count(WasmJitAcknexTransformCounter::CandidateAccepted);
        count(WasmJitAcknexTransformCounter::OuterCandidateAccepted);
        return true;
    }
#endif
#endif
    KProcess* process = cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    MappedFilePtr mapping = process ? process->getMappedFileForRange(entryAddress, 1) : nullptr;
    if (!mapping || !mapping->file || !mapping->file->openFile ||
            !mapping->file->openFile->node ||
            !mapping->file->openFile->node->path.endsWith("acknex.dll", true)) {
        count(WasmJitAcknexTransformCounter::CandidateWrongMapping);
        return false;
    }
    const U64 fileOffset=(U64)(entryAddress-mapping->address)+mapping->offset;
    const bool transformEntry=fileOffset==ACKNEX_TRANSFORM_ENTRY_RVA;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    const bool outerEntry=fileOffset==ACKNEX_OUTER_ENTRY_RVA;
#else
    const bool outerEntry=false;
#endif
    if (!transformEntry && !outerEntry) {
        count(WasmJitAcknexTransformCounter::CandidateWrongMapping);
        return false;
    }
    U32 base = 0;
    if (!resolveIdentity(cpu, entryAddress, base)) {
        count(WasmJitAcknexTransformCounter::CandidateContentMismatch);
        return false;
    }
    U32 expectedRva=ACKNEX_TRANSFORM_ENTRY_RVA;
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (outerEntry) expectedRva=ACKNEX_OUTER_ENTRY_RVA;
#endif
    if (entryAddress != base + expectedRva) {
        count(WasmJitAcknexTransformCounter::CandidateWrongMapping);
        return false;
    }
    count(WasmJitAcknexTransformCounter::CandidateAccepted);
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (outerEntry) count(WasmJitAcknexTransformCounter::OuterCandidateAccepted);
#endif
    return true;
}

#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
bool wasmJitAcknexTransformIslandIsOuterEntry(CPU* cpu, U32 entryAddress) {
#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
    if (entryAddress==fixtureModuleBase.load(std::memory_order_relaxed)+ACKNEX_OUTER_ENTRY_RVA)
        return true;
#endif
    KProcess* process=cpu && cpu->thread && cpu->thread->process ?
        cpu->thread->process.get() : nullptr;
    MappedFilePtr mapping=process ? process->getMappedFileForRange(entryAddress,1) : nullptr;
    return mapping && mapping->file && mapping->file->openFile &&
        mapping->file->openFile->node &&
        mapping->file->openFile->node->path.endsWith("acknex.dll",true) &&
        (U64)(entryAddress-mapping->address)+mapping->offset==ACKNEX_OUTER_ENTRY_RVA;
}
#endif

void wasmJitAcknexTransformIslandRecordBlockHashMismatch() {
    count(WasmJitAcknexTransformCounter::CandidateBlockHashMismatch);
}

void wasmJitAcknexTransformIslandHelper(CPU* cpu) {
    count(WasmJitAcknexTransformCounter::HelperEntries);
    cpu->tmpReg = 0;
    const U32 entry = cpu->getEipAddress();
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    const bool generatedOuterMarker=cpu->memHelperValue==1u;
#endif
    U32 base = 0;
    if (!resolveIdentity(cpu, entry, base)) {
        count(WasmJitAcknexTransformCounter::FallbackIdentity);
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
        if (generatedOuterMarker) {
            count(WasmJitAcknexTransformCounter::OuterHelperEntries);
            cpu->eip.u32=entry-cpu->seg[CS].address;
            finishOuter(cpu,WasmJitAcknexOuterOutcome::StaleCodeExit,0,false,false);
        }
#endif
        return;
    }
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    if (entry==base+ACKNEX_OUTER_ENTRY_RVA) {
        runAcknexOuterLoop(cpu,entry,base);
        return;
    }
#endif
    KMemory* memory = cpu->memory;
    const U32 target3x3 = base + ACKNEX_TRANSFORM3X3_RVA;
    const U32 targetCoord = base + ACKNEX_TRANSFORM_COORD_RVA;
    if (negativeCodeCacheMatches(cpu,entry,base)) {
        if (captureFirstMismatch(cpu,entry,base,negativeCodeCache.mismatch))
            count(mismatchCounter(negativeCodeCache.mismatch.component));
        count(WasmJitAcknexTransformCounter::FallbackCodeBytes);
        count(WasmJitAcknexTransformCounter::FallbackCodeNegativeCached);
        return;
    }
    CodeMismatch mismatch;
    if (!validateCodeContract(memory,entry,base,mismatch)) {
        rememberNegativeCode(cpu,entry,base,mismatch);
        count(mismatchCounter(mismatch.component));
        captureFirstMismatch(cpu,entry,base,mismatch);
        count(WasmJitAcknexTransformCounter::FallbackCodeBytes);
        return;
    }
    if (!readable(memory,base+ACKNEX_IAT_COORD_RVA,4) ||
        !readable(memory,base+ACKNEX_IAT3X3_RVA,4) ||
        // +84364 is the position/projective call and +84477 is the normal
        // 3x3 call.  The import slots are intentionally opposite the helper
        // function-name order in the PE.
        memory->readd(base+ACKNEX_IAT_COORD_RVA) != targetCoord ||
        memory->readd(base+ACKNEX_IAT3X3_RVA) != target3x3) {
        count(WasmJitAcknexTransformCounter::FallbackLiveTarget);
        return;
    }
    if (cpu->mxcsr != 0x1f80u) {
        count(WasmJitAcknexTransformCounter::FallbackMxcsr);
        return;
    }
    if (cpu->seg[CS].address != 0 || cpu->seg[DS].address != 0 || cpu->seg[SS].address != 0 ||
            cpu->stackMask != 0xffffffffu || cpu->stackNotMask != 0) {
        count(WasmJitAcknexTransformCounter::FallbackSegment);
        return;
    }
    const U32 entryEsp = cpu->reg[regSP].u32;
    if (entryEsp < 96u) {
        count(WasmJitAcknexTransformCounter::FallbackStack);
        return;
    }
    const U32 alignedFrame = (entryEsp - 36u) & ~0x0fu;
    const U32 stackScratchStart = alignedFrame - 32u;
    const U32 stackScratchBytes = entryEsp - stackScratchStart;
    if (!readable(memory,stackScratchStart,stackScratchBytes+4u) ||
            !writable(memory,stackScratchStart,stackScratchBytes)) {
        count(WasmJitAcknexTransformCounter::FallbackStack);
        return;
    }
    const U32 destination = cpu->reg[regCX].u32;
    const U32 source = cpu->reg[regDX].u32;
    if (!readable(memory,source,0x24) || !writable(memory,destination,0x18) ||
        !readable(memory,base+ACKNEX_ENGINE_GLOBAL_RVA,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return;
    }
    const S32 matrixIndex = (S32)memory->readd(source+0x20);
    if (matrixIndex < 0) {
        count(WasmJitAcknexTransformCounter::FallbackNegativeIndex);
        return;
    }
    const U32 engine = memory->readd(base+ACKNEX_ENGINE_GLOBAL_RVA);
    if (!readable(memory,engine+0x4f8,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return;
    }
    const U32 matrixTable = memory->readd(engine+0x4f8);
    const U32 index37 = (U32)matrixIndex + ((U32)matrixIndex + (U32)matrixIndex*8u)*4u;
    const U32 matrixRecord = matrixTable + index37*4u;
    if (!matrixRecord) {
        count(WasmJitAcknexTransformCounter::FallbackNullMatrix);
        return;
    }
    if (!readable(memory,matrixRecord+4,4)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return;
    }
    if (memory->readd(matrixRecord+4) != 3) {
        count(WasmJitAcknexTransformCounter::FallbackType);
        return;
    }
    const U32 matrix = matrixRecord + 0x14;
    if ((destination|source|matrix) & 3u) {
        count(WasmJitAcknexTransformCounter::FallbackUnaligned);
        return;
    }
    if (!readable(memory,matrix,0x40)) {
        count(WasmJitAcknexTransformCounter::FallbackMemory);
        return;
    }
    if (rangesOverlap(stackScratchStart,stackScratchBytes+4u,source,0x24) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,destination,0x18) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,matrix,0x40) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,base+ACKNEX_ENGINE_GLOBAL_RVA,4) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,engine+0x4f8,4) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,matrixRecord+4,4) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,entry,sizeof(ACKNEX_TRANSFORM_CODE)) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,base+ACKNEX_THUNK_COORD_RVA,sizeof(ACKNEX_THUNK_COORD_CODE)) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,base+ACKNEX_THUNK3X3_RVA,sizeof(ACKNEX_THUNK3X3_CODE)) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,targetCoord,sizeof(ACKNEX_TRANSFORM_COORD_CODE)) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,target3x3,sizeof(ACKNEX_TRANSFORM3X3_CODE)) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,base+ACKNEX_IAT_COORD_RVA,4) ||
        rangesOverlap(stackScratchStart,stackScratchBytes+4u,base+ACKNEX_IAT3X3_RVA,4) ||
        rangesOverlap(destination,0x18,entry,sizeof(ACKNEX_TRANSFORM_CODE)) ||
        rangesOverlap(destination,0x18,base+ACKNEX_THUNK_COORD_RVA,sizeof(ACKNEX_THUNK_COORD_CODE)) ||
        rangesOverlap(destination,0x18,base+ACKNEX_THUNK3X3_RVA,sizeof(ACKNEX_THUNK3X3_CODE)) ||
        rangesOverlap(destination,0x18,targetCoord,sizeof(ACKNEX_TRANSFORM_COORD_CODE)) ||
        rangesOverlap(destination,0x18,target3x3,sizeof(ACKNEX_TRANSFORM3X3_CODE)) ||
        rangesOverlap(destination,0x18,base+ACKNEX_IAT_COORD_RVA,4) ||
        rangesOverlap(destination,0x18,base+ACKNEX_IAT3X3_RVA,4)) {
        count(WasmJitAcknexTransformCounter::FallbackAlias);
        return;
    }

    // FUN_10046760 transforms position/projective first and normal/3x3 second.
    // Do not merge
    // their input snapshots: the first destination can legally alias the
    // second source or matrix and the guest observes that ordering.
    const U32 entryEbx=cpu->reg[regBX].u32, entryEsi=cpu->reg[regSI].u32;
    const U32 entryEdi=cpu->reg[regDI].u32, entryEbp=cpu->reg[regBP].u32;
    runTransformCoord(cpu,destination,source,matrix);
    const U32 firstSourceZ=cpu->reg[regCX].u32;
    runTransform3x3(cpu,destination+0x0c,source+0x0c,matrix,entryEsp);
    const U32 secondSourceZ=cpu->reg[regCX].u32;

    // Reproduce the final below-ESP memory image left by wrapper pushes, the
    // second CALL/prologue and its source-Z local.  The scratch-read range was
    // preflighted above; disjoint admission prevents these writes from
    // changing transform inputs or module identity mid-helper.
    memory->writed(entryEsp-4,entryEsi);
    memory->writed(entryEsp-8,entryEdi);
    memory->writed(entryEsp-12,entryEbx);
    memory->writed(entryEsp-16,matrix);
    memory->writed(entryEsp-20,source+0x0c);
    memory->writed(entryEsp-24,destination+0x0c);
    memory->writed(entryEsp-28,base+0x000467a6u);
    memory->writed(entryEsp-32,matrix);
    memory->writed(entryEsp-36,firstSourceZ);
    memory->writed(alignedFrame,entryEbp);
    memory->writed(alignedFrame-4,secondSourceZ);
    cpu->reg[regDX].u32 = index37;
    cpu->eip.u32 = cpu->pop32();
    cpu->tmpReg = 1;
    count(WasmJitAcknexTransformCounter::HelperSuccess);
}

extern "C" U32 wasm_jit_get_acknex_transform_island_abi_version() {
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    return 3;
#else
    return 2;
#endif
}
extern "C" U32 wasm_jit_get_acknex_transform_island_counter_count() {
    return WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_acknex_transform_island_snapshot_bytes() {
    return WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT * (U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_acknex_transform_island_snapshot(U64* output, U32 outputBytes) {
    if (!output || outputBytes < wasm_jit_get_acknex_transform_island_snapshot_bytes()) return;
    for (U32 i=0;i<WASM_JIT_ACKNEX_TRANSFORM_COUNTER_COUNT;++i)
        output[i]=counters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_acknex_transform_island_counters() {
    for (auto& value:counters) value.store(0,std::memory_order_relaxed);
}

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_TRANSFORM_ISLAND_FIXTURE)
void wasmJitAcknexTransformIslandFixtureSetIdentity(bool valid) {
    fixtureIdentityValid.store(valid,std::memory_order_relaxed);
    identityCache = {};
    negativeCodeCache = {};
}
void wasmJitAcknexTransformIslandFixtureSetModuleBase(U32 moduleBase) {
    fixtureModuleBase.store(moduleBase,std::memory_order_relaxed);
    identityCache={};
    negativeCodeCache={};
}
void wasmJitAcknexTransformIslandFixtureInstallCode(KMemory* memory) {
    negativeCodeCache={};
    const U32 base=fixtureModuleBase.load(std::memory_order_relaxed);
    memory->memcpy(base+ACKNEX_TRANSFORM_ENTRY_RVA,ACKNEX_TRANSFORM_CODE,sizeof(ACKNEX_TRANSFORM_CODE));
    memory->writed(base+ACKNEX_TRANSFORM_ENTRY_RVA+0x13,base+ACKNEX_ENGINE_GLOBAL_RVA);
    memory->memcpy(base+ACKNEX_THUNK_COORD_RVA,ACKNEX_THUNK_COORD_CODE,sizeof(ACKNEX_THUNK_COORD_CODE));
    memory->writed(base+ACKNEX_THUNK_COORD_RVA+2,base+ACKNEX_IAT_COORD_RVA);
    memory->memcpy(base+ACKNEX_THUNK3X3_RVA,ACKNEX_THUNK3X3_CODE,sizeof(ACKNEX_THUNK3X3_CODE));
    memory->writed(base+ACKNEX_THUNK3X3_RVA+2,base+ACKNEX_IAT3X3_RVA);
    memory->memcpy(base+ACKNEX_TRANSFORM3X3_RVA,ACKNEX_TRANSFORM3X3_CODE,sizeof(ACKNEX_TRANSFORM3X3_CODE));
    memory->memcpy(base+ACKNEX_TRANSFORM_COORD_RVA,ACKNEX_TRANSFORM_COORD_CODE,sizeof(ACKNEX_TRANSFORM_COORD_CODE));
    memory->writed(base+ACKNEX_IAT_COORD_RVA,base+ACKNEX_TRANSFORM_COORD_RVA);
    memory->writed(base+ACKNEX_IAT3X3_RVA,base+ACKNEX_TRANSFORM3X3_RVA);
#ifdef BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FUSION
    memory->memcpy(base+ACKNEX_OUTER_ENTRY_RVA,ACKNEX_OUTER_CODE,sizeof(ACKNEX_OUTER_CODE));
    memory->writed(base+ACKNEX_OUTER_ENTRY_RVA+ACKNEX_OUTER_CODE_GLOBAL_OFFSET,
        base+ACKNEX_OUTER_STATE_GLOBAL_RVA);
#endif
}
bool wasmJitAcknexTransformIslandRunForTest(CPU* cpu) {
    wasmJitAcknexTransformIslandHelper(cpu);
    return cpu->tmpReg == 1;
}
bool wasmJitAcknexTransformIslandSha256SelfTest() {
    static constexpr U8 expected[32]={
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Sha256 sha; const U8 input[3]={'a','b','c'}; U8 output[32];
    sha.update(input,3); sha.finish(output);
    return std::memcmp(output,expected,32)==0;
}
#endif

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_ACKNEX_OUTER_LOOP_FIXTURE)
U32 wasmJitAcknexOuterLoopRunForTest(CPU* cpu) {
    cpu->memHelperValue=1;
    wasmJitAcknexTransformIslandHelper(cpu);
    return cpu->tmpReg;
}
void wasmJitAcknexOuterLoopFixtureSetStop(U32 completedElements,
        WasmJitAcknexOuterFixtureStop stop) {
    fixtureOuterStopAfter.store(completedElements,std::memory_order_relaxed);
    fixtureOuterStopKind.store((U32)stop,std::memory_order_relaxed);
}
#endif

#endif
