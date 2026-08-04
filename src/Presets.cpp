#include "Presets.h"

namespace
{
    // Verbatim clipboard-payload artifacts (Code Contract §3, "Settings
    // presets"): each is embedded byte-for-byte from the supplied
    // preset-*.txt file, including its own captured "[Environment]" block
    // - left in deliberately, since the reserved "Env" prefix already makes
    // ParseSettings discard it, and stripping it by hand would reintroduce
    // the manual editing this design exists to avoid. Never hand-edit
    // these literals; re-embed from the source file if a preset changes.
    const wchar_t* kPandemicPayload =
LR"PRESET(
; AuraBars settings snapshot
; Version: 2.3.1322
; Build: x64
; Generated: 2026-07-28 14:12:47

[Environment]
EnvSkin=Pandemic.acs5
EnvDisplayWidth=164
EnvDisplayHeight=75
EnvDrawableWidth=162
EnvDrawableHeight=73
EnvEffectiveBarWidth=8
EnvEffectiveBarCount=18
EnvEffectiveSegments=14
EnvDpi=96
EnvAimpVersion=v5.40.2703

[System]
DebugLogging=false

[Scaling]
MinFrequency=35
MaxFrequency=16000
BinAggregation=Peak
SpectralTilt=3.0
AmplitudeScaling=Logarithmic
dBFloor=-40.0
dBCeiling=0.0
AutoGainCeiling=false
BarSmoothing=true
BarSmoothingAttack=0.9
BarSmoothingDecay=0.2
NumberOfBars=18

[Bars]
BarStyle=LED
BarColorMode=Gradient
BarColor=#8C1200
BarGradientTop=#D90000
BarGradientBottom=#970000
BarSpacing=1
SegmentGap=1
SegmentHeight=4

[Peaks]
PeakMarkers=true
PeakMarkerColor=#FE7A2C
PeakFallSpeed=1.5
PeakMarkerThickness=1
PeakMarkerHeightSegments=1
PeakSmoothMotion=true
[Background]
BackgroundStyle=Flat
BackgroundColor=#0A0A0A
GridLines=false
GridLineColor=#303030
GridLineSpacing=24
GridLineOpacity=100
GridLineStyle=Dashed
BackgroundBarsEnabled=false
BackgroundBarColorMode=Gradient
BackgroundBarColor=#3A2A1A
BackgroundBarGradientTop=#4A3A22
BackgroundBarGradientBottom=#2A1A18

[Layout]
ChannelMode=Mono
TopMargin=1
BottomMargin=1
LeftMargin=1
RightMargin=1
BorderEnabled=false
BorderColor=#303030
BorderThickness=1
SeparatorEnabled=true
SeparatorColor=#B0691C
SeparatorThickness=1
)PRESET";

    const wchar_t* kCygnusPayload =
LR"PRESET(
; AuraBars settings snapshot
; Version: 2.3.1322
; Build: x64
; Generated: 2026-07-28 14:21:42

[Environment]
EnvSkin=Bliss 4K.acs5
EnvDisplayWidth=1415
EnvDisplayHeight=373
EnvDrawableWidth=1395
EnvDrawableHeight=353
EnvEffectiveBarWidth=62
EnvEffectiveBarCount=20
EnvEffectiveSegments=58
EnvDpi=96
EnvAimpVersion=v5.40.2703

[System]
DebugLogging=false

[Scaling]
MinFrequency=35
MaxFrequency=16000
BinAggregation=Peak
SpectralTilt=3.0
AmplitudeScaling=Logarithmic
dBFloor=-40.0
dBCeiling=0.0
AutoGainCeiling=false
BarSmoothing=true
BarSmoothingAttack=0.9
BarSmoothingDecay=0.2
NumberOfBars=20

[Bars]
BarStyle=LED
BarColorMode=Gradient
BarColor=#8C1200
BarGradientTop=#AE0000
BarGradientBottom=#880000
BarSpacing=8
SegmentGap=2
SegmentHeight=4

