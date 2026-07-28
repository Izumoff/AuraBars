#include "Common.h"
#include <cstdio>
#include <ctime>

IAIMPCore* g_Core = nullptr;
HINSTANCE g_hInstance = nullptr;
std::atomic<int> g_EnvDisplayWidth{-1};
std::atomic<int> g_EnvDisplayHeight{-1};

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

void AppendDebugLog(const wchar_t* line)
{
    wchar_t path[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, path);
    if (len == 0 || len >= MAX_PATH - 20)
        return;
    wcscat_s(path, L"AuraBars_debug.log");

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f)
        return;

    time_t t = time(nullptr);
    tm local;
    localtime_s(&local, &t);
    wchar_t timestamp[32];
    wcsftime(timestamp, 32, L"%Y-%m-%d %H:%M:%S", &local);

    fwprintf(f, L"[%s] %s\n", timestamp, line);
    fclose(f);
}
