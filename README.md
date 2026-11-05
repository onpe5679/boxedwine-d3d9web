# boxedwine-d3d9web

[한국어](#한국어) | [English](#english)

> **첫 릴리스는 소스 전용입니다. / The first release is source only.**
> 컴파일된 런타임(`.wasm`/`.js`), 게스트 파일시스템 zip, JIT 프로필은 포함하지 않습니다. 바이너리는 범용 타깃으로 다시 빌드한 뒤(홈 경로 prefix-map 적용, `gulim.ttc` 제거, Wine LGPL 패치/소스 제공 준비) 별도의 GitHub Release로 올릴 예정입니다. 자세한 조건은 [RELEASE_ASSETS.md](RELEASE_ASSETS.md).
> No compiled runtime (`.wasm`/`.js`), no guest filesystem zip, no JIT profiles are included. Binaries will follow in a separate GitHub Release after a generic rebuild (home-path prefix map, `gulim.ttc` removed, Wine LGPL patch/source offer prepared). Conditions: [RELEASE_ASSETS.md](RELEASE_ASSETS.md).

---

## 한국어

### 무엇인가

2000년대 Windows용 Direct3D 9 게임(EXE)을 **수정 없이 브라우저에서 실행**하기 위한 두 가지를 담은 저장소입니다.

1. [BoxedWine](https://github.com/danoon2/Boxedwine)(WebAssembly로 x86 Windows 프로그램을 실행하는 에뮬레이터, GPL-2.0)의 포크. 업스트림 커밋 `35dc3f0`(2026-07-20)에서 갈라졌고, 변경 내역은 [PATCHES.md](PATCHES.md)에 파일 단위로 정리했습니다.
2. `d3d9web/` : 게임 폴더에 `d3d9.dll`로 넣는 드롭인 DLL(`d3d9web/guest/d3d9_web.cpp`)과, 그 호출을 받아 WebGL2로 그리는 호스트 렌더러(`d3d9web/web_backend/`).

게임 파일, 게임 서버, 게임 데이터는 이 저장소에 없습니다.

### 구조

```
게임 EXE (x86, 무수정)
   |  Direct3D9 COM 호출
   v
Wine 11  (BoxedWine 게스트 파일시스템: Tiny Core Linux)
   |  DllOverrides: d3d9=native
   v
d3d9.dll   <- d3d9web/guest/d3d9_web.cpp  (호출을 순서 있는 명령 배치로 직렬화)
   |  INT 0x9A  (인덱스 0xd3900000 대역)
   v
BoxedWine 호스트 (WASM)   source/opengl/d3d9web.cpp   (비동기 링, Present에서 배치 완료)
   |  globalThis.D3D9WebRuntime
   v
WebGL2   d3d9web/web_backend/host/d3d9web-runtime.js + shaders/d3d9_ffp.{vert,frag}
   v
<canvas>
```

### 현재 상태

- **VERIFIED(직접 확인):** 게스트 DLL 빌드(`build_d3d9.bat`의 6개 변형 모두 컴파일됨). 현재 소스에서 나온 production 변형이 실제 배포된 DLL과 `.text/.data/.reloc`가 바이트 단위로 같음. 배포된 웹 런타임(`Build/FOTR10` 타깃)의 산출물이 배포본과 바이트 단위로 같음(개발 PC에서 확인).
- **개발·검증은 타이틀 1개**(서비스가 종료된 2008년 한국 온라인 게임, D3D9 고정 함수 파이프라인)에서만 했습니다. 다른 타이틀에서의 동작은 **NOT VERIFIED**입니다.
- **이번 릴리스를 위해 웹 런타임 빌드 절차를 다시 실행하지 않았습니다**(아래 "소스에서 빌드하기"). 범용 타깃을 이 저장소 상태에서 처음부터 빌드해 보지 않았고, 브라우저에서 실행해 보지도 않았습니다(**NOT VERIFIED**).

### 한계

**성능/구조 (프로젝트 측정, 이번 릴리스에서 재측정하지 않음)**

- 에뮬레이트되는 게스트 x86 연산은 **하나의 직렬 CPU 스트림**으로 실행됩니다. 일을 Web Worker로 옮겨도 빨라지지 않았습니다. 성능은 WebAssembly 위의 x86 에뮬레이션 수준이며, 포크의 JIT 확장은 한 타이틀 기준으로 튜닝되어 있습니다.
- 업스트림도 웹 빌드의 소리가 완벽하지 않다고 밝히고 있습니다. 이 포크는 오디오 prebuffer/주파수 옵션을 일부 고쳤지만 끊김이 없다는 보장은 없습니다.

**코드로 확인된 한계 (VERIFIED)**

- **고정 함수 파이프라인(FFP)만 지원합니다. 셰이더는 없습니다.** `SetVertexShader`/`SetPixelShader`에 null이 아닌 셰이더를 넘기면 `D3DERR_INVALIDCALL`을 반환합니다. `GetDeviceCaps`는 VS/PS 3.0을 보고하지만, 이는 특정 게임의 버전 검사를 통과시키기 위한 값이며 셰이더를 실행한다는 뜻이 아닙니다.
- 어댑터 1개, 스왑체인 1개, 전역 디바이스 1개만 다룹니다.
- D3D9 명령은 하나의 순서 있는 스트림으로 처리되고 Present에서 완료됩니다. 여러 스레드에서의 렌더링 호출 동작은 NOT VERIFIED.
- 32비트 x86 게스트만 대상입니다. D3D8/D3D10/D3D11/DirectDraw 변환은 이 저장소에 없습니다.
- 멀티스레드 빌드는 `SharedArrayBuffer`가 필요하므로 COOP/COEP 헤더를 보내는 서버가 필요합니다(`project/emscripten/server.mjs`가 보냅니다).

### Azrok 전용 코드 (Azrok-specific, safe to disable for other games)

이 포크는 실제로 돌았던 모습 그대로를 담기 때문에, 개발 대상이던 한 게임에만 의미가 있는 코드가 들어 있습니다. FACE-WIS HLE(`wasmJitAzrokFaceWisHle.*`, `wasmJitAzrokFaceWisManifest.h`), Acknex transform island(`wasmJitAcknexTransformIsland.*`), `AZROK_*` 계측 플래그가 그것입니다. **전부 Azrok 전용이며 다른 게임에서는 꺼도 됩니다.** `-D` 플래그로만 켜지는 `#if` 코드입니다.

- **끄는 방법:** 범용 타깃 `azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoops`를 쓰면 됩니다. 이 타깃은 `-D` 스위치 9개만 설정하고, 배포 타깃(`...FaceWisHleCwdIndependentEpoch`)이 추가로 설정하는 20개(`BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_*`, `BOXEDWINE_WASM_JIT_FACE_*_TRACE_DIAGNOSTIC`, `BOXEDWINE_WASM_JIT_OWNER_THREAD_IDENTITY`, `BOXEDWINE_AZROK_WAIT_FRAME_*`, `BOXEDWINE_AZROK_CAUSAL_*`)는 설정하지 않습니다(두 타깃의 플래그 목록을 비교해 확인함, VERIFIED). 전체 목록과 파일은 [PATCHES.md](PATCHES.md).
- 주의: `BOXEDWINE_AZROK_HOT_WIS_CACHE`는 이름만 Azrok이고 범용 zip 캐시라 범용 타깃에서도 켜져 있습니다.
- "꺼도 안전하다"는 것은 코드가 `#if`로 분리되어 있고 해당 플래그가 없다는 사실에 근거합니다. 그 결과물을 실제로 빌드해 실행해 본 것은 아닙니다(NOT VERIFIED).

### 저장소 구성

| 경로 | 내용 |
|---|---|
| `source/ include/ platform/ project/ lib/ tools/ docs/` | BoxedWine 포크 (업스트림 구조 그대로, `lib/` 전체 유지) |
| `project/emscripten/` | 웹 빌드(makefile, 셸 JS, 서버) |
| `d3d9web/guest/` | 게스트 `d3d9.dll` 소스, 빌드 스크립트, 스모크 테스트 |
| `d3d9web/web_backend/` | WebGL2 런타임과 FFP 셰이더 |
| `d3d9web/shell/` | 런타임을 준비하고 BoxedWine을 시작하는 예제 페이지, JSZip |
| `d3d9web/tools/d3d9_probe/` | EXE가 어떤 D3D9 메서드를 부르는지 기록하는 Windows용 프록시 |
| `d3d9web/wine/` | 게스트 Wine 11.0의 수정분(15개 파일) 패치 |
| `d3d9web/recipes/` | 게스트 파일시스템/zip 보조 스크립트 |
| `PATCHES.md` | 업스트림 대비 변경 목록(기능별, 범용/Azrok 전용 구분) |
| `RELEASE_ASSETS.md` | 이후 Release로 올릴 바이너리의 조건과 라이선스 의무 |
| `THIRD_PARTY_NOTICES.md` | 서드파티 파일(JSZip 등)과 `lib/`의 라이선스 목록 |

### 소스에서 빌드하기

**전제 조건**

| 항목 | 값 |
|---|---|
| 업스트림 기준 | [danoon2/Boxedwine](https://github.com/danoon2/Boxedwine) 커밋 `35dc3f0875a2d954b2fab22b9f357d9bc8ab7e69` (2026-07-20). 변경분을 직접 비교하려면 이 커밋을 체크아웃하십시오. |
| Emscripten | **emsdk 6.0.3** (개발 PC의 emsdk 버전 파일 값: VERIFIED. 이 버전으로 배포 빌드를 만들었다는 것은 INFERRED). emsdk 내장 Node는 22.16.0. |
| make | GNU make. makefile이 `$(shell find ... \| grep ...)`와 `mkdir -p`를 호출하므로 POSIX 도구가 필요합니다(VERIFIED). Windows에서는 Git Bash 같은 셸 + GNU make (개발 PC는 이 방식, INFERRED). |
| 네트워크 | 첫 빌드에서 Emscripten의 SDL2/zlib 포트(`-s USE_SDL=2 -s USE_ZLIB=1`가 makefile에 있음, VERIFIED)를 내려받습니다(Emscripten의 일반 동작, INFERRED). |
| 메모리 | 결과 wasm은 초기 메모리 1 GiB, 최대 3 GiB로 링크됩니다(VERIFIED). |
| 게스트 DLL | Windows + Visual Studio Build Tools(x86 도구 집합) |

**1) 웹 런타임 (범용 타깃)**

```
# emsdk 6.0.3 환경을 활성화한 셸에서
cd project/emscripten
make azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoops
# -> Build/AzrokTouch2MultiDirectLoops1/
#    boxedwine.upstreamjit64fn6inlineabi2korean6chat1touch2multidirectloops1.{html,js,wasm}
#    + boxedwine.css, boxedwine-shell.js
```

**이 절차는 이번 릴리스를 위해 다시 실행하지 않았습니다.** makefile 텍스트(타깃 정의, 플래그, 링크 옵션)를 읽어 적은 것입니다. 실제 배포된 빌드는 `...FaceWisHleCwdIndependentEpoch` 타깃(`Build/FOTR10`)이었고, 그 산출물이 배포본과 바이트 단위로 같음을 개발 PC에서 확인했습니다. 그 빌드는 Azrok 전용 코드를 켠 것이라 다른 게임에는 위의 범용 타깃을 쓰십시오.

**2) 게스트 d3d9.dll** (VERIFIED: 아래 변형이 모두 컴파일됨)

```
d3d9web\guest\build_d3d9.bat            :: production 변형 (D3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1)
d3d9web\guest\build_d3d9.bat plain      :: 기능 플래그 없음
```

산출물 `out\d3d9_web.<변형>.dll`을 게임 EXE 옆에 `d3d9.dll`로 둡니다. 다른 변형(guestbatch, recompose, colorwritetrace, slottrace)은 같은 `.cpp`의 `#if` 플래그입니다. 두 trace 변형은 진단용이며 배포하지 마십시오.

**3) 게스트 파일시스템:** 이번 릴리스는 배포하지 않습니다. 업스트림의 [docs/How-To-Create-FileSystem-For-Web.md](docs/How-To-Create-FileSystem-For-Web.md)를 따라 직접 만들어야 합니다. Wine 11.0에 한글 입력(IME/XIM) 등을 위해 가한 수정은 `d3d9web/wine/wine-11.0-changes.patch`입니다. 업스트림이 제공하는 다른 파일시스템 압축본이 이 포크와 호환되는지는 NOT VERIFIED.

