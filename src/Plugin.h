#pragma once

#include "Common.h"
#include <atomic>

// IAIMPPlugin implementation: the DLL's single root object. AIMP obtains
// it via the exported AIMPPluginGetHeader function (not QueryInterface),
// so this class only needs to answer for IUnknown itself.
class CPlugin : public IAIMPPlugin
{
public:
    CPlugin() = default;

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID iid, void** obj) override;
    ULONG WINAPI AddRef() override;
    ULONG WINAPI Release() override;

    // IAIMPPlugin
    TChar* WINAPI InfoGet(int Index) override;
    LongWord WINAPI InfoGetCategories() override;
    HRESULT WINAPI Initialize(IAIMPCore* Core) override;
    HRESULT WINAPI Finalize() override;
    void WINAPI SystemNotification(int NotifyID, IUnknown* Data) override;

private:
    std::atomic<long> m_RefCount{1};
    IAIMPCore* m_Core = nullptr;
};
