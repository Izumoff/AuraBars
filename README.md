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

MSVC (x86 + x64), `vcvarsall.bat` environment required per architecture. `build.bat` → `build/x86/AuraBars.dll` and `build/x64/AuraBars.dll`. `package.bat` assembles both into `AuraBars.aimppack`.

## Install

**Via package**: double-click `AuraBars.aimppack` — AIMP handles architecture selection automatically.

**Manual test**: close AIMP → copy the matching-architecture `AuraBars.dll` to `C:\Program Files\AIMP\Plugins` (admin required) → relaunch AIMP.

## Known issues

- Strange rendering of the Settings dialog (when clicking on tabs - AIMP rerender everything well)
- Horizontal dashed line shows white background between dashes (looks like feature)

## TODO

- Left/right/bottom margin settings
- Horizontal grid line customization (spacing, style)
- Bottom panel
- Dark theme support in settings dialog