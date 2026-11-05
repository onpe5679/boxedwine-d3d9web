# PATCHES - delta of this fork against upstream Boxedwine

## Base

- Upstream: <https://github.com/danoon2/Boxedwine>
- Base commit: **`35dc3f0875a2d954b2fab22b9f357d9bc8ab7e69`** (2026-07-20, "updated jenkins file to copy extra files for the web artifact ..."), `git describe` = `26R1.0-198-g35dc3f0`. **VERIFIED** (see method).
- Upstream has moved on (478 commits to 2026-11-01 in the clone we compared); none of them are merged here.
- Method: every fork source file was hashed (git blob SHA-1, raw and LF-normalised) and compared to the tree of each of the 929 commits in a shallow upstream clone. Commit `35dc3f0` has the fewest mismatching files (61 of 3500 common files; its parent `f3176f6` has 62 and older commits more). The 61 mismatches are exactly the "modified" files below, so every difference is accounted for.
- Totals (`git diff --numstat --ignore-cr-at-eol`): **61 modified files, +16,884 / -471 lines**; 22 added source/header/script files are included here; 181 further added files (logs, Azrok-only verification scripts, WSL wrappers, data maps) are not published.
- Changes that are **not** in this source tree: the guest filesystem (Wine 11 / Tiny Core Linux with Korean locale, XIM, audio and Winsock fixes) is a separate binary asset - see `RELEASE_ASSETS.md`.
- Repo layout differences vs upstream: upstream `README.md` is kept as `README.upstream-boxedwine.md`; upstream `.gitignore` is merged into ours; the `d3d9web/` directory is new; upstream-committed prebuilt binaries (`*.lib`, `*.dll`, `*.so`, `*.jar`, `*.pfx`) and 117 macOS Carthage files missing in the working copy are omitted. Windows/macOS/Linux desktop builds therefore need the omitted upstream files; this repo targets the web build.

- **Removed upstream personal IDE state files** (not needed to build): `tools/BoxedWineRunner/.idea/workspace.xml`, `tools/vulkan/gen/.idea/workspace.xml`, the two `xcuserdata/<upstream author>.xcuserdatad` folders (under `lib/sdl2/Xcode/SDL/SDL.xcodeproj` and `project/mac-xcode/Boxedwine/Boxedwine.xcodeproj`), `UserInterfaceState.xcuserstate` and a macOS Finder file (`SDL_DS_Store`). The other `.idea/*.xml` project files were kept (they contain no user paths or names).

