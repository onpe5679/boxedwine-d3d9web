/*
 * Exact-content host cache for Azrok's repeated HP_*.wis loader.
 *
 * A miss always executes the original guest loader.  The capture boundary
 * snapshots the complete 0x20000 bytes observed by the original descriptor;
 * only a second identical tail observation admits a synthetic descriptor.
 * Synthetic descriptors are never linked into the guest CResourceMan list.
 */

#include "boxedwine.h"
#include "wasmJitAzrokFaceWisHle.h"

#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE)

#include "wasmJitAzrokFaceWisManifest.h"
#include "../../../io/fsfilenode.h"
#include "../../../io/fsopennode.h"
#include "../../../io/fszip.h"
#include "../../../io/fszipnode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>

namespace {

constexpr U32 LOOKUP_EIP = 0x00551198u;
constexpr U32 CAPTURE_EIP = 0x005511b1u;
constexpr U32 DESCRIPTOR_PAYLOAD_OFFSET = 0x24u;
constexpr U32 DESCRIPTOR_FIELD0_EXPECTED =
    WASM_JIT_AZROK_FACE_WIS_HEIGHT; // loader writes header dword #3
constexpr U32 DESCRIPTOR_FIELD4_EXPECTED =
    WASM_JIT_AZROK_FACE_WIS_WIDTH;  // loader writes header dword #2
constexpr U32 SYNTHETIC_PAYLOAD_OFFSET = 0x1000u;
constexpr U32 SYNTHETIC_SLAB_BYTES = 0x21000u;
constexpr U32 CACHE_PHYSICAL_CAP = 16u * 1024u * 1024u;
constexpr U32 TAIL_PROBE_HITS = 1024u;
constexpr U32 PATH_LIMIT = 256u;
constexpr U32 EXE_FILE_BYTES = 2199552u;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
constexpr U32 PROCESS_IDENTITY_STABILITY_WARMUP_BOUNDARIES = 256u;
#endif

#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1) || \
        defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1)
std::array<std::atomic<U64>,WASM_JIT_AZROK_FACE_WIS_CWD_EPOCH_COUNTER_COUNT>
    cwdEpochCounters = {};
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
constexpr U32 PROCESS_IDENTITY_RETRY_INTERVAL = 64u;
static_assert((PROCESS_IDENTITY_RETRY_INTERVAL &
    (PROCESS_IDENTITY_RETRY_INTERVAL - 1u)) == 0u);
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
std::array<std::atomic<U64>,
    WASM_JIT_AZROK_FACE_WIS_SCOPED_GENERATION_COUNTER_COUNT>
    scopedGenerationCounters = {};
inline void countScopedGeneration(
        WasmJitAzrokFaceWisScopedGenerationCounter counter, U64 value = 1) {
    scopedGenerationCounters[(U32)counter].fetch_add(value,
        std::memory_order_relaxed);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
std::array<std::atomic<U64>,WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT>
    warmupCounters = {};
std::atomic<U64> warmupNextGeneration{1};
std::atomic<U64> warmupGaugeState{0};
inline void countWarmup(WasmJitAzrokFaceWisWarmupCounter counter,
        U64 value = 1) {
    warmupCounters[(U32)counter].fetch_add(value,std::memory_order_relaxed);
}
inline void lowerWarmupGauge(U64 packedState) {
    U64 currentState=warmupGaugeState.load(std::memory_order_relaxed);
    while ((currentState>>32)==(packedState>>32) &&
            (U32)currentState>(U32)packedState) {
        if (warmupGaugeState.compare_exchange_weak(currentState,packedState,
                std::memory_order_relaxed,std::memory_order_relaxed)) {
            return;
        }
    }
}
#endif

constexpr const char* APP_ZIP_SUFFIX =
    "Azrok_D3D9Web_pathouse_guestprofiles6_rsdefaults1_utf8ime2_"
    "dynamicaccounts2_startersplash1_fastboot4.zip";
constexpr const char* ROOT_ZIP_SUFFIX =
    "TinyCore15Wine11.0-wsaeventfix5-korean4-bgmloop3-dsound80ms1-"
    "pcrestd1-noprefixupd1-acp949-xim25koreanlocale2.zip";
constexpr const char* APP_GUEST_BASE =
    "/home/username/.wine/dosdevices/c:/files/";
constexpr const char* APP_DOS_BASE = "c:/files/";
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
constexpr const char* CWD_WRITER_MODULE_SUFFIX =
    "/opt/wine/lib/wine/i386-unix/ntdll.dll.so";
constexpr U32 CWD_WRITER_MODULE_BYTES = 872392u;
constexpr const char* CWD_WRITER_MODULE_SHA256 =
    "211c4bc423a05e971355012660898c963b8582f53ac1123258b0b10c9bb09955";
constexpr U32 CWD_WRITER_ENTRY_RVA = 0x0006462au;
constexpr U32 CWD_WRITER_EXIT_RVA = 0x000646deu;
constexpr U8 CWD_WRITER_ENTRY_BYTES[] =
    {0x64,0xa1,0x18,0x00,0x00,0x00,0x8b,0x50,0x0c};
constexpr U8 CWD_WRITER_EXIT_BYTES[] =
    {0x89,0xf0,0x8d,0x65,0xf0,0x59,0x5b,0x5e};
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
// Wine 11 i386 ntdll/path.c uses PEB ProcessParameters CurrentDirectory.DosPath
// for relative Win32 path expansion. Read that authoritative guest state, not
// BoxedWine KProcess::currentDirectory.
constexpr U32 I386_TEB_PEB_OFFSET = 0x30u;
constexpr U32 I386_PEB_PROCESS_PARAMETERS_OFFSET = 0x10u;
constexpr U32 I386_RTL_USER_PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET = 0x24u;
constexpr U32 I386_UNICODE_STRING_LENGTH_OFFSET = 0u;
constexpr U32 I386_UNICODE_STRING_MAXIMUM_LENGTH_OFFSET = 2u;
constexpr U32 I386_UNICODE_STRING_BUFFER_OFFSET = 4u;
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
std::array<std::atomic<U64>,WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_COUNTER_COUNT>
    cwdDiscoveryCounters = {};
#endif

constexpr U64 ROOT_LIBC_CENTRAL_OFFSET = 167358053ULL;
constexpr U64 ROOT_LIBC_COMPRESSED_LENGTH = 722748ULL;
constexpr U32 ROOT_LIBC_FILE_BYTES = 1655568u;
constexpr U32 ROOT_LIBC_CRC32 = 0x0e6d6118u;
constexpr const char* ROOT_LIBC_SHA256 =
    "141319771edb3c2ffbd91d7617dc133bc70eafa3bf718defa60d11ba5a729d7d";
constexpr const char* EXE_SHA256 =
    "39680da744df27cf13eae03f675d91c301b3ca9332f6979ca4447fba9908caba";

constexpr U8 LOOKUP_CODE[] = {
    0xc7,0x45,0xfc,0x00,0x00,0x00,0x00,
    0x8d,0x85,0xec,0xfe,0xff,0xff,
    0x50,
    0x8d,0x8d,0xc4,0xfe,0xff,0xff,
    0xe8,0x46,0xa3,0xf8,0xff,
};
constexpr U8 CAPTURE_CODE[] = {
    0x89,0x85,0xc0,0xfe,0xff,0xff,
    0x83,0xbd,0xc0,0xfe,0xff,0xff,0x00,
    0x75,0x17,
};
static_assert(sizeof(LOOKUP_CODE) == 25u);
static_assert(sizeof(CAPTURE_CODE) == 15u);
static_assert(offsetof(CPU, eip) == 112u);
static_assert(offsetof(CPU, tmpReg) == 116u,
    "group wrapper pins CPU::tmpReg at linear-memory offset 116");
static_assert(SYNTHETIC_SLAB_BYTES * WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT <=
    CACHE_PHYSICAL_CAP);

std::array<std::atomic<U64>, WASM_JIT_AZROK_FACE_WIS_COUNTER_COUNT> counters = {};

inline void count(WasmJitAzrokFaceWisCounter counter, U64 value = 1) {
    counters[(U32)counter].fetch_add(value, std::memory_order_relaxed);
}

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
std::array<std::atomic<U64>, WASM_JIT_AZROK_FACE_WIS_RETRY_COUNTER_COUNT>
    retryCounters = {};
inline void countRetry(WasmJitAzrokFaceWisRetryCounter counter, U64 value = 1) {
    retryCounters[(U32)counter].fetch_add(value, std::memory_order_relaxed);
}
#endif

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
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) |
            ((U32)p[2] << 8) | p[3];
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
        for (U32 i=0;i<16;++i) w[i]=loadBe(block+i*4);
        for (U32 i=16;i<64;++i) {
            const U32 s0=rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3);
            const U32 s1=rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        U32 a=state[0],b=state[1],c=state[2],d=state[3];
        U32 e=state[4],f=state[5],g=state[6],h=state[7];
        for (U32 i=0;i<64;++i) {
            const U32 s1=rotate(e,6)^rotate(e,11)^rotate(e,25);
            const U32 ch=(e&f)^(~e&g);
            const U32 t1=h+s1+ch+k[i]+w[i];
            const U32 s0=rotate(a,2)^rotate(a,13)^rotate(a,22);
            const U32 maj=(a&b)^(a&c)^(b&c);
            const U32 t2=s0+maj;
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
        state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(const U8* data, U32 len) {
        bytes += len;
        while (len) {
            const U32 take=std::min<U32>(len,64-pendingBytes);
            std::memcpy(pending+pendingBytes,data,take);
            pendingBytes+=take;data+=take;len-=take;
            if (pendingBytes==64) { transform(pending); pendingBytes=0; }
        }
    }
    void finish(U8 output[32]) {
        const U64 bitCount=bytes*8;
        pending[pendingBytes++]=0x80;
        if (pendingBytes>56) {
            std::memset(pending+pendingBytes,0,64-pendingBytes);
            transform(pending);pendingBytes=0;
        }
        std::memset(pending+pendingBytes,0,56-pendingBytes);
        for (U32 i=0;i<8;++i) pending[63-i]=(U8)(bitCount>>(i*8));
        transform(pending);
        for (U32 i=0;i<8;++i) {
            output[i*4]=(U8)(state[i]>>24);
            output[i*4+1]=(U8)(state[i]>>16);
            output[i*4+2]=(U8)(state[i]>>8);
            output[i*4+3]=(U8)state[i];
        }
    }
};

