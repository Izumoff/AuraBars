#pragma once

#include "Common.h"
#include "Settings.h"
#include <atomic>
#include <vector>

// IAIMPOptionsDialogFrame implementation built on AIMP's native IAIMPUIForm /
// IAIMPUIControl widget framework (apiGUI.h) rather than a raw Win32 dialog.
// There is no SDK API to query the active skin's actual colors for hand-
// painting a Win32 dialog - the native control framework is the only
// supported way to get a settings page that automatically follows AIMP's
// skin (light/dark, accent, DPI), since AIMP itself draws these controls.
//
// COptionsFrame implements two IUnknown-derived interfaces
// (IAIMPOptionsDialogFrame and IAIMPUIChangeEvents), which would normally
// create an ambiguous IUnknown base in C++. This is resolved because
// COptionsFrame provides its own concrete QueryInterface/AddRef/Release
// that override both bases simultaneously - callers must never implicitly
// upcast a COptionsFrame* to IUnknown* directly (ambiguous); pass an
// explicit single-interface upcast instead (see the .cpp, e.g.
// static_cast<IAIMPUIChangeEvents*>(this) when handing "this" to
// CreateControl as an EventsHandler).
class COptionsFrame : public IAIMPOptionsDialogFrame, public IAIMPUIChangeEvents
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

    // IAIMPUIChangeEvents - shared by every interactive control; dispatch
    // is by identity comparison against the stored member pointers.
    void WINAPI OnChanged(IUnknown* Sender) override;

