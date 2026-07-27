#include "OptionsFrame.h"
#include "Version.h"
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <unordered_map>

#pragma comment(lib, "comdlg32.lib")

namespace
{
    // --- small property-list accessors shared by every control type ---

    void SetEditText(IAIMPUIEdit* edit, const wchar_t* text)
    {
        if (!edit)
            return;
        AutoAIMPString s(MakeAIMPString(text));
        if (s.Get())
            edit->SetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, s.Get());
    }

    void SetEditInt(IAIMPUIEdit* edit, int value)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", value);
        SetEditText(edit, buf);
    }

    void SetEditFloat(IAIMPUIEdit* edit, double value)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.2f", value);
        SetEditText(edit, buf);
    }

    int GetEditInt(IAIMPUIEdit* edit, int def)
    {
        if (!edit)
            return def;
        IAIMPString* s = nullptr;
        if (FAILED(edit->GetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, IID_IAIMPString, (void**)&s)) || !s)
            return def;
        wchar_t* end = nullptr;
        long v = wcstol(s->GetData(), &end, 10);
        bool used = (end != s->GetData());
        s->Release();
        return used ? (int)v : def;
    }

    double GetEditFloat(IAIMPUIEdit* edit, double def)
    {
        if (!edit)
            return def;
        IAIMPString* s = nullptr;
        if (FAILED(edit->GetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, IID_IAIMPString, (void**)&s)) || !s)
            return def;
        wchar_t* end = nullptr;
        double v = wcstod(s->GetData(), &end);
        bool used = (end != s->GetData());
        s->Release();
        return used ? v : def;
    }

    int GetComboIndex(IAIMPUIComboBox* combo, int def)
    {
        if (!combo)
            return def;
        int v = def;
        combo->GetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, &v);
        return v;
    }

    void SetComboIndex(IAIMPUIComboBox* combo, int index)
    {
        if (combo)
            combo->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, index);
    }

    bool GetCheckState(IAIMPUICheckBox* cb)
    {
        if (!cb)
            return false;
        int v = AIMPUI_CHECKSTATE_UNCHECKED;
        cb->GetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, &v);
        return v == AIMPUI_CHECKSTATE_CHECKED;
    }

    void SetCheckState(IAIMPUICheckBox* cb, bool checked)
    {
        if (cb)
            cb->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, checked ? AIMPUI_CHECKSTATE_CHECKED : AIMPUI_CHECKSTATE_UNCHECKED);
    }

    void SetEnabled(IAIMPUIControl* control, bool enabled)
    {
        if (control)
            control->SetValueAsInt32(AIMPUI_CONTROL_PROPID_ENABLED, enabled ? 1 : 0);
    }

    // Shared row grid: every label lives at a fixed x/width regardless of
    // its text, so every field in a tab (grouped or not) lines up on one
    // column, matching the reference Podcasts settings page.
    constexpr int kFieldX = 205;
    constexpr int kFieldW = 90;
    constexpr int kRowH = 26;           // fits a full-height button without crowding the next row
    constexpr int kFieldH = 16;         // edits/combos (buttons are taller - see kButtonH)
    constexpr int kButtonH = 20;
    constexpr int kGroupContentTop = 20; // clears the group box caption
    constexpr int kGroupContentBottom = 10;

    // --- Clipboard snapshot formatting helpers (§3 payload format) ---

    const wchar_t* NameOf(BarStyle s) { return s == BarStyle::LED ? L"LED" : L"Smooth"; }
    const wchar_t* NameOf(ColorMode m) { return m == ColorMode::Solid ? L"Solid" : L"Gradient"; }
    const wchar_t* NameOf(BackgroundStyle s) { return s == BackgroundStyle::Flat ? L"Flat" : L"Gradient"; }
    const wchar_t* NameOf(AmplitudeScaling s) { return s == AmplitudeScaling::Linear ? L"Linear" : L"Logarithmic"; }
    const wchar_t* NameOf(GridLineStyle s) { return s == GridLineStyle::Dashed ? L"Dashed" : L"Solid"; }
    const wchar_t* NameOf(ChannelMode m) { return m == ChannelMode::Mono ? L"Mono" : L"Stereo"; }
    const wchar_t* NameOf(bool b) { return b ? L"true" : L"false"; }

    std::wstring ColorHex(COLORREF c)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        return buf;
    }

    std::wstring Float1(double v)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", v);
        return buf;
    }

    // Window property name used to recover the COptionsFrame* from inside
    // the static clipboard-timer callback (see ClipboardTimerProc). Copy and
    // Paste each get their own timer ID so both buttons can be mid-revert
    // simultaneously without one click cancelling the other's countdown.
    const wchar_t* kClipboardFramePropName = L"AuraBarsOptionsFrame";
    constexpr UINT_PTR kClipboardTimerId = 1;
    constexpr UINT_PTR kPasteTimerId = 2;

    // --- Clipboard payload parsing helpers (§3 "Paste settings from
    // clipboard") - deliberately lenient on input, mirroring the formatting
    // rules the serializer above follows on output. ---

    std::wstring Trim(const std::wstring& s)
    {
        size_t b = s.find_first_not_of(L" \t\r\n");
        if (b == std::wstring::npos)
            return L"";
        size_t e = s.find_last_not_of(L" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::wstring ToLowerCopy(std::wstring s)
    {
        for (wchar_t& c : s)
            c = (wchar_t)towlower(c);
        return s;
    }

    bool IEquals(const std::wstring& a, const wchar_t* b)
    {
        size_t n = wcslen(b);
        if (a.size() != n)
            return false;
        for (size_t i = 0; i < n; ++i)
        {
            if (towlower(a[i]) != towlower(b[i]))
                return false;
        }
        return true;
    }

    bool TryParseBool(const std::wstring& v, bool& out)
    {
        if (IEquals(v, L"true") || v == L"1") { out = true; return true; }
        if (IEquals(v, L"false") || v == L"0") { out = false; return true; }
        return false;
    }

    // Requires the entire (already-trimmed) string to be consumed - "12abc"
    // must fail, not silently parse as 12.
    bool TryParseInt(const std::wstring& v, int& out)
    {
        if (v.empty())
            return false;
        wchar_t* end = nullptr;
        long value = wcstol(v.c_str(), &end, 10);
        if (end != v.c_str() + v.size())
            return false;
        out = (int)value;
        return true;
    }

    bool TryParseFloat(const std::wstring& v, double& out)
    {
        if (v.empty())
            return false;
        wchar_t* end = nullptr;
        double value = wcstod(v.c_str(), &end);
        if (end != v.c_str() + v.size())
            return false;
        out = value;
        return true;
    }

    // Accepts #RRGGBB / #AARRGGBB (leading # optional, case-insensitive).
    // The serializer never emits an alpha byte and nothing in this codebase
    // stores one either, so an alpha byte on input is accepted and discarded
    // rather than rejected.
    bool TryParseColor(std::wstring v, COLORREF& out)
    {
        if (!v.empty() && v[0] == L'#')
            v = v.substr(1);
        if (v.size() != 6 && v.size() != 8)
            return false;
        for (wchar_t c : v)
        {
            if (!iswxdigit(c))
                return false;
        }
        if (v.size() == 8)
            v = v.substr(2);
        unsigned long value = wcstoul(v.c_str(), nullptr, 16);
        BYTE r = (BYTE)((value >> 16) & 0xFF);
        BYTE g = (BYTE)((value >> 8) & 0xFF);
        BYTE b = (BYTE)(value & 0xFF);
        out = RGB(r, g, b);
        return true;
    }

    bool ApplyBool(const std::wstring& v, bool& field) { return TryParseBool(v, field); }
    bool ApplyInt(const std::wstring& v, int& field) { return TryParseInt(v, field); }
    bool ApplyFloat(const std::wstring& v, double& field) { return TryParseFloat(v, field); }
    bool ApplyColor(const std::wstring& v, COLORREF& field) { return TryParseColor(v, field); }

    bool ApplyBarStyle(const std::wstring& v, BarStyle& field)
    {
        if (IEquals(v, L"LED")) { field = BarStyle::LED; return true; }
        if (IEquals(v, L"Smooth")) { field = BarStyle::Smooth; return true; }
        return false;
    }
    bool ApplyColorMode(const std::wstring& v, ColorMode& field)
    {
        if (IEquals(v, L"Solid")) { field = ColorMode::Solid; return true; }
        if (IEquals(v, L"Gradient")) { field = ColorMode::Gradient; return true; }
        return false;
    }
    bool ApplyBackgroundStyle(const std::wstring& v, BackgroundStyle& field)
    {
        if (IEquals(v, L"Flat")) { field = BackgroundStyle::Flat; return true; }
        if (IEquals(v, L"Gradient")) { field = BackgroundStyle::Gradient; return true; }
        return false;
    }
    bool ApplyAmplitudeScaling(const std::wstring& v, AmplitudeScaling& field)
    {
        if (IEquals(v, L"Linear")) { field = AmplitudeScaling::Linear; return true; }
        if (IEquals(v, L"Logarithmic")) { field = AmplitudeScaling::Logarithmic; return true; }
        return false;
    }
    bool ApplyGridLineStyle(const std::wstring& v, GridLineStyle& field)
    {
        if (IEquals(v, L"Dashed")) { field = GridLineStyle::Dashed; return true; }
        if (IEquals(v, L"Solid")) { field = GridLineStyle::Solid; return true; }
        return false;
    }
    bool ApplyChannelMode(const std::wstring& v, ChannelMode& field)
    {
        if (IEquals(v, L"Mono")) { field = ChannelMode::Mono; return true; }
        if (IEquals(v, L"Stereo")) { field = ChannelMode::Stereo; return true; }
        return false;
    }

    using KeyHandler = std::function<bool(const std::wstring&, AuraBarsSettings&)>;

    // Key list mirrors SerializeSettings()'s output one-for-one (46 keys) -
    // kept immediately below it in this file on purpose, per the contract's
    // "the two must live next to each other" requirement. Key names are
    // matched case-insensitively by the caller (ParseSettings lowercases
    // before lookup), so they're registered here in whatever casing reads
    // best.
    std::unordered_map<std::wstring, KeyHandler> BuildParseHandlers()
    {
        std::unordered_map<std::wstring, KeyHandler> h;

        h[L"debuglogging"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.debugLogging); };

        h[L"amplitudescaling"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyAmplitudeScaling(v, s.amplitudeScaling); };
        h[L"dbfloor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyFloat(v, s.dbFloor); };
        h[L"dbceiling"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyFloat(v, s.dbCeiling); };
        h[L"autogainceiling"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.autoGainCeiling); };
        h[L"barsmoothing"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.barSmoothing); };
        h[L"barsmoothingattack"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyFloat(v, s.barSmoothingAttack); };
        h[L"barsmoothingdecay"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyFloat(v, s.barSmoothingDecay); };
        h[L"numberofbars"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.barCount); };

        h[L"barstyle"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBarStyle(v, s.barStyle); };
        h[L"barcolormode"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColorMode(v, s.barColorMode); };
        h[L"barcolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.barColorSolid); };
        h[L"bargradienttop"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.barGradientTop); };
        h[L"bargradientbottom"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.barGradientBottom); };
        h[L"barspacing"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.barSpacing); };
        h[L"segmentgap"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.segmentGap); };
        h[L"segmentheight"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.segmentHeight); };

        h[L"peakmarkers"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.peakMarkers); };
        h[L"peakmarkercolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.peakMarkerColor); };
        h[L"peakfallspeed"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyFloat(v, s.peakFallSpeed); };
        h[L"peakmarkerthickness"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.peakMarkerThickness); };
        h[L"peakmarkerheightsegments"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.peakMarkerHeightSegments); };
        h[L"peaksmoothmotion"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.peakSmoothMotion); };

        h[L"backgroundstyle"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBackgroundStyle(v, s.backgroundStyle); };
        h[L"backgroundcolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.backgroundColor); };
        h[L"gridlines"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.gridLines); };
        h[L"gridlinecolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.gridLineColor); };
        h[L"gridlinespacing"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.gridLineSpacing); };
        h[L"gridlineopacity"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.gridLineOpacity); };
        h[L"gridlinestyle"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyGridLineStyle(v, s.gridLineStyle); };
        h[L"backgroundbarsenabled"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.backgroundBarsEnabled); };
        h[L"backgroundbarcolormode"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColorMode(v, s.backgroundBarColorMode); };
        h[L"backgroundbarcolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.backgroundBarColorSolid); };
        h[L"backgroundbargradienttop"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.backgroundBarGradientTop); };
        h[L"backgroundbargradientbottom"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.backgroundBarGradientBottom); };

        h[L"channelmode"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyChannelMode(v, s.channelMode); };
        h[L"topmargin"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.topMargin); };
        h[L"bottommargin"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.bottomMargin); };
        h[L"leftmargin"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.leftMargin); };
        h[L"rightmargin"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.rightMargin); };
        h[L"borderenabled"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.borderEnabled); };
        h[L"bordercolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.borderColor); };
        h[L"borderthickness"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.borderThickness); };
        h[L"separatorenabled"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyBool(v, s.separatorEnabled); };
        h[L"separatorcolor"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyColor(v, s.separatorColor); };
        h[L"separatorthickness"] = [](const std::wstring& v, AuraBarsSettings& s) { return ApplyInt(v, s.separatorThickness); };

        return h;
    }

    // Pulls the informational "; Version: x.y.z" header comment out of a
    // pasted payload for the debug-log mismatch note (§3) - never applied to
    // settings, so it's kept separate from ParseSettings' key handling above.
    std::wstring ExtractVersionComment(const std::wstring& payload)
    {
        size_t pos = payload.find(L"; Version:");
        if (pos == std::wstring::npos)
            return L"";
        size_t lineEnd = payload.find(L'\n', pos);
        std::wstring line = (lineEnd == std::wstring::npos) ? payload.substr(pos) : payload.substr(pos, lineEnd - pos);
        size_t colon = line.find(L':');
        if (colon == std::wstring::npos)
            return L"";
        return Trim(line.substr(colon + 1));
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
    if (iid == IID_IAIMPUIChangeEvents)
    {
        *obj = static_cast<IAIMPUIChangeEvents*>(this);
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

    if (!g_Core)
        return nullptr;

    if (FAILED(g_Core->QueryInterface(IID_IAIMPServiceUI, (void**)&m_UIService)) || !m_UIService)
        return nullptr;

    AutoAIMPString name(MakeAIMPString(L"AuraBarsOptions"));
    if (FAILED(m_UIService->CreateForm(ParentWnd, AIMPUI_SERVICE_CREATEFORM_FLAGS_CHILD, name.Get(), nullptr, &m_Form)) || !m_Form)
    {
        m_UIService->Release();
        m_UIService = nullptr;
        return nullptr;
    }

    // Embedded page, not a real window - no chrome of its own; AIMP's own
    // preferences dialog frame supplies the outer border/caption.
    m_Form->SetValueAsInt32(AIMPUI_FORM_PROPID_BORDERSTYLE, AIMPUI_FLAGS_BORDERSTYLE_NONE);

    // Lets the static ClipboardTimerProc recover this instance from the
    // HWND alone (SetTimer's callback only gets the window handle back, not
    // a user-data pointer) - a window property, not GWLP_USERDATA, since
    // this HWND belongs to AIMP's own UI framework and may already use that
    // slot for its own purposes.
    SetPropW(m_Form->GetHandle(), kClipboardFramePropName, (HANDLE)this);

    BuildTabs();
    PopulateControls();

    m_UIService->Release();
    m_UIService = nullptr;

    return m_Form->GetHandle();
}

void WINAPI COptionsFrame::DestroyFrame()
{
    for (IUnknown* ctrl : m_OwnedControls)
    {
        if (ctrl)
            ctrl->Release();
    }
    m_OwnedControls.clear();
    // Every typed member pointer above pointed at one of the controls just
    // released; CreateFrame always rebuilds all of them from scratch on the
    // next call, so there's no need to null out each one individually here.

    if (m_Form)
    {
        // Must happen before the form's HWND is torn down below: killing
        // the timer here guarantees a pending callback can never fire into
        // a freed form, and RemovePropW cleans up what SetPropW attached in
        // CreateFrame (window properties don't self-clean on destroy).
        HWND hwnd = m_Form->GetHandle();
        KillTimer(hwnd, kClipboardTimerId);
        KillTimer(hwnd, kPasteTimerId);
        RemovePropW(hwnd, kClipboardFramePropName);

        // IAIMPUIForm::Release(BOOL Postponed) is AIMP's own window-teardown
        // method, unrelated to COM refcounting despite the name - it hides
        // IUnknown::Release() by name in C++ (no "using" clause in the SDK
        // header), so the actual COM reference can only be dropped by going
        // through the base-typed pointer, where name lookup can't see the
        // derived override.
        m_Form->Release(FALSE);
        static_cast<IUnknown*>(m_Form)->Release();
        m_Form = nullptr;
    }
}

void WINAPI COptionsFrame::Notification(int ID)
{
    if (!m_Form)
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

bool COptionsFrame::SenderIs(IUnknown* sender, IUnknown* control) const
{
    if (!sender || !control)
        return false;
    IUnknown* a = nullptr;
    sender->QueryInterface(IID_IUnknown, (void**)&a);
    if (a)
        a->Release();
    IUnknown* b = nullptr;
    control->QueryInterface(IID_IUnknown, (void**)&b);
    if (b)
        b->Release();
    return a == b;
}

void WINAPI COptionsFrame::OnChanged(IUnknown* Sender)
{
    if (!Sender)
        return;

    // Buttons: each needs its own specific action rather than a generic
    // "something changed" response.
    if (SenderIs(Sender, m_ResetDefaultsBtn))    { ResetToDefaults(); return; }
    if (SenderIs(Sender, m_BarColorBtn))         { PickColor(m_BarColorBtn, m_Working.barColorSolid); return; }
    if (SenderIs(Sender, m_BarGradTopBtn))       { PickColor(m_BarGradTopBtn, m_Working.barGradientTop); return; }
    if (SenderIs(Sender, m_BarGradBottomBtn))    { PickColor(m_BarGradBottomBtn, m_Working.barGradientBottom); return; }
    if (SenderIs(Sender, m_BackgroundBarColorBtn))     { PickColor(m_BackgroundBarColorBtn, m_Working.backgroundBarColorSolid); return; }
    if (SenderIs(Sender, m_BackgroundBarGradTopBtn))   { PickColor(m_BackgroundBarGradTopBtn, m_Working.backgroundBarGradientTop); return; }
    if (SenderIs(Sender, m_BackgroundBarGradBottomBtn)){ PickColor(m_BackgroundBarGradBottomBtn, m_Working.backgroundBarGradientBottom); return; }
    if (SenderIs(Sender, m_BackColorBtn))        { PickColor(m_BackColorBtn, m_Working.backgroundColor); return; }
    if (SenderIs(Sender, m_GridColorBtn))        { PickColor(m_GridColorBtn, m_Working.gridLineColor); return; }
    if (SenderIs(Sender, m_PeakColorBtn))        { PickColor(m_PeakColorBtn, m_Working.peakMarkerColor); return; }
    if (SenderIs(Sender, m_BorderColorBtn))      { PickColor(m_BorderColorBtn, m_Working.borderColor); return; }
    if (SenderIs(Sender, m_SeparatorColorBtn))   { PickColor(m_SeparatorColorBtn, m_Working.separatorColor); return; }
    if (SenderIs(Sender, m_CopyClipboardBtn))    { CopySettingsToClipboard(); return; }
    if (SenderIs(Sender, m_PasteClipboardBtn))   { PasteSettingsFromClipboard(); return; }

    // Combos/checkboxes that gate other controls' enabled state.
    if (SenderIs(Sender, m_BarStyleCombo) || SenderIs(Sender, m_BarColorModeCombo) ||
        SenderIs(Sender, m_AmpScaleCombo) || SenderIs(Sender, m_ChannelModeCombo) ||
        SenderIs(Sender, m_AutoGainCheck) || SenderIs(Sender, m_BarSmoothingCheck) ||
        SenderIs(Sender, m_BackgroundBarColorModeCombo) || SenderIs(Sender, m_PeakSmoothMotionCheck))
    {
        UpdateEnabledStates();
        NotifyModified();
        return;
    }

    // Everything else (plain edits, and checkboxes/combos with no gating
    // effect on other controls) just marks the frame modified.
    NotifyModified();
}

void COptionsFrame::SetBounds(IAIMPUIControl* control, int x, int y, int w, int h)
{
    if (!control)
        return;
    TAIMPUIControlPlacement placement{};
    placement.Alignment = ualNone;
    placement.AlignmentMargins = RECT{ 0, 0, 0, 0 };
    // Anchors is a set of four boolean flags (Left, Top, Right, Bottom), not
    // a margin rect - {0,0,0,0} means "anchored to nothing", which left the
    // control's position undefined relative to its explicit Bounds. The
    // Delphi reference header's own Reset() default is {1,1,0,0} (anchored
    // to left+top only, i.e. a fixed offset from the parent's top-left).
    placement.Anchors = RECT{ 1, 1, 0, 0 };
    placement.Bounds = RECT{ x, y, x + w, y + h };
    control->SetPlacement(placement);
}

void COptionsFrame::SetText(IAIMPPropertyList* propertyList, int propId, const wchar_t* text)
{
    if (!propertyList)
        return;
    AutoAIMPString s(MakeAIMPString(text));
    if (s.Get())
        propertyList->SetValueAsObject(propId, s.Get());
}

IAIMPUILabel* COptionsFrame::CreateLabel(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* text)
{
    IAIMPUILabel* label = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, nullptr, IID_IAIMPUILabel, (void**)&label)) || !label)
        return nullptr;
    SetBounds(label, x, y, w, h);
    SetText(label, AIMPUI_LABEL_PROPID_TEXT, text);
    m_OwnedControls.push_back(static_cast<IUnknown*>(label));
    return label;
}

