#pragma once

#include "Common.h"
#include "Settings.h"
#include <atomic>

// IAIMPOptionsDialogFrame implementation: hosts a plain Win32 dialog
// template (IDD_OPTIONS) as a child window inside AIMP's options dialog.
// Edits happen against a local working copy so Cancel (no SAVE
// notification) discards them; OK/Apply commits into g_Settings and
// persists via IAIMPServiceConfig.
class COptionsFrame : public IAIMPOptionsDialogFrame
{
public:
    COptionsFrame() = default;

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID iid, void** obj) override;
    ULONG WINAPI AddRef() override;
    ULONG WINAPI Release() override;

    // IAIMPOptionsDialogFrame
    HRESULT WINAPI GetName(IAIMPString** S) override;
    HWND WINAPI CreateFrame(HWND ParentWnd) override;
    void WINAPI DestroyFrame() override;
    void WINAPI Notification(int ID) override;

private:
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void HandleCommand(WPARAM wParam, LPARAM lParam);

    void PopulateControls();
    void ReadControlsIntoWorking();
    void UpdateEnabledStates();
    void PickColor(int buttonId, COLORREF& target);
    void NotifyModified();
    void ResetToDefaults();

    void InitTabs();
    void SwitchToTab(int index);

    std::atomic<long> m_RefCount{1};
    HWND m_hDlg = nullptr;
    AuraBarsSettings m_Working;
    int m_ActiveTab = 0;
};
