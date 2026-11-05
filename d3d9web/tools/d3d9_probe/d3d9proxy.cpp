// d3d9.dll 로깅 프록시 — 아즈로크가 실제로 쓰는 D3D9 API 표면을 전수 측정한다.
//
// 왜 이게 가능한가:
//   acknex.dll 은 d3d9.dll 에서 `Direct3DCreate9` **하나만** 임포트한다(정적 임포트 1건).
//   그 뒤는 전부 반환된 COM 인터페이스의 vtable 호출이다.
//   따라서 이 DLL 을 게임 폴더에 두면(로컬 디렉터리 우선 탐색) 렌더링 전체를 가로챌 수 있다.
//
// 방식:
//   객체를 감싸지 않고 **vtable 을 제자리에서 패치**한다. vtable 은 클래스 단위로 공유되므로
//   한 번만 패치하면 모든 인스턴스에 적용되고, COM 수명주기(QueryInterface/AddRef/Release)를
//   흉내낼 필요가 없다 — 래핑 방식의 가장 흔한 버그원을 통째로 회피한다.
//
//   각 슬롯은 32바이트 썽크로 대체된다:
//       pushfd / pushad / push <id> / call LogCall / add esp,4 / popad / popfd / jmp [원본]
//   `jmp` 시점의 스택과 레지스터가 진입 시와 **완전히 동일**하므로 thiscall/stdcall 어느 규약이든
//   인자 전달이 깨지지 않는다.
//
// 출력: 게임 폴더에 d3d9_probe.log — 호출된 메서드와 호출 횟수, 그 아래에 인자 분포
//       (SetRenderState/SetTextureStageState/SetSamplerState/SetFVF/CreateTexture/
//        DrawPrimitiveUP/DrawIndexedPrimitive/SetTransform 의 실제 사용 조합·등장횟수).
//       인자는 상태/드로우 슬롯만 전용 C 훅으로 덮어써서 잡는다(핫패스는 해시셋에
//       고정 크기로만 기록 — 문자열 포맷팅·힙 할당 없음, 출력 시점에만 사람이 읽을 형태로 뽑는다).
//
//       비정상 종료 대비: DLL_PROCESS_DETACH 최종 덤프와 별개로, Present 훅이 FLUSH_INTERVAL_FRAMES
//       프레임(기본 600)마다 같은 파일을 덮어쓴다. 파일 첫 줄 `# flush: frame=N status=...` 로
//       부분 데이터인지 정상 종료된 완전한 데이터인지 구분한다. 기존 측정 항목의 형식은 그대로다.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HMODULE g_real = NULL;
static CRITICAL_SECTION g_cs;     // vtable 패치용 (드묾: 객체 생성 시에만)
static CRITICAL_SECTION g_argcs;  // 인자 조합 테이블용 (핫패스: 프레임당 수백~수천회)
static CRITICAL_SECTION g_flushcs;// 로그 덤프(파일 I/O) 직렬화 전용 — g_argcs 와 별개, 핫패스 아님
static bool g_init = false;

// ---------------------------------------------------------------- 비정상종료 대비 주기적 flush
// Present 는 세션당 약 2,000회 호출된다. 카운터 증가는 InterlockedIncrement 로 락 없이 처리하고,
// 실제 로그 덤프(파일 I/O)만 FLUSH_INTERVAL_FRAMES 프레임마다 g_flushcs 임계구역에서 수행한다.
// 덮어쓰기 방식이므로 파일이 커지지 않고, 항상 "마지막 flush 시점까지의" 상태가 디스크에 있다.
// -> 강제종료/크래시로 DLL_PROCESS_DETACH 가 못 불려도 최근 flush 분까지는 보존된다.
static volatile LONG g_frameCount = 0;

// One-frame native DrawPrimitiveUP capture. A file named
// d3d9_dpup_arm.flag beside Azrok.exe arms the next complete frame.
static CRITICAL_SECTION g_capturecs;
static bool g_dpupCaptureActive = false;
static DWORD g_currentFvf = 0;
static DWORD g_worldRaw[16] = {0};
static DWORD g_viewRaw[16] = {0};
static DWORD g_projectionRaw[16] = {0};
struct CaptureViewport { DWORD x, y, width, height, minZ, maxZ; };
static CaptureViewport g_viewportRaw = {0, 0, 0, 0, 0, 0x3F800000u};
static DWORD g_tssRaw[2][33] = {{0}};
static DWORD g_samplerRaw[2][14] = {{0}};
static void* g_textureRaw[2] = {NULL, NULL};
static DWORD g_renderStateRaw[256] = {0};
static const DWORD g_captureRenderStateIds[] = {
    7, 14, 15, 19, 20, 22, 23, 24, 25, 27, 168, 174
};
static const int CAPTURE_RS_COUNT = sizeof(g_captureRenderStateIds) / sizeof(g_captureRenderStateIds[0]);

static const int DPUP_CAPTURE_MAX = 256;
static const int DPUP_VERTEX_BYTES_MAX = 364;

struct DpupCaptureRecord {
    LONG frame;
    DWORD ordinal, threadId, primType, primCount, stride, fvf, vertexBytes, byteHash;
    DWORD world[16];
    DWORD view[16];
    DWORD projection[16];
    CaptureViewport viewport;
    DWORD tss[2][33];
    DWORD sampler[2][14];
    DWORD renderState[12];
    DWORD texture[2];
    BYTE vertices[DPUP_VERTEX_BYTES_MAX];
};

static DpupCaptureRecord g_dpupRecords[DPUP_CAPTURE_MAX];
static DWORD g_dpupRecordCount = 0;
static DWORD g_allDpupOrdinal = 0;
static DWORD g_dpupDropped = 0;

