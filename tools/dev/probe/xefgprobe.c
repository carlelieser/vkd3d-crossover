/* XeSS-FG / XeLL probe: drives the game's libxess_fg.dll + libxell.dll the way a game does
 * (XeLL context -> FG latency reduction -> init from swapchain -> present through the FG proxy with per-frame
 * resource tags), FG off then on. Presented pixels are checked by MoltenVK's present probe
 * (MVK_PRESENT_PROBE_LOG=1). Declarations follow Intel's public xefg_swapchain / xell headers. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma pack(push, 8)
typedef struct { IDXGISwapChain *pApplicationSwapChain; uint32_t initFlags, maxInterpolatedFrames, creationNodeMask, visibleNodeMask;
        ID3D12Heap *pTempBufferHeap; uint64_t bufferHeapOffset; ID3D12Heap *pTempTextureHeap; uint64_t textureHeapOffset;
        ID3D12PipelineLibrary *pPipelineLibrary; int32_t uiMode; } xefg_init_params;
typedef struct { ID3D12Resource *pResource; int32_t type; int32_t validity; uint32_t baseX, baseY, sizeX, sizeY; D3D12_RESOURCE_STATES incomingState; } xefg_res_data;
typedef struct { float jitterX, jitterY, mvScaleX, mvScaleY; uint32_t resetHistory; float frameRenderTime; } xefg_consts;
typedef struct { uint32_t bLowLatencyMode, bLowLatencyBoost, minimumIntervalUs; } xell_sleep_params;
#pragma pack(pop)
typedef struct { uint16_t major, minor, patch, reserved; } ver_t;
#define RES_HUDLESS 0
#define RES_DEPTH 1
#define RES_MV 2
#define RES_BACKBUFFER 4
#define RV_UNTIL_NEXT_PRESENT 0
#define XELL_SIMULATION_START 0
#define XELL_SIMULATION_END 1
#define XELL_RENDERSUBMIT_START 2
#define XELL_RENDERSUBMIT_END 3
#define XELL_PRESENT_START 4
#define XELL_PRESENT_END 5

#define FN(m, name, type) ((type)GetProcAddress(m, name))
typedef int (*PFN_p)(void *);
typedef int (*PFN_pu)(void *, uint32_t);
typedef int (*PFN_pp)(void *, void *);
typedef int (*PFN_dev_create)(ID3D12Device *, void **);

static ID3D12Device *dev;
static ID3D12CommandQueue *q;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;

static ID3D12Resource *tex(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags };
    ID3D12Resource *r = NULL;
    ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)&r);
    return r;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER br = {0};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition.pResource = r;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; br.Transition.StateBefore = a; br.Transition.StateAfter = b;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &br);
}

int main(void)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    HMODULE fg = LoadLibraryA("libxess_fg.dll"), xl = LoadLibraryA("libxell.dll");
    void *fgh = NULL, *xlh = NULL;
    IDXGIFactory4 *f; WNDCLASSA wc = {0}; HWND wnd; DXGI_SWAP_CHAIN_DESC1 d = {0};
    IDXGISwapChain1 *sc = NULL; IDXGISwapChain3 *proxy = NULL; ID3D12DescriptorHeap *rtv;
    ID3D12Resource *depth, *mv, *hudless; ID3D12Fence *fence; HANDLE ev;
    xefg_init_params ip = {0}; xell_sleep_params sp = { 1, 0, 0 };
    UINT64 fv = 0; unsigned int pass, i; int rc;
    static const float colors[2][4] = { { 0.5f, 0.25f, 0.75f, 1.0f }, { 0.25f, 0.75f, 0.5f, 1.0f } };

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!fg || !xl) { printf("libxess_fg/libxell missing\n"); return 1; }
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtv);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&fence);
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&f);

    rc = FN(xl, "xellD3D12CreateContext", PFN_dev_create)(dev, &xlh);
    printf("xellD3D12CreateContext: %d\n", rc);
    rc = ((int (*)(void *, const xell_sleep_params *))GetProcAddress(xl, "xellSetSleepMode"))(xlh, &sp);
    printf("xellSetSleepMode(low latency): %d\n", rc);
    rc = FN(fg, "xefgSwapChainD3D12CreateContext", PFN_dev_create)(dev, &fgh);
    printf("xefgSwapChainD3D12CreateContext: %d\n", rc);
    rc = FN(fg, "xefgSwapChainSetLatencyReduction", PFN_pp)(fgh, xlh);
    printf("xefgSwapChainSetLatencyReduction: %d\n", rc);

    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "xefg";
    RegisterClassA(&wc);
    wnd = CreateWindowExA(0, "xefg", "xefg", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 1280, 720, NULL, NULL, wc.hInstance, NULL);
    d.Width = 1280; d.Height = 720; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 3; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    printf("CreateSwapChainForHwnd: 0x%08lx\n", (unsigned long)IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown *)q, wnd, &d, NULL, NULL, &sc));
    ip.pApplicationSwapChain = (IDXGISwapChain *)sc; ip.maxInterpolatedFrames = 1;
    rc = ((int (*)(void *, ID3D12CommandQueue *, const xefg_init_params *))GetProcAddress(fg, "xefgSwapChainD3D12InitFromSwapChain"))(fgh, q, &ip);
    printf("xefgSwapChainD3D12InitFromSwapChain: %d\n", rc);
    if (rc < 0) return 1;
    rc = ((int (*)(void *, REFIID, void **))GetProcAddress(fg, "xefgSwapChainD3D12GetSwapChainPtr"))(fgh, &IID_IDXGISwapChain3, (void **)&proxy);
    printf("xefgSwapChainD3D12GetSwapChainPtr: %d\n", rc);
    if (rc < 0 || !proxy) return 1;
    /* XeSS-FG took ownership of sc and destroyed it (per Intel docs); do not touch it. */

    depth = tex(1280, 720, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    mv = tex(1280, 720, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    hudless = tex(1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    for (pass = (getenv("FG_ONLY_ON") ? 1 : 0); pass < 2; pass++)
    {
        rc = FN(fg, "xefgSwapChainSetEnabled", PFN_pu)(fgh, pass);
        printf("\n== FG %s (clear colour %.2f %.2f %.2f; expect present-sample BGRA bytes %u %u %u)\n", pass ? "ON" : "OFF",
                colors[pass][0], colors[pass][1], colors[pass][2],
                (unsigned)(colors[pass][2] * 255 + 0.5f), (unsigned)(colors[pass][1] * 255 + 0.5f), (unsigned)(colors[pass][0] * 255 + 0.5f));
        printf("xefgSwapChainSetEnabled(%u): %d\n", pass, rc);
        for (i = 0; i < 180; i++)
        {
            uint32_t frame = pass * 1000 + i + 1;
            ID3D12Resource *bb = NULL;
            D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv);
            xefg_res_data rd;
            xefg_consts c = { 0, 0, 1280.0f, 720.0f, i == 0, 16.6f };
            HRESULT hr;

            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_SIMULATION_START);
            ((int (*)(void *, uint32_t))GetProcAddress(xl, "xellSleep"))(xlh, frame);
            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_SIMULATION_END);
            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_RENDERSUBMIT_START);
            IDXGISwapChain3_GetBuffer(proxy, IDXGISwapChain3_GetCurrentBackBufferIndex(proxy), &IID_ID3D12Resource, (void **)&bb);
            ID3D12CommandAllocator_Reset(alloc);
            ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
            ID3D12Device_CreateRenderTargetView(dev, hudless, NULL, h);
            ID3D12GraphicsCommandList_ClearRenderTargetView(list, h, colors[pass], 0, NULL);
            barrier(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            ID3D12Device_CreateRenderTargetView(dev, bb, NULL, h);
            ID3D12GraphicsCommandList_ClearRenderTargetView(list, h, colors[pass], 0, NULL);
            barrier(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            FN(fg, "xefgSwapChainSetPresentId", PFN_pu)(fgh, frame);
            ((int (*)(void *, uint32_t, const xefg_consts *))GetProcAddress(fg, "xefgSwapChainTagFrameConstants"))(fgh, frame, &c);
            memset(&rd, 0, sizeof(rd)); rd.validity = RV_UNTIL_NEXT_PRESENT; rd.sizeX = 1280; rd.sizeY = 720;
            rd.pResource = hudless; rd.type = RES_HUDLESS; rd.incomingState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            ((int (*)(void *, ID3D12CommandList *, uint32_t, const xefg_res_data *))GetProcAddress(fg, "xefgSwapChainD3D12TagFrameResource"))(fgh, (ID3D12CommandList *)list, frame, &rd);
            rd.pResource = depth; rd.type = RES_DEPTH; rd.incomingState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            ((int (*)(void *, ID3D12CommandList *, uint32_t, const xefg_res_data *))GetProcAddress(fg, "xefgSwapChainD3D12TagFrameResource"))(fgh, (ID3D12CommandList *)list, frame, &rd);
            rd.pResource = mv; rd.type = RES_MV;
            ((int (*)(void *, ID3D12CommandList *, uint32_t, const xefg_res_data *))GetProcAddress(fg, "xefgSwapChainD3D12TagFrameResource"))(fgh, (ID3D12CommandList *)list, frame, &rd);
            ID3D12GraphicsCommandList_Close(list);
            ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&list);
            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_RENDERSUBMIT_END);
            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_PRESENT_START);
            hr = IDXGISwapChain3_Present(proxy, 1, 0);
            ((int (*)(void *, uint32_t, int))GetProcAddress(xl, "xellAddMarkerData"))(xlh, frame, XELL_PRESENT_END);
            if (FAILED(hr) || (i % 60) == 0)
                printf("  frame %u: Present 0x%08lx, last present status %d\n", i, (unsigned long)hr,
                        FN(fg, "xefgSwapChainGetLastPresentStatus", PFN_p)(fgh));
            ID3D12CommandQueue_Signal(q, fence, ++fv);
            ID3D12Fence_SetEventOnCompletion(fence, fv, ev);
            if (WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) { printf("  frame %u: GPU TIMEOUT <<<\n", i); return 1; }
            ID3D12Resource_Release(bb);
        }
    }
    IDXGISwapChain3_Release(proxy);
    FN(fg, "xefgSwapChainDestroy", PFN_p)(fgh);
    FN(xl, "xellDestroyContext", PFN_p)(xlh);
    printf("done\n");
    return 0;
}
