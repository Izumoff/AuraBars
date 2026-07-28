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
#include "apiGUI.h"
#include "apiSkins.h"
#include <atomic>

// Set once by CPlugin::Initialize, used by helpers that need to reach the
// core (creating IAIMPString instances, resolving services) from anywhere
// in the plugin without threading a pointer through every call site.
extern IAIMPCore* g_Core;

// Set once in DllMain (DLL_PROCESS_ATTACH); needed to load the options
// dialog template out of this DLL's own resources.
extern HINSTANCE g_hInstance;

// Cached display size from the visualization's Initialize/Resize calls -
// consumed only by the "[Environment]" clipboard diagnostics block (see
// OptionsFrame.cpp's BuildEnvironmentSection). The options dialog can be
// opened while no visualization instance is running, and its render path
// is documented by the SDK as not thread-safe / not provably the same
// thread as the dialog's UI thread, so these are atomics rather than plain
// ints. -1 means "never initialized this session" - a diagnostic field
// must read as "n/a", never as a fake 0x0 measurement.
extern std::atomic<int> g_EnvDisplayWidth;
extern std::atomic<int> g_EnvDisplayHeight;

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

// Appends one timestamped line to %TEMP%\AuraBars_debug.log. Best-effort: a
// failed open (e.g. locked file) just drops the sample, never crashes the
// caller. Shared by Visualization.cpp (per-frame sample logging) and
// OptionsFrame.cpp (clipboard-copy failure logging).
void AppendDebugLog(const wchar_t* line);