[Peaks]
PeakMarkers=true
PeakMarkerColor=#F71B02
PeakFallSpeed=1.5
PeakMarkerThickness=3
PeakMarkerHeightSegments=1
PeakSmoothMotion=false

[Background]
BackgroundStyle=Flat
BackgroundColor=#0A0A0A
GridLines=false
GridLineColor=#303030
GridLineSpacing=24
GridLineOpacity=100
GridLineStyle=Dashed
BackgroundBarsEnabled=true
BackgroundBarColorMode=Gradient
BackgroundBarColor=#3A2A1A
BackgroundBarGradientTop=#2F0D0D
BackgroundBarGradientBottom=#2D0202

[Layout]
ChannelMode=Mono
TopMargin=8
BottomMargin=8
LeftMargin=8
RightMargin=8
BorderEnabled=true
BorderColor=#303030
BorderThickness=2
SeparatorEnabled=true
SeparatorColor=#B0691C
SeparatorThickness=1

)PRESET";

    const wchar_t* kAimpPayload =
LR"PRESET(
; AuraBars settings snapshot
; Version: 2.3.1322
; Build: x64
; Generated: 2026-07-28 14:34:36

[Environment]
EnvSkin=Pandemic.acs5
EnvDisplayWidth=164
EnvDisplayHeight=75
EnvDrawableWidth=144
EnvDrawableHeight=55
EnvEffectiveBarWidth=7
EnvEffectiveBarCount=18
EnvEffectiveSegments=3
EnvDpi=96
EnvAimpVersion=v5.40.2703

[System]
DebugLogging=false

[Scaling]
MinFrequency=35
MaxFrequency=16000
BinAggregation=Peak
SpectralTilt=3.0
AmplitudeScaling=Logarithmic
dBFloor=-40.0
dBCeiling=0.0
AutoGainCeiling=false
BarSmoothing=true
BarSmoothingAttack=0.9
BarSmoothingDecay=0.2
NumberOfBars=34

[Bars]
BarStyle=LED
BarColorMode=Gradient
BarColor=#8C1200
BarGradientTop=#F4D1AB
BarGradientBottom=#FF9F40
BarSpacing=1
SegmentGap=1
SegmentHeight=16

[Peaks]
PeakMarkers=true
PeakMarkerColor=#FEFFFF
PeakFallSpeed=1.5
PeakMarkerThickness=3
PeakMarkerHeightSegments=1
PeakSmoothMotion=true

[Background]
BackgroundStyle=Flat
BackgroundColor=#0A0A0A
GridLines=false
GridLineColor=#303030
GridLineSpacing=24
GridLineOpacity=100
GridLineStyle=Dashed
BackgroundBarsEnabled=true
BackgroundBarColorMode=Gradient
BackgroundBarColor=#3A2A1A
BackgroundBarGradientTop=#382310
BackgroundBarGradientBottom=#221300

[Layout]
ChannelMode=Mono
TopMargin=8
BottomMargin=8
LeftMargin=8
RightMargin=8
BorderEnabled=true
BorderColor=#303030
BorderThickness=2
SeparatorEnabled=true
SeparatorColor=#B0691C
SeparatorThickness=1

)PRESET";

    const wchar_t* kWinAmpPayload =
LR"PRESET(
; AuraBars settings snapshot
; Version: 2.3.1322
; Build: x64
; Generated: 2026-07-28 14:48:02

[Environment]
EnvSkin=Pandemic.acs5
EnvDisplayWidth=164
EnvDisplayHeight=75
EnvDrawableWidth=144
EnvDrawableHeight=55
EnvEffectiveBarWidth=7
EnvEffectiveBarCount=18
EnvEffectiveSegments=3
EnvDpi=96
EnvAimpVersion=v5.40.2703

[System]
DebugLogging=false

