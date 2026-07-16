#include "Plugin.h"
#include <commctrl.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
        {
            g_hInstance = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);

            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
            InitCommonControlsEx(&icc);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** Header)
{
    if (!Header)
        return E_POINTER;

    *Header = new CPlugin();
    return S_OK;
}
