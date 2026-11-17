/* Deferred-PSO stress: 8 threads x 300 graphics PSOs (blend/write-mask/format variants of the bb shaders), half
 * released immediately (never used), the rest each used for one draw into an offscreen target; the clear colour from
 * root constants must appear. Reports wrong pixels and time. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *load(const char *path, SIZE_T *sz)
{ HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); DWORD rd, n = GetFileSize(h, NULL); void *c = malloc(n); ReadFile(h, c, n, &rd, NULL); CloseHandle(h); *sz = n; return c; }

static ID3D12Device *dev; static ID3D12RootSignature *rs; static void *vsc, *psc; static SIZE_T vsz, psz;
#define PER_THREAD 300
static ID3D12PipelineState *kept[8][PER_THREAD];

static DWORD WINAPI worker(void *arg)
{
    int t = (int)(INT_PTR)arg, i;
    for (i = 0; i < PER_THREAD; i++)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0}; ID3D12PipelineState *p = NULL;
        pd.pRootSignature = rs; pd.VS.pShaderBytecode = vsc; pd.VS.BytecodeLength = vsz; pd.PS.pShaderBytecode = psc; pd.PS.BytecodeLength = psz;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf; pd.SampleMask = ~0u;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
        pd.RasterizerState.DepthBias = t * PER_THREAD + i;     /* makes every PSO distinct without changing output */
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pd.SampleDesc.Count = 1;
        if (FAILED(ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&p))) { printf("create failed\n"); continue; }
        if (i & 1) { ID3D12PipelineState_Release(p); kept[t][i] = NULL; } else kept[t][i] = p;
    }
    return 0;
}

int main(void)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER rp[2] = {0}; D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT }, rbp = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
    D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256 * 64, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0 };
    D3D12_DESCRIPTOR_HEAP_DESC rhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 }, shd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l; ID3D12Fence *fe; ID3D12Resource *rt, *rb; ID3D12DescriptorHeap *rh, *sh;
    ID3DBlob *blob, *err; HANDLE th[8], ev = CreateEventA(NULL, FALSE, FALSE, NULL); UINT64 fv = 0; DWORD t0; int t, i, used = 0, wrong = 0;
    float k[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    D3D12_VIEWPORT vp = { 0, 0, 64, 64, 0, 1 }; D3D12_RECT sr = { 0, 0, 64, 64 };

    vsc = load("C:\\dxc\\bb_vs.dxil", &vsz); psc = load("C:\\dxc\\bb_ps.dxil", &psz);
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants.Num32BitValues = 4;
    rsd.NumParameters = 2; rsd.pParameters = rp;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);
    t0 = GetTickCount();
    for (t = 0; t < 8; t++) th[t] = CreateThread(NULL, 0, worker, (void *)(INT_PTR)t, 0, NULL);
    WaitForMultipleObjects(8, th, TRUE, INFINITE);
    printf("created 2400 PSOs on 8 threads in %lu ms\n", GetTickCount() - t0); fflush(stdout);

    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, NULL, &IID_ID3D12GraphicsCommandList, (void **)&l);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&fe);
    ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&rt);
    ID3D12Device_CreateCommittedResource(dev, &rbp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&rb);
    ID3D12Device_CreateDescriptorHeap(dev, &rhd, &IID_ID3D12DescriptorHeap, (void **)&rh);
    ID3D12Device_CreateDescriptorHeap(dev, &shd, &IID_ID3D12DescriptorHeap, (void **)&sh);
    ID3D12Device_CreateRenderTargetView(dev, rt, NULL, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rh));
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(dev, NULL, &sv, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sh));

    t0 = GetTickCount();
    for (t = 0; t < 8; t++) for (i = 0; i < PER_THREAD; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rh);
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0}; D3D12_RESOURCE_BARRIER b = {0}; float clear[4] = {0, 0, 0, 0}; UINT32 *px;
        if (!kept[t][i]) continue;
        used++;
        ID3D12CommandAllocator_Reset(a); ID3D12GraphicsCommandList_Reset(l, a, kept[t][i]);
        ID3D12GraphicsCommandList_ClearRenderTargetView(l, rtv, clear, 0, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(l, 1, &sh);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(l, rs);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(l, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sh));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(l, 1, 4, k, 0);
        ID3D12GraphicsCommandList_OMSetRenderTargets(l, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(l, 1, &vp); ID3D12GraphicsCommandList_RSSetScissorRects(l, 1, &sr);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(l, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(l, 3, 1, 0, 0);
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = rt; b.Transition.Subresource = ~0u;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = 64; dst.PlacedFootprint.Footprint.Height = 64; dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = 256;
        src.pResource = rt; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(l, &dst, 0, 0, 0, &src, NULL);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12GraphicsCommandList_Close(l);
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
        ID3D12CommandQueue_Signal(q, fe, ++fv); ID3D12Fence_SetEventOnCompletion(fe, fv, ev); WaitForSingleObject(ev, 5000);
        ID3D12Resource_Map(rb, 0, NULL, (void **)&px);
        if (px[32 * 64 + 32] != 0xffbf8040u) { if (wrong < 5) printf("PSO %d/%d: pixel %08x\n", t, i, px[32 * 64 + 32]); wrong++; }
        ID3D12Resource_Unmap(rb, 0, NULL);
        ID3D12PipelineState_Release(kept[t][i]);
    }
    printf("drew with %d PSOs in %lu ms, %d wrong\ndone\n", used, GetTickCount() - t0, wrong);
    return 0;
}
