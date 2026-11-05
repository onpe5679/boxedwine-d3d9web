/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include "knativesystem.h"
#if defined(BOXEDWINE_JIT_ARMV8)
#include "../armv8/jitArmV8CodeGen.h"
#endif
#ifdef BOXEDWINE_JIT
#include "../jit/jitCodeLifecycle.h"
#endif
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
#include "../wasm/wasmPgoQuiesce.h"
#endif

#if defined(BOXEDWINE_MULTI_THREADED)

std::atomic<int> platformThreadCount = 0;
void platformInitExceptionHandling();

// inline (not noinline): runs after every cpu->run() in the hot loop, so an
// out-of-line call here would cost a call per dispatch (measurably slower on the
// fast JIT). Keeping the try/catch outside the noinline platformThreadRun measured
// ~4 PERF_W95 points faster for multiThreadedJit and is also valid on native hosts.
static inline bool platformThreadShouldStop(CPU* cpu) {
#ifdef __TEST
    if (cpu->nextOp && cpu->nextOp->inst == TestEnd) {
        return true;
    }
#endif
    if (cpu->thread->process->terminated) {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(cpu->memory->mutex);
        cpu->memory->cleanup();
    }
    return cpu->thread->terminating;
}

static NO_INLINE void platformThreadRun(CPU* cpu) {
    do {
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
        const U64 wasmPgoQuiesceToken = cpu->thread->wasmPgoQuiesceToken;
        if (!wasmPgoQuiesceToken || !wasmPgoQuiesceSetActive(wasmPgoQuiesceToken, true)) {
            kpanic("PGO CPU fixture lost its registered thread token");
        }
        try {
            cpu->run();
        } catch (...) {
            // An exception is not a post-run safe point.  Clear active before
            // platformThread's recovery path observes it, then rethrow.
            if (!wasmPgoQuiesceSetActive(wasmPgoQuiesceToken, false)) {
                kpanic("PGO CPU fixture could not clear active state after exception");
            }
            throw;
        }
        if (!wasmPgoQuiesceSetActive(wasmPgoQuiesceToken, false)) {
            kpanic("PGO CPU fixture could not clear active state");
        }
        // Poll only after a successful cpu->run() return.  If the thread is
        // terminal, unregister below invalidates a pending epoch instead.
        if (!platformThreadShouldStop(cpu)) {
#if defined(__TEST)
            if (!wasmPgoQuiesceTestCpuPostRunProbe(wasmPgoQuiesceToken)) {
                continue;
            }
#endif
            wasmPgoQuiescePoll(wasmPgoQuiesceToken);
        }
#else
        cpu->run();
#endif
    } while (!platformThreadShouldStop(cpu));
}

static void platformThread(CPU* cpu) {
#ifdef BOXEDWINE_HOST_EXCEPTIONS
    platformInitExceptionHandling();
#endif
#if defined(BOXEDWINE_JIT_ARMV8)
    ensureArmV8HardwareTSOForThread();
#endif
    KThread::setCurrentThread(cpu->thread);
    KProcessPtr process = KSystem::getProcess(cpu->thread->process->id);

    cpu->nextOp = cpu->getNextOp();
    if (!cpu->nextOp) {
        cpu->thread->seg_mapper(cpu->getEipAddress(), true, false, false);
        cpu->nextOp = cpu->getNextOp();
        if (!cpu->nextOp) {
			kpanic_fmt("Failed to get first op for thread %d of process %d at address %x", cpu->thread->id, process->id, cpu->getEipAddress());
		}
    }
#ifdef __TEST
    bool testEnded = false;
#endif
    while (true) {
        try {
            platformThreadRun(cpu);
#ifdef __TEST
            if (cpu->nextOp && cpu->nextOp->inst == TestEnd) {
                testEnded = true;
                break;
            }
#endif
            break;
        } catch (...) {
            if (!cpu->thread->terminating) {
                cpu->nextOp = cpu->getNextOp();
            }
            if (platformThreadShouldStop(cpu)) {
                break;
            }
        }
    }

#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
    const U64 wasmPgoQuiesceToken = cpu->thread->wasmPgoQuiesceToken;
    if (!wasmPgoQuiesceToken || !wasmPgoQuiesceUnregister(wasmPgoQuiesceToken)) {
        kpanic("PGO CPU fixture could not unregister thread token");
    }
    cpu->thread->wasmPgoQuiesceToken = 0;
#endif

#ifdef __TEST
    if (testEnded) {
        // TestEnd intentionally leaves KThread ownership to the test, but its
        // native worker has still exited and must not leak the global count.
        platformThreadCount--;
        return;
    }
#endif

    cpu->thread->cleanup();

    platformThreadCount--;
    process->deleteThread(cpu->thread);
    if (platformThreadCount == 0) {
        KSystem::shutingDown = true;
        KNativeSystem::postQuit();
    }
}

static void* platformThreadStart(void* arg) {
    platformThread((CPU*)arg);
    return nullptr;
}

#ifdef __TEST
void initThreadForTesting() {
}

void joinThread(KThread* thread) {
    platformJoinThread(thread);
}
#endif

void scheduleThread(KThread* thread) {
    platformThreadCount++;
    CPU* cpu = thread->cpu;
#ifdef BOXEDWINE_JIT
    jitThreadStartPreparing(cpu);
#endif
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
    thread->wasmPgoQuiesceToken = wasmPgoQuiesceRegister(WasmPgoQuiesceDomain::GuestCpu);
    if (!thread->wasmPgoQuiesceToken) {
        kpanic("PGO CPU fixture could not register thread token");
    }
#endif
    S32 result = platformStartThread(thread, platformThreadStart);
    if (result) {
#if defined(BOXEDWINE_WASM_PGO_QUIESCE_CPU_FIXTURE)
        if (!wasmPgoQuiesceUnregister(thread->wasmPgoQuiesceToken)) {
            kpanic("PGO CPU fixture could not unregister failed thread start");
        }
        thread->wasmPgoQuiesceToken = 0;
#endif
#ifdef BOXEDWINE_JIT
        jitThreadStartCancelled(cpu);
#endif
        platformThreadCount--;
        kpanic_fmt("platformStartThread failed: %d", result);
        return;
    }
    if (!thread->process->isSystemProcess() && KSystem::cpuAffinityCountForApp) {
        Platform::setCpuAffinityForThread(thread, KSystem::cpuAffinityCountForApp);
    }
}

void terminateOtherThread(const KProcessPtr& process, U32 threadId) {
    KThread* thread = process->getThreadById(threadId);
    if (thread) {
        thread->terminating = true;

        const std::shared_ptr<BoxedWineCondition> cond = thread->waitingCond;

        // wake up the thread if it is waiting
        if (cond) {
            cond->lock();
            cond->signal();
            cond->unlock();
        }
    }

    while (true) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(process->threadRemovedCondition);
        if (!process->getThreadById(threadId)) {
            break;
        }
        BOXEDWINE_CONDITION_WAIT_TIMEOUT(process->threadRemovedCondition, 1000);
    }
}

void terminateCurrentThread(KThread* thread) {
    thread->terminating = true;
}

void unscheduleThread(KThread* thread) {
}

#endif
