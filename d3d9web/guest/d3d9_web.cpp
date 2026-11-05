#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <stddef.h>
#include <stdint.h>
#if D3D9WEB_TRACE_COLORWRITE
#include <intrin.h>   // _ReturnAddress, diagnostic colour-write trace only
#endif

#ifndef D3D9WEB_STATE_CACHE_TEXTURE_VIEWPORT
#define D3D9WEB_STATE_CACHE_TEXTURE_VIEWPORT 1
#endif

#ifndef D3D9WEB_STATE_CACHE_TRANSFORM
#define D3D9WEB_STATE_CACHE_TRANSFORM 0
#endif

#ifndef D3D9WEB_STATE_EQUALITY_PROFILE
#define D3D9WEB_STATE_EQUALITY_PROFILE 0
#endif

#ifndef D3D9WEB_GUEST_BATCH
#define D3D9WEB_GUEST_BATCH 0
#endif

#ifndef D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
#define D3D9WEB_TRANSIENT_TEXTURE_RECYCLE 0
#endif

extern "C" int _fltused = 0;

extern "C" void* __cdecl memset(void* destination, int value, size_t bytes) {
    BYTE* out = static_cast<BYTE*>(destination);
    for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<BYTE>(value);
    return destination;
}

extern "C" void* __cdecl memcpy(
    void* destination, const void* source, size_t bytes) {
    volatile BYTE* out = static_cast<volatile BYTE*>(destination);
    const volatile BYTE* in = static_cast<const volatile BYTE*>(source);
    for (size_t i = 0; i < bytes; ++i) out[i] = in[i];
    return destination;
}

namespace {

struct D3D9Object {
    void** vtable;
    volatile LONG refs;
};

struct TextureObject;
struct BufferObject;
struct DeclarationObject;
struct QueryObject;
struct StateBlockObject;

struct DeviceObject {
    void** vtable;
    volatile LONG refs;
    DWORD clear_color;
    DWORD clear_calls;
    DWORD begin_calls;
    DWORD end_calls;
    DWORD present_calls;
    DWORD fvf;
    D3DVIEWPORT9 viewport;
    RECT scissor;
    D3DMATRIX transforms[512];
    DWORD render_states[256];
    BOOL render_state_set[256];
    DWORD texture_stage_states[8][33];
    BOOL texture_stage_state_set[8][33];
    DWORD sampler_states[8][14];
    BOOL sampler_state_set[8][14];
    TextureObject* textures[8];
    BufferObject* stream0;
    UINT stream0_offset;
    UINT stream0_stride;
    BufferObject* indices;
    DeclarationObject* declaration;
    D3DMATERIAL9 material;
    D3DLIGHT9 lights[8];
    BOOL light_enabled[8];
    BOOL cursor_visible;
    D3DFORMAT backbuffer_format;
    BOOL recording_state_block;
    StateBlockObject* recording_block;
    BOOL fvf_valid;
    BOOL transform_valid[512];
    BOOL viewport_valid;
    BOOL texture_bind_valid[8];
    D3DDEVICE_CREATION_PARAMETERS creation_parameters;
    float npatch_mode;
    PALETTEENTRY palettes[16][256];
};

enum D3D9WebCommand : uint32_t {
    COMMAND_INIT = 1,
    COMMAND_CLEAR = 2,
    COMMAND_SET_TRANSFORM = 3,
    COMMAND_SET_VIEWPORT = 4,
    COMMAND_SET_RENDER_STATE = 5,
    COMMAND_SET_TEXTURE_STAGE_STATE = 6,
    COMMAND_SET_SAMPLER_STATE = 7,
    COMMAND_SET_SCISSOR = 8,
    COMMAND_SET_FVF = 9,
    COMMAND_DRAW_PRIMITIVE_UP = 10,
    COMMAND_PRESENT = 11,
    COMMAND_CREATE_TEXTURE = 12,
    COMMAND_CREATE_BUFFER = 13,
    COMMAND_UPLOAD_RESOURCE = 14,
    COMMAND_DESTROY_RESOURCE = 15,
    COMMAND_BIND_TEXTURE = 16,
    COMMAND_BIND_STREAM = 17,
    COMMAND_BIND_INDICES = 18,
    COMMAND_DRAW_INDEXED = 19,
    COMMAND_CREATE_DECLARATION = 20,
    COMMAND_BIND_DECLARATION = 21,
    COMMAND_SET_MATERIAL = 22,
    COMMAND_SET_LIGHT = 23,
    COMMAND_LIGHT_ENABLE = 24,
};

enum ResourceKind : uint32_t {
    RESOURCE_TEXTURE = 1,
    RESOURCE_VERTEX_BUFFER = 2,
    RESOURCE_INDEX_BUFFER = 3,
    RESOURCE_VERTEX_DECLARATION = 4,
};

#pragma pack(push, 1)
struct Command1 { uint32_t command; uint32_t value; };
struct Command2 { uint32_t command; uint32_t value1; uint32_t value2; };
struct Command3 {
    uint32_t command; uint32_t value1; uint32_t value2; uint32_t value3;
};
struct CommandTransform {
    uint32_t command; uint32_t slot; float matrix[16];
};
struct CommandViewport {
    uint32_t command; uint32_t x; uint32_t y; uint32_t width; uint32_t height;
    float min_z; float max_z;
};
struct CommandScissor {
    uint32_t command; int32_t left; int32_t top; int32_t right; int32_t bottom;
};
struct CommandClear {
    uint32_t command; uint32_t count; uint32_t flags; uint32_t color;
    float z; uint32_t stencil;
};
struct CommandDrawUP {
    uint32_t command; uint32_t primitive_type; uint32_t primitive_count;
    uint32_t stride; uint32_t fvf; uint32_t vertex_bytes;
};
struct CommandCreateTexture {
    uint32_t command; uint32_t handle; uint32_t width; uint32_t height;
    uint32_t levels; uint32_t usage; uint32_t format; uint32_t pool;
    uint32_t pitch; uint32_t total_bytes;
};
struct CommandCreateBuffer {
    uint32_t command; uint32_t handle; uint32_t kind; uint32_t length;
    uint32_t usage; uint32_t format_or_fvf; uint32_t pool;
};
struct CommandUploadResource {
    uint32_t command; uint32_t handle; uint32_t kind; uint32_t offset;
    uint32_t total_bytes; uint32_t chunk_bytes; uint32_t final_chunk;
    uint32_t lock_requested_bytes; uint32_t lock_flags; uint32_t lock_hash;
};
struct DirectUploadDescriptor {
    uint32_t handle; uint32_t kind; uint32_t data_pointer;
    uint32_t resource_offset; uint32_t resource_bytes; uint32_t upload_bytes;
    uint32_t final_chunk; uint32_t lock_requested_bytes; uint32_t lock_flags;
    uint32_t lock_hash;
};
struct DirectDrawUPDescriptor {
    uint32_t primitive_type; uint32_t primitive_count; uint32_t stride;
    uint32_t fvf; uint32_t data_pointer; uint32_t vertex_bytes;
};
struct GuestBatchEnvelope {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t record_count;
    uint32_t record_bytes;
    uint32_t reserved;
};
struct CommandPresentProfile {
    uint32_t command; uint32_t present_count;
    uint32_t packet_copy_ticks_low; uint32_t packet_copy_ticks_high;
    uint32_t packet_copy_calls; uint32_t packet_copy_bytes;
    uint32_t upload_int_ticks_low; uint32_t upload_int_ticks_high;
    uint32_t upload_int_calls;
    uint32_t present_int_ticks_low; uint32_t present_int_ticks_high;
    uint32_t present_int_calls;
    uint32_t counter_frequency_low; uint32_t counter_frequency_high;
    uint32_t timer_overhead_ticks_low; uint32_t timer_overhead_ticks_high;
    uint32_t copy_site_calls[7];
    uint32_t copy_site_bytes[7];
#if D3D9WEB_STATE_EQUALITY_PROFILE
    uint32_t state_calls[3];
    uint32_t state_same[3];
#endif
};
struct CommandBindTexture {
    uint32_t command; uint32_t stage; uint32_t handle;
};
struct CommandBindStream {
    uint32_t command; uint32_t stream; uint32_t handle; uint32_t offset;
    uint32_t stride;
};
struct CommandDrawIndexed {
    uint32_t command; uint32_t primitive_type; int32_t base_vertex_index;
    uint32_t min_vertex_index; uint32_t num_vertices; uint32_t start_index;
    uint32_t primitive_count;
};
struct CommandCreateDeclaration {
    uint32_t command; uint32_t handle; uint32_t element_count;
};
struct CommandMaterial { uint32_t command; D3DMATERIAL9 material; };
struct CommandLight { uint32_t command; uint32_t index; D3DLIGHT9 light; };
#pragma pack(pop)

struct ResourceHeader {
    void** vtable;
    volatile LONG refs;
    // Browser delivery is asynchronous at the guest/host boundary.  Keep a
    // zero-ref object mapped for the lifetime of the smoke process so a late
    // duplicate Release or a late Unlock cannot dereference freed heap memory.
    volatile LONG destroy_sent;
    ResourceKind kind;
    uint32_t handle;
    DWORD usage;
    D3DPOOL pool;
    D3DFORMAT format;
    BYTE* data;
    UINT data_bytes;
    UINT lock_offset;
    UINT lock_bytes;
    UINT lock_requested_bytes;
    DWORD lock_flags;
    BOOL locked;
};

struct SurfaceObject {
    void** vtable;
    volatile LONG refs;
    TextureObject* texture;
    UINT level;
};

struct TextureObject {
    ResourceHeader base;
    UINT width;
    UINT height;
    UINT levels;
    UINT level_offsets[16];
    UINT level_widths[16];
    UINT level_heights[16];
    UINT level_pitches[16];
    UINT locked_level;
    DWORD lod;
    D3DTEXTUREFILTERTYPE filter;
    BOOL recycled_needs_full_upload;
    LONG recycle_epoch;
    SurfaceObject surfaces[16];
};

struct TransientTextureSlot {
    volatile LONG state; // 0 empty, 1 being moved, 2 ready
    uint32_t handle;
    UINT width;
    UINT height;
    UINT levels;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    UINT pitch;
    UINT total_bytes;
    LONG epoch;
};

struct BufferObject {
    ResourceHeader base;
    DWORD fvf;
};

struct DeclarationObject {
    void** vtable;
    volatile LONG refs;
    uint32_t handle;
    UINT element_count;
    D3DVERTEXELEMENT9 elements[65];
};

struct QueryObject {
    void** vtable;
    volatile LONG refs;
    D3DQUERYTYPE type;
    BOOL issued;
};

struct StateBlockObject {
    void** vtable;
    volatile LONG refs;
    DeviceObject* device;
    BOOL transform_set[512];
    D3DMATRIX transforms[512];
    BOOL render_state_set[256];
    DWORD render_states[256];
    BOOL texture_stage_state_set[8][33];
    DWORD texture_stage_states[8][33];
    BOOL sampler_state_set[8][14];
    DWORD sampler_states[8][14];
    BOOL viewport_set;
    D3DVIEWPORT9 viewport;
    BOOL scissor_set;
    RECT scissor;
    BOOL material_set;
    D3DMATERIAL9 material;
    BOOL light_set[8];
    D3DLIGHT9 lights[8];
    BOOL light_enabled_set[8];
    BOOL light_enabled[8];
    BOOL texture_set[8];
    TextureObject* textures[8];
    BOOL stream0_set;
    BufferObject* stream0;
    UINT stream0_offset;
    UINT stream0_stride;
    BOOL indices_set;
    BufferObject* indices;
    BOOL declaration_set;
    DeclarationObject* declaration;
    BOOL fvf_set;
    DWORD fvf;
    BOOL vertex_shader_set;
    BOOL pixel_shader_set;
};

void* g_d3d9_vtable[17] = {};
void* g_device_vtable[119] = {};
void* g_texture_vtable[22] = {};
void* g_vertex_buffer_vtable[14] = {};
void* g_index_buffer_vtable[14] = {};
void* g_surface_vtable[17] = {};
void* g_declaration_vtable[5] = {};
void* g_query_vtable[9] = {};
void* g_state_block_vtable[6] = {};
D3D9Object g_d3d9 = {g_d3d9_vtable, 1};
DeviceObject g_device = {};
SurfaceObject g_backbuffer = {};
volatile LONG g_tables_ready = 0;
volatile LONG g_next_resource_handle = 0;
const uint32_t kBridgeSuccess = 0x6001u;
// Wine 11's BoxedWine filesystem already uses low INT 0x9A indices for its
// Vulkan thunk.  Keep the D3D9Web ABI in a disjoint high range so the host can
// leave those Wine probes untouched when Vulkan is disabled in Emscripten.
const uint32_t kD3D9WebIndexBase = 0xD3900000u;
const UINT kMaxCommandBytes = 2u * 1024u * 1024u;
BYTE g_command_bytes[kMaxCommandBytes];
#if D3D9WEB_GUEST_BATCH
const uint32_t kGuestBatchMagic = 0x31423944u; // "D9B1"
const uint32_t kGuestBatchVersion = 1u;
const UINT kGuestBatchCapacity = 64u * 1024u;
BYTE g_guest_batch_bytes[kGuestBatchCapacity];
UINT g_guest_batch_record_bytes = 0;
UINT g_guest_batch_record_count = 0;
BOOL g_guest_batch_enabled = TRUE;
#endif
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
TransientTextureSlot g_transient_texture_available = {};
TransientTextureSlot g_transient_texture_quarantine = {};
BOOL g_transient_texture_recycle_enabled = TRUE;
volatile LONG g_transient_texture_epoch = 1;
void AbandonTransientTextureSlots();
#endif

uint64_t g_profile_packet_copy_ticks = 0;
uint64_t g_profile_upload_int_ticks = 0;
uint64_t g_profile_present_int_ticks = 0;
uint64_t g_profile_counter_frequency = 0;
uint64_t g_profile_timer_overhead_ticks = 0;
uint32_t g_profile_packet_copy_calls = 0;
uint32_t g_profile_packet_copy_bytes = 0;
uint32_t g_profile_upload_int_calls = 0;
uint32_t g_profile_present_int_calls = 0;
enum CopySite {
    COPY_UPDATE_SURFACE = 0,
    COPY_PALETTE,
    COPY_DECLARATION,
    COPY_TRANSFORM,
    COPY_DRAW_UP_HEADER,
    COPY_DRAW_UP_PAYLOAD,
    COPY_CLEAR,
    COPY_SITE_COUNT
};
uint32_t g_copy_site_calls[COPY_SITE_COUNT] = {};
uint32_t g_copy_site_bytes[COPY_SITE_COUNT] = {};
#if D3D9WEB_STATE_EQUALITY_PROFILE
enum StateEqualitySite {
    STATE_EQUALITY_TRANSFORM = 0,
    STATE_EQUALITY_VIEWPORT,
    STATE_EQUALITY_TEXTURE,
    STATE_EQUALITY_SITE_COUNT
};
uint32_t g_state_calls[STATE_EQUALITY_SITE_COUNT] = {};
uint32_t g_state_same[STATE_EQUALITY_SITE_COUNT] = {};
#endif

void TrackCopySite(CopySite site, UINT bytes) {
    ++g_copy_site_calls[site];
    g_copy_site_bytes[site] += bytes;
}

uint64_t ReadProfileCounter() {
    LARGE_INTEGER value = {};
    QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

void InitializeProfileCounter() {
    if (g_profile_counter_frequency) return;
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    g_profile_counter_frequency = static_cast<uint64_t>(frequency.QuadPart);
    uint64_t samples[64] = {};
    for (UINT i = 0; i < 64; ++i) {
        const uint64_t before = ReadProfileCounter();
        const uint64_t after = ReadProfileCounter();
        samples[i] = after - before;
    }
    for (UINT i = 1; i < 64; ++i) {
        const uint64_t value = samples[i];
        UINT position = i;
        while (position && samples[position - 1] > value) {
            samples[position] = samples[position - 1];
            --position;
        }
        samples[position] = value;
    }
    g_profile_timer_overhead_ticks = samples[32];
}

uint32_t CounterLow(uint64_t value) {
    return static_cast<uint32_t>(value);
}

uint32_t CounterHigh(uint64_t value) {
    return static_cast<uint32_t>(value >> 32);
}

__declspec(noinline) uint32_t D3D9WebCall(
    uint32_t index,
    uint32_t arg1,
    uint32_t arg2) {
    uint32_t result;
    __asm {
        xor eax, eax
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

void ByteCopy(void* destination, const void* source, UINT bytes) {
    __asm {
        mov edi, destination
        mov esi, source
        mov ecx, bytes
        mov edx, ecx
        cld
        shr ecx, 2
        rep movsd
        mov ecx, edx
        and ecx, 3
        rep movsb
    }
}

BOOL ByteEqual(const void* left, const void* right, UINT bytes) {
    const BYTE* a = static_cast<const BYTE*>(left);
    const BYTE* b = static_cast<const BYTE*>(right);
    for (UINT i = 0; i < bytes; ++i) {
        if (a[i] != b[i]) return FALSE;
    }
    return TRUE;
}

void InvalidateDeviceHostStateCaches() {
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    // A failed bridge call leaves host resource lifetime ambiguous.  Never
    // reuse a handle from either side of that failed boundary.
    AbandonTransientTextureSlots();
#endif
    g_device.fvf_valid = FALSE;
    g_device.viewport_valid = FALSE;
    for (UINT state = 0; state < 512; ++state) {
        g_device.transform_valid[state] = FALSE;
    }
    for (UINT stage = 0; stage < 8; ++stage) {
        g_device.texture_bind_valid[stage] = FALSE;
        for (UINT type = 0; type < 33; ++type) {
            g_device.texture_stage_state_set[stage][type] = FALSE;
        }
        for (UINT type = 0; type < 14; ++type) {
            g_device.sampler_state_set[stage][type] = FALSE;
        }
    }
    for (UINT state = 0; state < 256; ++state) {
        g_device.render_state_set[state] = FALSE;
    }
}

HRESULT SendCommandImmediate(const void* packet, UINT bytes) {
    const uint32_t result = D3D9WebCall(
        kD3D9WebIndexBase + 2u,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(packet)), bytes);
    return result == kBridgeSuccess ? D3D_OK : E_FAIL;
}

#if D3D9WEB_GUEST_BATCH
BOOL IsGuestBatchable(uint32_t opcode) {
    switch (opcode) {
        case COMMAND_CLEAR:
        case COMMAND_SET_TRANSFORM:
        case COMMAND_SET_VIEWPORT:
        case COMMAND_SET_RENDER_STATE:
        case COMMAND_SET_TEXTURE_STAGE_STATE:
        case COMMAND_SET_SAMPLER_STATE:
        case COMMAND_SET_SCISSOR:
        case COMMAND_SET_FVF:
        case COMMAND_DRAW_PRIMITIVE_UP:
        case COMMAND_UPLOAD_RESOURCE:
        case COMMAND_DESTROY_RESOURCE:
        case COMMAND_BIND_TEXTURE:
        case COMMAND_BIND_STREAM:
        case COMMAND_BIND_INDICES:
        case COMMAND_DRAW_INDEXED:
        case COMMAND_BIND_DECLARATION:
        case COMMAND_SET_MATERIAL:
        case COMMAND_SET_LIGHT:
        case COMMAND_LIGHT_ENABLE:
            return TRUE;
        default:
            return FALSE;
    }
}

void ResetGuestBatch() {
    g_guest_batch_record_bytes = 0;
    g_guest_batch_record_count = 0;
}

BOOL QueueGuestBatchPacket(const void* packet, UINT bytes) {
    const UINT header_bytes = sizeof(GuestBatchEnvelope);
    const uint64_t required = static_cast<uint64_t>(header_bytes) +
        g_guest_batch_record_bytes + sizeof(uint32_t) + bytes;
    if (required > kGuestBatchCapacity) return FALSE;
    BYTE* destination = g_guest_batch_bytes + header_bytes + g_guest_batch_record_bytes;
    const uint32_t packet_bytes = bytes;
    ByteCopy(destination, &packet_bytes, sizeof(packet_bytes));
    ByteCopy(destination + sizeof(packet_bytes), packet, bytes);
    g_guest_batch_record_bytes += sizeof(packet_bytes) + bytes;
    ++g_guest_batch_record_count;
    return TRUE;
}

HRESULT ReplayGuestBatchIndividually() {
    const UINT header_bytes = sizeof(GuestBatchEnvelope);
    UINT offset = 0;
    for (UINT record = 0; record < g_guest_batch_record_count; ++record) {
        if (offset > g_guest_batch_record_bytes ||
            g_guest_batch_record_bytes - offset < sizeof(uint32_t)) return E_FAIL;
        uint32_t packet_bytes = 0;
        ByteCopy(&packet_bytes,
                 g_guest_batch_bytes + header_bytes + offset,
                 sizeof(packet_bytes));
        offset += sizeof(packet_bytes);
        if (packet_bytes < sizeof(uint32_t) ||
            packet_bytes > g_guest_batch_record_bytes - offset) return E_FAIL;
        const HRESULT hr = SendCommandImmediate(
            g_guest_batch_bytes + header_bytes + offset, packet_bytes);
        if (FAILED(hr)) return hr;
        offset += packet_bytes;
    }
    return offset == g_guest_batch_record_bytes ? D3D_OK : E_FAIL;
}

HRESULT FlushGuestBatch() {
    if (!g_guest_batch_record_count) return D3D_OK;
    GuestBatchEnvelope envelope = {
        kGuestBatchMagic, kGuestBatchVersion, 0u, g_guest_batch_record_count,
        g_guest_batch_record_bytes, 0u};
    ByteCopy(g_guest_batch_bytes, &envelope, sizeof(envelope));
    const UINT total_bytes = sizeof(envelope) + g_guest_batch_record_bytes;
    const uint32_t result = D3D9WebCall(
        kD3D9WebIndexBase + 5u,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_guest_batch_bytes)),
        total_bytes);
    if (result == kBridgeSuccess) {
        ResetGuestBatch();
        return D3D_OK;
    }
    if (result == 0u) {
        const HRESULT fallback = ReplayGuestBatchIndividually();
        ResetGuestBatch();
        if (SUCCEEDED(fallback)) g_guest_batch_enabled = FALSE;
        return fallback;
    }
    ResetGuestBatch();
    return E_FAIL;
}
#else
HRESULT FlushGuestBatch() { return D3D_OK; }
#endif

HRESULT SendCommand(const void* packet, UINT bytes) {
    if (!packet || bytes < sizeof(uint32_t) || bytes > kMaxCommandBytes) {
        return D3DERR_INVALIDCALL;
    }
    uint32_t opcode = 0;
    ByteCopy(&opcode, packet, sizeof(opcode));
#if D3D9WEB_GUEST_BATCH
    if (g_guest_batch_enabled && IsGuestBatchable(opcode)) {
        if (QueueGuestBatchPacket(packet, bytes)) return D3D_OK;
        const HRESULT flush = FlushGuestBatch();
        if (FAILED(flush)) {
            InvalidateDeviceHostStateCaches();
            return flush;
        }
        if (g_guest_batch_enabled && QueueGuestBatchPacket(packet, bytes)) return D3D_OK;
    } else {
        const HRESULT flush = FlushGuestBatch();
        if (FAILED(flush)) {
            InvalidateDeviceHostStateCaches();
            return flush;
        }
    }
#endif
    const HRESULT immediate = SendCommandImmediate(packet, bytes);
    if (SUCCEEDED(immediate)) return D3D_OK;
    InvalidateDeviceHostStateCaches();
    return E_FAIL;
}

UINT PrimitiveVertexCount(D3DPRIMITIVETYPE type, UINT primitive_count) {
    switch (type) {
        case D3DPT_POINTLIST: return primitive_count;
        case D3DPT_LINELIST: return primitive_count * 2u;
        case D3DPT_LINESTRIP: return primitive_count + 1u;
        case D3DPT_TRIANGLELIST: return primitive_count * 3u;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN: return primitive_count + 2u;
        default: return 0;
    }
}

UINT BytesPerPixel(D3DFORMAT format) {
    switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: return 4;
        case D3DFMT_R5G6B5:
        case D3DFMT_A4R4G4B4: return 2;
        default: return 0;
    }
}

