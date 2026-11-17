/* Fence stall probe: leaves a CPU wait (SetEventOnCompletion) and a queue Wait unsatisfied and polls a stuck fence,
 * while presenting for ~25 s. gfx-failures.log should show fence-stall lines for all three. Then it signals the
 * fence so the queue drains and the process exits cleanly. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

int main(void)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    DXGI_SWAP_CHAIN_DESC1 d = {0};
    IDXGIFactory4 *f; ID3D12Device *dev; ID3D12CommandQueue *q, *q2; ID3D12Fence *cpu_fence, *gpu_fence, *poll_fence;
    IDXGISwapChain1 *sc; WNDCLASSA wc = {0}; HWND wnd; HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD start; UINT64 sum = 0; MSG m;

    CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&f);
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q2);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&cpu_fence);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&gpu_fence);
    ID3D12Device_CreateFence(dev, 3, 0, &IID_ID3D12Fence, (void **)&poll_fence);
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "fs";
    RegisterClassA(&wc);
    wnd = CreateWindowExA(0, "fs", "fs", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 360, NULL, NULL, wc.hInstance, NULL);
    d.Width = 640; d.Height = 360; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown *)q, wnd, &d, NULL, NULL, &sc);

    ID3D12Fence_SetEventOnCompletion(cpu_fence, 10, ev);       /* expect: CPU wait for 10, no signal ever submitted */
    ID3D12CommandQueue_Wait(q2, gpu_fence, 20);                 /* expect: queue Wait for 20, no signal ever submitted */
    printf("waits registered; presenting and polling for 25 s\n"); fflush(stdout);
    start = GetTickCount();
    while (GetTickCount() - start < 25000)
    {
        int i;
        for (i = 0; i < 50; i++) sum += ID3D12Fence_GetCompletedValue(poll_fence); /* expect: polled, stuck at 3 */
        IDXGISwapChain1_Present(sc, 1, 0);
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    }
    ID3D12Fence_Signal(gpu_fence, 20);
    ID3D12Fence_Signal(cpu_fence, 10);
    printf("event signalled: %s\n", WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0 ? "yes" : "no");
    printf("done (%llu)\n", (unsigned long long)sum);
    return 0;
}