IAIMPUIEdit* COptionsFrame::CreateEdit(IAIMPUIWinControl* parent, int x, int y, int w, int h)
{
    IAIMPUIEdit* edit = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, static_cast<IAIMPUIChangeEvents*>(this), IID_IAIMPUIEdit, (void**)&edit)) || !edit)
        return nullptr;
    SetBounds(edit, x, y, w, h);
    m_OwnedControls.push_back(static_cast<IUnknown*>(edit));
    return edit;
}

IAIMPUICheckBox* COptionsFrame::CreateCheckBox(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption)
{
    IAIMPUICheckBox* cb = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, static_cast<IAIMPUIChangeEvents*>(this), IID_IAIMPUICheckBox, (void**)&cb)) || !cb)
        return nullptr;
    SetBounds(cb, x, y, w, h);
    SetText(cb, AIMPUI_CHECKBOX_PROPID_CAPTION, caption);
    m_OwnedControls.push_back(static_cast<IUnknown*>(cb));
    return cb;
}

IAIMPUIComboBox* COptionsFrame::CreateCombo(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* a, const wchar_t* b)
{
    IAIMPUIComboBox* combo = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, static_cast<IAIMPUIChangeEvents*>(this), IID_IAIMPUIComboBox, (void**)&combo)) || !combo)
        return nullptr;
    SetBounds(combo, x, y, w, h);
    combo->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_STYLE, AIMPUI_COMBOBOX_STYLE_LIST);

    AutoAIMPString sa(MakeAIMPString(a));
    AutoAIMPString sb(MakeAIMPString(b));
    if (sa.Get())
        combo->Add(sa.Get(), 0);
    if (sb.Get())
        combo->Add(sb.Get(), 0);

    m_OwnedControls.push_back(static_cast<IUnknown*>(combo));
    return combo;
}

