/* DXGI probe #4: fullscreen / mode-switch / output / HDR metadata / latency calls on a D3D12 swapchain. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

static int fails;
static void report(const char* what, HRESULT hr) {
	if (FAILED(hr)) fails++;
	printf("%-66s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL <<<");
	fflush(stdout);
}

int main(void) {
	IDXGIFactory6* f = NULL;
	report("CreateDXGIFactory2(IDXGIFactory6)", CreateDXGIFactory2(0, &IID_IDXGIFactory6, (void**)&f));
	if (!f) return 1;
	IDXGIAdapter1* ad = NULL;
	report("EnumAdapterByGpuPreference(HIGH_PERFORMANCE)", IDXGIFactory6_EnumAdapterByGpuPreference(f, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, (void**)&ad));
	if (!ad) IDXGIFactory6_EnumAdapters1(f, 0, &ad);
	IDXGIAdapter3* ad3 = NULL;
	if (SUCCEEDED(IDXGIAdapter1_QueryInterface(ad, &IID_IDXGIAdapter3, (void**)&ad3))) {
		DXGI_QUERY_VIDEO_MEMORY_INFO mi;
		report("QueryVideoMemoryInfo LOCAL", IDXGIAdapter3_QueryVideoMemoryInfo(ad3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi));
		printf("  budget=%llu MiB usage=%llu MiB\n", (unsigned long long)(mi.Budget >> 20), (unsigned long long)(mi.CurrentUsage >> 20));
		report("QueryVideoMemoryInfo NON_LOCAL", IDXGIAdapter3_QueryVideoMemoryInfo(ad3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &mi));
		report("SetVideoMemoryReservation", IDXGIAdapter3_SetVideoMemoryReservation(ad3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 256ull << 20));
		DWORD cookie = 0; HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
		report("RegisterVideoMemoryBudgetChangeNotificationEvent", IDXGIAdapter3_RegisterVideoMemoryBudgetChangeNotificationEvent(ad3, ev, &cookie));
		IDXGIAdapter3_Release(ad3);
	}
	DXGI_ADAPTER_DESC1 ad_desc;
	report("Adapter GetDesc1", IDXGIAdapter1_GetDesc1(ad, &ad_desc));
	LARGE_INTEGER umd;
	report("CheckInterfaceSupport(IDXGIDevice)", IDXGIAdapter1_CheckInterfaceSupport(ad, &IID_IDXGIDevice, &umd));

	ID3D12Device* dev = NULL;
	report("D3D12CreateDevice", D3D12CreateDevice((IUnknown*)ad, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev));
	if (!dev) return 1;
	D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12CommandQueue* q = NULL;
	ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&q);

	WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe4";
	RegisterClassA(&wc);
	HWND wnd = CreateWindowA("probe4", "probe4", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 1280, 720, NULL, NULL, wc.hInstance, NULL);
	report("MakeWindowAssociation(NO_ALT_ENTER)", IDXGIFactory6_MakeWindowAssociation(f, wnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES));

	/* Classic flip-model swapchain with mode switch, via CreateSwapChainForHwnd + fullscreen desc. */
	DXGI_SWAP_CHAIN_DESC1 d = {0};
	d.Width = 1920; d.Height = 1080; d.Format = DXGI_FORMAT_R10G10B10A2_UNORM; d.SampleDesc.Count = 1;
	d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT; d.BufferCount = 3;
	d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_NONE; d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fd = { { 60, 1 }, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, DXGI_MODE_SCALING_UNSPECIFIED, TRUE };
	IDXGISwapChain1* sc1 = NULL;
	report("CreateSwapChainForHwnd RGB10A2 + SHADER_INPUT + MODE_SWITCH + fsdesc", IDXGIFactory6_CreateSwapChainForHwnd(f, (IUnknown*)q, wnd, &d, &fd, NULL, &sc1));
	if (!sc1) {
		d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		report("  retry without SHADER_INPUT", IDXGIFactory6_CreateSwapChainForHwnd(f, (IUnknown*)q, wnd, &d, &fd, NULL, &sc1));
	}
	if (!sc1) return 1;
	IDXGISwapChain4* sc = NULL;
	report("QI IDXGISwapChain4", IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain4, (void**)&sc));
	if (!sc) return 1;

	report("SetMaximumFrameLatency(2)", IDXGISwapChain4_SetMaximumFrameLatency(sc, 2));
	HANDLE lat = IDXGISwapChain4_GetFrameLatencyWaitableObject(sc);
	printf("GetFrameLatencyWaitableObject = %p, wait=%lu\n", lat, lat ? WaitForSingleObject(lat, 1000) : 99);
	IDXGIOutput* out = NULL;
	report("GetContainingOutput", IDXGISwapChain4_GetContainingOutput(sc, &out));
	if (out) {
		DXGI_MODE_DESC want = { 1920, 1080, { 60, 1 }, DXGI_FORMAT_R10G10B10A2_UNORM }, got;
		report("FindClosestMatchingMode RGB10A2 1080p60 (with device)", IDXGIOutput_FindClosestMatchingMode(out, &want, &got, (IUnknown*)dev));
		want.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		report("FindClosestMatchingMode RGBA8 1080p60 (no device)", IDXGIOutput_FindClosestMatchingMode(out, &want, &got, NULL));
		IDXGIOutput1* o1 = NULL;
		if (SUCCEEDED(IDXGIOutput_QueryInterface(out, &IID_IDXGIOutput1, (void**)&o1))) {
			DXGI_MODE_DESC1 w1 = { 1920, 1080, { 60, 1 }, DXGI_FORMAT_R10G10B10A2_UNORM }, g1;
			report("Output1 FindClosestMatchingMode1", IDXGIOutput1_FindClosestMatchingMode1(o1, &w1, &g1, (IUnknown*)dev));
			UINT n = 0;
			report("Output1 GetDisplayModeList1 RGB10A2", IDXGIOutput1_GetDisplayModeList1(o1, DXGI_FORMAT_R10G10B10A2_UNORM, 0, &n, NULL));
			printf("  modes=%u\n", n);
			IDXGIOutput1_Release(o1);
		}
		DXGI_FRAME_STATISTICS fs;
		HRESULT hr = IDXGIOutput_GetFrameStatistics(out, &fs);
		printf("Output GetFrameStatistics hr=0x%08lx (informational)\n", (unsigned long)hr);
		report("Output WaitForVBlank", IDXGIOutput_WaitForVBlank(out));
		DXGI_GAMMA_CONTROL_CAPABILITIES gc;
		hr = IDXGIOutput_GetGammaControlCapabilities(out, &gc);
		printf("GetGammaControlCapabilities hr=0x%08lx (informational)\n", (unsigned long)hr);
	}

	DXGI_HDR_METADATA_HDR10 md = { {34000, 16000}, {13250, 34500}, {7500, 3000}, {15635, 16450}, 10000000, 1, 1000, 400 };
	report("SetHDRMetaData HDR10", IDXGISwapChain4_SetHDRMetaData(sc, DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md));
	report("SetHDRMetaData NONE", IDXGISwapChain4_SetHDRMetaData(sc, DXGI_HDR_METADATA_TYPE_NONE, 0, NULL));
	UINT csf = 0;
	report("CheckColorSpaceSupport G2084_P2020", IDXGISwapChain4_CheckColorSpaceSupport(sc, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &csf));
	printf("  flags=%u\n", csf);
	report("SetColorSpace1 G22_P709", IDXGISwapChain4_SetColorSpace1(sc, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
	DXGI_SWAP_CHAIN_DESC1 dd; report("GetDesc1", IDXGISwapChain4_GetDesc1(sc, &dd));
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fdd; report("GetFullscreenDesc", IDXGISwapChain4_GetFullscreenDesc(sc, &fdd));
	HWND h2; report("GetHwnd", IDXGISwapChain4_GetHwnd(sc, &h2));
	UINT idx = IDXGISwapChain4_GetCurrentBackBufferIndex(sc); printf("GetCurrentBackBufferIndex=%u\n", idx);
	DXGI_RGBA bg = {0, 0, 0, 1}; HRESULT hr = IDXGISwapChain4_SetBackgroundColor(sc, &bg);
	printf("SetBackgroundColor hr=0x%08lx (informational)\n", (unsigned long)hr);
	DXGI_FRAME_STATISTICS sfs; hr = IDXGISwapChain4_GetFrameStatistics(sc, &sfs);
	printf("SwapChain GetFrameStatistics hr=0x%08lx (informational)\n", (unsigned long)hr);
	UINT pc; report("GetLastPresentCount", IDXGISwapChain4_GetLastPresentCount(sc, &pc));

	report("Present(1,0)", IDXGISwapChain4_Present(sc, 1, 0));
	DXGI_PRESENT_PARAMETERS pp = {0};
	report("Present1(0, ALLOW_TEARING)", IDXGISwapChain4_Present1(sc, 0, DXGI_PRESENT_ALLOW_TEARING, &pp));
	report("Present(0, TEST)", IDXGISwapChain4_Present(sc, 0, DXGI_PRESENT_TEST));

	/* Fullscreen transitions. */
	report("SetFullscreenState(TRUE, NULL)", IDXGISwapChain4_SetFullscreenState(sc, TRUE, NULL));
	BOOL fs_state = FALSE; IDXGIOutput* fso = NULL;
	report("GetFullscreenState", IDXGISwapChain4_GetFullscreenState(sc, &fs_state, &fso));
	printf("  fullscreen=%d\n", fs_state);
	if (fso) IDXGIOutput_Release(fso);
	DXGI_MODE_DESC tm = { 1920, 1080, { 60, 1 }, DXGI_FORMAT_R10G10B10A2_UNORM };
	report("ResizeTarget 1920x1080", IDXGISwapChain4_ResizeTarget(sc, &tm));
	report("ResizeBuffers (keep)", IDXGISwapChain4_ResizeBuffers(sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, d.Flags));
	report("Present(1,0) fullscreen", IDXGISwapChain4_Present(sc, 1, 0));
	report("SetFullscreenState(FALSE)", IDXGISwapChain4_SetFullscreenState(sc, FALSE, NULL));
	report("ResizeBuffers1 after windowed", IDXGISwapChain4_ResizeBuffers(sc, 3, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, d.Flags));
	if (out) {
		report("SetFullscreenState(TRUE, out)", IDXGISwapChain4_SetFullscreenState(sc, TRUE, out));
		report("SetFullscreenState(FALSE) again", IDXGISwapChain4_SetFullscreenState(sc, FALSE, NULL));
	}

	/* Legacy CreateSwapChain path (IDXGIFactory::CreateSwapChain with DXGI_SWAP_CHAIN_DESC). */
	IDXGISwapChain4_Release(sc); IDXGISwapChain1_Release(sc1);
	DXGI_SWAP_CHAIN_DESC ld = { { 1280, 720, { 0, 0 }, DXGI_FORMAT_R8G8B8A8_UNORM }, { 1, 0 }, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, wnd, TRUE, DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH };
	IDXGISwapChain* lsc = NULL;
	report("Factory::CreateSwapChain legacy desc", IDXGIFactory6_CreateSwapChain(f, (IUnknown*)q, &ld, &lsc));
	if (lsc) IDXGISwapChain_Release(lsc);

	printf("done, %d failures\n", fails);
	return 0;
}