**4) 서버:** `node project/emscripten/server.mjs <포트>` (COOP/COEP/CORP 헤더 포함).

### 다른 EXE 시도해 보기

1. **호출 표면 측정.** Windows에서 `d3d9web/tools/d3d9_probe`를 빌드해(산출물 `d3d9_flushprobe.dll`) `d3d9.dll`이라는 이름으로 EXE 옆에 두고 한 번 플레이합니다. `d3d9_probe.log`에 호출된 IDirect3D9/IDirect3DDevice9 메서드와 인자 분포가 남습니다.
2. **셰이더를 쓰면 여기서 중단합니다.** `CreateVertexShader`/`CreatePixelShader`나 null이 아닌 `SetPixelShader`가 보이면 이 렌더러로는 그려지지 않습니다.
3. **게스트 DLL 빌드.** 위 2)번대로 빌드합니다. 시작 단계에서 막히면 `d3d9_web.cpp`의 `CheckDeviceFormat`/`GetDeviceCaps`가 돌려주는 값이 게임의 기대와 맞는지 먼저 보십시오.
4. **앱 zip 만들기.** EXE, 게임 파일, `d3d9.dll`, 그리고 Wine에서 `d3d9`를 native로 지정한 뒤 EXE를 실행하는 `.bat`을 한 zip에 넣습니다. `.bat`의 예: `d3d9web/guest/smoke/run_phase4_browser.bat` (`reg add "HKCU\Software\Wine\DllOverrides" /v d3d9 /d native /f`). zip 구성 방식은 예제 페이지 기준으로 추정한 것입니다(INFERRED).
5. **페이지.** `d3d9web/shell/index_d3d9web_phase9.html`을 복사해 `app=`(앱 zip 이름), `p=`(실행할 `.bat`), `root=`(게스트 파일시스템 zip 이름)와 `boxedwine*.js` 경로를 고칩니다. 이 페이지는 `prepareD3D9WebRuntime()`으로 `globalThis.D3D9WebRuntime`을 먼저 만든 뒤 BoxedWine을 시작합니다. 예제는 `assets/jszip.min.js`(포함됨, [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md))를 쓰며 BrowserFS는 쓰지 않습니다. 이 예제 페이지가 현재 포크 셸과 함께 동작하는지는 이번 릴리스에서 확인하지 않았습니다(NOT VERIFIED).