static void BuildExeSiblingPath(char* out, const char* leaf)
{
    GetModuleFileNameA(NULL, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    if (slash) strcpy(slash + 1, leaf); else strcpy(out, leaf);
}

static DWORD Fnv1a32(const BYTE* bytes, DWORD size)
{
    DWORD h = 2166136261u;
    for (DWORD i = 0; i < size; ++i) { h ^= bytes[i]; h *= 16777619u; }
    return h;
}

struct CaptureSurfaceDesc {
    DWORD format, type, usage, pool, multiSampleType, multiSampleQuality, width, height;
};
struct CaptureLockedRect { INT pitch; void* bits; };
static void DumpCapturedTexture(FILE* f, void* texture);

static void DumpDpupCapture(void)
{
    char path[MAX_PATH];
    BuildExeSiblingPath(path, "d3d9_dpup_capture.log");
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# native DrawPrimitiveUP one-frame capture\n# records=%lu dropped=%lu\n",
            g_dpupRecordCount, g_dpupDropped);
    for (DWORD i = 0; i < g_dpupRecordCount; ++i) {
        const DpupCaptureRecord& r = g_dpupRecords[i];
        fprintf(f, "REC frame=%ld ordinal=%lu tid=%lu prim=%lu count=%lu stride=%lu fvf=0x%08lX bytes=%lu fnv1a=0x%08lX tex0=0x%08lX tex1=0x%08lX\n",
                r.frame, r.ordinal, r.threadId, r.primType, r.primCount, r.stride, r.fvf,
                r.vertexBytes, r.byteHash, r.texture[0], r.texture[1]);
        fprintf(f, "WORLD");
        for (int j = 0; j < 16; ++j) fprintf(f, " %08lX", r.world[j]);
        fprintf(f, "\nVIEW");
        for (int j = 0; j < 16; ++j) fprintf(f, " %08lX", r.view[j]);
        fprintf(f, "\nPROJECTION");
        for (int j = 0; j < 16; ++j) fprintf(f, " %08lX", r.projection[j]);
        fprintf(f, "\nVIEWPORT %lu %lu %lu %lu %08lX %08lX\n",
                r.viewport.x, r.viewport.y, r.viewport.width, r.viewport.height,
                r.viewport.minZ, r.viewport.maxZ);
        for (int stage = 0; stage < 2; ++stage) {
            fprintf(f, "TSS%d", stage);
            for (int j = 0; j < 33; ++j) fprintf(f, " %08lX", r.tss[stage][j]);
            fprintf(f, "\nSS%d", stage);
            for (int j = 0; j < 14; ++j) fprintf(f, " %08lX", r.sampler[stage][j]);
            fprintf(f, "\n");
        }
        fprintf(f, "RS");
        for (int j = 0; j < CAPTURE_RS_COUNT; ++j) fprintf(f, " %08lX", r.renderState[j]);
        fprintf(f, "\n");
        fprintf(f, "VERTEX");
        for (DWORD j = 0; j < r.vertexBytes; ++j) fprintf(f, " %02X", r.vertices[j]);
        fprintf(f, "\n");
    }
    fprintf(f, "# captured texture contents (tight rows, all mip levels)\n");
    void* seenTextures[32] = {0};
    DWORD seenCount = 0;
    for (DWORD i = 0; i < g_dpupRecordCount && seenCount < 32; ++i) {
        if (g_dpupRecords[i].ordinal < 22 || g_dpupRecords[i].ordinal > 29) continue;
        for (int stage = 0; stage < 2; ++stage) {
            void* texture = (void*)(ULONG_PTR)g_dpupRecords[i].texture[stage];
            bool seen = !texture;
            for (DWORD j = 0; j < seenCount; ++j) if (seenTextures[j] == texture) seen = true;
            if (!seen) seenTextures[seenCount++] = texture;
        }
    }
    for (DWORD i = 0; i < seenCount; ++i) DumpCapturedTexture(f, seenTextures[i]);
    fclose(f);
}
static const LONG FLUSH_INTERVAL_FRAMES = 600;   // ~10초 @60fps, 세션당 flush 3~4회

// ---------------------------------------------------------------- 메서드 이름표
static const char* kD3D9[] = {
    "QueryInterface","AddRef","Release",
    "RegisterSoftwareDevice","GetAdapterCount","GetAdapterIdentifier","GetAdapterModeCount",
    "EnumAdapterModes","GetAdapterDisplayMode","CheckDeviceType","CheckDeviceFormat",
    "CheckDeviceMultiSampleType","CheckDepthStencilMatch","CheckDeviceFormatConversion",
    "GetDeviceCaps","GetAdapterMonitor","CreateDevice"
};
static const int kD3D9N = sizeof(kD3D9)/sizeof(kD3D9[0]);   // 17

static const char* kDev[] = {
    "QueryInterface","AddRef","Release",
    "TestCooperativeLevel","GetAvailableTextureMem","EvictManagedResources","GetDirect3D",
    "GetDeviceCaps","GetDisplayMode","GetCreationParameters","SetCursorProperties",
    "SetCursorPosition","ShowCursor","CreateAdditionalSwapChain","GetSwapChain",
    "GetNumberOfSwapChains","Reset","Present","GetBackBuffer","GetRasterStatus",
    "SetDialogBoxMode","SetGammaRamp","GetGammaRamp","CreateTexture","CreateVolumeTexture",
    "CreateCubeTexture","CreateVertexBuffer","CreateIndexBuffer","CreateRenderTarget",
    "CreateDepthStencilSurface","UpdateSurface","UpdateTexture","GetRenderTargetData",
    "GetFrontBufferData","StretchRect","ColorFill","CreateOffscreenPlainSurface",
    "SetRenderTarget","GetRenderTarget","SetDepthStencilSurface","GetDepthStencilSurface",
    "BeginScene","EndScene","Clear","SetTransform","GetTransform","MultiplyTransform",
    "SetViewport","GetViewport","SetMaterial","GetMaterial","SetLight","GetLight",
    "LightEnable","GetLightEnable","SetClipPlane","GetClipPlane","SetRenderState",
    "GetRenderState","CreateStateBlock","BeginStateBlock","EndStateBlock","SetClipStatus",
    "GetClipStatus","GetTexture","SetTexture","GetTextureStageState","SetTextureStageState",
    "GetSamplerState","SetSamplerState","ValidateDevice","SetPaletteEntries","GetPaletteEntries",
    "SetCurrentTexturePalette","GetCurrentTexturePalette","SetScissorRect","GetScissorRect",
    "SetSoftwareVertexProcessing","GetSoftwareVertexProcessing","SetNPatchMode","GetNPatchMode",
    "DrawPrimitive","DrawIndexedPrimitive","DrawPrimitiveUP","DrawIndexedPrimitiveUP",
    "ProcessVertices","CreateVertexDeclaration","SetVertexDeclaration","GetVertexDeclaration",
    "SetFVF","GetFVF","CreateVertexShader","SetVertexShader","GetVertexShader",
    "SetVertexShaderConstantF","GetVertexShaderConstantF","SetVertexShaderConstantI",
    "GetVertexShaderConstantI","SetVertexShaderConstantB","GetVertexShaderConstantB",
    "SetStreamSource","GetStreamSource","SetStreamSourceFreq","GetStreamSourceFreq",
    "SetIndices","GetIndices","CreatePixelShader","SetPixelShader","GetPixelShader",
    "SetPixelShaderConstantF","GetPixelShaderConstantF","SetPixelShaderConstantI",
    "GetPixelShaderConstantI","SetPixelShaderConstantB","GetPixelShaderConstantB",
    "DrawRectPatch","DrawTriPatch","DeletePatch","CreateQuery"
};
static const int kDevN = sizeof(kDev)/sizeof(kDev[0]);      // 119

// ---------------------------------------------------------------- 리소스 인터페이스
// vtable 은 **클래스 단위로 공유**되므로 각 타입의 첫 인스턴스 하나만 잡으면
// 이후 생성되는 모든 인스턴스가 자동으로 계측된다.

