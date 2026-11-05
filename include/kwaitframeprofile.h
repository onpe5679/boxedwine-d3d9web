/*
 * Azrok-only, compile-gated wait residency profiler.
 *
 * The syscall side reuses ksyscall()'s existing entry/exit timestamps.  The
 * D3D9 side only advances the aggregation window at a completed Present, so
 * no profiler clock read is added on either path.
 */
#ifndef __KWAITFRAMEPROFILE_H__
#define __KWAITFRAMEPROFILE_H__

#include <memory>

#ifdef BOXEDWINE_AZROK_WAIT_FRAME_PROFILE

class CPU;
class KObject;
class KMemory;
class KThread;

enum KWaitFrameProfileKind : U32 {
    K_WAIT_FRAME_SELECT = 0,
    K_WAIT_FRAME_NANOSLEEP,
    K_WAIT_FRAME_CLOCK_NANOSLEEP,
    K_WAIT_FRAME_POLL,
    K_WAIT_FRAME_FUTEX,
    K_WAIT_FRAME_READ,
    K_WAIT_FRAME_KIND_COUNT
};

struct KWaitFrameProfilePending {
    U64 requestedMicros = 0;
    U32 kind = K_WAIT_FRAME_KIND_COUNT;
    U32 fd = 0xffffffffu;
    U32 requestedLen = 0;
    U32 syscallEip = 0;
    U32 wineRequestId = 0xffffffffu;
    U32 wineRequestFd = 0xffffffffu;
    U32 objectType = 0xffffffffu;
    std::shared_ptr<KObject> object;
    bool active = false;
    bool requestedKnown = false;
    bool wineRequestWrite = false;
#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
    bool causalWis = false;
#ifdef BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
    // Diagnostic-only witnesses needed after the guest syscall returns.
    // The read buffer carries Wine's reply header/create_file handle, while
    // the KThread identity prevents a host-worker-local match from being
    // mistaken for a same-client-thread match.
    KMemory* causalMemory = nullptr;
    KThread* causalThread = nullptr;
    U32 causalReplyAddress = 0;
    bool causalWisFdClose = false;
#endif
#endif
};

KWaitFrameProfilePending prepareAzrokWaitFrameProfile(CPU* cpu, U32 syscallNo);
void recordAzrokWaitFrameProfile(const KWaitFrameProfilePending& pending, U32 result,
                                 U64 startMicros, U64 endMicros);
void markAzrokWaitFrameProfile(U32 phase, U32 timeoutMillis, U32 caller, U32 value);
void presentAzrokWaitFrameProfile(CPU* cpu);

#ifdef BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
// These hooks are diagnostics only.  They preserve every production lock,
// cursor and backend decision and only accumulate one 120-Present snapshot.
enum KAzrokCausalWisStage : U32 {
    K_AZROK_CAUSAL_WIS_FILE_POS_WAIT = 0,
    K_AZROK_CAUSAL_WIS_OUTER_WAIT,
    K_AZROK_CAUSAL_WIS_OUTER_HELD,
    K_AZROK_CAUSAL_WIS_PAGE_PREPARE,
    K_AZROK_CAUSAL_WIS_CACHE_LOOKUP,
    K_AZROK_CAUSAL_WIS_CACHE_MEMCPY,
    K_AZROK_CAUSAL_WIS_HOST_READ,
    K_AZROK_CAUSAL_WIS_ZIP_SETUP,
    K_AZROK_CAUSAL_WIS_ZIP_INFLATE,
    K_AZROK_CAUSAL_WIS_STAGE_COUNT
};

enum KAzrokCausalWisBackend : U32 {
    K_AZROK_CAUSAL_WIS_BACKEND_CACHE = 0,
    K_AZROK_CAUSAL_WIS_BACKEND_DIRECT,
    K_AZROK_CAUSAL_WIS_BACKEND_ZIP,
    K_AZROK_CAUSAL_WIS_BACKEND_HOST,
    K_AZROK_CAUSAL_WIS_BACKEND_COUNT
};

bool azrokCausalWisActive();
U64 azrokCausalProfileNow();
void azrokCausalWisRecordStage(U32 stage, U64 startMicros, U64 endMicros);
void azrokCausalWisRecordBackend(U32 backend);
void azrokCausalWisRecordCacheLookup(bool hit);

// Called only after the complete guest write/writev payload has been copied
// into its Unix-socket peer buffer and before that peer is signalled.
void azrokCausalWineSocketPublished(U32 bytes);
#endif

#endif

#endif
