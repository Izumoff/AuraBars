#pragma once

#include "Common.h"
#include "Settings.h"
#include <atomic>
#include <string>
#include <vector>

// Clipboard payload serialization (System tab "Copy settings to clipboard" /
// "Paste settings from clipboard", Code Contract §3). Pure functions over
// AuraBarsSettings - no clipboard calls, no control handles - so the parser
// sits directly beside the serializer in OptionsFrame.cpp and the two key
// lists can never silently drift apart. SerializeSettings' output is
// byte-identical to what the Copy button always produced.
struct ParseSettingsResult
{
    AuraBarsSettings settings;   // populated from recognized keys; any key that's missing or fails to parse keeps its factory-default value here
    bool valid = false;          // true iff the payload contained at least one key name recognized from the serializer's key list
    int keysApplied = 0;         // recognized keys successfully parsed from the payload
    int keysDefaulted = 0;       // recognized keys that fell back to their factory default (absent from the payload, or present but unparseable)
};

std::wstring SerializeSettings(const AuraBarsSettings& s);
ParseSettingsResult ParseSettings(const std::wstring& payload);

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
    // Win32 TIMERPROC callback for the "Copied!"/"Copy failed" caption
    // revert - must be a plain static function (not a real member) since
    // SetTimer takes a C-callable function pointer. The frame instance is
    // recovered via a window property on the form's own HWND rather than a
    // global, so it stays correct even if multiple frames existed.
    static void CALLBACK ClipboardTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

    // --- low-level construction helpers ---
    void SetBounds(IAIMPUIControl* control, int x, int y, int w, int h);
    void SetText(IAIMPPropertyList* propertyList, int propId, const wchar_t* text);
    IAIMPUILabel* CreateLabel(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* text);
    IAIMPUIEdit* CreateEdit(IAIMPUIWinControl* parent, int x, int y, int w, int h);
    IAIMPUICheckBox* CreateCheckBox(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* caption);
    // NOTE: a font-metric-based auto-width variant of these (measuring each
    // item via GDI against the form's HWND font) shipped in 2.5.1639 and
    // was reverted - it called SendMessageW(m_Form->GetHandle(), WM_GETFONT,
    // ...) between CreateControl and the item-population loop below, which
    // is suspected to have caused three Background-tab dropdowns to
    // populate empty. Width is a plain caller-supplied constant again until
    // a font-measuring approach that doesn't message a live AIMP window
    // has been verified safe.
    IAIMPUIComboBox* CreateCombo(IAIMPUIWinControl* parent, int x, int y, int w, int h, const wchar_t* a, const wchar_t* b);
    IAIMPUIComboBox* CreateComboItems(IAIMPUIWinControl* parent, int x, int y, int w, int h, const std::vector<const wchar_t*>& items);
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
    void BuildAboutTab(IAIMPUIWinControl* tab);
    void BuildSystemTab(IAIMPUIWinControl* tab);
    void BuildScalingTab(IAIMPUIWinControl* tab);
    void BuildBarsTab(IAIMPUIWinControl* tab);
    void BuildPeaksTab(IAIMPUIWinControl* tab);
    void BuildBackgroundTab(IAIMPUIWinControl* tab);
    void BuildLayoutTab(IAIMPUIWinControl* tab);

    void PopulateControls();
    void ReadControlsIntoWorking();
    void UpdateEnabledStates();
    // Unconditionally enables every control UpdateEnabledStates() ever
    // conditionally disables. Called at the top of PopulateControls(),
    // before any value is written - see the comment there for why.
    void EnableAllControls();
    void PickColor(IAIMPUIButton* button, COLORREF& target);
    // Reimplemented as "load the Pandemic preset" (Code Contract §3) rather
    // than a second, independently-maintained copy of the default values -
    // Pandemic is kPresets[kDefaultPresetIndex]. Confirmation-if-dirty and
    // the dropdown sync both live inside LoadPreset, so this is just that
    // one call.
    void ResetToDefaults();
    void NotifyModified();

    // --- Settings presets (Code Contract §3) ---
    // Parses kPresets[index]'s payload and applies it to the dialog
    // controls, exactly like PasteSettingsFromClipboard, except ChannelMode
    // and DebugLogging are excluded: restored from the dialog's current
    // values rather than taken from the preset. This is the only
    // behavioral difference between loading a preset and pasting a
    // snapshot - both otherwise share ParseSettings and the
    // PopulateControls/UpdateEnabledStates path. Prompts for confirmation
    // first if IsPresetModified() - both Load and Reset to Defaults call
    // this same function, so the prompt covers both with no duplicated
    // logic. A no-op (silently returns) if the payload doesn't parse or the
    // user declines the prompt.
    void LoadPreset(int index);
    // Called whenever the dialog (re)opens (CreateFrame, and
    // Notification's LOAD case): resolves which built-in preset, if any,
    // the *current* settings exactly match (ChannelMode/DebugLogging
    // excluded from the comparison, same as on load) and selects it in the
    // dropdown with a clean (non-"modified") baseline. Preset selection is
    // never persisted to config (Code Contract §3) - if nothing matches,
    // this leaves whatever preset (if any) was last loaded this AIMP
    // session alone, so the dirty indicator can correctly show
    // "<name> (modified)" against it; if nothing has ever been loaded
    // either, the dropdown just falls back to showing Pandemic with no
    // status text, rather than asserting a baseline that was never real.
    void ResolvePresetSelector();
    // Index of the one preset (ChannelMode/DebugLogging excluded) whose
    // parsed payload exactly equals `current`, or -1 if none match.
    int ResolveMatchingPreset(const AuraBarsSettings& current) const;
    // Compares *live* control state against the most recently loaded/
    // resolved preset's parsed values (m_LoadedPresetSettings) - a no-op
    // (false) if no preset has been loaded or resolved this session
    // (m_LoadedPresetIndex < 0). Reads the controls as a side effect
    // (via ReadControlsIntoWorking), same as the status label already did.
    bool IsPresetModified();
    // Refreshes the status label to "<name>" or "<name> (modified)" from
    // IsPresetModified() - blank if m_LoadedPresetIndex < 0.
    void UpdatePresetStatusLabel();

    // Copy/Paste settings to/from clipboard (System tab buttons) - thin
    // wrappers over the free SerializeSettings()/ParseSettings() functions
    // declared above.
    void CopySettingsToClipboard();
    void PasteSettingsFromClipboard();
    // Builds the "[Environment]" diagnostics block Copy prepends ahead of
    // SerializeSettings' output. Deliberately kept out of SerializeSettings
    // itself: these are live host observations (skin, display size, AIMP
    // version), not settings, and SerializeSettings must stay a pure
    // function of AuraBarsSettings alone (see §3/§7 forward note about
    // reusing it for 2.4.x presets, which must never carry diagnostics).
    std::wstring BuildEnvironmentSection() const;

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

    // System tab - Presets group. -1 = no preset loaded or resolved this
    // session (dropdown just defaults to showing Pandemic, no status text
    // - see ResolvePresetSelector). Otherwise an index into kPresets, and
    // m_LoadedPresetSettings is the exact AuraBarsSettings the controls
    // were set to at that point (after the ChannelMode/DebugLogging
    // restore, or the exact current settings when resolved on open), used
    // as the dirty-indicator baseline.
    IAIMPUIComboBox* m_PresetCombo = nullptr;
    IAIMPUIButton* m_PresetLoadBtn = nullptr;
    IAIMPUILabel* m_PresetStatusLabel = nullptr;
    IAIMPUIButton* m_ResetDefaultsBtn = nullptr;
    int m_LoadedPresetIndex = -1;
    AuraBarsSettings m_LoadedPresetSettings;

    // System tab - Settings Transfer / Diagnostics groups
    IAIMPUIButton* m_CopyClipboardBtn = nullptr;
    IAIMPUIButton* m_PasteClipboardBtn = nullptr;
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