UINT FullMipCount(UINT width, UINT height) {
    UINT levels = 1;
    while ((width > 1 || height > 1) && levels < 16) {
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
        ++levels;
    }
    return levels;
}

BOOL BuildTextureLayout(TextureObject* texture, UINT width, UINT height,
                        UINT levels, UINT bpp, UINT* total_bytes) {
    if (!texture || !total_bytes || !levels || levels > 16) return FALSE;
    uint64_t total = 0;
    for (UINT level = 0; level < levels; ++level) {
        const UINT level_width = width > 1 ? width : 1;
        const UINT level_height = height > 1 ? height : 1;
        const uint64_t pitch = static_cast<uint64_t>(level_width) * bpp;
        const uint64_t bytes = pitch * level_height;
        if (pitch > 0xFFFFFFFFu || total + bytes > 0xFFFFFFFFu) return FALSE;
        texture->level_offsets[level] = static_cast<UINT>(total);
        texture->level_widths[level] = level_width;
        texture->level_heights[level] = level_height;
        texture->level_pitches[level] = static_cast<UINT>(pitch);
        total += bytes;
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
    }
    *total_bytes = static_cast<UINT>(total);
    return TRUE;
}

HRESULT UploadResource(
    uint32_t handle, ResourceKind kind, const BYTE* data, UINT resource_bytes,
    UINT resource_offset, UINT upload_bytes, UINT lock_requested_bytes = 0,
    DWORD lock_flags = 0, UINT lock_hash = 0) {
    if (!data || !resource_bytes || !upload_bytes ||
        resource_offset > resource_bytes ||
        upload_bytes > resource_bytes - resource_offset) return D3DERR_INVALIDCALL;
    const HRESULT pending = FlushGuestBatch();
    if (FAILED(pending)) {
        InvalidateDeviceHostStateCaches();
        return pending;
    }
    const UINT max_chunk = kMaxCommandBytes - sizeof(CommandUploadResource);
    UINT offset = 0;
    while (offset < upload_bytes) {
        UINT chunk = upload_bytes - offset;
        if (chunk > max_chunk) chunk = max_chunk;
        DirectUploadDescriptor descriptor = {
            handle, static_cast<uint32_t>(kind),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data + offset)),
            resource_offset + offset, resource_bytes, chunk,
            offset + chunk == upload_bytes ? 1u : 0u,
            lock_requested_bytes, lock_flags, lock_hash};
        const BOOL profile_large_packet = chunk >= 4096u;
        if (profile_large_packet) InitializeProfileCounter();
        const uint64_t int_start = profile_large_packet ? ReadProfileCounter() : 0;
        const uint32_t result = D3D9WebCall(
            kD3D9WebIndexBase + 3u,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&descriptor)),
            sizeof(descriptor));
        HRESULT hr = result == kBridgeSuccess ? D3D_OK : E_FAIL;
        if (profile_large_packet) {
            g_profile_upload_int_ticks += ReadProfileCounter() - int_start;
            ++g_profile_upload_int_calls;
        }
        if (FAILED(hr)) {
            InvalidateDeviceHostStateCaches();
            return hr;
        }
        offset += chunk;
    }
    return D3D_OK;
}

uint32_t AllocateResourceHandle() {
    return static_cast<uint32_t>(InterlockedIncrement(&g_next_resource_handle));
}

#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
BOOL IsMeasuredTransientTexture(
    UINT width, UINT height, UINT levels, D3DFORMAT format, UINT pitch,
    UINT total_bytes) {
    return width == 256u && height == 256u && levels == 1u &&
           format == D3DFMT_R5G6B5 && pitch == 512u &&
           total_bytes == 131072u;
}

BOOL TransientTextureDescriptorMatches(
    const TransientTextureSlot* slot, UINT width, UINT height, UINT levels,
    DWORD usage, D3DFORMAT format, D3DPOOL pool, UINT pitch,
    UINT total_bytes) {
    return slot && slot->state == 2 && slot->width == width &&
           slot->height == height && slot->levels == levels &&
           slot->usage == usage && slot->format == format &&
           slot->pool == pool && slot->pitch == pitch &&
           slot->total_bytes == total_bytes;
}

BOOL TryQuarantineTransientTexture(TextureObject* texture) {
    if (!g_transient_texture_recycle_enabled || !texture ||
        texture->recycle_epoch != g_transient_texture_epoch ||
        texture->base.locked ||
        !IsMeasuredTransientTexture(
            texture->width, texture->height, texture->levels,
            texture->base.format, texture->level_pitches[0],
            texture->base.data_bytes) ||
        g_transient_texture_available.state != 0 ||
        InterlockedCompareExchange(
            &g_transient_texture_quarantine.state, 1, 0) != 0) {
        return FALSE;
    }
    const LONG epoch = g_transient_texture_epoch;
    TransientTextureSlot* slot = &g_transient_texture_quarantine;
    slot->handle = texture->base.handle;
    slot->width = texture->width;
    slot->height = texture->height;
    slot->levels = texture->levels;
    slot->usage = texture->base.usage;
    slot->format = texture->base.format;
    slot->pool = texture->base.pool;
    slot->pitch = texture->level_pitches[0];
    slot->total_bytes = texture->base.data_bytes;
    slot->epoch = epoch;
    if (g_transient_texture_recycle_enabled &&
        g_transient_texture_epoch == epoch &&
        InterlockedCompareExchange(&slot->state, 2, 1) == 1) {
        return TRUE;
    }
    InterlockedCompareExchange(&slot->state, 0, 1);
    return FALSE;
}

BOOL TryAcquireTransientTextureHandle(
    UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
    D3DPOOL pool, UINT pitch, UINT total_bytes, uint32_t* handle,
    LONG* claimed_epoch) {
    if (!g_transient_texture_recycle_enabled || !handle || !claimed_epoch ||
        !IsMeasuredTransientTexture(
            width, height, levels, format, pitch, total_bytes) ||
        !TransientTextureDescriptorMatches(
            &g_transient_texture_available, width, height, levels, usage,
            format, pool, pitch, total_bytes) ||
        g_transient_texture_available.epoch != g_transient_texture_epoch ||
        InterlockedCompareExchange(
            &g_transient_texture_available.state, 1, 2) != 2) {
        return FALSE;
    }
    const LONG epoch = g_transient_texture_available.epoch;
    const uint32_t claimed_handle = g_transient_texture_available.handle;
    if (!g_transient_texture_recycle_enabled ||
        epoch != g_transient_texture_epoch ||
        InterlockedCompareExchange(
            &g_transient_texture_available.state, 0, 1) != 1 ||
        !g_transient_texture_recycle_enabled ||
        epoch != g_transient_texture_epoch) {
        InterlockedCompareExchange(&g_transient_texture_available.state, 0, 1);
        return FALSE;
    }
    *handle = claimed_handle;
    *claimed_epoch = epoch;
    return TRUE;
}

void PromoteTransientTextureAfterPresent() {
    if (!g_transient_texture_recycle_enabled ||
        g_transient_texture_quarantine.epoch != g_transient_texture_epoch ||
        g_transient_texture_available.state != 0 ||
        InterlockedCompareExchange(
            &g_transient_texture_quarantine.state, 1, 2) != 2) {
        return;
    }
    if (InterlockedCompareExchange(
            &g_transient_texture_available.state, 1, 0) != 0) {
        InterlockedCompareExchange(&g_transient_texture_quarantine.state, 2, 1);
        return;
    }
    g_transient_texture_available.handle = g_transient_texture_quarantine.handle;
    g_transient_texture_available.width = g_transient_texture_quarantine.width;
    g_transient_texture_available.height = g_transient_texture_quarantine.height;
    g_transient_texture_available.levels = g_transient_texture_quarantine.levels;
    g_transient_texture_available.usage = g_transient_texture_quarantine.usage;
    g_transient_texture_available.format = g_transient_texture_quarantine.format;
    g_transient_texture_available.pool = g_transient_texture_quarantine.pool;
    g_transient_texture_available.pitch = g_transient_texture_quarantine.pitch;
    g_transient_texture_available.total_bytes =
        g_transient_texture_quarantine.total_bytes;
    g_transient_texture_available.epoch = g_transient_texture_quarantine.epoch;
    if (!g_transient_texture_recycle_enabled ||
        g_transient_texture_available.epoch != g_transient_texture_epoch ||
        InterlockedCompareExchange(
            &g_transient_texture_available.state, 2, 1) != 1) {
        InterlockedCompareExchange(&g_transient_texture_available.state, 0, 1);
        InterlockedCompareExchange(&g_transient_texture_quarantine.state, 0, 1);
        return;
    }
    InterlockedCompareExchange(&g_transient_texture_quarantine.state, 0, 1);
}

HRESULT DestroyTransientTextureSlot(TransientTextureSlot* slot) {
    if (!slot || InterlockedCompareExchange(&slot->state, 1, 2) != 2) {
        return D3D_OK;
    }
    const uint32_t handle = slot->handle;
    InterlockedExchange(&slot->state, 0);
    Command2 command = {COMMAND_DESTROY_RESOURCE, handle, RESOURCE_TEXTURE};
    return SendCommand(&command, sizeof(command));
}

HRESULT DrainTransientTextureSlots() {
    HRESULT result = DestroyTransientTextureSlot(&g_transient_texture_available);
    const HRESULT quarantine =
        DestroyTransientTextureSlot(&g_transient_texture_quarantine);
    return FAILED(result) ? result : quarantine;
}

void AbandonTransientTextureSlots() {
    g_transient_texture_recycle_enabled = FALSE;
    InterlockedIncrement(&g_transient_texture_epoch);
    InterlockedExchange(&g_transient_texture_available.state, 0);
    InterlockedExchange(&g_transient_texture_quarantine.state, 0);
}

