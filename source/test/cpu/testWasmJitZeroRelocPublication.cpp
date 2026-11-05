/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "boxedwine.h"
#include "testCPU.h"
#include "testWasmJitZeroRelocPublication.h"

#if defined(__TEST) && defined(BOXEDWINE_WASM_JIT) && \
        defined(BOXEDWINE_MULTI_THREADED) && defined(__EMSCRIPTEN__) && \
        defined(BOXEDWINE_WASM_JIT_ZERO_RELOC_PUBLICATION_TOKEN)

#include "../../emulation/cpu/wasm/jitWasmCodeGen.h"
#include "../../emulation/cpu/wasm/wasmEmitter.h"
#include "../../emulation/cpu/wasm/wasmModuleMerger.h"
#include <pthread.h>

namespace {

std::vector<U8> makeZeroRelocPublicationModule(S32 value) {
    WasmEmitter emitter;
    U32 type = emitter.addFuncType({WasmType::I32, WasmType::I32}, {});
    emitter.addMemoryImport("env", "memory");
    U32 function = emitter.addFunction(type);
    emitter.addExport("execute", function);
    emitter.beginFunction({});
    emitter.emitLocalGet(0);
    emitter.emitI32Const(value);
    emitter.emitI32Store(0);
    emitter.endFunction();
    return emitter.finalize();
}

struct ZeroRelocWorkerArgs {
    S32 slot = -1;
    bool installed = false;
    U32 token = 0;
};

void* zeroRelocWorker(void* opaque) {
    ZeroRelocWorkerArgs* args = (ZeroRelocWorkerArgs*)opaque;
    args->installed = wasmJitTestLazyInstallMtSlot(args->slot);
    args->token = wasmJitTestGetMtGroupPublicationToken(args->slot);
    return nullptr;
}

bool runZeroRelocWorker(ZeroRelocWorkerArgs& args, KMemory* owner) {
    wasmJitTestPrepareMtThreadStart(&args, owner);
    pthread_t thread;
    if (pthread_create(&thread, nullptr, zeroRelocWorker, &args)) {
        wasmJitTestCancelMtThreadStart(&args);
        return false;
    }
    return pthread_join(thread, nullptr) == 0;
}

struct RestoreBrokerMode {
    S32 oldEnabled;
    ~RestoreBrokerMode() {
        wasmJitTestSetMtModuleBrokerEnabled(oldEnabled);
    }
};

} // namespace

void testWasmJitZeroRelocPublication() {
    if (wasm_jit_get_zero_reloc_publication_token_abi_version() != 1) {
        testFail("MT WASM zero-reloc publication capability is not ABI v1");
        return;
    }

    RestoreBrokerMode restoreBroker{wasmJitTestSetMtModuleBrokerEnabled(0)};
    std::vector<U8> first = makeZeroRelocPublicationModule(0x11223344);
    std::vector<U8> second = makeZeroRelocPublicationModule(0x55667788);
    std::vector<WasmJitMergeInput> inputs = {{&first}, {&second}};
    std::vector<U8> merged;
    BString error;
    if (!wasmJitMergeModules(inputs, merged, error)) {
        testFail("MT WASM zero-reloc publication fixture merge: %s",
            error.c_str());
        return;
    }

    // The two registered originals deliberately share EIP and differ only in
    // block hash.  This proves key identity and recompilation never alias.
    constexpr U32 eip = 0x51510000;
    constexpr U32 firstHash = 0xa1000001;
    constexpr U32 secondHash = 0xa1000002;
    int groupIdx = wasm_jit_mt_register_group(merged.data(), (int)merged.size());
    wasm_jit_mt_register_group_entry(groupIdx, eip, firstHash, "b0", 0);
    wasm_jit_mt_register_group_entry(groupIdx, eip, secondHash, "b1", 0);

    int firstSlot = wasmJitTestInstantiateMtGroupEntry(
        (U32)groupIdx, 0, testContext().memory);
    int secondSlot = wasmJitTestInstantiateMtGroupEntry(
        (U32)groupIdx, 1, testContext().memory);
    U32 firstToken = wasmJitTestGetMtGroupPublicationToken(firstSlot);
    U32 secondToken = wasmJitTestGetMtGroupPublicationToken(secondSlot);
    U32 zeroRelocEntries = 0;
    U32 payloadBytes = wasmJitTestGetMtImportedGroupPublicationPayloadBytes(
        (U32)groupIdx, testContext().memory, &zeroRelocEntries);
    if (firstSlot <= 0 || secondSlot <= 0 || firstSlot == secondSlot ||
            firstToken != (U32)firstSlot ||
            secondToken != (U32)secondSlot ||
            zeroRelocEntries != 2 || payloadBytes != 2 * sizeof(U32)) {
        testFail("MT WASM zero-reloc originals allocate exact U32[1] tokens: first=%d/%x second=%d/%x entries=%u bytes=%u",
            firstSlot, firstToken, secondSlot, secondToken,
            zeroRelocEntries, payloadBytes);
        return;
    }

    DecodedOp publication;
    if (wasmJitTestLoadMtPublishedCode(&publication) != 0 ||
            !wasmJitTestPublishMtGroupCode(&publication, firstSlot) ||
            wasmJitTestLoadMtPublishedCode(&publication) != firstToken) {
        testFail("MT WASM zero-reloc publish exposes token == atomic pfnJitCode");
    }

    U32 invalidated = wasmJitTestUnpublishMtCode(&publication);
    if (invalidated != firstToken ||
            wasmJitTestLoadMtPublishedCode(&publication) != 0 ||
            wasmJitTestGetMtGroupPublicationToken(firstSlot) != firstToken) {
        testFail("MT WASM zero-reloc invalidation clears pfn before token lifetime ends");
    }

    if (!wasmJitTestPublishMtGroupCode(&publication, secondSlot) ||
            wasmJitTestLoadMtPublishedCode(&publication) != secondToken ||
            secondToken == firstToken ||
            wasmJitTestGetMtGroupPublicationToken(firstSlot) != firstToken) {
        testFail("MT WASM same-EIP different-hash recompile publishes a new exact token");
    }
    wasmJitTestUnpublishMtCode(&publication);

    DecodedOp nullPublication;
    if (!firstToken || wasmJitTestLoadMtPublishedCode(&nullPublication) != 0 ||
            firstToken == wasmJitTestLoadMtPublishedCode(&nullPublication)) {
        testFail("MT WASM null publication fails the exact token comparison");
    }

    ZeroRelocWorkerArgs worker;
    worker.slot = firstSlot;
    if (!runZeroRelocWorker(worker, testContext().memory) ||
            !worker.installed || worker.token != firstToken) {
        testFail("MT WASM zero-reloc token is stable across worker-local installs: installed=%d token=%x expected=%x",
            worker.installed, worker.token, firstToken);
    }

    wasmJitTestRetireMtSlot(firstSlot);
    wasmJitTestRetireMtSlot(secondSlot);
    wasmJitTestReapMtRetiredSlots(testContext().memory);
    if (wasmJitTestHasMtSlotMetadata(firstSlot) ||
            wasmJitTestHasMtSlotMetadata(secondSlot) ||
            wasmJitTestGetMtGroupPublicationToken(firstSlot) != 0 ||
            wasmJitTestGetMtGroupPublicationToken(secondSlot) != 0) {
        testFail("MT WASM zero-reloc token storage is reclaimed after slot quiescence");
    }
}

#else

void testWasmJitZeroRelocPublication() {
}

#endif
