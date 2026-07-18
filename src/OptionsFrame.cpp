#include "OptionsFrame.h"
#include "resource.h"
#include "Version.h"
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#pragma comment(lib, "comdlg32.lib")

namespace
{
    void SetButtonColorText(HWND dlg, int id, COLORREF c)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        SetDlgItemTextW(dlg, id, buf);
    }

    void SetComboItems2(HWND dlg, int id, const wchar_t* a, const wchar_t* b, int sel)
    {
        HWND cb = GetDlgItem(dlg, id);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)a);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)b);
        SendMessageW(cb, CB_SETCURSEL, sel, 0);
    }

    double GetDlgItemFloat(HWND dlg, int id, double def)
    {
        wchar_t buf[64];
        if (!GetDlgItemTextW(dlg, id, buf, 64))
            return def;
        wchar_t* end = nullptr;
        double v = wcstod(buf, &end);
        if (end == buf)
            return def;
        return v;
    }

    void SetDlgItemFloat(HWND dlg, int id, double value)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"%.2f", value);
        SetDlgItemTextW(dlg, id, buf);
    }

    // Controls (and their standalone labels) belonging to each tab page.
    // The dialog holds all controls for all tabs at once; switching tabs
    // just shows one group and hides the rest, rather than swapping in
    // separate child dialogs.
    const int kTabSystem[] = {
        IDC_LBL_VERSION, IDC_LBL_VERSION_VALUE,
        IDC_RESET_DEFAULTS_BTN,
        IDC_DEBUGLOG_CHECK,
    };

    const int kTabScaling[] = {
        IDC_LBL_AMPSCALE, IDC_AMPSCALE_COMBO,
        IDC_LBL_DBFLOOR, IDC_DBFLOOR_EDIT,
        IDC_LBL_DBCEILING, IDC_DBCEILING_EDIT,
        IDC_AUTOGAIN_CHECK,
        IDC_LBL_BARCOUNT, IDC_BARCOUNT_EDIT,
        IDC_LBL_SEGMENTGAP, IDC_SEGMENTGAP_EDIT,
        IDC_LBL_SEGMENTHEIGHT, IDC_SEGMENTHEIGHT_EDIT,
    };

    const int kTabBars[] = {
        IDC_LBL_BARSTYLE, IDC_BARSTYLE_COMBO,
        IDC_LBL_BARCOLORMODE, IDC_BARCOLORMODE_COMBO,
        IDC_LBL_BARCOLOR, IDC_BARCOLOR_BTN,
        IDC_LBL_BARGRADTOP, IDC_BARGRADTOP_BTN,
        IDC_LBL_BARGRADBOTTOM, IDC_BARGRADBOTTOM_BTN,
        IDC_LBL_BARSPACING, IDC_BARSPACING_EDIT,
    };

    const int kTabPeaks[] = {
        IDC_PEAKMARKERS_CHECK, IDC_PEAKCOLOR_BTN,
        IDC_LBL_PEAKFALLSPEED, IDC_PEAKFALLSPEED_EDIT,
        IDC_LBL_PEAKTHICKNESS, IDC_PEAKTHICKNESS_EDIT,
        IDC_LBL_PEAKHEIGHTSEG, IDC_PEAKHEIGHTSEG_EDIT,
    };

    const int kTabBackground[] = {
        IDC_LBL_BACKSTYLE, IDC_BACKSTYLE_COMBO,
        IDC_LBL_BACKCOLOR, IDC_BACKCOLOR_BTN,
        IDC_GRIDLINES_CHECK, IDC_GRIDCOLOR_BTN,
        IDC_LBL_GRIDSPACING, IDC_GRIDSPACING_EDIT,
        IDC_LBL_GRIDOPACITY, IDC_GRIDOPACITY_EDIT,
        IDC_LBL_GRIDSTYLE, IDC_GRIDSTYLE_COMBO,
    };

    const int kTabLayout[] = {
        IDC_LBL_TOPMARGIN, IDC_TOPMARGIN_EDIT,
        IDC_LBL_CHANNELMODE, IDC_CHANNELMODE_COMBO,
        IDC_LBL_CHANNELGAP, IDC_CHANNELGAP_EDIT,
        IDC_LBL_LEFTMARGIN, IDC_LEFTMARGIN_EDIT,
        IDC_LBL_RIGHTMARGIN, IDC_RIGHTMARGIN_EDIT,
    };

    void ShowGroup(HWND dlg, const int* ids, size_t count, bool show)
    {
        for (size_t i = 0; i < count; ++i)
        {
            HWND ctrl = GetDlgItem(dlg, ids[i]);
            if (ctrl)
                ShowWindow(ctrl, show ? SW_SHOW : SW_HIDE);
        }
    }
}

