#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <stdio.h>

typedef void* (WINAPI* PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI* PFN_CreateDevice)(
    void*, UINT, DWORD, HWND, DWORD, void*, void**);
typedef HRESULT (WINAPI* PFN_Present)(
    void*, const void*, const void*, HWND, const void*);
typedef HRESULT (WINAPI* PFN_Scene)(void*);
typedef HRESULT (WINAPI* PFN_Clear)(
    void*, DWORD, const RECT*, DWORD, DWORD, float, DWORD);
typedef ULONG (WINAPI* PFN_Release)(void*);
typedef UINT (WINAPI* PFN_GetAdapterCount)(void*);
typedef HRESULT (WINAPI* PFN_GetAdapterIdentifier)(
    void*, UINT, DWORD, D3DADAPTER_IDENTIFIER9*);
typedef UINT (WINAPI* PFN_GetAdapterModeCount)(void*, UINT, D3DFORMAT);
typedef HRESULT (WINAPI* PFN_EnumAdapterModes)(
    void*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);
typedef HRESULT (WINAPI* PFN_GetAdapterDisplayMode)(
    void*, UINT, D3DDISPLAYMODE*);
typedef HRESULT (WINAPI* PFN_CheckDeviceType)(
    void*, UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL);
typedef HRESULT (WINAPI* PFN_CheckDeviceFormat)(
    void*, UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT);
typedef HRESULT (WINAPI* PFN_CheckDeviceMultiSampleType)(
    void*, UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE, DWORD*);
typedef HRESULT (WINAPI* PFN_CheckDepthStencilMatch)(
    void*, UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT);
typedef HRESULT (WINAPI* PFN_CheckDeviceFormatConversion)(
    void*, UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT);
typedef HRESULT (WINAPI* PFN_GetDeviceCaps)(
    void*, UINT, D3DDEVTYPE, D3DCAPS9*);