HRESULT EnsureRecycledTextureUploaded(TextureObject* texture) {
    if (!texture || !texture->recycled_needs_full_upload) return D3D_OK;
    const HRESULT result = UploadResource(
        texture->base.handle, RESOURCE_TEXTURE, texture->base.data,
        texture->base.data_bytes, 0, texture->base.data_bytes);
    if (SUCCEEDED(result)) texture->recycled_needs_full_upload = FALSE;
    return result;
}
#endif

LONG g_d3d9_trace_seen[17] = {};
LONG g_device_trace_sequence = 0;

void TraceD3D9Once(UINT slot, UINT value) {
    (void)slot;
    (void)value;
}

void TraceDeviceCall(UINT slot, UINT value) {
    (void)slot;
    (void)value;
}

void DebugRejectHex(const char* reason, UINT value) {
    static const char hex[] = "0123456789ABCDEF";
    char line[96];
    UINT pos = 0;
    while (reason[pos] && pos + 12 < sizeof(line)) {
        line[pos] = reason[pos];
        ++pos;
    }
    line[pos++] = '0';
    line[pos++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) {
        line[pos++] = hex[(value >> shift) & 0xFu];
    }
    line[pos++] = '\n';
    line[pos] = 0;
    OutputDebugStringA(line);
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

void ByteZero(void* destination, UINT bytes) {
    BYTE* out = static_cast<BYTE*>(destination);
    for (UINT i = 0; i < bytes; ++i) out[i] = 0;
}

HRESULT WINAPI D3D9_RegisterSoftwareDevice(void*, void*) {
    TraceD3D9Once(3, 0);
    return D3D_OK;
}

UINT WINAPI D3D9_GetAdapterCount(void*) {
    TraceD3D9Once(4, 1);
    return 1;
}

HRESULT WINAPI D3D9_GetAdapterIdentifier(
    void*, UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9* identifier) {
    TraceD3D9Once(5, adapter);
    if (adapter != D3DADAPTER_DEFAULT || !identifier) return D3DERR_INVALIDCALL;
    ByteZero(identifier, sizeof(*identifier));
    lstrcpynA(identifier->Driver, "d3d9web.dll", MAX_DEVICE_IDENTIFIER_STRING);
    lstrcpynA(identifier->Description, "D3D9Web Browser Adapter",
              MAX_DEVICE_IDENTIFIER_STRING);
    lstrcpynA(identifier->DeviceName, "\\\\.\\DISPLAY1",
              sizeof(identifier->DeviceName));
    identifier->DriverVersion.QuadPart = 1;
    identifier->VendorId = 0xFFFFu;
    identifier->DeviceId = 0x0009u;
    identifier->SubSysId = 0;
    identifier->Revision = 1;
    identifier->DeviceIdentifier.Data1 = 0xD3D90001u;
    identifier->WHQLLevel = 0;
    return D3D_OK;
}

UINT WINAPI D3D9_GetAdapterModeCount(void*, UINT adapter, D3DFORMAT format) {
    TraceD3D9Once(6, static_cast<UINT>(format));
    if (adapter != D3DADAPTER_DEFAULT || format == D3DFMT_UNKNOWN) return 0;
    return 1;
}

HRESULT WINAPI D3D9_EnumAdapterModes(
    void*, UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE* display) {
    TraceD3D9Once(7, static_cast<UINT>(format));
    if (adapter != D3DADAPTER_DEFAULT || mode != 0 || !display ||
        format == D3DFMT_UNKNOWN) {
        return D3DERR_INVALIDCALL;
    }
    display->Width = 800;
    display->Height = 600;
    display->RefreshRate = 60;
    display->Format = format;
    return D3D_OK;
}

HRESULT WINAPI D3D9_GetAdapterDisplayMode(
    void*, UINT adapter, D3DDISPLAYMODE* display) {
    TraceD3D9Once(8, adapter);
    if (adapter != D3DADAPTER_DEFAULT || !display) return D3DERR_INVALIDCALL;
    display->Width = 800;
    display->Height = 600;
    display->RefreshRate = 60;
    display->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT WINAPI D3D9_CheckDeviceType(
    void*, UINT adapter, D3DDEVTYPE, D3DFORMAT adapter_format,
    D3DFORMAT backbuffer_format, BOOL) {
    TraceD3D9Once(9, static_cast<UINT>(backbuffer_format));
    if (adapter != D3DADAPTER_DEFAULT || adapter_format == D3DFMT_UNKNOWN ||
        backbuffer_format == D3DFMT_UNKNOWN) {
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT WINAPI D3D9_CheckDeviceFormat(
    void*, UINT adapter, D3DDEVTYPE, D3DFORMAT adapter_format, DWORD,
    D3DRESOURCETYPE resource_type, D3DFORMAT checked_format) {
    TraceD3D9Once(10, static_cast<UINT>(checked_format));
    if (adapter != D3DADAPTER_DEFAULT || adapter_format == D3DFMT_UNKNOWN ||
        checked_format == D3DFMT_UNKNOWN) {
        return D3DERR_NOTAVAILABLE;
    }
    // Acknex uses CheckDeviceFormat as a fallback selector before calling
    // CreateTexture.  Advertising formats that CreateTexture cannot create
    // prevents that fallback and turns a recoverable choice into a late
    // failure.  Keep the texture capability contract truthful.
    if (resource_type == D3DRTYPE_TEXTURE && !BytesPerPixel(checked_format)) {
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT WINAPI D3D9_CheckDeviceMultiSampleType(
    void*, UINT adapter, D3DDEVTYPE, D3DFORMAT surface_format, BOOL,
    D3DMULTISAMPLE_TYPE multisample, DWORD* quality_levels) {
    TraceD3D9Once(11, static_cast<UINT>(multisample));
    if (quality_levels) *quality_levels = multisample == D3DMULTISAMPLE_NONE ? 1 : 0;
    if (adapter != D3DADAPTER_DEFAULT || surface_format == D3DFMT_UNKNOWN ||
        multisample != D3DMULTISAMPLE_NONE) {
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT WINAPI D3D9_CheckDepthStencilMatch(
    void*, UINT adapter, D3DDEVTYPE, D3DFORMAT adapter_format,
    D3DFORMAT render_target_format, D3DFORMAT depth_format) {
    TraceD3D9Once(12, static_cast<UINT>(depth_format));
    if (adapter != D3DADAPTER_DEFAULT || adapter_format == D3DFMT_UNKNOWN ||
        render_target_format == D3DFMT_UNKNOWN || depth_format == D3DFMT_UNKNOWN) {
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT WINAPI D3D9_CheckDeviceFormatConversion(
    void*, UINT adapter, D3DDEVTYPE, D3DFORMAT source, D3DFORMAT target) {
    TraceD3D9Once(13, static_cast<UINT>(target));
    if (adapter != D3DADAPTER_DEFAULT || source == D3DFMT_UNKNOWN ||
        target == D3DFMT_UNKNOWN) {
        return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT WINAPI D3D9_GetDeviceCaps(
    void*, UINT adapter, D3DDEVTYPE device_type, D3DCAPS9* caps) {
    TraceD3D9Once(14, static_cast<UINT>(device_type));
    if (adapter != D3DADAPTER_DEFAULT || !caps) return D3DERR_INVALIDCALL;
    ByteZero(caps, sizeof(*caps));
    caps->DeviceType = device_type;
    caps->AdapterOrdinal = adapter;
    caps->Caps = D3DCAPS_READ_SCANLINE;
    caps->Caps2 = D3DCAPS2_CANMANAGERESOURCE | D3DCAPS2_DYNAMICTEXTURES |
                  D3DCAPS2_FULLSCREENGAMMA;
    caps->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE |
                                  D3DPRESENT_INTERVAL_IMMEDIATE;
    caps->CursorCaps = D3DCURSORCAPS_COLOR | D3DCURSORCAPS_LOWRES;
    caps->DevCaps = D3DDEVCAPS_HWRASTERIZATION |
                    D3DDEVCAPS_HWTRANSFORMANDLIGHT |
                    D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                    D3DDEVCAPS_TEXTUREVIDEOMEMORY;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW |
                              D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE |
                              D3DPMISCCAPS_BLENDOP;
    caps->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST |
                       D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_FOGVERTEX |
                       D3DPRASTERCAPS_FOGTABLE;
    caps->ZCmpCaps = 0xFFu;
    caps->SrcBlendCaps = 0x3FFFu;
    caps->DestBlendCaps = 0x3FFFu;
    caps->AlphaCmpCaps = 0xFFu;
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB |
                      D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    // Acknex uses this capability bit when deciding whether material textures
    // are created with their source mip chain.  The native adapter advertises
    // mipmaps and the PatHouse carpet's stage-1 texture is consequently
    // 512x512 with four levels.  Omitting the bit made the browser guest ask
    // for a single level even though both the proxy and WebGL2 support mips.
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA |
                        D3DPTEXTURECAPS_PERSPECTIVE |
                        D3DPTEXTURECAPS_MIPMAP;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT |
                              D3DPTFILTERCAPS_MINFLINEAR |
                              D3DPTFILTERCAPS_MAGFPOINT |
                              D3DPTFILTERCAPS_MAGFLINEAR |
                              D3DPTFILTERCAPS_MIPFPOINT |
                              D3DPTFILTERCAPS_MIPFLINEAR;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_CLAMP |
                               D3DPTADDRESSCAPS_MIRROR;
    // Gate 1's native adapter reports 0x03FEFFFF.  The WebGL2 FFP shader
    // implements this D3DTOP set, and Acknex requires ADD/MODULATE2X plus at
    // least two texture stages before it publishes D3D_MODE >= 3.
    caps->TextureOpCaps = 0x03FEFFFFu;
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST |
                     D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP;
    caps->MaxTextureWidth = 4096;
    caps->MaxTextureHeight = 4096;
    caps->MaxVolumeExtent = 1;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 4096;
    caps->MaxAnisotropy = 1;
    caps->MaxVertexW = 1.0e10f;
    caps->GuardBandLeft = -1.0e10f;
    caps->GuardBandTop = -1.0e10f;
    caps->GuardBandRight = 1.0e10f;
    caps->GuardBandBottom = 1.0e10f;
    caps->MaxTextureBlendStages = 8;
    caps->MaxSimultaneousTextures = 8;
    caps->MaxActiveLights = 8;
    caps->MaxUserClipPlanes = 0;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    caps->MaxPrimitiveCount = 0xFFFFFu;
    caps->MaxVertexIndex = 0xFFFFFFu;
    caps->MaxStreams = 8;
    caps->MaxStreamStride = 255;
    // Match the native D3D9 capability baseline used by the original client.
    // Acknex publishes these two fields as D3D_SHADERVERSION, and Azrok exits
    // before its first rendered frame when that value is below 3.
    caps->VertexShaderVersion = 0xFFFE0300u;  // D3DVS_VERSION(3, 0)
    caps->MaxVertexShaderConst = 256;
    caps->PixelShaderVersion = 0xFFFF0300u;   // D3DPS_VERSION(3, 0)
    caps->PixelShader1xMaxValue = 1.0f;
    return D3D_OK;
}

HMONITOR WINAPI D3D9_GetAdapterMonitor(void*, UINT adapter) {
    TraceD3D9Once(15, adapter);
    if (adapter != D3DADAPTER_DEFAULT) return 0;
    // D3D9 exposes HMONITOR as an opaque identity. Calling user32's monitor
    // discovery in the browser root enters Wine's unavailable X11 path.
    return reinterpret_cast<HMONITOR>(1);
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
    const BOOL reached_zero = value == 0;
    if (value < 0) {
        static_cast<DeviceObject*>(self)->refs = 0;
        value = 0;
    }
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (reached_zero) DrainTransientTextureSlots();
#endif
    return static_cast<ULONG>(value);
}

HRESULT WINAPI Device_TestCooperativeLevel(void*) {
    TraceDeviceCall(3, 0);
    return D3D_OK;
}

UINT WINAPI Device_GetAvailableTextureMem(void*) { return 256u * 1024u * 1024u; }

HRESULT WINAPI Device_GetDirect3D(void*, IDirect3D9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    D3D9_AddRef(&g_d3d9);
    *out = reinterpret_cast<IDirect3D9*>(&g_d3d9);
    return D3D_OK;
}

HRESULT WINAPI Device_GetDeviceCaps(void*, D3DCAPS9* caps) {
    return D3D9_GetDeviceCaps(&g_d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, caps);
}

HRESULT WINAPI Device_GetDisplayMode(void* self, UINT swap_chain, D3DDISPLAYMODE* mode) {
    if (swap_chain != 0 || !mode) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    mode->Width = device->viewport.Width;
    mode->Height = device->viewport.Height;
    mode->RefreshRate = 60;
    mode->Format = device->backbuffer_format;
    return D3D_OK;
}

HRESULT WINAPI Device_GetCreationParameters(
    void* self, D3DDEVICE_CREATION_PARAMETERS* parameters) {
    TraceDeviceCall(9, 0);
    if (!parameters) return D3DERR_INVALIDCALL;
    *parameters = static_cast<DeviceObject*>(self)->creation_parameters;
    return D3D_OK;
}

BOOL WINAPI Device_ShowCursor(void* self, BOOL show) {
    DeviceObject* device = static_cast<DeviceObject*>(self);
    const BOOL previous = device->cursor_visible;
    device->cursor_visible = show;
    return previous;
}

HRESULT WINAPI Device_Reset(void* self, D3DPRESENT_PARAMETERS* present) {
    TraceDeviceCall(16, present ? present->BackBufferWidth : 0u);
    if (!present) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    const HRESULT drain = DrainTransientTextureSlots();
    if (FAILED(drain)) return drain;
#endif
    InvalidateDeviceHostStateCaches();
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = present->BackBufferWidth ? present->BackBufferWidth : 800;
    device->viewport.Height = present->BackBufferHeight ? present->BackBufferHeight : 600;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device->scissor.left = 0;
    device->scissor.top = 0;
    device->scissor.right = static_cast<LONG>(device->viewport.Width);
    device->scissor.bottom = static_cast<LONG>(device->viewport.Height);
    device->backbuffer_format = present->BackBufferFormat == D3DFMT_UNKNOWN ?
        D3DFMT_X8R8G8B8 : present->BackBufferFormat;
    Command2 command = {COMMAND_INIT, device->viewport.Width, device->viewport.Height};
    const HRESULT result = SendCommand(&command, sizeof(command));
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (SUCCEEDED(result)) g_transient_texture_recycle_enabled = TRUE;
#endif
    return result;
}

HRESULT WINAPI Device_GetBackBuffer(
    void*, UINT swap_chain, UINT back_buffer, D3DBACKBUFFER_TYPE type,
    IDirect3DSurface9** out) {
    if (!out || swap_chain != 0 || back_buffer != 0 ||
        type != D3DBACKBUFFER_TYPE_MONO) return D3DERR_INVALIDCALL;
    InterlockedIncrement(&g_backbuffer.refs);
    *out = reinterpret_cast<IDirect3DSurface9*>(&g_backbuffer);
    return D3D_OK;
}

HRESULT WINAPI Device_GetRenderTarget(
    void*, DWORD index, IDirect3DSurface9** out) {
    TraceDeviceCall(38, index);
    if (!out || index != 0) return D3DERR_INVALIDCALL;
    InterlockedIncrement(&g_backbuffer.refs);
    *out = reinterpret_cast<IDirect3DSurface9*>(&g_backbuffer);
    return D3D_OK;
}

HRESULT WINAPI Device_UpdateSurface(
    void*, IDirect3DSurface9* source_surface, const RECT* source_rect,
    IDirect3DSurface9* destination_surface, const POINT* destination_point) {
    TraceDeviceCall(30, 0);
    if (!source_surface || !destination_surface) return D3DERR_INVALIDCALL;

    SurfaceObject* source = reinterpret_cast<SurfaceObject*>(source_surface);
    SurfaceObject* destination = reinterpret_cast<SurfaceObject*>(destination_surface);
    if (!source->texture || !destination->texture ||
        source->level >= source->texture->levels ||
        destination->level >= destination->texture->levels ||
        source->texture->base.format != destination->texture->base.format) {
        return D3DERR_INVALIDCALL;
    }

    TextureObject* source_texture = source->texture;
    TextureObject* destination_texture = destination->texture;
    if (source_texture->base.destroy_sent ||
        destination_texture->base.destroy_sent ||
        !source_texture->base.data || !destination_texture->base.data) {
        return D3DERR_INVALIDCALL;
    }
    const UINT source_width = source_texture->level_widths[source->level];
    const UINT source_height = source_texture->level_heights[source->level];
    const UINT destination_width = destination_texture->level_widths[destination->level];
    const UINT destination_height = destination_texture->level_heights[destination->level];
    RECT area = {0, 0, static_cast<LONG>(source_width), static_cast<LONG>(source_height)};
    if (source_rect) area = *source_rect;
    POINT point = {0, 0};
    if (destination_point) point = *destination_point;
    if (area.left < 0 || area.top < 0 || area.right <= area.left ||
        area.bottom <= area.top || area.right > static_cast<LONG>(source_width) ||
        area.bottom > static_cast<LONG>(source_height) || point.x < 0 || point.y < 0) {
        return D3DERR_INVALIDCALL;
    }

    const UINT copy_width = static_cast<UINT>(area.right - area.left);
    const UINT copy_height = static_cast<UINT>(area.bottom - area.top);
    if (static_cast<UINT>(point.x) > destination_width ||
        static_cast<UINT>(point.y) > destination_height ||
        copy_width > destination_width - static_cast<UINT>(point.x) ||
        copy_height > destination_height - static_cast<UINT>(point.y)) {
        return D3DERR_INVALIDCALL;
    }
    const UINT bpp = BytesPerPixel(source_texture->base.format);
    if (!bpp || source_texture == destination_texture) return D3DERR_INVALIDCALL;

    const UINT source_pitch = source_texture->level_pitches[source->level];
    const UINT destination_pitch = destination_texture->level_pitches[destination->level];
    const BYTE* source_bytes = source_texture->base.data +
        source_texture->level_offsets[source->level] +
        static_cast<UINT>(area.top) * source_pitch +
        static_cast<UINT>(area.left) * bpp;
    BYTE* destination_bytes = destination_texture->base.data +
        destination_texture->level_offsets[destination->level] +
        static_cast<UINT>(point.y) * destination_pitch +
        static_cast<UINT>(point.x) * bpp;
    const UINT row_bytes = copy_width * bpp;
    for (UINT row = 0; row < copy_height; ++row) {
        TrackCopySite(COPY_UPDATE_SURFACE, row_bytes);
        ByteCopy(destination_bytes + row * destination_pitch,
                 source_bytes + row * source_pitch, row_bytes);
    }

    // The host currently validates texture uploads as a complete retained
    // resource. Upload the destination backing after the CPU-side D3D copy;
    // this also preserves mip levels populated by earlier UpdateSurface calls.
    const HRESULT result = UploadResource(
        destination_texture->base.handle, RESOURCE_TEXTURE,
        destination_texture->base.data, destination_texture->base.data_bytes,
        0, destination_texture->base.data_bytes);
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (SUCCEEDED(result)) {
        destination_texture->recycled_needs_full_upload = FALSE;
    }
#endif
    return result;
}

HRESULT WINAPI Device_SetMaterial(void* self, const D3DMATERIAL9* material) {
    TraceDeviceCall(49, material ? 1u : 0u);
    if (!material) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->material_set = TRUE;
        device->recording_block->material = *material;
        return D3D_OK;
    }
    device->material = *material;
    CommandMaterial command = {COMMAND_SET_MATERIAL, *material};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_SetLight(void* self, DWORD index, const D3DLIGHT9* light) {
    TraceDeviceCall(51, index);
    if (!light || index >= 8) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->light_set[index] = TRUE;
        device->recording_block->lights[index] = *light;
        return D3D_OK;
    }
    device->lights[index] = *light;
    CommandLight command = {COMMAND_SET_LIGHT, index, *light};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_LightEnable(void* self, DWORD index, BOOL enable) {
    TraceDeviceCall(53, index);
    if (index >= 8) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->light_enabled_set[index] = TRUE;
        device->recording_block->light_enabled[index] = enable;
        return D3D_OK;
    }
    device->light_enabled[index] = enable;
    Command3 command = {COMMAND_LIGHT_ENABLE, index, enable ? 1u : 0u, 0};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_BeginStateBlock(void* self) {
    TraceDeviceCall(60, 0);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_state_block || device->recording_block) {
        return D3DERR_INVALIDCALL;
    }
    StateBlockObject* block = static_cast<StateBlockObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(StateBlockObject)));
    if (!block) return E_OUTOFMEMORY;
    block->vtable = g_state_block_vtable;
    block->refs = 1;
    block->device = device;
    Device_AddRef(device);
    device->recording_block = block;
    device->recording_state_block = TRUE;
    return D3D_OK;
}

HRESULT WINAPI Device_ValidateDevice(void*, DWORD* passes) {
    TraceDeviceCall(70, 0);
    if (!passes) return D3DERR_INVALIDCALL;
    *passes = 1;
    return D3D_OK;
}

HRESULT WINAPI Device_SetPaletteEntries(
    void* self, UINT palette_number, const PALETTEENTRY* entries) {
    TraceDeviceCall(71, palette_number);
    if (!entries || palette_number >= 16) return D3DERR_INVALIDCALL;
    TrackCopySite(COPY_PALETTE, 256 * sizeof(PALETTEENTRY));
    ByteCopy(static_cast<DeviceObject*>(self)->palettes[palette_number], entries,
             256 * sizeof(PALETTEENTRY));
    return D3D_OK;
}

HRESULT WINAPI Resource_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<ResourceHeader*>(self)->refs);
    return S_OK;
}

ULONG WINAPI Resource_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<ResourceHeader*>(self)->refs));
}

ULONG WINAPI Resource_Release(void* self) {
    ResourceHeader* resource = static_cast<ResourceHeader*>(self);
    LONG refs = InterlockedDecrement(&resource->refs);
    if (refs > 0) return static_cast<ULONG>(refs);
    // Keep a small tombstone after the last guest-side Release so a late
    // duplicate Release cannot dereference a freed COM header. Emit host
    // retirement only once and reclaim the large backing separately below.
    resource->refs = 0;
    if (InterlockedCompareExchange(&resource->destroy_sent, 1, 0) == 0) {
        BOOL recycled = FALSE;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
        if (resource->kind == RESOURCE_TEXTURE) {
            recycled = TryQuarantineTransientTexture(
                reinterpret_cast<TextureObject*>(resource));
        }
#endif
        if (!recycled) {
            Command2 command = {
                COMMAND_DESTROY_RESOURCE, resource->handle,
                static_cast<uint32_t>(resource->kind)};
            SendCommand(&command, sizeof(command));
        }
        // UploadResource and DrawPrimitiveUP snapshot all caller-owned bytes
        // into the BoxedWine batch before returning.  The host therefore
        // cannot retain this guest backing after the last COM reference is
        // released.  Keep only the small tombstone object for defensive late
        // duplicate calls; retaining the backing here leaked one 128 KiB
        // temporary texture per PatHouse frame and eventually exhausted the
        // fixed 1 GiB WASM heap.
        if (resource->data && !resource->locked) {
            HeapFree(GetProcessHeap(), 0, resource->data);
            resource->data = 0;
        } else if (resource->data) {
            DebugRejectHex("D3D9WEB_RESOURCE_RELEASE_LOCKED ", resource->handle);
        }
    }
    return 0;
}

HRESULT WINAPI Resource_GetDevice(void*, IDirect3DDevice9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    Device_AddRef(&g_device);
    *out = reinterpret_cast<IDirect3DDevice9*>(&g_device);
    return D3D_OK;
}

HRESULT WINAPI Resource_SetPrivateData(
    void*, const GUID&, const void*, DWORD, DWORD) { return D3D_OK; }
HRESULT WINAPI Resource_GetPrivateData(
    void*, const GUID&, void*, DWORD*) { return D3DERR_NOTFOUND; }
HRESULT WINAPI Resource_FreePrivateData(void*, const GUID&) { return D3D_OK; }
DWORD WINAPI Resource_SetPriority(void*, DWORD) { return 0; }
DWORD WINAPI Resource_GetPriority(void*) { return 0; }
void WINAPI Resource_PreLoad(void*) {}
D3DRESOURCETYPE WINAPI Resource_GetType(void* self) {
    switch (static_cast<ResourceHeader*>(self)->kind) {
        case RESOURCE_TEXTURE: return D3DRTYPE_TEXTURE;
        case RESOURCE_VERTEX_BUFFER: return D3DRTYPE_VERTEXBUFFER;
        case RESOURCE_INDEX_BUFFER: return D3DRTYPE_INDEXBUFFER;
        default: return D3DRTYPE_FORCE_DWORD;
    }
}

DWORD WINAPI Texture_SetLOD(void* self, DWORD lod) {
    TextureObject* texture = static_cast<TextureObject*>(self);
    DWORD old = texture->lod;
    texture->lod = lod;
    return old;
}
DWORD WINAPI Texture_GetLOD(void* self) { return static_cast<TextureObject*>(self)->lod; }
DWORD WINAPI Texture_GetLevelCount(void* self) {
    return static_cast<TextureObject*>(self)->levels;
}
HRESULT WINAPI Texture_SetAutoGenFilterType(
    void* self, D3DTEXTUREFILTERTYPE filter) {
    static_cast<TextureObject*>(self)->filter = filter;
    return D3D_OK;
}
D3DTEXTUREFILTERTYPE WINAPI Texture_GetAutoGenFilterType(void* self) {
    return static_cast<TextureObject*>(self)->filter;
}
void WINAPI Texture_GenerateMipSubLevels(void*) {}

void FillSurfaceDesc(TextureObject* texture, UINT level, D3DSURFACE_DESC* desc) {
    ByteZero(desc, sizeof(*desc));
    desc->Format = texture->base.format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->base.usage;
    desc->Pool = texture->base.pool;
    desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    desc->MultiSampleQuality = 0;
    desc->Width = texture->level_widths[level];
    desc->Height = texture->level_heights[level];
}

HRESULT WINAPI Texture_GetLevelDesc(void* self, UINT level, D3DSURFACE_DESC* desc) {
    TextureObject* texture = static_cast<TextureObject*>(self);
    if (!desc || level >= texture->levels) return D3DERR_INVALIDCALL;
    FillSurfaceDesc(texture, level, desc);
    return D3D_OK;
}

HRESULT WINAPI Surface_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (surface->texture) Resource_AddRef(&surface->texture->base);
    else InterlockedIncrement(&surface->refs);
    return S_OK;
}
ULONG WINAPI Surface_AddRef(void* self) {
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (surface->texture) return Resource_AddRef(&surface->texture->base);
    return static_cast<ULONG>(InterlockedIncrement(&surface->refs));
}
ULONG WINAPI Surface_Release(void* self) {
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (surface->texture) return Resource_Release(&surface->texture->base);
    LONG refs = InterlockedDecrement(&surface->refs);
    if (refs < 0) { surface->refs = 0; refs = 0; }
    return static_cast<ULONG>(refs);
}

HRESULT WINAPI Texture_GetSurfaceLevel(void* self, UINT level, IDirect3DSurface9** out) {
    TextureObject* texture = static_cast<TextureObject*>(self);
    if (!out || level >= texture->levels) return D3DERR_INVALIDCALL;
    SurfaceObject* surface = &texture->surfaces[level];
    Resource_AddRef(&texture->base);
    *out = reinterpret_cast<IDirect3DSurface9*>(surface);
    return D3D_OK;
}

HRESULT TextureLock(
    TextureObject* texture, UINT level, D3DLOCKED_RECT* locked,
    const RECT* rect, DWORD flags) {
    if (!texture || !locked || texture->base.locked ||
        texture->base.destroy_sent || !texture->base.data) {
        return D3DERR_INVALIDCALL;
    }
    if (level >= texture->levels) return D3DERR_INVALIDCALL;
    const UINT width = texture->level_widths[level];
    const UINT height = texture->level_heights[level];
    const UINT pitch = texture->level_pitches[level];
    RECT area = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (rect) area = *rect;
    if (area.left < 0 || area.top < 0 || area.right > static_cast<LONG>(width) ||
        area.bottom > static_cast<LONG>(height) || area.right <= area.left ||
        area.bottom <= area.top) return D3DERR_INVALIDCALL;
    const UINT bpp = BytesPerPixel(texture->base.format);
    const UINT offset = texture->level_offsets[level] +
                        static_cast<UINT>(area.top) * pitch +
                        static_cast<UINT>(area.left) * bpp;
    texture->base.locked = TRUE;
    texture->base.lock_offset = offset;
    texture->base.lock_bytes = static_cast<UINT>(area.bottom - area.top) * pitch;
    texture->base.lock_requested_bytes = texture->base.lock_bytes;
    texture->base.lock_flags = flags;
    texture->locked_level = level;
    locked->Pitch = pitch;
    locked->pBits = texture->base.data + offset;
    return D3D_OK;
}

HRESULT TextureUnlock(TextureObject* texture) {
    if (!texture || !texture->base.locked || texture->base.destroy_sent ||
        !texture->base.data) return D3DERR_INVALIDCALL;
    const DWORD flags = texture->base.lock_flags;
    texture->base.locked = FALSE;
    texture->base.lock_flags = 0;
    if (flags & D3DLOCK_READONLY) return D3D_OK;
    const HRESULT result = UploadResource(
        texture->base.handle, RESOURCE_TEXTURE, texture->base.data,
        texture->base.data_bytes, 0, texture->base.data_bytes);
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (SUCCEEDED(result)) texture->recycled_needs_full_upload = FALSE;
#endif
    return result;
}

HRESULT WINAPI Texture_LockRect(
    void* self, UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) {
    TraceDeviceCall(0x93, level);
    TextureObject* texture = static_cast<TextureObject*>(self);
    if (level >= texture->levels) return D3DERR_INVALIDCALL;
    return TextureLock(texture, level, locked, rect, flags);
}
HRESULT WINAPI Texture_UnlockRect(void* self, UINT level) {
    TraceDeviceCall(0x94, level);
    TextureObject* texture = static_cast<TextureObject*>(self);
    if (level >= texture->levels || !texture->base.locked ||
        texture->locked_level != level) return D3DERR_INVALIDCALL;
    return TextureUnlock(texture);
}
HRESULT WINAPI Texture_AddDirtyRect(void*, const RECT*) { return D3D_OK; }

HRESULT WINAPI Surface_GetDevice(void* self, IDirect3DDevice9** out) {
    return Resource_GetDevice(self, out);
}
HRESULT WINAPI Surface_SetPrivateData(
    void*, const GUID&, const void*, DWORD, DWORD) { return D3D_OK; }
HRESULT WINAPI Surface_GetPrivateData(
    void*, const GUID&, void*, DWORD*) { return D3DERR_NOTFOUND; }
HRESULT WINAPI Surface_FreePrivateData(void*, const GUID&) { return D3D_OK; }
DWORD WINAPI Surface_SetPriority(void*, DWORD) { return 0; }
DWORD WINAPI Surface_GetPriority(void*) { return 0; }
void WINAPI Surface_PreLoad(void*) {}
D3DRESOURCETYPE WINAPI Surface_GetType(void*) { return D3DRTYPE_SURFACE; }
HRESULT WINAPI Surface_GetContainer(void* self, const GUID&, void** out) {
    if (!out) return E_POINTER;
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (!surface->texture) return D3DERR_INVALIDCALL;
    Resource_AddRef(&surface->texture->base);
    *out = &surface->texture->base;
    return D3D_OK;
}
HRESULT WINAPI Surface_GetDesc(void* self, D3DSURFACE_DESC* desc) {
    if (!desc) return D3DERR_INVALIDCALL;
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (surface->texture) {
        FillSurfaceDesc(surface->texture, surface->level, desc);
    } else {
        ByteZero(desc, sizeof(*desc));
        desc->Format = g_device.backbuffer_format;
        desc->Type = D3DRTYPE_SURFACE;
        desc->Pool = D3DPOOL_DEFAULT;
        desc->MultiSampleType = D3DMULTISAMPLE_NONE;
        desc->Width = g_device.viewport.Width;
        desc->Height = g_device.viewport.Height;
    }
    return D3D_OK;
}
HRESULT WINAPI Surface_LockRect(
    void* self, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) {
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (!surface->texture) return D3DERR_INVALIDCALL;
    return TextureLock(surface->texture, surface->level, locked, rect, flags);
}
HRESULT WINAPI Surface_UnlockRect(void* self) {
    SurfaceObject* surface = static_cast<SurfaceObject*>(self);
    if (!surface->texture) return D3DERR_INVALIDCALL;
    if (!surface->texture->base.locked ||
        surface->texture->locked_level != surface->level) return D3DERR_INVALIDCALL;
    return TextureUnlock(surface->texture);
}
HRESULT WINAPI Surface_GetDC(void*, HDC*) { return D3DERR_INVALIDCALL; }
HRESULT WINAPI Surface_ReleaseDC(void*, HDC) { return D3DERR_INVALIDCALL; }

HRESULT WINAPI Buffer_Lock(
    void* self, UINT offset, UINT size, void** out, DWORD flags) {
    BufferObject* buffer = static_cast<BufferObject*>(self);
    if (!out || buffer->base.locked || buffer->base.destroy_sent ||
        !buffer->base.data || offset > buffer->base.data_bytes) {
        return D3DERR_INVALIDCALL;
    }
    const UINT requested_size = size;
    if (!size) size = buffer->base.data_bytes - offset;
    if (size > buffer->base.data_bytes - offset) return D3DERR_INVALIDCALL;
    buffer->base.locked = TRUE;
    buffer->base.lock_offset = offset;
    buffer->base.lock_bytes = size;
    buffer->base.lock_requested_bytes = requested_size;
    buffer->base.lock_flags = flags;
    *out = buffer->base.data + offset;
    return D3D_OK;
}

HRESULT WINAPI Buffer_Unlock(void* self) {
    BufferObject* buffer = static_cast<BufferObject*>(self);
    if (!buffer->base.locked || buffer->base.destroy_sent ||
        !buffer->base.data) return D3DERR_INVALIDCALL;
    const DWORD flags = buffer->base.lock_flags;
    buffer->base.locked = FALSE;
    buffer->base.lock_flags = 0;
    if (flags & D3DLOCK_READONLY) return D3D_OK;
    BYTE* changed_bytes = buffer->base.data + buffer->base.lock_offset;
    return UploadResource(buffer->base.handle, buffer->base.kind,
                          changed_bytes, buffer->base.data_bytes,
                          buffer->base.lock_offset, buffer->base.lock_bytes,
                          buffer->base.lock_requested_bytes, flags, 0);
}

HRESULT WINAPI VertexBuffer_GetDesc(void* self, D3DVERTEXBUFFER_DESC* desc) {
    if (!desc) return D3DERR_INVALIDCALL;
    BufferObject* buffer = static_cast<BufferObject*>(self);
    ByteZero(desc, sizeof(*desc));
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = buffer->base.usage;
    desc->Pool = buffer->base.pool;
    desc->Size = buffer->base.data_bytes;
    desc->FVF = buffer->fvf;
    return D3D_OK;
}

HRESULT WINAPI IndexBuffer_GetDesc(void* self, D3DINDEXBUFFER_DESC* desc) {
    if (!desc) return D3DERR_INVALIDCALL;
    BufferObject* buffer = static_cast<BufferObject*>(self);
    ByteZero(desc, sizeof(*desc));
    desc->Format = buffer->base.format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = buffer->base.usage;
    desc->Pool = buffer->base.pool;
    desc->Size = buffer->base.data_bytes;
    return D3D_OK;
}

HRESULT WINAPI Declaration_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<DeclarationObject*>(self)->refs);
    return S_OK;
}

ULONG WINAPI Declaration_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<DeclarationObject*>(self)->refs));
}

