#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <dxgi1_6.h>
#include <stdio.h>
int main(void)
{
    IDXGIFactory1 *f; IDXGIAdapter1 *a; IDXGIAdapter3 *a3 = NULL; DXGI_ADAPTER_DESC1 d; DXGI_QUERY_VIDEO_MEMORY_INFO mi;
    CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f); IDXGIFactory1_EnumAdapters1(f, 0, &a); IDXGIAdapter1_GetDesc1(a, &d);
    printf("adapter: %ls vendor %04x device %08x\n", d.Description, d.VendorId, d.DeviceId);
    printf("DedicatedVideoMemory %.0f MB, DedicatedSystemMemory %.0f MB, SharedSystemMemory %.0f MB\n",
            d.DedicatedVideoMemory / 1048576.0, d.DedicatedSystemMemory / 1048576.0, d.SharedSystemMemory / 1048576.0);
    if (SUCCEEDED(IDXGIAdapter1_QueryInterface(a, &IID_IDXGIAdapter3, (void **)&a3)))
    {
        IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi);
        printf("local budget %.0f MB, in use %.0f MB\n", mi.Budget / 1048576.0, mi.CurrentUsage / 1048576.0);
        IDXGIAdapter3_QueryVideoMemoryInfo(a3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &mi);
        printf("non-local budget %.0f MB\n", mi.Budget / 1048576.0);
    }
    return 0;
}