U8 hexNibble(char value) {
    if (value>='0' && value<='9') return (U8)(value-'0');
    if (value>='a' && value<='f') return (U8)(value-'a'+10);
    if (value>='A' && value<='F') return (U8)(value-'A'+10);
    return 0xff;
}

bool digestMatches(const U8 digest[32], const char* expected) {
    if (!expected) return false;
    for (U32 i=0;i<32;++i) {
        const U8 hi=hexNibble(expected[i*2]);
        const U8 lo=hexNibble(expected[i*2+1]);
        if (hi>0x0f || lo>0x0f || digest[i]!=(U8)((hi<<4)|lo)) return false;
    }
    return expected[64]==0;
}

bool memoryDigest(KMemory* memory, U32 address, U32 len, Sha256& sha) {
    if (!len || (U64)address+len>0x100000000ULL ||
            !memory->canRead(address,len)) return false;
    bool ok=true;
    memory->performOnMemory(address,len,true,[&](U8* data,U32 chunk) {
        sha.update(data,chunk);
        return ok;
    });
    return ok;
}

bool hashNode(const std::shared_ptr<FsNode>& node, U32 expectedBytes,
        const char* expectedSha
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        , bool* readFailure = nullptr
#endif
        ) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (readFailure) *readFailure=false;
#endif
    if (!node || node->isDirectory() || node->length()!=expectedBytes) return false;
    FsOpenNode* open=node->open(K_O_RDONLY);
    if (!open) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        if (readFailure) *readFailure=true;
#endif
        return false;
    }
    Sha256 sha;
    U8 buffer[16384];
    U32 total=0;
    bool ok=true;
    while (total<expectedBytes) {
        const U32 want=std::min<U32>((U32)sizeof(buffer),expectedBytes-total);
        const U32 got=open->readNative(buffer,want);
        if (got!=want) {
            ok=false;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
            if (readFailure) *readFailure=true;
#endif
            break;
        }
        sha.update(buffer,got);
        total+=got;
    }
    open->close();
    delete open;
    U8 digest[32];
    sha.finish(digest);
    return ok && total==expectedBytes && digestMatches(digest,expectedSha);
}

bool hashMappedFile(const MappedFilePtr& mapping, U32 expectedBytes,
        const char* expectedSha
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        , bool* readFailure = nullptr
#endif
        ) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (readFailure) *readFailure=false;
#endif
    if (!mapping || !mapping->file || mapping->file->length()!=expectedBytes)
        return false;
    Sha256 sha;
    U8 buffer[16384];
    U32 offset=0;
    while (offset<expectedBytes) {
        const U32 want=std::min<U32>((U32)sizeof(buffer),expectedBytes-offset);
        if (mapping->file->preadNative(buffer,offset,want)!=want) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
            if (readFailure) *readFailure=true;
#endif
            return false;
        }
        sha.update(buffer,want);
        offset+=want;
    }
    U8 digest[32];
    sha.finish(digest);
    return digestMatches(digest,expectedSha);
}

bool readOnlyMemoryEquals(KMemory* memory,U32 address,const U8* bytes,U32 len) {
    if (!memory || !bytes || !len || (U64)address+len>0x100000000ULL ||
            !memory->canRead(address,len)) return false;
    bool equal=true;
    const U8* cursor=bytes;
    memory->performOnMemory(address,len,true,[&](U8* ram,U32 chunk) {
        if (std::memcmp(cursor,ram,chunk)!=0) {
            equal=false;
            return false;
        }
        cursor+=chunk;
        return true;
    });
    return equal;
}

bool codeMatches(KMemory* memory,U32 address,const U8* bytes,U32 len) {
    return memory && memory->canExec(address>>K_PAGE_SHIFT) &&
        readOnlyMemoryEquals(memory,address,bytes,len);
}

#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1) || \
        defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1)
struct GuestWineCwdSnapshot {
    U32 teb=0,peb=0,parameters=0,dosPath=0;
    U32 length=0,maximumLength=0,buffer=0;
    U16 normalized[10]={};
};
bool sameGuestWineCwdSnapshot(const GuestWineCwdSnapshot& a,
        const GuestWineCwdSnapshot& b) {
    return a.teb==b.teb && a.peb==b.peb && a.parameters==b.parameters &&
        a.dosPath==b.dosPath && a.length==b.length &&
        a.maximumLength==b.maximumLength && a.buffer==b.buffer &&
        std::memcmp(a.normalized,b.normalized,sizeof(a.normalized))==0;
}
bool guestWineDosCwdIsFixedAppBase(CPU* cpu,
        GuestWineCwdSnapshot* output=nullptr) {
    static constexpr char expected[]="c:\\files";
    if (!cpu || !cpu->memory) return false;
    KMemory* memory=cpu->memory;
    GuestWineCwdSnapshot snapshot;
    const U32 teb=snapshot.teb=cpu->seg[FS].address;
    if (!teb || (U64)teb+I386_TEB_PEB_OFFSET+4u>0x100000000ULL ||
            !memory->canRead(teb+I386_TEB_PEB_OFFSET,4u)) return false;
    const U32 peb=snapshot.peb=memory->readd(teb+I386_TEB_PEB_OFFSET);
    if (!peb || (U64)peb+I386_PEB_PROCESS_PARAMETERS_OFFSET+4u>
            0x100000000ULL ||
            !memory->canRead(peb+I386_PEB_PROCESS_PARAMETERS_OFFSET,4u))
        return false;
    const U32 parameters=snapshot.parameters=memory->readd(
        peb+I386_PEB_PROCESS_PARAMETERS_OFFSET);
    const U32 dosPath=snapshot.dosPath=parameters+
        I386_RTL_USER_PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET;
    if (!parameters || dosPath<parameters || (U64)dosPath+8u>0x100000000ULL ||
            !memory->canRead(dosPath,8u)) return false;
    const U32 length=snapshot.length=memory->readw(dosPath+
        I386_UNICODE_STRING_LENGTH_OFFSET);
    const U32 maximumLength=snapshot.maximumLength=memory->readw(dosPath+
        I386_UNICODE_STRING_MAXIMUM_LENGTH_OFFSET);
    const U32 buffer=snapshot.buffer=memory->readd(dosPath+
        I386_UNICODE_STRING_BUFFER_OFFSET);
    constexpr U32 expectedChars=(U32)sizeof(expected)-1u;
    if ((length!=expectedChars*2u && length!=(expectedChars+1u)*2u) ||
            (maximumLength&1u)!=0 || maximumLength<length+2u ||
            maximumLength>520u || !buffer || (buffer&1u)!=0 ||
            (U64)buffer+length+2u>0x100000000ULL ||
            !memory->canRead(buffer,length+2u) ||
            memory->readw(buffer+length)!=0) return false;
    for (U32 i=0;i<expectedChars;++i) {
        U32 value=memory->readw(buffer+i*2u);
        if (value>='A' && value<='Z') value=value-'A'+'a';
        if (value!=(U8)expected[i]) return false;
        snapshot.normalized[i]=(U16)value;
    }
    if (length==(expectedChars+1u)*2u) {
        const U32 slash=memory->readw(buffer+expectedChars*2u);
        if (slash!='\\' && slash!='/') return false;
        snapshot.normalized[expectedChars]='\\';
    }
    snapshot.normalized[length/2u]=0;
    if (output) *output=snapshot;
    return true;
}
#endif

bool suffixEquals(const BString& value,const char* suffix) {
    return value.endsWith(suffix,true);
}

bool exactZipNode(const std::shared_ptr<FsNode>& node,const char* zipSuffix,
        U64 entryOffset,U64 compressedLength,U32 fileBytes,U32 crc32,
        std::shared_ptr<FsZipNode>& zipNode,std::shared_ptr<FsZip>& zip
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
        , U64* stableGeneration = nullptr
#endif
        ) {
    std::shared_ptr<FsFileNode> fileNode=
        std::dynamic_pointer_cast<FsFileNode>(node);
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    BString nativePathSnapshot;
    if (!fileNode) return false;
    zipNode=fileNode->getAzrokFaceWisZipNode(&nativePathSnapshot);
    if (Fs::doesNativePathExist(nativePathSnapshot)) return false;
#else
    if (!fileNode || Fs::doesNativePathExist(fileNode->nativePath) ||
            fileNode->length()!=fileBytes) return false;
    zipNode=fileNode->getAzrokFaceWisZipNode();
#endif
    if (!zipNode || !zipNode->fsZip) return false;
    zip=zipNode->fsZip;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    const U64 generationBefore=zip->getAzrokFaceWisGeneration();
#endif
    const bool exact=suffixEquals(zip->zipPath,zipSuffix) &&
        zip->getAzrokFaceWisGeneration()!=0 &&
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
        zipNode->length()==fileBytes &&
#endif
        zipNode->getAzrokFaceWisEntryOffset()==entryOffset &&
        zipNode->getAzrokFaceWisCompressedLength()==compressedLength &&
        zipNode->getAzrokFaceWisCompressionMethod()==
            WASM_JIT_AZROK_FACE_WIS_COMPRESSION &&
        zipNode->getAzrokFaceWisCrc32()==crc32;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    const U64 generationAfter=zip->getAzrokFaceWisGeneration();
    if (!exact || !generationBefore || generationBefore!=generationAfter)
        return false;
    if (stableGeneration) *stableGeneration=generationBefore;
#endif
    return exact;
}