- **Edited for privacy** (home-path prefixes replaced by `<home>` in 6 places; none affects the build): `docs/How-To-Build-Boxedwine.md` and `commandLine.txt` (example Windows profile paths), `lib/sdl2/include/SDL_filesystem.h` (two example paths in a comment), `lib/sdl2/src/hidapi/testgui/start.sh` and `lib/sdl2/Xcode/SDL/SDL.xcodeproj/project.pbxproj` (absolute paths on the upstream authors' machines). Apart from these edits, the rename of `README.md` and the merge of `.gitignore`, the staging process changed no upstream-identical file.

Legend - **generic**: useful for other games/EXEs; **Azrok-specific**: only meaningful for the one title this was developed against; **generic mechanism, Azrok-named flag**: mechanism is game-agnostic but the identifier says Azrok. Tags VERIFIED / INFERRED / NOT VERIFIED follow the staging report.

## Azrok-specific code - safe to disable for other games

The tree keeps the code exactly as it ran in production, including parts that only make sense for the one game it was developed against. **Everything listed here is Azrok-specific and safe to disable for other games**: it is switched on only by `-D` flags, and the generic make target does not set them.

**Files (all flag-guarded):** `source/emulation/cpu/wasm/wasmJitAzrokFaceWisHle.{cpp,h}`, `wasmJitAzrokFaceWisManifest.h`, `wasmJitAcknexTransformIsland.{cpp,h}`, `project/emscripten/boxedwine-face-wis-hle-worker-alert-pre.js`, `source/test/cpu/testWasmJitAcknex*.{cpp,h}`; hook sites in `include/kprocess.h`, `source/kernel/{kprocess,kmemory}.cpp`, `source/io/fszip*`, `fsfilenode*`, `source/emulation/cpu/normal/normalCPU.cpp`, `jitWasmCodeGen.{cpp,h}`.

**Make targets:**

| target | `-D` switches | use |
|---|---:|---|
| `azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoopsFaceWisHleCwdIndependentEpoch` (`Build/FOTR10`) | 29 | the build that was deployed; Azrok-specific code **on** |
| `azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoops` (`Build/AzrokTouch2MultiDirectLoops1`) | 9 | **generic target: Azrok-specific code compiled out** |

**The generic target's switches (VERIFIED from the makefile):** `BOXEDWINE_MULTI_THREADED`, `BOXEDWINE_WASM_JIT`, `BOXEDWINE_D3D9WEB`, `BOXEDWINE_D3D9WEB_ASYNC_RING`, `BOXEDWINE_AZROK_HOT_WIS_CACHE`, `BOXEDWINE_EMSCRIPTEN_SOCKETS`, `BOXEDWINE_D3D9WEB_AUDIO_GESTURE_READY`, `BOXEDWINE_IME_RUNTIME_DEBUG`, `BOXEDWINE_WASM_JIT_MULTI_DIRECT_LOOPS`.

**The 20 switches that only the deployed target sets (leave these out for other games; VERIFIED by diffing the two targets' flag lists):**

```
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE
BOXEDWINE_WASM_JIT_OWNER_THREAD_IDENTITY
BOXEDWINE_WASM_JIT_FACE_OWNER_TRACE_DIAGNOSTIC
BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_TRACE_DIAGNOSTIC
BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_RETRY_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_SCOPED_GENERATION_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
BOXEDWINE_WASM_JIT_FACE_STABILITY_WARMUP_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
BOXEDWINE_WASM_JIT_FACE_CWD_DISCOVERY_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
BOXEDWINE_WASM_JIT_FACE_CWD_INDEPENDENT_EPOCH_TRACE_DIAGNOSTIC
```

Also: the deployed target adds `--pre-js=boxedwine-face-wis-hle-worker-alert-pre.js` to the link flags and exports the `_wasm_jit_*azrok_face_wis*` functions; the generic target does neither (VERIFIED). The `BOXEDWINE_WASM_JIT_ACKNEX_*` switches are not set by either target.

**Caveats.** (1) "Safe to disable" rests on the code being `#if`-guarded and the flags being absent; the generic target was **not rebuilt or run for this release** (NOT VERIFIED). (2) The generic target is INFERRED to match the configuration that was deployed on 2026-08-31, before FACE-WIS HLE was added (deploy list in the maintainer's scripts). (3) `BOXEDWINE_AZROK_HOT_WIS_CACHE` is Azrok-named but stays **on** in the generic target: it is a generic hot-member cache for zip-backed files. `BOXEDWINE_IME_RUNTIME_DEBUG` also stays on.

## Features

### 1. D3D9 INT 0x9A bridge (host transport) + WebGL2 FFP renderer + guest d3d9.dll

**Classification:** generic

Guest `d3d9.dll` serialises Direct3D9 calls into ordered command batches and issues `INT 0x9A` (VERIFIED: inline `__asm { ... _emit 0xCD _emit 0x9A }` in `d3d9web/guest/d3d9_web.cpp`, minimal example in `d3d9web/guest/smoke/phase0_int9a_guest.c`). The emulator's existing INT 0x9A (Vulkan thunk) handler forwards indices in the private `0xd3900000` range to `callD3D9Web()` when `BOXEDWINE_D3D9WEB` is defined (VERIFIED: `common_other.cpp`, `normal_other.h`). `source/opengl/d3d9web.cpp` collects batches and hands them to `globalThis.D3D9WebRuntime` (JS), which drives WebGL2 through a single fixed-function-pipeline vertex/fragment shader pair (`d3d9_ffp.vert`/`.frag`). The renderer, the shaders and the guest DLL live under `d3d9web/` (new files, not patches).

| file | change | size of change |
|---|---|---|
| `source/emulation/cpu/common/common_other.cpp` | modified | +4 / -1 |
| `source/emulation/cpu/normal/normal_other.h` | modified | +4 / -1 |
| `source/opengl/d3d9web.cpp` | added | 54,949 B |

### 2. Async command ring (BOXEDWINE_D3D9WEB_ASYNC_RING)

**Classification:** generic

Inside `d3d9web.cpp`: a 3-slot ring (`ASYNC_RING_SLOTS = 3`, VERIFIED) lets the emulated CPU publish a batch and continue while the browser main thread drains it via `MAIN_THREAD_ASYNC_EM_ASM`. Requires the multi-threaded (pthread) Emscripten build. No separate files; enabled in the production target.

### 3. Wasm JIT block/loop linking, register-threaded ABI, module broker

**Classification:** generic

Extensions to upstream's Wasm JIT: multi-direct loops (`BOXEDWINE_WASM_JIT_MULTI_DIRECT_LOOPS`, enabled in production), typed/register-threaded block entry ABI (`..._THREADED_BODY_ABI`, off in production), sticky-GP locals, inline parity/ZnZ, if-read caches, module merger and broker changes. Flag names are VERIFIED; the semantics here are INFERRED from names and comments - the 4.4k added lines in `jitWasmCodeGen.cpp` have not been reviewed one by one.

| file | change | size of change |
|---|---|---|
| `project/emscripten/boxedwine-wasm-jit-module-broker.js` | modified | +37 / -6 |
| `source/emulation/cpu/wasm/jitWasmCodeGen.cpp` | modified | +4422 / -243 |
| `source/emulation/cpu/wasm/jitWasmCodeGen.h` | modified | +345 / -5 |
| `source/emulation/cpu/wasm/wasmEmitter.cpp` | modified | +14 / -4 |
| `source/emulation/cpu/wasm/wasmEmitter.h` | modified | +6 / -0 |
| `source/emulation/cpu/wasm/wasmModuleMerger.cpp` | modified | +185 / -22 |

### 4. AOT/JIT-cache pipeline and shell-side manifest validation

**Classification:** generic

`boxedwine-wasm-jit-cache-pipeline.mjs` (+3.4k lines) post-processes a recorded JIT profile into a grouped cache zip; the shell validates that cache's manifest against the runtime's ABI version before use (`boxedwineValidate*Manifest` functions in the shell, VERIFIED). The cache itself is built from a recorded play session of one specific game (INFERRED from the cache file names) and is **not** part of this repo. `ntcurrentteb-original-region-spec.mjs` is imported by the pipeline (VERIFIED: sole import).

| file | change | size of change |
|---|---|---|
| `project/emscripten/boxedwine-shell.js` | modified | +409 / -21 |
| `project/emscripten/boxedwine-wasm-jit-cache-pipeline.mjs` | modified | +3442 / -64 |
| `project/emscripten/ntcurrentteb-original-region-spec.mjs` | added | 10,222 B |

### 5. JIT diagnostics, census counters and CPU/JIT test fixtures

**Classification:** generic

Flag-gated telemetry and `source/test` fixtures (MOVSD runtime zero guard, FISTP qword oracle, INT9A direct dispatch, composite-32 logic, threaded tail ...). All `*_CENSUS` / `*_FIXTURE` / `*_ORACLE` switches are off in the production target (VERIFIED from the make target).

| file | change | size of change |
|---|---|---|
| `source/emulation/cpu/common/cpu.cpp` | modified | +274 / -1 |
| `source/emulation/cpu/common/cpu.h` | modified | +81 / -0 |
| `source/test/cpu/testBit.cpp` | modified | +31 / -0 |
| `source/test/cpu/testBit.h` | modified | +1 / -0 |
| `source/test/cpu/testCPU.cpp` | modified | +863 / -0 |
| `source/test/cpu/testCPU.h` | modified | +6 / -0 |
| `source/test/cpu/testFPU.cpp` | modified | +185 / -0 |
| `source/test/cpu/testFPU.h` | modified | +2 / -0 |
| `source/test/cpu/testString.cpp` | modified | +993 / -0 |
| `source/test/cpu/testString.h` | modified | +3 / -0 |
| `source/test/cpu/testWasmJitBatch.cpp` | modified | +422 / -0 |
| `source/test/cpu/testWasmJitBatch.h` | modified | +2 / -0 |
| `source/test/cpu/testWasmJitModuleBroker.cpp` | modified | +211 / -25 |
| `source/test/cpu/testWasmJitMultiDirectLoops.cpp` | added | 12,230 B |
| `source/test/cpu/testWasmJitMultiDirectLoops.h` | added | 453 B |
| `source/test/cpu/testWasmJitZeroRelocPublication.cpp` | added | 6,613 B |
| `source/test/cpu/testWasmJitZeroRelocPublication.h` | added | 466 B |
| `source/test/testMain.cpp` | modified | +56 / -0 |

### 6. Zip-backed filesystem hot cache; wait-frame and causal-stage profiling

**Classification:** generic mechanism, Azrok-named flag

`BOXEDWINE_AZROK_HOT_WIS_CACHE` caches hot zip members (enabled in production); `..._CACHED_ZIP_UNLOCKED_READ` is off. `BOXEDWINE_AZROK_WAIT_FRAME_PROFILE` / `..._CAUSAL_STAGE_PROFILE` / `..._CAUSAL_WIS_HANDLE_JOIN` add counters around blocking reads and waits (`syscall.cpp` +1506 lines, `include/kwaitframeprofile.h`); they are **enabled in the production target**, i.e. the shipped runtime carries this instrumentation (VERIFIED). The mechanisms are game-agnostic; only the names are not. The profiling switches are among the 20 that the generic target leaves out.

| file | change | size of change |
|---|---|---|
| `include/kwaitframeprofile.h` | added | 3,285 B |
| `source/emulation/cpu/normal/normalCPU.cpp` | modified | +18 / -0 |
| `source/io/fsfilenode.cpp` | modified | +15 / -0 |
| `source/io/fsfilenode.h` | modified | +11 / -0 |
| `source/io/fsfileopennode.cpp` | modified | +11 / -0 |
| `source/io/fsopennode.cpp` | modified | +18 / -0 |
| `source/io/fszip.cpp` | modified | +41 / -0 |
| `source/io/fszip.h` | modified | +23 / -0 |
| `source/io/fszipnode.cpp` | modified | +139 / -0 |
| `source/io/fszipnode.h` | modified | +27 / -0 |
| `source/io/fszipopennode.cpp` | modified | +122 / -0 |
| `source/io/fszipopennode.h` | modified | +10 / -0 |
| `source/kernel/kfile.cpp` | modified | +15 / -0 |
| `source/kernel/kunixsocket.cpp` | modified | +7 / -0 |
| `source/kernel/syscall.cpp` | modified | +1506 / -2 |

### 7. FACE-WIS HLE and Acknex transform islands

**Classification:** Azrok-specific, safe to disable for other games

High-level emulation of specific guest routines of one title (FACE/WIS archive handling) keyed by a manifest of guest code addresses/hashes (`wasmJitAzrokFaceWisManifest.h`), plus Acknex-engine loop/transform fusion. Enabled in the production target via `..._AZROK_FACE_WIS_HLE*` flags; the `ACKNEX_*` fusion flags are off. **Useless for other games.** It stays in the tree on purpose, because it is part of the fork as it actually ran; the generic make target compiles it out (see the next section).

| file | change | size of change |
|---|---|---|
| `include/kprocess.h` | modified | +153 / -0 |
| `project/emscripten/boxedwine-face-wis-hle-worker-alert-pre.js` | added | 907 B |
| `source/emulation/cpu/wasm/wasmJitAcknexTransformIsland.cpp` | added | 61,889 B |
| `source/emulation/cpu/wasm/wasmJitAcknexTransformIsland.h` | added | 4,528 B |
| `source/emulation/cpu/wasm/wasmJitAzrokFaceWisHle.cpp` | added | 76,092 B |
| `source/emulation/cpu/wasm/wasmJitAzrokFaceWisHle.h` | added | 7,417 B |
| `source/emulation/cpu/wasm/wasmJitAzrokFaceWisManifest.h` | added | 20,635 B |
| `source/kernel/kmemory.cpp` | modified | +142 / -0 |
| `source/kernel/kprocess.cpp` | modified | +222 / -0 |
| `source/test/cpu/testWasmJitAcknexOuterLoop.cpp` | added | 19,799 B |
| `source/test/cpu/testWasmJitAcknexOuterLoop.h` | added | 134 B |
| `source/test/cpu/testWasmJitAcknexTransformIsland.cpp` | added | 21,114 B |
| `source/test/cpu/testWasmJitAcknexTransformIsland.h` | added | 203 B |

### 8. Korean IME / XIM / text input and touch pointer

**Classification:** generic

X server side XIM and preedit handling (`xkeyboard`, `x11common`, `xserver`), guest `libX11` source change (`tools/x11/X11.c`; the built `.so` is a release asset, not committed), browser text injection/paste and touch-pointer exports in `knativeinputSDL`. `BOXEDWINE_IME_RUNTIME_DEBUG` is enabled in production; `BOXEDWINE_D3D9WEB_INPUT_TRACE` is not. The `boxedwine_inject_text_utf8` / `boxedwine_paste_text_utf8` / `boxedwine_touch_pointer` entry points are `EMSCRIPTEN_KEEPALIVE` exports in every Emscripten build (VERIFIED: `knativeinputSDL.cpp`). Works for any Wine app that uses an X input method; verified only with CP949/Korean.

| file | change | size of change |
|---|---|---|
| `include/knativeinput_math.h` | added | 685 B |
| `platform/sdl/knativeinputSDL.cpp` | modified | +255 / -5 |
| `platform/sdl/knativeinputSDL.h` | modified | +2 / -0 |
| `source/x11/x11common.cpp` | modified | +233 / -16 |
| `source/x11/xkeyboard.cpp` | modified | +166 / -0 |
| `source/x11/xkeyboard.h` | modified | +18 / -0 |
| `source/x11/xserver.cpp` | modified | +64 / -0 |
| `source/x11/xserver.h` | modified | +59 / -0 |
| `tools/x11/X11.c` | modified | +113 / -2 |

### 9. Web audio (44.1 kHz option, prebuffer, user-gesture handling)

**Classification:** generic

Accepts 44.1 kHz as a browser `audioFreq` setting (the built-in default stays 11.025 kHz: `dspMaxOutputFreq = 11025`), scales the OSS fragment count with the ceiling to keep the ~186 ms queue window, shrinks the audio callback to 2048 frames (diff-verified in `devdsp.cpp`, `kdspaudio_math.h`, `startupArgs.cpp`), real-PCM prebuffer/queue in `kdspaudio.cpp`, and `BOXEDWINE_D3D9WEB_AUDIO_GESTURE_READY` (start real output after the page's explicit audio-enable click).

| file | change | size of change |
|---|---|---|
| `include/kdspaudio_math.h` | modified | +4 / -1 |
| `platform/sdl/kdspaudio.cpp` | modified | +106 / -16 |
| `project/emscripten/boxedwine-multithreaded-audio.js` | modified | +5 / -1 |
| `source/kernel/devs/devdsp.cpp` | modified | +7 / -2 |
| `source/sdl/startupArgs.cpp` | modified | +9 / -2 |

### 10. Emscripten sockets (browser WebSocket-backed TCP for the guest)

**Classification:** generic

`BOXEDWINE_EMSCRIPTEN_SOCKETS` (enabled in production): socket tracking/poll/recv changes so guest `connect/recv/poll` work over Emscripten's WebSocket sockets. Needs a WebSocket-to-TCP bridge on the server side (not part of this repo).

| file | change | size of change |
|---|---|---|
| `include/knativesocket.h` | modified | +1 / -0 |
| `source/kernel/knativesocket.cpp` | modified | +211 / -12 |
| `source/kernel/ksocket.cpp` | modified | +3 / -0 |
| `source/sdl/emscripten/mainloop.cpp` | modified | +3 / -0 |

### 11. Build system

**Classification:** generic mechanism, Azrok-named flag

`project/emscripten/makefile` grows from 18.6 KB to 272.9 KB: roughly one named target per experiment. The production target is `azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoopsFaceWisHleCwdIndependentEpoch` (-> `Build/FOTR10`). A generic target without the Azrok-specific FACE-WIS pieces is `...ImeDebugKeyTouch2MultiDirectLoops` (-> `Build/AzrokTouch2MultiDirectLoops1`).

| file | change | size of change |
|---|---|---|
| `project/emscripten/makefile` | modified | +1075 / -18 |

### 12. PGO quiesce lifecycle (test fixture)

**Classification:** generic

Quiesce domains (guest CPU, main loop, native socket) used by a test fixture (INFERRED: purpose read from the type names only). `BOXEDWINE_WASM_PGO_QUIESCE*` is off in production.

| file | change | size of change |
|---|---|---|
| `include/kthread.h` | modified | +5 / -0 |
| `source/emulation/cpu/normal/normalPlatformMultiThreaded.cpp` | modified | +67 / -1 |
| `source/emulation/cpu/wasm/wasmPgoQuiesce.cpp` | added | 13,343 B |
| `source/emulation/cpu/wasm/wasmPgoQuiesce.h` | added | 2,764 B |
| `source/test/testWasmPgoQuiesce.cpp` | added | 8,113 B |
| `source/test/testWasmPgoQuiesceCpuLifecycle.cpp` | added | 5,003 B |

### 13. New top-level directory `d3d9web/` (not a patch; all new)

**Classification:** generic (renderer, DLL, probe, recipes); the sample page and a few comments mention the original target game.

| path | what |
|---|---|
| `d3d9web/guest/d3d9_web.cpp`, `d3d9.def`, `build_d3d9.bat` | 32-bit guest Direct3D9 drop-in (FFP only) and its build script with 6 variants |
| `d3d9web/guest/smoke/` | INT 0x9A hello-world, COM smoke, capability bootstrap client, Winsock smoke tests |
| `d3d9web/web_backend/host/*.js`, `shaders/d3d9_ffp.{vert,frag}` | WebGL2 runtime (`d3d9web-runtime.js`, 168 KB) and FFP uber-shaders |
| `d3d9web/web_backend/host/README.md`, `shaders/PORTING_NOTES.md`, `UNIFORMS.md` | design notes written 2026-07-19 against an earlier renderer; may be stale vs the current runtime (NOT VERIFIED) |
| `d3d9web/shell/index_d3d9web_phase9.html` | example page that prepares the runtime then starts Boxedwine (BrowserFS script tag removed; see `THIRD_PARTY_NOTICES.md`) |
| `d3d9web/shell/assets/jszip.min.js` | third-party JSZip, verbatim, MIT option (see `THIRD_PARTY_NOTICES.md`) |
| `d3d9web/tools/d3d9_probe/` | Windows-only proxy that logs which D3D9 methods an EXE calls (use it to size the work for a new EXE) |
| `d3d9web/recipes/` | generic guest-fs / zip helper scripts |

## Wine 11.0 modifications (guest filesystem; not part of Boxedwine)

The guest filesystem ships a Wine 11.0 build whose source differs from the pristine `wine-11.0.tar.xz` in exactly **15 files** (+163 / -34 lines, VERIFIED by hashing every tarball member against the edited tree on the maintainer's machine). The diff is `d3d9web/wine/wine-11.0-changes.patch` (LGPL source for the modified modules). Purposes below are INFERRED from file names and the hunks I read; whether these 15 files are exactly the ones compiled into the shipped binaries is VERIFIED only for `ws2_32.dll.so` (its embedded build path points at this edited tree) and NOT VERIFIED for the rest.

| Wine file | change | probable purpose |
|---|---|---|
| `dlls/comctl32_v6/edit.c` | +10 / -5 | ANSI edit control code page (hard-coded CP949 macro) |
| `dlls/dsound/primary.c` | +4 / -1 | DirectSound primary buffer timing |
| `dlls/imm32/imm.c` | +17 / -10 | IME code-page handling |
| `dlls/kernelbase/sync.c` | +50 / -0 | synchronisation / wait behaviour |
| `dlls/ntdll/unix/socket.c` | +2 / -0 | socket behaviour |
| `dlls/user32/defwnd.c` | +1 / -1 | IME default window procedure |
| `dlls/user32/message.c` | +12 / -8 | ANSI/Unicode message conversion for IME |
| `dlls/user32/winproc.c` | +5 / -0 | window-proc conversion |
| `dlls/winegstreamer/main.c` | +6 / -1 | GStreamer audio (MP3/BGM) path |
| `dlls/winegstreamer/quartz_transform.c` | +18 / -0 | GStreamer audio path |
| `dlls/winegstreamer/wg_parser.c` | +11 / -1 | GStreamer parser |
| `dlls/winex11.drv/keyboard.c` | +5 / -0 | X11 keyboard / input method |
| `dlls/winex11.drv/xim.c` | +13 / -3 | XIM (Korean input method) client |
| `dlls/ws2_32/socket.c` | +2 / -4 | Winsock event handling |
| `server/sock.c` | +7 / -0 | wineserver socket events |

## Build flags introduced by the fork

The production make target defines 29 `-D` switches; 27 of them are introduced by this fork (VERIFIED by comparing each token against the upstream tree):

```
BOXEDWINE_AZROK_CAUSAL_STAGE_PROFILE
BOXEDWINE_AZROK_CAUSAL_WIS_HANDLE_JOIN
BOXEDWINE_AZROK_HOT_WIS_CACHE
BOXEDWINE_AZROK_WAIT_FRAME_PROFILE
BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH
BOXEDWINE_AZROK_WAIT_FRAME_SHARED_PUBLISH_LAZY
BOXEDWINE_D3D9WEB
BOXEDWINE_D3D9WEB_ASYNC_RING
BOXEDWINE_D3D9WEB_AUDIO_GESTURE_READY
BOXEDWINE_EMSCRIPTEN_SOCKETS
BOXEDWINE_IME_RUNTIME_DEBUG
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_CWD_DISCOVERY_V1
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_CWD_INDEPENDENT_EPOCH_V1
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_RETRY_V1
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_SCOPED_GENERATION_V1
BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE_STABILITY_WARMUP_V1
BOXEDWINE_WASM_JIT_FACE_CWD_DISCOVERY_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_CWD_INDEPENDENT_EPOCH_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_RETRY_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_SCOPED_GENERATION_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_CAUSAL_WIS_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_OWNER_READWAIT_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_OWNER_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_FACE_STABILITY_WARMUP_TRACE_DIAGNOSTIC
BOXEDWINE_WASM_JIT_MULTI_DIRECT_LOOPS
BOXEDWINE_WASM_JIT_OWNER_THREAD_IDENTITY
```

A further 60 fork-introduced `BOXEDWINE_*` switches exist in the sources and are **not defined by the production target's `-D` flags** (experiments, censuses, fixtures, oracles): `BOXEDWINE_AZROK_CACHED_ZIP_UNLOCKED_READ`, `BOXEDWINE_D3D9WEB_{ASYNC_GAP_PROFILE,AUDIO_TRACE,BRIDGE_PROFILE,INPUT_TRACE,INT_CENSUS}`, all `BOXEDWINE_WASM_JIT_*_{CENSUS,FIXTURE}`, `*_ORACLE`, `BOXEDWINE_WASM_JIT_{ACKNEX_*,INT9A_*,STICKY_GP_*,THREADED_*,INLINE_*,IF_*READ_CACHE,RUNTIME_*,ANCHORED_*}`, `BOXEDWINE_WASM_PGO_QUIESCE*`. The full machine-generated list is in the staging report workspace (`flag_table.json`).

## Added files not published (Azrok-specific tooling)

Excluded from this repo because nothing included imports them and they only work with one title's recorded profiles: 80 `project/emscripten/*.mjs` AOT stage/verify/fixture scripts, 2 `hqemu-tier2-*.json` maps, 4 `emcc-*wsl.sh`/`emxx-*wsl.sh` wrappers (hard-coded workstation paths), and build/test logs. Ask the maintainer if you need them.