HRESULT WINAPI COptionsFrame::QueryInterface(REFIID iid, void** obj)
{
    if (!obj)
        return E_POINTER;

    if (iid == IID_IUnknown || iid == IID_IAIMPOptionsDialogFrame)
    {
        *obj = static_cast<IAIMPOptionsDialogFrame*>(this);
        AddRef();
        return S_OK;
    }

    *obj = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI COptionsFrame::AddRef()
{
    return (ULONG)++m_RefCount;
}

ULONG WINAPI COptionsFrame::Release()
{
    long count = --m_RefCount;
    if (count == 0)
        delete this;
    return (ULONG)count;
}

HRESULT WINAPI COptionsFrame::GetName(IAIMPString** S)
{
    if (!S)
        return E_POINTER;
    *S = MakeAIMPString(L"AuraBars");
    return *S ? S_OK : E_FAIL;
}

HWND WINAPI COptionsFrame::CreateFrame(HWND ParentWnd)
{
    m_Working = g_Settings;
    m_hDlg = CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_OPTIONS), ParentWnd, DialogProc, (LPARAM)this);
    return m_hDlg;
}

void WINAPI COptionsFrame::DestroyFrame()
{
    if (m_hDlg)
    {
        DestroyWindow(m_hDlg);
        m_hDlg = nullptr;
    }
}

void WINAPI COptionsFrame::Notification(int ID)
{
    if (!m_hDlg)
        return;

    switch (ID)
    {
        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD:
            m_Working = g_Settings;
            PopulateControls();
            break;

        case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE:
        {
            ReadControlsIntoWorking();
            ClampSettings(m_Working);
            g_Settings = m_Working;

            if (g_Core)
            {
                IAIMPServiceConfig* cfg = nullptr;
                if (SUCCEEDED(g_Core->QueryInterface(IID_IAIMPServiceConfig, (void**)&cfg)) && cfg)
                {
                    SaveSettings(cfg, g_Settings);
                    cfg->Release();
                }
            }
            break;
        }

        default:
            break;
    }
}

INT_PTR CALLBACK COptionsFrame::DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    COptionsFrame* self = nullptr;
    if (msg == WM_INITDIALOG)
    {
        self = reinterpret_cast<COptionsFrame*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = reinterpret_cast<COptionsFrame*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);
    return FALSE;
}

INT_PTR COptionsFrame::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_INITDIALOG:
            InitTabs();
            PopulateControls();
            return TRUE;

        case WM_COMMAND:
            HandleCommand(wParam, lParam);
            return TRUE;

        case WM_WINDOWPOSCHANGED:
        {
            // AIMP calls CreateFrame() and gets back an HWND that is NOT
            // yet inserted/visible in its settings-page container - it
            // does that itself sometime *after* CreateFrame() returns, so
            // WM_INITDIALOG (which runs synchronously inside
            // CreateDialogParamW, before CreateFrame() even returns) fires
            // on a window nobody can see yet. Re-asserting the active tab
            // here, at the moment the window is actually revealed
            // (SWP_SHOWWINDOW), is what makes the very first reveal
            // correct instead of relying on a stale paint from creation
            // time that a later tab click happened to correct.
            const WINDOWPOS* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
            if (wp && (wp->flags & SWP_SHOWWINDOW))
                SwitchToTab(m_ActiveTab);
            return FALSE;
        }

        default:
            return FALSE;
    }
}

void COptionsFrame::InitTabs()
{
    CheckRadioButton(m_hDlg, IDC_TAB_BTN0, IDC_TAB_BTN5, IDC_TAB_BTN0);
    SwitchToTab(0);
}