enum EntryPhase : U32 {
    EntryEmpty=0,
    EntryWarming=1,
    EntryAdmitted=2,
    EntryDisabled=3,
};

struct CacheEntry {
    std::atomic<U32> phase{EntryEmpty};
    std::atomic<U32> hitsUntilProbe{TAIL_PROBE_HITS};
    U8* slab=nullptr;
    U32 guestBase=0;
    U32 guestDescriptor=0;
    U32 pathHash=0;
    U64 captures=0;
    U64 tailComparisons=0;
    U64 tailChanges=0;
    U8 tailHash[32]={};
    std::shared_ptr<FsNode> node;
    std::shared_ptr<FsZipNode> zipNode;
    U64 zipGeneration=0;
};

struct ProcessState {
    KProcess* process=nullptr;
    KMemory* memory=nullptr;
    std::atomic<bool> enabled{false};
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    // The state is CAS-installed before the first expensive identity hash.
    // Exactly one helper may transition Validating at a time.
    std::atomic<U32> retryStatus{0};
    std::atomic<bool> retryMaterialized{false};
    std::atomic<U64> retryHelperCount{0};
    std::atomic<U64> retryLastAttemptHelper{0};
    std::atomic<U32> retryBackoffShift{0};
    std::atomic<U64> retryObservedProcessEpoch{0};
    std::atomic<U64> retryObservedZipGeneration{0};
#endif
    U64 processEpoch=0;
    U64 globalZipGeneration=0;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    U64 rootZipGeneration=0;
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
    // High dword is an arm generation, low dword is remaining boundaries.
    // One CAS protects new arms from stale consumers of the prior generation.
    std::atomic<U64> warmupState{0};
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    U64 cwdWriterBaselineEpoch=0;
#endif
    U32 physicalBytes=0;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    std::atomic<bool> appBound{false};
#else
    bool appBound=false;
#endif
    std::shared_ptr<FsNode> rootNode;
    std::shared_ptr<FsZip> rootZip;
    std::shared_ptr<FsZip> appZip;
    U64 appZipGeneration=0;
    std::mutex captureMutex;
    std::array<CacheEntry,WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT> entries;
};

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
enum RetryStatus : U32 {
    RetryUnvalidated = 0,
    RetryValidating,
    RetryEnabled,
    RetryTransientBackoff,
    RetryPermanent,
    RetryInvalidated,
};
enum class IdentityValidationResult : U32 {
    Valid = 0,
    TransientFailure,
    PermanentFailure,
};
constexpr U32 PROCESS_IDENTITY_RETRY_MAX_SHIFT = 4u;

void disableRetryState(ProcessState& state, bool permanent) {
    const U32 status=state.retryMaterialized.load(std::memory_order_acquire) ?
        RetryInvalidated : (permanent ? RetryPermanent : RetryTransientBackoff);
    // Publish status before enabled=false. A helper that observes the latter
    // cannot CAS an old RetryEnabled value back into Validating.
    state.retryStatus.store(status,std::memory_order_release);
    state.enabled.store(false,std::memory_order_release);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
void armStabilityWarmup(ProcessState& state) {
    const U64 generation=warmupNextGeneration.fetch_add(1,
        std::memory_order_relaxed);
    const U64 packed=(generation<<32)|
        PROCESS_IDENTITY_STABILITY_WARMUP_BOUNDARIES;
    state.warmupState.store(packed,std::memory_order_release);
    warmupGaugeState.store(packed,std::memory_order_relaxed);
    countWarmup(WasmJitAzrokFaceWisWarmupCounter::Arms);
}

void resetStabilityWarmup(ProcessState& state, bool processMismatch,
        bool rootMismatch, bool appMismatch) {
    const U64 generation=warmupNextGeneration.fetch_add(1,
        std::memory_order_relaxed);
    const U64 packed=generation<<32;
    state.warmupState.store(packed,std::memory_order_release);
    warmupGaugeState.store(packed,std::memory_order_relaxed);
    countWarmup(WasmJitAzrokFaceWisWarmupCounter::Resets);
    if (processMismatch)
        countWarmup(WasmJitAzrokFaceWisWarmupCounter::ProcessEpochResets);
    if (rootMismatch)
        countWarmup(WasmJitAzrokFaceWisWarmupCounter::RootGenerationResets);
    if (appMismatch)
        countWarmup(WasmJitAzrokFaceWisWarmupCounter::AppGenerationResets);
}

bool consumeStabilityWarmupBoundary(ProcessState& state) {
    U64 before=state.warmupState.load(std::memory_order_acquire);
    while ((U32)before) {
        const U64 after=(before&0xffffffff00000000ULL)|((U32)before-1u);
        if (state.warmupState.compare_exchange_weak(before,after,
                std::memory_order_acq_rel,std::memory_order_acquire)) {
            lowerWarmupGauge(after);
            countWarmup(WasmJitAzrokFaceWisWarmupCounter::BoundarySkips);
            if ((U32)before==1)
                countWarmup(WasmJitAzrokFaceWisWarmupCounter::Completions);
            return true;
        }
    }
    return false;
}
#endif

struct EntrySnapshot {
    U32 abiVersion;
    U32 index;
    U32 phase;
    U32 pathHash;
    U64 captures;
    U64 tailComparisons;
    U64 tailChanges;
    U8 tailHash[32];
};
static_assert(sizeof(EntrySnapshot)==72u);

std::mutex diagnosticMutex;
std::array<EntrySnapshot,WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT>
    latestEntrySnapshots = {};

void publishEntrySnapshot(U32 index,const CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(diagnosticMutex);
    EntrySnapshot& snapshot=latestEntrySnapshots[index];
    snapshot.abiVersion=1;
    snapshot.index=index;
    snapshot.phase=entry.phase.load(std::memory_order_relaxed);
    snapshot.pathHash=entry.pathHash;
    snapshot.captures=entry.captures;
    snapshot.tailComparisons=entry.tailComparisons;
    snapshot.tailChanges=entry.tailChanges;
    std::memcpy(snapshot.tailHash,entry.tailHash,sizeof(snapshot.tailHash));
}

struct CanonicalPath {
    char raw[PATH_LIMIT]={};
    char canonical[PATH_LIMIT]={};
    U32 rawLen=0;
    U32 canonicalLen=0;
    U32 hash=0;
};

bool rawPathPrefixEquals(const char* raw,U32 len,const char* prefix) {
    const U32 prefixLen=(U32)std::strlen(prefix);
    if (!raw || len<prefixLen) return false;
    for (U32 i=0;i<prefixLen;++i) {
        char value=raw[i];
        if (value=='\\') value='/';
        if (value>='A' && value<='Z') value=(char)(value-'A'+'a');
        if (value!=prefix[i]) return false;
    }
    return true;
}

bool readCanonicalPath(KMemory* memory,U32 address,CanonicalPath& result) {
    if (!memory || !address || (U64)address+PATH_LIMIT>0x100000000ULL)
        return false;
    const U32 firstBytes=std::min<U32>(PATH_LIMIT,K_PAGE_SIZE-(address&K_PAGE_MASK));
    if (!memory->canRead(address,firstBytes)) return false;
    U8* first=memory->getRamPtr(address,firstBytes,false);
    if (!first) return false;
    U8* second=nullptr;
    U32 len=0;
    for (;len<PATH_LIMIT;++len) {
        if (len==firstBytes) {
            const U32 remaining=PATH_LIMIT-firstBytes;
            if (!remaining || !memory->canRead(address+firstBytes,remaining))
                return false;
            second=memory->getRamPtr(address+firstBytes,remaining,false);
            if (!second) return false;
        }
        const U8 value=len<firstBytes ? first[len] : second[len-firstBytes];
        if (!value) break;
        if (value<0x20 || value>0x7e) return false;
        result.raw[len]=(char)value;
    }
    if (!len || len==PATH_LIMIT) return false;
    result.rawLen=len;

    U32 inputStart=0;
    if (rawPathPrefixEquals(result.raw,len,APP_GUEST_BASE)) {
        inputStart=(U32)std::strlen(APP_GUEST_BASE);
    } else if (rawPathPrefixEquals(result.raw,len,APP_DOS_BASE)) {
        inputStart=(U32)std::strlen(APP_DOS_BASE);
    } else {
        for (U32 i=0;i<len;++i) if (result.raw[i]==':') return false;
    }

    U32 out=0;
    bool previousSlash=false;
    for (U32 i=inputStart;i<len;++i) {
        char value=result.raw[i];
        if (value=='\\') value='/';
        if (value>='A' && value<='Z') value=(char)(value-'A'+'a');
        if (value==':') return false;
        if (value=='/') {
            if (!out || previousSlash) continue;
            previousSlash=true;
        } else {
            previousSlash=false;
        }
        result.canonical[out++]=value;
    }
    while (out && result.canonical[out-1]=='/') --out;
    U32 start=0;
    while (start+1<out && result.canonical[start]=='.' &&
            result.canonical[start+1]=='/') start+=2;
    if (start) {
        std::memmove(result.canonical,result.canonical+start,out-start);
        out-=start;
    }
    if (!out) return false;
    for (U32 i=0;i<out;) {
        U32 end=i;
        while (end<out && result.canonical[end]!='/') ++end;
        if ((end-i==1 && result.canonical[i]=='.') ||
                (end-i==2 && result.canonical[i]=='.' &&
                 result.canonical[i+1]=='.')) return false;
        i=end+1;
    }
    U32 hash=2166136261u;
    for (U32 i=0;i<out;++i) {
        hash^=(U8)result.canonical[i];
        hash*=16777619u;
    }
    result.canonical[out]=0;
    result.raw[len]=0;
    result.canonicalLen=out;
    result.hash=hash;
    return true;
}

S32 manifestIndexForPath(const CanonicalPath& path) {
    U32 low=0,high=WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT;
    while (low<high) {
        const U32 mid=low+(high-low)/2;
        const U32 value=WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[mid].fnv1a;
        if (value<path.hash) low=mid+1;
        else high=mid;
    }
    if (low>=WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT ||
            WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[low].fnv1a!=path.hash)
        return -1;
    const U32 index=WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[low].manifestIndex;
    const char* expected=WASM_JIT_AZROK_FACE_WIS_MANIFEST[index].canonicalPath;
    return std::strlen(expected)==path.canonicalLen &&
        std::memcmp(expected,path.canonical,path.canonicalLen)==0 ? (S32)index : -1;
}

bool validateProcessIdentity(ProcessState& state
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        , IdentityValidationResult* validationResult = nullptr
#endif
        ) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (validationResult) *validationResult=IdentityValidationResult::TransientFailure;
#endif
    KProcess* process=state.process;
    KMemory* memory=state.memory;
    if (!process || !memory || process->memory!=memory) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        if (validationResult) *validationResult=IdentityValidationResult::PermanentFailure;
#endif
        return false;
    }
    const U64 epoch=process->getAzrokFaceWisHleEpoch();
    const U64 zipGeneration=FsZip::getAzrokFaceWisGlobalGeneration();

    MappedFilePtr lookup=process->getMappedFileForRange(LOOKUP_EIP,
        sizeof(LOOKUP_CODE));
    MappedFilePtr capture=process->getMappedFileForRange(CAPTURE_EIP,
        sizeof(CAPTURE_CODE));
    // Mappings can legitimately be absent before the new process reaches its
    // executable image. A present-but-wrong image/code is permanent for this
    // KProcess and must not be rehashed on unrelated ZIP churn.
    if (!lookup || !capture) return false;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (lookup->key!=capture->key || lookup->file!=capture->file || !lookup->file ||
            !lookup->file->openFile || !lookup->file->openFile->node ||
            !lookup->file->openFile->node->path.endsWith("Azrok.exe",true) ||
            !codeMatches(memory,LOOKUP_EIP,LOOKUP_CODE,sizeof(LOOKUP_CODE)) ||
            !codeMatches(memory,CAPTURE_EIP,CAPTURE_CODE,sizeof(CAPTURE_CODE))) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        if (validationResult) *validationResult=IdentityValidationResult::PermanentFailure;
