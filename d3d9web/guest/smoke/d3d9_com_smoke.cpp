#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>

namespace {

struct D3D9Object {
    void** vtable;
    volatile LONG refs;
};

struct DeviceObject {
    void** vtable;
    volatile LONG refs;
    DWORD clear_color;
    DWORD clear_calls;
    DWORD begin_calls;
    DWORD end_calls;
    DWORD present_calls;
};

void* g_d3d9_vtable[17] = {};
void* g_device_vtable[119] = {};
D3D9Object g_d3d9 = {g_d3d9_vtable, 1};
DeviceObject g_device = {g_device_vtable, 1, 0, 0, 0, 0, 0};
volatile LONG g_tables_ready = 0;

__declspec(noinline) uint32_t D3D9WebCall(
    uint32_t index,
    uint32_t arg1,
    uint32_t arg2) {
    uint32_t result;
    __asm {
        push arg2
        push arg1
        push index
        _emit 0xCD
        _emit 0x9A
        add esp, 12
        mov result, eax
    }
    return result;
}

HRESULT WINAPI D3D9_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<D3D9Object*>(self)->refs);
    return S_OK;
}

ULONG WINAPI D3D9_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<D3D9Object*>(self)->refs));
}

ULONG WINAPI D3D9_Release(void* self) {
    LONG value = InterlockedDecrement(&static_cast<D3D9Object*>(self)->refs);
    if (value < 0) {
        static_cast<D3D9Object*>(self)->refs = 0;
        value = 0;
    }
    return static_cast<ULONG>(value);
}

HRESULT WINAPI Device_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<DeviceObject*>(self)->refs);
    return S_OK;
}

ULONG WINAPI Device_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<DeviceObject*>(self)->refs));
}

ULONG WINAPI Device_Release(void* self) {
    LONG value = InterlockedDecrement(&static_cast<DeviceObject*>(self)->refs);
    if (value < 0) {
        static_cast<DeviceObject*>(self)->refs = 0;
        value = 0;
    }
    return static_cast<ULONG>(value);
}

HRESULT WINAPI D3D9_CreateDevice(
    void*, UINT, DWORD, HWND, DWORD, void*, void** out_device) {
    if (!out_device) return E_POINTER;
    g_device.refs = 1;
    g_device.clear_color = 0;
    g_device.clear_calls = 0;
    g_device.begin_calls = 0;
    g_device.end_calls = 0;
    g_device.present_calls = 0;
    *out_device = &g_device;
    return S_OK;
}

HRESULT WINAPI Device_Present(
    void* self, const void*, const void*, HWND, const void*) {
    DeviceObject* device = static_cast<DeviceObject*>(self);
    ++device->present_calls;
    const uint32_t result = D3D9WebCall(1, 800, 600);
    return result == 0x3001u ? S_OK : E_FAIL;
}

HRESULT WINAPI Device_BeginScene(void* self) {
    ++static_cast<DeviceObject*>(self)->begin_calls;
    return S_OK;
}

HRESULT WINAPI Device_EndScene(void* self) {
    ++static_cast<DeviceObject*>(self)->end_calls;
    return S_OK;
}

HRESULT WINAPI Device_Clear(
    void* self, DWORD, const RECT*, DWORD, DWORD color, float, DWORD) {
    DeviceObject* device = static_cast<DeviceObject*>(self);
    device->clear_color = color;
    ++device->clear_calls;
    return S_OK;
}

void InitializeTables() {
    if (InterlockedCompareExchange(&g_tables_ready, 1, 0) != 0) return;

    g_d3d9_vtable[0] = reinterpret_cast<void*>(&D3D9_QueryInterface);
    g_d3d9_vtable[1] = reinterpret_cast<void*>(&D3D9_AddRef);
    g_d3d9_vtable[2] = reinterpret_cast<void*>(&D3D9_Release);
    g_d3d9_vtable[16] = reinterpret_cast<void*>(&D3D9_CreateDevice);

    g_device_vtable[0] = reinterpret_cast<void*>(&Device_QueryInterface);
    g_device_vtable[1] = reinterpret_cast<void*>(&Device_AddRef);
    g_device_vtable[2] = reinterpret_cast<void*>(&Device_Release);
    g_device_vtable[17] = reinterpret_cast<void*>(&Device_Present);
    g_device_vtable[41] = reinterpret_cast<void*>(&Device_BeginScene);
    g_device_vtable[42] = reinterpret_cast<void*>(&Device_EndScene);
    g_device_vtable[43] = reinterpret_cast<void*>(&Device_Clear);
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate9(UINT sdk_version) {
    if (sdk_version != 32u) return 0;
    InitializeTables();
    g_d3d9.refs = 1;
    return &g_d3d9;
}
