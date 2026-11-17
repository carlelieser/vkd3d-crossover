/* PSO retry probe: creates compute PSO A, takes its cached blob, then creates PSO B (different shader) with A's blob
 * (must fail E_INVALIDARG), then B without a blob (must succeed). The gfx-failures.log should then show one
 * "pso-retry ... recreated successfully" line. A short present loop afterwards triggers the periodic status line. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdlib.h>

static void *load(const char *path, DWORD *sz)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD rd; void *code;
    *sz = GetFileSize(h, NULL); code = malloc(*sz); ReadFile(h, code, *sz, &rd, NULL); CloseHandle(h);
    return code;
}

int main(void)
{
    D3D12_ROOT_PARAMETER rp = {0}; D3D12_ROOT_SIGNATURE_DESC rsd = {0}; D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {0};
    ID3D12Device *dev; ID3D12RootSignature *rs; ID3D12PipelineState *a = NULL, *b = NULL; ID3DBlob *blob, *err, *cached = NULL;
    DWORD sza, szb; void *ca = load("C:\\dxc\\nan.dxil", &sza), *cb = load("C:\\dxc\\fault.dxil", &szb);
    HRESULT hr; int fails = 0;

    D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rsd.NumParameters = 1; rsd.pParameters = &rp;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void **)&rs);

    pd.pRootSignature = rs; pd.CS.pShaderBytecode = ca; pd.CS.BytecodeLength = sza;
    hr = ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&a);
    printf("create A: 0x%08lx\n", hr); fails += FAILED(hr);
    hr = ID3D12PipelineState_GetCachedBlob(a, &cached);
    printf("GetCachedBlob(A): 0x%08lx, %lu bytes\n", hr, cached ? (unsigned long)ID3D10Blob_GetBufferSize(cached) : 0ul); fails += FAILED(hr);

    pd.CS.pShaderBytecode = cb; pd.CS.BytecodeLength = szb;
    pd.CachedPSO.pCachedBlob = ID3D10Blob_GetBufferPointer(cached); pd.CachedPSO.CachedBlobSizeInBytes = ID3D10Blob_GetBufferSize(cached);
    hr = ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&b);
    printf("create B with A's blob: 0x%08lx (expect 0x80070057)\n", hr); fails += hr != E_INVALIDARG;
    memset(&pd.CachedPSO, 0, sizeof(pd.CachedPSO));
    hr = ID3D12Device_CreateComputePipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&b);
    printf("create B without blob: 0x%08lx\n", hr); fails += FAILED(hr);
    printf("done, %d failures\n", fails);
    return 0;
}
