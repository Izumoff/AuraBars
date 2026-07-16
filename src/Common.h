#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602 // needed for GradientFill/TRIVERTEX in wingdi.h
#endif
#include <windows.h>
#include <wingdi.h>
#pragma comment(lib, "msimg32.lib")
#include <commctrl.h>

#include "apiCore.h"
#include "apiObjects.h"
#include "apiPlugin.h"
#include "apiVisuals.h"
#include "apiOptions.h"
#include "apiTypes.h"

// Set once by CPlugin::Initialize, used by helpers that need to reach the
// core (creating IAIMPString instances, resolving services) from anywhere
// in the plugin without threading a pointer through every call site.
extern IAIMPCore* g_Core;

// Set once in DllMain (DLL_PROCESS_ATTACH); needed to load the options
// dialog template out of this DLL's own resources.
extern HINSTANCE g_hInstance;

// Owning wrapper around an IAIMPString*: releases on destruction, no copy.
class AutoAIMPString
{
public:
    AutoAIMPString() = default;
    explicit AutoAIMPString(IAIMPString* s) : m_Str(s) {}
    AutoAIMPString(const AutoAIMPString&) = delete;
    AutoAIMPString& operator=(const AutoAIMPString&) = delete;
    ~AutoAIMPString() { if (m_Str) m_Str->Release(); }

    IAIMPString* Get() const { return m_Str; }
    IAIMPString** AddressOf() { return &m_Str; }

private:
    IAIMPString* m_Str = nullptr;
};

// Creates a new IAIMPString from a wide string via the core object factory.
// Returns an AddRef'd pointer (caller owns it) or nullptr on failure.
IAIMPString* MakeAIMPString(const wchar_t* text);
