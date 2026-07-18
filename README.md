# AuraBars

Native C++ spectrum-bar visualization plugin for AIMP (x86 + x64), implementing `IAIMPExtensionEmbeddedVisualization`.

[AIMP forum thread](https://aimp.ru/forum/index.php?topic=77997.0)

## Features

- LED (segmented) / Smooth bar rendering
- Solid / gradient bar color
- Flat / gradient background
- Grid lines: on/off, spacing, opacity, style (Dashed/Solid)
- Peak markers (fall speed, thickness/segment height)
- Linear / logarithmic (dB) amplitude scaling, configurable floor/ceiling, logarithmic bin-to-bar grouping
- Auto-gain ceiling override
- Mono (combined) or Stereo (side-by-side L/R) channel mode, with independent channel gap
- Configurable bar count, spacing, segment gap/height
- Left/Right/Top margin
- Tabbed settings dialog: System, Scaling, Bars, Peaks, Background, Layout
- Settings persisted via `IAIMPServiceConfig`
- Double-buffered GDI rendering
- Debug logging (once/sec: raw magnitude + dB for bar1/mid/last bar → `%TEMP%\AuraBars_debug.log`)

## Build

MSVC (x86 + x64), `vcvarsall.bat` environment required per architecture. `build.bat` → `build/x86/AuraBars.dll` and `build/x64/AuraBars.dll`. `package.bat` assembles both into `AuraBars.aimppack`, along with `AuraBars.txt` (name/version/author/topic/description metadata, version auto-stamped per build) and the hand-maintained `ReadMe.txt`.

## Install

**Via package**: double-click `AuraBars.aimppack` — AIMP handles architecture selection automatically.

**Manual test**: close AIMP → copy the matching-architecture `AuraBars.dll` to `C:\Program Files\AIMP\Plugins` (admin required) → relaunch AIMP.

## TODO

- About tab in settings dialog
- Bottom margin setting
- Border and padding
- Bottom panel
- Dark theme support in settings dialog

