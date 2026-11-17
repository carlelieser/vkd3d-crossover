/* D3D12 allocation probe: performs the kinds of large allocations modern engines (e.g. RE Engine) make at
 * startup and prints the HRESULT of each, to find which ones vkd3d-proton-on-MoltenVK rejects. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>

static ID3D12Device* dev;

static void report(const char* what, HRESULT hr) {
	printf("%-58s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : (hr == E_OUTOFMEMORY ? "E_OUTOFMEMORY  <<<" : "FAIL <<<"));
	fflush(stdout);
}

static void heap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT n, BOOL visible, const char* name) {
	D3D12_DESCRIPTOR_HEAP_DESC d = { type, n, visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	ID3D12DescriptorHeap* h = NULL; char buf[128];
	HRESULT hr = ID3D12Device_CreateDescriptorHeap(dev, &d, &IID_ID3D12DescriptorHeap, (void**)&h);
	snprintf(buf, sizeof(buf), "DescriptorHeap %s x%u%s", name, n, visible ? " (shader-visible)" : "");
	report(buf, hr);
	if (h) ID3D12DescriptorHeap_Release(h);
}

static void mheap(D3D12_HEAP_TYPE type, UINT64 size, D3D12_HEAP_FLAGS flags, const char* name) {
	D3D12_HEAP_DESC d = { size, { type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 }, 0, flags };
	ID3D12Heap* h = NULL; char buf[128];
	HRESULT hr = ID3D12Device_CreateHeap(dev, &d, &IID_ID3D12Heap, (void**)&h);
	snprintf(buf, sizeof(buf), "Heap %s %llu MiB flags=0x%x", name, (unsigned long long)(size >> 20), (unsigned)flags);
	report(buf, hr);
	if (h) ID3D12Heap_Release(h);
}

static void buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_FLAGS flags, const char* name) {
	D3D12_HEAP_PROPERTIES hp = { type };
	D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags };
	ID3D12Resource* r = NULL; char buf[128];
	D3D12_RESOURCE_STATES st = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : (type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON);
	HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, st, NULL, &IID_ID3D12Resource, (void**)&r);
	snprintf(buf, sizeof(buf), "Committed buffer %s %llu MiB", name, (unsigned long long)(size >> 20));
	report(buf, hr);
	if (r) ID3D12Resource_Release(r);
}

static void texture(UINT64 w, UINT h, UINT16 depthOrArray, UINT16 mips, DXGI_FORMAT fmt, D3D12_RESOURCE_DIMENSION dim, D3D12_RESOURCE_FLAGS flags, const char* name) {
	D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC rd = { dim, 0, w, h, depthOrArray, mips, fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags };
	ID3D12Resource* r = NULL;
	HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&r);
	report(name, hr);
	if (r) ID3D12Resource_Release(r);
}

static void reserved(D3D12_RESOURCE_DIMENSION dim, UINT64 w, UINT h, DXGI_FORMAT fmt, const char* name) {
	D3D12_RESOURCE_DESC rd = { dim, 0, w, h, 1, 1, fmt, {1, 0}, dim == D3D12_RESOURCE_DIMENSION_BUFFER ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* r = NULL;
	HRESULT hr = ID3D12Device_CreateReservedResource(dev, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&r);
	report(name, hr);
	if (r) ID3D12Resource_Release(r);
}

static void query(D3D12_QUERY_HEAP_TYPE type, UINT n, const char* name) {
	D3D12_QUERY_HEAP_DESC d = { type, n, 0 };
	ID3D12QueryHeap* q = NULL; char buf[128];
	HRESULT hr = ID3D12Device_CreateQueryHeap(dev, &d, &IID_ID3D12QueryHeap, (void**)&q);
	snprintf(buf, sizeof(buf), "QueryHeap %s x%u", name, n);
	report(buf, hr);
	if (q) ID3D12QueryHeap_Release(q);
}

int main(void) {
	HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
	report("D3D12CreateDevice FL11_0", hr);
	if (FAILED(hr)) return 1;

	D3D12_FEATURE_DATA_D3D12_OPTIONS o = {0};
	ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
	printf("ResourceBindingTier=%d TiledResourcesTier=%d ResourceHeapTier=%d\n", o.ResourceBindingTier, o.TiledResourcesTier, o.ResourceHeapTier);

	/* Descriptor heaps (tier-3 engines allocate up to 1M CBV/SRV/UAV and the full 2048 samplers). */
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, TRUE, "CBV_SRV_UAV");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 500000, TRUE, "CBV_SRV_UAV");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, TRUE, "CBV_SRV_UAV");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000, FALSE, "CBV_SRV_UAV");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, TRUE, "SAMPLER");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024, TRUE, "SAMPLER");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4096, FALSE, "SAMPLER");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8192, FALSE, "RTV");
	heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2048, FALSE, "DSV");

	/* Memory heaps for placed resources. */
	mheap(D3D12_HEAP_TYPE_DEFAULT, 256ull << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, "DEFAULT buffers");
	mheap(D3D12_HEAP_TYPE_DEFAULT, 256ull << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, "DEFAULT textures");
	mheap(D3D12_HEAP_TYPE_DEFAULT, 256ull << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES, "DEFAULT rt/ds");
	mheap(D3D12_HEAP_TYPE_DEFAULT, 1ull << 30, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, "DEFAULT all");
	mheap(D3D12_HEAP_TYPE_DEFAULT, 4ull << 30, D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES, "DEFAULT all");
	mheap(D3D12_HEAP_TYPE_UPLOAD, 256ull << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, "UPLOAD");
	mheap(D3D12_HEAP_TYPE_READBACK, 64ull << 20, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, "READBACK");

	/* Committed buffers / textures. */
	buffer(D3D12_HEAP_TYPE_DEFAULT, 512ull << 20, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, "DEFAULT UAV");
	buffer(D3D12_HEAP_TYPE_DEFAULT, 2ull << 30, D3D12_RESOURCE_FLAG_NONE, "DEFAULT");
	buffer(D3D12_HEAP_TYPE_DEFAULT, 4ull << 30, D3D12_RESOURCE_FLAG_NONE, "DEFAULT");
	buffer(D3D12_HEAP_TYPE_UPLOAD, 512ull << 20, D3D12_RESOURCE_FLAG_NONE, "UPLOAD");
	texture(16384, 16384, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_FLAG_NONE, "Texture2D 16384^2 RGBA8");
	texture(4096, 4096, 1, 1, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, "Depth 4096^2 D32");
	texture(2048, 2048, 64, 1, DXGI_FORMAT_BC7_UNORM, D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_FLAG_NONE, "Texture2DArray 2048^2 x64 BC7");
	texture(256, 256, 256, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_DIMENSION_TEXTURE3D, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, "Texture3D 256^3 RGBA16F UAV");

	/* Reserved (tiled) resources. */
	reserved(D3D12_RESOURCE_DIMENSION_BUFFER, 1ull << 30, 1, DXGI_FORMAT_UNKNOWN, "Reserved buffer 1 GiB");
	reserved(D3D12_RESOURCE_DIMENSION_TEXTURE2D, 16384, 16384, DXGI_FORMAT_BC7_UNORM, "Reserved Texture2D 16384^2 BC7");

	/* Query heaps. */
	query(D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 65536, "TIMESTAMP");
	query(D3D12_QUERY_HEAP_TYPE_OCCLUSION, 8192, "OCCLUSION");
	query(D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1024, "PIPELINE_STATISTICS");

	printf("done\n");
	return 0;
}
