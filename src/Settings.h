#pragma once

#include "Common.h"

enum class BarStyle { LED = 0, Smooth = 1 };
enum class ColorMode { Solid = 0, Gradient = 1 };
enum class BackgroundStyle { Flat = 0, Gradient = 1 };
enum class AmplitudeScaling { Linear = 0, Logarithmic = 1 };

struct AuraBarsSettings
{
    int barCount = 40;                                 // 8-128
    AmplitudeScaling amplitudeScaling = AmplitudeScaling::Logarithmic;
    double dbFloor = -60.0;                            // dB, -100.0 to -20.0, Logarithmic only
    double dbCeiling = 0.0;                            // dB, -20.0 to 0.0, Logarithmic only
    bool autoGainCeiling = true;                       // Logarithmic only; overrides dbCeiling with an adaptive value
    BarStyle barStyle = BarStyle::LED;
    ColorMode barColorMode = ColorMode::Solid;
    COLORREF barColorSolid = RGB(0xD9, 0x70, 0x2C);
    COLORREF barGradientTop = RGB(0xE0, 0xB2, 0x3C);
    COLORREF barGradientBottom = RGB(0xC6, 0x2F, 0x2F);
    BackgroundStyle backgroundStyle = BackgroundStyle::Flat;
    COLORREF backgroundColor = RGB(0x0A, 0x0A, 0x0A);
    bool gridLines = true;
    COLORREF gridLineColor = RGB(0x33, 0x33, 0x33);
    bool peakMarkers = true;
    COLORREF peakMarkerColor = RGB(0xD8, 0xD8, 0xD8);
    double peakFallSpeed = 1.5;                        // px/frame, 0.1-10.0
    int peakMarkerThickness = 3;                       // px, 1-10, Smooth only
    int peakMarkerHeightSegments = 1;                  // segments, 1-3, LED only
    int barSpacing = 4;                                // px, 0-20
    int topMarginPercent = 0;                          // %, 0-30, headroom above bars
    int segmentGap = 2;                                // px, 1-8, LED only
    int segmentHeight = 8;                             // px, 4-20, LED only
    bool debugLogging = false;                         // once/sec, writes sample bars' raw+dB values to %TEMP%\AuraBars_debug.log
};

// Live settings shared by the visualization and the options dialog: the
// dialog writes here on Save and the renderer reads it every frame, so
// changes apply immediately without an explicit reload step.
extern AuraBarsSettings g_Settings;

// Clamps every field to the range from the spec; call after loading and
// after reading dialog controls so out-of-range/corrupt config can't crash
// the renderer.
void ClampSettings(AuraBarsSettings& s);

void LoadSettings(IAIMPServiceConfig* cfg, AuraBarsSettings& s);
void SaveSettings(IAIMPServiceConfig* cfg, const AuraBarsSettings& s);
