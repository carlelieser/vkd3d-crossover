/* Probe #6: swapchain creation/recreation scenarios and presents during window changes. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>

static int fails;
static IDXGIFactory4* f;
static ID3D12CommandQueue* q;
static ID3D12Device* dev;

static void report(const char* what, HRESULT hr) {
	if (FAILED(hr)) fails++;
	printf("%-64s hr=0x%08lx %s\n", what, (unsigned long)hr, SUCCEEDED(hr) ? "OK" : "FAIL <<<");
	fflush(stdout);
}

static void pump(void) {
	MSG m;
	while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

static HWND mkwin(DWORD style, int w, int h, BOOL show, HWND parent) {
	HWND wnd = CreateWindowExA(0, "probe6", "probe6", style, 50, 50, w, h, parent, NULL, GetModuleHandleA(NULL), NULL);
	if (show) ShowWindow(wnd, SW_SHOW);
	pump();
	return wnd;
}

static IDXGISwapChain3* mksc(HWND wnd, UINT w, UINT h, DXGI_FORMAT fmt, const char* what) {
	DXGI_SWAP_CHAIN_DESC1 d = {0};
	d.Width = w; d.Height = h; d.Format = fmt; d.SampleDesc.Count = 1;
	d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
	d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_STRETCH;
	d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	IDXGISwapChain1* s1 = NULL; IDXGISwapChain3* s3 = NULL;
	HRESULT hr = IDXGIFactory4_CreateSwapChainForHwnd(f, (IUnknown*)q, wnd, &d, NULL, NULL, &s1);
	report(what, hr);
	if (s1) { IDXGISwapChain1_QueryInterface(s1, &IID_IDXGISwapChain3, (void**)&s3); IDXGISwapChain1_Release(s1); }
	return s3;
}

static void present_n(IDXGISwapChain3* sc, int n, const char* what) {
	HRESULT worst = S_OK;
	for (int i = 0; i < n; i++) {
		HRESULT hr = IDXGISwapChain3_Present(sc, 0, DXGI_PRESENT_ALLOW_TEARING);
		if (FAILED(hr) || (hr != S_OK && worst == S_OK)) worst = hr;
		pump();
	}
	char buf[128]; snprintf(buf, sizeof(buf), "  %d presents: %s", n, what);
	report(buf, worst);
}

static void wait_idle(void) {
	ID3D12Fence* fe = NULL;
	ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void**)&fe);
	ID3D12CommandQueue_Signal(q, fe, 1);
	HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
	ID3D12Fence_SetEventOnCompletion(fe, 1, ev);
	WaitForSingleObject(ev, 5000);
	CloseHandle(ev); ID3D12Fence_Release(fe);
}

int main(void) {
	CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&f);
	report("D3D12CreateDevice", D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev));
	D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&q);
	WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe6";
	RegisterClassA(&wc);

	/* 1. Recreate on the same window (release, then create again), 3 times. */
	HWND w1 = mkwin(WS_OVERLAPPEDWINDOW, 1280, 720, TRUE, NULL);
	for (int i = 0; i < 3; i++) {
		char b[64]; snprintf(b, sizeof(b), "same window, create #%d", i + 1);
		IDXGISwapChain3* sc = mksc(w1, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, b);
		if (sc) { present_n(sc, 3, "after create"); wait_idle(); IDXGISwapChain3_Release(sc); }
	}
	/* 2. Second swapchain on a window that already has one (DXGI says E_ACCESSDENIED). */
	IDXGISwapChain3* keep = mksc(w1, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, "same window, keep first");
	IDXGISwapChain3* dup = mksc(w1, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, "same window, second concurrent (expect E_ACCESSDENIED)");
	if (dup) IDXGISwapChain3_Release(dup);
	/* 3. Present through resizes, minimize, hide. */
	if (keep) {
		SetWindowPos(w1, NULL, 0, 0, 1920, 1080, SWP_NOMOVE | SWP_NOZORDER); pump();
		present_n(keep, 5, "after window resize w/o ResizeBuffers");
		report("  ResizeBuffers to window size", IDXGISwapChain3_ResizeBuffers(keep, 0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
		present_n(keep, 5, "after ResizeBuffers(0,0)");
		report("  ResizeBuffers 3840x2160 (> window)", IDXGISwapChain3_ResizeBuffers(keep, 2, 3840, 2160, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
		present_n(keep, 5, "4K buffers into 1080p window");
		ShowWindow(w1, SW_MINIMIZE); pump(); Sleep(300); pump();
		present_n(keep, 5, "while minimized");
		report("  ResizeBuffers(0,0) while minimized", IDXGISwapChain3_ResizeBuffers(keep, 0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
		ShowWindow(w1, SW_RESTORE); pump(); Sleep(300); pump();
		present_n(keep, 5, "after restore");
		ShowWindow(w1, SW_HIDE); pump(); Sleep(200);
		present_n(keep, 5, "while hidden");
		ShowWindow(w1, SW_SHOW); pump();
		SetWindowLongA(w1, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(w1, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_FRAMECHANGED); pump();
		present_n(keep, 5, "borderless fullscreen style change");
		wait_idle();
		IDXGISwapChain3_Release(keep);
	}
	/* 4. Creation on unusual windows. */
	HWND hidden = mkwin(WS_OVERLAPPEDWINDOW, 1280, 720, FALSE, NULL);
	IDXGISwapChain3* sc = mksc(hidden, 1280, 720, DXGI_FORMAT_R8G8B8A8_UNORM, "hidden (never shown) window");
	if (sc) { present_n(sc, 3, "hidden window"); ShowWindow(hidden, SW_SHOW); pump(); present_n(sc, 3, "after first show"); wait_idle(); IDXGISwapChain3_Release(sc); }
	HWND tiny = mkwin(WS_POPUP, 1, 1, TRUE, NULL);
	sc = mksc(tiny, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, "1x1 popup, size 0x0 (from window)");
	if (sc) { present_n(sc, 3, "1x1 window"); SetWindowPos(tiny, NULL, 0, 0, 1600, 900, SWP_NOMOVE | SWP_NOZORDER); pump();
		report("  ResizeBuffers after growing", IDXGISwapChain3_ResizeBuffers(sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
		present_n(sc, 3, "after growing"); wait_idle(); IDXGISwapChain3_Release(sc); }
	HWND mini = mkwin(WS_OVERLAPPEDWINDOW, 1280, 720, TRUE, NULL);
	ShowWindow(mini, SW_MINIMIZE); pump(); Sleep(300); pump();
	sc = mksc(mini, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, "minimized window, size from window");
	if (sc) { present_n(sc, 3, "minimized"); wait_idle(); IDXGISwapChain3_Release(sc); }
	HWND parent = mkwin(WS_OVERLAPPEDWINDOW, 1280, 720, TRUE, NULL);
	HWND child = mkwin(WS_CHILD | WS_VISIBLE, 1200, 650, TRUE, parent);
	sc = mksc(child, 1200, 650, DXGI_FORMAT_R8G8B8A8_UNORM, "child window");
	if (sc) { present_n(sc, 3, "child window"); wait_idle(); IDXGISwapChain3_Release(sc); }
	HWND big = mkwin(WS_POPUP, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE, NULL);
	sc = mksc(big, 3840, 2160, DXGI_FORMAT_R16G16B16A16_FLOAT, "screen-size popup, 4K FP16 buffers");
	if (sc) { present_n(sc, 3, "fp16 popup"); wait_idle(); IDXGISwapChain3_Release(sc); }
	/* 5. Destroy the window under a live swapchain, then present. */
	HWND doomed = mkwin(WS_OVERLAPPEDWINDOW, 800, 600, TRUE, NULL);
	sc = mksc(doomed, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, "window to be destroyed");
	if (sc) { DestroyWindow(doomed); pump(); present_n(sc, 3, "after DestroyWindow"); wait_idle(); IDXGISwapChain3_Release(sc); }
	printf("done, %d failures\n", fails);
	return 0;
}