#endif
        count(WasmJitAzrokFaceWisCounter::FallbackCodeIdentity);
        return false;
    }
    bool exeReadFailure=false;
    if (!hashMappedFile(lookup,EXE_FILE_BYTES,EXE_SHA256,&exeReadFailure)) {
        if (validationResult) *validationResult=exeReadFailure ?
            IdentityValidationResult::TransientFailure :
            IdentityValidationResult::PermanentFailure;
        count(WasmJitAzrokFaceWisCounter::FallbackCodeIdentity);
        return false;
    }
#else
    // Preserve the accepted r5 validation order byte-for-byte when retry is
    // not compiled.
    if (lookup->key!=capture->key || lookup->file!=capture->file || !lookup->file ||
            !lookup->file->openFile || !lookup->file->openFile->node ||
            !lookup->file->openFile->node->path.endsWith("Azrok.exe",true) ||
            !hashMappedFile(lookup,EXE_FILE_BYTES,EXE_SHA256) ||
            !codeMatches(memory,LOOKUP_EIP,LOOKUP_CODE,sizeof(LOOKUP_CODE)) ||
            !codeMatches(memory,CAPTURE_EIP,CAPTURE_CODE,sizeof(CAPTURE_CODE))) {
        count(WasmJitAzrokFaceWisCounter::FallbackCodeIdentity);
        return false;
    }