// IDirect3DResource9 공통 앞부분 (0..10) — Texture/VB/IB/Surface 가 모두 상속
#define RES_BASE \
    "QueryInterface","AddRef","Release", \
    "GetDevice","SetPrivateData","GetPrivateData","FreePrivateData", \
    "SetPriority","GetPriority","PreLoad","GetType"

static const char* kTex[] = {   // IDirect3DTexture9 : BaseTexture9 : Resource9
    RES_BASE,
    "SetLOD","GetLOD","GetLevelCount","SetAutoGenFilterType","GetAutoGenFilterType",
    "GenerateMipSubLevels",
    "GetLevelDesc","GetSurfaceLevel","LockRect","UnlockRect","AddDirtyRect"
};
static const int kTexN = sizeof(kTex)/sizeof(kTex[0]);      // 22

static const char* kVB[] = { RES_BASE, "Lock","Unlock","GetDesc" };
static const int kVBN = sizeof(kVB)/sizeof(kVB[0]);         // 14

static const char* kIB[] = { RES_BASE, "Lock","Unlock","GetDesc" };
static const int kIBN = sizeof(kIB)/sizeof(kIB[0]);         // 14

static const char* kSurf[] = {
    RES_BASE, "GetContainer","GetDesc","LockRect","UnlockRect","GetDC","ReleaseDC"
};
static const int kSurfN = sizeof(kSurf)/sizeof(kSurf[0]);   // 17

static const char* kDecl[] = {
    "QueryInterface","AddRef","Release","GetDevice","GetDeclaration"
};
static const int kDeclN = sizeof(kDecl)/sizeof(kDecl[0]);   // 5

static const char* kQuery[] = {
    "QueryInterface","AddRef","Release","GetDevice","GetType","GetDataSize","Issue","GetData"
};
static const int kQueryN = sizeof(kQuery)/sizeof(kQuery[0]);// 8

// iface id: 0=IDirect3D9 1=Device9 2=Texture9 3=VertexBuffer9 4=IndexBuffer9
//           5=Surface9 6=VertexDeclaration9 7=Query9
#define MAXIF 8
#define MAXM  160
static volatile LONG g_count[MAXIF][MAXM];
static void*         g_orig [MAXIF][MAXM];
static bool          g_hooked[MAXIF];

static void DumpCapturedTexture(FILE* f, void* texture)
{
    if (!texture || !g_hooked[2]) return;
    typedef UINT (WINAPI *PFN_GetLevelCount)(void*);
    typedef HRESULT (WINAPI *PFN_GetLevelDesc)(void*, UINT, CaptureSurfaceDesc*);
    typedef HRESULT (WINAPI *PFN_LockRect)(void*, UINT, CaptureLockedRect*, const void*, DWORD);
    typedef HRESULT (WINAPI *PFN_UnlockRect)(void*, UINT);
    UINT levels = ((PFN_GetLevelCount)g_orig[2][13])(texture);
    DWORD hash = 2166136261u;
    DWORD totalBytes = 0;
    CaptureSurfaceDesc first = {0};
    bool ok = levels > 0;
    for (UINT level = 0; ok && level < levels; ++level) {
        CaptureSurfaceDesc desc = {0};
        CaptureLockedRect locked = {0};
        if (FAILED(((PFN_GetLevelDesc)g_orig[2][17])(texture, level, &desc))) { ok = false; break; }
        if (level == 0) first = desc;
        DWORD bpp = (desc.format == 21 || desc.format == 22) ? 4u :
                    (desc.format == 23 || desc.format == 26) ? 2u : 0u;
        if (!bpp || FAILED(((PFN_LockRect)g_orig[2][19])(texture, level, &locked, NULL, 0x10u)) ||
            !locked.bits) { ok = false; break; }
        DWORD rowBytes = desc.width * bpp;
        DWORD levelHash = 2166136261u;
        DWORD levelBytes = 0;
        for (DWORD y = 0; y < desc.height; ++y) {
            const BYTE* row = (const BYTE*)locked.bits + y * locked.pitch;
            for (DWORD x = 0; x < rowBytes; ++x) {
                hash ^= row[x]; hash *= 16777619u;
                levelHash ^= row[x]; levelHash *= 16777619u;
            }
            totalBytes += rowBytes;
            levelBytes += rowBytes;
        }
        ((PFN_UnlockRect)g_orig[2][20])(texture, level);
        fprintf(f, "TEXLEVEL ptr=0x%08lX level=%u size=%lux%lu fmt=%lu bytes=%lu fnv1a=0x%08lX\n",
                (DWORD)(ULONG_PTR)texture, level, desc.width, desc.height, desc.format,
                levelBytes, levelHash);
    }
    fprintf(f, "TEX ptr=0x%08lX size=%lux%lu fmt=%lu levels=%u bytes=%lu fnv1a=%s%08lX\n",
            (DWORD)(ULONG_PTR)texture, first.width, first.height, first.format, levels,
            totalBytes, ok ? "0x" : "ERROR-0x", hash);
}

// ---------------------------------------------------------------- 로거
extern "C" void __cdecl LogCall(int id)
{
    int iface = (id >> 16) & 0xFF;
    int m     = id & 0xFFFF;
    if (iface < MAXIF && m < MAXM) InterlockedIncrement(&g_count[iface][m]);
}

// ---------------------------------------------------------------- 썽크 생성
// pushfd(9C) pushad(60) push imm32(68 xx) call rel32(E8 xx) add esp,4(83 C4 04)
// popad(61) popfd(9D) jmp dword ptr [abs](FF 25 xx)
static const int THUNK = 32;
static BYTE* g_pool = NULL;
static int   g_used = 0;

static void* MakeThunk(int id, void** origSlot)
{
    if (!g_pool) {
        g_pool = (BYTE*)VirtualAlloc(NULL, 64 * 1024,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_pool) return NULL;
    }
    BYTE* p = g_pool + g_used;
    g_used += THUNK;
    int i = 0;
    p[i++] = 0x9C;                                   // pushfd
    p[i++] = 0x60;                                   // pushad
    p[i++] = 0x68; *(int*)(p+i) = id; i += 4;        // push id
    p[i++] = 0xE8;                                   // call rel32 -> LogCall
    *(int*)(p+i) = (int)((BYTE*)LogCall - (p+i+4)); i += 4;
    p[i++] = 0x83; p[i++] = 0xC4; p[i++] = 0x04;     // add esp,4
    p[i++] = 0x61;                                   // popad
    p[i++] = 0x9D;                                   // popfd
    p[i++] = 0xFF; p[i++] = 0x25;                    // jmp dword ptr [origSlot]
    *(void***)(p+i) = origSlot;
    return p;
}

