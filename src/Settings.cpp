#include "Settings.h"
#include <algorithm>

AuraBarsSettings g_Settings;

namespace
{
    const wchar_t* kKeyBarCount              = L"AuraBars\\BarCount";
    const wchar_t* kKeyAmplitudeScaling      = L"AuraBars\\AmplitudeScaling";
    const wchar_t* kKeyDbFloor               = L"AuraBars\\DbFloor";
    const wchar_t* kKeyDbCeiling             = L"AuraBars\\DbCeiling";
    const wchar_t* kKeyAutoGainCeiling       = L"AuraBars\\AutoGainCeiling";
    const wchar_t* kKeyBarStyle              = L"AuraBars\\BarStyle";
    const wchar_t* kKeyBarColorMode          = L"AuraBars\\BarColorMode";
    const wchar_t* kKeyBarColorSolid         = L"AuraBars\\BarColorSolid";
    const wchar_t* kKeyBarGradientTop        = L"AuraBars\\BarGradientTop";
    const wchar_t* kKeyBarGradientBottom     = L"AuraBars\\BarGradientBottom";
    const wchar_t* kKeyBackgroundStyle       = L"AuraBars\\BackgroundStyle";
    const wchar_t* kKeyBackgroundColor       = L"AuraBars\\BackgroundColor";
    const wchar_t* kKeyGridLines             = L"AuraBars\\GridLines";
    const wchar_t* kKeyGridLineColor         = L"AuraBars\\GridLineColor";
    const wchar_t* kKeyGridLineSpacing       = L"AuraBars\\GridLineSpacing";
    const wchar_t* kKeyGridLineOpacity       = L"AuraBars\\GridLineOpacity";
    const wchar_t* kKeyGridLineStyle         = L"AuraBars\\GridLineStyle";
    const wchar_t* kKeyPeakMarkers           = L"AuraBars\\PeakMarkers";
    const wchar_t* kKeyPeakMarkerColor       = L"AuraBars\\PeakMarkerColor";
    const wchar_t* kKeyPeakFallSpeed         = L"AuraBars\\PeakFallSpeed";
    const wchar_t* kKeyPeakMarkerThickness   = L"AuraBars\\PeakMarkerThickness";
    const wchar_t* kKeyPeakMarkerHeightSegs  = L"AuraBars\\PeakMarkerHeightSegments";
    const wchar_t* kKeyBarSpacing            = L"AuraBars\\BarSpacing";
    const wchar_t* kKeySegmentGap            = L"AuraBars\\SegmentGap";
    const wchar_t* kKeySegmentHeight         = L"AuraBars\\SegmentHeight";
    const wchar_t* kKeyTopMarginPercent      = L"AuraBars\\TopMarginPercent";
    const wchar_t* kKeyDebugLogging          = L"AuraBars\\DebugLogging";

    int ReadInt(IAIMPServiceConfig* cfg, const wchar_t* key, int def)
    {
        AutoAIMPString k(MakeAIMPString(key));
        int value = def;
        if (!k.Get() || FAILED(cfg->GetValueAsInt32(k.Get(), &value)))
            return def;
        return value;
    }

    double ReadFloat(IAIMPServiceConfig* cfg, const wchar_t* key, double def)
    {
        AutoAIMPString k(MakeAIMPString(key));
        double value = def;
        if (!k.Get() || FAILED(cfg->GetValueAsFloat(k.Get(), &value)))
            return def;
        return value;
    }

    void WriteInt(IAIMPServiceConfig* cfg, const wchar_t* key, int value)
    {
        AutoAIMPString k(MakeAIMPString(key));
        if (k.Get())
            cfg->SetValueAsInt32(k.Get(), value);
    }

    void WriteFloat(IAIMPServiceConfig* cfg, const wchar_t* key, double value)
    {
        AutoAIMPString k(MakeAIMPString(key));
        if (k.Get())
            cfg->SetValueAsFloat(k.Get(), value);
    }
}

void ClampSettings(AuraBarsSettings& s)
{
    s.barCount = std::clamp(s.barCount, 8, 128);
    s.dbFloor = std::clamp(s.dbFloor, -100.0, -20.0);
    s.dbCeiling = std::clamp(s.dbCeiling, -20.0, 0.0);
    if (s.dbCeiling <= s.dbFloor)
        s.dbCeiling = s.dbFloor + 1.0;
    s.peakFallSpeed = std::clamp(s.peakFallSpeed, 0.1, 10.0);
    s.peakMarkerThickness = std::clamp(s.peakMarkerThickness, 1, 10);
    s.peakMarkerHeightSegments = std::clamp(s.peakMarkerHeightSegments, 1, 3);
    s.barSpacing = std::clamp(s.barSpacing, 0, 20);
    s.segmentGap = std::clamp(s.segmentGap, 1, 8);
    s.segmentHeight = std::clamp(s.segmentHeight, 4, 20);
    s.topMarginPercent = std::clamp(s.topMarginPercent, 0, 30);
    s.gridLineSpacing = std::clamp(s.gridLineSpacing, 8, 100);
    s.gridLineOpacity = std::clamp(s.gridLineOpacity, 0, 100);
}

