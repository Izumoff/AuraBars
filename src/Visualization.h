#pragma once

#include "Common.h"
#include "Settings.h"
#include <atomic>
#include <vector>

// IAIMPExtensionEmbeddedVisualization implementation: an LED/Smooth
// spectrum-bar meter. Reads settings from the shared g_Settings instance
// on every Draw call, so changes made in the options dialog take effect
// on the next frame with no explicit reload.
class CVisualization : public IAIMPExtensionEmbeddedVisualization
{
public:
    CVisualization() = default;
    ~CVisualization();

    // IUnknown
    HRESULT WINAPI QueryInterface(REFIID iid, void** obj) override;
    ULONG WINAPI AddRef() override;
    ULONG WINAPI Release() override;

    // IAIMPExtensionEmbeddedVisualization
    int WINAPI GetFlags() override;
    HRESULT WINAPI GetMaxDisplaySize(int* Width, int* Height) override;
    HRESULT WINAPI GetName(IAIMPString** S) override;
    HRESULT WINAPI Initialize(int Width, int Height) override;
    void WINAPI Finalize() override;
    void WINAPI Click(int X, int Y, int Button) override;
    void WINAPI Draw(HCANVAS Canvas, PAIMPVisualData Data) override;
    void WINAPI Resize(int NewWidth, int NewHeight) override;

private:
    void EnsureBackBuffer();
    void ReleaseBackBuffer();

    void DrawBackground(HDC dc, const RECT& area);
    void DrawGrid(HDC dc, const RECT& area);
    void DrawBars(HDC dc, const RECT& area, const TAIMPVisualData* data);
    void DrawLedBar(HDC dc, const RECT& barRect, const RECT& area);
    void DrawSmoothBar(HDC dc, const RECT& barRect, const RECT& area);
    void DrawPeakMarker(HDC dc, const RECT& barRect, const RECT& area, float peakTopY);
    COLORREF ColorAtHeight(COLORREF top, COLORREF bottom, int y, const RECT& area) const;
    void UpdateAutoGainCeiling(float& ceilingDb, float currentDb);
    void LogDebugSample(int firstIdx, float firstRaw, float firstDb,
                         int midIdx, float midRaw, float midDb,
                         int lastIdx, float lastRaw, float lastDb) const;

    std::atomic<long> m_RefCount{1};

    int m_Width = 0;
    int m_Height = 0;

    HDC m_MemDC = nullptr;
    HBITMAP m_MemBitmap = nullptr;
    HBITMAP m_OldBitmap = nullptr;

    std::vector<float> m_PeakY;             // absolute pixel Y (device coords) of each bar's peak marker
    std::vector<float> m_AutoGainCeilingDb; // per-bar running adaptive ceiling (dB), fast attack / slow release
    ULONGLONG m_LastDebugLogTick = 0;       // GetTickCount64() at last debug-log write, for the 1/sec cadence
};
