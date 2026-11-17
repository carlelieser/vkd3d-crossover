/* XeSS probe: drives a game's own libxess.dll (XeSS 2, D3D12 API) on vkd3d-proton.
 * Upscales a flat-colour frame to 1920x1080 and checks every output pixel (and NaN/Inf/black).
 * Declarations follow Intel's published xess.h / xess_d3d12.h. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

typedef struct _xess_context_handle_t *xess_context_handle_t;
#pragma pack(push, 8)
typedef struct { uint16_t major, minor, patch, reserved; } xess_version_t;
typedef struct { uint32_t x, y; } xess_2d_t;
typedef struct { uint32_t requiredDescriptorCount; uint64_t tempBufferHeapSize, tempTextureHeapSize; } xess_properties_t;
typedef struct {
    ID3D12Resource *pColorTexture, *pVelocityTexture, *pDepthTexture, *pExposureScaleTexture, *pResponsivePixelMaskTexture, *pOutputTexture;
    float jitterOffsetX, jitterOffsetY, exposureScale;
    uint32_t resetHistory, inputWidth, inputHeight;
    xess_2d_t inputColorBase, inputMotionVectorBase, inputDepthBase, inputResponsiveMaskBase, reserved0, outputColorBase;
    ID3D12DescriptorHeap *pDescriptorHeap; uint32_t descriptorHeapOffset;
} xess_d3d12_execute_params_t;
typedef struct {
    xess_2d_t outputResolution; int32_t qualitySetting; uint32_t initFlags, creationNodeMask, visibleNodeMask;
    ID3D12Heap *pTempBufferHeap; uint64_t bufferHeapOffset; ID3D12Heap *pTempTextureHeap; uint64_t textureHeapOffset;
    ID3D12PipelineLibrary *pPipelineLibrary;
} xess_d3d12_init_params_t;
#pragma pack(pop)
#define XESS_QUALITY_SETTING_BALANCED 102
#define XESS_INIT_FLAG_LDR_INPUT_COLOR (1 << 6)
#define XESS_INIT_FLAG_INVERTED_DEPTH (1 << 1)
typedef void (*xess_log_cb)(const char *, int);
typedef int (*PFN_GetVersion)(xess_version_t *);
typedef int (*PFN_CreateContext)(ID3D12Device *, xess_context_handle_t *);
typedef int (*PFN_Init)(xess_context_handle_t, const xess_d3d12_init_params_t *);
typedef int (*PFN_Execute)(xess_context_handle_t, ID3D12GraphicsCommandList *, const xess_d3d12_execute_params_t *);
typedef int (*PFN_GetInputResolution)(xess_context_handle_t, const xess_2d_t *, int32_t, xess_2d_t *);
typedef int (*PFN_GetXeFX)(xess_context_handle_t, xess_version_t *);
typedef int (*PFN_SetLogging)(xess_context_handle_t, int, xess_log_cb);
typedef int (*PFN_Destroy)(xess_context_handle_t);
typedef int (*PFN_IsOptimal)(xess_context_handle_t);

static ID3D12Device *dev;
static ID3D12CommandQueue *cq;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static int fails;


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


static void on_log(const char *msg, int level)
{
    if (level >= 1) { printf("  xess log[%d]: %s\n", level, msg); fflush(stdout); }
}

static void run(HMODULE x, const char *name, uint32_t flags, float r, float g, float b)
{
    PFN_CreateContext create = (PFN_CreateContext)GetProcAddress(x, "xessD3D12CreateContext");
    PFN_Init init = (PFN_Init)GetProcAddress(x, "xessD3D12Init");
    PFN_Execute exec = (PFN_Execute)GetProcAddress(x, "xessD3D12Execute");
    PFN_GetInputResolution in_res = (PFN_GetInputResolution)GetProcAddress(x, "xessGetInputResolution");
    PFN_GetXeFX xefx = (PFN_GetXeFX)GetProcAddress(x, "xessGetIntelXeFXVersion");
    PFN_SetLogging setlog = (PFN_SetLogging)GetProcAddress(x, "xessSetLoggingCallback");
    PFN_Destroy destroy = (PFN_Destroy)GetProcAddress(x, "xessDestroyContext");
    const UINT ow = 1920, oh = 1080;
    xess_2d_t out_res = { ow, oh }, rin = { 0, 0 };
    xess_d3d12_init_params_t ip;
    xess_context_handle_t ctx = NULL;
    xess_version_t fxv = {0};
    ID3D12Resource *color, *depth, *mv, *out, *rb;
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 };
    D3D12_HEAP_PROPERTIES rbp = { D3D12_HEAP_TYPE_READBACK };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION src, dst;
    D3D12_RESOURCE_DESC rbd, od;
    ID3D12DescriptorHeap *rtvs;
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    const float cc[4] = { r, g, b, 1.0f }, dc[4] = { 0.5f, 0, 0, 0 }, zc[4] = { 0 };
    unsigned int nan = 0, zero = 0, off = 0, i, px, py, inc;
    float maxerr = 0, cr = 0, cg = 0, cb = 0;
    UINT64 total;
    BYTE *p;
    int rc;

    printf("\n== %s\n", name);
    rc = create(dev, &ctx);
    printf("  xessD3D12CreateContext: %d%s\n", rc, rc < 0 ? " <<<" : "");
    if (rc < 0) { fails++; return; }
    if (setlog) setlog(ctx, 1, on_log);
    if (xefx && !xefx(ctx, &fxv)) printf("  XeFX version %u.%u.%u\n", fxv.major, fxv.minor, fxv.patch);
    rc = in_res(ctx, &out_res, XESS_QUALITY_SETTING_BALANCED, &rin);
    printf("  input resolution for Balanced: %ux%u (rc %d)\n", rin.x, rin.y, rc);
    memset(&ip, 0, sizeof(ip));
    ip.outputResolution = out_res; ip.qualitySetting = XESS_QUALITY_SETTING_BALANCED; ip.initFlags = flags;
    rc = init(ctx, &ip);
    printf("  xessD3D12Init: %d%s\n", rc, rc < 0 ? " <<<" : "");
    if (rc < 0) { fails++; destroy(ctx); return; }

    color = tex(rin.x, rin.y, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    depth = tex(rin.x, rin.y, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mv = tex(rin.x, rin.y, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    out = tex(ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtvs);
    inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (i = 0; i < 4; i++)
    {
        xess_d3d12_execute_params_t ep;
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
        memset(&ep, 0, sizeof(ep));
        ep.pColorTexture = color; ep.pVelocityTexture = mv; ep.pDepthTexture = depth; ep.pOutputTexture = out;
        ep.exposureScale = 1.0f; ep.resetHistory = (i == 0); ep.inputWidth = rin.x; ep.inputHeight = rin.y;
        rc = exec(ctx, list, &ep);
        if (rc < 0) { printf("  xessD3D12Execute frame %u: %d <<<\n", i, rc); fails++; }
        ID3D12GraphicsCommandList_Close(list);
        ID3D12CommandQueue_ExecuteCommandLists(cq, 1, (ID3D12CommandList **)&list);
        gpu_wait();
    }

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
    for (py = 0; py < oh; py++)
        for (px = 0; px < ow; px++)
        {
            const uint16_t *q = (const uint16_t *)(p + fp.Offset + py * fp.Footprint.RowPitch + px * 8);
            float v[3] = { half_to_float(q[0]), half_to_float(q[1]), half_to_float(q[2]) };
            float e = fmaxf(fabsf(v[0] - r), fmaxf(fabsf(v[1] - g), fabsf(v[2] - b)));
            if (isnan(v[0]) || isnan(v[1]) || isnan(v[2]) || isinf(v[0]) || isinf(v[1]) || isinf(v[2])) { nan++; continue; }
            if (v[0] == 0 && v[1] == 0 && v[2] == 0) zero++;
            if (e > 0.05f * fmaxf(r, fmaxf(g, b)) + 0.004f) off++;
            if (e > maxerr) maxerr = e;
            if (px == ow / 2 && py == oh / 2) { cr = v[0]; cg = v[1]; cb = v[2]; }
        }
    ID3D12Resource_Unmap(rb, 0, NULL);
    printf("  input (%.3f, %.3f, %.3f) -> centre (%.3f, %.3f, %.3f); NaN/Inf %u, black %u, off-colour(>5%%) %u of %u, max error %.4f\n",
            r, g, b, cr, cg, cb, nan, zero, off, ow * oh, maxerr);
    if (nan || zero || off) { printf("  RESULT: FAIL <<<\n"); fails++; } else printf("  RESULT: OK\n");
    destroy(ctx);
    ID3D12Resource_Release(rb); ID3D12Resource_Release(color); ID3D12Resource_Release(depth);
    ID3D12Resource_Release(mv); ID3D12Resource_Release(out); ID3D12DescriptorHeap_Release(rtvs);
}

int main(void)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    xess_version_t v = {0};
    PFN_GetVersion getver;
    HMODULE x;
    HRESULT hr;

    if (!(x = LoadLibraryA("libxess.dll"))) { printf("libxess.dll not found (%lu)\n", GetLastError()); return 1; }
    if ((getver = (PFN_GetVersion)GetProcAddress(x, "xessGetVersion")) && !getver(&v))
        printf("XeSS %u.%u.%u\n", v.major, v.minor, v.patch);
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    printf("D3D12CreateDevice(12_0): 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&cq);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);

    run(x, "XeSS Balanced, HDR colour (values > 1)", 0, 4.0f, 2.0f, 0.5f);
    run(x, "XeSS Balanced, LDR colour, inverted depth", XESS_INIT_FLAG_LDR_INPUT_COLOR | XESS_INIT_FLAG_INVERTED_DEPTH, 0.5f, 0.25f, 0.125f);
    printf("\ndone, %d failures\n", fails);
    return 0;
}
