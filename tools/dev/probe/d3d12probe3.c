/* D3D12 startup probe #3: samplers (border colors, anisotropy, comparison, min/max), root signatures
 * (static samplers, bindless tables, root constants), command signatures, MSAA resources, placed
 * resources, fences (incl. shared), typed buffer views, device interfaces. Prints each HRESULT. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

static ID3D12Device* dev;
static int fails;

static void report(const char* what, HRESULT hr) {
	if (FAILED(hr)) fails++;
	printf("%-72s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL <<<");
	fflush(stdout);
}

typedef HRESULT (WINAPI *PFN_SERIALIZE_VERSIONED)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
static PFN_SERIALIZE_VERSIONED serialize_versioned;

static void root_sig(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* d, const char* name) {
	ID3DBlob *blob = NULL, *err = NULL; char buf[128];
	HRESULT hr = serialize_versioned(d, &blob, &err);
	snprintf(buf, sizeof(buf), "Serialize root sig: %s", name);
	report(buf, hr);
	if (err) { printf("    %s\n", (const char*)ID3D10Blob_GetBufferPointer(err)); ID3D10Blob_Release(err); }
	if (!blob) return;
	ID3D12RootSignature* rs = NULL;
	hr = ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature, (void**)&rs);
	snprintf(buf, sizeof(buf), "CreateRootSignature: %s", name);
	report(buf, hr);
	if (rs) ID3D12RootSignature_Release(rs);
	ID3D10Blob_Release(blob);
}

static D3D12_STATIC_SAMPLER_DESC ssamp(UINT reg, D3D12_FILTER f, D3D12_TEXTURE_ADDRESS_MODE am, D3D12_STATIC_BORDER_COLOR bc, D3D12_COMPARISON_FUNC cmp) {
	D3D12_STATIC_SAMPLER_DESC s = { f, am, am, am, 0.0f, 16, cmp, bc, 0.0f, D3D12_FLOAT32_MAX, reg, 0, D3D12_SHADER_VISIBILITY_ALL };
	return s;
}

int main(void) {
	HMODULE d3d12 = GetModuleHandleA("d3d12.dll");
	if (!d3d12) d3d12 = LoadLibraryA("d3d12.dll");
	serialize_versioned = (PFN_SERIALIZE_VERSIONED)GetProcAddress(d3d12, "D3D12SerializeVersionedRootSignature");

	HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void**)&dev);
	report("D3D12CreateDevice FL11_0", hr);
	if (FAILED(hr)) return 1;

	/* Device interface versions engines QI for. */
	const GUID* iids[] = { &IID_ID3D12Device1, &IID_ID3D12Device2, &IID_ID3D12Device3, &IID_ID3D12Device4, &IID_ID3D12Device5,
		&IID_ID3D12Device6, &IID_ID3D12Device7, &IID_ID3D12Device8, &IID_ID3D12Device9 };
	for (int i = 0; i < 9; i++) {
		IUnknown* u = NULL; char buf[64];
		hr = ID3D12Device_QueryInterface(dev, iids[i], (void**)&u);
		snprintf(buf, sizeof(buf), "QueryInterface ID3D12Device%d", i + 1);
		report(buf, hr);
		if (u) IUnknown_Release(u);
	}

	/* Descriptor-heap samplers. */
	D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
	ID3D12DescriptorHeap* sh = NULL;
	report("CreateDescriptorHeap SAMPLER x64", ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&sh));
	if (sh) {
		D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sh);
		UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		struct { D3D12_FILTER f; D3D12_TEXTURE_ADDRESS_MODE am; float bc[4]; D3D12_COMPARISON_FUNC cmp; const char* n; } s[] = {
			{ D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, {0,0,0,0}, D3D12_COMPARISON_FUNC_NEVER, "aniso16 wrap" },
			{ D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, {1,1,1,1}, D3D12_COMPARISON_FUNC_LESS_EQUAL, "cmp border white" },
			{ D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, {0,0,0,0}, D3D12_COMPARISON_FUNC_NEVER, "border transparent black" },
			{ D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, {0.5f,0.25f,0.1f,1}, D3D12_COMPARISON_FUNC_NEVER, "border custom color" },
			{ D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, {0,0,0,0}, D3D12_COMPARISON_FUNC_NEVER, "max reduction" },
			{ D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, {0,0,0,0}, D3D12_COMPARISON_FUNC_NEVER, "min reduction" },
			{ D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, {0,0,0,0}, D3D12_COMPARISON_FUNC_NEVER, "mirror once" },
		};
		for (int i = 0; i < (int)(sizeof(s) / sizeof(s[0])); i++) {
			D3D12_SAMPLER_DESC sd = { s[i].f, s[i].am, s[i].am, s[i].am, 0.0f, 16, s[i].cmp, { s[i].bc[0], s[i].bc[1], s[i].bc[2], s[i].bc[3] }, 0.0f, D3D12_FLOAT32_MAX };
			D3D12_CPU_DESCRIPTOR_HANDLE hh = { h.ptr + i * inc };
			ID3D12Device_CreateSampler(dev, &sd, hh); /* void; failures show only in the log */
			printf("CreateSampler %-58s (void)\n", s[i].n);
		}
	}

	/* Root signatures. */
	D3D12_STATIC_SAMPLER_DESC samplers[] = {
		ssamp(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, D3D12_COMPARISON_FUNC_NEVER),
		ssamp(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, D3D12_COMPARISON_FUNC_NEVER),
		ssamp(2, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_COMPARISON_FUNC_NEVER),
		ssamp(3, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE, D3D12_COMPARISON_FUNC_GREATER_EQUAL),
		ssamp(4, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, D3D12_COMPARISON_FUNC_NEVER),
		ssamp(5, D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, D3D12_COMPARISON_FUNC_NEVER),
	};
	D3D12_DESCRIPTOR_RANGE1 ranges[] = {
		{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0 },
		{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX, 0, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0 },
		{ D3D12_DESCRIPTOR_RANGE_TYPE_CBV, UINT_MAX, 0, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0 },
	};
	D3D12_DESCRIPTOR_RANGE1 sranges[] = { { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, UINT_MAX, 0, 4, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0 } };
	D3D12_ROOT_PARAMETER1 params[8] = {0};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[0].Constants.Num32BitValues = 16; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[1].Descriptor.ShaderRegister = 1; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; params[2].Descriptor.ShaderRegister = 0; params[2].Descriptor.RegisterSpace = 5; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; params[3].Descriptor.ShaderRegister = 0; params[3].Descriptor.RegisterSpace = 6; params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[4].DescriptorTable.NumDescriptorRanges = 3; params[4].DescriptorTable.pDescriptorRanges = ranges; params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[5].DescriptorTable.NumDescriptorRanges = 1; params[5].DescriptorTable.pDescriptorRanges = sranges; params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[6].Descriptor.ShaderRegister = 2; params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[7].Constants.ShaderRegister = 3; params[7].Constants.Num32BitValues = 4; params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC v = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
	v.Desc_1_1.NumStaticSamplers = 6; v.Desc_1_1.pStaticSamplers = samplers;
	v.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	root_sig(&v, "6 static samplers only (incl. border/cmp/max)");
	v.Desc_1_1.NumParameters = 8; v.Desc_1_1.pParameters = params;
	root_sig(&v, "bindless tables + root CBV/SRV/UAV + 20 constants + samplers");
	v.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
	v.Desc_1_1.NumParameters = 1;
	root_sig(&v, "SM6.6 directly indexed heaps + constants");
	{
		D3D12_ROOT_PARAMETER1 big = {0};
		big.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; big.Constants.Num32BitValues = 64; big.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC vb = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		vb.Desc_1_1.NumParameters = 1; vb.Desc_1_1.pParameters = &big;
		root_sig(&vb, "64 root constants (max 256 bytes)");
	}
	{
		D3D12_STATIC_SAMPLER_DESC many[16];
		for (int i = 0; i < 16; i++) many[i] = ssamp(i, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, D3D12_COMPARISON_FUNC_NEVER);
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC vm = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		vm.Desc_1_1.NumStaticSamplers = 16; vm.Desc_1_1.pStaticSamplers = many;
		root_sig(&vm, "16 static samplers");
	}

	/* Command signatures (no root signature needed for draw/dispatch-only). */
	{
		D3D12_INDIRECT_ARGUMENT_DESC a[1]; ID3D12CommandSignature* cs = NULL;
		D3D12_INDIRECT_ARGUMENT_TYPE types[] = { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH };
		UINT strides[] = { 16, 20, 12, 12 };
		const char* n[] = { "DRAW", "DRAW_INDEXED", "DISPATCH", "DISPATCH_MESH" };
		for (int i = 0; i < 4; i++) {
			a[0].Type = types[i];
			D3D12_COMMAND_SIGNATURE_DESC d = { strides[i], 1, a, 0 }; char buf[64];
			hr = ID3D12Device_CreateCommandSignature(dev, &d, NULL, &IID_ID3D12CommandSignature, (void**)&cs);
			snprintf(buf, sizeof(buf), "CreateCommandSignature %s", n[i]);
			report(buf, hr);
			if (cs) { ID3D12CommandSignature_Release(cs); cs = NULL; }
		}
	}

	/* MSAA. */
	{
		DXGI_FORMAT fmts[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT };
		for (int i = 0; i < 6; i++) for (UINT sc = 2; sc <= 8; sc *= 2) {
			D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ql = { fmts[i], sc, 0, 0 };
			ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ql, sizeof(ql));
			if (!ql.NumQualityLevels) { printf("MSAA fmt %d x%u: not advertised\n", fmts[i], sc); continue; }
			BOOL ds = i >= 3;
			D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
			D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 1920, 1080, 1, 1, fmts[i], { sc, 0 }, D3D12_TEXTURE_LAYOUT_UNKNOWN,
				ds ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
			ID3D12Resource* r = NULL; char buf[96];
			hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, ds ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&r);
			snprintf(buf, sizeof(buf), "MSAA texture fmt %d x%u", fmts[i], sc);
			report(buf, hr);
			if (r) ID3D12Resource_Release(r);
		}
	}

	/* Placed resources: textures aliased in one heap, and a buffer + texture in an ALLOW_ALL heap. */
	{
		D3D12_HEAP_DESC hdsc = { 64ull << 20, { D3D12_HEAP_TYPE_DEFAULT }, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES };
		ID3D12Heap* heap = NULL;
		report("CreateHeap 64MiB RT/DS MSAA-aligned", ID3D12Device_CreateHeap(dev, &hdsc, &IID_ID3D12Heap, (void**)&heap));
		if (heap) {
			D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 1920, 1080, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
			ID3D12Resource *a = NULL, *b = NULL;
			report("CreatePlacedResource RGBA16F RT+UAV @0", ID3D12Device_CreatePlacedResource(dev, heap, 0, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&a));
			rd.Format = DXGI_FORMAT_D32_FLOAT; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			report("CreatePlacedResource D32 aliased @0", ID3D12Device_CreatePlacedResource(dev, heap, 0, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void**)&b));
			rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 4; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			ID3D12Resource* c = NULL;
			report("CreatePlacedResource RGBA8 MSAA4 @32MiB", ID3D12Device_CreatePlacedResource(dev, heap, 32ull << 20, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&c));
			if (a) ID3D12Resource_Release(a); if (b) ID3D12Resource_Release(b); if (c) ID3D12Resource_Release(c);
			ID3D12Heap_Release(heap);
		}
	}

	/* Fences. */
	{
		ID3D12Fence* f = NULL;
		report("CreateFence NONE", ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&f));
		if (f) {
			HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
			report("Fence SetEventOnCompletion", ID3D12Fence_SetEventOnCompletion(f, 1, ev));
			report("Fence Signal(1)", ID3D12Fence_Signal(f, 1));
			printf("WaitForSingleObject = %lu\n", WaitForSingleObject(ev, 1000));
			ID3D12Fence_Release(f); f = NULL; CloseHandle(ev);
		}
		report("CreateFence SHARED", ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void**)&f));
		if (f) {
			HANDLE sh2 = NULL;
			report("CreateSharedHandle(fence)", ID3D12Device_CreateSharedHandle(dev, (ID3D12DeviceChild*)f, NULL, GENERIC_ALL, NULL, &sh2));
			if (sh2) CloseHandle(sh2);
			ID3D12Fence_Release(f);
		}
	}

	/* Typed buffer SRV/UAV views incl. R32G32B32 (needs a CPU heap). */
	{
		D3D12_DESCRIPTOR_HEAP_DESC vd = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
		ID3D12DescriptorHeap* vh = NULL;
		ID3D12Device_CreateDescriptorHeap(dev, &vd, &IID_ID3D12DescriptorHeap, (void**)&vh);
		D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
		D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, 1 << 20, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
		ID3D12Resource* b = NULL;
		report("Committed buffer 1MiB UAV", ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&b));
		if (vh && b) {
			D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(vh);
			DXGI_FORMAT f[] = { DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_UINT };
			for (int i = 0; i < 5; i++) {
				D3D12_SHADER_RESOURCE_VIEW_DESC s = { f[i], D3D12_SRV_DIMENSION_BUFFER, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
				s.Buffer.NumElements = 1024;
				ID3D12Device_CreateShaderResourceView(dev, b, &s, h);
				printf("Typed buffer SRV fmt %d (void)\n", f[i]);
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC raw = { DXGI_FORMAT_R32_TYPELESS, D3D12_SRV_DIMENSION_BUFFER, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
			raw.Buffer.NumElements = 1024; raw.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
			ID3D12Device_CreateShaderResourceView(dev, b, &raw, h);
			printf("Raw buffer SRV (void)\n");
		}
		if (b) ID3D12Resource_Release(b);
		if (vh) ID3D12DescriptorHeap_Release(vh);
	}

	/* Feature queries engines gate on. */
	{
		D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_7 };
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
		printf("ShaderModel highest = 0x%x (hr=0x%08lx)\n", sm.HighestShaderModel, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7));
		printf("MeshShaderTier=%d SamplerFeedbackTier=%d (hr=0x%08lx)\n", o7.MeshShaderTier, o7.SamplerFeedbackTier, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
		printf("RaytracingTier=%d RenderPassesTier=%d (hr=0x%08lx)\n", o5.RaytracingTier, o5.RenderPassesTier, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof(o6));
		printf("VariableShadingRateTier=%d (hr=0x%08lx)\n", o6.VariableShadingRateTier, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS9 o9 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS9, &o9, sizeof(o9));
		printf("AtomicInt64 typed=%d groupshared=%d (hr=0x%08lx)\n", o9.AtomicInt64OnTypedResourceSupported, o9.AtomicInt64OnGroupSharedSupported, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS11 o11 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS11, &o11, sizeof(o11));
		printf("AtomicInt64OnDescriptorHeapResource=%d (hr=0x%08lx)\n", o11.AtomicInt64OnDescriptorHeapResourceSupported, (unsigned long)hr);
		D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = {0};
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1));
		printf("Int64ShaderOps=%d WaveOps=%d WaveLaneCount=%u..%u (hr=0x%08lx)\n", o1.Int64ShaderOps, o1.WaveOps, o1.WaveLaneCountMin, o1.WaveLaneCountMax, (unsigned long)hr);
		D3D12_FEATURE_DATA_ROOT_SIGNATURE rs = { D3D_ROOT_SIGNATURE_VERSION_1_1 };
		hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_ROOT_SIGNATURE, &rs, sizeof(rs));
		printf("RootSignature highest = 0x%x (hr=0x%08lx)\n", rs.HighestVersion, (unsigned long)hr);
	}

	printf("done, %d failures\n", fails);
	return 0;
}