void LoadSettings(IAIMPServiceConfig* cfg, AuraBarsSettings& s)
{
    if (!cfg)
        return;

    AuraBarsSettings def; // defaults to fall back on

    s.barCount = ReadInt(cfg, kKeyBarCount, def.barCount);
    s.amplitudeScaling = (AmplitudeScaling)ReadInt(cfg, kKeyAmplitudeScaling, (int)def.amplitudeScaling);
    s.dbFloor = ReadFloat(cfg, kKeyDbFloor, def.dbFloor);
    s.dbCeiling = ReadFloat(cfg, kKeyDbCeiling, def.dbCeiling);
    s.autoGainCeiling = ReadInt(cfg, kKeyAutoGainCeiling, def.autoGainCeiling ? 1 : 0) != 0;
    s.barStyle = (BarStyle)ReadInt(cfg, kKeyBarStyle, (int)def.barStyle);
    s.barColorMode = (ColorMode)ReadInt(cfg, kKeyBarColorMode, (int)def.barColorMode);
    s.barColorSolid = (COLORREF)ReadInt(cfg, kKeyBarColorSolid, (int)def.barColorSolid);
    s.barGradientTop = (COLORREF)ReadInt(cfg, kKeyBarGradientTop, (int)def.barGradientTop);
    s.barGradientBottom = (COLORREF)ReadInt(cfg, kKeyBarGradientBottom, (int)def.barGradientBottom);
    s.backgroundStyle = (BackgroundStyle)ReadInt(cfg, kKeyBackgroundStyle, (int)def.backgroundStyle);
    s.backgroundColor = (COLORREF)ReadInt(cfg, kKeyBackgroundColor, (int)def.backgroundColor);
    s.gridLines = ReadInt(cfg, kKeyGridLines, def.gridLines ? 1 : 0) != 0;
    s.gridLineColor = (COLORREF)ReadInt(cfg, kKeyGridLineColor, (int)def.gridLineColor);
    s.gridLineSpacing = ReadInt(cfg, kKeyGridLineSpacing, def.gridLineSpacing);
    s.gridLineOpacity = ReadInt(cfg, kKeyGridLineOpacity, def.gridLineOpacity);
    s.gridLineStyle = (GridLineStyle)ReadInt(cfg, kKeyGridLineStyle, (int)def.gridLineStyle);
    s.peakMarkers = ReadInt(cfg, kKeyPeakMarkers, def.peakMarkers ? 1 : 0) != 0;
    s.peakMarkerColor = (COLORREF)ReadInt(cfg, kKeyPeakMarkerColor, (int)def.peakMarkerColor);
    s.peakFallSpeed = ReadFloat(cfg, kKeyPeakFallSpeed, def.peakFallSpeed);
    s.peakMarkerThickness = ReadInt(cfg, kKeyPeakMarkerThickness, def.peakMarkerThickness);
    s.peakMarkerHeightSegments = ReadInt(cfg, kKeyPeakMarkerHeightSegs, def.peakMarkerHeightSegments);
    s.barSpacing = ReadInt(cfg, kKeyBarSpacing, def.barSpacing);
    s.segmentGap = ReadInt(cfg, kKeySegmentGap, def.segmentGap);
    s.segmentHeight = ReadInt(cfg, kKeySegmentHeight, def.segmentHeight);
    s.topMarginPercent = ReadInt(cfg, kKeyTopMarginPercent, def.topMarginPercent);
    s.debugLogging = ReadInt(cfg, kKeyDebugLogging, def.debugLogging ? 1 : 0) != 0;

    ClampSettings(s);
}

void SaveSettings(IAIMPServiceConfig* cfg, const AuraBarsSettings& s)
{
    if (!cfg)
        return;

    WriteInt(cfg, kKeyBarCount, s.barCount);
    WriteInt(cfg, kKeyAmplitudeScaling, (int)s.amplitudeScaling);
    WriteFloat(cfg, kKeyDbFloor, s.dbFloor);
    WriteFloat(cfg, kKeyDbCeiling, s.dbCeiling);
    WriteInt(cfg, kKeyAutoGainCeiling, s.autoGainCeiling ? 1 : 0);
    WriteInt(cfg, kKeyBarStyle, (int)s.barStyle);
    WriteInt(cfg, kKeyBarColorMode, (int)s.barColorMode);
    WriteInt(cfg, kKeyBarColorSolid, (int)s.barColorSolid);
    WriteInt(cfg, kKeyBarGradientTop, (int)s.barGradientTop);
    WriteInt(cfg, kKeyBarGradientBottom, (int)s.barGradientBottom);
    WriteInt(cfg, kKeyBackgroundStyle, (int)s.backgroundStyle);
    WriteInt(cfg, kKeyBackgroundColor, (int)s.backgroundColor);
    WriteInt(cfg, kKeyGridLines, s.gridLines ? 1 : 0);
    WriteInt(cfg, kKeyGridLineColor, (int)s.gridLineColor);
    WriteInt(cfg, kKeyGridLineSpacing, s.gridLineSpacing);
    WriteInt(cfg, kKeyGridLineOpacity, s.gridLineOpacity);
    WriteInt(cfg, kKeyGridLineStyle, (int)s.gridLineStyle);
    WriteInt(cfg, kKeyPeakMarkers, s.peakMarkers ? 1 : 0);
    WriteInt(cfg, kKeyPeakMarkerColor, (int)s.peakMarkerColor);
    WriteFloat(cfg, kKeyPeakFallSpeed, s.peakFallSpeed);
    WriteInt(cfg, kKeyPeakMarkerThickness, s.peakMarkerThickness);
    WriteInt(cfg, kKeyPeakMarkerHeightSegs, s.peakMarkerHeightSegments);
    WriteInt(cfg, kKeyBarSpacing, s.barSpacing);
    WriteInt(cfg, kKeySegmentGap, s.segmentGap);
    WriteInt(cfg, kKeySegmentHeight, s.segmentHeight);
    WriteInt(cfg, kKeyTopMarginPercent, s.topMarginPercent);
    WriteInt(cfg, kKeyDebugLogging, s.debugLogging ? 1 : 0);

    cfg->FlushCache();
}