게임 파일(`Data`, `.wmb`, `.mdl`, `.pcx`, `.wis`, `.tga`, `.ogg`, `.mp3`, `.wmv` 등)과 앱/파일시스템 zip은 절대 커밋하지 마십시오(`.gitignore`에 넣어 두었습니다).

### 라이선스와 크레딧

- 이 저장소는 **GPL-2.0**입니다(`LICENSE`, 업스트림 `license.txt`와 동일).
- **BoxedWine** - James Bryant([danoon2](https://github.com/danoon2)) 외 BoxedWine Team, GPL-2.0. 이 저장소는 그 소스를 포함하는 포크입니다.
- **Wine** - WineHQ, LGPL-2.1 이상. 게스트에서 실행되는 Wine 11.0(수정 15개 파일은 `d3d9web/wine/`).
- **Tiny Core Linux** - 게스트 파일시스템의 기반(여러 오픈소스 라이선스). [RELEASE_ASSETS.md](RELEASE_ASSETS.md) 참고.
- `lib/` 아래의 SDL2, asmjit, softfloat, zlib, Dear ImGui 등과 JSZip은 각자의 라이선스를 따릅니다([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
- 아이디어와 출발점: <https://velog.io/@aespa/d3d9-webgl>

이 프로젝트는 서비스가 종료된 게임을 보존하고 상호운용성을 확보하려는 목적입니다. 운영 중인 상용 서비스를 대상으로 하지 않습니다.

---

## English

### What this is

A repository for running **unmodified 2000s Windows Direct3D 9 games in a browser**. It contains two things:

1. A fork of [BoxedWine](https://github.com/danoon2/Boxedwine) (a WebAssembly x86 Windows emulator, GPL-2.0), branched from upstream commit `35dc3f0` (2026-07-20). Every change is listed per file in [PATCHES.md](PATCHES.md).
2. `d3d9web/`: a drop-in `d3d9.dll` for the game folder (`d3d9web/guest/d3d9_web.cpp`) and the host-side renderer that receives its calls and draws with WebGL2 (`d3d9web/web_backend/`).

No game files, game server or game data are included.

### Architecture

```
game EXE (x86, unmodified)
   |  Direct3D9 COM calls
   v
Wine 11  (BoxedWine guest filesystem: Tiny Core Linux)
   |  DllOverrides: d3d9=native
   v
d3d9.dll   <- d3d9web/guest/d3d9_web.cpp  (serialises calls into ordered command batches)
   |  INT 0x9A  (indices in the 0xd3900000 range)
   v
BoxedWine host (WASM)   source/opengl/d3d9web.cpp   (async ring; batch completes at Present)
   |  globalThis.D3D9WebRuntime
   v
WebGL2   d3d9web/web_backend/host/d3d9web-runtime.js + shaders/d3d9_ffp.{vert,frag}
   v
<canvas>
```

### Status

- **VERIFIED (checked directly):** the guest DLL builds (all 6 variants of `build_d3d9.bat` compile). The production variant built from the current source matches the deployed DLL byte for byte in `.text`, `.data` and `.reloc`. The output of the deployed web runtime's make target (`Build/FOTR10`) is byte-identical to the deployed files (checked on the maintainer's machine).
- **Developed and checked against one title only** (a 2008 Korean online game whose service has ended, D3D9 fixed-function). Behaviour with other titles is **NOT VERIFIED**.
- **The web-runtime build steps were not re-run for this release** (see "Building from source"). The generic target has not been built from a clean checkout of this repository and has not been run in a browser (**NOT VERIFIED**).

### Limits

**Performance / architecture (project measurements, not re-measured for this release)**

- The emulated guest x86 work runs as **one serial CPU stream**; moving work to Web Workers did not make it faster. Performance is that of x86 emulation on WebAssembly, and the fork's JIT extensions were tuned against a single title.
- Upstream itself says web audio is not perfect. This fork fixes some prebuffer/frequency issues but makes no guarantee of glitch-free audio.

**Code-verified limits (VERIFIED)**

- **Fixed-function pipeline only. No shaders.** A non-null `SetVertexShader` / `SetPixelShader` returns `D3DERR_INVALIDCALL`. `GetDeviceCaps` reports VS/PS 3.0 only so that one game's version check passes; it does not mean shaders run.
- One adapter, one swap chain, one global device.
- D3D9 commands are handled as one ordered stream that completes at Present. Behaviour when several guest threads render is NOT VERIFIED.
- 32-bit x86 guests only. There is no D3D8/D3D10/D3D11/DirectDraw translation here.
- The multi-threaded build needs `SharedArrayBuffer`, hence a server that sends COOP/COEP headers (`project/emscripten/server.mjs` does).

### Azrok-specific code (safe to disable for other games)

The fork contains the code as it actually ran, including parts that only make sense for the one game it was developed against: FACE-WIS HLE (`wasmJitAzrokFaceWisHle.*`, `wasmJitAzrokFaceWisManifest.h`), the Acknex transform island (`wasmJitAcknexTransformIsland.*`) and the `AZROK_*` instrumentation flags. **All of it is Azrok-specific and safe to disable for other games.** It is `#if` code that only `-D` flags switch on.

- **How to leave it out:** use the generic target `azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoops`. It sets only 9 `-D` switches and none of the 20 that the deployed target (`...FaceWisHleCwdIndependentEpoch`) adds (`BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_*`, `BOXEDWINE_WASM_JIT_FACE_*_TRACE_DIAGNOSTIC`, `BOXEDWINE_WASM_JIT_OWNER_THREAD_IDENTITY`, `BOXEDWINE_AZROK_WAIT_FRAME_*`, `BOXEDWINE_AZROK_CAUSAL_*`); VERIFIED by diffing the two targets' flag lists. Full list and files: [PATCHES.md](PATCHES.md).
- Note: `BOXEDWINE_AZROK_HOT_WIS_CACHE` is Azrok-named but is a generic zip cache and stays on in the generic target.
- "Safe to disable" rests on the code being `#if`-guarded and the flags being absent; I did not build and run that configuration (NOT VERIFIED).

### Layout

| Path | Contents |
|---|---|
| `source/ include/ platform/ project/ lib/ tools/ docs/` | BoxedWine fork (upstream layout, full `lib/` kept) |
| `project/emscripten/` | web build (makefile, shell JS, server) |
| `d3d9web/guest/` | guest `d3d9.dll` source, build script, smoke tests |
| `d3d9web/web_backend/` | WebGL2 runtime and FFP shaders |
| `d3d9web/shell/` | example page that prepares the runtime and starts BoxedWine; JSZip |
| `d3d9web/tools/d3d9_probe/` | Windows proxy that logs which D3D9 methods an EXE calls |
| `d3d9web/wine/` | patch for the guest Wine 11.0 (15 modified files) |
| `d3d9web/recipes/` | guest-filesystem / zip helper scripts |
| `PATCHES.md` | per-feature delta against upstream (generic vs Azrok-specific) |
| `RELEASE_ASSETS.md` | conditions and licence duties for the binaries of a later Release |
| `THIRD_PARTY_NOTICES.md` | third-party files (JSZip, ...) and the licences under `lib/` |

### Building from source

**Prerequisites**

| Item | Value |
|---|---|
| Upstream base | [danoon2/Boxedwine](https://github.com/danoon2/Boxedwine) commit `35dc3f0875a2d954b2fab22b9f357d9bc8ab7e69` (2026-07-20). Check it out if you want to diff against this fork yourself. |
| Emscripten | **emsdk 6.0.3** (the version file on the maintainer's machine says so: VERIFIED; that the deployed build used it is INFERRED). The bundled Node is 22.16.0. |
| make | GNU make. The makefile runs `$(shell find ... \| grep ...)` and `mkdir -p`, so POSIX tools are required (VERIFIED). On Windows, use a POSIX shell such as Git Bash with GNU make (what the maintainer did, INFERRED). |
| Network | The first build downloads Emscripten's SDL2/zlib ports (`-s USE_SDL=2 -s USE_ZLIB=1` are in the makefile, VERIFIED; the download is standard Emscripten behaviour, INFERRED). |
| Memory | The resulting wasm links with 1 GiB initial and 3 GiB maximum memory (VERIFIED). |
| Guest DLL | Windows + Visual Studio Build Tools (x86 toolset) |

**1) Web runtime (generic target)**

```
# in a shell with the emsdk 6.0.3 environment active
cd project/emscripten
make azrokMultiThreadedJitAudioO3AsyncRingFn6InlineAbiImeDebugKeyTouch2MultiDirectLoops
# -> Build/AzrokTouch2MultiDirectLoops1/
#    boxedwine.upstreamjit64fn6inlineabi2korean6chat1touch2multidirectloops1.{html,js,wasm}
#    + boxedwine.css, boxedwine-shell.js
```

**These steps were not re-run for this release.** They were written from reading the makefile (target definition, flags, link options). The build that was actually deployed used the `...FaceWisHleCwdIndependentEpoch` target (`Build/FOTR10`); its output was checked to be byte-identical to the deployed files on the maintainer's machine. That build has the Azrok-specific code switched on, so for other games use the generic target above.

**2) Guest d3d9.dll** (VERIFIED: all variants below compile)

```
d3d9web\guest\build_d3d9.bat            :: production variant (D3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1)
d3d9web\guest\build_d3d9.bat plain      :: no feature flags
```

Copy `out\d3d9_web.<variant>.dll` next to the game EXE as `d3d9.dll`. The other variants (guestbatch, recompose, colorwritetrace, slottrace) are `#if` flags in the same `.cpp`; the two trace variants are diagnostics and must not be shipped.

**3) Guest filesystem:** not distributed in this release. Build it yourself following upstream's [docs/How-To-Create-FileSystem-For-Web.md](docs/How-To-Create-FileSystem-For-Web.md). The changes made to Wine 11.0 (Korean IME/XIM and others) are in `d3d9web/wine/wine-11.0-changes.patch`. Whether other filesystem archives published by upstream work with this fork is NOT VERIFIED.

**4) Server:** `node project/emscripten/server.mjs <port>` (sends COOP/COEP/CORP headers).

### Trying another EXE

1. **Measure the call surface.** On Windows, build `d3d9web/tools/d3d9_probe` (output `d3d9_flushprobe.dll`), drop it next to the EXE renamed to `d3d9.dll`, and play once. `d3d9_probe.log` lists the IDirect3D9 / IDirect3DDevice9 methods called, with argument distributions.
2. **Stop if it uses shaders.** If you see `CreateVertexShader`/`CreatePixelShader` or a non-null `SetPixelShader`, this renderer cannot draw the game.
3. **Build the guest DLL** as in step 2 above. If startup fails early, first check that what `CheckDeviceFormat` / `GetDeviceCaps` in `d3d9_web.cpp` return matches what the game expects.
4. **Make an app zip** containing the EXE, the game files, `d3d9.dll`, and a `.bat` that sets the `d3d9` DLL override to native and then runs the EXE (example: `d3d9web/guest/smoke/run_phase4_browser.bat`, which runs `reg add "HKCU\Software\Wine\DllOverrides" /v d3d9 /d native /f`). The zip layout is inferred from the example page (INFERRED).
5. **Page.** Copy `d3d9web/shell/index_d3d9web_phase9.html` and adjust `app=` (app zip name), `p=` (the `.bat` to run), `root=` (guest filesystem zip name) and the `boxedwine*.js` path. The page first creates `globalThis.D3D9WebRuntime` via `prepareD3D9WebRuntime()` and then starts BoxedWine. The example uses `assets/jszip.min.js` (included, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) and does not use BrowserFS. I have not checked that this example page works with the current fork shell (NOT VERIFIED).

Never commit game files (`Data`, `.wmb`, `.mdl`, `.pcx`, `.wis`, `.tga`, `.ogg`, `.mp3`, `.wmv`, ...) or app/filesystem zips (they are in `.gitignore`).

### Licence and credits

- This repository is **GPL-2.0** (`LICENSE`, identical to upstream `license.txt`).
- **BoxedWine** - James Bryant ([danoon2](https://github.com/danoon2)) and the BoxedWine Team, GPL-2.0. This repository is a fork that contains its source.
- **Wine** - WineHQ, LGPL-2.1 or later. Wine 11.0 runs in the guest (15 modified files are in `d3d9web/wine/`).
- **Tiny Core Linux** - base of the guest filesystem (various open-source licences); see [RELEASE_ASSETS.md](RELEASE_ASSETS.md).
- SDL2, asmjit, softfloat, zlib, Dear ImGui and others under `lib/`, and JSZip, keep their own licences ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
- Inspiration and starting point: <https://velog.io/@aespa/d3d9-webgl>

The project exists to preserve a game whose service has ended and to keep it interoperable. It does not target any live commercial service.
