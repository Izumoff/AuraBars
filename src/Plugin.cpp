#include "Plugin.h"
#include "Settings.h"
#include "Visualization.h"
#include "OptionsFrame.h"

HRESULT WINAPI CPlugin::QueryInterface(REFIID iid, void** obj)
{
    if (!obj)
        return E_POINTER;

    if (iid == IID_IUnknown)
    {
        *obj = static_cast<IAIMPPlugin*>(this);
        AddRef();
        return S_OK;
    }

    *obj = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI CPlugin::AddRef()
{
    return (ULONG)++m_RefCount;
}

ULONG WINAPI CPlugin::Release()
{
    long count = --m_RefCount;
    if (count == 0)
        delete this;
    return (ULONG)count;
}

TChar* WINAPI CPlugin::InfoGet(int Index)
{
    switch (Index)
    {
        case AIMP_PLUGIN_INFO_NAME:
            return const_cast<TChar*>(L"AuraBars");
        case AIMP_PLUGIN_INFO_AUTHOR:
            return const_cast<TChar*>(L"AuraBars");
        case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION:
            return const_cast<TChar*>(L"LED/Smooth spectrum bar visualization");
        default:
            return const_cast<TChar*>(L"");
    }
}

LongWord WINAPI CPlugin::InfoGetCategories()
{
    return AIMP_PLUGIN_CATEGORY_VISUALS;
}

HRESULT WINAPI CPlugin::Initialize(IAIMPCore* Core)
{
    if (!Core)
        return E_POINTER;

    m_Core = Core;
    m_Core->AddRef();
    g_Core = Core;

    IAIMPServiceConfig* cfg = nullptr;
    if (SUCCEEDED(Core->QueryInterface(IID_IAIMPServiceConfig, (void**)&cfg)) && cfg)
    {
        LoadSettings(cfg, g_Settings);
        cfg->Release();
    }

    // RegisterExtension AddRefs on success, so drop our local reference
    // right after registering.
    CVisualization* visualization = new CVisualization();
    Core->RegisterExtension(IID_IAIMPServiceVisualizations, visualization);
    visualization->Release();

    COptionsFrame* optionsFrame = new COptionsFrame();
    // COptionsFrame implements two IUnknown-derived interfaces
    // (IAIMPOptionsDialogFrame and IAIMPUIChangeEvents), so an implicit
    // upcast straight to IUnknown* is ambiguous - upcast through one
    // specific interface first.
    Core->RegisterExtension(IID_IAIMPServiceOptionsDialog, static_cast<IAIMPOptionsDialogFrame*>(optionsFrame));
    optionsFrame->Release();

    return S_OK;
}

HRESULT WINAPI CPlugin::Finalize()
{
    if (m_Core)
    {
        m_Core->Release();
        m_Core = nullptr;
    }
    g_Core = nullptr;
    return S_OK;
}

void WINAPI CPlugin::SystemNotification(int /*NotifyID*/, IUnknown* /*Data*/)
{
    // no system notifications handled
}
