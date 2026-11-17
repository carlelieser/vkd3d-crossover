/* inputprobe: what a game sees as controllers inside a Wine bottle.
 * Prints XInput slots (1.4 and 9.1.0), DirectInput 8 game controllers and raw HID game devices.
 * Build: x86_64-w64-mingw32-gcc -O2 -o inputprobe.exe inputprobe.c -lxinput -ldinput8 -ldxguid -luser32 -lole32
 */
#define INITGUID
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdio.h>
#include <xinput.h>
#include <dinput.h>

static int di_count;
static BOOL CALLBACK di_cb(const DIDEVICEINSTANCEA *d, void *ctx)
{
    (void)ctx;
    di_count++;
    printf("  DInput: \"%s\" type 0x%lx vid/pid %04lx:%04lx\n", d->tszProductName, (unsigned long)d->dwDevType,
           (unsigned long)(d->guidProduct.Data1 & 0xffff), (unsigned long)(d->guidProduct.Data1 >> 16));
    return DIENUM_CONTINUE;
}

static void xinput_slots(const char *dll)
{
    typedef DWORD (WINAPI *get_state_t)(DWORD, XINPUT_STATE *);
    HMODULE m = LoadLibraryA(dll);
    get_state_t get = m ? (get_state_t)GetProcAddress(m, "XInputGetState") : NULL;
    DWORD i;
    if (!get) { printf("%s: not loadable\n", dll); return; }
    for (i = 0; i < 4; i++)
    {
        XINPUT_STATE s;
        DWORD r = get(i, &s);
        printf("%s slot %lu: %s", dll, (unsigned long)i, r == ERROR_SUCCESS ? "CONNECTED" : "empty");
        if (r == ERROR_SUCCESS) printf(" (buttons 0x%04x, LX %d)", s.Gamepad.wButtons, s.Gamepad.sThumbLX);
        printf("\n");
    }
}

int main(void)
{
    IDirectInput8A *di;
    UINT n = 0, i;
    RAWINPUTDEVICELIST *list;

    xinput_slots("xinput1_4.dll");
    xinput_slots("xinput9_1_0.dll");
    xinput_slots("xinput1_3.dll");

    if (SUCCEEDED(DirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&di, NULL)))
    {
        IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, di_cb, NULL, DIEDFL_ATTACHEDONLY);
        printf("DInput game controllers: %d\n", di_count);
        IDirectInput8_Release(di);
    }

    GetRawInputDeviceList(NULL, &n, sizeof(*list));
    list = calloc(n ? n : 1, sizeof(*list));
    GetRawInputDeviceList(list, &n, sizeof(*list));
    for (i = 0; i < n; i++)
    {
        RID_DEVICE_INFO info = { sizeof(info) };
        UINT sz = sizeof(info);
        char name[256] = ""; UINT nsz = sizeof(name);
        if (list[i].dwType != RIM_TYPEHID) continue;
        GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &info, &sz);
        GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, name, &nsz);
        printf("RawHID: usage %04x:%04x vid/pid %04lx:%04lx %s\n", info.hid.usUsagePage, info.hid.usUsage,
               (unsigned long)info.hid.dwVendorId, (unsigned long)info.hid.dwProductId, name);
    }
    return 0;
}