private:
    // --- low-level construction helpers ---
    void SetBounds(IAIMPUIControl* control, int x, int y, int w, int h);
    void SetText(IAIMPPropertyList* propertyList, int propId, const wchar_t* text);
    IAIMPUILabel* CreateLabel(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* text);
    IAIMPUIEdit* CreateEdit(IAIMPUIWinControl* parent, int x, int y, int w, int h);
    IAIMPUICheckBox* CreateCheckBox(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption);
    IAIMPUIComboBox* CreateCombo(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* a, const wchar_t* b);
    IAIMPUIButton* CreateButton(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption);
    IAIMPUIButton* CreateColorButton(IAIMPUIWinControl* parent, int x, int y, int w, int h);
    IAIMPUIGroupBox* CreateGroupBox(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption);
    void SetButtonColorText(IAIMPUIButton* btn, COLORREF c);
    // Places a label at the shared label column and returns the y-position
    // for that row's field, so every field in every group lines up on one
    // column regardless of label length.
    int LabelRow(IAIMPUIWinControl* parent, int y, int indent, const wchar_t* text);
    bool SenderIs(IUnknown* sender, IUnknown* control) const;

    // --- tab construction --- (all use the m_UIService member, acquired
    // for the duration of CreateFrame only)
    void BuildTabs();
    void BuildSystemTab(IAIMPUIWinControl* tab);
    void BuildScalingTab(IAIMPUIWinControl* tab);
    void BuildBarsTab(IAIMPUIWinControl* tab);
    void BuildPeaksTab(IAIMPUIWinControl* tab);
    void BuildBackgroundTab(IAIMPUIWinControl* tab);
    void BuildLayoutTab(IAIMPUIWinControl* tab);

    void PopulateControls();
    void ReadControlsIntoWorking();
    void UpdateEnabledStates();
    void PickColor(IAIMPUIButton* button, COLORREF& target);
    void ResetToDefaults();
    void NotifyModified();

    std::atomic<long> m_RefCount{1};
    IAIMPServiceUI* m_UIService = nullptr; // borrowed for the duration of CreateFrame's Build* calls; not held long-term
    IAIMPUIForm* m_Form = nullptr;
    AuraBarsSettings m_Working;

    // Every control created via the CreateXxx helpers also gets pushed here
    // (as its IUnknown identity) so DestroyFrame can release them all in
    // one pass without hand-writing ~40 individual Release() calls. This
    // does not add an extra reference - it tracks the same reference the
    // typed member pointer below already owns.
    std::vector<IUnknown*> m_OwnedControls;

    // System tab
    IAIMPUILabel* m_VersionValueLabel = nullptr;
    IAIMPUIButton* m_ResetDefaultsBtn = nullptr;
    IAIMPUICheckBox* m_DebugLogCheck = nullptr;

    // Scaling tab
    IAIMPUIComboBox* m_AmpScaleCombo = nullptr;
    IAIMPUILabel* m_DbFloorLabel = nullptr;
    IAIMPUIEdit* m_DbFloorEdit = nullptr;
    IAIMPUILabel* m_DbCeilingLabel = nullptr;
    IAIMPUIEdit* m_DbCeilingEdit = nullptr;
    IAIMPUICheckBox* m_AutoGainCheck = nullptr;
    IAIMPUIEdit* m_BarCountEdit = nullptr;
    IAIMPUICheckBox* m_BarSmoothingCheck = nullptr;
    IAIMPUILabel* m_BarSmoothingAttackLabel = nullptr;
    IAIMPUIEdit* m_BarSmoothingAttackEdit = nullptr;
    IAIMPUILabel* m_BarSmoothingDecayLabel = nullptr;
    IAIMPUIEdit* m_BarSmoothingDecayEdit = nullptr;

    // Bars tab
    IAIMPUIComboBox* m_BarStyleCombo = nullptr;
    IAIMPUIComboBox* m_BarColorModeCombo = nullptr;
    IAIMPUILabel* m_BarColorLabel = nullptr;
    IAIMPUIButton* m_BarColorBtn = nullptr;
    IAIMPUILabel* m_BarGradTopLabel = nullptr;
    IAIMPUIButton* m_BarGradTopBtn = nullptr;
    IAIMPUILabel* m_BarGradBottomLabel = nullptr;
    IAIMPUIButton* m_BarGradBottomBtn = nullptr;
    IAIMPUIEdit* m_BarSpacingEdit = nullptr;
    IAIMPUILabel* m_SegmentGapLabel = nullptr;
    IAIMPUIEdit* m_SegmentGapEdit = nullptr;
    IAIMPUILabel* m_SegmentHeightLabel = nullptr;
    IAIMPUIEdit* m_SegmentHeightEdit = nullptr;
    IAIMPUICheckBox* m_BackgroundBarsCheck = nullptr;
    IAIMPUILabel* m_BackgroundBarColorModeLabel = nullptr;
    IAIMPUIComboBox* m_BackgroundBarColorModeCombo = nullptr;
    IAIMPUILabel* m_BackgroundBarColorLabel = nullptr;
    IAIMPUIButton* m_BackgroundBarColorBtn = nullptr;
    IAIMPUILabel* m_BackgroundBarGradTopLabel = nullptr;
    IAIMPUIButton* m_BackgroundBarGradTopBtn = nullptr;
    IAIMPUILabel* m_BackgroundBarGradBottomLabel = nullptr;
    IAIMPUIButton* m_BackgroundBarGradBottomBtn = nullptr;

    // Peaks tab
    IAIMPUICheckBox* m_PeakMarkersCheck = nullptr;
    IAIMPUIButton* m_PeakColorBtn = nullptr;
    IAIMPUIEdit* m_PeakFallSpeedEdit = nullptr;
    IAIMPUILabel* m_PeakThicknessLabel = nullptr;
    IAIMPUIEdit* m_PeakThicknessEdit = nullptr;
    IAIMPUILabel* m_PeakHeightSegLabel = nullptr;
    IAIMPUIEdit* m_PeakHeightSegEdit = nullptr;
    IAIMPUICheckBox* m_PeakSmoothMotionCheck = nullptr;

    // Background tab
    IAIMPUIComboBox* m_BackStyleCombo = nullptr;
    IAIMPUIButton* m_BackColorBtn = nullptr;
    IAIMPUICheckBox* m_GridLinesCheck = nullptr;
    IAIMPUIButton* m_GridColorBtn = nullptr;
    IAIMPUIEdit* m_GridSpacingEdit = nullptr;
    IAIMPUIEdit* m_GridOpacityEdit = nullptr;
    IAIMPUIComboBox* m_GridStyleCombo = nullptr;

    // Layout tab
    IAIMPUIEdit* m_TopMarginEdit = nullptr;
    IAIMPUIEdit* m_BottomMarginEdit = nullptr;
    IAIMPUIEdit* m_LeftMarginEdit = nullptr;
    IAIMPUIEdit* m_RightMarginEdit = nullptr;
    IAIMPUIComboBox* m_ChannelModeCombo = nullptr;
    IAIMPUICheckBox* m_BorderEnabledCheck = nullptr;
    IAIMPUIButton* m_BorderColorBtn = nullptr;
    IAIMPUILabel* m_BorderThicknessLabel = nullptr;
    IAIMPUIEdit* m_BorderThicknessEdit = nullptr;
    IAIMPUICheckBox* m_SeparatorEnabledCheck = nullptr;
    IAIMPUIButton* m_SeparatorColorBtn = nullptr;
    IAIMPUILabel* m_SeparatorThicknessLabel = nullptr;
    IAIMPUIEdit* m_SeparatorThicknessEdit = nullptr;
};
