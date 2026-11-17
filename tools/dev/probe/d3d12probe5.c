/* Probe #5: texture creation across dimensions (1D/1D array/2D array/cube/3D), mips, MSAA arrays and
 * usage flags (RT/DS/UAV/simultaneous access), committed + placed. Prints only failures plus totals. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <stdint.h>

static ID3D12Device* dev;
static ID3D12Heap* heap;
static int fails, tested;

static const char* dimname(D3D12_RESOURCE_DIMENSION d) {
	return d == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? "1D" : d == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? "2D" : "3D";
}

static void try_tex(D3D12_RESOURCE_DIMENSION dim, UINT w, UINT h, UINT16 da, UINT16 mips, DXGI_FORMAT fmt, UINT samples,
		D3D12_RESOURCE_FLAGS fl, const char* note) {
	D3D12_RESOURCE_DESC rd = { dim, 0, w, h, da, mips, fmt, { samples, 0 }, D3D12_TEXTURE_LAYOUT_UNKNOWN, fl };
	D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_STATES st = (fl & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ? D3D12_RESOURCE_STATE_DEPTH_WRITE :
		(fl & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COMMON;
	D3D12_CLEAR_VALUE cv = { fmt };
	const D3D12_CLEAR_VALUE* pcv = (fl & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) ? &cv : NULL;
	if (fl & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) cv.DepthStencil.Depth = 1.0f;
	ID3D12Resource* r = NULL;
	HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, st, pcv, &IID_ID3D12Resource, (void**)&r);
	tested++;
	if (FAILED(hr)) {
		fails++;
		printf("FAIL committed %s %ux%u da=%u mips=%u fmt=%d s=%u flags=0x%x (%s) hr=0x%08lx\n",
			dimname(dim), w, h, da, mips, fmt, samples, (unsigned)fl, note, (unsigned long)hr);
	}
	if (r) ID3D12Resource_Release(r);
	if (heap && samples == 1) {
		D3D12_RESOURCE_ALLOCATION_INFO ai = ID3D12Device_GetResourceAllocationInfo(dev, 0, 1, &rd);
		if (ai.SizeInBytes != UINT64_MAX && ai.SizeInBytes <= (128ull << 20)) {
			r = NULL;
			hr = ID3D12Device_CreatePlacedResource(dev, heap, 0, &rd, st, pcv, &IID_ID3D12Resource, (void**)&r);
			tested++;
			if (FAILED(hr)) {
				fails++;
				printf("FAIL placed    %s %ux%u da=%u mips=%u fmt=%d flags=0x%x (%s) hr=0x%08lx\n",
					dimname(dim), w, h, da, mips, fmt, (unsigned)fl, note, (unsigned long)hr);
			}
			if (r) ID3D12Resource_Release(r);
		}
	}
}

int main(void) {
	HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
	if (FAILED(hr)) { printf("device fail\n"); return 1; }
	D3D12_FEATURE_DATA_D3D12_OPTIONS o = {0};
	ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
	D3D12_HEAP_DESC hd = { 128ull << 20, { D3D12_HEAP_TYPE_DEFAULT }, D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT,
		o.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2 ? D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES : D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES };
	ID3D12Device_CreateHeap(dev, &hd, &IID_ID3D12Heap, (void**)&heap);
	printf("ResourceHeapTier=%d heap=%p\n", o.ResourceHeapTier, (void*)heap);

	const D3D12_RESOURCE_FLAGS RT = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, UAV = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		DS = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, SIM = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, NOSRV = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

	DXGI_FORMAT color[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R11G11B10_FLOAT,
		DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM,
		DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP, DXGI_FORMAT_B8G8R8A8_UNORM };
	DXGI_FORMAT bc[] = { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC5_UNORM,
		DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_TYPELESS, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM_SRGB };
	DXGI_FORMAT depth[] = { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
		DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R32G8X24_TYPELESS };
	D3D12_RESOURCE_FLAGS cflags[] = { 0, RT, UAV, RT | UAV, RT | SIM, UAV | SIM };

	for (int i = 0; i < (int)(sizeof(color) / sizeof(color[0])); i++) for (int f = 0; f < 6; f++) {
		DXGI_FORMAT fm = color[i]; D3D12_RESOURCE_FLAGS fl = cflags[f];
		D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { fm };
		ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
		if ((fl & RT) && !(fs.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) && fm != DXGI_FORMAT_R8G8B8A8_TYPELESS && fm != DXGI_FORMAT_R32_TYPELESS) continue;
		if ((fl & UAV) && !(fs.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) && fm != DXGI_FORMAT_R8G8B8A8_TYPELESS && fm != DXGI_FORMAT_R32_TYPELESS) continue;
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 256, 1, 1, 1, fm, 1, fl, "1D");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 256, 1, 1, 0, fm, 1, fl, "1D full mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 256, 1, 8, 4, fm, 1, fl, "1D array mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 512, 1, 0, fm, 1, fl, "2D full mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 1, 1, fm, 1, fl, "2D 1080p");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 256, 256, 6, 0, fm, 1, fl, "cube-compatible array6");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 256, 256, 24, 1, fm, 1, fl, "array24");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 160, 90, 64, 1, fm, 1, fl, "3D froxel");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 32, 32, 32, 0, fm, 1, fl, "3D LUT mips");
		if ((fl & RT) && !(fl & UAV) && !(fl & SIM)) {
			try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 1, 1, fm, 4, fl, "2D MSAA4");
			try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1024, 1024, 4, 1, fm, 4, fl, "2D MSAA4 array");
			try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 2, 1, fm, 2, fl, "2D MSAA2 array (stereo)");
		}
	}
	for (int i = 0; i < (int)(sizeof(bc) / sizeof(bc[0])); i++) {
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2048, 2048, 1, 0, bc[i], 1, 0, "BC 2D mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1024, 1024, 6, 0, bc[i], 1, 0, "BC cube array");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1000, 600, 1, 0, bc[i], 1, 0, "BC non-pow2 mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE3D, 64, 64, 64, 0, bc[i], 1, 0, "BC 3D");
	}
	D3D12_RESOURCE_FLAGS dflags[] = { DS, DS | NOSRV };
	for (int i = 0; i < (int)(sizeof(depth) / sizeof(depth[0])); i++) for (int f = 0; f < 2; f++) {
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 1, 1, depth[i], 1, dflags[f], "depth 2D");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 2048, 2048, 4, 1, depth[i], 1, dflags[f], "depth array4 (CSM)");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 512, 6, 1, depth[i], 1, dflags[f], "depth cube");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 512, 512, 36, 1, depth[i], 1, dflags[f], "depth cube array6");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 1, 0, depth[i], 1, dflags[f], "depth Hi-Z mips");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1920, 1080, 1, 1, depth[i], 4, dflags[f], "depth MSAA4");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 1024, 1024, 2, 1, depth[i], 4, dflags[f], "depth MSAA4 array");
		try_tex(D3D12_RESOURCE_DIMENSION_TEXTURE1D, 1024, 1, 1, 1, depth[i], 1, dflags[f], "depth 1D");
	}
	printf("tested %d, failures %d\n", tested, fails);
	return 0;
}
