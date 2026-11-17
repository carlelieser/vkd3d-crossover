/* FSR probe: drives a game's own amd_fidelityfx_dx12.dll (ffx_api) on vkd3d-proton.
 * Upscales a flat-colour 960x540 frame to 1920x1080 for a few frames and checks every output pixel
 * against the input colour (and for NaN/Inf/zero). Declarations follow AMD's MIT-licensed ffx_api headers. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ---- ffx_api subset ---- */
typedef void *ffxContext;
typedef struct ffxApiHeader { uint64_t type; struct ffxApiHeader *pNext; } ffxApiHeader;
typedef void (*ffxApiMessage)(uint32_t type, const wchar_t *message);
struct FfxApiDimensions2D { uint32_t width, height; };
struct FfxApiFloatCoords2D { float x, y; };
struct FfxApiResourceDescription { uint32_t type, format, width, height, depth, mipCount, flags, usage; };
struct FfxApiResource { void *resource; struct FfxApiResourceDescription description; uint32_t state; };
struct ffxCreateBackendDX12Desc { ffxApiHeader header; ID3D12Device *device; };
struct ffxCreateContextDescUpscale { ffxApiHeader header; uint32_t flags; struct FfxApiDimensions2D maxRenderSize, maxUpscaleSize; ffxApiMessage fpMessage; };
struct ffxDispatchDescUpscale {
    ffxApiHeader header; void *commandList;
    struct FfxApiResource color, depth, motionVectors, exposure, reactive, transparencyAndComposition, output;
    struct FfxApiFloatCoords2D jitterOffset, motionVectorScale;
    struct FfxApiDimensions2D renderSize, upscaleSize;
    bool enableSharpening; float sharpness, frameTimeDelta, preExposure; bool reset;
    float cameraNear, cameraFar, cameraFovAngleVertical, viewSpaceToMetersFactor; uint32_t flags;
};
struct ffxQueryGetProviderVersion { ffxApiHeader header; uint64_t versionId; const char *versionName; };
struct ffxConfigureDescGlobalDebug { ffxApiHeader header; uint64_t effectId; ffxApiMessage fpMessage; uint32_t debugLevel; };

#define TYPE_BACKEND_DX12   0x00000002ull
#define TYPE_CREATE_UPSCALE 0x00010000ull
#define TYPE_DISPATCH_UPSCALE 0x00010001ull
#define TYPE_QUERY_PROVIDER_VERSION 6ull
#define TYPE_CONFIGURE_GLOBALDEBUG 7ull
#define FLAG_ENABLE_HDR (1u << 0)
#define FLAG_DEPTH_INVERTED (1u << 3)
#define FLAG_AUTO_EXPOSURE (1u << 5)
#define RES_TYPE_TEXTURE2D 2
#define FMT_R16G16B16A16_FLOAT 4
#define FMT_R16G16_FLOAT 18
#define FMT_R32_FLOAT 28
#define USAGE_READ_ONLY 0
#define USAGE_UAV 2
#define STATE_UAV (1u << 1)
#define STATE_COMPUTE_READ (1u << 2)

typedef uint32_t (*PFN_ffxCreateContext)(ffxContext *, ffxApiHeader *, const void *);
typedef uint32_t (*PFN_ffxDestroyContext)(ffxContext *, const void *);
typedef uint32_t (*PFN_ffxDispatch)(ffxContext *, const ffxApiHeader *);
typedef uint32_t (*PFN_ffxQuery)(ffxContext *, ffxApiHeader *);
typedef uint32_t (*PFN_ffxConfigure)(ffxContext *, const ffxApiHeader *);

static ID3D12Device *dev;
static ID3D12CommandQueue *cq;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static int fails;

static void on_message(uint32_t type, const wchar_t *msg)
{
    printf("  ffx %s: %ls\n", type == 0 ? "ERROR" : "warning", msg);
    fflush(stdout);
}

static void gpu_wait(void)
{
    ID3D12Fence *f = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&f);
    ID3D12CommandQueue_Signal(cq, f, 1);
    ID3D12Fence_SetEventOnCompletion(f, 1, ev);
    if (WaitForSingleObject(ev, 20000) != WAIT_OBJECT_0) { printf("GPU wait TIMEOUT <<<\n"); fails++; }
    CloseHandle(ev);
    ID3D12Fence_Release(f);
}

static ID3D12Resource *tex(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags };
    ID3D12Resource *r = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)&r);
    if (FAILED(hr)) printf("CreateCommittedResource %ux%u fmt %u failed 0x%08lx\n", w, h, fmt, (unsigned long)hr);
    return r;
}

