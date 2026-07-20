#pragma once

#include "Common.h"

enum class BarStyle { LED = 0, Smooth = 1 };
enum class ColorMode { Solid = 0, Gradient = 1 };
enum class BackgroundStyle { Flat = 0, Gradient = 1 };
enum class AmplitudeScaling { Linear = 0, Logarithmic = 1 };
enum class GridLineStyle { Dashed = 0, Solid = 1 };
enum class ChannelMode { Mono = 0, Stereo = 1 };

struct AuraBarsSettings
{
    int barCount = 20;                                  // 8-128
    AmplitudeScaling amplitudeScaling = AmplitudeScaling::Logarithmic;
    double dbFloor = -40.0;                            // dB, -100.0 to -20.0, Logarithmic only
    double dbCeiling = 0.0;                            // dB, -20.0 to 0.0, Logarithmic only
    bool autoGainCeiling = false;                      // Logarithmic only; overrides dbCeiling with an adaptive value
    bool barSmoothing = true;                          // smooths raw magnitude before the Linear/Log scaling branch
    double barSmoothingAttack = 0.9;                   // 0.0-1.0, 1.0 = instant (no smoothing) on rising values
    double barSmoothingDecay = 0.2;                    // 0.0-1.0, 1.0 = instant (no smoothing) on falling values
    BarStyle barStyle = BarStyle::LED;
    ColorMode barColorMode = ColorMode::Gradient;
    COLORREF barColorSolid = RGB(0x8C, 0x12, 0x00);
    COLORREF barGradientTop = RGB(0xD9, 0x00, 0x00);
    COLORREF barGradientBottom = RGB(0x97, 0x00, 0x00);
    BackgroundStyle backgroundStyle = BackgroundStyle::Flat;
    COLORREF backgroundColor = RGB(0x0A, 0x0A, 0x0A);
    bool gridLines = false;
    COLORREF gridLineColor = RGB(0x30, 0x30, 0x30);
    int gridLineSpacing = 24;                          // px between grid lines, 8-100
    int gridLineOpacity = 100;                          // %, 0-100
    GridLineStyle gridLineStyle = GridLineStyle::Dashed;
    bool peakMarkers = true;
    COLORREF peakMarkerColor = RGB(0xFD, 0x42, 0x2D);
    double peakFallSpeed = 1.5;                        // px/frame, 0.1-10.0
    int peakMarkerThickness = 3;                       // px, 1-10, Smooth only
    int peakMarkerHeightSegments = 1;                  // segments, 1-3, LED only
    int barSpacing = 8;                                // px, 0-20
    int segmentGap = 2;                                // px, 1-8, LED only
    int segmentHeight = 4;                             // px, 4-20, LED only
    bool backgroundBarsEnabled = false;                // always-full-height "unlit LED" layer drawn behind the front bar
    ColorMode backgroundBarColorMode = ColorMode::Gradient;
    COLORREF backgroundBarColorSolid = RGB(0x3A, 0x2A, 0x1A);
    COLORREF backgroundBarGradientTop = RGB(0x4A, 0x3A, 0x22);
    COLORREF backgroundBarGradientBottom = RGB(0x2A, 0x1A, 0x18);
    bool debugLogging = false;                         // once/sec, writes sample bars' raw+dB values to %TEMP%\AuraBars_debug.log
    ChannelMode channelMode = ChannelMode::Mono;
    int topMargin = 8;                                 // px, 0-100 (was percent-based; old saved value intentionally NOT migrated)
    int bottomMargin = 8;                              // px, 0-100
    int leftMargin = 8;                                // px, 0-100; Mono: left edge of the spectrum. Stereo: left edge of the Left channel, AND gap between the Left channel's right edge and the Separator
    int rightMargin = 8;                               // px, 0-100; Mono: right edge of the spectrum. Stereo: gap between the Separator and the Right channel's left edge, AND right edge of the Right channel
    bool borderEnabled = true;
    COLORREF borderColor = RGB(0x30, 0x30, 0x30);
    int borderThickness = 2;                           // px, 1-10
    bool separatorEnabled = true;                       // Stereo mode only
    COLORREF separatorColor = RGB(0xB0, 0x69, 0x1C);
    int separatorThickness = 2;                        // px, 1-10
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