ULONG WINAPI Declaration_Release(void* self) {
    DeclarationObject* declaration = static_cast<DeclarationObject*>(self);
    LONG refs = InterlockedDecrement(&declaration->refs);
    if (refs > 0) return static_cast<ULONG>(refs);
    Command2 command = {
        COMMAND_DESTROY_RESOURCE, declaration->handle,
        static_cast<uint32_t>(RESOURCE_VERTEX_DECLARATION)};
    SendCommand(&command, sizeof(command));
    HeapFree(GetProcessHeap(), 0, declaration);
    return 0;
}

HRESULT WINAPI Declaration_GetDevice(void*, IDirect3DDevice9** out) {
    return Resource_GetDevice(0, out);
}

HRESULT WINAPI Declaration_GetDeclaration(
    void* self, D3DVERTEXELEMENT9* elements, UINT* element_count) {
    if (!element_count) return D3DERR_INVALIDCALL;
    DeclarationObject* declaration = static_cast<DeclarationObject*>(self);
    const UINT required = declaration->element_count + 1;
    if (!elements) {
        *element_count = required;
        return D3D_OK;
    }
    if (*element_count < required) {
        *element_count = required;
        return D3DERR_MOREDATA;
    }
    const UINT declaration_bytes = required * sizeof(D3DVERTEXELEMENT9);
    TrackCopySite(COPY_DECLARATION, declaration_bytes);
    ByteCopy(elements, declaration->elements, declaration_bytes);
    *element_count = required;
    return D3D_OK;
}

