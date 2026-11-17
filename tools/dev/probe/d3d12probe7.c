/* Probe #7: output path for SDR, scRGB (FP16) and HDR10 swapchains.
 * For each case: report output colour space and luminance, CheckColorSpaceSupport, SetColorSpace1, then clear
 * the back buffer to a known colour and present for ~2 s. MoltenVK's present probe
 * (MVK_PRESENT_PROBE_LOG=1) logs the presented pixel, so the log shows exactly what reached CAMetalLayer.
 * Expected raw values are printed here for comparison. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>

static IDXGIFactory4 *f;
static ID3D12CommandQueue *q;
static ID3D12Device *dev;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static ID3D12DescriptorHeap *rtv_heap;
static ID3D12Fence *fence;
static UINT64 fence_value;
static int fails;

static void report(const char *what, HRESULT hr)
{
    if (FAILED(hr)) fails++;
    printf("%-60s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL <<<");
    fflush(stdout);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

static void wait_idle(void)
{
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12CommandQueue_Signal(q, fence, ++fence_value);
    ID3D12Fence_SetEventOnCompletion(fence, fence_value, ev);
    WaitForSingleObject(ev, 5000);
    CloseHandle(ev);
}

static const char *cs_name(DXGI_COLOR_SPACE_TYPE cs)
{
    switch (cs)
    {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return "sRGB (G22 P709)";
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: return "scRGB (G10 P709)";
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return "HDR10 (G2084 P2020)";
        default: return "other";
    }
}

static void print_output(IDXGISwapChain3 *sc)
{
    IDXGIOutput *out = NULL;
    IDXGIOutput6 *out6 = NULL;
    DXGI_OUTPUT_DESC1 d;
    if (FAILED(IDXGISwapChain3_GetContainingOutput(sc, &out)))
    {
        printf("  GetContainingOutput failed\n");
        return;
    }
    if (SUCCEEDED(IDXGIOutput_QueryInterface(out, &IID_IDXGIOutput6, (void **)&out6)) &&
            SUCCEEDED(IDXGIOutput6_GetDesc1(out6, &d)))
        printf("  output: colour space %s, %u bpc, luminance min %.4f max %.1f full-frame %.1f\n",
                cs_name(d.ColorSpace), d.BitsPerColor, d.MinLuminance, d.MaxLuminance, d.MaxFullFrameLuminance);
    if (out6) IDXGIOutput6_Release(out6);
    IDXGIOutput_Release(out);
}

struct test
{
    const char *name;
    DXGI_FORMAT format;
    DXGI_COLOR_SPACE_TYPE cs;
    float color[4];
    const char *expected;
};

static void run(HWND wnd, const struct test *t)
{
    DXGI_SWAP_CHAIN_DESC1 d = {0};
    IDXGISwapChain1 *s1 = NULL;
    IDXGISwapChain3 *sc = NULL;
    UINT support = 0, i;
    char buf[128];
    HRESULT hr;

    printf("\n== %s\n", t->name);
    d.Width = 1280; d.Height = 720; d.Format = t->format; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_STRETCH;
    hr = IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown *)q, wnd, &d, NULL, NULL, &s1);
    report("  CreateSwapChainForHwnd", hr);
    if (FAILED(hr)) return;
    IDXGISwapChain1_QueryInterface(s1, &IID_IDXGISwapChain3, (void **)&sc);
    IDXGISwapChain1_Release(s1);
    print_output(sc);

    hr = IDXGISwapChain3_CheckColorSpaceSupport(sc, t->cs, &support);
    printf("  CheckColorSpaceSupport(%s): hr 0x%08lx, present %s\n", cs_name(t->cs), (unsigned long)hr,
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ? "yes" : "no");
    hr = IDXGISwapChain3_SetColorSpace1(sc, t->cs);
    snprintf(buf, sizeof(buf), "  SetColorSpace1(%s)", cs_name(t->cs));
    printf("%-60s hr=0x%08lx %s\n", buf, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "rejected");
    printf("  clear (%.3f, %.3f, %.3f, %.3f), expect presented %s\n", t->color[0], t->color[1], t->color[2], t->color[3],
            t->expected);
    fflush(stdout);

    for (i = 0; i < 120; i++)
    {
        ID3D12Resource *bb = NULL;
        D3D12_RESOURCE_BARRIER b = {0};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);
        UINT idx = IDXGISwapChain3_GetCurrentBackBufferIndex(sc);

        IDXGISwapChain3_GetBuffer(sc, idx, &IID_ID3D12Resource, (void **)&bb);
        ID3D12CommandAllocator_Reset(alloc);
        ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
        ID3D12Device_CreateRenderTargetView(dev, bb, NULL, rtv);
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = bb;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, t->color, 0, NULL);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
        ID3D12GraphicsCommandList_Close(list);
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&list);
        hr = IDXGISwapChain3_Present(sc, 1, 0);
        if (FAILED(hr) && i == 0) report("  Present", hr);
        wait_idle();
        ID3D12Resource_Release(bb);
        pump();
    }
    printf("  presented 120 frames (last hr 0x%08lx)\n", (unsigned long)hr);
    fflush(stdout);
    wait_idle();
    IDXGISwapChain3_Release(sc);
}

int main(int argc, char **argv)
{
    static const struct test tests[] =
    {
        { "SDR RGBA8 sRGB", DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
          { 0.5f, 0.25f, 0.75f, 1.0f }, "8-bit 128 64 191 255 (RGBA or BGRA order)" },
        { "SDR RGB10A2 sRGB", DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
          { 0.5f, 0.25f, 0.75f, 1.0f }, "10-bit r=512 g=256 b=767 a=3" },
        { "scRGB RGBA16F (values > 1.0)", DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
          { 2.0f, 0.5f, 0.25f, 1.0f }, "RGBA16F 4000 3800 3400 3c00" },
        { "HDR10 RGB10A2 PQ", DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,
          { 0.5f, 0.25f, 0.75f, 1.0f }, "10-bit r=512 g=256 b=767 a=3 (PQ-encoded, needs HDR10 layer)" },
        { "RGBA16F asked for sRGB (DXGI forbids; expect rejection)", DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, { 0.5f, 0.5f, 0.5f, 1.0f }, "RGBA16F 3800 3800 3800 3c00 (stays scRGB)" },
    };
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    WNDCLASSA wc = {0};
    HWND wnd;
    unsigned int i;

    CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&f);
    report("D3D12CreateDevice", D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev));
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&rtv_heap);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&fence);

    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe7";
    RegisterClassA(&wc);
    wnd = CreateWindowExA(0, "probe7", "probe7", WS_OVERLAPPEDWINDOW, 50, 50, 1280, 720, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(wnd, SW_SHOW);
    pump();

    for (i = 0; i < ARRAYSIZE(tests); i++)
        if (argc < 2 || (unsigned int)atoi(argv[1]) == i)
            run(wnd, &tests[i]);

    printf("\ndone, %d failures\n", fails);
    return 0;
}
