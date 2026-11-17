/* Reports what Windows APIs say the current display is, DPI-unaware first, then per-monitor DPI-aware. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <dxgi1_6.h>
#include <stdio.h>

static void report(const char *tag)
{
    DEVMODEA dm = { .dmSize = sizeof(dm) }; MONITORINFOEXA mi = { .cbSize = sizeof(mi) };
    IDXGIFactory1 *f = NULL; IDXGIAdapter1 *a = NULL; IDXGIOutput *o = NULL; DXGI_OUTPUT_DESC od; DXGI_MODE_DESC want = {0}, got = {0};
    HMONITOR mon = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY); UINT n = 0;
    printf("== %s\n", tag);
    printf("GetSystemMetrics: %d x %d, DPI for system %u\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), GetDpiForSystem());
    EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm);
    printf("EnumDisplaySettings(current): %lu x %lu @ %lu Hz\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    GetMonitorInfoA(mon, (MONITORINFO *)&mi);
    printf("GetMonitorInfo: %ld x %ld (%s)\n", mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, mi.szDevice);
    CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f);
    if (f && SUCCEEDED(IDXGIFactory1_EnumAdapters1(f, 0, &a)) && SUCCEEDED(IDXGIAdapter1_EnumOutputs(a, 0, &o)))
    {
        IDXGIOutput_GetDesc(o, &od);
        printf("DXGI output desktop: %ld x %ld\n", od.DesktopCoordinates.right - od.DesktopCoordinates.left, od.DesktopCoordinates.bottom - od.DesktopCoordinates.top);
        IDXGIOutput_GetDisplayModeList(o, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, NULL);
        want.Width = od.DesktopCoordinates.right - od.DesktopCoordinates.left; want.Height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top; want.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        IDXGIOutput_FindClosestMatchingMode(o, &want, &got, NULL);
        printf("DXGI modes: %u, closest to desktop: %u x %u @ %u/%u\n", n, got.Width, got.Height, got.RefreshRate.Numerator, got.RefreshRate.Denominator);
    }
    fflush(stdout);
}

int main(void)
{
    report("DPI-unaware (default)");
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    report("per-monitor DPI-aware");
    return 0;
}
