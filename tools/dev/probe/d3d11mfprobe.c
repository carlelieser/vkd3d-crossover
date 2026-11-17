/* D3D11 + Media Foundation device manager probe: what RE Engine does before video playback. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <stdio.h>

int main(void)
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    IDXGIFactory1 *f = NULL; IDXGIAdapter1 *ad = NULL; ID3D11Device *dev = NULL; ID3D11DeviceContext *ctx = NULL;
    ID3D10Multithread *mt = NULL; IMFDXGIDeviceManager *mgr = NULL; ID3D11VideoDevice *vd = NULL;
    D3D_FEATURE_LEVEL fl = 0; UINT token = 0; HRESULT hr; char path[MAX_PATH] = "?";
    HMODULE m;

    printf("CreateDXGIFactory1: 0x%08lx\n", CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f));
    if (f) printf("EnumAdapters1: 0x%08lx\n", IDXGIFactory1_EnumAdapters1(f, 0, &ad));
    hr = D3D11CreateDevice((IDXGIAdapter *)ad, ad ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if ((m = GetModuleHandleA("d3d11.dll"))) GetModuleFileNameA(m, path, sizeof(path));
    printf("d3d11.dll in use: %s\n", path);
    printf("D3D11CreateDevice(VIDEO_SUPPORT|BGRA): 0x%08lx, feature level %#x\n", hr, fl);
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice((IDXGIAdapter *)ad, ad ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        printf("D3D11CreateDevice(BGRA only): 0x%08lx, feature level %#x\n", hr, fl);
    }
    if (dev)
    {
        printf("QI ID3D10Multithread: 0x%08lx\n", ID3D11Device_QueryInterface(dev, &IID_ID3D10Multithread, (void **)&mt));
        if (mt) ID3D10Multithread_SetMultithreadProtected(mt, TRUE);
        printf("QI ID3D11VideoDevice: 0x%08lx\n", ID3D11Device_QueryInterface(dev, &IID_ID3D11VideoDevice, (void **)&vd));
    }
    printf("MFStartup: 0x%08lx\n", MFStartup(MF_VERSION, 0));
    printf("MFCreateDXGIDeviceManager: 0x%08lx\n", MFCreateDXGIDeviceManager(&token, &mgr));
    if (mgr && dev) printf("ResetDevice: 0x%08lx\n", IMFDXGIDeviceManager_ResetDevice(mgr, (IUnknown *)dev, token));
    printf("done\n");
    return 0;
}
