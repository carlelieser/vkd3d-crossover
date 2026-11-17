/* Hang/fault probe: what does the stack do when the GPU work never completes or faults?
 * Usage: hangprobe.exe hang|fault. Reports whether a fence wait returns, the device-removed reason,
 * the fence's completed value (D3D12: UINT64_MAX after removal), and whether later submissions behave. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <string.h>

static void *load(const char *path, SIZE_T *size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, rd; void *p;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL); p = malloc(sz); ReadFile(h, p, sz, &rd, NULL); CloseHandle(h);
    *size = sz; return p;
}

static IDXGISwapChain3 *g_sc;
static DWORD WINAPI present_thread(void *arg)
{
    HRESULT *out = arg;
    *out = IDXGISwapChain3_Present(g_sc, 1, 0);
    return 0;
}

static void present_report(ID3D12Device *dev, const char *what)
{
    unsigned int i;
    for (i = 0; i < 3; i++)
    {
        HRESULT hr = 0xdeadbeef;
        DWORD t0 = GetTickCount();
        HANDLE t = CreateThread(NULL, 0, present_thread, &hr, 0, NULL);
        DWORD w = WaitForSingleObject(t, 10000);
        printf("%s: Present #%u %s after %.1f s, hr 0x%08lx\n", what, i, w == WAIT_OBJECT_0 ? "returned" : "HUNG (>10 s) <<<",
                (GetTickCount() - t0) / 1000.0, (unsigned long)hr);
        if (w != WAIT_OBJECT_0) return;
        CloseHandle(t);
    }
}

static void wait_report(ID3D12Device *dev, ID3D12Fence *f, UINT64 value, DWORD ms, const char *what)
{
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    DWORD t0 = GetTickCount(), w;
    HRESULT hr = ID3D12Fence_SetEventOnCompletion(f, value, ev);
    w = WaitForSingleObject(ev, ms);
    printf("%s: SetEventOnCompletion hr 0x%08lx, wait %s after %.1f s; GetDeviceRemovedReason 0x%08lx; fence completed value 0x%llx\n",
            what, (unsigned long)hr, w == WAIT_OBJECT_0 ? "RETURNED" : "TIMED OUT <<<", (GetTickCount() - t0) / 1000.0,
            (unsigned long)ID3D12Device_GetDeviceRemovedReason(dev), (unsigned long long)ID3D12Fence_GetCompletedValue(f));
    CloseHandle(ev);
}

int main(int argc, char **argv)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_ROOT_PARAMETER rp[2] = {0};
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {0};
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 64 << 20, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    ID3D12Device *dev; ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l;
    ID3D12RootSignature *rs; ID3D12PipelineState *pso; ID3D12Resource *buf; ID3D12Fence *f; ID3DBlob *blob = NULL, *err = NULL;
    const char *mode = argc > 1 ? argv[1] : "hang";
    int fault = !strcmp(mode, "fault");
    UINT consts[2] = { 0xffffffffu, 7 };
    D3D12_GPU_VIRTUAL_ADDRESS va;
    SIZE_T size; void *code;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!(code = load(fault ? "C:\\dxc\\fault.dxil" : "C:\\dxc\\hang.dxil", &size))) { printf("shader missing\n"); return 1; }
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, NULL, &IID_ID3D12GraphicsCommandList, (void **)&l);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&f);

    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants.Num32BitValues = 2;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rsd.NumParameters = 2; rsd.pParameters = rp;
    hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);
    pd.pRootSignature = rs; pd.CS.pShaderBytecode = code; pd.CS.BytecodeLength = size;
    hr = ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    printf("mode %s: pipeline hr 0x%08lx\n", mode, (unsigned long)hr);
    ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&buf);
    va = ID3D12Resource_GetGPUVirtualAddress(buf);
    if (fault) va += 1ull << 40;  /* 1 TiB past a real buffer: unmapped */

    {
        IDXGIFactory4 *fac; WNDCLASSA wc = {0}; HWND wnd; DXGI_SWAP_CHAIN_DESC1 d = {0}; IDXGISwapChain1 *s1 = NULL;
        CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&fac);
        wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "hang";
        RegisterClassA(&wc);
        wnd = CreateWindowExA(0, "hang", "hang", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 480, NULL, NULL, wc.hInstance, NULL);
        d.Width = 640; d.Height = 480; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)q, wnd, &d, NULL, NULL, &s1);
        IDXGISwapChain1_QueryInterface(s1, &IID_IDXGISwapChain3, (void **)&g_sc);
        present_report(dev, "before bad work");
    }
    ID3D12GraphicsCommandList_SetComputeRootSignature(l, rs);
    ID3D12GraphicsCommandList_SetPipelineState(l, pso);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(l, 0, 2, consts, 0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(l, 1, va);
    ID3D12GraphicsCommandList_Dispatch(l, fault ? 256 : 1024, 1, 1);
    ID3D12GraphicsCommandList_Close(l);
    ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
    ID3D12CommandQueue_Signal(q, f, 1);
    printf("submitted bad work + Signal(1)\n");
    wait_report(dev, f, 1, 60000, "wait for the bad work");

    /* Later, innocent work: must not hang either. */
    ID3D12CommandAllocator_Reset(a);
    ID3D12GraphicsCommandList_Reset(l, a, NULL);
    ID3D12GraphicsCommandList_Close(l);
    ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
    hr = ID3D12CommandQueue_Signal(q, f, 2);
    printf("follow-up empty submit + Signal(2): hr 0x%08lx\n", (unsigned long)hr);
    wait_report(dev, f, 2, 20000, "wait for follow-up work");
    present_report(dev, "after device loss");
    {
        /* Does the GPU still compute correct results? upload -> default -> readback copy of 1 MiB. */
        D3D12_HEAP_PROPERTIES up = { D3D12_HEAP_TYPE_UPLOAD }, rb = { D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC d1 = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 1 << 20, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0 };
        ID3D12Resource *u = NULL, *r = NULL, *g = NULL; unsigned int *p, i, bad = 0;
        ID3D12Device_CreateCommittedResource(dev, &up, D3D12_HEAP_FLAG_NONE, &d1, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&u);
        ID3D12Device_CreateCommittedResource(dev, &rb, D3D12_HEAP_FLAG_NONE, &d1, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&r);
        ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &d1, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void **)&g);
        if (u && r && g)
        {
            ID3D12Resource_Map(u, 0, NULL, (void **)&p); for (i = 0; i < (1 << 18); i++) p[i] = i * 2654435761u; ID3D12Resource_Unmap(u, 0, NULL);
            ID3D12CommandAllocator_Reset(a); ID3D12GraphicsCommandList_Reset(l, a, NULL);
            ID3D12GraphicsCommandList_CopyBufferRegion(l, g, 0, u, 0, 1 << 20);
            ID3D12GraphicsCommandList_CopyBufferRegion(l, r, 0, g, 0, 1 << 20);
            ID3D12GraphicsCommandList_Close(l);
            ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
            ID3D12CommandQueue_Signal(q, f, 3);
            wait_report(dev, f, 3, 20000, "GPU copy after the error");
            ID3D12Resource_Map(r, 0, NULL, (void **)&p); for (i = 0; i < (1 << 18); i++) bad += p[i] != i * 2654435761u; ID3D12Resource_Unmap(r, 0, NULL);
            printf("GPU copy after the error: %u of %u words wrong%s\n", bad, 1 << 18, bad ? " <<<" : " (GPU fully working)");
        }
        else printf("resource creation after the error failed (device removed)\n");
    }
    printf("done\n");
    return 0;
}
