/* D3D12/DXGI startup probe #2: swapchains (incl. HDR formats/color spaces), queue types, query heap types,
 * and texture creation for every format CheckFeatureSupport claims to support. Prints each HRESULT. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

static ID3D12Device* dev;
static int fails;

static void report(const char* what, HRESULT hr) {
	if (FAILED(hr)) fails++;
	printf("%-70s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL <<<");
	fflush(stdout);
}

static void try_swapchain(IDXGIFactory4* f, ID3D12CommandQueue* q, HWND wnd, DXGI_FORMAT fmt, const char* name, DXGI_COLOR_SPACE_TYPE cs) {
	DXGI_SWAP_CHAIN_DESC1 d = {0};
	d.Width = 1280; d.Height = 720; d.Format = fmt; d.SampleDesc.Count = 1;
	d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 3;
	d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_STRETCH;
	d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	IDXGISwapChain1* sc1 = NULL; char buf[160];
	HRESULT hr = IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown*)q, wnd, &d, NULL, NULL, &sc1);
	snprintf(buf, sizeof(buf), "CreateSwapChainForHwnd %s (tearing+waitable, 3 buffers)", name);
	report(buf, hr);
	if (!sc1) return;
	IDXGISwapChain3* sc3 = NULL;
	if (SUCCEEDED(IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void**)&sc3))) {
		UINT support = 0;
		IDXGISwapChain3_CheckColorSpaceSupport(sc3, cs, &support);
		hr = IDXGISwapChain3_SetColorSpace1(sc3, cs);
		snprintf(buf, sizeof(buf), "  SetColorSpace1(%d) on %s (supportFlags=%u)", (int)cs, name, support);
		report(buf, hr);
		hr = IDXGISwapChain3_ResizeBuffers(sc3, 3, 1920, 1080, fmt, d.Flags);
		snprintf(buf, sizeof(buf), "  ResizeBuffers 1920x1080 %s", name);
		report(buf, hr);
		ID3D12Resource* bb = NULL;
		hr = IDXGISwapChain3_GetBuffer(sc3, 0, &IID_ID3D12Resource, (void**)&bb);
		report("  GetBuffer(0)", hr);
		if (bb) ID3D12Resource_Release(bb);
		hr = IDXGISwapChain3_Present(sc3, 0, DXGI_PRESENT_ALLOW_TEARING);
		report("  Present(0, ALLOW_TEARING)", hr);
		IDXGISwapChain3_Release(sc3);
	}
	IDXGISwapChain1_Release(sc1);
}

int main(void) {
	IDXGIFactory4* f = NULL;
	HRESULT hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&f);
	report("CreateDXGIFactory2", hr);
	IDXGIFactory5* f5 = NULL;
	if (f && SUCCEEDED(IDXGIFactory4_QueryInterface(f, &IID_IDXGIFactory5, (void**)&f5))) {
		BOOL tearing = FALSE;
		hr = IDXGIFactory5_CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));
		printf("DXGI_FEATURE_PRESENT_ALLOW_TEARING = %d (hr=0x%08lx)\n", tearing, (unsigned long)hr);
		IDXGIFactory5_Release(f5);
	}
	IDXGIAdapter1* ad = NULL;
	hr = IDXGIFactory4_EnumAdapters1(f, 0, &ad);
	report("EnumAdapters1(0)", hr);
	IDXGIOutput* out = NULL;
	if (ad) { hr = IDXGIAdapter1_EnumOutputs(ad, 0, &out); report("EnumOutputs(0)", hr); }
	if (out) {
		IDXGIOutput6* o6 = NULL;
		if (SUCCEEDED(IDXGIOutput_QueryInterface(out, &IID_IDXGIOutput6, (void**)&o6))) {
			DXGI_OUTPUT_DESC1 od; hr = IDXGIOutput6_GetDesc1(o6, &od);
			report("IDXGIOutput6::GetDesc1 (HDR query)", hr);
			IDXGIOutput6_Release(o6);
		} else report("QueryInterface IDXGIOutput6", E_NOINTERFACE);
		UINT n = 0; hr = IDXGIOutput_GetDisplayModeList(out, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, NULL);
		report("GetDisplayModeList R8G8B8A8", hr);
		hr = IDXGIOutput_GetDisplayModeList(out, DXGI_FORMAT_R10G10B10A2_UNORM, 0, &n, NULL);
		report("GetDisplayModeList R10G10B10A2", hr);
	}

	hr = D3D12CreateDevice((IUnknown*)ad, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
	report("D3D12CreateDevice", hr);
	if (FAILED(hr)) return 1;

	/* Command queues of every type and priority. */
	D3D12_COMMAND_LIST_TYPE types[] = { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY };
	const char* tn[] = { "DIRECT", "COMPUTE", "COPY" };
	ID3D12CommandQueue* direct = NULL;
	for (int i = 0; i < 3; i++) for (int pr = 0; pr < 2; pr++) {
		D3D12_COMMAND_QUEUE_DESC qd = { types[i], pr ? D3D12_COMMAND_QUEUE_PRIORITY_HIGH : D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, 0, 0 };
		ID3D12CommandQueue* q = NULL; char buf[96];
		hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&q);
		snprintf(buf, sizeof(buf), "CreateCommandQueue %s priority=%s", tn[i], pr ? "HIGH" : "NORMAL");
		report(buf, hr);
		if (q && i == 0 && !pr) direct = q; else if (q) ID3D12CommandQueue_Release(q);
	}

	/* Query heaps of every type. */
	struct { D3D12_QUERY_HEAP_TYPE t; const char* n; D3D12_COMMAND_LIST_TYPE ql; } qh[] = {
		{ D3D12_QUERY_HEAP_TYPE_OCCLUSION, "OCCLUSION" }, { D3D12_QUERY_HEAP_TYPE_TIMESTAMP, "TIMESTAMP" },
		{ D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, "PIPELINE_STATISTICS" }, { D3D12_QUERY_HEAP_TYPE_SO_STATISTICS, "SO_STATISTICS" },
		{ D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP, "COPY_QUEUE_TIMESTAMP" },
	};
	for (int i = 0; i < 5; i++) {
		D3D12_QUERY_HEAP_DESC d = { qh[i].t, 256, 0 }; ID3D12QueryHeap* h = NULL; char buf[96];
		hr = ID3D12Device_CreateQueryHeap(dev, &d, &IID_ID3D12QueryHeap, (void**)&h);
		snprintf(buf, sizeof(buf), "CreateQueryHeap %s x256", qh[i].n);
		report(buf, hr);
		if (h) ID3D12QueryHeap_Release(h);
	}

	/* Swapchains. */
	WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe2";
	RegisterClassA(&wc);
	HWND wnd = CreateWindowA("probe2", "probe2", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, NULL, NULL, wc.hInstance, NULL);
	if (direct && wnd) {
		try_swapchain(f, direct, wnd, DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		try_swapchain(f, direct, wnd, DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM", DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		try_swapchain(f, direct, wnd, DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2_UNORM", DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		try_swapchain(f, direct, wnd, DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
	}

	/* Texture creation for every format whose CheckFeatureSupport says TEXTURE2D. */
	int tested = 0;
	for (int fmt = 1; fmt <= 132; fmt++) {
		D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { (DXGI_FORMAT)fmt };
		if (FAILED(ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs)))) continue;
		if (!(fs.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)) continue;
		D3D12_RESOURCE_FLAGS fl = D3D12_RESOURCE_FLAG_NONE;
		if (fs.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) fl |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		else if (fs.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) fl |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		if (fs.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) fl |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		UINT w = 256, h = 256;
		D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
		D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, (DXGI_FORMAT)fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, fl };
		ID3D12Resource* r = NULL; char buf[96];
		hr = ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource, (void**)&r);
		tested++;
		if (FAILED(hr)) { snprintf(buf, sizeof(buf), "Texture2D format %d flags 0x%x (CheckFeatureSupport says supported)", fmt, (unsigned)fl); report(buf, hr); }
		if (r) ID3D12Resource_Release(r);
	}
	printf("format sweep: %d formats tested\n", tested);
	printf("done, %d failures\n", fails);
	return 0;
}