HRESULT WINAPI Device_CreateVertexDeclaration(
    void*, const D3DVERTEXELEMENT9* elements,
    IDirect3DVertexDeclaration9** out) {
    TraceDeviceCall(86, elements ? 1u : 0u);
    if (!elements || !out) return D3DERR_INVALIDCALL;
    UINT count = 0;
    for (; count < 64; ++count) {
        const D3DVERTEXELEMENT9& element = elements[count];
        if (element.Stream == 0xFF && element.Type == D3DDECLTYPE_UNUSED) break;
        if (element.Stream >= 8 || element.Method != D3DDECLMETHOD_DEFAULT ||
            element.Type > D3DDECLTYPE_FLOAT16_4) return D3DERR_INVALIDCALL;
    }
    if (!count || count >= 64) return D3DERR_INVALIDCALL;
    DeclarationObject* declaration = static_cast<DeclarationObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DeclarationObject)));
    if (!declaration) return E_OUTOFMEMORY;
    declaration->vtable = g_declaration_vtable;
    declaration->refs = 1;
    declaration->handle = AllocateResourceHandle();
    declaration->element_count = count;
    const UINT retained_declaration_bytes = (count + 1) * sizeof(D3DVERTEXELEMENT9);
    TrackCopySite(COPY_DECLARATION, retained_declaration_bytes);
    ByteCopy(declaration->elements, elements, retained_declaration_bytes);
    CommandCreateDeclaration header = {
        COMMAND_CREATE_DECLARATION, declaration->handle, count};
    const UINT packet_bytes = sizeof(header) + count * sizeof(D3DVERTEXELEMENT9);
    TrackCopySite(COPY_DECLARATION, sizeof(header));
    ByteCopy(g_command_bytes, &header, sizeof(header));
    const UINT command_declaration_bytes = count * sizeof(D3DVERTEXELEMENT9);
    TrackCopySite(COPY_DECLARATION, command_declaration_bytes);
    ByteCopy(g_command_bytes + sizeof(header), elements, command_declaration_bytes);
    HRESULT hr = SendCommand(g_command_bytes, packet_bytes);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, declaration);
        return hr;
    }
    *out = reinterpret_cast<IDirect3DVertexDeclaration9*>(declaration);
    return D3D_OK;
}

HRESULT WINAPI Device_SetVertexDeclaration(
    void* self, IDirect3DVertexDeclaration9* declaration_interface) {
    TraceDeviceCall(87, declaration_interface ? 1u : 0u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    DeclarationObject* declaration =
        reinterpret_cast<DeclarationObject*>(declaration_interface);
    if (device->recording_block) {
        if (declaration) Declaration_AddRef(declaration);
        DeclarationObject* old = device->recording_block->declaration;
        device->recording_block->declaration = declaration;
        device->recording_block->declaration_set = TRUE;
        device->recording_block->fvf_set = FALSE;
        if (old) Declaration_Release(old);
        return D3D_OK;
    }
    if (declaration) Declaration_AddRef(declaration);
    DeclarationObject* old = device->declaration;
    Command1 command = {
        COMMAND_BIND_DECLARATION, declaration ? declaration->handle : 0};
    HRESULT hr = SendCommand(&command, sizeof(command));
    if (FAILED(hr)) {
        if (declaration) Declaration_Release(declaration);
        return hr;
    }
    device->declaration = declaration;
    device->fvf_valid = FALSE;
    if (old) Declaration_Release(old);
    return D3D_OK;
}

HRESULT WINAPI Query_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<QueryObject*>(self)->refs);
    return S_OK;
}
ULONG WINAPI Query_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<QueryObject*>(self)->refs));
}
ULONG WINAPI Query_Release(void* self) {
    QueryObject* query = static_cast<QueryObject*>(self);
    LONG refs = InterlockedDecrement(&query->refs);
    if (refs > 0) return static_cast<ULONG>(refs);
    HeapFree(GetProcessHeap(), 0, query);
    return 0;
}
HRESULT WINAPI Query_GetDevice(void*, IDirect3DDevice9** out) {
    return Resource_GetDevice(0, out);
}
D3DQUERYTYPE WINAPI Query_GetType(void* self) {
    return static_cast<QueryObject*>(self)->type;
}
DWORD WINAPI Query_GetDataSize(void* self) {
    switch (static_cast<QueryObject*>(self)->type) {
        case D3DQUERYTYPE_EVENT: return sizeof(BOOL);
        case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
        case D3DQUERYTYPE_TIMESTAMP: return sizeof(UINT64);
        case D3DQUERYTYPE_TIMESTAMPDISJOINT: return sizeof(BOOL);
        case D3DQUERYTYPE_TIMESTAMPFREQ: return sizeof(UINT64);
        case D3DQUERYTYPE_VCACHE: return sizeof(D3DDEVINFO_VCACHE);
        default: return sizeof(DWORD);
    }
}
HRESULT WINAPI Query_Issue(void* self, DWORD) {
    static_cast<QueryObject*>(self)->issued = TRUE;
    return D3D_OK;
}
HRESULT WINAPI Query_GetData(void* self, void* data, DWORD size, DWORD) {
    QueryObject* query = static_cast<QueryObject*>(self);
    const DWORD required = Query_GetDataSize(self);
    if (data && size < required) return D3DERR_INVALIDCALL;
    if (data) {
        ByteZero(data, required);
        if (query->type == D3DQUERYTYPE_EVENT) *static_cast<BOOL*>(data) = TRUE;
        if (query->type == D3DQUERYTYPE_VCACHE) {
            D3DDEVINFO_VCACHE* cache = static_cast<D3DDEVINFO_VCACHE*>(data);
            cache->Pattern = MAKEFOURCC('C', 'A', 'C', 'H');
            cache->OptMethod = 0;
            cache->CacheSize = 24;
            cache->MagicNumber = 0;
        }
    }
    return D3D_OK;
}
HRESULT WINAPI Device_CreateQuery(
    void*, D3DQUERYTYPE type, IDirect3DQuery9** out) {
    if (!out) return D3D_OK;
    QueryObject* query = static_cast<QueryObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(QueryObject)));
    if (!query) return E_OUTOFMEMORY;
    query->vtable = g_query_vtable;
    query->refs = 1;
    query->type = type;
    *out = reinterpret_cast<IDirect3DQuery9*>(query);
    return D3D_OK;
}

HRESULT WINAPI Device_CreateTexture(
    void*, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
    D3DPOOL pool, IDirect3DTexture9** out, HANDLE* shared) {
    TraceDeviceCall(23, static_cast<UINT>(format));
    if (!out) {
        DebugRejectHex("D3D9WEB_REJECT CreateTexture out ", 0);
        return D3DERR_INVALIDCALL;
    }
    if (shared) {
        DebugRejectHex("D3D9WEB_REJECT CreateTexture shared ",
                       static_cast<UINT>(reinterpret_cast<uintptr_t>(shared)));
        return D3DERR_INVALIDCALL;
    }
    if (!width || !height) {
        DebugRejectHex("D3D9WEB_REJECT CreateTexture dimensions ",
                       (width << 16) ^ height);
        return D3DERR_INVALIDCALL;
    }
    const UINT bpp = BytesPerPixel(format);
    if (!bpp) {
        DebugRejectHex("D3D9WEB_REJECT CreateTexture format ",
                       static_cast<UINT>(format));
        return D3DERR_NOTAVAILABLE;
    }
    const UINT full_levels = FullMipCount(width, height);
    const UINT resolved_levels = levels ? levels : full_levels;
    if (!resolved_levels || resolved_levels > full_levels || resolved_levels > 16) {
        DebugRejectHex("D3D9WEB_REJECT CreateTexture levels ", levels);
        return D3DERR_INVALIDCALL;
    }
    TextureObject* texture = static_cast<TextureObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TextureObject)));
    if (!texture) return E_OUTOFMEMORY;
    UINT total = 0;
    if (!BuildTextureLayout(texture, width, height, resolved_levels, bpp, &total)) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D3DERR_OUTOFVIDEOMEMORY;
    }
    texture->base.vtable = g_texture_vtable;
    texture->base.refs = 1;
    texture->base.kind = RESOURCE_TEXTURE;
    texture->base.usage = usage;
    texture->base.pool = pool;
    texture->base.format = format;
    texture->base.data_bytes = total;
    texture->base.data = static_cast<BYTE*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total));
    if (!texture->base.data) {
        HeapFree(GetProcessHeap(), 0, texture);
        return E_OUTOFMEMORY;
    }
    BOOL recycled = FALSE;
    uint32_t recycled_handle = 0;
    LONG recycled_epoch = 0;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    recycled = TryAcquireTransientTextureHandle(
        width, height, resolved_levels, usage, format, pool,
        texture->level_pitches[0], total, &recycled_handle, &recycled_epoch);
#endif
    texture->base.handle = recycled ? recycled_handle : AllocateResourceHandle();
    texture->recycled_needs_full_upload = recycled;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    texture->recycle_epoch = recycled ? recycled_epoch :
        g_transient_texture_epoch;
#endif
    texture->width = width;
    texture->height = height;
    texture->levels = resolved_levels;
    texture->filter = D3DTEXF_NONE;
    for (UINT level = 0; level < resolved_levels; ++level) {
        texture->surfaces[level].vtable = g_surface_vtable;
        texture->surfaces[level].refs = 0;
        texture->surfaces[level].texture = texture;
        texture->surfaces[level].level = level;
    }
    if (!recycled) {
        CommandCreateTexture command = {
            COMMAND_CREATE_TEXTURE, texture->base.handle, width, height,
            resolved_levels, usage, static_cast<uint32_t>(format),
            static_cast<uint32_t>(pool), texture->level_pitches[0], total};
        HRESULT hr = SendCommand(&command, sizeof(command));
        if (FAILED(hr)) {
            HeapFree(GetProcessHeap(), 0, texture->base.data);
            HeapFree(GetProcessHeap(), 0, texture);
            return hr;
        }
    }
    *out = reinterpret_cast<IDirect3DTexture9*>(texture);
    return D3D_OK;
}

HRESULT CreateBuffer(
    UINT length, DWORD usage, DWORD format_or_fvf, D3DPOOL pool,
    ResourceKind kind, void** out, HANDLE* shared) {
    if (!out || shared || !length) return D3DERR_INVALIDCALL;
    if (kind == RESOURCE_INDEX_BUFFER && format_or_fvf != D3DFMT_INDEX16 &&
        format_or_fvf != D3DFMT_INDEX32) return D3DERR_NOTAVAILABLE;
    BufferObject* buffer = static_cast<BufferObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BufferObject)));
    if (!buffer) return E_OUTOFMEMORY;
    buffer->base.vtable = kind == RESOURCE_VERTEX_BUFFER ?
        g_vertex_buffer_vtable : g_index_buffer_vtable;
    buffer->base.refs = 1;
    buffer->base.kind = kind;
    buffer->base.handle = AllocateResourceHandle();
    buffer->base.usage = usage;
    buffer->base.pool = pool;
    buffer->base.format = kind == RESOURCE_INDEX_BUFFER ?
        static_cast<D3DFORMAT>(format_or_fvf) : D3DFMT_VERTEXDATA;
    buffer->base.data_bytes = length;
    buffer->base.data = static_cast<BYTE*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length));
    if (!buffer->base.data) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return E_OUTOFMEMORY;
    }
    buffer->fvf = kind == RESOURCE_VERTEX_BUFFER ? format_or_fvf : 0;
    CommandCreateBuffer command = {
        COMMAND_CREATE_BUFFER, buffer->base.handle, static_cast<uint32_t>(kind),
        length, usage, format_or_fvf, static_cast<uint32_t>(pool)};
    HRESULT hr = SendCommand(&command, sizeof(command));
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, buffer->base.data);
        HeapFree(GetProcessHeap(), 0, buffer);
        return hr;
    }
    *out = buffer;
    return D3D_OK;
}

HRESULT WINAPI Device_CreateVertexBuffer(
    void*, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
    IDirect3DVertexBuffer9** out, HANDLE* shared) {
    TraceDeviceCall(26, length);
    return CreateBuffer(length, usage, fvf, pool, RESOURCE_VERTEX_BUFFER,
                        reinterpret_cast<void**>(out), shared);
}

HRESULT WINAPI Device_CreateIndexBuffer(
    void*, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
    IDirect3DIndexBuffer9** out, HANDLE* shared) {
    TraceDeviceCall(27, length);
    return CreateBuffer(length, usage, static_cast<DWORD>(format), pool,
                        RESOURCE_INDEX_BUFFER, reinterpret_cast<void**>(out), shared);
}

HRESULT WINAPI Device_SetTexture(
    void* self, DWORD stage, IDirect3DBaseTexture9* texture_interface) {
    TraceDeviceCall(65, stage);
    if (stage >= 8) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    TextureObject* texture = reinterpret_cast<TextureObject*>(texture_interface);
    if (texture && texture->base.kind != RESOURCE_TEXTURE) return D3DERR_INVALIDCALL;
    if (device->recording_block) {
        if (texture) Resource_AddRef(&texture->base);
        TextureObject* old = device->recording_block->textures[stage];
        device->recording_block->textures[stage] = texture;
        device->recording_block->texture_set[stage] = TRUE;
        if (old) Resource_Release(&old->base);
        return D3D_OK;
    }
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (texture) {
        const HRESULT upload = EnsureRecycledTextureUploaded(texture);
        if (FAILED(upload)) return upload;
    }
#endif
#if D3D9WEB_STATE_EQUALITY_PROFILE
    ++g_state_calls[STATE_EQUALITY_TEXTURE];
    if (device->texture_bind_valid[stage] && device->textures[stage] == texture) {
        ++g_state_same[STATE_EQUALITY_TEXTURE];
    }
#endif
#if D3D9WEB_STATE_CACHE_TEXTURE_VIEWPORT
    if (device->texture_bind_valid[stage] && device->textures[stage] == texture) {
        return D3D_OK;
    }
#endif
    if (texture) Resource_AddRef(&texture->base);
    TextureObject* old = device->textures[stage];
    CommandBindTexture command = {
        COMMAND_BIND_TEXTURE, stage, texture ? texture->base.handle : 0};
    HRESULT hr = SendCommand(&command, sizeof(command));
    device->texture_bind_valid[stage] = SUCCEEDED(hr) ? TRUE : FALSE;
    if (SUCCEEDED(hr)) {
        device->textures[stage] = texture;
        if (old) Resource_Release(&old->base);
    } else if (texture) {
        Resource_Release(&texture->base);
    }
    return hr;
}