static void HookVTable(void* obj, int iface, int n)
{
    if (!obj || iface >= MAXIF || g_hooked[iface]) return;
    void** vt = *(void***)obj;
    DWORD old;
    if (!VirtualProtect(vt, n * sizeof(void*), PAGE_READWRITE, &old)) return;
    for (int i = 0; i < n && i < MAXM; ++i) {
        g_orig[iface][i] = vt[i];
        void* t = MakeThunk((iface << 16) | i, &g_orig[iface][i]);
        if (t) vt[i] = t;
    }
    VirtualProtect(vt, n * sizeof(void*), old, &old);
    g_hooked[iface] = true;
}

// ---------------------------------------------------------------- 인자 조합 기록
// 핫패스(SetRenderState/SetTextureStageState/... 프레임당 600+ 호출)에서는
// 문자열 포맷팅·힙 할당을 절대 하지 않는다. 고정 크기 오픈어드레싱 해시셋에
// (인자조합 -> 등장횟수) 만 누적하고, 사람이 읽을 형태로 뽑는 건 DLL_PROCESS_DETACH
// 시점의 DumpLog() 뿐이다.
struct ComboKey { DWORD a, b, c, d, e, f; };
struct ComboEntry { ComboKey key; LONG count; };

static bool ComboEq(const ComboKey& x, const ComboKey& y)
{
    return x.a==y.a && x.b==y.b && x.c==y.c && x.d==y.d && x.e==y.e && x.f==y.f;
}

static DWORD ComboHash(const ComboKey& k)
{
    DWORD h = 2166136261u;                 // FNV-1a
    h = (h ^ k.a) * 16777619u;
    h = (h ^ k.b) * 16777619u;
    h = (h ^ k.c) * 16777619u;
    h = (h ^ k.d) * 16777619u;
    h = (h ^ k.e) * 16777619u;
    h = (h ^ k.f) * 16777619u;
    return h;
}

// table 이 가득 차면(모든 슬롯이 이미 다른 키로 점유) 새 조합은 조용히 버린다 —
// 총 호출수는 기존 g_count[][] 로 이미 정확히 세고 있으므로 데이터 손실이 아니라
// "희귀 조합 일부 미수집" 정도의 손실이며, 덤프에서 used>=cap 이면 명시적으로 표시한다.
static void ComboRecord(ComboEntry* table, int cap, LONG* used,
                         DWORD a, DWORD b = 0, DWORD c = 0, DWORD d = 0, DWORD e = 0, DWORD f = 0)
{
    ComboKey k = { a, b, c, d, e, f };
    DWORD h = ComboHash(k);
    unsigned idx = h % (unsigned)cap;
    EnterCriticalSection(&g_argcs);
    for (int probe = 0; probe < cap; ++probe) {
        int i = (idx + (unsigned)probe) % (unsigned)cap;
        ComboEntry* en = &table[i];
        if (en->count == 0) {
            en->key = k; en->count = 1;
            if (used) ++*used;
            break;
        }
        if (ComboEq(en->key, k)) { ++en->count; break; }
        // 충돌: 다음 슬롯 계속 탐색
    }
    LeaveCriticalSection(&g_argcs);
}

// ---- 메서드별 테이블 (용량은 실제 관측 가능한 고유 조합 수 여유있게 추정) ----
static const int RS_CAP   = 4096;  // SetRenderState:        (state, value)
static const int TSS_CAP  = 8192;  // SetTextureStageState:  (stage, type, value)
static const int SS_CAP   = 4096;  // SetSamplerState:       (sampler, type, value)
static const int FVF_CAP  = 256;   // SetFVF:                (fvf)
static const int TEX_CAP  = 1024;  // CreateTexture:         (w,h,levels,usage,format,pool)
static const int PT_CAP   = 32;    // Draw*Primitive*:       (primitive type)
static const int XF_CAP   = 512;   // SetTransform:          (state; WORLD0..255 plus fixed slots)

static ComboEntry g_rsTab  [RS_CAP];  static LONG g_rsUsed;
static ComboEntry g_tssTab [TSS_CAP]; static LONG g_tssUsed;
static ComboEntry g_ssTab  [SS_CAP];  static LONG g_ssUsed;
static ComboEntry g_fvfTab [FVF_CAP]; static LONG g_fvfUsed;
static ComboEntry g_texTab [TEX_CAP]; static LONG g_texUsed;
static ComboEntry g_dpupTab[PT_CAP];  static LONG g_dpupUsed;  // DrawPrimitiveUP
static ComboEntry g_dipTab [PT_CAP];  static LONG g_dipUsed;   // DrawIndexedPrimitive
static ComboEntry g_xfTab  [XF_CAP];  static LONG g_xfUsed;

// ---- 덤프 유틸: 정상종료(DLL_PROCESS_DETACH) 뿐 아니라 세션 중간(Present 훅)에서도 호출된다.
//      g_flushcs 로 직렬화하므로(호출측 책임) 동시 실행은 없다 — g_scratch 공유 스크래치도 안전.
static void DumpLog(LONG frame, bool cleanExit);   // 전방 선언 (Hook_Present 보다 먼저 참조됨)

static ComboEntry g_scratch[TSS_CAP];  // 가장 큰 테이블 기준 재사용 스크래치

static int ComboCmpDesc(const void* pa, const void* pb)
{
    const ComboEntry* a = (const ComboEntry*)pa;
    const ComboEntry* b = (const ComboEntry*)pb;
    if (b->count > a->count) return 1;
    if (b->count < a->count) return -1;
    return 0;
}

static int CollectSorted(ComboEntry* table, int cap)
{
    int n = 0;
    for (int i = 0; i < cap; ++i) if (table[i].count) g_scratch[n++] = table[i];
    qsort(g_scratch, n, sizeof(ComboEntry), ComboCmpDesc);
    return n;
}

static void DumpComboHeader(FILE* f, const char* title, int n, LONG used, int cap)
{
    fprintf(f, "-- %s: 고유 조합 %d개 (테이블 사용 %ld/%d)%s --\n",
            title, n, used, cap, (used >= cap) ? " *** 테이블 가득참, 일부 희귀 조합 누락 가능 ***" : "");
}

static void DumpCombo1(FILE* f, const char* title, const char* n1,
                        ComboEntry* table, int cap, LONG used)
{
    int n = CollectSorted(table, cap);
    DumpComboHeader(f, title, n, used, cap);
    for (int i = 0; i < n; ++i)
        fprintf(f, "  %s=%-10lu (0x%08lX)   calls=%ld\n",
                n1, g_scratch[i].key.a, g_scratch[i].key.a, g_scratch[i].count);
    fprintf(f, "\n");
}

static void DumpCombo2(FILE* f, const char* title, const char* n1, const char* n2,
                        ComboEntry* table, int cap, LONG used)
{
    int n = CollectSorted(table, cap);
    DumpComboHeader(f, title, n, used, cap);
    for (int i = 0; i < n; ++i)
        fprintf(f, "  %s=%-6lu %s=%-10lu (0x%08lX)   calls=%ld\n",
                n1, g_scratch[i].key.a, n2, g_scratch[i].key.b, g_scratch[i].key.b, g_scratch[i].count);
    fprintf(f, "\n");
}

