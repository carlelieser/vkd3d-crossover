/* Display sizes from the APIs not covered before: QueryDisplayConfig (source/target modes, preferred mode),
 * EnumDisplaySettings(ENUM_REGISTRY_SETTINGS), mode 0, GetDeviceCaps. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    UINT32 np = 0, nm = 0, i; DISPLAYCONFIG_PATH_INFO p[8]; DISPLAYCONFIG_MODE_INFO m[16]; LONG r;
    DEVMODEA dm = { .dmSize = sizeof(dm) }; HDC dc = GetDC(NULL);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    printf("GetDeviceCaps: HORZRES %d VERTRES %d DESKTOPHORZRES %d DESKTOPVERTRES %d LOGPIXELSX %d HORZSIZE %dmm VERTSIZE %dmm\n",
           GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, DESKTOPHORZRES), GetDeviceCaps(dc, DESKTOPVERTRES),
           GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, HORZSIZE), GetDeviceCaps(dc, VERTSIZE));
    if (EnumDisplaySettingsA(NULL, ENUM_REGISTRY_SETTINGS, &dm)) printf("EnumDisplaySettings(registry): %lux%lu @ %lu\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    if (EnumDisplaySettingsA(NULL, 0, &dm)) printf("EnumDisplaySettings(mode 0): %lux%lu @ %lu\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    r = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm);
    printf("GetDisplayConfigBufferSizes: %ld, paths %u modes %u\n", r, np, nm);
    if (np > 8) np = 8; if (nm > 16) nm = 16;
    r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, p, &nm, m, NULL);
    printf("QueryDisplayConfig: %ld\n", r);
    for (i = 0; r == 0 && i < nm; i++)
    {
        if (m[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            printf("  source mode: %ux%u at (%ld,%ld)\n", m[i].sourceMode.width, m[i].sourceMode.height, m[i].sourceMode.position.x, m[i].sourceMode.position.y);
        else if (m[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            printf("  target mode: active %ux%u total %ux%u, vsync %u/%u\n", m[i].targetMode.targetVideoSignalInfo.activeSize.cx, m[i].targetMode.targetVideoSignalInfo.activeSize.cy,
                   m[i].targetMode.targetVideoSignalInfo.totalSize.cx, m[i].targetMode.targetVideoSignalInfo.totalSize.cy,
                   m[i].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator, m[i].targetMode.targetVideoSignalInfo.vSyncFreq.Denominator);
        else printf("  mode %u: type %d\n", i, m[i].infoType);
    }
    for (i = 0; r == 0 && i < np; i++)
    {
        DISPLAYCONFIG_TARGET_PREFERRED_MODE pm = {0}; DISPLAYCONFIG_TARGET_DEVICE_NAME tn = {0};
        pm.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE; pm.header.size = sizeof(pm);
        pm.header.adapterId = p[i].targetInfo.adapterId; pm.header.id = p[i].targetInfo.id;
        LONG rr = DisplayConfigGetDeviceInfo(&pm.header);
        printf("  path %u preferred mode: %ld -> %ux%u (active %ux%u)\n", i, rr, pm.width, pm.height,
               pm.targetMode.targetVideoSignalInfo.activeSize.cx, pm.targetMode.targetVideoSignalInfo.activeSize.cy);
        tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME; tn.header.size = sizeof(tn);
        tn.header.adapterId = p[i].targetInfo.adapterId; tn.header.id = p[i].targetInfo.id;
        rr = DisplayConfigGetDeviceInfo(&tn.header);
        printf("  path %u target name: %ld '%ls' native res flags %#x\n", i, rr, tn.monitorFriendlyDeviceName, tn.flags.value);
    }
    return 0;
}