static struct FfxApiResource ffx_res(ID3D12Resource *r, UINT w, UINT h, uint32_t fmt, uint32_t usage, uint32_t state)
{
    struct FfxApiResource res = { r, { RES_TYPE_TEXTURE2D, fmt, w, h, 1, 1, 0, usage }, state };
    return res;
}

static float half_to_float(uint16_t h)
{
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = ldexpf((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = ldexpf((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER br = {0};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    br.Transition.StateBefore = a;
    br.Transition.StateAfter = b;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &br);
}

static void run(HMODULE fx, const char *name, uint32_t ctx_flags, float r, float g, float b, float depth_value)
{
    PFN_ffxCreateContext create = (PFN_ffxCreateContext)GetProcAddress(fx, "ffxCreateContext");
    PFN_ffxDestroyContext destroy = (PFN_ffxDestroyContext)GetProcAddress(fx, "ffxDestroyContext");
    PFN_ffxDispatch dispatch = (PFN_ffxDispatch)GetProcAddress(fx, "ffxDispatch");
    PFN_ffxQuery query = (PFN_ffxQuery)GetProcAddress(fx, "ffxQuery");
    const UINT rw = 960, rh = 540, ow = 1920, oh = 1080;
    struct ffxCreateBackendDX12Desc backend = { { TYPE_BACKEND_DX12, NULL }, dev };
    struct ffxCreateContextDescUpscale cd = { { TYPE_CREATE_UPSCALE, &backend.header }, ctx_flags | FLAG_AUTO_EXPOSURE,
            { rw, rh }, { ow, oh }, on_message };
    struct ffxQueryGetProviderVersion pv = { { TYPE_QUERY_PROVIDER_VERSION, NULL }, 0, NULL };
    ID3D12Resource *color, *depth, *mv, *out, *rb;
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 };
    D3D12_HEAP_PROPERTIES rbp = { D3D12_HEAP_TYPE_READBACK };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION src, dst;
    D3D12_RESOURCE_DESC rbd, od;
    ID3D12DescriptorHeap *rtvs;
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    UINT inc, i, x, y;
    UINT64 total;
    ffxContext ctx = NULL;
    uint32_t rc;
    BYTE *p;
    const float cc[4] = { r, g, b, 1.0f }, dc[4] = { depth_value, 0, 0, 0 }, zc[4] = { 0, 0, 0, 0 };
    unsigned int nan = 0, zero = 0, off = 0;
    float maxerr = 0, cr = 0, cg = 0, cb = 0;

    printf("\n== %s\n", name);
    rc = create(&ctx, &cd.header, NULL);
    printf("  ffxCreateContext: %u%s\n", rc, rc ? " <<<" : "");
    if (rc) { fails++; return; }
    if (query && !query(&ctx, &pv.header))
        printf("  provider: %s (id 0x%llx)\n", pv.versionName ? pv.versionName : "?", (unsigned long long)pv.versionId);

    color = tex(rw, rh, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    depth = tex(rw, rh, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mv = tex(rw, rh, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    out = tex(ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtvs);
    inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (i = 0; i < 4; i++)
    {
        struct ffxDispatchDescUpscale dd;
        ID3D12CommandAllocator_Reset(alloc);
        ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
        if (i)
        {
            barrier(color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            barrier(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            barrier(mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtvs);
        ID3D12Device_CreateRenderTargetView(dev, color, NULL, h);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, h, cc, 0, NULL);
        h.ptr += inc; ID3D12Device_CreateRenderTargetView(dev, depth, NULL, h);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, h, dc, 0, NULL);
        h.ptr += inc; ID3D12Device_CreateRenderTargetView(dev, mv, NULL, h);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, h, zc, 0, NULL);
        barrier(color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(depth, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(mv, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        memset(&dd, 0, sizeof(dd));
        dd.header.type = TYPE_DISPATCH_UPSCALE;
        dd.commandList = list;
        dd.color = ffx_res(color, rw, rh, FMT_R16G16B16A16_FLOAT, USAGE_READ_ONLY, STATE_COMPUTE_READ);
        dd.depth = ffx_res(depth, rw, rh, FMT_R32_FLOAT, USAGE_READ_ONLY, STATE_COMPUTE_READ);
        dd.motionVectors = ffx_res(mv, rw, rh, FMT_R16G16_FLOAT, USAGE_READ_ONLY, STATE_COMPUTE_READ);
        dd.output = ffx_res(out, ow, oh, FMT_R16G16B16A16_FLOAT, USAGE_UAV, STATE_UAV);
        dd.motionVectorScale.x = (float)rw; dd.motionVectorScale.y = (float)rh;
        dd.renderSize.width = rw; dd.renderSize.height = rh;
        dd.upscaleSize.width = ow; dd.upscaleSize.height = oh;
        dd.frameTimeDelta = 16.6f; dd.preExposure = 1.0f; dd.reset = (i == 0);
        dd.cameraNear = 0.1f; dd.cameraFar = 1000.0f; dd.cameraFovAngleVertical = 1.0f; dd.viewSpaceToMetersFactor = 1.0f;
        rc = dispatch(&ctx, &dd.header);
        if (rc) { printf("  ffxDispatch frame %u: %u <<<\n", i, rc); fails++; }
        ID3D12GraphicsCommandList_Close(list);
        ID3D12CommandQueue_ExecuteCommandLists(cq, 1, (ID3D12CommandList **)&list);
        gpu_wait();
    }

    /* Read back the output. */
    od = ID3D12Resource_GetDesc(out);
    ID3D12Device_GetCopyableFootprints(dev, &od, 0, 1, 0, &fp, NULL, NULL, &total);
    memset(&rbd, 0, sizeof(rbd));
    rbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rbd.Width = total; rbd.Height = 1; rbd.DepthOrArraySize = 1;
    rbd.MipLevels = 1; rbd.SampleDesc.Count = 1; rbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Device_CreateCommittedResource(dev, &rbp, D3D12_HEAP_FLAG_NONE, &rbd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&rb);
    ID3D12CommandAllocator_Reset(alloc);
    ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
    barrier(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    src.pResource = out; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12CommandQueue_ExecuteCommandLists(cq, 1, (ID3D12CommandList **)&list);
    gpu_wait();
    ID3D12Resource_Map(rb, 0, NULL, (void **)&p);
    for (y = 0; y < oh; y++)
        for (x = 0; x < ow; x++)
        {
            const uint16_t *px = (const uint16_t *)(p + fp.Offset + y * fp.Footprint.RowPitch + x * 8);
            float v[3] = { half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]) };
            float e = fmaxf(fabsf(v[0] - r), fmaxf(fabsf(v[1] - g), fabsf(v[2] - b)));
            if (isnan(v[0]) || isnan(v[1]) || isnan(v[2]) || isinf(v[0]) || isinf(v[1]) || isinf(v[2])) { nan++; continue; }
            if (v[0] == 0 && v[1] == 0 && v[2] == 0) zero++;
            if (e > 0.02f * fmaxf(r, fmaxf(g, b)) + 0.004f) off++;
            if (e > maxerr) maxerr = e;
            if (x == ow / 2 && y == oh / 2) { cr = v[0]; cg = v[1]; cb = v[2]; }
        }
    ID3D12Resource_Unmap(rb, 0, NULL);
    printf("  input (%.3f, %.3f, %.3f) -> centre (%.3f, %.3f, %.3f); NaN/Inf %u, black %u, off-colour %u of %u, max error %.4f\n",
            r, g, b, cr, cg, cb, nan, zero, off, ow * oh, maxerr);
    if (nan || zero || off) { printf("  RESULT: FAIL <<<\n"); fails++; }
    else printf("  RESULT: OK\n");

    destroy(&ctx, NULL);
    ID3D12Resource_Release(rb); ID3D12Resource_Release(color); ID3D12Resource_Release(depth);
    ID3D12Resource_Release(mv); ID3D12Resource_Release(out); ID3D12DescriptorHeap_Release(rtvs);
}

int main(int argc, char **argv)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    PFN_ffxConfigure configure;
    HMODULE fx;
    HRESULT hr;

    const char *dll = argc > 1 ? argv[1] : "amd_fidelityfx_dx12.dll";
    if (!(fx = LoadLibraryA(dll))) { printf("%s not found (%lu)\n", dll, GetLastError()); return 1; }
    printf("using %s\n", dll);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    printf("D3D12CreateDevice(12_0): 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&cq);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);

    if ((configure = (PFN_ffxConfigure)GetProcAddress(fx, "ffxConfigure")))
    {
        struct ffxConfigureDescGlobalDebug gd = { { TYPE_CONFIGURE_GLOBALDEBUG, NULL }, 0, on_message, 0xfffffff };
        configure(NULL, &gd.header);
    }

    run(fx, "FSR upscale, SDR colour, standard depth", 0, 0.5f, 0.25f, 0.125f, 0.5f);
    run(fx, "FSR upscale, HDR colour (values > 1), inverted depth", FLAG_ENABLE_HDR | FLAG_DEPTH_INVERTED, 4.0f, 2.0f, 0.5f, 0.5f);
    printf("\ndone, %d failures\n", fails);
    return 0;
}