void COptionsFrame::SwitchToTab(int index)
{
    m_ActiveTab = index;

    ShowGroup(m_hDlg, kTabSystem, _countof(kTabSystem), index == 0);
    ShowGroup(m_hDlg, kTabScaling, _countof(kTabScaling), index == 1);
    ShowGroup(m_hDlg, kTabBars, _countof(kTabBars), index == 2);
    ShowGroup(m_hDlg, kTabPeaks, _countof(kTabPeaks), index == 3);
    ShowGroup(m_hDlg, kTabBackground, _countof(kTabBackground), index == 4);
    ShowGroup(m_hDlg, kTabLayout, _countof(kTabLayout), index == 5);

    // ShowWindow() only schedules invalidation; it doesn't force an
    // immediate repaint. Forcing one here means every call site - initial
    // setup, a real tab click, or the WM_WINDOWPOSCHANGED re-assertion
    // above - always leaves the correct single-tab state actually painted,
    // not just logically set.
    InvalidateRect(m_hDlg, nullptr, TRUE);
    UpdateWindow(m_hDlg);
}

void COptionsFrame::HandleCommand(WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);

    switch (id)
    {
        case IDC_TAB_BTN0:
        case IDC_TAB_BTN1:
        case IDC_TAB_BTN2:
        case IDC_TAB_BTN3:
        case IDC_TAB_BTN4:
        case IDC_TAB_BTN5:
            if (code == BN_CLICKED)
                SwitchToTab(id - IDC_TAB_BTN0);
            return;

        case IDC_RESET_DEFAULTS_BTN:
            if (code == BN_CLICKED)
                ResetToDefaults();
            return;

        case IDC_BARCOLOR_BTN:
            PickColor(IDC_BARCOLOR_BTN, m_Working.barColorSolid);
            return;
        case IDC_BARGRADTOP_BTN:
            PickColor(IDC_BARGRADTOP_BTN, m_Working.barGradientTop);
            return;
        case IDC_BARGRADBOTTOM_BTN:
            PickColor(IDC_BARGRADBOTTOM_BTN, m_Working.barGradientBottom);
            return;
        case IDC_BACKCOLOR_BTN:
            PickColor(IDC_BACKCOLOR_BTN, m_Working.backgroundColor);
            return;
        case IDC_GRIDCOLOR_BTN:
            PickColor(IDC_GRIDCOLOR_BTN, m_Working.gridLineColor);
            return;
        case IDC_PEAKCOLOR_BTN:
            PickColor(IDC_PEAKCOLOR_BTN, m_Working.peakMarkerColor);
            return;

        case IDC_BARSTYLE_COMBO:
        case IDC_BARCOLORMODE_COMBO:
        case IDC_AMPSCALE_COMBO:
            if (code == CBN_SELCHANGE)
            {
                UpdateEnabledStates();
                NotifyModified();
            }
            return;

        case IDC_AUTOGAIN_CHECK:
            if (code == BN_CLICKED)
            {
                UpdateEnabledStates();
                NotifyModified();
            }
            return;

        case IDC_BACKSTYLE_COMBO:
        case IDC_GRIDSTYLE_COMBO:
            if (code == CBN_SELCHANGE)
                NotifyModified();
            return;

        case IDC_CHANNELMODE_COMBO:
            if (code == CBN_SELCHANGE)
            {
                UpdateEnabledStates();
                NotifyModified();
            }
            return;

        case IDC_GRIDLINES_CHECK:
        case IDC_PEAKMARKERS_CHECK:
        case IDC_DEBUGLOG_CHECK:
            if (code == BN_CLICKED)
                NotifyModified();
            return;

        case IDC_BARCOUNT_EDIT:
        case IDC_DBFLOOR_EDIT:
        case IDC_DBCEILING_EDIT:
        case IDC_PEAKFALLSPEED_EDIT:
        case IDC_PEAKTHICKNESS_EDIT:
        case IDC_PEAKHEIGHTSEG_EDIT:
        case IDC_BARSPACING_EDIT:
        case IDC_TOPMARGIN_EDIT:
        case IDC_SEGMENTGAP_EDIT:
        case IDC_SEGMENTHEIGHT_EDIT:
        case IDC_GRIDSPACING_EDIT:
        case IDC_GRIDOPACITY_EDIT:
        case IDC_CHANNELGAP_EDIT:
        case IDC_LEFTMARGIN_EDIT:
        case IDC_RIGHTMARGIN_EDIT:
            if (code == EN_CHANGE)
                NotifyModified();
            return;

        default:
            return;
    }
}

