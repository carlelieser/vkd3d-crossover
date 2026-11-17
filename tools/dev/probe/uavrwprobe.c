/* Typed UAV read/write probe per format: texture pixel 0 = (0.5,0.25,0.75,1) and pixel 1 = 0 uploaded; CS loads pixel 0
 * (written to a buffer), stores pixel0 + (0.25,0,0,0) and pixel 1 = (0.5,0.25,0.75,1). Reads everything back. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static ID3D12Device *dev; static ID3D12CommandQueue *q; static ID3D12CommandAllocator *a; static ID3D12GraphicsCommandList *l;
static ID3D12Fence *fe; static UINT64 fv; static HANDLE ev;
static void submit(void) { ID3D12GraphicsCommandList_Close(l); ID3D12CommandQueue_ExecuteCommandLists(q, 1, (ID3D12CommandList **)&l);
    ID3D12CommandQueue_Signal(q, fe, ++fv); ID3D12Fence_SetEventOnCompletion(fe, fv, ev); WaitForSingleObject(ev, 5000);
    ID3D12CommandAllocator_Reset(a); ID3D12GraphicsCommandList_Reset(l, a, NULL); }
static ID3D12Resource *buffer(D3D12_HEAP_TYPE t, UINT64 size, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st)
{ D3D12_HEAP_PROPERTIES hp = { t }; D3D12_RESOURCE_DESC d = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, fl };
  ID3D12Resource *r; ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &d, st, NULL, &IID_ID3D12Resource, (void **)&r); return r; }

struct fmt { const char *name; DXGI_FORMAT f; UINT bpp; UINT32 px0[2]; };
int main(void)
{
    static const struct fmt fmts[] = {
        { "R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM, 4, { 512u | (256u << 10) | (767u << 20) | (3u << 30) } },
        { "R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM, 4, { 128u | (64u << 8) | (191u << 16) | (255u << 24) } },
        { "R11G11B10_FLOAT", DXGI_FORMAT_R11G11B10_FLOAT, 4, { 0x3c0u | (0x380u << 11) | (0x1e0u << 22) } },
        { "R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT, 8, { 0x34003800u, 0x3c003a00u } },
    };
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER rp[2] = {0}; D3D12_ROOT_SIGNATURE_DESC rsd = {0}; D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {0};
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
    D3D12_FEATURE_DATA_D3D12_OPTIONS opts = {0};
    ID3D12RootSignature *rs; ID3D12PipelineState *pso; ID3D12DescriptorHeap *heap; ID3DBlob *blob, *err;
    HANDLE h = CreateFileA("C:\\dxc\\uavrw.dxil", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz = GetFileSize(h, NULL), rd; void *code = malloc(sz); unsigned int i; int fails = 0;
    ReadFile(h, code, sz, &rd, NULL); CloseHandle(h);
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts));
    printf("TypedUAVLoadAdditionalFormats: %d\n", opts.TypedUAVLoadAdditionalFormats);
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, NULL, &IID_ID3D12GraphicsCommandList, (void **)&l);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&fe);
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp[1].Descriptor.ShaderRegister = 1;
    rsd.NumParameters = 2; rsd.pParameters = rp;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);
    pd.pRootSignature = rs; pd.CS.pShaderBytecode = code; pd.CS.BytecodeLength = sz;
    printf("PSO: 0x%08lx\n", ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso));
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);

    for (i = 0; i < ARRAYSIZE(fmts); i++)
    {
        const struct fmt *ft = &fmts[i];
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { ft->f };
        D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC td = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 2, 1, 1, 1, ft->f, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        ID3D12Resource *tex, *up = buffer(D3D12_HEAP_TYPE_UPLOAD, 512, 0, D3D12_RESOURCE_STATE_GENERIC_READ);
        ID3D12Resource *out = buffer(D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12Resource *rb = buffer(D3D12_HEAP_TYPE_READBACK, 512, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER b = {0}; unsigned char *p; float *lf; UINT32 *tx;

        ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
        ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&tex);
        ID3D12Resource_Map(up, 0, NULL, (void **)&p); memset(p, 0, 512); memcpy(p, ft->px0, ft->bpp); ID3D12Resource_Unmap(up, 0, NULL);
        dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = ft->f; src.PlacedFootprint.Footprint.Width = 2; src.PlacedFootprint.Footprint.Height = 1;
        src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = 256;
        ID3D12GraphicsCommandList_CopyTextureRegion(l, &dst, 0, 0, 0, &src, NULL);
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = tex; b.Transition.Subresource = ~0u;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        ID3D12Device_CreateUnorderedAccessView(dev, tex, NULL, NULL, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetDescriptorHeaps(l, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(l, rs); ID3D12GraphicsCommandList_SetPipelineState(l, pso);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(l, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(l, 1, ID3D12Resource_GetGPUVirtualAddress(out));
        ID3D12GraphicsCommandList_Dispatch(l, 1, 1, 1);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        b.Transition.pResource = out; ID3D12GraphicsCommandList_ResourceBarrier(l, 1, &b);
        dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = src.PlacedFootprint;
        src.pResource = tex; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
        ID3D12GraphicsCommandList_CopyTextureRegion(l, &dst, 0, 0, 0, &src, NULL);
        ID3D12GraphicsCommandList_CopyBufferRegion(l, rb, 256, out, 0, 16);
        submit();
        ID3D12Resource_Map(rb, 0, NULL, (void **)&p);
        lf = (float *)(p + 256); tx = (UINT32 *)p;
        printf("%-20s typed UAV load %s store %s | loaded (%.3f %.3f %.3f %.3f) | px0 %08x%s px1 %08x%s\n", ft->name,
                (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) ? "yes" : "no", (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) ? "yes" : "no",
                lf[0], lf[1], lf[2], lf[3], tx[0], ft->bpp == 8 ? "" : "", ft->bpp == 8 ? tx[2] : tx[1], "");
        if (fabsf(lf[0] - 0.5f) > 0.01f || fabsf(lf[3] - 1.0f) > 0.01f) { printf("  ^ LOAD WRONG\n"); fails++; }
        ID3D12Resource_Unmap(rb, 0, NULL);
        ID3D12Resource_Release(tex); ID3D12Resource_Release(up); ID3D12Resource_Release(out); ID3D12Resource_Release(rb);
    }
    printf("done, %d failures\n", fails);
    return 0;
}
