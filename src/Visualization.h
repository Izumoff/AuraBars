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
    // One bar's fully-resolved geometry and audio data for this frame,
    // computed once up front so both the grid pass and the bar-drawing
    // pass can use the same values without recomputing spectrum math.
    struct BarLayout
    {
        RECT rect;      // left/right/bottom fixed; top = current amplitude-based top
        float rawValue;
        float rawDb;
        float peakY;    // this frame's updated peak-marker Y, device px
    };

    void EnsureBackBuffer();
    void ReleaseBackBuffer();

    void DrawBackground(HDC dc, const RECT& area);
    COLORREF BackgroundColorAtY(int y, const RECT& area) const;
    // spectrumChannel: -1 averages Spectrum[0]/[1] (Mono); 0 or 1 reads a
    // single real channel (Stereo). peakState/autoGainState/smoothedState
    // are the per-bar running state to read and update - Mono and
    // Stereo-left share m_PeakYLeft/m_AutoGainCeilingDbLeft/m_SmoothedLeft,
    // Stereo-right uses its own set, so switching channel modes or having
    // differently-loud L/R content never cross-contaminates decay state
    // between them.
    void ComputeBarLayout(const RECT& area, const TAIMPVisualData* data, int spectrumChannel,
                           std::vector<float>& peakState, std::vector<float>& autoGainState,
                           std::vector<float>& smoothedState,
                           bool emitDebugLog, std::vector<BarLayout>& out);
    // bounds: the region grid lines are actually drawn/confined within
    // (a channel's inner rect). backgroundRef: the full panel rect, used
    // only so BackgroundColorAtY's gradient interpolation matches what
    // DrawBackground actually painted, independent of how narrow bounds is.
    void DrawGrid(HDC dc, const RECT& bounds, const RECT& backgroundRef, const std::vector<BarLayout>& bars);
    void DrawBarVisuals(HDC dc, const RECT& area, const std::vector<BarLayout>& bars);
    // Always-full-height "unlit LED" layer, drawn before the front bar so
    // the front's normal draw naturally covers everything below the
    // current level, leaving this visible only above it.
    void DrawBackgroundBarLayer(HDC dc, const RECT& barRect, const RECT& area);
    void DrawBorderFrame(HDC dc, const RECT& rect, COLORREF color, int thickness);
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

    // Mono uses the "Left" pair as its one and only channel's state;
    // Stereo uses both pairs independently for its two channel halves.
    std::vector<float> m_PeakYLeft;             // absolute pixel Y (device coords) of each bar's peak marker
    std::vector<float> m_AutoGainCeilingDbLeft; // per-bar running adaptive ceiling (dB), fast attack / slow release
    // Unlike m_PeakY*, this holds an audio magnitude, not a device pixel
    // coordinate, so it stays valid across Resize()/Initialize() and is
    // deliberately never cleared there - clearing it would reintroduce an
    // attack-ramp transient on every window resize for no reason.
    std::vector<float> m_SmoothedLeft;
    std::vector<float> m_PeakYRight;
    std::vector<float> m_AutoGainCeilingDbRight;
    std::vector<float> m_SmoothedRight;
    ULONGLONG m_LastDebugLogTick = 0;       // GetTickCount64() at last debug-log write, for the 1/sec cadence
};