void COptionsFrame::PickColor(int buttonId, COLORREF& target)
{
    static COLORREF customColors[16] = { 0 };

    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = m_hDlg;
    cc.rgbResult = target;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc))
    {
        target = cc.rgbResult;
        SetButtonColorText(m_hDlg, buttonId, target);
        NotifyModified();
    }
}

void COptionsFrame::PopulateControls()
{
    SetDlgItemTextW(m_hDlg, IDC_LBL_VERSION_VALUE, AURABARS_FULL_VERSION_WSTR);

    SetDlgItemInt(m_hDlg, IDC_BARCOUNT_EDIT, m_Working.barCount, FALSE);

    SetComboItems2(m_hDlg, IDC_AMPSCALE_COMBO, L"Linear", L"Logarithmic (dB)", (int)m_Working.amplitudeScaling);
    SetDlgItemFloat(m_hDlg, IDC_DBFLOOR_EDIT, m_Working.dbFloor);
    SetDlgItemFloat(m_hDlg, IDC_DBCEILING_EDIT, m_Working.dbCeiling);
    CheckDlgButton(m_hDlg, IDC_AUTOGAIN_CHECK, m_Working.autoGainCeiling ? BST_CHECKED : BST_UNCHECKED);

    SetComboItems2(m_hDlg, IDC_BARSTYLE_COMBO, L"LED", L"Smooth", (int)m_Working.barStyle);
    SetComboItems2(m_hDlg, IDC_BARCOLORMODE_COMBO, L"Solid", L"Gradient", (int)m_Working.barColorMode);
    SetComboItems2(m_hDlg, IDC_BACKSTYLE_COMBO, L"Flat", L"Gradient", (int)m_Working.backgroundStyle);
    SetComboItems2(m_hDlg, IDC_GRIDSTYLE_COMBO, L"Dashed", L"Solid", (int)m_Working.gridLineStyle);
    SetComboItems2(m_hDlg, IDC_CHANNELMODE_COMBO, L"Mono", L"Stereo", (int)m_Working.channelMode);

    SetButtonColorText(m_hDlg, IDC_BARCOLOR_BTN, m_Working.barColorSolid);
    SetButtonColorText(m_hDlg, IDC_BARGRADTOP_BTN, m_Working.barGradientTop);
    SetButtonColorText(m_hDlg, IDC_BARGRADBOTTOM_BTN, m_Working.barGradientBottom);
    SetButtonColorText(m_hDlg, IDC_BACKCOLOR_BTN, m_Working.backgroundColor);
    SetButtonColorText(m_hDlg, IDC_GRIDCOLOR_BTN, m_Working.gridLineColor);
    SetButtonColorText(m_hDlg, IDC_PEAKCOLOR_BTN, m_Working.peakMarkerColor);

    CheckDlgButton(m_hDlg, IDC_GRIDLINES_CHECK, m_Working.gridLines ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(m_hDlg, IDC_GRIDSPACING_EDIT, m_Working.gridLineSpacing, FALSE);
    SetDlgItemInt(m_hDlg, IDC_GRIDOPACITY_EDIT, m_Working.gridLineOpacity, FALSE);
    CheckDlgButton(m_hDlg, IDC_PEAKMARKERS_CHECK, m_Working.peakMarkers ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hDlg, IDC_DEBUGLOG_CHECK, m_Working.debugLogging ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemFloat(m_hDlg, IDC_PEAKFALLSPEED_EDIT, m_Working.peakFallSpeed);
    SetDlgItemInt(m_hDlg, IDC_PEAKTHICKNESS_EDIT, m_Working.peakMarkerThickness, FALSE);
    SetDlgItemInt(m_hDlg, IDC_PEAKHEIGHTSEG_EDIT, m_Working.peakMarkerHeightSegments, FALSE);
    SetDlgItemInt(m_hDlg, IDC_BARSPACING_EDIT, m_Working.barSpacing, FALSE);
    SetDlgItemInt(m_hDlg, IDC_TOPMARGIN_EDIT, m_Working.topMarginPercent, FALSE);
    SetDlgItemInt(m_hDlg, IDC_SEGMENTGAP_EDIT, m_Working.segmentGap, FALSE);
    SetDlgItemInt(m_hDlg, IDC_SEGMENTHEIGHT_EDIT, m_Working.segmentHeight, FALSE);
    SetDlgItemInt(m_hDlg, IDC_CHANNELGAP_EDIT, m_Working.channelGap, FALSE);
    SetDlgItemInt(m_hDlg, IDC_LEFTMARGIN_EDIT, m_Working.leftMargin, FALSE);
    SetDlgItemInt(m_hDlg, IDC_RIGHTMARGIN_EDIT, m_Working.rightMargin, FALSE);

    UpdateEnabledStates();
}

void COptionsFrame::ReadControlsIntoWorking()
{
    m_Working.barCount = GetDlgItemInt(m_hDlg, IDC_BARCOUNT_EDIT, nullptr, FALSE);
    m_Working.amplitudeScaling = (AmplitudeScaling)SendMessageW(GetDlgItem(m_hDlg, IDC_AMPSCALE_COMBO), CB_GETCURSEL, 0, 0);
    m_Working.dbFloor = GetDlgItemFloat(m_hDlg, IDC_DBFLOOR_EDIT, m_Working.dbFloor);
    m_Working.dbCeiling = GetDlgItemFloat(m_hDlg, IDC_DBCEILING_EDIT, m_Working.dbCeiling);
    m_Working.autoGainCeiling = IsDlgButtonChecked(m_hDlg, IDC_AUTOGAIN_CHECK) == BST_CHECKED;
    m_Working.barStyle = (BarStyle)SendMessageW(GetDlgItem(m_hDlg, IDC_BARSTYLE_COMBO), CB_GETCURSEL, 0, 0);
    m_Working.barColorMode = (ColorMode)SendMessageW(GetDlgItem(m_hDlg, IDC_BARCOLORMODE_COMBO), CB_GETCURSEL, 0, 0);
    m_Working.backgroundStyle = (BackgroundStyle)SendMessageW(GetDlgItem(m_hDlg, IDC_BACKSTYLE_COMBO), CB_GETCURSEL, 0, 0);
    m_Working.gridLineStyle = (GridLineStyle)SendMessageW(GetDlgItem(m_hDlg, IDC_GRIDSTYLE_COMBO), CB_GETCURSEL, 0, 0);
    m_Working.channelMode = (ChannelMode)SendMessageW(GetDlgItem(m_hDlg, IDC_CHANNELMODE_COMBO), CB_GETCURSEL, 0, 0);

    m_Working.gridLines = IsDlgButtonChecked(m_hDlg, IDC_GRIDLINES_CHECK) == BST_CHECKED;
    m_Working.peakMarkers = IsDlgButtonChecked(m_hDlg, IDC_PEAKMARKERS_CHECK) == BST_CHECKED;
    m_Working.debugLogging = IsDlgButtonChecked(m_hDlg, IDC_DEBUGLOG_CHECK) == BST_CHECKED;

    m_Working.peakFallSpeed = GetDlgItemFloat(m_hDlg, IDC_PEAKFALLSPEED_EDIT, m_Working.peakFallSpeed);
    m_Working.peakMarkerThickness = GetDlgItemInt(m_hDlg, IDC_PEAKTHICKNESS_EDIT, nullptr, FALSE);
    m_Working.peakMarkerHeightSegments = GetDlgItemInt(m_hDlg, IDC_PEAKHEIGHTSEG_EDIT, nullptr, FALSE);
    m_Working.barSpacing = GetDlgItemInt(m_hDlg, IDC_BARSPACING_EDIT, nullptr, FALSE);
    m_Working.topMarginPercent = GetDlgItemInt(m_hDlg, IDC_TOPMARGIN_EDIT, nullptr, FALSE);
    m_Working.segmentGap = GetDlgItemInt(m_hDlg, IDC_SEGMENTGAP_EDIT, nullptr, FALSE);
    m_Working.segmentHeight = GetDlgItemInt(m_hDlg, IDC_SEGMENTHEIGHT_EDIT, nullptr, FALSE);
    m_Working.gridLineSpacing = GetDlgItemInt(m_hDlg, IDC_GRIDSPACING_EDIT, nullptr, FALSE);
    m_Working.gridLineOpacity = GetDlgItemInt(m_hDlg, IDC_GRIDOPACITY_EDIT, nullptr, FALSE);
    m_Working.channelGap = GetDlgItemInt(m_hDlg, IDC_CHANNELGAP_EDIT, nullptr, FALSE);
    m_Working.leftMargin = GetDlgItemInt(m_hDlg, IDC_LEFTMARGIN_EDIT, nullptr, FALSE);
    m_Working.rightMargin = GetDlgItemInt(m_hDlg, IDC_RIGHTMARGIN_EDIT, nullptr, FALSE);

    // Colors are already current in m_Working; PickColor() updates it live.
}

void COptionsFrame::UpdateEnabledStates()
{
    bool isLed = SendMessageW(GetDlgItem(m_hDlg, IDC_BARSTYLE_COMBO), CB_GETCURSEL, 0, 0) == 0;
    bool isGradient = SendMessageW(GetDlgItem(m_hDlg, IDC_BARCOLORMODE_COMBO), CB_GETCURSEL, 0, 0) == 1;
    bool isLogarithmic = SendMessageW(GetDlgItem(m_hDlg, IDC_AMPSCALE_COMBO), CB_GETCURSEL, 0, 0) == 1;
    bool autoGain = IsDlgButtonChecked(m_hDlg, IDC_AUTOGAIN_CHECK) == BST_CHECKED;
    bool isStereo = SendMessageW(GetDlgItem(m_hDlg, IDC_CHANNELMODE_COMBO), CB_GETCURSEL, 0, 0) == 1;

    EnableWindow(GetDlgItem(m_hDlg, IDC_DBFLOOR_EDIT), isLogarithmic);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_DBFLOOR), isLogarithmic);
    EnableWindow(GetDlgItem(m_hDlg, IDC_DBCEILING_EDIT), isLogarithmic && !autoGain);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_DBCEILING), isLogarithmic && !autoGain);
    EnableWindow(GetDlgItem(m_hDlg, IDC_AUTOGAIN_CHECK), isLogarithmic);

    EnableWindow(GetDlgItem(m_hDlg, IDC_BARCOLOR_BTN), !isGradient);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_BARCOLOR), !isGradient);
    EnableWindow(GetDlgItem(m_hDlg, IDC_BARGRADTOP_BTN), isGradient);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_BARGRADTOP), isGradient);
    EnableWindow(GetDlgItem(m_hDlg, IDC_BARGRADBOTTOM_BTN), isGradient);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_BARGRADBOTTOM), isGradient);

    EnableWindow(GetDlgItem(m_hDlg, IDC_PEAKTHICKNESS_EDIT), !isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_PEAKTHICKNESS), !isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_PEAKHEIGHTSEG_EDIT), isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_PEAKHEIGHTSEG), isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_SEGMENTGAP_EDIT), isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_SEGMENTGAP), isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_SEGMENTHEIGHT_EDIT), isLed);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_SEGMENTHEIGHT), isLed);

    EnableWindow(GetDlgItem(m_hDlg, IDC_CHANNELGAP_EDIT), isStereo);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LBL_CHANNELGAP), isStereo);
}

void COptionsFrame::ResetToDefaults()
{
    // Resets only the in-dialog working copy, same as every other edit
    // here - the user still has to Apply/OK to persist it, and Cancel
    // still backs out cleanly.
    m_Working = AuraBarsSettings{};
    PopulateControls();
    NotifyModified();
}

void COptionsFrame::NotifyModified()
{
    if (!g_Core)
        return;

    IAIMPServiceOptionsDialog* svc = nullptr;
    if (SUCCEEDED(g_Core->QueryInterface(IID_IAIMPServiceOptionsDialog, (void**)&svc)) && svc)
    {
        svc->FrameModified(this);
        svc->Release();
    }
}
