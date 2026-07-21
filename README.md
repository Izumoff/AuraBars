# AuraBars

Native C++ spectrum-bar visualization plugin for AIMP (x86 + x64), implementing `IAIMPExtensionEmbeddedVisualization`.

[AIMP forum thread](https://aimp.ru/forum/index.php?topic=77997.0)

## Features

- LED (segmented) / Smooth bar rendering
- Solid / gradient bar color
- Bar smoothing: attack/decay filter applied to raw magnitude before amplitude scaling, so both bar height and peak markers respond smoothly instead of jittering frame-to-frame
- Background bars/LEDs: an always-full-height "unlit" layer drawn behind each bar (own independent Solid/Gradient color mode, separate from the front bar's), for a hardware-LED-meter look; off by default
- Flat / gradient background
- Grid lines: on/off, color, spacing, opacity, style (Dashed/Solid)
- Peak markers: color, fall speed, thickness (Smooth style) / height in segments (LED style); optional smooth motion for LED style (continuous float position instead of segment-snapped, using thickness instead of segment height)
- Linear / logarithmic (dB) amplitude scaling, configurable floor/ceiling, logarithmic bin-to-bar grouping
- Auto-gain ceiling override (adaptive per-bar ceiling, fast attack / slow release)
- Mono (combined) or Stereo (side-by-side L/R) channel mode
- Configurable bar count, spacing, segment gap/height
- Top/Bottom/Left/Right margins (px)
- Border (enabled, color, thickness) and, in Stereo mode, a separator between channels (enabled, color, thickness)
- Settings dialog built on AIMP's native UI control framework (`IAIMPUIForm`/`IAIMPUIControl`), so it follows the active AIMP skin instead of default Win32 styling; tabbed (System, Scaling, Bars, Peaks, Background, Layout) with grouped, aligned controls
- Settings persisted via `IAIMPServiceConfig`
- Double-buffered GDI rendering
- Debug logging (once/sec: raw magnitude + dB for the first/middle/last bar → `%TEMP%\AuraBars_debug.log`)

## Build

MSVC (x86 + x64), `vcvarsall.bat` environment required per architecture. `build.bat` → `build/x86/AuraBars.dll` and `build/x64/AuraBars.dll` (both named `AuraBars.dll`, distinguished only by folder). `package.bat` assembles both into `AuraBars.aimppack`, along with `AuraBars.txt` (name/version/author/topic/description metadata, version auto-stamped per build) and the hand-maintained `ReadMe.txt`. Version number lives in exactly one place, `src/Version.h`, plus an auto-generated `src/BuildNumber.h` — both the System tab's version display and `AuraBars.txt` read from there, so they can never drift apart.

## Install

**Via package**: double-click `AuraBars.aimppack` — AIMP handles architecture selection automatically.

**Manual test**: close AIMP → copy the matching-architecture `AuraBars.dll` to `C:\Program Files\AIMP\Plugins` (admin required) → relaunch AIMP.

## TODO

- About tab in settings dialog
- Bottom panel
