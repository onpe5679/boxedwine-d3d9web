#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

static void* Slot(void* object, unsigned int index) {
    return (*reinterpret_cast<void***>(object))[index];
}

int main(int argc, char** argv) {
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

    if (argc > 1 && lstrcmpiA(argv[1], "--load-only") == 0) {
        printf("D3D9WEB_PHASE4_LOAD_ONLY create9=1\n");
        reinterpret_cast<PFN_Release>(Slot(d3d9, 2))(d3d9);
        return 0;
    }

    PFN_CreateDevice create_device =
        reinterpret_cast<PFN_CreateDevice>(Slot(d3d9, 16));
    void* device = 0;
    HRESULT hr_create = create_device(d3d9, 0, 1, 0, 0, 0, &device);
    if (FAILED(hr_create) || !device) {
        printf("D3D9WEB_PHASE4_CREATEDEVICE_FAIL hr=0x%08lX device=%p\n",
               static_cast<unsigned long>(hr_create), device);
        return 13;
    }

    PFN_Clear clear = reinterpret_cast<PFN_Clear>(Slot(device, 43));
    PFN_Scene begin_scene = reinterpret_cast<PFN_Scene>(Slot(device, 41));
    PFN_Scene end_scene = reinterpret_cast<PFN_Scene>(Slot(device, 42));
    PFN_Present present = reinterpret_cast<PFN_Present>(Slot(device, 17));

    HRESULT hr_clear = clear(device, 0, 0, 1, 0xFF101828u, 1.0f, 0);
    HRESULT hr_begin = begin_scene(device);
    HRESULT hr_end = end_scene(device);
    HRESULT hr_present = present(device, 0, 0, 0, 0);

    printf("D3D9WEB_PHASE4_COM create9=1 createDevice=0x%08lX "
           "clear=0x%08lX begin=0x%08lX end=0x%08lX present=0x%08lX\n",
           static_cast<unsigned long>(hr_create),
           static_cast<unsigned long>(hr_clear),
           static_cast<unsigned long>(hr_begin),
           static_cast<unsigned long>(hr_end),
           static_cast<unsigned long>(hr_present));

    reinterpret_cast<PFN_Release>(Slot(device, 2))(device);
    reinterpret_cast<PFN_Release>(Slot(d3d9, 2))(d3d9);

    const bool ok = SUCCEEDED(hr_clear) && SUCCEEDED(hr_begin) &&
                    SUCCEEDED(hr_end) && SUCCEEDED(hr_present);
    printf("D3D9WEB_PHASE4_RESULT=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 14;
}