[Scaling]
MinFrequency=35
MaxFrequency=16000
BinAggregation=Peak
SpectralTilt=3.0
AmplitudeScaling=Logarithmic
dBFloor=-40.0
dBCeiling=0.0
AutoGainCeiling=false
BarSmoothing=true
BarSmoothingAttack=0.9
BarSmoothingDecay=0.2
NumberOfBars=20

[Bars]
BarStyle=Smooth
BarColorMode=Gradient
BarColor=#8C1200
BarGradientTop=#D61A00
BarGradientBottom=#1DB010
BarSpacing=1
SegmentGap=1
SegmentHeight=16

[Peaks]
PeakMarkers=false
PeakMarkerColor=#FEFFFF
PeakFallSpeed=1.5
PeakMarkerThickness=2
PeakMarkerHeightSegments=1
PeakSmoothMotion=true

[Background]
BackgroundStyle=Flat
BackgroundColor=#0A0A0A
GridLines=false
GridLineColor=#303030
GridLineSpacing=24
GridLineOpacity=100
GridLineStyle=Dashed
BackgroundBarsEnabled=false
BackgroundBarColorMode=Gradient
BackgroundBarColor=#3A2A1A
BackgroundBarGradientTop=#382310
BackgroundBarGradientBottom=#221300

[Layout]
ChannelMode=Mono
TopMargin=8
BottomMargin=8
LeftMargin=8
RightMargin=8
BorderEnabled=true
BorderColor=#303030
BorderThickness=2
SeparatorEnabled=true
SeparatorColor=#B0691C
SeparatorThickness=1

)PRESET";

    const wchar_t* kSciencePayload =
LR"PRESET(
; AuraBars settings snapshot
; Version: 2.3.1322
; Build: x64
; Generated: 2026-07-28 15:01:42

[Environment]
EnvSkin=Pandemic.acs5
EnvDisplayWidth=164
EnvDisplayHeight=75
EnvDrawableWidth=158
EnvDrawableHeight=69
EnvEffectiveBarWidth=7
EnvEffectiveBarCount=17
EnvEffectiveSegments=6
EnvDpi=96
EnvAimpVersion=v5.40.2703

[System]
DebugLogging=false

[Scaling]
MinFrequency=35
MaxFrequency=16000
BinAggregation=Peak
SpectralTilt=3.0
AmplitudeScaling=Logarithmic
dBFloor=-40.0
dBCeiling=0.0
AutoGainCeiling=false
BarSmoothing=true
BarSmoothingAttack=0.9
BarSmoothingDecay=0.2
NumberOfBars=20

[Bars]
BarStyle=LED
BarColorMode=Solid
BarColor=#C0C0C0
BarGradientTop=#D61A00
BarGradientBottom=#1DB010
BarSpacing=2
SegmentGap=2
SegmentHeight=8

[Peaks]
PeakMarkers=true
PeakMarkerColor=#000000
PeakFallSpeed=1.5
PeakMarkerThickness=2
PeakMarkerHeightSegments=1
PeakSmoothMotion=true

[Background]
BackgroundStyle=Flat
BackgroundColor=#FFFFFF
GridLines=true
GridLineColor=#838383
GridLineSpacing=24
GridLineOpacity=100
GridLineStyle=Dashed
BackgroundBarsEnabled=false
BackgroundBarColorMode=Gradient
BackgroundBarColor=#3A2A1A
BackgroundBarGradientTop=#382310
BackgroundBarGradientBottom=#221300

[Layout]
ChannelMode=Mono
TopMargin=2
BottomMargin=2
LeftMargin=2
RightMargin=2
BorderEnabled=true
BorderColor=#303030
BorderThickness=1
SeparatorEnabled=true
SeparatorColor=#B0691C
SeparatorThickness=1
)PRESET";
}

const PresetInfo kPresets[5] = {
    { L"Pandemic", kPandemicPayload },
    { L"Cygnus", kCygnusPayload },
    { L"AIMP", kAimpPayload },
    { L"WinAmp", kWinAmpPayload },
    { L"Science", kSciencePayload },
};