HRESULT WINAPI Device_SetStreamSource(
    void* self, UINT stream, IDirect3DVertexBuffer9* buffer_interface,
    UINT offset, UINT stride) {
    TraceDeviceCall(100, stream);
    if (stream != 0) return D3DERR_NOTAVAILABLE;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    BufferObject* buffer = reinterpret_cast<BufferObject*>(buffer_interface);
    if (buffer && buffer->base.kind != RESOURCE_VERTEX_BUFFER) return D3DERR_INVALIDCALL;
    if (device->recording_block) {
        if (buffer) Resource_AddRef(&buffer->base);
        BufferObject* old = device->recording_block->stream0;
        device->recording_block->stream0 = buffer;
        device->recording_block->stream0_offset = offset;
        device->recording_block->stream0_stride = stride;
        device->recording_block->stream0_set = TRUE;
        if (old) Resource_Release(&old->base);
        return D3D_OK;
    }
    if (buffer) Resource_AddRef(&buffer->base);
    BufferObject* old = device->stream0;
    device->stream0 = buffer;
    device->stream0_offset = offset;
    device->stream0_stride = stride;
    CommandBindStream command = {
        COMMAND_BIND_STREAM, stream, buffer ? buffer->base.handle : 0, offset, stride};
    HRESULT hr = SendCommand(&command, sizeof(command));
    if (old) Resource_Release(&old->base);
    return hr;
}

HRESULT WINAPI Device_SetIndices(
    void* self, IDirect3DIndexBuffer9* buffer_interface) {
    TraceDeviceCall(104, buffer_interface ? 1u : 0u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    BufferObject* buffer = reinterpret_cast<BufferObject*>(buffer_interface);
    if (buffer && buffer->base.kind != RESOURCE_INDEX_BUFFER) return D3DERR_INVALIDCALL;
    if (device->recording_block) {
        if (buffer) Resource_AddRef(&buffer->base);
        BufferObject* old = device->recording_block->indices;
        device->recording_block->indices = buffer;
        device->recording_block->indices_set = TRUE;
        if (old) Resource_Release(&old->base);
        return D3D_OK;
    }
    if (buffer) Resource_AddRef(&buffer->base);
    BufferObject* old = device->indices;
    device->indices = buffer;
    Command1 command = {
        COMMAND_BIND_INDICES, buffer ? buffer->base.handle : 0};
    HRESULT hr = SendCommand(&command, sizeof(command));
    if (old) Resource_Release(&old->base);
    return hr;
}

HRESULT WINAPI Device_GetIndices(void* self, IDirect3DIndexBuffer9** out) {
    TraceDeviceCall(105, 0);
    if (!out) return D3DERR_INVALIDCALL;
    BufferObject* buffer = static_cast<DeviceObject*>(self)->indices;
    if (buffer) Resource_AddRef(&buffer->base);
    *out = reinterpret_cast<IDirect3DIndexBuffer9*>(buffer);
    return D3D_OK;
}

HRESULT WINAPI Device_DrawIndexedPrimitive(
    void* self, D3DPRIMITIVETYPE primitive_type, INT base_vertex_index,
    UINT min_vertex_index, UINT num_vertices, UINT start_index,
    UINT primitive_count) {
    TraceDeviceCall(82, static_cast<UINT>(primitive_type));
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (!device->stream0 || !device->indices || !device->stream0_stride) {
        return D3DERR_INVALIDCALL;
    }
    CommandDrawIndexed command = {
        COMMAND_DRAW_INDEXED, static_cast<uint32_t>(primitive_type),
        base_vertex_index, min_vertex_index, num_vertices, start_index,
        primitive_count};
    return SendCommand(&command, sizeof(command));
}

// D3D9 render states have documented non-zero defaults, but the device cache is
// zero-initialised.  StateBlock_Capture copies device->render_states[state] for
// every state the block records, without consulting the device's own "was this
// ever set" flag, so a state the game never wrote is captured as 0 and then
// pushed to the host by StateBlock_Apply.
//
// Measured (doc 535): the whole blue screen is this.  D3DRS_COLORWRITEENABLE
// defaults to 0x0000000F, was never set by the game, and got captured as 0 -
// disabling every colour write and freezing the navy clear on screen.  The only
// caller of SetRenderState(168) in a live trace was d3d9.dll itself.
//
// Seed the documented defaults.  render_state_set stays FALSE so the first real
// SetRenderState still forwards to the host, and the values chosen here are the
// same ones the host runtime already falls back to, so both sides now agree by
// construction instead of by accident.
void SeedDefaultRenderStates(DeviceObject* device) {
    struct Seed { UINT state; DWORD value; };
    static const Seed kSeeds[] = {
        {7, 1},            // D3DRS_ZENABLE = D3DZB_TRUE
        {8, 3},            // D3DRS_FILLMODE = D3DFILL_SOLID
        {9, 2},            // D3DRS_SHADEMODE = D3DSHADE_GOURAUD
        {14, 1},           // D3DRS_ZWRITEENABLE = TRUE
        {16, 1},           // D3DRS_LASTPIXEL = TRUE
        {19, 2},           // D3DRS_SRCBLEND = D3DBLEND_ONE
        {20, 1},           // D3DRS_DESTBLEND = D3DBLEND_ZERO
        {22, 3},           // D3DRS_CULLMODE = D3DCULL_CCW
        {23, 4},           // D3DRS_ZFUNC = D3DCMP_LESSEQUAL
        {25, 8},           // D3DRS_ALPHAFUNC = D3DCMP_ALWAYS
        {37, 0x3f800000},  // D3DRS_FOGEND = 1.0f
        {38, 0x3f800000},  // D3DRS_FOGDENSITY = 1.0f
        {53, 1},           // D3DRS_STENCILFAIL = D3DSTENCILOP_KEEP
        {54, 1},           // D3DRS_STENCILZFAIL = D3DSTENCILOP_KEEP
        {55, 1},           // D3DRS_STENCILPASS = D3DSTENCILOP_KEEP
        {56, 8},           // D3DRS_STENCILFUNC = D3DCMP_ALWAYS
        {58, 0xffffffff},  // D3DRS_STENCILMASK
        {59, 0xffffffff},  // D3DRS_STENCILWRITEMASK
        {60, 0xffffffff},  // D3DRS_TEXTUREFACTOR
        {136, 1},          // D3DRS_CLIPPING = TRUE
        {137, 1},          // D3DRS_LIGHTING = TRUE
        {141, 1},          // D3DRS_COLORVERTEX = TRUE
        {142, 1},          // D3DRS_LOCALVIEWER = TRUE
        {145, 1},          // D3DRS_DIFFUSEMATERIALSOURCE = D3DMCS_COLOR1
        {146, 2},          // D3DRS_SPECULARMATERIALSOURCE = D3DMCS_COLOR2
        {155, 0x3f800000}, // D3DRS_POINTSIZE_MIN = 1.0f
        {158, 0x3f800000}, // D3DRS_POINTSCALE_A = 1.0f
        {161, 1},          // D3DRS_MULTISAMPLEANTIALIAS = TRUE
        {162, 0xffffffff}, // D3DRS_MULTISAMPLEMASK
        {166, 0x42800000}, // D3DRS_POINTSIZE_MAX = 64.0f
        {168, 0x0000000f}, // D3DRS_COLORWRITEENABLE  <-- the blue screen
        {171, 1},          // D3DRS_BLENDOP = D3DBLENDOP_ADD
        {172, 3},          // D3DRS_POSITIONDEGREE = D3DDEGREE_CUBIC
        {173, 1},          // D3DRS_NORMALDEGREE = D3DDEGREE_LINEAR
        {178, 0x3f800000}, // D3DRS_MINTESSELLATIONLEVEL = 1.0f
        {179, 0x3f800000}, // D3DRS_MAXTESSELLATIONLEVEL = 1.0f
        {185, 1},          // D3DRS_CCW_STENCILFAIL = D3DSTENCILOP_KEEP
        {186, 1},          // D3DRS_CCW_STENCILZFAIL = D3DSTENCILOP_KEEP
        {187, 1},          // D3DRS_CCW_STENCILPASS = D3DSTENCILOP_KEEP
        {188, 8},          // D3DRS_CCW_STENCILFUNC = D3DCMP_ALWAYS
        {189, 0x0000000f}, // D3DRS_COLORWRITEENABLE1
        {190, 0x0000000f}, // D3DRS_COLORWRITEENABLE2
        {191, 0x0000000f}, // D3DRS_COLORWRITEENABLE3
        {192, 0xffffffff}, // D3DRS_BLENDFACTOR
        {207, 2},          // D3DRS_SRCBLENDALPHA = D3DBLEND_ONE
        {208, 1},          // D3DRS_DESTBLENDALPHA = D3DBLEND_ZERO
        {209, 1},          // D3DRS_BLENDOPALPHA = D3DBLENDOP_ADD
    };
    for (UINT i = 0; i < sizeof(kSeeds) / sizeof(kSeeds[0]); ++i) {
        if (kSeeds[i].state < 256u) device->render_states[kSeeds[i].state] = kSeeds[i].value;
    }
}

HRESULT WINAPI D3D9_CreateDevice(
    void*, UINT adapter, D3DDEVTYPE device_type, HWND focus_window,
    DWORD behavior_flags, D3DPRESENT_PARAMETERS* present,
    IDirect3DDevice9** out_device) {
    TraceD3D9Once(16, present ? present->BackBufferWidth : 0xffffffffu);
    if (!out_device || !present) return E_POINTER;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    const HRESULT drain = DrainTransientTextureSlots();
    if (FAILED(drain)) return drain;
    AbandonTransientTextureSlots();
#endif
    ByteZero(&g_device, sizeof(g_device));
    SeedDefaultRenderStates(&g_device);
    g_device.vtable = g_device_vtable;
    g_device.refs = 1;
    g_device.creation_parameters.AdapterOrdinal = adapter;
    g_device.creation_parameters.DeviceType = device_type;
    g_device.creation_parameters.hFocusWindow = focus_window;
    g_device.creation_parameters.BehaviorFlags = behavior_flags;
    g_device.viewport.X = 0;
    g_device.viewport.Y = 0;
    g_device.viewport.Width = present->BackBufferWidth ? present->BackBufferWidth : 800;
    g_device.viewport.Height = present->BackBufferHeight ? present->BackBufferHeight : 600;
    g_device.viewport.MinZ = 0.0f;
    g_device.viewport.MaxZ = 1.0f;
    g_device.scissor.left = 0;
    g_device.scissor.top = 0;
    g_device.scissor.right = static_cast<LONG>(g_device.viewport.Width);
    g_device.scissor.bottom = static_cast<LONG>(g_device.viewport.Height);
    g_device.backbuffer_format = present->BackBufferFormat == D3DFMT_UNKNOWN ?
        D3DFMT_X8R8G8B8 : present->BackBufferFormat;
    g_device.cursor_visible = FALSE;
    g_device.material.Diffuse.r = g_device.material.Diffuse.g =
        g_device.material.Diffuse.b = g_device.material.Diffuse.a = 1.0f;
    g_device.material.Ambient = g_device.material.Diffuse;
    g_backbuffer.vtable = g_surface_vtable;
    g_backbuffer.refs = 0;
    g_backbuffer.texture = 0;
    g_backbuffer.level = 0;

    Command2 command = {
        COMMAND_INIT, g_device.viewport.Width, g_device.viewport.Height};
    const HRESULT bridge = SendCommand(&command, sizeof(command));
    if (FAILED(bridge)) return bridge;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    g_transient_texture_recycle_enabled = TRUE;
#endif
    *out_device = reinterpret_cast<IDirect3DDevice9*>(&g_device);
    return S_OK;
}

HRESULT WINAPI Device_SetTransform(
    void* self, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) {
    TraceDeviceCall(44, static_cast<UINT>(state));
    if (!matrix || static_cast<UINT>(state) >= 512u) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->transform_set[state] = TRUE;
        device->recording_block->transforms[state] = *matrix;
        return D3D_OK;
    }
#if D3D9WEB_STATE_EQUALITY_PROFILE
    ++g_state_calls[STATE_EQUALITY_TRANSFORM];
    if (device->transform_valid[state] &&
        ByteEqual(&device->transforms[state], matrix, sizeof(*matrix))) {
        ++g_state_same[STATE_EQUALITY_TRANSFORM];
    }
#endif
#if D3D9WEB_STATE_CACHE_TRANSFORM
    if (device->transform_valid[state] &&
        ByteEqual(&device->transforms[state], matrix, sizeof(*matrix))) {
        return D3D_OK;
    }
#endif
    TrackCopySite(COPY_TRANSFORM, sizeof(*matrix));
    CommandTransform command = {};
    command.command = COMMAND_SET_TRANSFORM;
    command.slot = static_cast<uint32_t>(state);
    TrackCopySite(COPY_TRANSFORM, sizeof(command.matrix));
    ByteCopy(command.matrix, matrix, sizeof(command.matrix));
    HRESULT hr = SendCommand(&command, sizeof(command));
    device->transform_valid[state] = SUCCEEDED(hr) ? TRUE : FALSE;
    if (SUCCEEDED(hr)) {
        ByteCopy(&device->transforms[state], matrix, sizeof(*matrix));
    }
    return hr;
}

HRESULT WINAPI Device_SetViewport(void* self, const D3DVIEWPORT9* viewport) {
    TraceDeviceCall(47, viewport ? viewport->Width : 0u);
    if (!viewport || !viewport->Width || !viewport->Height) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->viewport_set = TRUE;
        device->recording_block->viewport = *viewport;
        return D3D_OK;
    }
#if D3D9WEB_STATE_EQUALITY_PROFILE
    ++g_state_calls[STATE_EQUALITY_VIEWPORT];
    if (device->viewport_valid &&
        ByteEqual(&device->viewport, viewport, sizeof(*viewport))) {
        ++g_state_same[STATE_EQUALITY_VIEWPORT];
    }
#endif
#if D3D9WEB_STATE_CACHE_TEXTURE_VIEWPORT
    if (device->viewport_valid &&
        ByteEqual(&device->viewport, viewport, sizeof(*viewport))) {
        return D3D_OK;
    }
#endif
    CommandViewport command = {
        COMMAND_SET_VIEWPORT, viewport->X, viewport->Y, viewport->Width,
        viewport->Height, viewport->MinZ, viewport->MaxZ};
    HRESULT hr = SendCommand(&command, sizeof(command));
    device->viewport_valid = SUCCEEDED(hr) ? TRUE : FALSE;
    if (SUCCEEDED(hr)) device->viewport = *viewport;
    return hr;
}

HRESULT WINAPI Device_GetViewport(void* self, D3DVIEWPORT9* viewport) {
    TraceDeviceCall(48, 0);
    if (!viewport) return D3DERR_INVALIDCALL;
    *viewport = static_cast<DeviceObject*>(self)->viewport;
    return D3D_OK;
}

HRESULT WINAPI Device_SetRenderState(
    void* self, D3DRENDERSTATETYPE state, DWORD value) {
    TraceDeviceCall(57, static_cast<UINT>(state));
    if (static_cast<UINT>(state) >= 256u) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->render_state_set[state] = TRUE;
        device->recording_block->render_states[state] = value;
        return D3D_OK;
    }
    if (device->render_state_set[state] && device->render_states[state] == value) return D3D_OK;
    device->render_states[state] = value;
    device->render_state_set[state] = TRUE;
#if D3D9WEB_TRACE_COLORWRITE
    // Diagnostic only.  The blue-screen investigation (doc 535) needs the guest
    // call site for every D3DRS_COLORWRITEENABLE write; the host cannot see it.
    // State id 0x9168 is not a real render state, so the host stores it and
    // falls through applyRenderState's default case with no side effect.
    if (static_cast<UINT>(state) == 168u) {
        Command2 mark = {COMMAND_SET_RENDER_STATE, 0x9168u,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()))};
        SendCommand(&mark, sizeof(mark));
    }
