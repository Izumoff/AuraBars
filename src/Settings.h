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
    double dbFloor = -40.0;                            // dB, -100.0 to -20.0, Logarithmic only
    double dbCeiling = 0.0;                            // dB, -20.0 to 0.0, Logarithmic only
    bool autoGainCeiling = false;                      // Logarithmic only; overrides dbCeiling with an adaptive value
    BarStyle barStyle = BarStyle::LED;
    ColorMode barColorMode = ColorMode::Gradient;
    COLORREF barColorSolid = RGB(0x8C, 0x12, 0x00);
    COLORREF barGradientTop = RGB(0xD9, 0x00, 0x00);
    COLORREF barGradientBottom = RGB(0x97, 0x00, 0x00);
    BackgroundStyle backgroundStyle = BackgroundStyle::Flat;
    COLORREF backgroundColor = RGB(0x0A, 0x0A, 0x0A);
    bool gridLines = false;
    COLORREF gridLineColor = RGB(0x30, 0x30, 0x30);
    bool peakMarkers = true;
    COLORREF peakMarkerColor = RGB(0xFD, 0x42, 0x2D);
    double peakFallSpeed = 1.5;                        // px/frame, 0.1-10.0
    int peakMarkerThickness = 3;                       // px, 1-10, Smooth only
    int peakMarkerHeightSegments = 1;                  // segments, 1-3, LED only
    int barSpacing = 8;                                // px, 0-20
    int topMarginPercent = 0;                          // %, 0-30, headroom above bars
    int segmentGap = 2;                                // px, 1-8, LED only
    int segmentHeight = 4;                             // px, 4-20, LED only
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