IAIMPUIButton* COptionsFrame::CreateButton(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption)
{
    IAIMPUIButton* btn = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, static_cast<IAIMPUIChangeEvents*>(this), IID_IAIMPUIButton, (void**)&btn)) || !btn)
        return nullptr;
    SetBounds(btn, x, y, w, h);
    SetText(btn, AIMPUI_BUTTON_PROPID_CAPTION, caption);
    m_OwnedControls.push_back(static_cast<IUnknown*>(btn));
    return btn;
}

IAIMPUIButton* COptionsFrame::CreateColorButton(IAIMPUIWinControl* parent, int x, int y, int w, int h)
{
    return CreateButton(parent, x, y, w, h, L"");
}

void COptionsFrame::SetButtonColorText(IAIMPUIButton* btn, COLORREF c)
{
    wchar_t buf[16];
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    SetText(btn, AIMPUI_BUTTON_PROPID_CAPTION, buf);
}

IAIMPUIGroupBox* COptionsFrame::CreateGroupBox(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption)
{
    IAIMPUIGroupBox* gb = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, parent, nullptr, nullptr, IID_IAIMPUIGroupBox, (void**)&gb)) || !gb)
        return nullptr;
    SetBounds(gb, x, y, w, h);
    SetText(gb, AIMPUI_GROUPBOX_PROPID_CAPTION, caption);
    m_OwnedControls.push_back(static_cast<IUnknown*>(gb));
    return gb;
}

int COptionsFrame::LabelRow(IAIMPUIWinControl* parent, int y, int indent, const wchar_t* text)
{
    CreateLabel(parent, indent, y, kFieldX - indent - 10, 12, text);
    return y;
}