#endif
    Command2 command = {COMMAND_SET_RENDER_STATE, static_cast<uint32_t>(state), value};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_SetTextureStageState(
    void* self, DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
    TraceDeviceCall(67, (stage << 16) | static_cast<UINT>(type));
    if (stage >= 8u || static_cast<UINT>(type) >= 33u) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->texture_stage_state_set[stage][type] = TRUE;
        device->recording_block->texture_stage_states[stage][type] = value;
        return D3D_OK;
    }
    if (device->texture_stage_state_set[stage][type] &&
        device->texture_stage_states[stage][type] == value) return D3D_OK;
    device->texture_stage_states[stage][type] = value;
    device->texture_stage_state_set[stage][type] = TRUE;
    Command3 command = {
        COMMAND_SET_TEXTURE_STAGE_STATE, stage, static_cast<uint32_t>(type), value};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_SetSamplerState(
    void* self, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) {
    TraceDeviceCall(69, (sampler << 16) | static_cast<UINT>(type));
    if (static_cast<UINT>(type) >= 14u) return D3DERR_INVALIDCALL;
    // D3D9 exposes pixel samplers 0..15, the displacement-map sampler 256,
    // and vertex samplers 257..260. Acknex initializes all of them even though
    // its fixed-function draws only consume texture stages 0..7. Mirror the
    // real runtime's permissive behavior for the unused samplers instead of
    // aborting engine initialization with D3DERR_INVALIDCALL.
    if (sampler >= 8u) {
        if (sampler <= 15u || (sampler >= 256u && sampler <= 260u)) return D3D_OK;
        return D3DERR_INVALIDCALL;
    }
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->sampler_state_set[sampler][type] = TRUE;
        device->recording_block->sampler_states[sampler][type] = value;
        return D3D_OK;
    }
    if (device->sampler_state_set[sampler][type] &&
        device->sampler_states[sampler][type] == value) return D3D_OK;
    device->sampler_states[sampler][type] = value;
    device->sampler_state_set[sampler][type] = TRUE;
    Command3 command = {
        COMMAND_SET_SAMPLER_STATE, sampler, static_cast<uint32_t>(type), value};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_SetScissorRect(void* self, const RECT* rect) {
    TraceDeviceCall(75, rect ? 1u : 0u);
    if (!rect || rect->right < rect->left || rect->bottom < rect->top) {
        return D3DERR_INVALIDCALL;
    }
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        device->recording_block->scissor_set = TRUE;
        device->recording_block->scissor = *rect;
        return D3D_OK;
    }
    device->scissor = *rect;
    CommandScissor command = {
        COMMAND_SET_SCISSOR, rect->left, rect->top, rect->right, rect->bottom};
    return SendCommand(&command, sizeof(command));
}

HRESULT WINAPI Device_GetScissorRect(void* self, RECT* rect) {
    TraceDeviceCall(76, 0);
    if (!rect) return D3DERR_INVALIDCALL;
    *rect = static_cast<DeviceObject*>(self)->scissor;
    return D3D_OK;
}

HRESULT WINAPI Device_SetFVF(void* self, DWORD fvf) {
    TraceDeviceCall(89, fvf);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (device->recording_block) {
        if (device->recording_block->declaration) {
            Declaration_Release(device->recording_block->declaration);
            device->recording_block->declaration = 0;
        }
        device->recording_block->declaration_set = FALSE;
        device->recording_block->fvf_set = TRUE;
        device->recording_block->fvf = fvf;
        return D3D_OK;
    }
    if (device->fvf_valid && device->fvf == fvf && !device->declaration &&
        !device->recording_state_block) {
        return D3D_OK;
    }
    if (device->declaration) {
        HRESULT unbind = Device_SetVertexDeclaration(self, 0);
        if (FAILED(unbind)) return unbind;
    }
    device->fvf = fvf;
    Command1 command = {COMMAND_SET_FVF, fvf};
    HRESULT hr = SendCommand(&command, sizeof(command));
    device->fvf_valid = SUCCEEDED(hr) ? TRUE : FALSE;
    return hr;
}

HRESULT WINAPI Device_SetVertexShader(
    void* self, IDirect3DVertexShader9* shader) {
    TraceDeviceCall(92, shader ? 1u : 0u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (shader) return D3DERR_INVALIDCALL;
    if (device->recording_block) {
        device->recording_block->vertex_shader_set = TRUE;
    }
    return D3D_OK;
}

HRESULT WINAPI Device_SetPixelShader(
    void* self, IDirect3DPixelShader9* shader) {
    TraceDeviceCall(107, shader ? 1u : 0u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (shader) return D3DERR_INVALIDCALL;
    if (device->recording_block) {
        device->recording_block->pixel_shader_set = TRUE;
    }
    return D3D_OK;
}

HRESULT WINAPI Device_SetNPatchMode(void* self, float segments) {
    TraceDeviceCall(79, segments > 0.0f ? 1u : 0u);
    if (segments < 0.0f) return D3DERR_INVALIDCALL;
    DeviceObject* device = static_cast<DeviceObject*>(self);
    device->npatch_mode = segments;
    return D3D_OK;
}

HRESULT WINAPI Device_DrawPrimitiveUP(
    void* self, D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
    const void* vertex_stream_zero_data, UINT stride) {
    TraceDeviceCall(83, static_cast<UINT>(primitive_type));
    if (!vertex_stream_zero_data || !stride) return D3DERR_INVALIDCALL;
    const UINT vertex_count = PrimitiveVertexCount(primitive_type, primitive_count);
    if (!vertex_count || vertex_count >
        (kMaxCommandBytes - sizeof(CommandDrawUP)) / stride) {
        return D3DERR_INVALIDCALL;
    }
    const UINT vertex_bytes = vertex_count * stride;
    CommandDrawUP header = {
        COMMAND_DRAW_PRIMITIVE_UP, static_cast<uint32_t>(primitive_type),
        primitive_count, stride, static_cast<DeviceObject*>(self)->fvf,
        vertex_bytes};
    TrackCopySite(COPY_DRAW_UP_HEADER, sizeof(header));
    TrackCopySite(COPY_DRAW_UP_PAYLOAD, vertex_bytes);
    DirectDrawUPDescriptor descriptor = {
        static_cast<uint32_t>(primitive_type), primitive_count, stride,
        static_cast<DeviceObject*>(self)->fvf,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vertex_stream_zero_data)),
        vertex_bytes};
    const HRESULT pending = FlushGuestBatch();
    if (FAILED(pending)) {
        InvalidateDeviceHostStateCaches();
        return pending;
    }
    const uint32_t direct_result = D3D9WebCall(
        kD3D9WebIndexBase + 4u,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&descriptor)),
        sizeof(descriptor));
    if (direct_result == kBridgeSuccess) return D3D_OK;
    InvalidateDeviceHostStateCaches();

    // Preserve compatibility with a host that predates the direct-UP bridge.
    ByteCopy(g_command_bytes, &header, sizeof(header));
    ByteCopy(g_command_bytes + sizeof(header), vertex_stream_zero_data, vertex_bytes);
    return SendCommand(g_command_bytes, sizeof(header) + vertex_bytes);
}

