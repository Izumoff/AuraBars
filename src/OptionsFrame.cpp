#include "OptionsFrame.h"
#include "Version.h"
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

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
    if (SenderIs(Sender, m_BackColorBtn))        { PickColor(m_BackColorBtn, m_Working.backgroundColor); return; }
    if (SenderIs(Sender, m_GridColorBtn))        { PickColor(m_GridColorBtn, m_Working.gridLineColor); return; }
    if (SenderIs(Sender, m_PeakColorBtn))        { PickColor(m_PeakColorBtn, m_Working.peakMarkerColor); return; }
    if (SenderIs(Sender, m_BorderColorBtn))      { PickColor(m_BorderColorBtn, m_Working.borderColor); return; }
    if (SenderIs(Sender, m_SeparatorColorBtn))   { PickColor(m_SeparatorColorBtn, m_Working.separatorColor); return; }

    // Combos/checkboxes that gate other controls' enabled state.
    if (SenderIs(Sender, m_BarStyleCombo) || SenderIs(Sender, m_BarColorModeCombo) ||
        SenderIs(Sender, m_AmpScaleCombo) || SenderIs(Sender, m_ChannelModeCombo) ||
        SenderIs(Sender, m_AutoGainCheck) || SenderIs(Sender, m_BarSmoothingCheck))
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

    if (IAIMPUITabSheet* tab = addTab(L"System"))     BuildSystemTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Scaling"))     BuildScalingTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Bars"))        BuildBarsTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Peaks"))       BuildPeaksTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Background"))  BuildBackgroundTab(tab);
    if (IAIMPUITabSheet* tab = addTab(L"Layout"))      BuildLayoutTab(tab);
}

void COptionsFrame::BuildSystemTab(IAIMPUIWinControl* tab)
{
    LabelRow(tab, 8, 10, L"Version:");
    m_VersionValueLabel = CreateLabel(tab, kFieldX, 8, kFieldW, 12, L"");

    m_ResetDefaultsBtn = CreateButton(tab, 10, 8 + kRowH, 150, kButtonH, L"Reset to Defaults");

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
    const int rows = 4;
    IAIMPUIGroupBox* group = CreateGroupBox(tab, 10, 8, groupW, kGroupContentTop + rows * kRowH + kGroupContentBottom, L"Peak Markers");

    m_PeakMarkersCheck = CreateCheckBox(group, 10, kGroupContentTop, 140, 12, L"Peak markers");
    m_PeakColorBtn = CreateColorButton(group, kFieldX, kGroupContentTop, kFieldW, kButtonH);

    LabelRow(group, kGroupContentTop + kRowH, 10, L"Peak fall speed (px/frame, 0.1-10.0):");
    m_PeakFallSpeedEdit = CreateEdit(group, kFieldX, kGroupContentTop + kRowH, kFieldW, kFieldH);

    m_PeakThicknessLabel = CreateLabel(group, 10, kGroupContentTop + 2 * kRowH, kFieldX - 20, 12, L"Peak thickness (Smooth, 1-10):");
    m_PeakThicknessEdit = CreateEdit(group, kFieldX, kGroupContentTop + 2 * kRowH, kFieldW, kFieldH);

    m_PeakHeightSegLabel = CreateLabel(group, 10, kGroupContentTop + 3 * kRowH, kFieldX - 20, 12, L"Peak height, segments (LED, 1-3):");
    m_PeakHeightSegEdit = CreateEdit(group, kFieldX, kGroupContentTop + 3 * kRowH, kFieldW, kFieldH);
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
    SetText(m_VersionValueLabel, AIMPUI_LABEL_PROPID_TEXT, AURABARS_FULL_VERSION_WSTR);

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
    SetComboIndex(m_BackStyleCombo, (int)m_Working.backgroundStyle);
    SetComboIndex(m_GridStyleCombo, (int)m_Working.gridLineStyle);
    SetComboIndex(m_ChannelModeCombo, (int)m_Working.channelMode);

    SetButtonColorText(m_BarColorBtn, m_Working.barColorSolid);
    SetButtonColorText(m_BarGradTopBtn, m_Working.barGradientTop);
    SetButtonColorText(m_BarGradBottomBtn, m_Working.barGradientBottom);
    SetButtonColorText(m_BackColorBtn, m_Working.backgroundColor);
    SetButtonColorText(m_GridColorBtn, m_Working.gridLineColor);
    SetButtonColorText(m_PeakColorBtn, m_Working.peakMarkerColor);
    SetButtonColorText(m_BorderColorBtn, m_Working.borderColor);
    SetButtonColorText(m_SeparatorColorBtn, m_Working.separatorColor);

    SetCheckState(m_GridLinesCheck, m_Working.gridLines);
    SetEditInt(m_GridSpacingEdit, m_Working.gridLineSpacing);
    SetEditInt(m_GridOpacityEdit, m_Working.gridLineOpacity);
    SetCheckState(m_PeakMarkersCheck, m_Working.peakMarkers);
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
    m_Working.backgroundStyle = (BackgroundStyle)GetComboIndex(m_BackStyleCombo, (int)m_Working.backgroundStyle);
    m_Working.gridLineStyle = (GridLineStyle)GetComboIndex(m_GridStyleCombo, (int)m_Working.gridLineStyle);
    m_Working.channelMode = (ChannelMode)GetComboIndex(m_ChannelModeCombo, (int)m_Working.channelMode);

    m_Working.gridLines = GetCheckState(m_GridLinesCheck);
    m_Working.peakMarkers = GetCheckState(m_PeakMarkersCheck);
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

    SetEnabled(m_PeakThicknessEdit, !isLed);
    SetEnabled(m_PeakThicknessLabel, !isLed);
    SetEnabled(m_PeakHeightSegEdit, isLed);
    SetEnabled(m_PeakHeightSegLabel, isLed);
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