#endif

    std::shared_ptr<FsNode> rootNode=
        Fs::getNodeFromLocalPath(BString::empty,B("/lib/libc.so.6"),true);
    std::shared_ptr<FsZipNode> rootZipNode;
    std::shared_ptr<FsZip> rootZip;
    if (!rootNode) {
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    std::shared_ptr<FsFileNode> rootFileNode=
        std::dynamic_pointer_cast<FsFileNode>(rootNode);
    if (!rootFileNode) {
        if (validationResult) *validationResult=
            IdentityValidationResult::PermanentFailure;
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    BString rootNativePathSnapshot;
    std::shared_ptr<FsZipNode> provisionalRootZip=
        rootFileNode->getAzrokFaceWisZipNode(&rootNativePathSnapshot);
#else
    std::shared_ptr<FsZipNode> provisionalRootZip=
        rootFileNode->getAzrokFaceWisZipNode();
#endif
    if (!provisionalRootZip || !provisionalRootZip->fsZip ||
            provisionalRootZip->fsZip->getAzrokFaceWisGeneration()==0) {
        // Mount publication can lag the first helper in a new KProcess.
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    if (Fs::doesNativePathExist(rootNativePathSnapshot) ||
            provisionalRootZip->length()!=ROOT_LIBC_FILE_BYTES) {
        if (validationResult) *validationResult=
            IdentityValidationResult::PermanentFailure;
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#else
    if (Fs::doesNativePathExist(rootFileNode->nativePath) ||
            rootFileNode->length()!=ROOT_LIBC_FILE_BYTES) {
        if (validationResult) *validationResult=
            IdentityValidationResult::PermanentFailure;
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#endif
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    U64 rootGeneration=0;
#endif
    if (!exactZipNode(rootNode,ROOT_ZIP_SUFFIX,ROOT_LIBC_CENTRAL_OFFSET,
            ROOT_LIBC_COMPRESSED_LENGTH,ROOT_LIBC_FILE_BYTES,ROOT_LIBC_CRC32,
            rootZipNode,rootZip
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
            ,&rootGeneration
#endif
            )) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        if (validationResult) *validationResult=IdentityValidationResult::PermanentFailure;
#endif
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    bool rootReadFailure=false;
    if (!hashNode(rootNode,ROOT_LIBC_FILE_BYTES,ROOT_LIBC_SHA256,
            &rootReadFailure)) {
        if (validationResult) *validationResult=rootReadFailure ?
            IdentityValidationResult::TransientFailure :
            IdentityValidationResult::PermanentFailure;
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#else
    if (!hashNode(rootNode,ROOT_LIBC_FILE_BYTES,ROOT_LIBC_SHA256)) {
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    MappedFilePtr cwdWriter=process->findAzrokFaceWisMappedModule(
        CWD_WRITER_MODULE_SUFFIX,CWD_WRITER_MODULE_BYTES,CWD_WRITER_ENTRY_RVA,
        (CWD_WRITER_EXIT_RVA-CWD_WRITER_ENTRY_RVA)+
            (U32)sizeof(CWD_WRITER_EXIT_BYTES));
    bool cwdWriterReadFailure=false;
    if (!cwdWriter) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_CENSUS_V1
        cwdDiscoveryCounters[(U32)WasmJitAzrokFaceWisCwdDiscoveryCounter::FindNull].fetch_add(1,std::memory_order_relaxed);
#endif
        wasmJitAzrokFaceWisCwdDiscoveryNoteHook(false);
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
    if (!hashMappedFile(cwdWriter,CWD_WRITER_MODULE_BYTES,
            CWD_WRITER_MODULE_SHA256,&cwdWriterReadFailure)) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_CENSUS_V1
        cwdDiscoveryCounters[(U32)(cwdWriterReadFailure ?
            WasmJitAzrokFaceWisCwdDiscoveryCounter::HashReadFailure :
            WasmJitAzrokFaceWisCwdDiscoveryCounter::HashMismatch)].fetch_add(1,std::memory_order_relaxed);
#endif
        wasmJitAzrokFaceWisCwdDiscoveryNoteHook(false);
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
    const U32 cwdWriterEntry=cwdWriter->address+
        (CWD_WRITER_ENTRY_RVA-cwdWriter->offset);
    const U32 cwdWriterExit=cwdWriter->address+
        (CWD_WRITER_EXIT_RVA-cwdWriter->offset);
    if (!codeMatches(memory,cwdWriterEntry,CWD_WRITER_ENTRY_BYTES,
            sizeof(CWD_WRITER_ENTRY_BYTES)) ||
            !codeMatches(memory,cwdWriterExit,CWD_WRITER_EXIT_BYTES,
                sizeof(CWD_WRITER_EXIT_BYTES))) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_CENSUS_V1
        cwdDiscoveryCounters[(U32)WasmJitAzrokFaceWisCwdDiscoveryCounter::CodeMismatch].fetch_add(1,std::memory_order_relaxed);
#endif
        wasmJitAzrokFaceWisCwdDiscoveryNoteHook(false);
        count(WasmJitAzrokFaceWisCounter::FallbackCodeIdentity);
        return false;
    }
    if (!process->republishAzrokFaceWisCwdWriterHook(
            cwdWriter,cwdWriterEntry,cwdWriterExit) ||
            !codeMatches(memory,cwdWriterEntry,CWD_WRITER_ENTRY_BYTES,
                sizeof(CWD_WRITER_ENTRY_BYTES)) ||
            !codeMatches(memory,cwdWriterExit,CWD_WRITER_EXIT_BYTES,
                sizeof(CWD_WRITER_EXIT_BYTES)) ||
            !process->confirmAzrokFaceWisCwdWriterHook(
                cwdWriterEntry,cwdWriterExit)) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_CENSUS_V1
        cwdDiscoveryCounters[(U32)WasmJitAzrokFaceWisCwdDiscoveryCounter::ConfirmFailure].fetch_add(1,std::memory_order_relaxed);
#endif
        wasmJitAzrokFaceWisCwdDiscoveryNoteHook(false);
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
    wasmJitAzrokFaceWisCwdDiscoveryNoteHook(true);
    U64 cwdWriterEpoch=0;
    GuestWineCwdSnapshot cwdSnapshot1,cwdSnapshot2;
    KThread* validationThread=KThread::currentThread();
    CPU* validationCpu=validationThread ? validationThread->cpu : nullptr;
    if (!process->getAzrokFaceWisCwdWriterToken(cwdWriterEpoch) ||
            !guestWineDosCwdIsFixedAppBase(validationCpu,&cwdSnapshot1) ||
            !guestWineDosCwdIsFixedAppBase(validationCpu,&cwdSnapshot2) ||
            !sameGuestWineCwdSnapshot(cwdSnapshot1,cwdSnapshot2) ||
            !process->azrokFaceWisCwdWriterTokenMatches(cwdWriterEpoch)) {
        wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback();
        count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
        return false;
    }
    state.cwdWriterBaselineEpoch=cwdWriterEpoch;
#endif
    if (epoch!=process->getAzrokFaceWisHleEpoch() ||
            zipGeneration!=FsZip::getAzrokFaceWisGlobalGeneration()
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
            || !rootGeneration ||
            rootGeneration!=rootZip->getAzrokFaceWisGeneration()
#endif
            ) {
        count(WasmJitAzrokFaceWisCounter::FallbackEpoch);
        return false;
    }
    state.processEpoch=epoch;
    state.globalZipGeneration=zipGeneration;
    state.rootNode=rootNode;
    state.rootZip=rootZip;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    state.rootZipGeneration=rootGeneration;
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (validationResult) *validationResult=IdentityValidationResult::Valid;
#endif
    return true;
}

ProcessState* getOrCreateState(CPU* cpu) {
    if (!cpu || !cpu->thread || !cpu->thread->process || !cpu->memory)
        return nullptr;
    KProcess* process=cpu->thread->process.get();
    ProcessState* current=(ProcessState*)process->getAzrokFaceWisHleState();
    if (current) return current;
    std::unique_ptr<ProcessState> candidate(new(std::nothrow) ProcessState());
    if (!candidate) return nullptr;
    candidate->process=process;
    candidate->memory=cpu->memory;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    candidate->retryObservedProcessEpoch.store(
        process->getAzrokFaceWisHleEpoch(),std::memory_order_relaxed);
    candidate->retryObservedZipGeneration.store(
        FsZip::getAzrokFaceWisGlobalGeneration(),std::memory_order_relaxed);
    // Install-before-validate prevents simultaneous first-use KThreads from
    // hashing Azrok.exe + libc independently.
    void* expected=nullptr;
    if (process->installAzrokFaceWisHleState(expected,candidate.get()))
        return candidate.release();
    return (ProcessState*)expected;
#else
    const bool valid=validateProcessIdentity(*candidate);
    candidate->enabled.store(valid,std::memory_order_release);
    void* expected=nullptr;
    if (process->installAzrokFaceWisHleState(expected,candidate.get()))
        return candidate.release();
    return (ProcessState*)expected;
#endif
}

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
bool retryDisabledProcessIdentity(ProcessState* state) {
    if (!state || state->enabled.load(std::memory_order_acquire)) return true;
    countRetry(WasmJitAzrokFaceWisRetryCounter::DisabledHelperEntries);
    U32 status=state->retryStatus.load(std::memory_order_acquire);
    if (state->retryMaterialized.load(std::memory_order_acquire)) {
        state->retryStatus.store(RetryInvalidated,std::memory_order_release);
        state->enabled.store(false,std::memory_order_release);
        countRetry(WasmJitAzrokFaceWisRetryCounter::NotDue);
        return false;
    }
    if (status==RetryPermanent || status==RetryInvalidated) {
        countRetry(WasmJitAzrokFaceWisRetryCounter::NotDue);
        return false;
    }
    if (status==RetryValidating) {
        countRetry(WasmJitAzrokFaceWisRetryCounter::BusySkips);
        return false;
    }
    if (status==RetryEnabled) {
        // Another validator has published status and is about to publish the
        // legacy enabled bit. Never CAS Enabled back into Validating.
        countRetry(WasmJitAzrokFaceWisRetryCounter::BusySkips);
        return state->enabled.load(std::memory_order_acquire);
    }
    const U64 helperCount=state->retryHelperCount.fetch_add(1,
        std::memory_order_relaxed)+1;
    const U64 processEpoch=state->process ?
        state->process->getAzrokFaceWisHleEpoch() : 0;
    const U64 zipGeneration=FsZip::getAzrokFaceWisGlobalGeneration();
    const bool epochChanged=processEpoch!=state->retryObservedProcessEpoch.load(
        std::memory_order_acquire);
    const bool zipChanged=zipGeneration!=state->retryObservedZipGeneration.load(
        std::memory_order_acquire);
    const U32 shift=std::min<U32>(state->retryBackoffShift.load(
        std::memory_order_acquire),PROCESS_IDENTITY_RETRY_MAX_SHIFT);
    const U64 interval=(U64)PROCESS_IDENTITY_RETRY_INTERVAL<<shift;
    const bool boundedDue=status==RetryTransientBackoff &&
        helperCount-state->retryLastAttemptHelper.load(std::memory_order_acquire)>=interval;
    const bool initialDue=status==RetryUnvalidated;
    if (!initialDue && !epochChanged && !zipChanged && !boundedDue) {
        countRetry(WasmJitAzrokFaceWisRetryCounter::NotDue);
        return false;
    }
    U32 expected=status;
    if (!state->retryStatus.compare_exchange_strong(expected,RetryValidating,
            std::memory_order_acq_rel,std::memory_order_acquire)) {
        countRetry(WasmJitAzrokFaceWisRetryCounter::BusySkips);
        return false;
    }
    if (state->retryMaterialized.load(std::memory_order_acquire)) {
        state->retryStatus.store(RetryInvalidated,std::memory_order_release);
        state->enabled.store(false,std::memory_order_release);
        countRetry(WasmJitAzrokFaceWisRetryCounter::NotDue);
        return false;
    }
    // The winning thread rechecks after acquiring the gate. No other helper
    // hashes the root/EXE while validation is active.
    bool result=state->enabled.load(std::memory_order_acquire);
    if (!result) {
        const U64 currentEpoch=state->process ?
            state->process->getAzrokFaceWisHleEpoch() : 0;
        const U64 currentZip=FsZip::getAzrokFaceWisGlobalGeneration();
        const bool currentEpochChanged=currentEpoch!=
            state->retryObservedProcessEpoch.load(std::memory_order_relaxed);
        const bool currentZipChanged=currentZip!=
            state->retryObservedZipGeneration.load(std::memory_order_relaxed);
        const bool currentBoundedDue=boundedDue;
        if (initialDue || currentEpochChanged || currentZipChanged || currentBoundedDue) {
            if (currentEpochChanged)
                countRetry(WasmJitAzrokFaceWisRetryCounter::ProcessEpochTriggers);
            if (currentZipChanged)
                countRetry(WasmJitAzrokFaceWisRetryCounter::ZipGenerationTriggers);
            if (currentBoundedDue)
                countRetry(WasmJitAzrokFaceWisRetryCounter::BoundedTriggers);
            state->retryObservedProcessEpoch.store(currentEpoch,
                std::memory_order_release);
            state->retryObservedZipGeneration.store(currentZip,
                std::memory_order_release);
            state->retryLastAttemptHelper.store(helperCount,
                std::memory_order_release);
            if (currentEpochChanged || currentZipChanged)
                state->retryBackoffShift.store(0,std::memory_order_relaxed);
            countRetry(WasmJitAzrokFaceWisRetryCounter::Attempts);
            IdentityValidationResult validation=
                IdentityValidationResult::TransientFailure;
            result=validateProcessIdentity(*state,&validation);
            if (result) {
                // All identity fields written by validation become visible to
                // lookup/capture through this release/acquire transition.
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
                armStabilityWarmup(*state);
#endif
                state->retryStatus.store(RetryEnabled,std::memory_order_release);
                state->enabled.store(true,std::memory_order_release);
                countRetry(WasmJitAzrokFaceWisRetryCounter::Successes);
            } else if (validation==IdentityValidationResult::PermanentFailure) {
                state->retryStatus.store(RetryPermanent,std::memory_order_release);
                countRetry(WasmJitAzrokFaceWisRetryCounter::PermanentFailures);
            } else {
                state->retryBackoffShift.store(std::min<U32>(
                    state->retryBackoffShift.load(std::memory_order_relaxed)+1u,
                    PROCESS_IDENTITY_RETRY_MAX_SHIFT),std::memory_order_relaxed);
                state->retryStatus.store(RetryTransientBackoff,
                    std::memory_order_release);
                countRetry(WasmJitAzrokFaceWisRetryCounter::TransientFailures);
            }
        } else {
            state->retryStatus.store(status,std::memory_order_release);
            countRetry(WasmJitAzrokFaceWisRetryCounter::NotDue);
        }
    }
    return result;
}
#endif

bool epochsMatch(ProcessState* state) {
    if (!state || !state->enabled.load(std::memory_order_acquire)) return false;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    if (state->retryStatus.load(std::memory_order_acquire)!=RetryEnabled) {
        state->enabled.store(false,std::memory_order_release);
        return false;
    }
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    bool mismatch=false;
    bool processMismatch=false;
    bool rootMismatch=false;
    bool appMismatch=false;
    if (state->process->getAzrokFaceWisHleEpoch()!=state->processEpoch) {
        countScopedGeneration(
            WasmJitAzrokFaceWisScopedGenerationCounter::ProcessEpochMismatches);
        mismatch=true;
        processMismatch=true;
    }
    if (!state->rootZip || !state->rootZipGeneration ||
            state->rootZip->getAzrokFaceWisGeneration()!=
                state->rootZipGeneration) {
        countScopedGeneration(
            WasmJitAzrokFaceWisScopedGenerationCounter::RootGenerationMismatches);
        mismatch=true;
        rootMismatch=true;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    const bool appBound=state->appBound.load(std::memory_order_acquire);
#else
    const bool appBound=state->appBound;
#endif
    if (appBound && (!state->appZip || !state->appZipGeneration ||
            state->appZip->getAzrokFaceWisGeneration()!=
                state->appZipGeneration)) {
        countScopedGeneration(
            WasmJitAzrokFaceWisScopedGenerationCounter::AppGenerationMismatches);
        mismatch=true;
        appMismatch=true;
    }
    if (mismatch) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
        resetStabilityWarmup(*state,processMismatch,rootMismatch,appMismatch);
#endif
#else
    if (state->process->getAzrokFaceWisHleEpoch()!=state->processEpoch ||
            FsZip::getAzrokFaceWisGlobalGeneration()!=state->globalZipGeneration) {
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        disableRetryState(*state,false);
#else
        state->enabled.store(false,std::memory_order_release);
#endif
        count(WasmJitAzrokFaceWisCounter::FallbackEpoch);
        return false;
    }
    return true;
}

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
bool cwdWriterTokenMatches(ProcessState* state) {
    return state && state->process &&
        state->process->azrokFaceWisCwdWriterTokenMatches(
            state->cwdWriterBaselineEpoch);
}
#endif

bool resolveAssetIdentity(ProcessState& state,const CanonicalPath& path,
        U32 index,std::shared_ptr<FsNode>& node,
        std::shared_ptr<FsZipNode>& zipNode) {
    const WasmJitAzrokFaceWisManifestEntry& manifest=
        WASM_JIT_AZROK_FACE_WIS_MANIFEST[index];
    // Fs::splitPath only treats '/' as a separator.  Keep the guest's exact
    // case for the case-sensitive ZIP tree, but translate DOS separators
    // before resolving the archive-backed node.
    BString raw=BString::copy(path.raw,path.rawLen).replace('\\','/');
    while (raw.startsWith("./")) raw=raw.substr(2);
    BString fullPath;
    if (raw.startsWith(APP_GUEST_BASE,true)) {
        fullPath=raw;
    } else if (raw.startsWith(APP_DOS_BASE,true)) {
        fullPath=BString::copy(APP_GUEST_BASE)+
            raw.substr((U32)std::strlen(APP_DOS_BASE));
    } else {
        // Unknown absolute paths cannot name a manifest-relative FACE asset.
        if (raw.startsWith('/')) return false;
        fullPath=BString::copy(APP_GUEST_BASE)+raw;
    }
    node=Fs::getNodeFromLocalPath(BString::empty,fullPath,true);
    std::shared_ptr<FsZip> zip;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    U64 appGeneration=0;
#endif
    if (!exactZipNode(node,APP_ZIP_SUFFIX,manifest.centralDirectoryOffset,
            manifest.compressedLength,WASM_JIT_AZROK_FACE_WIS_FILE_BYTES,
            manifest.crc32,zipNode,zip
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
            ,&appGeneration
#endif
            )) return false;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
    const bool appBound=state.appBound.load(std::memory_order_acquire);
#else
    const bool appBound=state.appBound;
#endif
    if (!appBound) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        state.retryMaterialized.store(true,std::memory_order_release);
#endif
        state.appZip=zip;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
        state.appZipGeneration=appGeneration;
#else
        state.appZipGeneration=zip->getAzrokFaceWisGeneration();
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
        state.appBound.store(true,std::memory_order_release);
#else
        state.appBound=true;
#endif
    }
    return state.appZip==zip &&
        state.appZipGeneration==zip->getAzrokFaceWisGeneration();
}

bool descriptorDigests(KMemory* memory,U32 descriptor,
        const WasmJitAzrokFaceWisManifestEntry& manifest,
        U32& payload,U8 contentHash[32],U8 tailHash[32]) {
    if (!descriptor || !memory->canRead(descriptor,0x28) ||
            memory->readd(descriptor)!=DESCRIPTOR_FIELD0_EXPECTED ||
            memory->readd(descriptor+4)!=DESCRIPTOR_FIELD4_EXPECTED)
        return false;
    payload=memory->readd(descriptor+DESCRIPTOR_PAYLOAD_OFFSET);
    if (!payload || !memory->canRead(payload,WASM_JIT_AZROK_FACE_WIS_COPY_BYTES))
        return false;
    Sha256 content;
    content.update(WASM_JIT_AZROK_FACE_WIS_HEADER,
        WASM_JIT_AZROK_FACE_WIS_HEADER_BYTES);
    if (!memoryDigest(memory,payload,WASM_JIT_AZROK_FACE_WIS_PAYLOAD_BYTES,
            content)) return false;
    content.finish(contentHash);
    if (!digestMatches(contentHash,manifest.sha256)) return false;
    Sha256 tail;
    if (!memoryDigest(memory,payload+WASM_JIT_AZROK_FACE_WIS_PAYLOAD_BYTES,
            WASM_JIT_AZROK_FACE_WIS_TAIL_BYTES,tail)) return false;
    tail.finish(tailHash);
    return true;
}

bool allocateSnapshot(ProcessState& state,CacheEntry& entry,U32 payload,
        U32 descriptorField0,U32 descriptorField4) {
    if (state.physicalBytes+SYNTHETIC_SLAB_BYTES>CACHE_PHYSICAL_CAP) {
        count(WasmJitAzrokFaceWisCounter::FallbackBudget);
        return false;
    }
    void* raw=std::aligned_alloc(K_PAGE_SIZE,SYNTHETIC_SLAB_BYTES);
    if (!raw) return false;
    U8* slab=(U8*)raw;
    std::memset(slab,0,SYNTHETIC_SLAB_BYTES);
    state.memory->memcpy(slab+SYNTHETIC_PAYLOAD_OFFSET,payload,
        WASM_JIT_AZROK_FACE_WIS_COPY_BYTES);
    const U32 guestBase=state.memory->mapNativeMemory(slab,SYNTHETIC_SLAB_BYTES);
    if (!guestBase) {
        std::free(slab);
        return false;
    }
    // Preserve the loader's non-obvious layout exactly: +0=height(255),
    // +4=width(256).  The file header itself stores width before height.
    *(U32*)(slab+0)=descriptorField0;
    *(U32*)(slab+4)=descriptorField4;
    *(U32*)(slab+DESCRIPTOR_PAYLOAD_OFFSET)=guestBase+SYNTHETIC_PAYLOAD_OFFSET;
    entry.slab=slab;
    entry.guestBase=guestBase;
    entry.guestDescriptor=guestBase;
    state.physicalBytes+=SYNTHETIC_SLAB_BYTES;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    state.retryMaterialized.store(true,std::memory_order_release);
#endif
    count(WasmJitAzrokFaceWisCounter::SlabsAllocated);
    count(WasmJitAzrokFaceWisCounter::SlabBytesAllocated,
        SYNTHETIC_SLAB_BYTES);
    return true;
}

void lookup(CPU* cpu,ProcessState* state) {
    count(WasmJitAzrokFaceWisCounter::LookupEntries);
    cpu->tmpReg=0;
    if (!epochsMatch(state)) {
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
    const U32 ebp=cpu->reg[regBP].u32;
    if (ebp<0x114u) {
        count(WasmJitAzrokFaceWisCounter::FallbackPath);
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
    CanonicalPath path;
    if (!readCanonicalPath(cpu->memory,ebp-0x114u,path)) {
        count(WasmJitAzrokFaceWisCounter::FallbackPath);
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
    const S32 index=manifestIndexForPath(path);
    if (index<0) {
        count(WasmJitAzrokFaceWisCounter::FallbackManifest);
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
    CacheEntry& entry=state->entries[(U32)index];
    if (entry.phase.load(std::memory_order_acquire)!=EntryAdmitted ||
            !entry.guestDescriptor) {
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    U32 before=entry.hitsUntilProbe.load(std::memory_order_relaxed);
    bool probeMiss=false;
    for (;;) {
        const U32 after=before<=1 ? TAIL_PROBE_HITS : before-1;
        if (entry.hitsUntilProbe.compare_exchange_weak(before,after,
                std::memory_order_relaxed,std::memory_order_relaxed)) {
            probeMiss=before<=1;
            break;
        }
    }
    if (probeMiss) {
#else
    const U32 before=entry.hitsUntilProbe.fetch_sub(1,
        std::memory_order_relaxed);
    if (before<=1) {
        entry.hitsUntilProbe.store(TAIL_PROBE_HITS,std::memory_order_relaxed);
#endif
        count(WasmJitAzrokFaceWisCounter::LookupProbeMisses);
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
    if (!(cpu->memory->getPageFlags((ebp-4)>>K_PAGE_SHIFT)&PAGE_WRITE)) {
        count(WasmJitAzrokFaceWisCounter::FallbackDescriptor);
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    if (!cwdWriterTokenMatches(state)) {
        wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback();
        count(WasmJitAzrokFaceWisCounter::LookupMisses);
        return;
    }
#endif
    cpu->memory->writed(ebp-4,0);
    cpu->reg[regAX].u32=entry.guestDescriptor;
    cpu->eip.u32=CAPTURE_EIP;
    cpu->tmpReg=1;
    count(WasmJitAzrokFaceWisCounter::LookupHits);
}

void capture(CPU* cpu,ProcessState* state) {
    count(WasmJitAzrokFaceWisCounter::CaptureEntries);
    cpu->tmpReg=0;
    if (!epochsMatch(state)) return;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    if (!cwdWriterTokenMatches(state)) return;
#endif
    const U32 ebp=cpu->reg[regBP].u32;
    if (ebp<0x114u) {
        count(WasmJitAzrokFaceWisCounter::FallbackPath);
        return;
    }
    CanonicalPath path;
    if (!readCanonicalPath(cpu->memory,ebp-0x114u,path)) {
        count(WasmJitAzrokFaceWisCounter::FallbackPath);
        return;
    }
    const S32 signedIndex=manifestIndexForPath(path);
    if (signedIndex<0) {
        count(WasmJitAzrokFaceWisCounter::FallbackManifest);
        return;
    }
    const U32 index=(U32)signedIndex;
    const U32 descriptor=cpu->reg[regAX].u32;
    if (!descriptor) {
        count(WasmJitAzrokFaceWisCounter::FallbackDescriptor);
        return;
    }
    std::lock_guard<std::mutex> lock(state->captureMutex);
    if (!epochsMatch(state)) return;
    CacheEntry& entry=state->entries[index];
    if (entry.phase.load(std::memory_order_acquire)==EntryDisabled) return;
    // A synthetic descriptor reaches this boundary on every hit.  It must not
    // be recaptured or mistaken for a new allocator-tail observation.
    if (entry.guestDescriptor && descriptor==entry.guestDescriptor) return;

    std::shared_ptr<FsNode> node;
    std::shared_ptr<FsZipNode> zipNode;
    if (!resolveAssetIdentity(*state,path,index,node,zipNode)) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        disableRetryState(*state,true);
#else
        state->enabled.store(false,std::memory_order_release);
#endif
        count(WasmJitAzrokFaceWisCounter::FallbackVfsIdentity);
        return;
    }
    if (!epochsMatch(state)) return;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    if (!cwdWriterTokenMatches(state)) return;
#endif
    U32 payload=0;
    U8 contentHash[32];
    U8 tailHash[32];
    if (!descriptorDigests(cpu->memory,descriptor,
            WASM_JIT_AZROK_FACE_WIS_MANIFEST[index],payload,contentHash,
            tailHash)) {
        count(WasmJitAzrokFaceWisCounter::FallbackPayloadIdentity);
        return;
    }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
    if (!cwdWriterTokenMatches(state)) return;
#endif
    const U32 phase=entry.phase.load(std::memory_order_relaxed);
    entry.pathHash=path.hash;
    ++entry.captures;
    if (phase==EntryEmpty) {
        if (!epochsMatch(state)) return;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
        if (!cwdWriterTokenMatches(state)) return;
#endif
        if (!allocateSnapshot(*state,entry,payload,
                cpu->memory->readd(descriptor),
                cpu->memory->readd(descriptor+4))) {
            entry.phase.store(EntryDisabled,std::memory_order_release);
            count(WasmJitAzrokFaceWisCounter::FallbackAllocation);
            publishEntrySnapshot(index,entry);
            return;
        }
        entry.node=node;
        entry.zipNode=zipNode;
        entry.zipGeneration=zipNode->fsZip->getAzrokFaceWisGeneration();
        std::memcpy(entry.tailHash,tailHash,sizeof(entry.tailHash));
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
        if (!cwdWriterTokenMatches(state)) {
            entry.phase.store(EntryDisabled,std::memory_order_release);
            return;
        }
#endif
        entry.phase.store(EntryWarming,std::memory_order_release);
        count(WasmJitAzrokFaceWisCounter::CaptureFirstSnapshots);
        publishEntrySnapshot(index,entry);
        return;
    }
    if (entry.node!=node || entry.zipNode!=zipNode ||
            entry.zipGeneration!=zipNode->fsZip->getAzrokFaceWisGeneration()) {
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
        disableRetryState(*state,true);
#else
        state->enabled.store(false,std::memory_order_release);
#endif
        count(WasmJitAzrokFaceWisCounter::FallbackVfsIdentity);
        return;
    }
    ++entry.tailComparisons;
    count(WasmJitAzrokFaceWisCounter::TailComparisons);
    if (std::memcmp(entry.tailHash,tailHash,sizeof(entry.tailHash))!=0) {
        ++entry.tailChanges;
        entry.phase.store(EntryDisabled,std::memory_order_release);
        count(WasmJitAzrokFaceWisCounter::TailChanged);
        count(WasmJitAzrokFaceWisCounter::EntriesDisabledTail);
        publishEntrySnapshot(index,entry);
        return;
    }
    count(WasmJitAzrokFaceWisCounter::TailStable);
    if (phase==EntryWarming) {
        entry.hitsUntilProbe.store(TAIL_PROBE_HITS,std::memory_order_relaxed);
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
        if (!cwdWriterTokenMatches(state)) {
            entry.phase.store(EntryDisabled,std::memory_order_release);
            return;
        }
#endif
        entry.phase.store(EntryAdmitted,std::memory_order_release);
        count(WasmJitAzrokFaceWisCounter::CaptureStableAdmissions);
    } else {
        count(WasmJitAzrokFaceWisCounter::CaptureRefreshes);
    }
    publishEntrySnapshot(index,entry);
}

} // namespace

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
void wasmJitAzrokFaceWisHleNoteCwdEpochSkip(bool fchdir) {
    const U32 index=fchdir ?
        (U32)WasmJitAzrokFaceWisCwdEpochCounter::FchdirBumpsSkipped :
        (U32)WasmJitAzrokFaceWisCwdEpochCounter::ChdirBumpsSkipped;
    cwdEpochCounters[index].fetch_add(1,std::memory_order_relaxed);
}
void wasmJitAzrokFaceWisHleNoteCwdBoundary(bool fixed) {
    const U32 index=fixed ?
        (U32)WasmJitAzrokFaceWisCwdEpochCounter::FixedBoundaryPasses :
        (U32)WasmJitAzrokFaceWisCwdEpochCounter::NonFixedBoundaryFallbacks;
    cwdEpochCounters[index].fetch_add(1,std::memory_order_relaxed);
}
#endif

void wasmJitAzrokFaceWisHleHelper(CPU* cpu) {
    count(WasmJitAzrokFaceWisCounter::HelperEntries);
    if (!cpu) return;
    cpu->tmpReg=0;
    try {
        ProcessState* state=getOrCreateState(cpu);
        if (!state || (!state->enabled.load(std::memory_order_acquire)
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
                && !retryDisabledProcessIdentity(state)
#endif
                )) {
            count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
            return;
        }
        const U32 boundary=cpu->eip.u32;
        if (boundary!=LOOKUP_EIP && boundary!=CAPTURE_EIP) {
            count(WasmJitAzrokFaceWisCounter::UnknownBoundary);
            return;
        }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
        const bool discoveryFixed=guestWineDosCwdIsFixedAppBase(cpu);
        cwdDiscoveryCounters[(U32)
            WasmJitAzrokFaceWisCwdDiscoveryCounter::BoundarySamples].fetch_add(
                1,std::memory_order_relaxed);
        cwdDiscoveryCounters[(U32)(discoveryFixed ?
            WasmJitAzrokFaceWisCwdDiscoveryCounter::FixedCFiles :
            WasmJitAzrokFaceWisCwdDiscoveryCounter::NonFixedOrUnreadable)].fetch_add(
                1,std::memory_order_relaxed);
        if (!state->process->azrokFaceWisCwdWriterTokenMatches(
                state->cwdWriterBaselineEpoch) || !discoveryFixed ||
                !state->process->azrokFaceWisCwdWriterTokenMatches(
                    state->cwdWriterBaselineEpoch)) {
            wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback();
            return;
        }
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
        const bool fixedCwd=guestWineDosCwdIsFixedAppBase(cpu);
        wasmJitAzrokFaceWisHleNoteCwdBoundary(fixedCwd);
        if (!fixedCwd) return;
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
        // Warmup is fail-closed and must observe scoped identity before it
        // consumes a boundary. No app bind/capture/slab path is reachable.
        if (!epochsMatch(state)) {
            count(WasmJitAzrokFaceWisCounter::FallbackProcessIdentity);
            return;
        }
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
        if (!cwdWriterTokenMatches(state)) {
            wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback();
            return;
        }
#endif
        if (consumeStabilityWarmupBoundary(*state)) return;
#endif
        if (boundary==LOOKUP_EIP) {
            lookup(cpu,state);
        } else {
            capture(cpu,state);
        }
    } catch (...) {
        // Candidate policy is fail-closed: no cache/OOM/VFS failure becomes a
        // guest-visible exception.  The grouped wrapper observes tmpReg=0 and
        // executes the original generated body.
        cpu->tmpReg=0;
        count(WasmJitAzrokFaceWisCounter::FallbackAllocation);
    }
}

void wasmJitAzrokFaceWisHleProcessReset(KProcess* process) {
    if (!process) return;
    ProcessState* state=(ProcessState*)process->detachAzrokFaceWisHleState();
    if (!state) return;
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
    resetStabilityWarmup(*state,false,false,false);
#endif
#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
    state->retryStatus.store(RetryInvalidated,std::memory_order_release);
#endif
    state->enabled.store(false,std::memory_order_release);
    for (CacheEntry& entry:state->entries) {
        if (entry.guestBase && state->memory) {
            state->memory->unmapNativeMemory(entry.guestBase,
                SYNTHETIC_SLAB_BYTES);
        }
        if (entry.slab) std::free(entry.slab);
        entry.slab=nullptr;
        entry.guestBase=0;
        entry.guestDescriptor=0;
    }
    delete state;
    count(WasmJitAzrokFaceWisCounter::ProcessResets);
}

extern "C" U32 wasm_jit_get_azrok_face_wis_hle_abi_version() { return 1; }
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_helper_index() { return 36; }
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_tmp_reg_offset() {
    return offsetof(CPU,tmpReg);
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_lookup_eip() {
    return LOOKUP_EIP;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_capture_eip() {
    return CAPTURE_EIP;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_manifest_count() {
    return WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_COUNTER_COUNT*(U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_hle_snapshot(U64* output,
        U32 outputBytes) {
    if (!output || outputBytes<wasm_jit_get_azrok_face_wis_hle_snapshot_bytes())
        return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_COUNTER_COUNT;++i)
        output[i]=counters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_azrok_face_wis_hle_counters() {
    for (auto& value:counters) value.store(0,std::memory_order_relaxed);
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_entry_snapshot_bytes() {
    return sizeof(EntrySnapshot);
}
extern "C" U32 wasm_jit_copy_azrok_face_wis_hle_entry_snapshot(U32 index,
        U8* output,U32 outputBytes) {
    if (!output || index>=WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT ||
            outputBytes<sizeof(EntrySnapshot)) return 0;
    std::lock_guard<std::mutex> lock(diagnosticMutex);
    std::memcpy(output,&latestEntrySnapshots[index],sizeof(EntrySnapshot));
    return sizeof(EntrySnapshot);
}

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_abi_version() { return 1; }
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_interval() {
    return PROCESS_IDENTITY_RETRY_INTERVAL;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_RETRY_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_retry_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_RETRY_COUNTER_COUNT*(U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_hle_retry_snapshot(U64* output,
        U32 outputBytes) {
    if (!output || outputBytes<
            wasm_jit_get_azrok_face_wis_hle_retry_snapshot_bytes()) return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_RETRY_COUNTER_COUNT;++i)
        output[i]=retryCounters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_azrok_face_wis_hle_retry_counters() {
    for (auto& value:retryCounters) value.store(0,std::memory_order_relaxed);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_abi_version() {
    return 1;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_SCOPED_GENERATION_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_scoped_generation_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_SCOPED_GENERATION_COUNTER_COUNT*
        (U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_hle_scoped_generation_snapshot(
        U64* output,U32 outputBytes) {
    if (!output || outputBytes<
            wasm_jit_get_azrok_face_wis_hle_scoped_generation_snapshot_bytes())
        return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_SCOPED_GENERATION_COUNTER_COUNT;++i)
        output[i]=scopedGenerationCounters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_azrok_face_wis_hle_scoped_generation_counters() {
    for (auto& value:scopedGenerationCounters)
        value.store(0,std::memory_order_relaxed);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_abi_version() {
    return 1;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_boundary_count() {
    return PROCESS_IDENTITY_STABILITY_WARMUP_BOUNDARIES;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_warmup_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT*(U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_hle_warmup_snapshot(
        U64* output,U32 outputBytes) {
    if (!output || outputBytes<
            wasm_jit_get_azrok_face_wis_hle_warmup_snapshot_bytes()) return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT;++i) {
        output[i]=i==(U32)WasmJitAzrokFaceWisWarmupCounter::RemainingGauge ?
            (U32)warmupGaugeState.load(std::memory_order_acquire) :
            warmupCounters[i].load(std::memory_order_relaxed);
    }
}
extern "C" void wasm_jit_reset_azrok_face_wis_hle_warmup_counters() {
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_WARMUP_COUNTER_COUNT;++i) {
        if (i==(U32)WasmJitAzrokFaceWisWarmupCounter::RemainingGauge) continue;
        warmupCounters[i].store(0,std::memory_order_relaxed);
    }
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_abi_version() {
    return 1;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_cwd_discovery_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_COUNTER_COUNT*(U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_cwd_discovery_snapshot(
        U64* output,U32 outputBytes) {
    if (!output || outputBytes<
            wasm_jit_get_azrok_face_wis_cwd_discovery_snapshot_bytes()) return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_COUNTER_COUNT;++i)
        output[i]=cwdDiscoveryCounters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_azrok_face_wis_cwd_discovery_counters() {
    for (auto& value:cwdDiscoveryCounters)
        value.store(0,std::memory_order_relaxed);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
void wasmJitAzrokFaceWisCwdDiscoveryNoteHook(bool resolved) {
    cwdDiscoveryCounters[(U32)(resolved ?
        WasmJitAzrokFaceWisCwdDiscoveryCounter::HookResolved :
        WasmJitAzrokFaceWisCwdDiscoveryCounter::HookUnresolved)].fetch_add(
            1,std::memory_order_relaxed);
}
void wasmJitAzrokFaceWisCwdDiscoveryNoteSetter() {
    cwdDiscoveryCounters[(U32)
        WasmJitAzrokFaceWisCwdDiscoveryCounter::SetterObserved].fetch_add(
            1,std::memory_order_relaxed);
}
void wasmJitAzrokFaceWisCwdDiscoveryNoteRelativeFallback() {
    cwdDiscoveryCounters[(U32)
        WasmJitAzrokFaceWisCwdDiscoveryCounter::RelativeFallbacks].fetch_add(
            1,std::memory_order_relaxed);
}
#endif

#ifdef BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_abi_version() {
    return 1;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_counter_count() {
    return WASM_JIT_AZROK_FACE_WIS_CWD_EPOCH_COUNTER_COUNT;
}
extern "C" U32 wasm_jit_get_azrok_face_wis_hle_cwd_epoch_snapshot_bytes() {
    return WASM_JIT_AZROK_FACE_WIS_CWD_EPOCH_COUNTER_COUNT*(U32)sizeof(U64);
}
extern "C" void wasm_jit_copy_azrok_face_wis_hle_cwd_epoch_snapshot(
        U64* output,U32 outputBytes) {
    if (!output || outputBytes<
            wasm_jit_get_azrok_face_wis_hle_cwd_epoch_snapshot_bytes()) return;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_CWD_EPOCH_COUNTER_COUNT;++i)
        output[i]=cwdEpochCounters[i].load(std::memory_order_relaxed);
}
extern "C" void wasm_jit_reset_azrok_face_wis_hle_cwd_epoch_counters() {
    for (auto& value:cwdEpochCounters)
        value.store(0,std::memory_order_relaxed);
}
#endif

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_FIXTURE)
bool wasmJitAzrokFaceWisHleManifestSelfTest() {
    U32 previous=0;
    for (U32 i=0;i<WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT;++i) {
        const auto& hashEntry=WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[i];
        if (i && hashEntry.fnv1a<=previous) return false;
        previous=hashEntry.fnv1a;
        if (hashEntry.manifestIndex>=WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT)
            return false;
        const char* path=WASM_JIT_AZROK_FACE_WIS_MANIFEST[
            hashEntry.manifestIndex].canonicalPath;
        U32 hash=2166136261u;
        for (const char* p=path;*p;++p) { hash^=(U8)*p; hash*=16777619u; }
        if (hash!=hashEntry.fnv1a) return false;
    }
    return DESCRIPTOR_FIELD0_EXPECTED==255u &&
        DESCRIPTOR_FIELD4_EXPECTED==256u &&
        WASM_JIT_AZROK_FACE_WIS_HEADER[0]==2 &&
        WASM_JIT_AZROK_FACE_WIS_HEADER[4]==0 &&
        WASM_JIT_AZROK_FACE_WIS_HEADER[5]==1 &&
        WASM_JIT_AZROK_FACE_WIS_HEADER[8]==0xff;
}
bool wasmJitAzrokFaceWisHleSha256SelfTest() {
    static constexpr U8 expected[32]={
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Sha256 sha;const U8 input[3]={'a','b','c'};U8 output[32];
    sha.update(input,3);sha.finish(output);
    return std::memcmp(output,expected,32)==0;
}
#endif

#endif
