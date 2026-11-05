/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __WASM_PGO_QUIESCE_H__
#define __WASM_PGO_QUIESCE_H__

#if defined(__TEST) || defined(BOXEDWINE_WASM_PGO_QUIESCE) || defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)

enum class WasmPgoQuiesceDomain : U8 {
    GuestCpu,
    MainLoop,
    NativeSocket,
};

struct WasmPgoQuiesceSnapshot {
    U64 lastEpoch;
    U32 registered;
    U32 expected;
    U32 arrived;
    U32 parked;
    U32 active;
    bool freezeActive;
    bool releasePending;
    bool failed;
};

// A token belongs to one execution domain. It is deliberately not a CPU or
// pthread pointer, so the coordinator cannot silently confuse a recycled
// thread address with a frozen participant.
U64 wasmPgoQuiesceRegister(WasmPgoQuiesceDomain domain);
bool wasmPgoQuiesceUnregister(U64 token);

// This may be called only at a domain's documented cold safe point. It blocks
// only while a matching freeze is active and returns true only after a normal
// release, never after timeout/cancel/membership invalidation.
bool wasmPgoQuiescePoll(U64 token);
bool wasmPgoQuiesceSetActive(U64 token, bool active);

// Begin snapshots the complete registered membership. A zero epoch means
// busy, empty, or still releasing a prior epoch. Capture code must require
// Ready(epoch) before touching any LLVM profile buffer API.
U64 wasmPgoQuiesceBegin();
bool wasmPgoQuiesceReady(U64 epoch);
bool wasmPgoQuiesceWaitReady(U64 epoch, U32 timeoutMs);
typedef bool (*WasmPgoQuiesceCaptureFn)(void* opaque);
// capture runs while the coordinator mutex is held; it must be bounded and
// non-reentrant. A false return cancels the epoch and releases every parker.
bool wasmPgoQuiesceCapture(U64 epoch, WasmPgoQuiesceCaptureFn capture, void* opaque);
bool wasmPgoQuiesceRelease(U64 epoch);
bool wasmPgoQuiesceCancel(U64 epoch);
WasmPgoQuiesceSnapshot wasmPgoQuiesceSnapshot();

#ifdef __TEST
void wasmPgoQuiesceResetForTest();
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
// Test-only post-run/pre-Poll gate.  It is deliberately absent from every
// production and standalone fixture build: its only job is to make a real
// guest-thread lifecycle test deterministic.
bool wasmPgoQuiesceTestArmCpuPostRunProbe(U32 expected);
bool wasmPgoQuiesceTestWaitCpuPostRunProbe(U32 expected, U32 timeoutMs);
bool wasmPgoQuiesceTestReleaseCpuPostRunProbe(U32 count);
void wasmPgoQuiesceTestReleaseAllCpuPostRunProbes();
bool wasmPgoQuiesceTestCpuPostRunProbe(U64 token);
#endif
#endif

#endif
#endif
