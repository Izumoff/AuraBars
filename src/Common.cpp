#include "Common.h"

IAIMPCore* g_Core = nullptr;
HINSTANCE g_hInstance = nullptr;

IAIMPString* MakeAIMPString(const wchar_t* text)
{
    if (!g_Core || !text)
        return nullptr;

    IAIMPString* str = nullptr;
    if (FAILED(g_Core->CreateObject(IID_IAIMPString, (void**)&str)) || !str)
        return nullptr;

    str->SetData(const_cast<wchar_t*>(text), (int)wcslen(text));
    return str;
}