static void DumpCombo3(FILE* f, const char* title, const char* n1, const char* n2, const char* n3,
                        ComboEntry* table, int cap, LONG used)
{
    int n = CollectSorted(table, cap);
    DumpComboHeader(f, title, n, used, cap);
    for (int i = 0; i < n; ++i)
        fprintf(f, "  %s=%-4lu %s=%-4lu %s=%-10lu (0x%08lX)   calls=%ld\n",
                n1, g_scratch[i].key.a, n2, g_scratch[i].key.b,
                n3, g_scratch[i].key.c, g_scratch[i].key.c, g_scratch[i].count);
    fprintf(f, "\n");
}

static void DumpComboTex(FILE* f, const char* title, ComboEntry* table, int cap, LONG used)
{
    int n = CollectSorted(table, cap);
    DumpComboHeader(f, title, n, used, cap);
    for (int i = 0; i < n; ++i) {
        ComboKey& k = g_scratch[i].key;
        fprintf(f, "  w=%-5lu h=%-5lu levels=%-3lu usage=0x%08lX format=%-4lu(0x%04lX) pool=%lu   calls=%ld\n",
                k.a, k.b, k.c, k.d, k.e, k.e, k.f, g_scratch[i].count);
    }
    fprintf(f, "\n");
}

// ---------------------------------------------------------------- 결과 기록
// frame: 이 시점까지 관측된 Present 호출(=프레임) 수. cleanExit: DLL_PROCESS_DETACH 경유 여부.
// g_flushcs 로 직렬화 — 주기적 flush(Present 훅)와 최종 flush(DLL_PROCESS_DETACH)가
// 절대 동시에 파일을 건드리지 않도록 한다. 핫패스 락(g_argcs)과는 무관하므로
// SetRenderState 등 초당 수백~수천회 호출되는 경로는 이 락을 절대 기다리지 않는다.
static void DumpLog(LONG frame, bool cleanExit)
{
    EnterCriticalSection(&g_flushcs);
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* s = strrchr(path, '\\');
    if (s) strcpy(s + 1, "d3d9_probe.log"); else strcpy(path, "d3d9_probe.log");
    FILE* f = fopen(path, "w");
    if (!f) { LeaveCriticalSection(&g_flushcs); return; }
    // 부분 데이터 여부 표시 — 비정상 종료로 DLL_PROCESS_DETACH 가 못 불려도
    // 이 줄만 보면 "몇 프레임까지 살아있었는지 / 정상 종료였는지"를 바로 알 수 있다.
    fprintf(f, "# flush: frame=%ld status=%s\n",
            frame, cleanExit ? "clean-exit(complete)" : "partial(mid-session, will be overwritten by next flush)");
    fprintf(f, "# Azrok D3D9 API surface probe\n");
    fprintf(f, "# iface\tmethod\t\t\tidx\tcalls\n\n");
    struct { const char* nm; const char** tab; int n; } ifs[MAXIF] = {
        { "IDirect3D9",                 kD3D9,  kD3D9N  },
        { "IDirect3DDevice9",           kDev,   kDevN   },
        { "IDirect3DTexture9",          kTex,   kTexN   },
        { "IDirect3DVertexBuffer9",     kVB,    kVBN    },
        { "IDirect3DIndexBuffer9",      kIB,    kIBN    },
        { "IDirect3DSurface9",          kSurf,  kSurfN  },
        { "IDirect3DVertexDeclaration9",kDecl,  kDeclN  },
        { "IDirect3DQuery9",            kQuery, kQueryN },
    };
    int distinct = 0; long long total = 0;
    for (int a = 0; a < MAXIF; ++a) {
        fprintf(f, "== %s (hooked=%d) ==\n", ifs[a].nm, (int)g_hooked[a]);
        for (int i = 0; i < ifs[a].n; ++i) {
            LONG c = g_count[a][i];
            if (!c) continue;
            ++distinct; total += c;
            fprintf(f, "%-18s %-32s %3d  %10ld\n", ifs[a].nm, ifs[a].tab[i], i, c);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "== 미호출(구현 불필요) ==\n");
    for (int a = 0; a < MAXIF; ++a)
        for (int i = 0; i < ifs[a].n; ++i)
            if (!g_count[a][i]) fprintf(f, "  -%s::%s\n", ifs[a].nm, ifs[a].tab[i]);
    fprintf(f, "\n요약: 호출된 고유 메서드 %d개 / 총 호출 %lld회\n", distinct, total);

    // ---- 인자 분포: 고정기능 셰이더 순열 산정을 위한 실사용 상태 조합 집합 ----
    fprintf(f, "\n\n# ==================== 인자 분포 (arg distribution) ====================\n");
    fprintf(f, "# d3d9->WebGL2 고정기능 셰이더 순열 산정 근거. 조합은 호출 횟수 내림차순.\n\n");
    DumpCombo2(f, "SetRenderState",         "state",   "value",  g_rsTab,   RS_CAP,  g_rsUsed);
    DumpCombo3(f, "SetTextureStageState",   "stage",   "type",   "value", g_tssTab, TSS_CAP, g_tssUsed);
    DumpCombo3(f, "SetSamplerState",        "sampler", "type",   "value", g_ssTab,  SS_CAP,  g_ssUsed);
    DumpCombo1(f, "SetFVF",                 "fvf",               g_fvfTab, FVF_CAP, g_fvfUsed);
    DumpComboTex(f, "CreateTexture",                             g_texTab, TEX_CAP, g_texUsed);
    DumpCombo1(f, "DrawPrimitiveUP (primitive type)",       "primType", g_dpupTab, PT_CAP, g_dpupUsed);
    DumpCombo1(f, "DrawIndexedPrimitive (primitive type)",  "primType", g_dipTab,  PT_CAP, g_dipUsed);
    DumpCombo1(f, "SetTransform (transform slot, 행렬값 제외)", "state", g_xfTab, XF_CAP, g_xfUsed);

    fclose(f);
    LeaveCriticalSection(&g_flushcs);
}

// ---------------------------------------------------------------- 리소스 생성자 훅
// COM x86 ABI: STDMETHOD 는 __stdcall 이고 `this` 가 **첫 스택 인자**로 전달된다
// (그래서 C 스타일 `lpVtbl->M(pThis, ...)` 가 성립한다). WINAPI 로 선언하면 맞다.
//
// 각 훅은 원본을 호출한 뒤 반환된 인터페이스의 vtable 을 최초 1회만 패치한다.

static void HookVTable(void* obj, int iface, int n);   // 전방 선언

#define RESHOOK(slot, iface, count, name, params, args)                       \
    typedef HRESULT (WINAPI *PFN_##name) params;                              \
    static HRESULT WINAPI Hook_##name params {                                \
        LogCall((1 << 16) | (slot));                                          \
        HRESULT hr = ((PFN_##name)g_orig[1][slot]) args;                      \
        if (SUCCEEDED(hr) && pp && *pp) {                                     \
            EnterCriticalSection(&g_cs);                                      \
            HookVTable(*pp, (iface), (count));                                \
            LeaveCriticalSection(&g_cs);                                      \
        }                                                                     \
        return hr;                                                            \
    }

// CreateTexture 는 인자 조합(w,h,levels,usage,format,pool)도 기록해야 하므로
// RESHOOK 매크로 대신 직접 작성한다(반환 텍스처 vtable 훅은 동일하게 수행).
typedef HRESULT (WINAPI *PFN_CreateTexture)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
static HRESULT WINAPI Hook_CreateTexture(void* s, UINT w, UINT h, UINT levels,
                                         DWORD usage, DWORD fmt, DWORD pool, void** pp, void* shared)
{
    LogCall((1 << 16) | 23);
    ComboRecord(g_texTab, TEX_CAP, &g_texUsed, w, h, levels, usage, fmt, pool);
    HRESULT hr = ((PFN_CreateTexture)g_orig[1][23])(s, w, h, levels, usage, fmt, pool, pp, shared);
    if (SUCCEEDED(hr) && pp && *pp) {
        EnterCriticalSection(&g_cs);
        HookVTable(*pp, 2, kTexN);
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

RESHOOK(26, 3, kVBN,    CreateVertexBuffer,
        (void* s, UINT a, DWORD b, DWORD c, DWORD d, void** pp, void* e),
        (s, a, b, c, d, pp, e))

RESHOOK(27, 4, kIBN,    CreateIndexBuffer,
        (void* s, UINT a, DWORD b, DWORD c, DWORD d, void** pp, void* e),
        (s, a, b, c, d, pp, e))

RESHOOK(18, 5, kSurfN,  GetBackBuffer,
        (void* s, UINT a, UINT b, DWORD c, void** pp),
        (s, a, b, c, pp))

RESHOOK(86, 6, kDeclN,  CreateVertexDeclaration,
        (void* s, const void* a, void** pp),
        (s, a, pp))

RESHOOK(118, 7, kQueryN, CreateQuery,
        (void* s, DWORD a, void** pp),
        (s, a, pp))

// 디바이스 vtable 의 해당 슬롯을 전용 훅으로 덮는다(제네릭 썽크 위에).
static void InstallResourceHooks(void* dev)
{
    struct { int slot; void* fn; } h[] = {
        {  23, (void*)Hook_CreateTexture },
        {  26, (void*)Hook_CreateVertexBuffer },
        {  27, (void*)Hook_CreateIndexBuffer },
        {  18, (void*)Hook_GetBackBuffer },
        {  86, (void*)Hook_CreateVertexDeclaration },
        { 118, (void*)Hook_CreateQuery },
    };
    void** vt = *(void***)dev;
    for (int i = 0; i < (int)(sizeof(h)/sizeof(h[0])); ++i) {
        DWORD old;
        if (VirtualProtect(&vt[h[i].slot], sizeof(void*), PAGE_READWRITE, &old)) {
            vt[h[i].slot] = h[i].fn;
            VirtualProtect(&vt[h[i].slot], sizeof(void*), old, &old);
        }
    }
}

// ---------------------------------------------------------------- 인자 기록 전용 훅
// 아래 7개는 리소스를 생성하지 않는 순수 상태/드로우 호출이라 위의 RESHOOK 과 달리
// 반환 인터페이스를 잡을 필요가 없다. 그래도 제네릭 32바이트 썽크(호출 횟수만 셈) 대신
// 전용 C 훅으로 vtable 슬롯을 덮어써서 인자 조합까지 기록한다. g_orig[1][slot] 은
// HookVTable() 이 제네릭 썽크를 심기 전에 이미 저장해 둔 "진짜 원본"이므로 그대로 호출한다.

typedef HRESULT (WINAPI *PFN_SetRenderState)(void*, DWORD, DWORD);
static HRESULT WINAPI Hook_SetRenderState(void* s, DWORD state, DWORD value)
{
    LogCall((1 << 16) | 57);
    ComboRecord(g_rsTab, RS_CAP, &g_rsUsed, state, value);
    HRESULT hr = ((PFN_SetRenderState)g_orig[1][57])(s, state, value);
    if (SUCCEEDED(hr) && state < 256) {
        EnterCriticalSection(&g_capturecs);
        g_renderStateRaw[state] = value;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetTextureStageState)(void*, DWORD, DWORD, DWORD);
static HRESULT WINAPI Hook_SetTextureStageState(void* s, DWORD stage, DWORD type, DWORD value)
{
    LogCall((1 << 16) | 67);
    ComboRecord(g_tssTab, TSS_CAP, &g_tssUsed, stage, type, value);
    HRESULT hr = ((PFN_SetTextureStageState)g_orig[1][67])(s, stage, type, value);
    if (SUCCEEDED(hr) && stage < 2 && type < 33) {
        EnterCriticalSection(&g_capturecs);
        g_tssRaw[stage][type] = value;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetSamplerState)(void*, DWORD, DWORD, DWORD);
static HRESULT WINAPI Hook_SetSamplerState(void* s, DWORD sampler, DWORD type, DWORD value)
{
    LogCall((1 << 16) | 69);
    ComboRecord(g_ssTab, SS_CAP, &g_ssUsed, sampler, type, value);
    HRESULT hr = ((PFN_SetSamplerState)g_orig[1][69])(s, sampler, type, value);
    if (SUCCEEDED(hr) && sampler < 2 && type < 14) {
        EnterCriticalSection(&g_capturecs);
        g_samplerRaw[sampler][type] = value;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetTexture)(void*, DWORD, void*);
static HRESULT WINAPI Hook_SetTexture(void* s, DWORD stage, void* texture)
{
    LogCall((1 << 16) | 65);
    HRESULT hr = ((PFN_SetTexture)g_orig[1][65])(s, stage, texture);
    if (SUCCEEDED(hr) && stage < 2) {
        EnterCriticalSection(&g_capturecs);
        g_textureRaw[stage] = texture;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetFVF)(void*, DWORD);
static HRESULT WINAPI Hook_SetFVF(void* s, DWORD fvf)
{
    LogCall((1 << 16) | 89);
    ComboRecord(g_fvfTab, FVF_CAP, &g_fvfUsed, fvf);
    HRESULT hr = ((PFN_SetFVF)g_orig[1][89])(s, fvf);
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&g_capturecs);
        g_currentFvf = fvf;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetTransform)(void*, DWORD, const void*);
static HRESULT WINAPI Hook_SetTransform(void* s, DWORD state, const void* matrix)
{
    LogCall((1 << 16) | 44);
    ComboRecord(g_xfTab, XF_CAP, &g_xfUsed, state);   // 행렬 값은 기록 불필요, 슬롯만
    return ((PFN_SetTransform)g_orig[1][44])(s, state, matrix);
}

// Clean forwarding transform hook. The older probe hook above was damaged by
// a source-encoding/newline merge and no longer forwarded the original call.
static HRESULT WINAPI Hook_SetTransformCapture(void* s, DWORD state, const void* matrix)
{
    LogCall((1 << 16) | 44);
    ComboRecord(g_xfTab, XF_CAP, &g_xfUsed, state);
    HRESULT hr = ((PFN_SetTransform)g_orig[1][44])(s, state, matrix);
    if (SUCCEEDED(hr) && matrix && (state == 256 || state == 2 || state == 3)) {
        EnterCriticalSection(&g_capturecs);
        if (state == 256) memcpy(g_worldRaw, matrix, sizeof(g_worldRaw));
        else if (state == 2) memcpy(g_viewRaw, matrix, sizeof(g_viewRaw));
        else memcpy(g_projectionRaw, matrix, sizeof(g_projectionRaw));
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_SetViewport)(void*, const CaptureViewport*);
static HRESULT WINAPI Hook_SetViewportCapture(void* s, const CaptureViewport* viewport)
{
    LogCall((1 << 16) | 47);
    HRESULT hr = ((PFN_SetViewport)g_orig[1][47])(s, viewport);
    if (SUCCEEDED(hr) && viewport) {
        EnterCriticalSection(&g_capturecs);
        g_viewportRaw = *viewport;
        LeaveCriticalSection(&g_capturecs);
    }
    return hr;
}

typedef HRESULT (WINAPI *PFN_DrawPrimitiveUP)(void*, DWORD, UINT, const void*, UINT);
static HRESULT WINAPI Hook_DrawPrimitiveUP(void* s, DWORD primType, UINT primCount,
                                           const void* vdata, UINT stride)
{
    LogCall((1 << 16) | 83);
    ComboRecord(g_dpupTab, PT_CAP, &g_dpupUsed, primType);
    EnterCriticalSection(&g_capturecs);
    if (g_dpupCaptureActive) ++g_allDpupOrdinal;
    if (g_dpupCaptureActive && primType == 6 &&
        stride == 52 && (primCount == 2 || primCount == 3 || primCount == 5) &&
        vdata && g_dpupRecordCount < DPUP_CAPTURE_MAX) {
        DWORD bytes = (primCount + 2u) * stride;
        if (bytes <= DPUP_VERTEX_BYTES_MAX) {
            DpupCaptureRecord& r = g_dpupRecords[g_dpupRecordCount++];
            ZeroMemory(&r, sizeof(r));
            r.frame = g_frameCount + 1;
            r.ordinal = g_allDpupOrdinal;
            r.threadId = GetCurrentThreadId();
            r.primType = primType;
            r.primCount = primCount;
            r.stride = stride;
            r.fvf = g_currentFvf;
            r.vertexBytes = bytes;
            memcpy(r.world, g_worldRaw, sizeof(r.world));
            memcpy(r.view, g_viewRaw, sizeof(r.view));
            memcpy(r.projection, g_projectionRaw, sizeof(r.projection));
            r.viewport = g_viewportRaw;
            memcpy(r.tss, g_tssRaw, sizeof(r.tss));
            memcpy(r.sampler, g_samplerRaw, sizeof(r.sampler));
            for (int j = 0; j < CAPTURE_RS_COUNT; ++j)
                r.renderState[j] = g_renderStateRaw[g_captureRenderStateIds[j]];
            r.texture[0] = (DWORD)(ULONG_PTR)g_textureRaw[0];
            r.texture[1] = (DWORD)(ULONG_PTR)g_textureRaw[1];
            memcpy(r.vertices, vdata, bytes);
            r.byteHash = Fnv1a32(r.vertices, bytes);
        } else ++g_dpupDropped;
    } else if (g_dpupCaptureActive && primType == 6 &&
               stride == 52 && (primCount == 2 || primCount == 3 || primCount == 5) &&
               vdata && g_dpupRecordCount >= DPUP_CAPTURE_MAX) {
        ++g_dpupDropped;
    }
    LeaveCriticalSection(&g_capturecs);
    return ((PFN_DrawPrimitiveUP)g_orig[1][83])(s, primType, primCount, vdata, stride);
}

typedef HRESULT (WINAPI *PFN_DrawIndexedPrimitive)(void*, DWORD, INT, UINT, UINT, UINT, UINT);
static HRESULT WINAPI Hook_DrawIndexedPrimitive(void* s, DWORD primType, INT baseVertexIdx,
                                                UINT minVertexIdx, UINT numVertices,
                                                UINT startIndex, UINT primCount)
{
    LogCall((1 << 16) | 82);
    ComboRecord(g_dipTab, PT_CAP, &g_dipUsed, primType);
    return ((PFN_DrawIndexedPrimitive)g_orig[1][82])(s, primType, baseVertexIdx, minVertexIdx,
                                                     numVertices, startIndex, primCount);
}

// Present(슬롯 17) — 인자 조합 기록 대상은 아니지만, 세션당 ~2,000회 호출되는 유일한
// "프레임 경계" 지점이라 여기서 프레임 카운터를 세고 주기적 flush 를 트리거한다.
// 비정상 종료 대비: 원본 Present 를 먼저 호출해 이번 프레임은 정상적으로 화면에 낸 뒤,
// FLUSH_INTERVAL_FRAMES 배수째에만 DumpLog 를 부른다(세션당 3~4회, 무시 가능한 빈도).
// 카운터 증가 자체는 InterlockedIncrement 로 락 없이 처리 — 나머지 1,996회는 이 훅에서
// 정수 연산 하나만 추가될 뿐 락도 파일 I/O 도 전혀 타지 않는다.
typedef HRESULT (WINAPI *PFN_Present)(void*, const void*, const void*, HWND, const void*);
static HRESULT WINAPI Hook_Present(void* s, const void* srcRect, const void* destRect,
                                   HWND hwndOverride, const void* dirtyRegion)
{
    LogCall((1 << 16) | 17);
    HRESULT hr = ((PFN_Present)g_orig[1][17])(s, srcRect, destRect, hwndOverride, dirtyRegion);
    LONG frame = InterlockedIncrement(&g_frameCount);
    bool dumpCapture = false;
    EnterCriticalSection(&g_capturecs);
    if (g_dpupCaptureActive) {
        g_dpupCaptureActive = false;
        dumpCapture = true;
    }
    LeaveCriticalSection(&g_capturecs);
    if (dumpCapture) DumpDpupCapture();

    char armPath[MAX_PATH];
    BuildExeSiblingPath(armPath, "d3d9_dpup_arm.flag");
    DWORD armAttrs = GetFileAttributesA(armPath);
    if (armAttrs != INVALID_FILE_ATTRIBUTES && !(armAttrs & FILE_ATTRIBUTE_DIRECTORY) &&
        DeleteFileA(armPath)) {
        DWORD seedFvf = g_currentFvf;
        DWORD seedWorld[16];
        DWORD seedView[16];
        DWORD seedProjection[16];
        CaptureViewport seedViewport = g_viewportRaw;
        memcpy(seedWorld, g_worldRaw, sizeof(seedWorld));
        memcpy(seedView, g_viewRaw, sizeof(seedView));
        memcpy(seedProjection, g_projectionRaw, sizeof(seedProjection));
        typedef HRESULT (WINAPI *PFN_GetFVF)(void*, DWORD*);
        typedef HRESULT (WINAPI *PFN_GetTransform)(void*, DWORD, void*);
        typedef HRESULT (WINAPI *PFN_GetViewport)(void*, CaptureViewport*);
        typedef HRESULT (WINAPI *PFN_GetRenderState)(void*, DWORD, DWORD*);
        ((PFN_GetFVF)g_orig[1][90])(s, &seedFvf);
        ((PFN_GetTransform)g_orig[1][45])(s, 256, seedWorld);
        ((PFN_GetTransform)g_orig[1][45])(s, 2, seedView);
        ((PFN_GetTransform)g_orig[1][45])(s, 3, seedProjection);
        ((PFN_GetViewport)g_orig[1][48])(s, &seedViewport);
        DWORD seedRenderState[12];
        for (int j = 0; j < CAPTURE_RS_COUNT; ++j) {
            seedRenderState[j] = g_renderStateRaw[g_captureRenderStateIds[j]];
            ((PFN_GetRenderState)g_orig[1][58])(s, g_captureRenderStateIds[j], &seedRenderState[j]);
        }
        EnterCriticalSection(&g_capturecs);
        g_dpupRecordCount = 0;
        g_allDpupOrdinal = 0;
        g_dpupDropped = 0;
        g_currentFvf = seedFvf;
        memcpy(g_worldRaw, seedWorld, sizeof(g_worldRaw));
        memcpy(g_viewRaw, seedView, sizeof(g_viewRaw));
        memcpy(g_projectionRaw, seedProjection, sizeof(g_projectionRaw));
        g_viewportRaw = seedViewport;
        for (int j = 0; j < CAPTURE_RS_COUNT; ++j)
            g_renderStateRaw[g_captureRenderStateIds[j]] = seedRenderState[j];
        g_dpupCaptureActive = true;
        LeaveCriticalSection(&g_capturecs);
    }
    if ((frame % FLUSH_INTERVAL_FRAMES) == 0)
        DumpLog(frame, false);   // 부분 데이터 — 세션이 계속되면 다음 flush 가 덮어쓴다
    return hr;
}

// 디바이스 vtable 의 상태/드로우/프레임 슬롯을 전용 훅으로 덮는다(제네릭 썽크 위에).
static void InstallArgHooks(void* dev)
{
    struct { int slot; void* fn; } h[] = {
        { 17, (void*)Hook_Present },
        { 44, (void*)Hook_SetTransformCapture },
        { 47, (void*)Hook_SetViewportCapture },
        { 57, (void*)Hook_SetRenderState },
        { 65, (void*)Hook_SetTexture },
        { 67, (void*)Hook_SetTextureStageState },
        { 69, (void*)Hook_SetSamplerState },
        { 82, (void*)Hook_DrawIndexedPrimitive },
        { 83, (void*)Hook_DrawPrimitiveUP },
        { 89, (void*)Hook_SetFVF },
    };
    void** vt = *(void***)dev;
    for (int i = 0; i < (int)(sizeof(h)/sizeof(h[0])); ++i) {
        DWORD old;
        if (VirtualProtect(&vt[h[i].slot], sizeof(void*), PAGE_READWRITE, &old)) {
            vt[h[i].slot] = h[i].fn;
            VirtualProtect(&vt[h[i].slot], sizeof(void*), old, &old);
        }
    }
}

// ---------------------------------------------------------------- CreateDevice 특수처리
typedef HRESULT (WINAPI *PFN_CreateDevice)(void*, UINT, DWORD, HWND, DWORD, void*, void**);

static HRESULT WINAPI Hook_CreateDevice(void* self, UINT adapter, DWORD devType, HWND hwnd,
                                        DWORD behavior, void* pp, void** ppDev)
{
    LogCall((0 << 16) | 16);
    PFN_CreateDevice orig = (PFN_CreateDevice)g_orig[0][16];
    HRESULT hr = orig(self, adapter, devType, hwnd, behavior, pp, ppDev);
    if (SUCCEEDED(hr) && ppDev && *ppDev) {
        EnterCriticalSection(&g_cs);
        HookVTable(*ppDev, 1, kDevN);      // 디바이스 vtable 제네릭 계측
        InstallResourceHooks(*ppDev);      // 리소스 생성자만 전용 훅으로 덮어씀
        InstallArgHooks(*ppDev);           // 상태/드로우 인자 기록용 전용 훅으로 덮어씀
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

// ---------------------------------------------------------------- 진입점
typedef void* (WINAPI *PFN_D3DC9)(UINT);

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate9(UINT ver)
{
    if (!g_real) {
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strcat(sys, "\\d3d9.dll");
        g_real = LoadLibraryA(sys);
    }
    if (!g_real) return NULL;
    PFN_D3DC9 fn = (PFN_D3DC9)GetProcAddress(g_real, "Direct3DCreate9");
    if (!fn) return NULL;
    void* p = fn(ver);
    if (p) {
        EnterCriticalSection(&g_cs);
        HookVTable(p, 0, kD3D9N);
        // CreateDevice(16) 는 반환 디바이스를 잡아야 하므로 전용 훅으로 덮어쓴다.
        if (g_hooked[0]) {
            void** vt = *(void***)p;
            DWORD old;
            if (VirtualProtect(&vt[16], sizeof(void*), PAGE_READWRITE, &old)) {
                vt[16] = (void*)Hook_CreateDevice;
                VirtualProtect(&vt[16], sizeof(void*), old, &old);
            }
        }
        LeaveCriticalSection(&g_cs);
    }
    return p;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&g_cs);
        InitializeCriticalSection(&g_argcs);
        InitializeCriticalSection(&g_flushcs);
        InitializeCriticalSection(&g_capturecs);
        g_init = true;
        DisableThreadLibraryCalls(h);
    } else if (reason == DLL_PROCESS_DETACH && g_init) {
        DumpLog(g_frameCount, true);   // 정상 종료 — 완전한 최종 데이터로 마지막 flush 를 덮어씀
    }
    return TRUE;
}
