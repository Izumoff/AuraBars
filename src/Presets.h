#pragma once

// Built-in settings presets (Code Contract §3, "Settings presets"). Each
// preset is stored as the exact clipboard-payload text `Copy settings to
// clipboard` produces, parsed by the same ParseSettings() used for pasting -
// no hand-transcribed struct. A preset can therefore never contain a
// typo'd colour or an out-of-range value that bypasses validation; if a
// preset fails to load, so would the equivalent user paste. An embedded
// payload's own "[Environment]" block (if it has one) needs no stripping -
// the reserved "Env" key prefix already makes the parser discard it.
struct PresetInfo
{
    const wchar_t* name;
    const wchar_t* payload;
};

// Order matters: index 0 (Pandemic) is both the first dropdown entry and
// the one selected whenever the dialog (re)opens, and is also the factory
// default set - see Settings.h's AuraBarsSettings member initializers
// (which mirror this exact payload by hand) and
// COptionsFrame::ResetToDefaults (which loads this preset directly rather
// than keeping a second copy of the default values).
extern const PresetInfo kPresets[5];
constexpr int kPresetCount = 5;
constexpr int kDefaultPresetIndex = 0; // Pandemic
