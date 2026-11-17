/* NaN/Inf semantics probe: runs C:\dxc\nan.dxil and compares each result with D3D12's required behaviour. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const struct { const char *what; unsigned int expect; int is_float_nan; } checks[] = {
        { "isnan(NaN)", 1 }, { "isnan(1.5)", 0 }, { "isnan(NaN) ? 0 : NaN", 0 }, { "max(NaN, 0)", 0 },
        { "min(NaN, 1)", 0x3f800000 }, { "saturate(NaN)", 0 }, { "isfinite(+Inf)", 0 }, { "isinf(+Inf)", 1 },
        { "NaN == NaN", 0 }, { "NaN != NaN", 1 }, { "clamp(NaN, 0, 1)", 0 }, { "1.5 / 0", 0x7f800000 },
        { "max(1.5/0, 0) > 1e30", 0x3f800000 }, { "NaN * 0 (must stay NaN)", 0, 1 }, { "lerp(1, 2, saturate(NaN))", 0x3f800000 },
    };
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_ROOT_PARAMETER rp = {0}; D3D12_ROOT_SIGNATURE_DESC rsd = {0}; D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {0};
    D3D12_HEAP_PROPERTIES def = { D3D12_HEAP_TYPE_DEFAULT }, up = { D3D12_HEAP_TYPE_UPLOAD }, rbh = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0 };
    ID3D12Device *dev; ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12RootSignature *rs;
    ID3D12PipelineState *pso; ID3D12Resource *buf, *ub, *rb; ID3D12Fence *f; ID3DBlob *blob, *err;
    unsigned int *p, i, fails = 0; HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateFileA("C:\\dxc\\nan.dxil", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz = GetFileSize(h, NULL), rd; void *code = malloc(sz); ReadFile(h, code, sz, &rd, NULL); CloseHandle(h);

    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, NULL, &IID_ID3D12GraphicsCommandList, (void **)&l);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&f);
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rsd.NumParameters = 1; rsd.pParameters = &rp;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);
    pd.pRootSignature = rs; pd.CS.pShaderBytecode = code; pd.CS.BytecodeLength = sz;
    ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Device_CreateCommittedResource(dev, &def, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&buf);
    bd.Flags = 0;
    ID3D12Device_CreateCommittedResource(dev, &up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&ub);
    ID3D12Device_CreateCommittedResource(dev, &rbh, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&rb);
    ID3D12Resource_Map(ub, 0, NULL, (void **)&p); memset(p, 0, 256);
    p[0] = 0x7fc00000; p[1] = 0x7f800000; p[2] = 0x3fc00000; p[3] = 0xc0000000; ID3D12Resource_Unmap(ub, 0, NULL);
    ID3D12GraphicsCommandList_CopyBufferRegion(l, buf, 0, ub, 0, 256);
    {
        D3D12_RESOURCE_BARRIER b = {0}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = buf;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12GraphicsCommandList_SetComputeRootSignature(l, rs); ID3D12GraphicsCommandList_SetPipelineState(l, pso);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(l, 0, ID3D12Resource_GetGPUVirtualAddress(buf));
        ID3D12GraphicsCommandList_Dispatch(l, 1, 1, 1);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
    }
    ID3D12GraphicsCommandList_CopyBufferRegion(l, rb, 0, buf, 0, 256);
    ID3D12GraphicsCommandList_Close(l); ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
    ID3D12CommandQueue_Signal(q, f, 1); ID3D12Fence_SetEventOnCompletion(f, 1, ev); WaitForSingleObject(ev, 10000);
    ID3D12Resource_Map(rb, 0, NULL, (void **)&p);
    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++)
    {
        unsigned int v = p[16 + i];
        int ok = checks[i].is_float_nan ? ((v & 0x7f800000) == 0x7f800000 && (v & 0x7fffff)) : v == checks[i].expect;
        if (!ok) fails++;
        printf("%-28s got 0x%08x  expect %s0x%08x  %s\n", checks[i].what, v, checks[i].is_float_nan ? "a NaN, e.g. " : "",
                checks[i].is_float_nan ? 0x7fc00000 : checks[i].expect, ok ? "OK" : "WRONG <<<");
    }
    printf("done, %u wrong\n", fails);
    return 0;
}