typedef HMONITOR (WINAPI* PFN_GetAdapterMonitor)(void*, UINT);
typedef HRESULT (WINAPI* PFN_SetTransform)(
    void*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
typedef HRESULT (WINAPI* PFN_SetViewport)(void*, const D3DVIEWPORT9*);
typedef HRESULT (WINAPI* PFN_GetViewport)(void*, D3DVIEWPORT9*);
typedef HRESULT (WINAPI* PFN_SetRenderState)(
    void*, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT (WINAPI* PFN_SetTextureStageState)(
    void*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
typedef HRESULT (WINAPI* PFN_SetSamplerState)(
    void*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
typedef HRESULT (WINAPI* PFN_SetScissorRect)(void*, const RECT*);
typedef HRESULT (WINAPI* PFN_GetScissorRect)(void*, RECT*);
typedef HRESULT (WINAPI* PFN_DrawPrimitiveUP)(
    void*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT (WINAPI* PFN_SetFVF)(void*, DWORD);
typedef HRESULT (WINAPI* PFN_CreateTexture)(
    void*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void**, HANDLE*);
typedef HRESULT (WINAPI* PFN_CreateVertexBuffer)(
    void*, UINT, DWORD, DWORD, D3DPOOL, void**, HANDLE*);
typedef HRESULT (WINAPI* PFN_CreateIndexBuffer)(
    void*, UINT, DWORD, D3DFORMAT, D3DPOOL, void**, HANDLE*);
typedef HRESULT (WINAPI* PFN_SetTexture)(void*, DWORD, void*);
typedef HRESULT (WINAPI* PFN_SetStreamSource)(void*, UINT, void*, UINT, UINT);
typedef HRESULT (WINAPI* PFN_SetIndices)(void*, void*);
typedef HRESULT (WINAPI* PFN_DrawIndexedPrimitive)(
    void*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI* PFN_TextureLockRect)(
    void*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);
typedef HRESULT (WINAPI* PFN_TextureUnlockRect)(void*, UINT);
typedef HRESULT (WINAPI* PFN_BufferLock)(void*, UINT, UINT, void**, DWORD);
typedef HRESULT (WINAPI* PFN_BufferUnlock)(void*);
typedef HRESULT (WINAPI* PFN_CreateVertexDeclaration)(
    void*, const D3DVERTEXELEMENT9*, void**);
typedef HRESULT (WINAPI* PFN_SetVertexDeclaration)(void*, void*);
typedef DWORD (WINAPI* PFN_TextureGetLevelCount)(void*);
typedef HRESULT (WINAPI* PFN_TextureGetLevelDesc)(
    void*, UINT, D3DSURFACE_DESC*);
typedef HRESULT (WINAPI* PFN_TestCooperativeLevel)(void*);
typedef UINT (WINAPI* PFN_GetAvailableTextureMem)(void*);
typedef HRESULT (WINAPI* PFN_DeviceGetDirect3D)(void*, IDirect3D9**);
typedef HRESULT (WINAPI* PFN_DeviceGetCaps)(void*, D3DCAPS9*);
typedef HRESULT (WINAPI* PFN_DeviceGetDisplayMode)(void*, UINT, D3DDISPLAYMODE*);
typedef BOOL (WINAPI* PFN_ShowCursor)(void*, BOOL);
typedef HRESULT (WINAPI* PFN_Reset)(void*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (WINAPI* PFN_GetBackBuffer)(
    void*, UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
typedef HRESULT (WINAPI* PFN_SetMaterial)(void*, const D3DMATERIAL9*);
typedef HRESULT (WINAPI* PFN_SetLight)(void*, DWORD, const D3DLIGHT9*);
typedef HRESULT (WINAPI* PFN_LightEnable)(void*, DWORD, BOOL);
typedef HRESULT (WINAPI* PFN_BeginStateBlock)(void*);
typedef HRESULT (WINAPI* PFN_EndStateBlock)(void*, IDirect3DStateBlock9**);
typedef HRESULT (WINAPI* PFN_StateBlockGetDevice)(void*, IDirect3DDevice9**);
typedef HRESULT (WINAPI* PFN_StateBlockAction)(void*);
typedef HRESULT (WINAPI* PFN_ValidateDevice)(void*, DWORD*);
typedef HRESULT (WINAPI* PFN_SetPaletteEntries)(void*, UINT, const PALETTEENTRY*);
typedef HRESULT (WINAPI* PFN_CreateQuery)(void*, D3DQUERYTYPE, IDirect3DQuery9**);
typedef HRESULT (WINAPI* PFN_TextureGetSurfaceLevel)(
    void*, UINT, IDirect3DSurface9**);
typedef HRESULT (WINAPI* PFN_SurfaceGetDesc)(void*, D3DSURFACE_DESC*);
typedef HRESULT (WINAPI* PFN_SurfaceLockRect)(
    void*, D3DLOCKED_RECT*, const RECT*, DWORD);
typedef HRESULT (WINAPI* PFN_SurfaceUnlockRect)(void*);
typedef D3DQUERYTYPE (WINAPI* PFN_QueryGetType)(void*);
typedef DWORD (WINAPI* PFN_QueryGetDataSize)(void*);
typedef HRESULT (WINAPI* PFN_QueryIssue)(void*, DWORD);
typedef HRESULT (WINAPI* PFN_QueryGetData)(void*, void*, DWORD, DWORD);

struct Phase6Vertex {
    float x, y, z, rhw;
    DWORD diffuse;
    DWORD specular;
    float u0, v0;
    float u1, v1;
};

struct Phase8Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u0, v0;
    float u1, v1;
    float u2, v2, w2;
};

static void* Slot(void* object, unsigned int index) {
    return (*reinterpret_cast<void***>(object))[index];
}

int main(int argc, char** argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    printf("D3D9WEB_PHASE6_BEGIN\n");
    printf("D3D9WEB_PHASE7_BEGIN\n");
    printf("D3D9WEB_PHASE8_BEGIN\n");
    printf("D3D9WEB_PHASE9_BEGIN\n");
    HMODULE module = LoadLibraryA(".\\d3d9.dll");
    if (!module) {
        printf("D3D9WEB_PHASE4_LOAD_FAIL error=%lu\n", GetLastError());
        return 10;
    }

    PFN_Direct3DCreate9 create9 = reinterpret_cast<PFN_Direct3DCreate9>(
        GetProcAddress(module, "Direct3DCreate9"));
    if (!create9) {
        printf("D3D9WEB_PHASE4_EXPORT_FAIL error=%lu\n", GetLastError());
        return 11;
    }

    void* d3d9 = create9(32);
    if (!d3d9) {
        printf("D3D9WEB_PHASE4_CREATE9_FAIL\n");
        return 12;
    }
    printf("D3D9WEB_PHASE5_STEP create9\n");

    if (argc > 1 && lstrcmpiA(argv[1], "--load-only") == 0) {
        printf("D3D9WEB_PHASE5_LOAD_ONLY create9=1\n");
        reinterpret_cast<PFN_Release>(Slot(d3d9, 2))(d3d9);
        return 0;
    }

    PFN_GetAdapterCount get_adapter_count =
        reinterpret_cast<PFN_GetAdapterCount>(Slot(d3d9, 4));
    PFN_GetAdapterIdentifier get_adapter_identifier =
        reinterpret_cast<PFN_GetAdapterIdentifier>(Slot(d3d9, 5));
    PFN_GetAdapterModeCount get_mode_count =
        reinterpret_cast<PFN_GetAdapterModeCount>(Slot(d3d9, 6));
    PFN_EnumAdapterModes enum_modes =
        reinterpret_cast<PFN_EnumAdapterModes>(Slot(d3d9, 7));
    PFN_GetAdapterDisplayMode get_display_mode =
        reinterpret_cast<PFN_GetAdapterDisplayMode>(Slot(d3d9, 8));
    PFN_CheckDeviceType check_device_type =
        reinterpret_cast<PFN_CheckDeviceType>(Slot(d3d9, 9));
    PFN_CheckDeviceFormat check_device_format =
        reinterpret_cast<PFN_CheckDeviceFormat>(Slot(d3d9, 10));
    PFN_CheckDeviceMultiSampleType check_multisample =
        reinterpret_cast<PFN_CheckDeviceMultiSampleType>(Slot(d3d9, 11));
    PFN_CheckDepthStencilMatch check_depth =
        reinterpret_cast<PFN_CheckDepthStencilMatch>(Slot(d3d9, 12));
    PFN_CheckDeviceFormatConversion check_conversion =
        reinterpret_cast<PFN_CheckDeviceFormatConversion>(Slot(d3d9, 13));
    PFN_GetDeviceCaps get_caps =
        reinterpret_cast<PFN_GetDeviceCaps>(Slot(d3d9, 14));
    PFN_GetAdapterMonitor get_monitor =
        reinterpret_cast<PFN_GetAdapterMonitor>(Slot(d3d9, 15));

    D3DADAPTER_IDENTIFIER9 identifier = {};
    D3DDISPLAYMODE enumerated_mode = {};
    D3DDISPLAYMODE current_mode = {};
    D3DCAPS9 caps = {};
    DWORD quality = 0;

    const UINT adapter_count = get_adapter_count(d3d9);
    printf("D3D9WEB_PHASE5_STEP adapter_count=%u\n", adapter_count);
    const HRESULT hr_identifier = get_adapter_identifier(d3d9, 0, 0, &identifier);
    printf("D3D9WEB_PHASE5_STEP identifier=0x%08lX\n",
           static_cast<unsigned long>(hr_identifier));
    const UINT mode_count = get_mode_count(d3d9, 0, D3DFMT_X8R8G8B8);
    printf("D3D9WEB_PHASE5_STEP mode_count=%u\n", mode_count);
    const HRESULT hr_enum = enum_modes(
        d3d9, 0, D3DFMT_X8R8G8B8, 0, &enumerated_mode);
    printf("D3D9WEB_PHASE5_STEP enum=0x%08lX\n",
           static_cast<unsigned long>(hr_enum));
    const HRESULT hr_display = get_display_mode(d3d9, 0, &current_mode);
    printf("D3D9WEB_PHASE5_STEP display=0x%08lX\n",
           static_cast<unsigned long>(hr_display));
    const HRESULT hr_type = check_device_type(
        d3d9, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
        D3DFMT_X8R8G8B8, TRUE);
    printf("D3D9WEB_PHASE5_STEP type=0x%08lX\n",
           static_cast<unsigned long>(hr_type));
    const HRESULT hr_format = check_device_format(
        d3d9, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
        D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
    printf("D3D9WEB_PHASE5_STEP format=0x%08lX\n",
           static_cast<unsigned long>(hr_format));
    const HRESULT hr_ms = check_multisample(
        d3d9, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE,
        D3DMULTISAMPLE_NONE, &quality);
    printf("D3D9WEB_PHASE5_STEP multisample=0x%08lX\n",
           static_cast<unsigned long>(hr_ms));
    const HRESULT hr_depth = check_depth(
        d3d9, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
        D3DFMT_X8R8G8B8, D3DFMT_D24S8);
    printf("D3D9WEB_PHASE5_STEP depth=0x%08lX\n",
           static_cast<unsigned long>(hr_depth));
    const HRESULT hr_conversion = check_conversion(
        d3d9, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8);
    printf("D3D9WEB_PHASE5_STEP conversion=0x%08lX\n",
           static_cast<unsigned long>(hr_conversion));
    const HRESULT hr_caps = get_caps(d3d9, 0, D3DDEVTYPE_HAL, &caps);
    printf("D3D9WEB_PHASE5_STEP caps=0x%08lX\n",
           static_cast<unsigned long>(hr_caps));
    const HMONITOR monitor = get_monitor(d3d9, 0);
    printf("D3D9WEB_PHASE5_STEP monitor=%p\n", monitor);

    const bool caps_ok = adapter_count == 1 && mode_count == 1 &&
        SUCCEEDED(hr_identifier) && SUCCEEDED(hr_enum) &&
        SUCCEEDED(hr_display) && SUCCEEDED(hr_type) && SUCCEEDED(hr_format) &&
        SUCCEEDED(hr_ms) && SUCCEEDED(hr_depth) && SUCCEEDED(hr_conversion) &&
        SUCCEEDED(hr_caps) && current_mode.Width == 800 &&
        current_mode.Height == 600 && caps.MaxTextureWidth >= 4096 &&
        caps.MaxSimultaneousTextures >= 8 && monitor != 0;

    printf("D3D9WEB_PHASE5_CAPS adapters=%u modes=%u id=0x%08lX "
           "enum=0x%08lX display=0x%08lX type=0x%08lX format=0x%08lX "
           "ms=0x%08lX depth=0x%08lX conversion=0x%08lX caps=0x%08lX "
            "size=%ux%u textures=%lu devcaps=0x%08lX texops=0x%08lX "
            "maxtex=%lux%lu vs=0x%08lX ps=0x%08lX monitor=%p result=%s\n",
           adapter_count, mode_count,
           static_cast<unsigned long>(hr_identifier),
           static_cast<unsigned long>(hr_enum),
           static_cast<unsigned long>(hr_display),
           static_cast<unsigned long>(hr_type),
           static_cast<unsigned long>(hr_format),
           static_cast<unsigned long>(hr_ms),
           static_cast<unsigned long>(hr_depth),
           static_cast<unsigned long>(hr_conversion),
           static_cast<unsigned long>(hr_caps),
            current_mode.Width, current_mode.Height,
            static_cast<unsigned long>(caps.MaxSimultaneousTextures),
            static_cast<unsigned long>(caps.DevCaps),
            static_cast<unsigned long>(caps.TextureOpCaps),
            static_cast<unsigned long>(caps.MaxTextureWidth),
            static_cast<unsigned long>(caps.MaxTextureHeight),
            static_cast<unsigned long>(caps.VertexShaderVersion),
            static_cast<unsigned long>(caps.PixelShaderVersion),
            monitor, caps_ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE5_CAPS_DETAIL caps2=0x%08lX primitive=0x%08lX "
           "raster=0x%08lX shade=0x%08lX texture=0x%08lX filter=0x%08lX "
           "address=0x%08lX line=0x%08lX activeLights=%lu blendMatrices=%lu "
           "blendIndex=%lu streams=%lu primitiveCount=%lu vertexIndex=%lu\n",
           static_cast<unsigned long>(caps.Caps2),
           static_cast<unsigned long>(caps.PrimitiveMiscCaps),
           static_cast<unsigned long>(caps.RasterCaps),
           static_cast<unsigned long>(caps.ShadeCaps),
           static_cast<unsigned long>(caps.TextureCaps),
           static_cast<unsigned long>(caps.TextureFilterCaps),
           static_cast<unsigned long>(caps.TextureAddressCaps),
           static_cast<unsigned long>(caps.LineCaps),
           static_cast<unsigned long>(caps.MaxActiveLights),
           static_cast<unsigned long>(caps.MaxVertexBlendMatrices),
           static_cast<unsigned long>(caps.MaxVertexBlendMatrixIndex),
           static_cast<unsigned long>(caps.MaxStreams),
           static_cast<unsigned long>(caps.MaxPrimitiveCount),
           static_cast<unsigned long>(caps.MaxVertexIndex));
    if (!caps_ok) return 15;

    PFN_CreateDevice create_device =
        reinterpret_cast<PFN_CreateDevice>(Slot(d3d9, 16));
    void* device = 0;
    D3DPRESENT_PARAMETERS present_parameters = {};
    present_parameters.BackBufferWidth = 800;
    present_parameters.BackBufferHeight = 600;
    present_parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    present_parameters.BackBufferCount = 1;
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.Windowed = TRUE;
    present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr_create = create_device(
        d3d9, 0, D3DDEVTYPE_HAL, 0, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &present_parameters, &device);
    if (FAILED(hr_create) || !device) {
        printf("D3D9WEB_PHASE5_CREATEDEVICE_FAIL hr=0x%08lX device=%p\n",
               static_cast<unsigned long>(hr_create), device);
        return 13;
    }

    PFN_Clear clear = reinterpret_cast<PFN_Clear>(Slot(device, 43));
    PFN_Scene begin_scene = reinterpret_cast<PFN_Scene>(Slot(device, 41));
    PFN_Scene end_scene = reinterpret_cast<PFN_Scene>(Slot(device, 42));
    PFN_Present present = reinterpret_cast<PFN_Present>(Slot(device, 17));
    PFN_SetTransform set_transform =
        reinterpret_cast<PFN_SetTransform>(Slot(device, 44));
    PFN_SetViewport set_viewport =
        reinterpret_cast<PFN_SetViewport>(Slot(device, 47));
    PFN_GetViewport get_viewport =
        reinterpret_cast<PFN_GetViewport>(Slot(device, 48));
    PFN_SetRenderState set_render_state =
        reinterpret_cast<PFN_SetRenderState>(Slot(device, 57));
    PFN_SetTextureStageState set_tss =
        reinterpret_cast<PFN_SetTextureStageState>(Slot(device, 67));
    PFN_SetSamplerState set_sampler =
        reinterpret_cast<PFN_SetSamplerState>(Slot(device, 69));
    PFN_SetScissorRect set_scissor =
        reinterpret_cast<PFN_SetScissorRect>(Slot(device, 75));
    PFN_GetScissorRect get_scissor =
        reinterpret_cast<PFN_GetScissorRect>(Slot(device, 76));
    PFN_DrawPrimitiveUP draw_up =
        reinterpret_cast<PFN_DrawPrimitiveUP>(Slot(device, 83));
    PFN_SetFVF set_fvf = reinterpret_cast<PFN_SetFVF>(Slot(device, 89));
    PFN_CreateTexture create_texture =
        reinterpret_cast<PFN_CreateTexture>(Slot(device, 23));
    PFN_CreateVertexBuffer create_vb =
        reinterpret_cast<PFN_CreateVertexBuffer>(Slot(device, 26));
    PFN_CreateIndexBuffer create_ib =
        reinterpret_cast<PFN_CreateIndexBuffer>(Slot(device, 27));
    PFN_SetTexture set_texture =
        reinterpret_cast<PFN_SetTexture>(Slot(device, 65));
    PFN_DrawIndexedPrimitive draw_indexed =
        reinterpret_cast<PFN_DrawIndexedPrimitive>(Slot(device, 82));
    PFN_SetStreamSource set_stream =
        reinterpret_cast<PFN_SetStreamSource>(Slot(device, 100));
    PFN_SetIndices set_indices =
        reinterpret_cast<PFN_SetIndices>(Slot(device, 104));
    PFN_CreateVertexDeclaration create_declaration =
        reinterpret_cast<PFN_CreateVertexDeclaration>(Slot(device, 86));
    PFN_SetVertexDeclaration set_declaration =
        reinterpret_cast<PFN_SetVertexDeclaration>(Slot(device, 87));
    PFN_TestCooperativeLevel test_cooperative =
        reinterpret_cast<PFN_TestCooperativeLevel>(Slot(device, 3));
    PFN_GetAvailableTextureMem get_available_texture_mem =
        reinterpret_cast<PFN_GetAvailableTextureMem>(Slot(device, 4));
    PFN_DeviceGetDirect3D device_get_d3d =
        reinterpret_cast<PFN_DeviceGetDirect3D>(Slot(device, 6));
    PFN_DeviceGetCaps device_get_caps =
        reinterpret_cast<PFN_DeviceGetCaps>(Slot(device, 7));
    PFN_DeviceGetDisplayMode device_get_display_mode =
        reinterpret_cast<PFN_DeviceGetDisplayMode>(Slot(device, 8));
    PFN_ShowCursor show_cursor = reinterpret_cast<PFN_ShowCursor>(Slot(device, 12));
    PFN_Reset reset = reinterpret_cast<PFN_Reset>(Slot(device, 16));
    PFN_GetBackBuffer get_back_buffer =
        reinterpret_cast<PFN_GetBackBuffer>(Slot(device, 18));
    PFN_SetMaterial set_material =
        reinterpret_cast<PFN_SetMaterial>(Slot(device, 49));
    PFN_SetLight set_light = reinterpret_cast<PFN_SetLight>(Slot(device, 51));
    PFN_LightEnable light_enable =
        reinterpret_cast<PFN_LightEnable>(Slot(device, 53));
    PFN_BeginStateBlock begin_state_block =
        reinterpret_cast<PFN_BeginStateBlock>(Slot(device, 60));
    PFN_EndStateBlock end_state_block =
        reinterpret_cast<PFN_EndStateBlock>(Slot(device, 61));
    PFN_ValidateDevice validate_device =
        reinterpret_cast<PFN_ValidateDevice>(Slot(device, 70));
    PFN_SetPaletteEntries set_palette_entries =
        reinterpret_cast<PFN_SetPaletteEntries>(Slot(device, 71));
    PFN_CreateQuery create_query =
        reinterpret_cast<PFN_CreateQuery>(Slot(device, 118));

    HRESULT hr_cooperative = test_cooperative(device);
    const UINT available_texture_mem = get_available_texture_mem(device);
    IDirect3D9* parent_d3d = 0;
    HRESULT hr_parent = device_get_d3d(device, &parent_d3d);
    D3DCAPS9 device_caps = {};
    HRESULT hr_device_caps = device_get_caps(device, &device_caps);
    D3DDISPLAYMODE device_mode = {};
    HRESULT hr_device_mode = device_get_display_mode(device, 0, &device_mode);
    const BOOL cursor_before = show_cursor(device, TRUE);
    const BOOL cursor_after = show_cursor(device, FALSE);
    IDirect3DSurface9* backbuffer = 0;
    HRESULT hr_backbuffer = get_back_buffer(
        device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    D3DSURFACE_DESC backbuffer_desc = {};
    HRESULT hr_backbuffer_desc = backbuffer ?
        reinterpret_cast<PFN_SurfaceGetDesc>(Slot(backbuffer, 12))(
            backbuffer, &backbuffer_desc) : E_FAIL;
    if (backbuffer) reinterpret_cast<PFN_Release>(Slot(backbuffer, 2))(backbuffer);
    if (parent_d3d) reinterpret_cast<PFN_Release>(Slot(parent_d3d, 2))(parent_d3d);

    D3DPRESENT_PARAMETERS reset_parameters = present_parameters;
    reset_parameters.BackBufferWidth = 640;
    reset_parameters.BackBufferHeight = 480;
    HRESULT hr_reset_640 = reset(device, &reset_parameters);
    D3DDISPLAYMODE reset_mode = {};
    HRESULT hr_reset_mode = device_get_display_mode(device, 0, &reset_mode);
    HRESULT hr_reset_800 = reset(device, &present_parameters);
    DWORD validation_passes = 0;
    HRESULT hr_validate = validate_device(device, &validation_passes);
    PALETTEENTRY palette[256] = {};
    palette[0].peRed = 0x12;
    palette[0].peGreen = 0x34;
    palette[0].peBlue = 0x56;
    palette[0].peFlags = 0x78;
    HRESULT hr_palette = set_palette_entries(device, 0, palette);

    const bool lifecycle_ok = SUCCEEDED(hr_cooperative) &&
        available_texture_mem == 256u * 1024u * 1024u &&
        SUCCEEDED(hr_parent) && SUCCEEDED(hr_device_caps) &&
        SUCCEEDED(hr_device_mode) && device_mode.Width == 800 && device_mode.Height == 600 &&
        cursor_before == FALSE && cursor_after == TRUE &&
        SUCCEEDED(hr_backbuffer) && SUCCEEDED(hr_backbuffer_desc) &&
        backbuffer_desc.Width == 800 && backbuffer_desc.Height == 600 &&
        SUCCEEDED(hr_reset_640) && SUCCEEDED(hr_reset_mode) &&
        reset_mode.Width == 640 && reset_mode.Height == 480 &&
        SUCCEEDED(hr_reset_800) && SUCCEEDED(hr_validate) && validation_passes == 1 &&
        SUCCEEDED(hr_palette);
    printf("D3D9WEB_PHASE9_LIFECYCLE coop=0x%08lX mem=%u parent=0x%08lX "
           "caps=0x%08lX mode=%ux%u cursor=%d/%d backbuffer=%ux%u "
           "reset=%ux%u passes=%lu palette=0x%08lX result=%s\n",
           static_cast<unsigned long>(hr_cooperative), available_texture_mem,
           static_cast<unsigned long>(hr_parent),
           static_cast<unsigned long>(hr_device_caps), device_mode.Width, device_mode.Height,
           cursor_before, cursor_after, backbuffer_desc.Width, backbuffer_desc.Height,
           reset_mode.Width, reset_mode.Height, static_cast<unsigned long>(validation_passes),
           static_cast<unsigned long>(hr_palette), lifecycle_ok ? "PASS" : "FAIL");

    D3DMATRIX identity = {};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    D3DVIEWPORT9 viewport = {0, 0, 800, 600, 0.0f, 1.0f};
    D3DVIEWPORT9 viewport_echo = {};
    RECT scissor = {0, 0, 800, 600};
    RECT scissor_echo = {};
    const DWORD phase6_fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE |
                             D3DFVF_SPECULAR | D3DFVF_TEX2;
    const Phase6Vertex vertices[3] = {
        {150.0f, 500.0f, 0.5f, 1.0f, 0xFFFF4050u, 0, 0, 1, 0, 0},
        {650.0f, 500.0f, 0.5f, 1.0f, 0xFF40FF70u, 0, 1, 1, 0, 0},
        {400.0f, 100.0f, 0.5f, 1.0f, 0xFF4080FFu, 0, 0.5f, 0, 0, 0},
    };

    HRESULT state_hr[20] = {};
    unsigned state_i = 0;
    state_hr[state_i++] = set_transform(device, D3DTS_WORLD, &identity);
    state_hr[state_i++] = set_transform(device, D3DTS_VIEW, &identity);
    state_hr[state_i++] = set_transform(device, D3DTS_PROJECTION, &identity);
    state_hr[state_i++] = set_viewport(device, &viewport);
    state_hr[state_i++] = get_viewport(device, &viewport_echo);
    state_hr[state_i++] = set_scissor(device, &scissor);
    state_hr[state_i++] = get_scissor(device, &scissor_echo);
    state_hr[state_i++] = set_render_state(device, D3DRS_CULLMODE, D3DCULL_NONE);
    state_hr[state_i++] = set_render_state(device, D3DRS_ZENABLE, FALSE);
    state_hr[state_i++] = set_render_state(device, D3DRS_ALPHABLENDENABLE, FALSE);
    state_hr[state_i++] = set_render_state(device, D3DRS_LIGHTING, FALSE);
    state_hr[state_i++] = set_tss(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    state_hr[state_i++] = set_tss(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    state_hr[state_i++] = set_tss(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    state_hr[state_i++] = set_tss(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    state_hr[state_i++] = set_tss(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    state_hr[state_i++] = set_sampler(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    state_hr[state_i++] = set_sampler(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    state_hr[state_i++] = set_sampler(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    state_hr[state_i++] = set_fvf(device, phase6_fvf);

    bool state_ok = viewport_echo.Width == 800 && viewport_echo.Height == 600 &&
                    scissor_echo.right == 800 && scissor_echo.bottom == 600;
    for (unsigned i = 0; i < state_i; ++i) state_ok = state_ok && SUCCEEDED(state_hr[i]);

    HRESULT hr_clear = clear(device, 0, 0, 1, 0xFF101828u, 1.0f, 0);
    HRESULT hr_begin = begin_scene(device);
    HRESULT hr_draw = draw_up(
        device, D3DPT_TRIANGLELIST, 1, vertices, sizeof(Phase6Vertex));
    HRESULT hr_end = end_scene(device);
    HRESULT hr_present = present(device, 0, 0, 0, 0);

    void* texture = 0;
    void* vertex_buffer = 0;
    void* index_buffer = 0;
    void* declaration = 0;
    HRESULT hr_tex = create_texture(
        device, 8, 8, 4, 0, D3DFMT_R5G6B5, D3DPOOL_MANAGED, &texture, 0);
    HRESULT hr_vb = create_vb(
        device, 3 * sizeof(Phase8Vertex), 0, 0, D3DPOOL_MANAGED,
        &vertex_buffer, 0);
    HRESULT hr_ib = create_ib(
        device, 3 * sizeof(WORD), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
        &index_buffer, 0);

    const D3DVERTEXELEMENT9 phase8_declaration[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        {0, 40, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        D3DDECL_END()
    };
    HRESULT hr_decl = create_declaration(device, phase8_declaration, &declaration);

    HRESULT hr_tex_lock[4] = {E_FAIL, E_FAIL, E_FAIL, E_FAIL};
    HRESULT hr_tex_unlock[4] = {E_FAIL, E_FAIL, E_FAIL, E_FAIL};
    HRESULT hr_vb_lock = E_FAIL, hr_vb_unlock = E_FAIL;
    HRESULT hr_ib_lock = E_FAIL, hr_ib_unlock = E_FAIL;
    LONG texture_pitch[4] = {};
    DWORD texture_levels = 0;
    D3DSURFACE_DESC texture_desc[4] = {};
    if (SUCCEEDED(hr_tex) && texture) {
        PFN_TextureGetLevelCount get_level_count =
            reinterpret_cast<PFN_TextureGetLevelCount>(Slot(texture, 13));
        PFN_TextureGetLevelDesc get_level_desc =
            reinterpret_cast<PFN_TextureGetLevelDesc>(Slot(texture, 17));
        PFN_TextureLockRect lock_texture =
            reinterpret_cast<PFN_TextureLockRect>(Slot(texture, 19));
        PFN_TextureUnlockRect unlock_texture =
            reinterpret_cast<PFN_TextureUnlockRect>(Slot(texture, 20));
        texture_levels = get_level_count(texture);
        const WORD solid_levels[4] = {0, 0xFFE0u, 0xF81Fu, 0x07FFu};
        for (UINT level = 0; level < 4; ++level) {
            get_level_desc(texture, level, &texture_desc[level]);
            D3DLOCKED_RECT locked = {};
            hr_tex_lock[level] = lock_texture(texture, level, &locked, 0, 0);
            texture_pitch[level] = locked.Pitch;
            if (SUCCEEDED(hr_tex_lock[level])) {
                for (UINT y = 0; y < texture_desc[level].Height; ++y) {
                    WORD* row = reinterpret_cast<WORD*>(
                        static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
                    for (UINT x = 0; x < texture_desc[level].Width; ++x) {
                        if (level == 0) {
                            const UINT quadrant = ((x >= 4) ? 1u : 0u) |
                                                  ((y >= 4) ? 2u : 0u);
                            const WORD colors[4] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu};
                            row[x] = colors[quadrant];
                        } else {
                            row[x] = solid_levels[level];
                        }
                    }
                }
                hr_tex_unlock[level] = unlock_texture(texture, level);
            }
        }
    }

    HRESULT hr_surface_level = E_FAIL, hr_surface_desc = E_FAIL;
    HRESULT hr_surface_lock = E_FAIL, hr_surface_unlock = E_FAIL;
    HRESULT hr_verify_lock = E_FAIL, hr_verify_unlock = E_FAIL;
    D3DSURFACE_DESC surface_desc = {};
    LONG surface_pitch = 0;
    WORD surface_value = 0;
    IDirect3DSurface9* mip_surface = 0;
    if (texture) {
        PFN_TextureGetSurfaceLevel get_surface_level =
            reinterpret_cast<PFN_TextureGetSurfaceLevel>(Slot(texture, 18));
        hr_surface_level = get_surface_level(texture, 2, &mip_surface);
        if (SUCCEEDED(hr_surface_level) && mip_surface) {
            PFN_SurfaceGetDesc get_surface_desc =
                reinterpret_cast<PFN_SurfaceGetDesc>(Slot(mip_surface, 12));
            PFN_SurfaceLockRect lock_surface =
                reinterpret_cast<PFN_SurfaceLockRect>(Slot(mip_surface, 13));
            PFN_SurfaceUnlockRect unlock_surface =
                reinterpret_cast<PFN_SurfaceUnlockRect>(Slot(mip_surface, 14));
            hr_surface_desc = get_surface_desc(mip_surface, &surface_desc);
            D3DLOCKED_RECT locked = {};
            hr_surface_lock = lock_surface(mip_surface, &locked, 0, 0);
            surface_pitch = locked.Pitch;
            if (SUCCEEDED(hr_surface_lock)) {
                *static_cast<WORD*>(locked.pBits) = 0x1234u;
                hr_surface_unlock = unlock_surface(mip_surface);
            }
        }
        D3DLOCKED_RECT verify = {};
        PFN_TextureLockRect lock_texture =
            reinterpret_cast<PFN_TextureLockRect>(Slot(texture, 19));
        PFN_TextureUnlockRect unlock_texture =
            reinterpret_cast<PFN_TextureUnlockRect>(Slot(texture, 20));
        hr_verify_lock = lock_texture(texture, 2, &verify, 0, D3DLOCK_READONLY);
        if (SUCCEEDED(hr_verify_lock)) {
            surface_value = *static_cast<WORD*>(verify.pBits);
            hr_verify_unlock = unlock_texture(texture, 2);
        }
        if (mip_surface) reinterpret_cast<PFN_Release>(Slot(mip_surface, 2))(mip_surface);
    }
    const bool surface_upload_ok = SUCCEEDED(hr_surface_level) &&
        SUCCEEDED(hr_surface_desc) && surface_desc.Width == 2 && surface_desc.Height == 2 &&
        surface_desc.Format == D3DFMT_R5G6B5 && SUCCEEDED(hr_surface_lock) &&
        SUCCEEDED(hr_surface_unlock) && surface_pitch == 4 &&
        SUCCEEDED(hr_verify_lock) && SUCCEEDED(hr_verify_unlock) && surface_value == 0x1234u;
    printf("D3D9WEB_PHASE9_SURFACE level=2 size=%ux%u pitch=%ld value=0x%04X result=%s\n",
           surface_desc.Width, surface_desc.Height, surface_pitch, surface_value,
           surface_upload_ok ? "PASS" : "FAIL");

    Phase8Vertex indexed_vertices[3] = {
        {-0.625f, -0.6666667f, 0.4f, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1},
        { 0.625f, -0.6666667f, 0.4f, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1},
        { 0.0f,    0.6666667f, 0.4f, 0, 0, 1, 0.5f, 0, 0, 0, 0, 0, 1},
    };
    if (SUCCEEDED(hr_vb) && vertex_buffer) {
        void* data = 0;
        PFN_BufferLock lock_buffer =
            reinterpret_cast<PFN_BufferLock>(Slot(vertex_buffer, 11));
        PFN_BufferUnlock unlock_buffer =
            reinterpret_cast<PFN_BufferUnlock>(Slot(vertex_buffer, 12));
        hr_vb_lock = lock_buffer(vertex_buffer, 0, 0, &data, 0);
        if (SUCCEEDED(hr_vb_lock)) {
            CopyMemory(data, indexed_vertices, sizeof(indexed_vertices));
            hr_vb_unlock = unlock_buffer(vertex_buffer);
        }
    }
    if (SUCCEEDED(hr_ib) && index_buffer) {
        void* data = 0;
        PFN_BufferLock lock_buffer =
            reinterpret_cast<PFN_BufferLock>(Slot(index_buffer, 11));
        PFN_BufferUnlock unlock_buffer =
            reinterpret_cast<PFN_BufferUnlock>(Slot(index_buffer, 12));
        hr_ib_lock = lock_buffer(index_buffer, 0, 0, &data, 0);
        if (SUCCEEDED(hr_ib_lock)) {
            WORD indices[3] = {0, 1, 2};
            CopyMemory(data, indices, sizeof(indices));
            hr_ib_unlock = unlock_buffer(index_buffer);
        }
    }

    HRESULT hr_stage_color_op = set_tss(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    HRESULT hr_stage_color_a = set_tss(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    HRESULT hr_stage_color_b = set_tss(device, 0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    HRESULT hr_stage_alpha_op = set_tss(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    HRESULT hr_stage_alpha_a = set_tss(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    HRESULT hr_min = set_sampler(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    HRESULT hr_mag = set_sampler(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    HRESULT hr_mip = set_sampler(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    HRESULT hr_bind_texture = set_texture(device, 0, texture);
    HRESULT hr_bind_stream = set_stream(
        device, 0, vertex_buffer, 0, sizeof(Phase8Vertex));
    HRESULT hr_bind_indices = set_indices(device, index_buffer);
    HRESULT hr_bind_declaration = set_declaration(device, declaration);
    HRESULT hr_index_begin = begin_scene(device);
    HRESULT hr_index_draw = draw_indexed(
        device, D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
    HRESULT hr_index_end = end_scene(device);
    HRESULT hr_index_present = present(device, 0, 0, 0, 0);

    D3DMATERIAL9 material = {};
    material.Diffuse.r = 0.25f;
    material.Diffuse.g = 0.50f;
    material.Diffuse.b = 0.75f;
    material.Diffuse.a = 1.0f;
    material.Ambient = material.Diffuse;
    material.Power = 8.0f;
    D3DLIGHT9 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1.0f;
    light.Direction.z = -1.0f;
    HRESULT hr_material = set_material(device, &material);
    HRESULT hr_light = set_light(device, 0, &light);
    HRESULT hr_enable = light_enable(device, 0, TRUE);
    HRESULT hr_light_present = present(device, 0, 0, 0, 0);
    HRESULT hr_disable = light_enable(device, 0, FALSE);
    HRESULT hr_bad_light = set_light(device, 8, &light);
    HRESULT hr_bad_enable = light_enable(device, 8, TRUE);

    IDirect3DQuery9* query = 0;
    HRESULT hr_query_probe = create_query(device, D3DQUERYTYPE_VCACHE, 0);
    HRESULT hr_query_create = create_query(device, D3DQUERYTYPE_VCACHE, &query);
    D3DQUERYTYPE query_type = static_cast<D3DQUERYTYPE>(0);
    DWORD query_size = 0;
    HRESULT hr_query_issue = E_FAIL, hr_query_data = E_FAIL;
    D3DDEVINFO_VCACHE vcache = {};
    if (SUCCEEDED(hr_query_create) && query) {
        query_type = reinterpret_cast<PFN_QueryGetType>(Slot(query, 4))(query);
        query_size = reinterpret_cast<PFN_QueryGetDataSize>(Slot(query, 5))(query);
        hr_query_issue = reinterpret_cast<PFN_QueryIssue>(Slot(query, 6))(
            query, D3DISSUE_END);
        hr_query_data = reinterpret_cast<PFN_QueryGetData>(Slot(query, 7))(
            query, &vcache, sizeof(vcache), D3DGETDATA_FLUSH);
        reinterpret_cast<PFN_Release>(Slot(query, 2))(query);
    }
    const bool light_ok = SUCCEEDED(hr_material) && SUCCEEDED(hr_light) &&
        SUCCEEDED(hr_enable) && SUCCEEDED(hr_light_present) && SUCCEEDED(hr_disable) &&
        hr_bad_light == D3DERR_INVALIDCALL && hr_bad_enable == D3DERR_INVALIDCALL;
    const bool query_ok = SUCCEEDED(hr_query_probe) && SUCCEEDED(hr_query_create) &&
        query_type == D3DQUERYTYPE_VCACHE && query_size == sizeof(D3DDEVINFO_VCACHE) &&
        SUCCEEDED(hr_query_issue) && SUCCEEDED(hr_query_data) &&
        vcache.Pattern == MAKEFOURCC('C', 'A', 'C', 'H');
    printf("D3D9WEB_PHASE9_LIGHT material=0x%08lX light=0x%08lX enable=0x%08lX "
           "disable=0x%08lX invalid=%08lX/%08lX result=%s\n",
           static_cast<unsigned long>(hr_material), static_cast<unsigned long>(hr_light),
           static_cast<unsigned long>(hr_enable), static_cast<unsigned long>(hr_disable),
           static_cast<unsigned long>(hr_bad_light), static_cast<unsigned long>(hr_bad_enable),
           light_ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE9_QUERY type=%u size=%lu issue=0x%08lX data=0x%08lX "
           "pattern=0x%08lX result=%s\n", static_cast<unsigned>(query_type),
           static_cast<unsigned long>(query_size), static_cast<unsigned long>(hr_query_issue),
           static_cast<unsigned long>(hr_query_data),
           static_cast<unsigned long>(vcache.Pattern), query_ok ? "PASS" : "FAIL");

    bool mip_ok = texture_levels == 4;
    const LONG expected_pitch[4] = {16, 8, 4, 2};
    const UINT expected_size[4] = {8, 4, 2, 1};
    for (UINT level = 0; level < 4; ++level) {
        mip_ok = mip_ok && SUCCEEDED(hr_tex_lock[level]) &&
            SUCCEEDED(hr_tex_unlock[level]) && texture_pitch[level] == expected_pitch[level] &&
            texture_desc[level].Width == expected_size[level] &&
            texture_desc[level].Height == expected_size[level];
    }
    const bool resource_ok = SUCCEEDED(hr_tex) && SUCCEEDED(hr_vb) && SUCCEEDED(hr_ib) &&
        SUCCEEDED(hr_decl) && mip_ok &&
        SUCCEEDED(hr_vb_lock) && SUCCEEDED(hr_vb_unlock) &&
        SUCCEEDED(hr_ib_lock) && SUCCEEDED(hr_ib_unlock);
    const bool indexed_ok = SUCCEEDED(hr_stage_color_op) && SUCCEEDED(hr_stage_color_a) &&
        SUCCEEDED(hr_stage_color_b) && SUCCEEDED(hr_stage_alpha_op) &&
        SUCCEEDED(hr_stage_alpha_a) && SUCCEEDED(hr_min) && SUCCEEDED(hr_mag) &&
        SUCCEEDED(hr_mip) && SUCCEEDED(hr_bind_texture) && SUCCEEDED(hr_bind_stream) &&
        SUCCEEDED(hr_bind_indices) && SUCCEEDED(hr_bind_declaration) && SUCCEEDED(hr_index_begin) &&
        SUCCEEDED(hr_index_draw) && SUCCEEDED(hr_index_end) && SUCCEEDED(hr_index_present);

    printf("D3D9WEB_PHASE8_RESOURCE tex=23/8x8/levels4/170 pitches=%ld,%ld,%ld,%ld mip=%s "
           "vb=156/52 lock=%s ib=6/index16 lock=%s decl=5/stride52 result=%s\n",
           texture_pitch[0], texture_pitch[1], texture_pitch[2], texture_pitch[3],
           mip_ok ? "PASS" : "FAIL",
           SUCCEEDED(hr_vb_lock) && SUCCEEDED(hr_vb_unlock) ? "PASS" : "FAIL",
           SUCCEEDED(hr_ib_lock) && SUCCEEDED(hr_ib_unlock) ? "PASS" : "FAIL",
           resource_ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE7_COM createTex=0x%08lX createVB=0x%08lX createIB=0x%08lX "
           "bindTex=0x%08lX bindStream=0x%08lX bindIndices=0x%08lX "
           "dip=0x%08lX present=0x%08lX\n",
           static_cast<unsigned long>(hr_tex), static_cast<unsigned long>(hr_vb),
           static_cast<unsigned long>(hr_ib), static_cast<unsigned long>(hr_bind_texture),
           static_cast<unsigned long>(hr_bind_stream),
           static_cast<unsigned long>(hr_bind_indices),
           static_cast<unsigned long>(hr_index_draw),
           static_cast<unsigned long>(hr_index_present));

    set_texture(device, 0, 0);
    set_stream(device, 0, 0, 0, 0);
    set_indices(device, 0);
    set_declaration(device, 0);
    if (texture) reinterpret_cast<PFN_Release>(Slot(texture, 2))(texture);
    if (vertex_buffer) reinterpret_cast<PFN_Release>(Slot(vertex_buffer, 2))(vertex_buffer);
    if (index_buffer) reinterpret_cast<PFN_Release>(Slot(index_buffer, 2))(index_buffer);
    if (declaration) reinterpret_cast<PFN_Release>(Slot(declaration, 2))(declaration);

    printf("D3D9WEB_PHASE6_STATE fvf=0x%08lX stride=%u viewport=%ux%u "
           "scissor=%ldx%ld setters=%u result=%s\n",
           static_cast<unsigned long>(phase6_fvf),
           static_cast<unsigned>(sizeof(Phase6Vertex)),
           viewport_echo.Width, viewport_echo.Height,
           scissor_echo.right, scissor_echo.bottom, state_i,
           state_ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE6_COM create9=1 createDevice=0x%08lX "
           "clear=0x%08lX begin=0x%08lX drawUP=0x%08lX end=0x%08lX present=0x%08lX\n",
           static_cast<unsigned long>(hr_create),
           static_cast<unsigned long>(hr_clear),
           static_cast<unsigned long>(hr_begin),
           static_cast<unsigned long>(hr_draw),
           static_cast<unsigned long>(hr_end),
           static_cast<unsigned long>(hr_present));

    HRESULT hr_begin_state_block = begin_state_block(device);
    HRESULT hr_record_state = set_render_state(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DStateBlock9* state_block = 0;
    HRESULT hr_end_state_block = end_state_block(device, &state_block);
    HRESULT hr_state_capture = E_FAIL;
    HRESULT hr_state_apply = E_FAIL;
    HRESULT hr_state_get_device = E_FAIL;
    if (state_block) {
        hr_state_capture = reinterpret_cast<PFN_StateBlockAction>(
            Slot(state_block, 4))(state_block);
        hr_state_apply = reinterpret_cast<PFN_StateBlockAction>(
            Slot(state_block, 5))(state_block);
        IDirect3DDevice9* state_device = 0;
        hr_state_get_device = reinterpret_cast<PFN_StateBlockGetDevice>(
            Slot(state_block, 3))(state_block, &state_device);
        if (state_device) reinterpret_cast<PFN_Release>(Slot(state_device, 2))(state_device);
        reinterpret_cast<PFN_Release>(Slot(state_block, 2))(state_block);
    }
    reinterpret_cast<PFN_Release>(Slot(device, 2))(device);
    reinterpret_cast<PFN_Release>(Slot(d3d9, 2))(d3d9);

    const bool ok = state_ok && SUCCEEDED(hr_clear) && SUCCEEDED(hr_begin) &&
                    SUCCEEDED(hr_draw) && SUCCEEDED(hr_end) && SUCCEEDED(hr_present) &&
                    resource_ok && indexed_ok && lifecycle_ok && surface_upload_ok &&
                    light_ok && query_ok && SUCCEEDED(hr_begin_state_block) &&
                    SUCCEEDED(hr_record_state) && SUCCEEDED(hr_end_state_block) &&
                    SUCCEEDED(hr_state_capture) && SUCCEEDED(hr_state_apply) &&
                    SUCCEEDED(hr_state_get_device);
    printf("D3D9WEB_PHASE6_RESULT=%s\n", ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE7_RESULT=%s\n", ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE8_RESULT=%s\n", ok ? "PASS" : "FAIL");
    printf("D3D9WEB_PHASE9_RESULT=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 14;
}