void COptionsFrame::BuildTabs()
{
    // The old Win32 hand-rolled tab strip needed an explicit "hide every
    // non-active panel at creation" step (its absence was a fixed bug -
    // every tab's controls rendered simultaneously on first load). AIMP's
    // native IAIMPUIPageControl used here manages child-sheet visibility
    // itself as part of the control, so that rule is preserved structurally
    // by construction, not via any explicit hide-calls in this file.
    IAIMPUIPageControl* pages = nullptr;
    if (FAILED(m_UIService->CreateControl(m_Form, m_Form, nullptr, nullptr, IID_IAIMPUIPageControl, (void**)&pages)) || !pages)
        return;
    m_OwnedControls.push_back(static_cast<IUnknown*>(pages));

    TAIMPUIControlPlacement fill{};
    fill.Alignment = ualClient;
    fill.AlignmentMargins = RECT{ 0, 0, 0, 0 };
    fill.Anchors = RECT{ 0, 0, 0, 0 };
    fill.Bounds = RECT{ 0, 0, 0, 0 };
    pages->SetPlacement(fill);

    auto addTab = [&](const wchar_t* caption) -> IAIMPUITabSheet*
    {
        AutoAIMPString label(MakeAIMPString(caption));
        IAIMPUITabSheet* sheet = nullptr;
        pages->Add(label.Get(), &sheet);
        if (sheet)
        {
            m_OwnedControls.push_back(static_cast<IUnknown*>(sheet));
            // Add()'s Name argument is only an internal identifier, not the
            // visible tab caption - the displayed text is a separate
            // property on the sheet itself.
            SetText(sheet, AIMPUI_TABSHEET_PROPID_CAPTION, caption);
        }
        return sheet;
    };

    // Tab order matters here: IAIMPUIPageControl activates whichever tab
    // sheet was added first, so About-first-and-selected-on-open falls out
    // of this order with no separate "set active tab" call needed.
    if (IAIMPUITabSheet* tab = addTab(L"About"))       BuildAboutTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"System"))     BuildSystemTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Scaling"))     BuildScalingTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Bars"))        BuildBarsTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Peaks"))       BuildPeaksTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Background"))  BuildBackgroundTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Layout"))      BuildLayoutTab(tab);
}

void COptionsFrame::BuildAboutTab(IAIMPUIWinControl* tab)
{
    const int wideW = kFieldX + kFieldW + 90; // roomier than the standard field column - this is read-only prose, not a form field

    int y = 8;
    LabelRow(tab, y, 10, L"Name:");
    CreateLabel(tab, kFieldX, y, kFieldW, 12, AURABARS_NAME_WSTR);
    y += kRowH;

    LabelRow(tab, y, 10, L"Version:");
    CreateLabel(tab, kFieldX, y, kFieldW, 12, AURABARS_FULL_VERSION_WSTR);
    y += kRowH;

    LabelRow(tab, y, 10, L"Author:");
    CreateLabel(tab, kFieldX, y, kFieldW, 12, AURABARS_AUTHOR_WSTR);
    y += kRowH;

    // apiGUI.h exposes AIMPUI_LABEL_PROPID_URL directly on IAIMPUILabel -
    // AIMP's own native link control - so this renders as a real clickable
    // hyperlink with no manual click hit-testing or ShellExecuteW fallback
    // needed.
    LabelRow(tab, y, 10, L"Topic:");
    IAIMPUILabel* topicLink = CreateLabel(tab, kFieldX, y, wideW - kFieldX, 12, AURABARS_TOPIC_WSTR);
    SetText(topicLink, AIMPUI_LABEL_PROPID_URL, AURABARS_TOPIC_WSTR);
    y += kRowH;

    LabelRow(tab, y, 10, L"Description:");
    IAIMPUILabel* descLabel = CreateLabel(tab, kFieldX, y, wideW - kFieldX, 24, AURABARS_DESCRIPTION_WSTR);
    if (descLabel)
        descLabel->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, 1);
}

void COptionsFrame::BuildSystemTab(IAIMPUIWinControl* tab)
{
    m_ResetDefaultsBtn = CreateButton(tab, 10, 8, 150, kButtonH, L"Reset to Defaults");
    m_CopyClipboardBtn = CreateButton(tab, 10, 8 + kRowH, 190, kButtonH, L"Copy settings to clipboard");
    m_PasteClipboardBtn = CreateButton(tab, 10 + 190 + 10, 8 + kRowH, 200, kButtonH, L"Paste settings from clipboard");

    m_DebugLogCheck = CreateCheckBox(tab, 10, 8 + 2 * kRowH, 320, 20, L"Debug logging (writes to %TEMP%\\AuraBars_debug.log, 1/sec)");
}

