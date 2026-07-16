# AuraBars

Native C++ visualization plugin for AIMP (x64). Spectrum-bar meter implementing `IAIMPExtensionEmbeddedVisualization`.

## Features

- LED (segmented) / Smooth bar rendering
- Solid / gradient bar color
- Flat / gradient background, optional grid lines
- Peak markers (fall speed, thickness/segment height)
- Linear / logarithmic (dB) amplitude scaling, configurable floor/ceiling
- Auto-gain ceiling override
- Configurable bar count, spacing, segment gap/height, top margin
- Settings persisted via `IAIMPServiceConfig`
- Double-buffered GDI rendering
- Debug logging (once/sec: raw magnitude + dB for bar1/mid/last bar → `%TEMP%\AuraBars_debug.log`)

## Build

MSVC x64, `vcvarsall.bat x64` environment required. `build.bat` → `build/AuraBars.dll`.

## Install (manual test)

Close AIMP → copy `AuraBars.dll` to `C:\Program Files\AIMP\Plugins` (admin required) → relaunch AIMP.

## Known issues

- dB scaling miscalibrated: raw spectrum magnitude isn't normalized to 0–1, so bins regularly produce +20 to +33 dB against a 0 dB ceiling default, clamping most bars to max. Needs a calibration/reference divisor before `20*log10(x)`, or a rebased ceiling default matching actual data scale.
- 1px stray line rendered above the peak marker
- Last (rightmost) bar width incorrect — inconsistent with other bars

## TODO

- About tab in settings dialog
- Left/right/bottom margin settings
- Horizontal grid line customization (spacing, style)
- Bottom panel
- Dark theme support in settings dialog