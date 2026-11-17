/* StretchBlt probe (built from bbdrawprobe): runs C:\\dxc\\re9_final_ps.dxbc against an RGB10A2 texture filled with a known colour,
 * through a static bilinear-clamp sampler at s5 space32, into the backbuffer. Present-probe log shows the result.
 * Backbuffer draw probe: draws a fullscreen triangle into the swapchain backbuffer with one null SRV in its table and
 * a root-constant colour, presents for ~20 s. gfx-failures.log should show backbuffer-draw lines with the state. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
static void *load(const char *path, SIZE_T *sz)
{ HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); DWORD rd, n = GetFileSize(h, NULL); void *c = malloc(n); ReadFile(h, c, n, &rd, NULL); CloseHandle(h); *sz = n; return c; }
int main(void)
{
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_STATIC_SAMPLER_DESC ss = { D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 1, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0, D3D12_FLOAT32_MAX, 5, 32, D3D12_SHADER_VISIBILITY_PIXEL };
    D3D12_ROOT_PARAMETER rp[2] = {0}; D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0}; D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_DESCRIPTOR_HEAP_DESC rhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
    DXGI_SWAP_CHAIN_DESC1 d = {0}; IDXGIFactory4 *f; ID3D12Device *dev; ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l;
    ID3D12RootSignature *rs; ID3D12PipelineState *pso; ID3D12DescriptorHeap *heap, *rheap; ID3D12Fence *fe; IDXGISwapChain1 *s1; IDXGISwapChain3 *sc;
    ID3DBlob *blob, *err; WNDCLASSA wc = {0}; HWND wnd; SIZE_T vsz, psz; void *vsc = load("C:\\dxc\\blit_vs.dxil", &vsz), *psc = load("C:\\dxc\\re9_final_ps.dxbc", &psz);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL); UINT64 fv = 0; DWORD start; MSG m; HRESULT hr;
    float k[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    D3D12_VIEWPORT vp = { 0, 0, 640, 360, 0, 1 }; D3D12_RECT sr = { 0, 0, 640, 360 };

    CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&f);
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, NULL, &IID_ID3D12GraphicsCommandList, (void **)&l);
    ID3D12GraphicsCommandList_Close(l);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&fe);
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants.Num32BitValues = 4;
    rsd.NumParameters = 2; rsd.pParameters = rp; rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &ss;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);
    pd.pRootSignature = rs; pd.VS.pShaderBytecode = vsc; pd.VS.BytecodeLength = vsz; pd.PS.pShaderBytecode = psc; pd.PS.BytecodeLength = psz;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf; pd.SampleMask = ~0u;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM; pd.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    printf("PSO 0x%08lx\n", hr);
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ID3D12Device_CreateDescriptorHeap(dev, &rhd, &IID_ID3D12DescriptorHeap, (void **)&rheap);
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    {
        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT }, uhp = { D3D12_HEAP_TYPE_UPLOAD }; ID3D12Resource *tex, *up; UINT32 *px; unsigned int x;
        D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 64, 64, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0 };
        D3D12_RESOURCE_DESC bd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256 * 64, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0 };
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0}; D3D12_RESOURCE_BARRIER b = {0};
        ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&tex);
        ID3D12Device_CreateCommittedResource(dev, &uhp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&up);
        ID3D12Resource_Map(up, 0, NULL, (void **)&px);
        for (x = 0; x < 64 * 64; x++) px[x] = 512u | (256u << 10) | (767u << 20) | (3u << 30); /* (0.5, 0.25, 0.75, 1) */
        ID3D12Resource_Unmap(up, 0, NULL);
        dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        src.PlacedFootprint.Footprint.Width = 64; src.PlacedFootprint.Footprint.Height = 64; src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_Reset(l, a, NULL);
        ID3D12GraphicsCommandList_CopyTextureRegion(l, &dst, 0, 0, 0, &src, NULL);
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = tex; b.Transition.Subresource = ~0u;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12GraphicsCommandList_Close(l);
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
        ID3D12CommandQueue_Signal(q, fe, ++fv); ID3D12Fence_SetEventOnCompletion(fe, fv, ev); WaitForSingleObject(ev, 5000);
        ID3D12Device_CreateShaderResourceView(dev, tex, NULL, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));
    }
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "bb"; RegisterClassA(&wc);
    wnd = CreateWindowExA(0, "bb", "bb", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 360, NULL, NULL, wc.hInstance, NULL);
    d.Width = 640; d.Height = 360; d.Format = DXGI_FORMAT_R10G10B10A2_UNORM; d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown *)q, wnd, &d, NULL, NULL, &s1);
    IDXGISwapChain1_QueryInterface(s1, &IID_IDXGISwapChain3, (void **)&sc);
    start = GetTickCount();
    while (GetTickCount() - start < 34000)
    {
        ID3D12Resource *bb; D3D12_RESOURCE_BARRIER b = {0}; D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rheap);
        IDXGISwapChain3_GetBuffer(sc, IDXGISwapChain3_GetCurrentBackBufferIndex(sc), &IID_ID3D12Resource, (void **)&bb);
        ID3D12Device_CreateRenderTargetView(dev, bb, NULL, rtv);
        ID3D12CommandAllocator_Reset(a); ID3D12GraphicsCommandList_Reset(l, a, pso);
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = bb; b.Transition.Subresource = ~0u;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(l, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(l, rs);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(l, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(l, 1, 4, k, 0);
        ID3D12GraphicsCommandList_OMSetRenderTargets(l, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(l, 1, &vp); ID3D12GraphicsCommandList_RSSetScissorRects(l, 1, &sr);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(l, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(l, 3, 1, 0, 0);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12GraphicsCommandList_Close(l);
        ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
        IDXGISwapChain3_Present(sc, 1, 0);
        ID3D12CommandQueue_Signal(q, fe, ++fv); ID3D12Fence_SetEventOnCompletion(fe, fv, ev); WaitForSingleObject(ev, 5000);
        ID3D12Resource_Release(bb);
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    }
    printf("done\n");
    return 0;
}