void COptionsFrame::BuildScalingTab(IAIMPUIWinControl* tab)
{
    const int groupW = kFieldX + kFieldW + 10;
    const int rows = 4;
    IAIMPUIGroupBox* group = CreateGroupBox(tab, 10, 8, groupW, kGroupContentTop + rows * kRowH + kGroupContentBottom, L"Amplitude Scaling");

    LabelRow(group, kGroupContentTop, 10, L"Amplitude scaling:");
    m_AmpScaleCombo = CreateCombo(group, kFieldX, kGroupContentTop, kFieldW, 60, L"Linear", L"Logarithmic (dB)");

    m_DbFloorLabel = CreateLabel(group, 10, kGroupContentTop + kRowH, kFieldX - 20, 12, L"dB floor (-100 to -20):");
    m_DbFloorEdit = CreateEdit(group, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_DbCeilingLabel = CreateLabel(group, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"dB ceiling (-20 to 0):");
    m_DbCeilingEdit = CreateEdit(group, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);

    m_AutoGainCheck = CreateCheckBox(group, 10, kGroupContentTop + 3 * kRowH, groupW - 20, 12, L"Auto-gain ceiling (overrides dB ceiling above)");

    int smoothY = 8 + (kGroupContentTop + rows * kRowH + kGroupContentBottom) + 10;
    const int smoothRows = 3;
    IAIMPUIGroupBox* smoothGroup = CreateGroupBox(tab, 10, smoothY, groupW, kGroupContentTop + smoothRows * kRowH + kGroupContentBottom, L"Bar Smoothing");

    m_BarSmoothingCheck = CreateCheckBox(smoothGroup, 10, kGroupContentTop, groupW - 20, 12, L"Bar smoothing");

    m_BarSmoothingAttackLabel = CreateLabel(smoothGroup, 10, kGroupContentTop + kRowH, kFieldX - 20, 12, L"Attack (0.0-1.0):");
    m_BarSmoothingAttackEdit = CreateEdit(smoothGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_BarSmoothingDecayLabel = CreateLabel(smoothGroup, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Decay (0.0-1.0):");
    m_BarSmoothingDecayEdit = CreateEdit(smoothGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);

    int barsY = smoothY + (kGroupContentTop + smoothRows * kRowH + kGroupContentBottom) + 10;
    LabelRow(tab, barsY, 10, L"Number of bars (8-128):");
    m_BarCountEdit = CreateEdit(tab, kFieldX, barsY, kFieldW, kFieldH);
}

void COptionsFrame::BuildBarsTab(IAIMPUIWinControl* tab)
{
    const int groupW = kFieldX + kFieldW + 10;

    const int styleRows = 5;
    IAIMPUIGroupBox* styleGroup = CreateGroupBox(tab, 10, 8, groupW, kGroupContentTop + styleRows * kRowH + kGroupContentBottom, L"Bar Style");

    LabelRow(styleGroup, kGroupContentTop, 10, L"Bar style:");
    m_BarStyleCombo = CreateCombo(styleGroup, kFieldX, kGroupContentTop, kFieldW, 60, L"LED", L"Smooth");

    LabelRow(styleGroup, kGroupContentTop + kRowH, 10, L"Bar color mode:");
    m_BarColorModeCombo = CreateCombo(styleGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, 60, L"Solid", L"Gradient");

    m_BarColorLabel = CreateLabel(styleGroup, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Bar color:");
    m_BarColorBtn = CreateColorButton(styleGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kButtonH);

    m_BarGradTopLabel = CreateLabel(styleGroup, 10, kGroupContentTop + 3 * kRowH, kFieldX - 20, 12, L"Gradient top color:");
    m_BarGradTopBtn = CreateColorButton(styleGroup, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kButtonH);

    m_BarGradBottomLabel = CreateLabel(styleGroup, 10, kGroupContentTop + 4 * kRowH, kFieldX - 20, 12, L"Gradient bottom color:");
    m_BarGradBottomBtn = CreateColorButton(styleGroup, kFieldX, kGroupContentTop + 4 * kRowH, kFieldW, kButtonH);

    int geoY = 8 + (kGroupContentTop + styleRows * kRowH + kGroupContentBottom) + 10;
    const int geoRows = 3;
    IAIMPUIGroupBox* geoGroup = CreateGroupBox(tab, 10, geoY, groupW, kGroupContentTop + geoRows * kRowH + kGroupContentBottom, L"Bar Geometry");

    LabelRow(geoGroup, kGroupContentTop, 10, L"Bar spacing (px, 0-20):");
    m_BarSpacingEdit = CreateEdit(geoGroup, kFieldX, kGroupContentTop, kFieldW, kFieldH);

    m_SegmentGapLabel = CreateLabel(geoGroup, 10, kGroupContentTop + kRowH, kFieldX - 20, 12, L"Segment gap (LED, px, 1-8):");
    m_SegmentGapEdit = CreateEdit(geoGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_SegmentHeightLabel = CreateLabel(geoGroup, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Segment height (LED, px, 4-20):");
    m_SegmentHeightEdit = CreateEdit(geoGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);
}

void COptionsFrame::BuildPeaksTab(IAIMPUIWinControl* tab)
{
    const int groupW = kFieldX + kFieldW + 10;
    const int rows = 5;
    IAIMPUIGroupBox* group = CreateGroupBox(tab, 10, 8, groupW, kGroupContentTop + rows * kRowH + kGroupContentBottom, L"Peak Markers");

    m_PeakMarkersCheck = CreateCheckBox(group, 10, kGroupContentTop, 140, 12, L"Peak markers");
    m_PeakColorBtn = CreateColorButton(group, kFieldX, kGroupContentTop, kFieldW, kButtonH);

    LabelRow(group, kGroupContentTop + kRowH, 10, L"Peak fall speed (px/frame, 0.1-10.0):");
    m_PeakFallSpeedEdit = CreateEdit(group, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_PeakThicknessLabel = CreateLabel(group, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Peak thickness (Smooth, 1-10):");
    m_PeakThicknessEdit = CreateEdit(group, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);

    m_PeakHeightSegLabel = CreateLabel(group, 10, kGroupContentTop + 3 * kRowH, kFieldX - 20, 12, L"Peak height, segments (LED, 1-3):");
    m_PeakHeightSegEdit = CreateEdit(group, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kFieldH);

    m_PeakSmoothMotionCheck = CreateCheckBox(group, 10, kGroupContentTop + 4 * kRowH, groupW - 20, 12, L"Peak marker smooth motion (LED style)");
}

void COptionsFrame::BuildBackgroundTab(IAIMPUIWinControl* tab)
{
    const int groupW = kFieldX + kFieldW + 10;

    IAIMPUIGroupBox* backGroup = CreateGroupBox(tab, 10, 8, groupW, kGroupContentTop + 2 * kRowH + kGroupContentBottom, L"Background");
    LabelRow(backGroup, kGroupContentTop, 10, L"Background style:");
    m_BackStyleCombo = CreateCombo(backGroup, kFieldX, kGroupContentTop, kFieldW, 60, L"Flat", L"Gradient");
    LabelRow(backGroup, kGroupContentTop + kRowH, 10, L"Background color:");
    m_BackColorBtn = CreateColorButton(backGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kButtonH);

    int gridY = 8 + (kGroupContentTop + 2 * kRowH + kGroupContentBottom) + 10;
    IAIMPUIGroupBox* gridGroup = CreateGroupBox(tab, 10, gridY, groupW, kGroupContentTop + 4 * kRowH + kGroupContentBottom, L"Grid Lines");
    m_GridLinesCheck = CreateCheckBox(gridGroup, 10, kGroupContentTop, 140, 12, L"Grid lines");
    m_GridColorBtn = CreateColorButton(gridGroup, kFieldX, kGroupContentTop, kFieldW, kButtonH);

    const int nestedIndent = 26;
    LabelRow(gridGroup, kGroupContentTop + kRowH, nestedIndent, L"Grid line spacing (px, 8-100):");
    m_GridSpacingEdit = CreateEdit(gridGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);
    LabelRow(gridGroup, kGroupContentTop + 2 * kRowH, nestedIndent, L"Grid line opacity (%, 0-100):");
    m_GridOpacityEdit = CreateEdit(gridGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);
    LabelRow(gridGroup, kGroupContentTop + 3 * kRowH, nestedIndent, L"Grid line style:");
    m_GridStyleCombo = CreateCombo(gridGroup, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, 60, L"Dashed", L"Solid");

    int bgY = gridY + (kGroupContentTop + 4 * kRowH + kGroupContentBottom) + 10;
    const int bgRows = 5;
    IAIMPUIGroupBox* bgGroup = CreateGroupBox(tab, 10, bgY, groupW, kGroupContentTop + bgRows * kRowH + kGroupContentBottom, L"Background Bars");

    m_BackgroundBarsCheck = CreateCheckBox(bgGroup, 10, kGroupContentTop, groupW - 20, 12, L"Background bars/LEDs");

    m_BackgroundBarColorModeLabel = CreateLabel(bgGroup, 10, kGroupContentTop + kRowH, kFieldX - 20, 12, L"Background color mode:");
    m_BackgroundBarColorModeCombo = CreateCombo(bgGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, 60, L"Solid", L"Gradient");

    m_BackgroundBarColorLabel = CreateLabel(bgGroup, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Background color:");
    m_BackgroundBarColorBtn = CreateColorButton(bgGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kButtonH);

    m_BackgroundBarGradTopLabel = CreateLabel(bgGroup, 10, kGroupContentTop + 3 * kRowH, kFieldX - 20, 12, L"Background gradient top:");
    m_BackgroundBarGradTopBtn = CreateColorButton(bgGroup, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kButtonH);

    m_BackgroundBarGradBottomLabel = CreateLabel(bgGroup, 10, kGroupContentTop + 4 * kRowH, kFieldX - 20, 12, L"Background gradient bottom:");
    m_BackgroundBarGradBottomBtn = CreateColorButton(bgGroup, kFieldX, kGroupContentTop + 4 * kRowH, kFieldW, kButtonH);
}

void COptionsFrame::BuildLayoutTab(IAIMPUIWinControl* tab)
{
    const int groupW = kFieldX + kFieldW + 10;

    // No "channel gap" setting exists anymore (it was removed in favor of
    // margins + border + separator in an earlier contract) - Channels only
    // has one field, so it stays ungrouped rather than a near-empty group.
    LabelRow(tab, 8, 10, L"Channel mode:");
    m_ChannelModeCombo = CreateCombo(tab, kFieldX, 8, kFieldW, 60, L"Mono", L"Stereo");

    int marginsY = 8 + kRowH + 10;
    const int marginRows = 4;
    IAIMPUIGroupBox* marginsGroup = CreateGroupBox(tab, 10, marginsY, groupW, kGroupContentTop + marginRows * kRowH + kGroupContentBottom, L"Margins");

    LabelRow(marginsGroup, kGroupContentTop, 10, L"Top margin (px, 0-100):");
    m_TopMarginEdit = CreateEdit(marginsGroup, kFieldX, kGroupContentTop, kFieldW, kFieldH);
    LabelRow(marginsGroup, kGroupContentTop + kRowH, 10, L"Bottom margin (px, 0-100):");
    m_BottomMarginEdit = CreateEdit(marginsGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);
    LabelRow(marginsGroup, kGroupContentTop + 2 * kRowH, 10, L"Left margin (px, 0-100):");
    m_LeftMarginEdit = CreateEdit(marginsGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);
    LabelRow(marginsGroup, kGroupContentTop + 3 * kRowH, 10, L"Right margin (px, 0-100):");
    m_RightMarginEdit = CreateEdit(marginsGroup, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kFieldH);

    int frameY = marginsY + (kGroupContentTop + marginRows * kRowH + kGroupContentBottom) + 10;
    const int frameRows = 4;
    IAIMPUIGroupBox* frameGroup = CreateGroupBox(tab, 10, frameY, groupW, kGroupContentTop + frameRows * kRowH + kGroupContentBottom, L"Border / Separator");

    m_BorderEnabledCheck = CreateCheckBox(frameGroup, 10, kGroupContentTop, 140, 12, L"Border enabled");
    m_BorderColorBtn = CreateColorButton(frameGroup, kFieldX, kGroupContentTop, kFieldW, kButtonH);

    m_BorderThicknessLabel = CreateLabel(frameGroup, 10, kGroupContentTop + kRowH, kFieldX - 20, 12, L"Border thickness (px, 1-10):");
    m_BorderThicknessEdit = CreateEdit(frameGroup, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_SeparatorEnabledCheck = CreateCheckBox(frameGroup, 10, kGroupContentTop + 2 * kRowH, 140, 12, L"Separator enabled");
    m_SeparatorColorBtn = CreateColorButton(frameGroup, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kButtonH);

    m_SeparatorThicknessLabel = CreateLabel(frameGroup, 10, kGroupContentTop + 3 * kRowH, kFieldX - 20, 12, L"Separator thickness (px, 1-10):");
    m_SeparatorThicknessEdit = CreateEdit(frameGroup, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kFieldH);
}

void COptionsFrame::PopulateControls()
{
    SetEditInt(m_BarCountEdit, m_Working.barCount);
    SetComboIndex(m_AmpScaleCombo, (int)m_Working.amplitudeScaling);
    SetEditFloat(m_DbFloorEdit, m_Working.dbFloor);
    SetEditFloat(m_DbCeilingEdit, m_Working.dbCeiling);
    SetCheckState(m_AutoGainCheck, m_Working.autoGainCeiling);
    SetCheckState(m_BarSmoothingCheck, m_Working.barSmoothing);
    SetEditFloat(m_BarSmoothingAttackEdit, m_Working.barSmoothingAttack);
    SetEditFloat(m_BarSmoothingDecayEdit, m_Working.barSmoothingDecay);

    SetComboIndex(m_BarStyleCombo, (int)m_Working.barStyle);
    SetComboIndex(m_BarColorModeCombo, (int)m_Working.barColorMode);
    SetComboIndex(m_BackgroundBarColorModeCombo, (int)m_Working.backgroundBarColorMode);
    SetComboIndex(m_BackStyleCombo, (int)m_Working.backgroundStyle);
    SetComboIndex(m_GridStyleCombo, (int)m_Working.gridLineStyle);
    SetComboIndex(m_ChannelModeCombo, (int)m_Working.channelMode);

    SetButtonColorText(m_BarColorBtn, m_Working.barColorSolid);
    SetButtonColorText(m_BarGradTopBtn, m_Working.barGradientTop);
    SetButtonColorText(m_BarGradBottomBtn, m_Working.barGradientBottom);
    SetButtonColorText(m_BackgroundBarColorBtn, m_Working.backgroundBarColorSolid);
    SetButtonColorText(m_BackgroundBarGradTopBtn, m_Working.backgroundBarGradientTop);
    SetButtonColorText(m_BackgroundBarGradBottomBtn, m_Working.backgroundBarGradientBottom);
    SetButtonColorText(m_BackColorBtn, m_Working.backgroundColor);
    SetButtonColorText(m_GridColorBtn, m_Working.gridLineColor);
    SetButtonColorText(m_PeakColorBtn, m_Working.peakMarkerColor);
    SetButtonColorText(m_BorderColorBtn, m_Working.borderColor);
    SetButtonColorText(m_SeparatorColorBtn, m_Working.separatorColor);

    SetCheckState(m_GridLinesCheck, m_Working.gridLines);
    SetCheckState(m_BackgroundBarsCheck, m_Working.backgroundBarsEnabled);
    SetEditInt(m_GridSpacingEdit, m_Working.gridLineSpacing);
    SetEditInt(m_GridOpacityEdit, m_Working.gridLineOpacity);
    SetCheckState(m_PeakMarkersCheck, m_Working.peakMarkers);
    SetCheckState(m_PeakSmoothMotionCheck, m_Working.peakSmoothMotion);
    SetCheckState(m_DebugLogCheck, m_Working.debugLogging);
    SetCheckState(m_BorderEnabledCheck, m_Working.borderEnabled);
    SetCheckState(m_SeparatorEnabledCheck, m_Working.separatorEnabled);

    SetEditFloat(m_PeakFallSpeedEdit, m_Working.peakFallSpeed);
    SetEditInt(m_PeakThicknessEdit, m_Working.peakMarkerThickness);
    SetEditInt(m_PeakHeightSegEdit, m_Working.peakMarkerHeightSegments);
    SetEditInt(m_BarSpacingEdit, m_Working.barSpacing);
    SetEditInt(m_TopMarginEdit, m_Working.topMargin);
    SetEditInt(m_BottomMarginEdit, m_Working.bottomMargin);
    SetEditInt(m_SegmentGapEdit, m_Working.segmentGap);
    SetEditInt(m_SegmentHeightEdit, m_Working.segmentHeight);
    SetEditInt(m_LeftMarginEdit, m_Working.leftMargin);
    SetEditInt(m_RightMarginEdit, m_Working.rightMargin);
    SetEditInt(m_BorderThicknessEdit, m_Working.borderThickness);
    SetEditInt(m_SeparatorThicknessEdit, m_Working.separatorThickness);

    UpdateEnabledStates();
}

void COptionsFrame::ReadControlsIntoWorking()
{
    m_Working.barCount = GetEditInt(m_BarCountEdit, m_Working.barCount);
    m_Working.amplitudeScaling = (AmplitudeScaling)GetComboIndex(m_AmpScaleCombo, (int)m_Working.amplitudeScaling);
    m_Working.dbFloor = GetEditFloat(m_DbFloorEdit, m_Working.dbFloor);
    m_Working.dbCeiling = GetEditFloat(m_DbCeilingEdit, m_Working.dbCeiling);
    m_Working.autoGainCeiling = GetCheckState(m_AutoGainCheck);
    m_Working.barSmoothing = GetCheckState(m_BarSmoothingCheck);
    m_Working.barSmoothingAttack = GetEditFloat(m_BarSmoothingAttackEdit, m_Working.barSmoothingAttack);
    m_Working.barSmoothingDecay = GetEditFloat(m_BarSmoothingDecayEdit, m_Working.barSmoothingDecay);
    m_Working.barStyle = (BarStyle)GetComboIndex(m_BarStyleCombo, (int)m_Working.barStyle);
    m_Working.barColorMode = (ColorMode)GetComboIndex(m_BarColorModeCombo, (int)m_Working.barColorMode);
    m_Working.backgroundBarColorMode = (ColorMode)GetComboIndex(m_BackgroundBarColorModeCombo, (int)m_Working.backgroundBarColorMode);
    m_Working.backgroundStyle = (BackgroundStyle)GetComboIndex(m_BackStyleCombo, (int)m_Working.backgroundStyle);
    m_Working.gridLineStyle = (GridLineStyle)GetComboIndex(m_GridStyleCombo, (int)m_Working.gridLineStyle);
    m_Working.channelMode = (ChannelMode)GetComboIndex(m_ChannelModeCombo, (int)m_Working.channelMode);

    m_Working.gridLines = GetCheckState(m_GridLinesCheck);
    m_Working.backgroundBarsEnabled = GetCheckState(m_BackgroundBarsCheck);
    m_Working.peakMarkers = GetCheckState(m_PeakMarkersCheck);
    m_Working.peakSmoothMotion = GetCheckState(m_PeakSmoothMotionCheck);
    m_Working.debugLogging = GetCheckState(m_DebugLogCheck);
    m_Working.borderEnabled = GetCheckState(m_BorderEnabledCheck);
    m_Working.separatorEnabled = GetCheckState(m_SeparatorEnabledCheck);

    m_Working.peakFallSpeed = GetEditFloat(m_PeakFallSpeedEdit, m_Working.peakFallSpeed);
    m_Working.peakMarkerThickness = GetEditInt(m_PeakThicknessEdit, m_Working.peakMarkerThickness);
    m_Working.peakMarkerHeightSegments = GetEditInt(m_PeakHeightSegEdit, m_Working.peakMarkerHeightSegments);
    m_Working.barSpacing = GetEditInt(m_BarSpacingEdit, m_Working.barSpacing);
    m_Working.topMargin = GetEditInt(m_TopMarginEdit, m_Working.topMargin);
    m_Working.bottomMargin = GetEditInt(m_BottomMarginEdit, m_Working.bottomMargin);
    m_Working.segmentGap = GetEditInt(m_SegmentGapEdit, m_Working.segmentGap);
    m_Working.segmentHeight = GetEditInt(m_SegmentHeightEdit, m_Working.segmentHeight);
    m_Working.gridLineSpacing = GetEditInt(m_GridSpacingEdit, m_Working.gridLineSpacing);
    m_Working.gridLineOpacity = GetEditInt(m_GridOpacityEdit, m_Working.gridLineOpacity);
    m_Working.leftMargin = GetEditInt(m_LeftMarginEdit, m_Working.leftMargin);
    m_Working.rightMargin = GetEditInt(m_RightMarginEdit, m_Working.rightMargin);
    m_Working.borderThickness = GetEditInt(m_BorderThicknessEdit, m_Working.borderThickness);
    m_Working.separatorThickness = GetEditInt(m_SeparatorThicknessEdit, m_Working.separatorThickness);

    // Colors are already current in m_Working; PickColor() updates it live.
}

void COptionsFrame::UpdateEnabledStates()
{
    bool isLed = GetComboIndex(m_BarStyleCombo, 0) == 0;
    bool isGradient = GetComboIndex(m_BarColorModeCombo, 0) == 1;
    bool isLogarithmic = GetComboIndex(m_AmpScaleCombo, 0) == 1;
    bool autoGain = GetCheckState(m_AutoGainCheck);
    bool isStereo = GetComboIndex(m_ChannelModeCombo, 0) == 1;

    SetEnabled(m_DbFloorEdit, isLogarithmic);
    SetEnabled(m_DbFloorLabel, isLogarithmic);
    SetEnabled(m_DbCeilingEdit, isLogarithmic && !autoGain);
    SetEnabled(m_DbCeilingLabel, isLogarithmic && !autoGain);
    SetEnabled(m_AutoGainCheck, isLogarithmic);

    bool smoothingOn = GetCheckState(m_BarSmoothingCheck);
    SetEnabled(m_BarSmoothingAttackEdit, smoothingOn);
    SetEnabled(m_BarSmoothingAttackLabel, smoothingOn);
    SetEnabled(m_BarSmoothingDecayEdit, smoothingOn);
    SetEnabled(m_BarSmoothingDecayLabel, smoothingOn);

    SetEnabled(m_BarColorBtn, !isGradient);
    SetEnabled(m_BarColorLabel, !isGradient);
    SetEnabled(m_BarGradTopBtn, isGradient);
    SetEnabled(m_BarGradTopLabel, isGradient);
    SetEnabled(m_BarGradBottomBtn, isGradient);
    SetEnabled(m_BarGradBottomLabel, isGradient);

    bool isBackgroundGradient = GetComboIndex(m_BackgroundBarColorModeCombo, 0) == 1;
    SetEnabled(m_BackgroundBarColorBtn, !isBackgroundGradient);
    SetEnabled(m_BackgroundBarColorLabel, !isBackgroundGradient);
    SetEnabled(m_BackgroundBarGradTopBtn, isBackgroundGradient);
    SetEnabled(m_BackgroundBarGradTopLabel, isBackgroundGradient);
    SetEnabled(m_BackgroundBarGradBottomBtn, isBackgroundGradient);
    SetEnabled(m_BackgroundBarGradBottomLabel, isBackgroundGradient);

    bool peakSmoothMotion = GetCheckState(m_PeakSmoothMotionCheck);
    bool useThickness = !isLed || peakSmoothMotion;
    SetEnabled(m_PeakThicknessEdit, useThickness);
    SetEnabled(m_PeakThicknessLabel, useThickness);
    SetEnabled(m_PeakHeightSegEdit, isLed && !peakSmoothMotion);
    SetEnabled(m_PeakHeightSegLabel, isLed && !peakSmoothMotion);
    SetEnabled(m_PeakSmoothMotionCheck, isLed);
    SetEnabled(m_SegmentGapEdit, isLed);
    SetEnabled(m_SegmentGapLabel, isLed);
    SetEnabled(m_SegmentHeightEdit, isLed);
    SetEnabled(m_SegmentHeightLabel, isLed);

    SetEnabled(m_SeparatorEnabledCheck, isStereo);
    SetEnabled(m_SeparatorColorBtn, isStereo);
    SetEnabled(m_SeparatorThicknessLabel, isStereo);
    SetEnabled(m_SeparatorThicknessEdit, isStereo);
}

void COptionsFrame::PickColor(IAIMPUIButton* button, COLORREF& target)
{
    static COLORREF customColors[16] = { 0 };

    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = m_Form ? m_Form->GetHandle() : nullptr;
    cc.rgbResult = target;
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc))
    {
        target = cc.rgbResult;
        SetButtonColorText(button, target);
        NotifyModified();
    }
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

void CALLBACK COptionsFrame::ClipboardTimerProc(HWND hwnd, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/)
{
    COptionsFrame* self = (COptionsFrame*)GetPropW(hwnd, kClipboardFramePropName);
    KillTimer(hwnd, idEvent);
    if (!self)
        return;
    if (idEvent == kClipboardTimerId && self->m_CopyClipboardBtn)
        self->SetText(self->m_CopyClipboardBtn, AIMPUI_BUTTON_PROPID_CAPTION, L"Copy settings to clipboard");
    else if (idEvent == kPasteTimerId && self->m_PasteClipboardBtn)
        self->SetText(self->m_PasteClipboardBtn, AIMPUI_BUTTON_PROPID_CAPTION, L"Paste settings from clipboard");
}

std::wstring SerializeSettings(const AuraBarsSettings& s)
{
    time_t t = time(nullptr);
    tm local;
    localtime_s(&local, &t);
    wchar_t timestamp[32];
    wcsftime(timestamp, 32, L"%Y-%m-%d %H:%M:%S", &local);

#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif

    std::wstring out;
    out += L"; AuraBars settings snapshot\r\n";
    out += L"; Version: "; out += AURABARS_FULL_VERSION_WSTR; out += L"\r\n";
    out += L"; Build: "; out += arch; out += L"\r\n";
    out += L"; Generated: "; out += timestamp; out += L"\r\n";
    out += L"\r\n";

    out += L"[System]\r\n";
    out += L"DebugLogging="; out += NameOf(s.debugLogging); out += L"\r\n";
    out += L"\r\n";

    // Number of bars actually lives on the Scaling tab (not Bars) - see the
    // reconciled Code Contract - so it's grouped here to match "sections in
    // tab order."
    out += L"[Scaling]\r\n";
    out += L"AmplitudeScaling="; out += NameOf(s.amplitudeScaling); out += L"\r\n";
    out += L"dBFloor="; out += Float1(s.dbFloor); out += L"\r\n";
    out += L"dBCeiling="; out += Float1(s.dbCeiling); out += L"\r\n";
    out += L"AutoGainCeiling="; out += NameOf(s.autoGainCeiling); out += L"\r\n";
    out += L"BarSmoothing="; out += NameOf(s.barSmoothing); out += L"\r\n";
    out += L"BarSmoothingAttack="; out += Float1(s.barSmoothingAttack); out += L"\r\n";
    out += L"BarSmoothingDecay="; out += Float1(s.barSmoothingDecay); out += L"\r\n";
    out += L"NumberOfBars="; out += std::to_wstring(s.barCount); out += L"\r\n";
    out += L"\r\n";

    out += L"[Bars]\r\n";
    out += L"BarStyle="; out += NameOf(s.barStyle); out += L"\r\n";
    out += L"BarColorMode="; out += NameOf(s.barColorMode); out += L"\r\n";
    out += L"BarColor="; out += ColorHex(s.barColorSolid); out += L"\r\n";
    out += L"BarGradientTop="; out += ColorHex(s.barGradientTop); out += L"\r\n";
    out += L"BarGradientBottom="; out += ColorHex(s.barGradientBottom); out += L"\r\n";
    out += L"BarSpacing="; out += std::to_wstring(s.barSpacing); out += L"\r\n";
    out += L"SegmentGap="; out += std::to_wstring(s.segmentGap); out += L"\r\n";
    out += L"SegmentHeight="; out += std::to_wstring(s.segmentHeight); out += L"\r\n";
    out += L"\r\n";

    out += L"[Peaks]\r\n";
    out += L"PeakMarkers="; out += NameOf(s.peakMarkers); out += L"\r\n";
    out += L"PeakMarkerColor="; out += ColorHex(s.peakMarkerColor); out += L"\r\n";
    out += L"PeakFallSpeed="; out += Float1(s.peakFallSpeed); out += L"\r\n";
    out += L"PeakMarkerThickness="; out += std::to_wstring(s.peakMarkerThickness); out += L"\r\n";
    out += L"PeakMarkerHeightSegments="; out += std::to_wstring(s.peakMarkerHeightSegments); out += L"\r\n";
    out += L"PeakSmoothMotion="; out += NameOf(s.peakSmoothMotion); out += L"\r\n";
    out += L"\r\n";

    // Background Bars now lives on the Background tab (moved there from
    // Bars in an earlier session) - grouped here to match.
    out += L"[Background]\r\n";
    out += L"BackgroundStyle="; out += NameOf(s.backgroundStyle); out += L"\r\n";
    out += L"BackgroundColor="; out += ColorHex(s.backgroundColor); out += L"\r\n";
    out += L"GridLines="; out += NameOf(s.gridLines); out += L"\r\n";
    out += L"GridLineColor="; out += ColorHex(s.gridLineColor); out += L"\r\n";
    out += L"GridLineSpacing="; out += std::to_wstring(s.gridLineSpacing); out += L"\r\n";
    out += L"GridLineOpacity="; out += std::to_wstring(s.gridLineOpacity); out += L"\r\n";
    out += L"GridLineStyle="; out += NameOf(s.gridLineStyle); out += L"\r\n";
    out += L"BackgroundBarsEnabled="; out += NameOf(s.backgroundBarsEnabled); out += L"\r\n";
    out += L"BackgroundBarColorMode="; out += NameOf(s.backgroundBarColorMode); out += L"\r\n";
    out += L"BackgroundBarColor="; out += ColorHex(s.backgroundBarColorSolid); out += L"\r\n";
    out += L"BackgroundBarGradientTop="; out += ColorHex(s.backgroundBarGradientTop); out += L"\r\n";
    out += L"BackgroundBarGradientBottom="; out += ColorHex(s.backgroundBarGradientBottom); out += L"\r\n";
    out += L"\r\n";

    out += L"[Layout]\r\n";
    out += L"ChannelMode="; out += NameOf(s.channelMode); out += L"\r\n";
    out += L"TopMargin="; out += std::to_wstring(s.topMargin); out += L"\r\n";
    out += L"BottomMargin="; out += std::to_wstring(s.bottomMargin); out += L"\r\n";
    out += L"LeftMargin="; out += std::to_wstring(s.leftMargin); out += L"\r\n";
    out += L"RightMargin="; out += std::to_wstring(s.rightMargin); out += L"\r\n";
    out += L"BorderEnabled="; out += NameOf(s.borderEnabled); out += L"\r\n";
    out += L"BorderColor="; out += ColorHex(s.borderColor); out += L"\r\n";
    out += L"BorderThickness="; out += std::to_wstring(s.borderThickness); out += L"\r\n";
    out += L"SeparatorEnabled="; out += NameOf(s.separatorEnabled); out += L"\r\n";
    out += L"SeparatorColor="; out += ColorHex(s.separatorColor); out += L"\r\n";
    out += L"SeparatorThickness="; out += std::to_wstring(s.separatorThickness); out += L"\r\n";

    return out;
}

ParseSettingsResult ParseSettings(const std::wstring& payload)
{
    static const std::unordered_map<std::wstring, KeyHandler> handlers = BuildParseHandlers();

    ParseSettingsResult result;
    AuraBarsSettings& s = result.settings; // default-constructed = factory defaults; only overwritten below for keys that actually parse

    // Per-key outcome, keyed by lowercased key name: present in this map at
    // all means the key was recognized in the payload (which is what makes
    // the payload "valid" below); the bool is whether its value parsed.
    // A repeated key's last occurrence wins, matching plain INI semantics.
    std::unordered_map<std::wstring, bool> outcomes;

    size_t pos = 0;
    while (pos < payload.size())
    {
        size_t nl = payload.find(L'\n', pos);
        std::wstring line = (nl == std::wstring::npos) ? payload.substr(pos) : payload.substr(pos, nl - pos);
        pos = (nl == std::wstring::npos) ? payload.size() : nl + 1;

        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'[')
            continue; // blank, comment, or section header (context only)

        size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = ToLowerCopy(Trim(trimmed.substr(0, eq)));
        std::wstring value = Trim(trimmed.substr(eq + 1));

        auto it = handlers.find(key);
        if (it == handlers.end())
            continue; // unknown key - ignored silently (forward compatibility)

        outcomes[key] = it->second(value, s);
    }

    result.valid = !outcomes.empty();
    for (const auto& kv : handlers)
    {
        auto oc = outcomes.find(kv.first);
        if (oc != outcomes.end() && oc->second)
            ++result.keysApplied;
        else
            ++result.keysDefaulted; // absent from payload, or present but unparseable
    }

    // Clamps every numeric field to its documented range - the same
    // ClampSettings() the Save path already uses, so parsed-but-out-of-range
    // values are clamped rather than rejected, with no duplicated range
    // logic to drift from Settings.cpp.
    ClampSettings(s);

    return result;
}

void COptionsFrame::CopySettingsToClipboard()
{
    // Live dialog state including unapplied edits, not the last-persisted
    // g_Settings - this is the same read used at Save time, just invoked
    // here instead without touching g_Settings or ClampSettings.
    ReadControlsIntoWorking();
    std::wstring payload = SerializeSettings(m_Working);

    bool ok = false;
    if (OpenClipboard(m_Form ? m_Form->GetHandle() : nullptr))
    {
        EmptyClipboard();
        SIZE_T bytes = (payload.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem)
        {
            void* dst = GlobalLock(mem);
            if (dst)
            {
                memcpy(dst, payload.c_str(), bytes);
                GlobalUnlock(mem);
                // SetClipboardData takes ownership of mem only on success -
                // freeing it ourselves after a successful call would be a
                // double-free of memory the clipboard now owns.
                if (SetClipboardData(CF_UNICODETEXT, mem))
                    ok = true;
            }
            if (!ok)
                GlobalFree(mem);
        }
        CloseClipboard();
    }

    if (m_CopyClipboardBtn)
        SetText(m_CopyClipboardBtn, AIMPUI_BUTTON_PROPID_CAPTION, ok ? L"Copied!" : L"Copy failed");

    if (!ok && g_Settings.debugLogging)
        AppendDebugLog(L"Copy settings to clipboard failed (OpenClipboard/GlobalAlloc/SetClipboardData)");

    // IAIMPUIButton exposes no per-control background/color-override
    // property in apiGUI.h (checked: CAPTION/FOCUSABLE/DEFAULT/
    // DROPDOWNMENU/IMAGEINDEX/IMAGELIST/MODALRESULT/STYLE/TEXTSTYLE only,
    // no color propid) - falling back to caption-only feedback rather than
    // inventing a workaround, per the contract's own fallback instruction.

    if (m_Form)
    {
        HWND hwnd = m_Form->GetHandle();
        KillTimer(hwnd, kClipboardTimerId); // re-click restarts the revert countdown from zero
        SetTimer(hwnd, kClipboardTimerId, 2000, &COptionsFrame::ClipboardTimerProc);
    }
}

void COptionsFrame::PasteSettingsFromClipboard()
{
    std::wstring payload;
    bool gotPayload = false;

    if (OpenClipboard(m_Form ? m_Form->GetHandle() : nullptr))
    {
        if (IsClipboardFormatAvailable(CF_UNICODETEXT))
        {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h)
            {
                const wchar_t* data = (const wchar_t*)GlobalLock(h);
                if (data)
                {
                    payload = data; // copy out before unlocking - never mutate the clipboard's own memory
                    gotPayload = true;
                    GlobalUnlock(h);
                }
            }
        }
        CloseClipboard();
    }

    ParseSettingsResult parsed;
    bool ok = false;
    if (gotPayload)
    {
        parsed = ParseSettings(payload);
        ok = parsed.valid;

        if (g_Settings.debugLogging)
        {
            std::wstring headerVersion = ExtractVersionComment(payload);
            if (!headerVersion.empty() && headerVersion != AURABARS_FULL_VERSION_WSTR)
            {
                std::wstring line = L"Paste settings from clipboard: payload version " + headerVersion +
                    L" differs from running version " + AURABARS_FULL_VERSION_WSTR;
                AppendDebugLog(line.c_str());
            }
        }
    }

    // Parse fully into parsed.settings first; only now, with valid==true
    // confirmed, do we touch a single dialog control. Zero recognized keys
    // (or no clipboard text at all) leaves every control exactly as it was.
    if (ok)
    {
        m_Working = parsed.settings;
        PopulateControls(); // writes every control and re-runs UpdateEnabledStates() for dependent-control gating
        NotifyModified();
    }

    if (m_PasteClipboardBtn)
        SetText(m_PasteClipboardBtn, AIMPUI_BUTTON_PROPID_CAPTION, ok ? L"Pasted!" : L"Paste failed");

    if (g_Settings.debugLogging)
    {
        wchar_t buf[160];
        if (ok)
            swprintf_s(buf, L"Paste settings from clipboard succeeded (applied=%d, defaulted=%d)", parsed.keysApplied, parsed.keysDefaulted);
        else
            swprintf_s(buf, L"Paste settings from clipboard failed (clipboard unavailable, empty, or no recognized keys)");
        AppendDebugLog(buf);
    }

    if (m_Form)
    {
        HWND hwnd = m_Form->GetHandle();
        KillTimer(hwnd, kPasteTimerId); // re-click restarts the revert countdown from zero; independent of Copy's timer
        SetTimer(hwnd, kPasteTimerId, 2000, &COptionsFrame::ClipboardTimerProc);
    }
}