HRESULT WINAPI Device_Present(
    void* self, const void*, const void*, HWND, const void*) {
    TraceDeviceCall(17, static_cast<DeviceObject*>(self)->present_calls + 1u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    ++device->present_calls;
    InitializeProfileCounter();
    CommandPresentProfile command = {
        COMMAND_PRESENT, device->present_calls,
        CounterLow(g_profile_packet_copy_ticks), CounterHigh(g_profile_packet_copy_ticks),
        g_profile_packet_copy_calls, g_profile_packet_copy_bytes,
        CounterLow(g_profile_upload_int_ticks), CounterHigh(g_profile_upload_int_ticks),
        g_profile_upload_int_calls,
        CounterLow(g_profile_present_int_ticks), CounterHigh(g_profile_present_int_ticks),
        g_profile_present_int_calls,
        CounterLow(g_profile_counter_frequency), CounterHigh(g_profile_counter_frequency),
        CounterLow(g_profile_timer_overhead_ticks), CounterHigh(g_profile_timer_overhead_ticks),
        {g_copy_site_calls[0], g_copy_site_calls[1], g_copy_site_calls[2],
         g_copy_site_calls[3], g_copy_site_calls[4], g_copy_site_calls[5],
         g_copy_site_calls[6]},
        {g_copy_site_bytes[0], g_copy_site_bytes[1], g_copy_site_bytes[2],
         g_copy_site_bytes[3], g_copy_site_bytes[4], g_copy_site_bytes[5],
         g_copy_site_bytes[6]}
#if D3D9WEB_STATE_EQUALITY_PROFILE
        , {g_state_calls[0], g_state_calls[1], g_state_calls[2]}
        , {g_state_same[0], g_state_same[1], g_state_same[2]}
#endif
    };
    const uint64_t int_start = ReadProfileCounter();
    const HRESULT result = SendCommand(&command, sizeof(command));
    g_profile_present_int_ticks += ReadProfileCounter() - int_start;
    ++g_profile_present_int_calls;
#if D3D9WEB_TRANSIENT_TEXTURE_RECYCLE
    if (SUCCEEDED(result)) PromoteTransientTextureAfterPresent();
#endif
    return result;
}

HRESULT WINAPI Device_BeginScene(void* self) {
    TraceDeviceCall(41, static_cast<DeviceObject*>(self)->begin_calls + 1u);
    ++static_cast<DeviceObject*>(self)->begin_calls;
    return S_OK;
}

HRESULT WINAPI Device_EndScene(void* self) {
    TraceDeviceCall(42, static_cast<DeviceObject*>(self)->end_calls + 1u);
    ++static_cast<DeviceObject*>(self)->end_calls;
    return S_OK;
}

HRESULT WINAPI Device_Clear(
    void* self, DWORD count, const D3DRECT* rects, DWORD flags,
    D3DCOLOR color, float z, DWORD stencil) {
    TraceDeviceCall(43, color);
    const DWORD valid_flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
    if (!(flags & valid_flags) || (flags & ~valid_flags) ||
        (count && !rects) ||
        count > (kMaxCommandBytes - sizeof(CommandClear)) / sizeof(D3DRECT)) {
        return D3DERR_INVALIDCALL;
    }
    DeviceObject* device = static_cast<DeviceObject*>(self);
    device->clear_color = color;
    ++device->clear_calls;
    CommandClear command = {COMMAND_CLEAR, count, flags, color, z, stencil};
    TrackCopySite(COPY_CLEAR, sizeof(command));
    ByteCopy(g_command_bytes, &command, sizeof(command));
    if (count) {
        TrackCopySite(COPY_CLEAR, count * sizeof(D3DRECT));
        ByteCopy(g_command_bytes + sizeof(command), rects, count * sizeof(D3DRECT));
    }
    return SendCommand(g_command_bytes, sizeof(command) + count * sizeof(D3DRECT));
}

HRESULT WINAPI Device_EndStateBlock(
    void* self, IDirect3DStateBlock9** out) {
    TraceDeviceCall(61, out ? 1u : 0u);
    DeviceObject* device = static_cast<DeviceObject*>(self);
    if (!out || !device->recording_state_block || !device->recording_block) {
        return D3DERR_INVALIDCALL;
    }
    StateBlockObject* block = device->recording_block;
    device->recording_block = 0;
    device->recording_state_block = FALSE;
    *out = reinterpret_cast<IDirect3DStateBlock9*>(block);
    return D3D_OK;
}

HRESULT WINAPI StateBlock_QueryInterface(void* self, const void*, void** out) {
    if (!out) return E_POINTER;
    *out = self;
    InterlockedIncrement(&static_cast<StateBlockObject*>(self)->refs);
    return S_OK;
}

ULONG WINAPI StateBlock_AddRef(void* self) {
    return static_cast<ULONG>(
        InterlockedIncrement(&static_cast<StateBlockObject*>(self)->refs));
}

void StateBlockReleaseReferences(StateBlockObject* block) {
    for (UINT stage = 0; stage < 8; ++stage) {
        if (block->textures[stage]) {
            Resource_Release(&block->textures[stage]->base);
            block->textures[stage] = 0;
        }
    }
    if (block->stream0) {
        Resource_Release(&block->stream0->base);
        block->stream0 = 0;
    }
    if (block->indices) {
        Resource_Release(&block->indices->base);
        block->indices = 0;
    }
    if (block->declaration) {
        Declaration_Release(block->declaration);
        block->declaration = 0;
    }
}

ULONG WINAPI StateBlock_Release(void* self) {
    StateBlockObject* block = static_cast<StateBlockObject*>(self);
    LONG refs = InterlockedDecrement(&block->refs);
    if (refs > 0) return static_cast<ULONG>(refs);
    StateBlockReleaseReferences(block);
    if (block->device) Device_Release(block->device);
    HeapFree(GetProcessHeap(), 0, block);
    return 0;
}

HRESULT WINAPI StateBlock_GetDevice(void* self, IDirect3DDevice9** out) {
    if (!out) return D3DERR_INVALIDCALL;
    StateBlockObject* block = static_cast<StateBlockObject*>(self);
    Device_AddRef(block->device);
    *out = reinterpret_cast<IDirect3DDevice9*>(block->device);
    return D3D_OK;
}

HRESULT WINAPI StateBlock_Capture(void* self) {
    StateBlockObject* block = static_cast<StateBlockObject*>(self);
    DeviceObject* device = block->device;
    if (!device || device->recording_state_block) return D3DERR_INVALIDCALL;
    for (UINT state = 0; state < 512; ++state) {
        if (block->transform_set[state]) block->transforms[state] = device->transforms[state];
    }
    for (UINT state = 0; state < 256; ++state) {
        if (block->render_state_set[state]) block->render_states[state] = device->render_states[state];
    }
    for (UINT stage = 0; stage < 8; ++stage) {
        for (UINT type = 0; type < 33; ++type) {
            if (block->texture_stage_state_set[stage][type]) {
                block->texture_stage_states[stage][type] = device->texture_stage_states[stage][type];
            }
        }
        for (UINT type = 0; type < 14; ++type) {
            if (block->sampler_state_set[stage][type]) {
                block->sampler_states[stage][type] = device->sampler_states[stage][type];
            }
        }
        if (block->texture_set[stage]) {
            TextureObject* replacement = device->textures[stage];
            if (replacement) Resource_AddRef(&replacement->base);
            if (block->textures[stage]) Resource_Release(&block->textures[stage]->base);
            block->textures[stage] = replacement;
        }
    }
    if (block->viewport_set) block->viewport = device->viewport;
    if (block->scissor_set) block->scissor = device->scissor;
    if (block->material_set) block->material = device->material;
    for (UINT index = 0; index < 8; ++index) {
        if (block->light_set[index]) block->lights[index] = device->lights[index];
        if (block->light_enabled_set[index]) {
            block->light_enabled[index] = device->light_enabled[index];
        }
    }
    if (block->stream0_set) {
        BufferObject* replacement = device->stream0;
        if (replacement) Resource_AddRef(&replacement->base);
        if (block->stream0) Resource_Release(&block->stream0->base);
        block->stream0 = replacement;
        block->stream0_offset = device->stream0_offset;
        block->stream0_stride = device->stream0_stride;
    }
    if (block->indices_set) {
        BufferObject* replacement = device->indices;
        if (replacement) Resource_AddRef(&replacement->base);
        if (block->indices) Resource_Release(&block->indices->base);
        block->indices = replacement;
    }
    // D3D9 has one vertex-input state.  An FVF internally replaces the
    // declaration and vice versa, so Capture must preserve the live tagged
    // alternative rather than blindly capture the originally recorded kind.
    if (block->declaration_set || block->fvf_set) {
        if (device->declaration) {
            Declaration_AddRef(device->declaration);
            if (block->declaration) Declaration_Release(block->declaration);
            block->declaration = device->declaration;
            block->declaration_set = TRUE;
            block->fvf_set = FALSE;
        } else if (device->fvf_valid) {
            if (block->declaration) Declaration_Release(block->declaration);
            block->declaration = 0;
            block->declaration_set = FALSE;
            block->fvf_set = TRUE;
            block->fvf = device->fvf;
        } else {
            if (block->declaration) Declaration_Release(block->declaration);
            block->declaration = 0;
            block->declaration_set = TRUE;
            block->fvf_set = FALSE;
        }
    }
    return D3D_OK;
}

HRESULT StateBlockApplyResult(HRESULT aggregate, HRESULT current) {
    return FAILED(aggregate) ? aggregate : current;
}

HRESULT WINAPI StateBlock_Apply(void* self) {
    StateBlockObject* block = static_cast<StateBlockObject*>(self);
    DeviceObject* device = block->device;
    if (!device || device->recording_state_block) return D3DERR_INVALIDCALL;
    HRESULT result = D3D_OK;
    for (UINT state = 0; state < 512; ++state) {
        if (block->transform_set[state]) {
            result = StateBlockApplyResult(result, Device_SetTransform(
                device, static_cast<D3DTRANSFORMSTATETYPE>(state), &block->transforms[state]));
        }
    }
    if (block->viewport_set) {
        result = StateBlockApplyResult(result, Device_SetViewport(device, &block->viewport));
    }
    if (block->scissor_set) {
        result = StateBlockApplyResult(result, Device_SetScissorRect(device, &block->scissor));
    }
    if (block->material_set) {
        result = StateBlockApplyResult(result, Device_SetMaterial(device, &block->material));
    }
    for (UINT index = 0; index < 8; ++index) {
        if (block->light_set[index]) {
            result = StateBlockApplyResult(result, Device_SetLight(device, index, &block->lights[index]));
        }
        if (block->light_enabled_set[index]) {
            result = StateBlockApplyResult(result, Device_LightEnable(
                device, index, block->light_enabled[index]));
        }
    }
    for (UINT state = 0; state < 256; ++state) {
        if (block->render_state_set[state]) {
            result = StateBlockApplyResult(result, Device_SetRenderState(
                device, static_cast<D3DRENDERSTATETYPE>(state), block->render_states[state]));
        }
    }
    for (UINT stage = 0; stage < 8; ++stage) {
        for (UINT type = 0; type < 33; ++type) {
            if (block->texture_stage_state_set[stage][type]) {
                result = StateBlockApplyResult(result, Device_SetTextureStageState(
                    device, stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                    block->texture_stage_states[stage][type]));
            }
        }
        for (UINT type = 0; type < 14; ++type) {
            if (block->sampler_state_set[stage][type]) {
                result = StateBlockApplyResult(result, Device_SetSamplerState(
                    device, stage, static_cast<D3DSAMPLERSTATETYPE>(type),
                    block->sampler_states[stage][type]));
            }
        }
    }
    if (block->vertex_shader_set) {
        result = StateBlockApplyResult(result, Device_SetVertexShader(device, 0));
    }
    if (block->pixel_shader_set) {
        result = StateBlockApplyResult(result, Device_SetPixelShader(device, 0));
    }
    if (block->declaration_set) {
        result = StateBlockApplyResult(result, Device_SetVertexDeclaration(
            device, reinterpret_cast<IDirect3DVertexDeclaration9*>(block->declaration)));
    }
    if (block->fvf_set) {
        result = StateBlockApplyResult(result, Device_SetFVF(device, block->fvf));
    }
    if (block->stream0_set) {
        result = StateBlockApplyResult(result, Device_SetStreamSource(
            device, 0, reinterpret_cast<IDirect3DVertexBuffer9*>(block->stream0),
            block->stream0_offset, block->stream0_stride));
    }
    if (block->indices_set) {
        result = StateBlockApplyResult(result, Device_SetIndices(
            device, reinterpret_cast<IDirect3DIndexBuffer9*>(block->indices)));
    }
    for (UINT stage = 0; stage < 8; ++stage) {
        if (block->texture_set[stage]) {
            result = StateBlockApplyResult(result, Device_SetTexture(
                device, stage, reinterpret_cast<IDirect3DBaseTexture9*>(block->textures[stage])));
        }
    }
    return result;
}

#if D3D9WEB_TRACE_MISSING_SLOT
// Diagnostic only - see the SlotFiller call at the end of InitializeTables.
// Marker state id 0x9500 is not a real render state, so the host stores it and
// falls through applyRenderState's default case with no side effect.
template <UINT Slot>
HRESULT WINAPI Device_MissingSlot() {
    Command2 mark = {COMMAND_SET_RENDER_STATE, 0x9500u, Slot};
    SendCommand(&mark, sizeof(mark));
    return E_NOTIMPL;
}

template <UINT N>
struct SlotFiller {
    static void Fill() {
        SlotFiller<N - 1>::Fill();
        if (!g_device_vtable[N]) {
            g_device_vtable[N] = reinterpret_cast<void*>(&Device_MissingSlot<N>);
        }
    }
};

template <>
struct SlotFiller<0> {
    static void Fill() {
        if (!g_device_vtable[0]) {
            g_device_vtable[0] = reinterpret_cast<void*>(&Device_MissingSlot<0>);
        }
    }
};
#endif

void InitializeTables() {
    if (InterlockedCompareExchange(&g_tables_ready, 1, 0) != 0) return;

    g_d3d9_vtable[0] = reinterpret_cast<void*>(&D3D9_QueryInterface);
    g_d3d9_vtable[1] = reinterpret_cast<void*>(&D3D9_AddRef);
    g_d3d9_vtable[2] = reinterpret_cast<void*>(&D3D9_Release);
    g_d3d9_vtable[3] = reinterpret_cast<void*>(&D3D9_RegisterSoftwareDevice);
    g_d3d9_vtable[4] = reinterpret_cast<void*>(&D3D9_GetAdapterCount);
    g_d3d9_vtable[5] = reinterpret_cast<void*>(&D3D9_GetAdapterIdentifier);
    g_d3d9_vtable[6] = reinterpret_cast<void*>(&D3D9_GetAdapterModeCount);
    g_d3d9_vtable[7] = reinterpret_cast<void*>(&D3D9_EnumAdapterModes);
    g_d3d9_vtable[8] = reinterpret_cast<void*>(&D3D9_GetAdapterDisplayMode);
    g_d3d9_vtable[9] = reinterpret_cast<void*>(&D3D9_CheckDeviceType);
    g_d3d9_vtable[10] = reinterpret_cast<void*>(&D3D9_CheckDeviceFormat);
    g_d3d9_vtable[11] = reinterpret_cast<void*>(&D3D9_CheckDeviceMultiSampleType);
    g_d3d9_vtable[12] = reinterpret_cast<void*>(&D3D9_CheckDepthStencilMatch);
    g_d3d9_vtable[13] = reinterpret_cast<void*>(&D3D9_CheckDeviceFormatConversion);
    g_d3d9_vtable[14] = reinterpret_cast<void*>(&D3D9_GetDeviceCaps);
    g_d3d9_vtable[15] = reinterpret_cast<void*>(&D3D9_GetAdapterMonitor);
    g_d3d9_vtable[16] = reinterpret_cast<void*>(&D3D9_CreateDevice);

    g_device_vtable[0] = reinterpret_cast<void*>(&Device_QueryInterface);
    g_device_vtable[1] = reinterpret_cast<void*>(&Device_AddRef);
    g_device_vtable[2] = reinterpret_cast<void*>(&Device_Release);
    g_device_vtable[3] = reinterpret_cast<void*>(&Device_TestCooperativeLevel);
    g_device_vtable[4] = reinterpret_cast<void*>(&Device_GetAvailableTextureMem);
    g_device_vtable[6] = reinterpret_cast<void*>(&Device_GetDirect3D);
    g_device_vtable[7] = reinterpret_cast<void*>(&Device_GetDeviceCaps);
    g_device_vtable[8] = reinterpret_cast<void*>(&Device_GetDisplayMode);
    g_device_vtable[9] = reinterpret_cast<void*>(&Device_GetCreationParameters);
    g_device_vtable[12] = reinterpret_cast<void*>(&Device_ShowCursor);
    g_device_vtable[16] = reinterpret_cast<void*>(&Device_Reset);
    g_device_vtable[17] = reinterpret_cast<void*>(&Device_Present);
    g_device_vtable[18] = reinterpret_cast<void*>(&Device_GetBackBuffer);
    g_device_vtable[23] = reinterpret_cast<void*>(&Device_CreateTexture);
    g_device_vtable[26] = reinterpret_cast<void*>(&Device_CreateVertexBuffer);
    g_device_vtable[27] = reinterpret_cast<void*>(&Device_CreateIndexBuffer);
    g_device_vtable[30] = reinterpret_cast<void*>(&Device_UpdateSurface);
    g_device_vtable[38] = reinterpret_cast<void*>(&Device_GetRenderTarget);
    g_device_vtable[41] = reinterpret_cast<void*>(&Device_BeginScene);
    g_device_vtable[42] = reinterpret_cast<void*>(&Device_EndScene);
    g_device_vtable[43] = reinterpret_cast<void*>(&Device_Clear);
    g_device_vtable[44] = reinterpret_cast<void*>(&Device_SetTransform);
    g_device_vtable[47] = reinterpret_cast<void*>(&Device_SetViewport);
    g_device_vtable[48] = reinterpret_cast<void*>(&Device_GetViewport);
    g_device_vtable[49] = reinterpret_cast<void*>(&Device_SetMaterial);
    g_device_vtable[51] = reinterpret_cast<void*>(&Device_SetLight);
    g_device_vtable[53] = reinterpret_cast<void*>(&Device_LightEnable);
    g_device_vtable[57] = reinterpret_cast<void*>(&Device_SetRenderState);
    g_device_vtable[60] = reinterpret_cast<void*>(&Device_BeginStateBlock);
    g_device_vtable[61] = reinterpret_cast<void*>(&Device_EndStateBlock);
    g_device_vtable[65] = reinterpret_cast<void*>(&Device_SetTexture);
    g_device_vtable[67] = reinterpret_cast<void*>(&Device_SetTextureStageState);
    g_device_vtable[69] = reinterpret_cast<void*>(&Device_SetSamplerState);
    g_device_vtable[70] = reinterpret_cast<void*>(&Device_ValidateDevice);
    g_device_vtable[71] = reinterpret_cast<void*>(&Device_SetPaletteEntries);
    g_device_vtable[75] = reinterpret_cast<void*>(&Device_SetScissorRect);
    g_device_vtable[76] = reinterpret_cast<void*>(&Device_GetScissorRect);
    g_device_vtable[79] = reinterpret_cast<void*>(&Device_SetNPatchMode);
    g_device_vtable[82] = reinterpret_cast<void*>(&Device_DrawIndexedPrimitive);
    g_device_vtable[83] = reinterpret_cast<void*>(&Device_DrawPrimitiveUP);
    g_device_vtable[86] = reinterpret_cast<void*>(&Device_CreateVertexDeclaration);
    g_device_vtable[87] = reinterpret_cast<void*>(&Device_SetVertexDeclaration);
    g_device_vtable[89] = reinterpret_cast<void*>(&Device_SetFVF);
    g_device_vtable[92] = reinterpret_cast<void*>(&Device_SetVertexShader);
    g_device_vtable[100] = reinterpret_cast<void*>(&Device_SetStreamSource);
    g_device_vtable[104] = reinterpret_cast<void*>(&Device_SetIndices);
    g_device_vtable[105] = reinterpret_cast<void*>(&Device_GetIndices);
    g_device_vtable[107] = reinterpret_cast<void*>(&Device_SetPixelShader);
    g_device_vtable[118] = reinterpret_cast<void*>(&Device_CreateQuery);
#if D3D9WEB_TRACE_MISSING_SLOT
    // Diagnostic only.  70 of the 119 IDirect3DDevice9 slots are left NULL, so
    // calling one branches straight to address 0 - that is the eip=00000000
    // crash in doc 534.  Filling them with a recording stub turns the crash
    // into a message naming the slot.  The stub returns with `ret 0` while a
    // stdcall caller expects callee cleanup, so the guest is unstable after the
    // first hit; that is acceptable because it was crashing anyway and the
    // marker has already been sent.  Never ship this build.
    SlotFiller<118>::Fill();
#endif

    g_texture_vtable[0] = reinterpret_cast<void*>(&Resource_QueryInterface);
    g_texture_vtable[1] = reinterpret_cast<void*>(&Resource_AddRef);
    g_texture_vtable[2] = reinterpret_cast<void*>(&Resource_Release);
    g_texture_vtable[3] = reinterpret_cast<void*>(&Resource_GetDevice);
    g_texture_vtable[4] = reinterpret_cast<void*>(&Resource_SetPrivateData);
    g_texture_vtable[5] = reinterpret_cast<void*>(&Resource_GetPrivateData);
    g_texture_vtable[6] = reinterpret_cast<void*>(&Resource_FreePrivateData);
    g_texture_vtable[7] = reinterpret_cast<void*>(&Resource_SetPriority);
    g_texture_vtable[8] = reinterpret_cast<void*>(&Resource_GetPriority);
    g_texture_vtable[9] = reinterpret_cast<void*>(&Resource_PreLoad);
    g_texture_vtable[10] = reinterpret_cast<void*>(&Resource_GetType);
    g_texture_vtable[11] = reinterpret_cast<void*>(&Texture_SetLOD);
    g_texture_vtable[12] = reinterpret_cast<void*>(&Texture_GetLOD);
    g_texture_vtable[13] = reinterpret_cast<void*>(&Texture_GetLevelCount);
    g_texture_vtable[14] = reinterpret_cast<void*>(&Texture_SetAutoGenFilterType);
    g_texture_vtable[15] = reinterpret_cast<void*>(&Texture_GetAutoGenFilterType);
    g_texture_vtable[16] = reinterpret_cast<void*>(&Texture_GenerateMipSubLevels);
    g_texture_vtable[17] = reinterpret_cast<void*>(&Texture_GetLevelDesc);
    g_texture_vtable[18] = reinterpret_cast<void*>(&Texture_GetSurfaceLevel);
    g_texture_vtable[19] = reinterpret_cast<void*>(&Texture_LockRect);
    g_texture_vtable[20] = reinterpret_cast<void*>(&Texture_UnlockRect);
    g_texture_vtable[21] = reinterpret_cast<void*>(&Texture_AddDirtyRect);

    for (UINT i = 0; i <= 10; ++i) {
        g_vertex_buffer_vtable[i] = g_texture_vtable[i];
        g_index_buffer_vtable[i] = g_texture_vtable[i];
    }
    g_vertex_buffer_vtable[11] = reinterpret_cast<void*>(&Buffer_Lock);
    g_vertex_buffer_vtable[12] = reinterpret_cast<void*>(&Buffer_Unlock);
    g_vertex_buffer_vtable[13] = reinterpret_cast<void*>(&VertexBuffer_GetDesc);
    g_index_buffer_vtable[11] = reinterpret_cast<void*>(&Buffer_Lock);
    g_index_buffer_vtable[12] = reinterpret_cast<void*>(&Buffer_Unlock);
    g_index_buffer_vtable[13] = reinterpret_cast<void*>(&IndexBuffer_GetDesc);

    g_surface_vtable[0] = reinterpret_cast<void*>(&Surface_QueryInterface);
    g_surface_vtable[1] = reinterpret_cast<void*>(&Surface_AddRef);
    g_surface_vtable[2] = reinterpret_cast<void*>(&Surface_Release);
    g_surface_vtable[3] = reinterpret_cast<void*>(&Surface_GetDevice);
    g_surface_vtable[4] = reinterpret_cast<void*>(&Surface_SetPrivateData);
    g_surface_vtable[5] = reinterpret_cast<void*>(&Surface_GetPrivateData);
    g_surface_vtable[6] = reinterpret_cast<void*>(&Surface_FreePrivateData);
    g_surface_vtable[7] = reinterpret_cast<void*>(&Surface_SetPriority);
    g_surface_vtable[8] = reinterpret_cast<void*>(&Surface_GetPriority);
    g_surface_vtable[9] = reinterpret_cast<void*>(&Surface_PreLoad);
    g_surface_vtable[10] = reinterpret_cast<void*>(&Surface_GetType);
    g_surface_vtable[11] = reinterpret_cast<void*>(&Surface_GetContainer);
    g_surface_vtable[12] = reinterpret_cast<void*>(&Surface_GetDesc);
    g_surface_vtable[13] = reinterpret_cast<void*>(&Surface_LockRect);
    g_surface_vtable[14] = reinterpret_cast<void*>(&Surface_UnlockRect);
    g_surface_vtable[15] = reinterpret_cast<void*>(&Surface_GetDC);
    g_surface_vtable[16] = reinterpret_cast<void*>(&Surface_ReleaseDC);

    g_declaration_vtable[0] = reinterpret_cast<void*>(&Declaration_QueryInterface);
    g_declaration_vtable[1] = reinterpret_cast<void*>(&Declaration_AddRef);
    g_declaration_vtable[2] = reinterpret_cast<void*>(&Declaration_Release);
    g_declaration_vtable[3] = reinterpret_cast<void*>(&Declaration_GetDevice);
    g_declaration_vtable[4] = reinterpret_cast<void*>(&Declaration_GetDeclaration);

    g_query_vtable[0] = reinterpret_cast<void*>(&Query_QueryInterface);
    g_query_vtable[1] = reinterpret_cast<void*>(&Query_AddRef);
    g_query_vtable[2] = reinterpret_cast<void*>(&Query_Release);
    g_query_vtable[3] = reinterpret_cast<void*>(&Query_GetDevice);
    g_query_vtable[4] = reinterpret_cast<void*>(&Query_GetType);
    g_query_vtable[5] = reinterpret_cast<void*>(&Query_GetDataSize);
    g_query_vtable[6] = reinterpret_cast<void*>(&Query_Issue);
    g_query_vtable[7] = reinterpret_cast<void*>(&Query_GetData);

    g_state_block_vtable[0] = reinterpret_cast<void*>(&StateBlock_QueryInterface);
    g_state_block_vtable[1] = reinterpret_cast<void*>(&StateBlock_AddRef);
    g_state_block_vtable[2] = reinterpret_cast<void*>(&StateBlock_Release);
    g_state_block_vtable[3] = reinterpret_cast<void*>(&StateBlock_GetDevice);
    g_state_block_vtable[4] = reinterpret_cast<void*>(&StateBlock_Capture);
    g_state_block_vtable[5] = reinterpret_cast<void*>(&StateBlock_Apply);
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk_version) {
    // One scalar marker distinguishes "native d3d9.dll never loaded" from a
    // failure later in the COM/device path without adding file I/O or hooks.
    TraceD3D9Once(0, sdk_version);
    // Acknex 6.31 calls the legacy D3D9 SDK value 31. Keep 32 for the
    // modern capability harness while accepting the original client's value.
    if (sdk_version != 31u && sdk_version != 32u) return 0;
    InitializeTables();
    g_d3d9.refs = 1;
    return reinterpret_cast<IDirect3D9*>(&g_d3d9);
}
