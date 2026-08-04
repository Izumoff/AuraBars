# AuraBars

Native C++ spectrum-bar visualization plugin for AIMP (x86 + x64), implementing `IAIMPExtensionEmbeddedVisualization`.

**Version 2.6.1552**

[AIMP forum thread](https://aimp.ru/forum/index.php?topic=77997.0)

---

## Signal processing

### Frequency-based bin grouping

AIMP's `Spectrum[0..255]` array is **already frequency-warped** before it reaches a plugin — bin 0 is ~20 Hz, bin 10 is ~200 Hz, bin 255 is ~20 kHz. The mapping is documented only as a comment block in the SDK's Delphi header (`apiVisuals.pas:54-81`); the C++ header carries no equivalent.

Earlier versions grouped bins logarithmically by *index*, which applied a second warp on top of AIMP's. The two compounded: low bars shared sub-bin ranges and duplicated each other, while the top bar averaged roughly 60 near-silent bins spanning a full octave. The result was a monotonic descending staircase with a dead right edge, regardless of the material.

Bar edges are now placed by **frequency** and converted to bin indices through a vendored copy of the SDK's mapping table, interpolated linearly in bin index against log frequency. Edges are spaced geometrically in Hz between the configured minimum and maximum.

A note on resolution limits: AIMP allocates only 9 bins below 160 Hz — three octaves' worth. Bars in that region unavoidably share bins and will sometimes move identically. No amount of grouping cleverness recovers detail that isn't in the input, and none is attempted.

### Bin aggregation

How multiple bins collapse into one bar value:

- **Peak** (default) — maximum of the range. Preserves narrow spectral features and keeps the wide upper bars alive.
- **Average** — arithmetic mean. Smoother and quieter; upper bars suffer because their ranges include mostly-silent bins.
- **Sum** — total. Scales with bin count, so upper bars read much hotter. Included for experimentation.

### Spectral tilt

Real music falls off roughly 3–6 dB per octave from bass to treble. Without compensation the display is honest but dull — bass tall, treble short, little movement on the right. Tilt applies a per-bar gain of `tilt × log₂(f_center / 1000)` dB, pivoting at 1 kHz, as a linear multiplier so it works identically in both amplitude modes. Set to 0.0 to disable and see the untilted spectrum.

### Amplitude scaling

Linear or logarithmic (dB) with configurable floor and ceiling, plus an adaptive auto-gain ceiling override (fast attack, slow release).

AIMP's spectrum data is **not** normalized to 0–1 — raw magnitudes reach 40+ before tilt and approach 100 after. The reference magnitude is calibrated against measured playback data rather than an assumed range; this was the root cause of an early vertical-clipping bug.

### Pipeline order

```
group by frequency → aggregate → tilt → smooth → amplitude scale → peak markers
```

Tilt precedes smoothing so the smoothing state holds tilted values. Peak markers run last, after the frame's smoothed bar value exists — reversing that ordering caused two separate shipped bugs and is a standing regression watch.

### Frame-rate independence

AIMP targets 32 fps (31.25 ms). Actual cadence varies with load: ~31.3 ms at typical sizes, ~34.7 ms at 3840×2160, and ~150 ms under Stereo + 128 bars at 4K.

All animation is therefore integrated against measured elapsed time (`QueryPerformanceCounter`, clamped to 100 ms per frame) rather than counted in frames. Bar smoothing coefficients remain 0.0–1.0 in the UI, defined at the 32 fps reference and converted per frame as `1 - (1 - coefficient)^(dt × 32)` — a no-op at nominal rate, correct everywhere else.

---

## Rendering

- **Bar style**: LED (segmented) or Smooth (continuous)
- **Bar color**: solid or gradient (bottom-to-top interpolation, per-segment in LED style)
- **Background bars/LEDs**: an always-full-height "unlit" layer drawn behind each bar, with its own independent Solid/Gradient color mode separate from the front bar's — any combination works. Reuses the front's segment grid in LED style. Off by default.
- **Background**: flat or gradient
- **Grid lines**: on/off, color, spacing, opacity, Dashed/Solid. Dashes are computed as continuous full-width lines, never per-bar-column.
- **Channel mode**: Mono (combined) or Stereo (side-by-side L/R, fully independent rendering state per channel)
- **Margins**: top/bottom/left/right, pixel-based. In Stereo mode the left/right margins carry a second meaning simultaneously — they inset the outer edges *and* define the spacing either side of the separator. There is no separate channel-gap setting.
- **Border**: on/off, color, thickness. One rectangle in Mono, two in Stereo (one per channel).
- **Separator**: Stereo only. Spans the full window height, deliberately ignoring top and bottom margins. Independent of the border settings.
- **Horizontal fit invariant**: bar count × effective width + spacing never exceeds the drawable width. Enforced at render time by reducing spacing toward a floor of 1, then reducing bar count. Clamp-only — stored settings are never modified. A clipping backstop provides defense in depth.
- Double-buffered GDI throughout.

---

## Peak markers

Color, thickness (Smooth style) or height in segments (LED style), plus optional smooth motion for LED style — a continuous float position instead of segment-snapped, using thickness rather than segment height.

### Fall physics

**Constant** mode falls at a fixed speed in px/s. **Gravity** mode (default) accelerates: velocity starts at zero and increases by the gravity constant each second, which reads as natural motion rather than the mechanical straight-line descent of a fixed speed.

Both modes support a **hold time** — the marker hangs briefly at a new peak before beginning to fall. The hold does as much work as the acceleration in making the display legible; it's what makes a marker read as having *caught* something.

Velocity is internally clamped to a terminal value scaled to the drawable height, so a marker falling a 2160-pixel window doesn't reach absurd speed before landing. Not a user setting.

In LED style, physics always run in continuous float space; the segment snap happens at render time only.

Rates are in px/s and px/s², not per frame, so behavior is identical regardless of the frame rate the plugin happens to achieve.

Deliberately rejected: bounce on landing, overshoot on re-trigger, easing on the hold-to-fall transition. Also rejected project-wide: glow, blur, reflection, rainbow-per-bar coloring.

---

## Settings dialog

Built on AIMP's native `IAIMPUIForm` / `IAIMPUIControl` framework rather than Win32, so it follows the active AIMP skin instead of default system styling. Fixed size, with a scrollbar fallback if a tab's content overflows.

**Tabs**: About, System, Scaling, Bars, Peaks, Background, Layout — About is index 0 and opens by default. Settings are organized into bordered, captioned group boxes with a shared two-column form grid.

### About

Read-only. Name, version, author, forum topic (as a clickable link), and description — all sourced from the same single source of truth as `AuraBars.txt`, so they cannot drift.

### System

- **Presets** — five built-in presets: Pandemic (factory default), Cygnus, AIMP, WinAmp, Science. Stored as settings payloads parsed through the same code path as pasted settings, so there is no separate preset format to keep in sync. *Reset to Defaults* loads Pandemic.
- **Settings Transfer** — copy the current dialog state to the clipboard as an INI-style snapshot, or paste one back. The copy reflects live control state including unapplied edits, not the last-persisted config. Every setting is included whether or not it's currently relevant, since an incomplete snapshot is useless in a bug report. Unknown keys are discarded on paste.
- **Diagnostics** — debug logging toggle. The clipboard payload also carries an environment block (skin name, display dimensions, drawable rect, effective bar geometry, DPI, AIMP version), `Env`-prefixed so it's discarded harmlessly on paste.

Channel mode and debug logging are permanently excluded from preset and paste loading.

### Debug logging

Once per second to `%TEMP%\AuraBars_debug.log`: three **frequency-anchored** reference bars — nearest 60 Hz, 1 kHz, and 8 kHz — each logging its index, center frequency, and both pre- and post-tilt magnitude and dB. Fixed bar indices were abandoned because "bar 20" is a different frequency at every bar count, making samples incomparable across configurations.

The effective bar count after fit-invariant clamping is logged, not the stored setting. Logged dB values are unclamped conversion output, so values outside the floor/ceiling range are expected and are not evidence of render clipping.

---

## Build

MSVC (x86 + x64), `vcvarsall.bat` environment required per architecture.

- `build.bat` → `build/x86/AuraBars.dll` and `build/x64/AuraBars.dll` (both named `AuraBars.dll`, distinguished only by folder)
- Packaging runs automatically on a successful build, producing `AuraBars.aimppack` with the x86 DLL at the package root and the x64 DLL in an `x64` subfolder, alongside the generated `AuraBars.txt` and the hand-maintained `ReadMe.txt`
- `deploy.bat` closes AIMP, installs the DLL, reports source and destination timestamps so a stale install is immediately visible, and relaunches

Version lives in exactly one place — `src/Version.h` plus an auto-generated `src/BuildNumber.h`, where the build number is the clock time of the build. The About tab, `AuraBars.txt`, and the packaging script all read from there, so they can never drift.

`ReadMe.txt` is hand-maintained and is never generated, templated, or overwritten by the build.

## Install

**Via package**: double-click `AuraBars.aimppack` — AIMP handles architecture selection automatically.

**During development**: `deploy.bat` (requires elevation).

---

## Known issues

- **Stereo + 128 bars at 3840×2160 renders at ~6.6 fps.** The cost is bar and segment count rather than the blit — the same resolution at lower bar counts hits 29 fps. At 128 bars across two channels on a 2160-pixel column of LED segments, the per-frame `FillRect` count runs to tens of thousands, doubling again with background bars enabled.
- **Fit-invariant clamping is invisible.** A stored bar count of 128 can render as 77 in a narrow window with no indication in the dialog.

## TODO

- Bottom panel
