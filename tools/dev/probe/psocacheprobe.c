/* Cross-process PSO cache probe. Run "save" then "load": "save" creates graphics PSOs (plain desc and pipeline-stream
 * variants) and writes their GetCachedBlob data to C:\vkd3d\pso-*.bin; "load" recreates identical PSOs with those
 * blobs and reports whether vkd3d accepts them (S_OK) or rejects them (E_INVALIDARG = description hash mismatch). */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { DXGI_FORMAT RTFormats[8]; UINT NumRenderTargets; } RT_FORMAT_ARRAY_;

static void *load(const char *path, SIZE_T *sz)
{ HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); DWORD rd, n; void *c;
  if (h == INVALID_HANDLE_VALUE) { *sz = 0; return NULL; } n = GetFileSize(h, NULL); c = malloc(n); ReadFile(h, c, n, &rd, NULL); CloseHandle(h); *sz = n; return c; }
static void save(const char *path, const void *p, SIZE_T n)
{ HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD wr; WriteFile(h, p, (DWORD)n, &wr, NULL); CloseHandle(h); }

struct stream { struct { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; ID3D12RootSignature *v; } __attribute__((aligned(8))) rs;
    struct { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; D3D12_SHADER_BYTECODE v; } __attribute__((aligned(8))) vs, ps;
    struct { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; RT_FORMAT_ARRAY_ v; } __attribute__((aligned(8))) rtv;
    struct { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; D3D12_PRIMITIVE_TOPOLOGY_TYPE v; } __attribute__((aligned(8))) topo;
    struct { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; D3D12_CACHED_PIPELINE_STATE v; } __attribute__((aligned(8))) cached; };

int main(int argc, char **argv)
{
    int saving = argc > 1 && !strcmp(argv[1], "save");
    D3D12_INPUT_ELEMENT_DESC il[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                                       { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
    D3D12_ROOT_SIGNATURE_DESC rsd = {0}; D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0}; D3D12_PIPELINE_STATE_STREAM_DESC sd;
    ID3D12Device *dev; ID3D12Device2 *dev2; ID3D12RootSignature *rs; ID3D12PipelineState *pso; ID3DBlob *blob, *err;
    SIZE_T vsz, psz, bsz; void *vsc = load("C:\\dxc\\bb_vs.dxil", &vsz), *psc = load("C:\\dxc\\bb_ps.dxil", &psz), *b;
    struct stream st; HRESULT hr; int v, fails = 0;
    static const char *names[] = { "desc: basic", "desc: input layout + DSV UNKNOWN + depth off", "desc: blend + raster + D32 depth", "stream: minimal" };

    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    ID3D12Device_QueryInterface(dev, &IID_ID3D12Device2, (void **)&dev2);
    {
        static D3D12_DESCRIPTOR_RANGE range = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        static D3D12_ROOT_PARAMETER rp[2];
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable.NumDescriptorRanges = 1; rp[0].DescriptorTable.pDescriptorRanges = &range;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[1].Constants.Num32BitValues = 4;
        rsd.NumParameters = 2; rsd.pParameters = rp;
    }
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);

    for (v = 0; v < 4; v++)
    {
        char path[64]; snprintf(path, sizeof(path), "C:\\vkd3d\\pso-%d.bin", v);
        b = saving ? NULL : load(path, &bsz);
        pso = NULL;
        if (v < 3)
        {
            memset(&pd, 0, sizeof(pd));
            pd.pRootSignature = rs; pd.VS.pShaderBytecode = vsc; pd.VS.BytecodeLength = vsz; pd.PS.pShaderBytecode = psc; pd.PS.BytecodeLength = psz;
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf; pd.SampleMask = ~0u;
            pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM; pd.SampleDesc.Count = 1;
            if (v == 1) { pd.InputLayout.pInputElementDescs = il; pd.InputLayout.NumElements = 2; pd.DSVFormat = DXGI_FORMAT_UNKNOWN; }
            if (v == 2) { pd.BlendState.RenderTarget[0].BlendEnable = TRUE; pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE; pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; pd.RasterizerState.DepthBias = 4; pd.RasterizerState.SlopeScaledDepthBias = 1.5f;
                pd.DepthStencilState.DepthEnable = TRUE; pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                pd.DSVFormat = DXGI_FORMAT_D32_FLOAT; }
            if (b) { pd.CachedPSO.pCachedBlob = b; pd.CachedPSO.CachedBlobSizeInBytes = bsz; }
            hr = ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
        }
        else
        {
            memset(&st, 0, sizeof(st));
            st.rs.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE; st.rs.v = rs;
            st.vs.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS; st.vs.v.pShaderBytecode = vsc; st.vs.v.BytecodeLength = vsz;
            st.ps.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS; st.ps.v.pShaderBytecode = psc; st.ps.v.BytecodeLength = psz;
            st.rtv.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS; st.rtv.v.NumRenderTargets = 1; st.rtv.v.RTFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
            st.topo.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY; st.topo.v = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            st.cached.t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO; if (b) { st.cached.v.pCachedBlob = b; st.cached.v.CachedBlobSizeInBytes = bsz; }
            sd.SizeInBytes = sizeof(st); sd.pPipelineStateSubobjectStream = &st;
            hr = ID3D12Device2_CreatePipelineState(dev2, &sd, &IID_ID3D12PipelineState, (void **)&pso);
        }
        if (saving)
        {
            ID3DBlob *cb = NULL;
            if (pso && SUCCEEDED(ID3D12PipelineState_GetCachedBlob(pso, &cb))) save(path, ID3D10Blob_GetBufferPointer(cb), ID3D10Blob_GetBufferSize(cb));
            printf("save %-45s create 0x%08lx, blob %lu bytes\n", names[v], hr, cb ? (unsigned long)ID3D10Blob_GetBufferSize(cb) : 0ul);
        }
        else
        {
            printf("load %-45s with saved blob: 0x%08lx %s\n", names[v], hr, hr == S_OK ? "ACCEPTED" : hr == E_INVALIDARG ? "REJECTED (desc hash mismatch)" : "other");
            if (hr != S_OK) fails++;
        }
        if (pso) ID3D12PipelineState_Release(pso);
    }
    printf("done, %d rejected\n", fails);
    return 0;
}
