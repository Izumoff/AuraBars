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

    // Debug-logging reference bar (Code Contract §3): one of the three
    // frequency-anchored samples (~60Hz/1kHz/8kHz) LogDebugSample prints.
    // freqHz is the bar's *actual* sampled centre frequency - via
    // BinToFrequency on its real (post-quantization) bin range, not the
    // idealized continuous bar-edge midpoint - so what's printed matches
    // what was genuinely read from Spectrum[]. index is 0-based; pre/post
    // capture the aggregated magnitude immediately before and after the
    // spectral-tilt multiplier, since the two can differ substantially.
    struct DebugBarSample
    {
        int index = 0;
        double freqHz = 0.0;
        float preRaw = 0.0f, preDb = 0.0f;
        float postRaw = 0.0f, postDb = 0.0f;
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
    // dt: wall-clock seconds since the previous Draw() call (Code Contract
    // §7 step 2 / §1), measured once per Draw and passed down - never
    // re-measured per bar or per channel. Feeds the smoothing-coefficient
    // frame-rate conversion inside; see ConvertSmoothingCoefficient.
    void ComputeBarLayout(const RECT& area, const TAIMPVisualData* data, int spectrumChannel,
                           std::vector<float>& peakState, std::vector<float>& autoGainState,
                           std::vector<float>& smoothedState,
                           bool emitDebugLog, double dt, std::vector<BarLayout>& out);
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
    void LogDebugSample(const DebugBarSample& bass, const DebugBarSample& mid, const DebugBarSample& treble,
                         int effectiveBarCount) const;

    // TEMP DIAGNOSTIC - frame-cadence measurement, see Draw()'s call site.
    // Not part of the Code Contract; remove this method and its members
    // once the units question it exists to answer is settled. now: the
    // single QPC read Draw() already took this call for dt - reused here
    // rather than reading QueryPerformanceCounter a second time.
    void LogCadenceSample(LARGE_INTEGER now);

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
    // Effective bar count (post horizontal-fit-invariant) from the most
    // recently completed ComputeBarLayout call this session - i.e. the
    // real count actually rendered, not g_Settings.barCount. In Stereo this
    // is whichever region (left/right) computed last each frame; the two
    // are the same width by construction except for rare 1px rounding
    // differences, immaterial for a once/sec diagnostic. Read by
    // LogCadenceSample, which runs before this frame's ComputeBarLayout, so
    // it reflects the previous frame's value - up to one frame stale.
    int m_LastEffectiveBarCount = 0;

    // QueryPerformanceFrequency(), captured once in Initialize() and never
    // re-read (the OS guarantees it's fixed for the life of the process).
    // Permanent - shared by the frame-rate-independence dt below (always
    // active) and the TEMP DIAGNOSTIC cadence logging further down (only
    // when DebugLogging is on); do not remove this one alongside that block.
    LARGE_INTEGER m_QpcFrequency{};
    // Frame-rate independence (Code Contract §7 step 2 / §1): previous
    // Draw() call's QPC timestamp and whether one exists yet. Reset (via
    // m_HasLastFrameQpc = false) in Initialize() and Resize() - "no
    // previous timestamp" per the contract, not merely "a large dt".
    LARGE_INTEGER m_LastFrameQpc{};
    bool m_HasLastFrameQpc = false;

    // TEMP DIAGNOSTIC - frame-cadence measurement (see LogCadenceSample).
    // Remove this whole block (but not m_QpcFrequency above, which is now
    // permanent) once the units decision it's here to settle is made; none
    // of it belongs in the Code Contract.
    bool m_CadenceActive = false;           // mirrors g_Settings.debugLogging as observed by the cadence path; false->true is "just (re)started"
    LARGE_INTEGER m_LastDrawQpc{};          // QPC timestamp of the previous Draw() call
    // Wall-clock emission target: advanced by exactly one QPC-frequency's
    // worth of ticks (== 1000ms) each time a line is emitted, rather than
    // reset to "now" - so the once/sec cadence tracks real elapsed time
    // without drifting later every window from rounding/processing slack.
    LARGE_INTEGER m_CadenceNextEmitQpc{};
    int m_CadenceFrameCount = 0;
    double m_CadenceSumMs = 0.0;
    double m_CadenceSumSqMs = 0.0;
    double m_CadenceMinMs = 0.0;
    double m_CadenceMaxMs = 0.0;
    bool m_LastPlaybackActive = true;       // last observed player state, for edge-triggered "Draw fired while not playing" logging
};
