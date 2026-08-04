#include "Visualization.h"
#include "BarGeometry.h"
#include "FrequencyMap.h"
#include "apiPlayer.h" // TEMP DIAGNOSTIC (LogCadenceSample) - only user of IAIMPServicePlayer in this file
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace
{
    COLORREF LightenColor(COLORREF c, double factor)
    {
        factor = std::clamp(factor, 0.0, 1.0);
        BYTE r = (BYTE)std::lround(GetRValue(c) + (255 - GetRValue(c)) * factor);
        BYTE g = (BYTE)std::lround(GetGValue(c) + (255 - GetGValue(c)) * factor);
        BYTE b = (BYTE)std::lround(GetBValue(c) + (255 - GetBValue(c)) * factor);
        return RGB(r, g, b);
    }

    void GradientFillVert(HDC dc, const RECT& r, COLORREF top, COLORREF bottom)
    {
        TRIVERTEX v[2];
        v[0].x = r.left;
        v[0].y = r.top;
        v[0].Red = (COLOR16)(GetRValue(top) << 8);
        v[0].Green = (COLOR16)(GetGValue(top) << 8);
        v[0].Blue = (COLOR16)(GetBValue(top) << 8);
        v[0].Alpha = 0;
        v[1].x = r.right;
        v[1].y = r.bottom;
        v[1].Red = (COLOR16)(GetRValue(bottom) << 8);
        v[1].Green = (COLOR16)(GetGValue(bottom) << 8);
        v[1].Blue = (COLOR16)(GetBValue(bottom) << 8);
        v[1].Alpha = 0;

        GRADIENT_RECT gr = { 0, 1 };
        GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    }

    void FillSolid(HDC dc, const RECT& r, COLORREF color)
    {
        HBRUSH br = CreateSolidBrush(color);
        FillRect(dc, &r, br);
        DeleteObject(br);
    }

    // Plain GDI has no alpha-channel pens, so "grid line opacity" is faked
    // by pre-blending the grid color against whatever's already painted
    // underneath it (t=0 -> pure background, t=1 -> pure grid color) and
    // drawing that blended color as an ordinary opaque pen.
    COLORREF BlendColor(COLORREF a, COLORREF b, double t)
    {
        t = std::clamp(t, 0.0, 1.0);
        BYTE r = (BYTE)std::lround(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
        BYTE g = (BYTE)std::lround(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
        BYTE bl = (BYTE)std::lround(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
        return RGB(r, g, bl);
    }

    const float kSilenceFloorDb = -120.0f; // avoids log10(0) = -inf

    // Fixed clearance between a peak marker and the top of the drawable
    // area, so a maxed-out bar's marker never renders flush against (or
    // behind) the top border. Applied both to the raw peakY state (matters
    // for Smooth style, which uses it directly) and, decisively, to
    // DrawPeakMarker's final rect (matters for LED style, whose marker
    // rect is re-derived from the segment grid and can land back at
    // area.top regardless of the raw peakY clamp, depending on how the
    // panel height happens to divide by the segment step).
    const float kPeakTopClearancePx = 2.0f;

    // AIMP's raw Spectrum[] magnitudes are not normalized to 0.0-1.0 (the
    // SDK's own demos multiply them straight against pixel dimensions with
    // no scaling at all, so they only look right by accident whenever a
    // track's raw levels happen to stay under 1.0). Debug-log capture from
    // real playback showed typical peak bins landing around 40-44, which
    // fed straight into 20*log10(x) produced +30dBish readings against a
    // ceiling of 0dB - every bar above a whisper pinned at max regardless
    // of the floor/ceiling settings. This reference divides the raw
    // magnitude down to roughly AIMP's own scale before the log transform,
    // so a typical loud peak lands at/near 0dB (and softer material falls
    // naturally into the floor..ceiling range) instead of deep into
    // positive dB territory that the UI has no range for.
    //
    // Raised from 40.0 to 100.0 on 2026-08-04 to account for spectral
    // tilt's positive gain range (up to +12dB, i.e. ~4x, at 16kHz) - post-
    // tilt peaks measured near 98 at the treble reference bar, which the
    // 40.0 reference would have pushed well past 0dB. Calibrated against
    // that measured peak, not derived analytically.
    const float kReferenceMagnitude = 100.0f;

    float LinearToDb(float linearValue)
    {
        float v = std::max(linearValue, 0.0f) / kReferenceMagnitude;
        return 20.0f * log10f(std::max(v, powf(10.0f, kSilenceFloorDb / 20.0f)));
    }

    float NormalizeDb(float db, float floorDb, float ceilingDb)
    {
        float range = std::max(1.0f, ceilingDb - floorDb);
        float t = (db - floorDb) / range;
        return std::clamp(t, 0.0f, 1.0f);
    }

    // Maps a raw linear magnitude (0..1) to a 0..1 bar-height fraction.
    // Real spectrum energy is bass-heavy, so under Linear scaling most
    // non-bass bins pin at/near 1.0; Logarithmic converts to dB first so
    // the visible range reflects perceptual loudness instead. ceilingDb is
    // passed in rather than always read from settings so the caller can
    // substitute the adaptive auto-gain ceiling when that mode is active.
    float NormalizeMagnitude(float linearValue, float ceilingDb)
    {
        if (g_Settings.amplitudeScaling == AmplitudeScaling::Linear)
            return std::clamp(linearValue, 0.0f, 1.0f);

        return NormalizeDb(LinearToDb(linearValue), (float)g_Settings.dbFloor, ceilingDb);
    }

    // Frame-rate independence (Code Contract §7 step 2): the user's
    // Bar smoothing attack/decay settings are frame-rate-agnostic 0.0-1.0
    // coefficients defined against a 32fps baseline (dt == 1/32). Applying
    // them as a raw per-frame blend factor - as the original code did -
    // means the *same* coefficient smooths harder at low frame rates
    // (fewer, bigger steps) and softer at high ones (more, smaller steps),
    // so the visible smoothing speed silently depends on how fast Draw()
    // happens to be called. This converts the configured coefficient into
    // the equivalent effective rate for the actual dt just measured, so
    // smoothing settles at the same wall-clock speed regardless of frame
    // rate. At dt == 1/32 (32fps) this is a no-op: effective == coefficient
    // to within floating-point noise, by construction (exponent == 1.0).
    //
    // coefficient==1.0 (instant/no smoothing) was verified not to produce
    // NaN in MSVC's pow(): base is exactly 0.0, and pow(0, x) is 0.0 for
    // every tested positive x (including a degenerate dt of 0, where the
    // exponent is exactly 0.0 and pow(0,0)==1.0, giving effective==0.0) -
    // never NaN.
    double ConvertSmoothingCoefficient(double coefficient, double dt)
    {
        double effective = 1.0 - std::pow(1.0 - coefficient, dt * 32.0);
        return std::clamp(effective, 0.0, 1.0);
    }
}

CVisualization::~CVisualization()
{
    ReleaseBackBuffer();
}

HRESULT WINAPI CVisualization::QueryInterface(REFIID iid, void** obj)
{
    if (!obj)
        return E_POINTER;

    if (iid == IID_IUnknown || iid == IID_IAIMPExtensionEmbeddedVisualization)
    {
        *obj = static_cast<IAIMPExtensionEmbeddedVisualization*>(this);
        AddRef();
        return S_OK;
    }

    *obj = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI CVisualization::AddRef()
{
    return (ULONG)++m_RefCount;
}

ULONG WINAPI CVisualization::Release()
{
    long count = --m_RefCount;
    if (count == 0)
        delete this;
    return (ULONG)count;
}

int WINAPI CVisualization::GetFlags()
{
    return AIMP_VISUAL_FLAGS_RQD_DATA_SPECTRUM;
}

HRESULT WINAPI CVisualization::GetMaxDisplaySize(int* /*Width*/, int* /*Height*/)
{
    return E_FAIL; // no limitation
}

HRESULT WINAPI CVisualization::GetName(IAIMPString** S)
{
    if (!S)
        return E_POINTER;
    *S = MakeAIMPString(L"AuraBars");
    return *S ? S_OK : E_FAIL;
}

HRESULT WINAPI CVisualization::Initialize(int Width, int Height)
{
    m_Width = Width;
    m_Height = Height;
    // Cached for the options dialog's "[Environment]" diagnostics block,
    // which can run while no visualization instance exists yet or while
    // the panel sits at a stale size - see Common.h.
    g_EnvDisplayWidth.store(Width);
    g_EnvDisplayHeight.store(Height);
    m_AutoGainCeilingDbLeft.clear();
    m_AutoGainCeilingDbRight.clear();
    EnsureBackBuffer();

    // Frame-rate independence (Code Contract §7 step 2): fixed for the
    // life of the process, so read exactly once here rather than lazily
    // per-Draw or (as the cadence diagnostic used to) only when
    // DebugLogging happens to be on. m_HasLastFrameQpc = false makes the
    // very next Draw() use the dt == 1/32 fallback, per the contract's
    // "first Draw after Initialize or Resize has no previous timestamp".
    QueryPerformanceFrequency(&m_QpcFrequency);
    m_HasLastFrameQpc = false;

    return S_OK;
}

void WINAPI CVisualization::Finalize()
{
    ReleaseBackBuffer();
}

void WINAPI CVisualization::Click(int /*X*/, int /*Y*/, int /*Button*/)
{
    // no interaction
}

void WINAPI CVisualization::Resize(int NewWidth, int NewHeight)
{
    m_Width = NewWidth;
    m_Height = NewHeight;
    g_EnvDisplayWidth.store(NewWidth);
    g_EnvDisplayHeight.store(NewHeight);
    EnsureBackBuffer();
    // Stale pixel coordinates from the old size are meaningless. Velocity
    // and remaining hold are cleared alongside position (not left to
    // persist, unlike m_Smoothed*) so a freshly-reset position never pairs
    // with leftover fall speed/hold from a very differently-sized panel -
    // all three describe one coherent physics state (Code Contract §7 step 3).
    m_PeakYLeft.clear();
    m_PeakVelocityLeft.clear();
    m_PeakHoldRemainingLeft.clear();
    m_PeakYRight.clear();
    m_PeakVelocityRight.clear();
    m_PeakHoldRemainingRight.clear();
    m_AutoGainCeilingDbLeft.clear();
    m_AutoGainCeilingDbRight.clear();

    // Frame-rate independence (Code Contract §7 step 2): "first Draw after
    // Initialize or Resize has no previous timestamp" - the resize gap
    // itself isn't real render-to-render time, so don't let it become one
    // giant dt (which the 100ms clamp would just flatten anyway) or a
    // false near-zero one against a stale timestamp.
    m_HasLastFrameQpc = false;
}

void CVisualization::EnsureBackBuffer()
{
    if (m_Width <= 0 || m_Height <= 0)
        return;

    ReleaseBackBuffer();

    HDC screenDC = GetDC(nullptr);
    m_MemDC = CreateCompatibleDC(screenDC);
    m_MemBitmap = CreateCompatibleBitmap(screenDC, m_Width, m_Height);
    ReleaseDC(nullptr, screenDC);

    if (m_MemDC && m_MemBitmap)
        m_OldBitmap = (HBITMAP)SelectObject(m_MemDC, m_MemBitmap);
}

void CVisualization::ReleaseBackBuffer()
{
    if (m_MemDC && m_OldBitmap)
        SelectObject(m_MemDC, m_OldBitmap);
    if (m_MemBitmap)
        DeleteObject(m_MemBitmap);
    if (m_MemDC)
        DeleteDC(m_MemDC);
    m_MemBitmap = nullptr;
    m_MemDC = nullptr;
    m_OldBitmap = nullptr;
}

void WINAPI CVisualization::Draw(HCANVAS Canvas, PAIMPVisualData Data)
{
    // Single QueryPerformanceCounter read for the whole call, shared by the
    // frame-rate-independence dt below and (when DebugLogging is on) the
    // cadence diagnostic further down - never re-read per bar or per
    // channel, and never a second timer alongside this one.
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // Frame-rate independence (Code Contract §7 step 2 / §1): dt is the
    // measured wall-clock interval since the previous Draw() call. Feeds
    // ConvertSmoothingCoefficient inside ComputeBarLayout, converting the
    // user's frame-rate-agnostic 0.0-1.0 attack/decay settings into the
    // effective per-frame rate for whatever dt actually turned out to be.
    // Clamped to 100ms so a stall (window drag, alt-tab, a genuinely dropped
    // frame) can't make the very next frame jump instead of smoothing.
    double dt;
    if (!m_HasLastFrameQpc || m_QpcFrequency.QuadPart <= 0)
        dt = 1.0 / 32.0; // no previous timestamp - first Draw after Initialize/Resize (or QPC unavailable)
    else
    {
        dt = (double)(now.QuadPart - m_LastFrameQpc.QuadPart) / (double)m_QpcFrequency.QuadPart;
        dt = std::min(dt, 0.1);
    }
    m_LastFrameQpc = now;
    m_HasLastFrameQpc = true;

    // TEMP DIAGNOSTIC - see LogCadenceSample(). Placed before every early
    // return so it times every actual call AIMP makes, not just the ones
    // that go on to paint. Remove this call (and LogCadenceSample) once the
    // cadence question it exists to answer is settled.
    if (g_Settings.debugLogging)
        LogCadenceSample(now);
    else
        m_CadenceActive = false; // so the next enable re-inits cleanly instead of measuring across the gap

    if (!Data || m_Width <= 0 || m_Height <= 0)
        return;

    if (!m_MemDC || !m_MemBitmap)
        EnsureBackBuffer();
    if (!m_MemDC)
        return;

    RECT area = { 0, 0, m_Width, m_Height };

    DrawBackground(m_MemDC, area);

    // Background always fills the whole panel regardless of margins.
    // Bars, peaks, and grid all live within the margined (and, below,
    // border-inset) area computed next.
    const int topMargin = std::clamp(g_Settings.topMargin, 0, 100);
    const int bottomMargin = std::clamp(g_Settings.bottomMargin, 0, 100);
    const int leftMargin = std::clamp(g_Settings.leftMargin, 0, 100);
    const int rightMargin = std::clamp(g_Settings.rightMargin, 0, 100);

    const int vTop = area.top + topMargin;
    int vBottom = area.bottom - bottomMargin;
    if (vBottom < vTop)
        vBottom = vTop;

    // Border is drawn as a frame growing inward from each channel's
    // margin-computed rect (frameRect), so anything that also draws out to
    // frameRect's own edges - bars, peaks, grid - would sit underneath the
    // border strip, getting its bottom/top/sides covered. Bars/peaks/grid
    // instead use frameRect inset by the border's thickness ("innerRect"),
    // so the border frames them from just outside rather than overlapping
    // them. When border is off there's nothing to avoid, so inset is 0 and
    // innerRect == frameRect.
    const int borderInset = g_Settings.borderEnabled ? std::clamp(g_Settings.borderThickness, 1, 10) : 0;
    auto insetRect = [borderInset](const RECT& r) -> RECT
    {
        RECT result = { r.left + borderInset, r.top + borderInset, r.right - borderInset, r.bottom - borderInset };
        if (result.right < result.left) result.right = result.left;
        if (result.bottom < result.top) result.bottom = result.top;
        return result;
    };

    std::vector<BarLayout> bars;
    std::vector<RECT> borderRects;   // 1 rect (Mono) or 2 rects (Stereo); border is drawn along these, bars/grid live inside their inset
    std::vector<RECT> innerRects;    // same count/order as borderRects; grid is bounded to these
    // Same count/order as innerRects - each entry holds only that region's
    // own bars, so DrawGrid's occupied-column skipping never walks into a
    // different channel's (or otherwise out-of-bounds) bar positions.
    std::vector<std::vector<BarLayout>> barsByRegion;
    bool drawSeparator = false;
    RECT separatorRect{};
    RECT innerVRange{};              // shared vertical bounds of the inner rect(s), for gradient reference

    if (g_Settings.channelMode == ChannelMode::Stereo)
    {
        // Stereo's margin meaning is not a simple inset: Left margin is
        // the gap on BOTH sides of the Left channel (window edge -> Left
        // channel, AND Left channel -> Separator); Right margin is the
        // gap on BOTH sides of the Right channel (Separator -> Right
        // channel, AND Right channel -> window edge). This fully replaces
        // the old Channel gap setting - the separator sits exactly in the
        // space these margins imply, not a separately configured gap.
        const int separatorW = std::clamp(g_Settings.separatorThickness, 1, 10);
        const int totalWidth = std::max(0, (int)(area.right - area.left));
        const int overhead = 2 * leftMargin + 2 * rightMargin + separatorW;
        const int remaining = std::max(0, totalWidth - overhead);
        const int leftWidth = remaining / 2;
        const int rightWidth = remaining - leftWidth;

        RECT leftRect = { area.left + leftMargin, vTop, area.left + leftMargin + leftWidth, vBottom };
        const int sepLeft = leftRect.right + leftMargin;
        const int sepRight = sepLeft + separatorW;
        RECT rightRect = { sepRight + rightMargin, vTop, area.right - rightMargin, vBottom };
        if (rightRect.right < rightRect.left)
            rightRect.right = rightRect.left;

        RECT leftInner = insetRect(leftRect);
        RECT rightInner = insetRect(rightRect);

        std::vector<BarLayout> leftBars, rightBars;
        ComputeBarLayout(leftInner, Data, 0, m_PeakYLeft, m_PeakVelocityLeft, m_PeakHoldRemainingLeft, m_AutoGainCeilingDbLeft, m_SmoothedLeft, true, dt, leftBars);
        ComputeBarLayout(rightInner, Data, 1, m_PeakYRight, m_PeakVelocityRight, m_PeakHoldRemainingRight, m_AutoGainCeilingDbRight, m_SmoothedRight, false, dt, rightBars);

        bars.reserve(leftBars.size() + rightBars.size());
        bars.insert(bars.end(), leftBars.begin(), leftBars.end());
        bars.insert(bars.end(), rightBars.begin(), rightBars.end());

        borderRects = { leftRect, rightRect };
        innerRects = { leftInner, rightInner };
        barsByRegion = { leftBars, rightBars };
        innerVRange = { area.left, leftInner.top, area.right, leftInner.bottom };

        if (g_Settings.separatorEnabled)
        {
            drawSeparator = true;
            // Full window height top-to-bottom - unlike the border
            // rectangles, the separator ignores Top/Bottom margin entirely.
            separatorRect = { sepLeft, area.top, sepRight, area.bottom };
        }
    }
    else
    {
        RECT barArea = { area.left + leftMargin, vTop, area.right - rightMargin, vBottom };
        if (barArea.right < barArea.left)
            barArea.right = barArea.left;

        RECT inner = insetRect(barArea);
        ComputeBarLayout(inner, Data, -1, m_PeakYLeft, m_PeakVelocityLeft, m_PeakHoldRemainingLeft, m_AutoGainCeilingDbLeft, m_SmoothedLeft, true, dt, bars);
        borderRects = { barArea };
        innerRects = { inner };
        barsByRegion = { bars };
        innerVRange = { area.left, inner.top, area.right, inner.bottom };
    }

    // Clipping backstop: confines grid/bar/peak drawing to exactly the
    // drawable rect(s) - the union of innerRects, one per channel - so a
    // geometry arithmetic error can never paint over the border or spill
    // into the skin's own chrome outside the plugin. This is defence in
    // depth, not a substitute for ComputeBarGeometry's fit invariant
    // (BarGeometry.cpp); it only ever crops an already-correct layout that
    // has nowhere it should legitimately be reaching beyond this region.
    // Lifted again below, before Separator/Border, which must paint outside
    // the inner rect deliberately.
    HRGN clipRgn = CreateRectRgnIndirect(&innerRects[0]);
    for (size_t i = 1; i < innerRects.size(); ++i)
    {
        HRGN extra = CreateRectRgnIndirect(&innerRects[i]);
        CombineRgn(clipRgn, clipRgn, extra, RGN_OR);
        DeleteObject(extra);
    }
    SelectClipRgn(m_MemDC, clipRgn);
    DeleteObject(clipRgn);

    // Grid needs each bar's current top *before* it draws, so it can skip
    // every pixel below that top for that bar's column - LED segment gaps
    // included, since that whole span visually belongs to the bar even
    // where the bar itself doesn't paint every pixel of it. Bounded to
    // each channel's own inner rect (not the whole panel) so grid lines
    // never spill into the margins, the border frame, or - in Stereo - the
    // gap around the separator.
    if (g_Settings.gridLines)
    {
        for (size_t i = 0; i < innerRects.size(); ++i)
            DrawGrid(m_MemDC, innerRects[i], area, barsByRegion[i]);
    }

    DrawBarVisuals(m_MemDC, innerVRange, bars);

    SelectClipRgn(m_MemDC, nullptr);

    // Separator and Border are decorative chrome, drawn last on top of
    // everything else so a loud bar reaching full height never occludes
    // them. Separator is independent of Border enabled/disabled.
    if (drawSeparator)
        FillSolid(m_MemDC, separatorRect, g_Settings.separatorColor);

    if (g_Settings.borderEnabled)
    {
        for (const RECT& r : borderRects)
            DrawBorderFrame(m_MemDC, r, g_Settings.borderColor, g_Settings.borderThickness);
    }

    BitBlt(Canvas, 0, 0, m_Width, m_Height, m_MemDC, 0, 0, SRCCOPY);
}

void CVisualization::DrawBackground(HDC dc, const RECT& area)
{
    if (g_Settings.backgroundStyle == BackgroundStyle::Flat)
    {
        FillSolid(dc, area, g_Settings.backgroundColor);
    }
    else
    {
        // No independent gradient-background colors are specified by the
        // contract; derive a subtle top highlight from the flat color.
        COLORREF top = LightenColor(g_Settings.backgroundColor, 0.35);
        GradientFillVert(dc, area, top, g_Settings.backgroundColor);
    }
}

COLORREF CVisualization::BackgroundColorAtY(int y, const RECT& area) const
{
    if (g_Settings.backgroundStyle == BackgroundStyle::Flat)
        return g_Settings.backgroundColor;

    // Mirrors DrawBackground's gradient exactly, so a grid line blended
    // against "the background" reads as blended against what's actually
    // underneath it at that row, not a flat approximation.
    COLORREF top = LightenColor(g_Settings.backgroundColor, 0.35);
    return ColorAtHeight(top, g_Settings.backgroundColor, y, area);
}

void CVisualization::DrawGrid(HDC dc, const RECT& bounds, const RECT& backgroundRef, const std::vector<BarLayout>& bars)
{
    const int spacingPx = std::clamp(g_Settings.gridLineSpacing, 8, 100);
    const double opacity = std::clamp(g_Settings.gridLineOpacity, 0, 100) / 100.0;
    const bool dashed = (g_Settings.gridLineStyle != GridLineStyle::Solid);

    // True whenever nothing in [xStart, xEnd) at row y belongs to a bar's
    // occupied column (from that bar's current top down to the panel
    // bottom, LED gaps included - see DrawBarVisuals/ComputeBarLayout).
    auto isRowClear = [&bars](int y, int xStart, int xEnd) -> bool
    {
        for (const BarLayout& bar : bars)
        {
            if (y < bar.rect.top)
                continue; // this bar's column isn't occupied up at this row
            if (bar.rect.right > xStart && bar.rect.left < xEnd)
                return false;
        }
        return true;
    };

    // Dashed style needs its own segment computation rather than a GDI
    // PS_DASH pen: GDI restarts a dash pattern's phase at every separate
    // MoveToEx/LineTo call (breaking multi-segment rows out of phase), and
    // even a single continuous line clipped to a region still partially
    // renders any dash segment that straddles a clip boundary - a bar's
    // edge - as a short stray fragment, since clipping just hides pixels
    // without knowing it's cutting a dash in half. Computing our own fixed-
    // phase segments and only drawing ones entirely clear of every bar
    // avoids both: one consistent phase across the full width, and no
    // partial-dash slivers hugging bar tops.
    const int kDashOn = 6;
    const int kDashGap = 4;
    const int kDashCycle = kDashOn + kDashGap;

    for (int y = bounds.top + spacingPx; y < bounds.bottom; y += spacingPx)
    {
        COLORREF lineColor = BlendColor(BackgroundColorAtY(y, backgroundRef), g_Settings.gridLineColor, opacity);
        HPEN pen = CreatePen(PS_SOLID, 1, lineColor);
        HPEN old = (HPEN)SelectObject(dc, pen);

        if (dashed)
        {
            for (int x = bounds.left; x < bounds.right; x += kDashCycle)
            {
                int segEnd = std::min(x + kDashOn, (int)bounds.right);
                if (isRowClear(y, x, segEnd))
                {
                    MoveToEx(dc, x, y, nullptr);
                    LineTo(dc, segEnd, y);
                }
            }
        }
        else
        {
            // A continuous solid stroke has no dash phase to protect, so
            // drawing it as several segments (skipping occupied columns)
            // is fine - no seam is visible between adjacent segments.
            int cursorX = bounds.left;
            for (const BarLayout& bar : bars)
            {
                if (bar.rect.left > cursorX)
                {
                    MoveToEx(dc, cursorX, y, nullptr);
                    LineTo(dc, bar.rect.left, y);
                }
                if (y < bar.rect.top)
                {
                    MoveToEx(dc, bar.rect.left, y, nullptr);
                    LineTo(dc, bar.rect.right, y);
                }
                cursorX = bar.rect.right;
            }
            if (cursorX < bounds.right)
            {
                MoveToEx(dc, cursorX, y, nullptr);
                LineTo(dc, bounds.right, y);
            }
        }

        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

void CVisualization::DrawBorderFrame(HDC dc, const RECT& rect, COLORREF color, int thickness)
{
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0)
        return;

    // Four filled edge strips rather than FrameRect: FrameRect's line
    // width isn't reliably controllable to an exact pixel count, while
    // strips give precise, symmetric thickness on all four sides. Clamped
    // so a thick border on a tiny (e.g. heavily margined) rect can't turn
    // into overlapping/inverted strips.
    int t = std::clamp(thickness, 1, 10);
    t = std::min(t, std::max(1, std::min(w, h) / 2));

    RECT top = { rect.left, rect.top, rect.right, rect.top + t };
    RECT bottom = { rect.left, rect.bottom - t, rect.right, rect.bottom };
    RECT left = { rect.left, rect.top, rect.left + t, rect.bottom };
    RECT right = { rect.right - t, rect.top, rect.right, rect.bottom };

    FillSolid(dc, top, color);
    FillSolid(dc, bottom, color);
    FillSolid(dc, left, color);
    FillSolid(dc, right, color);
}

COLORREF CVisualization::ColorAtHeight(COLORREF top, COLORREF bottom, int y, const RECT& area) const
{
    int h = area.bottom - area.top;
    if (h <= 0)
        return bottom;
    double t = (double)(area.bottom - y) / (double)h; // 0 at bottom, 1 at top
    t = std::clamp(t, 0.0, 1.0);
    BYTE r = (BYTE)std::lround(GetRValue(bottom) + (GetRValue(top) - GetRValue(bottom)) * t);
    BYTE g = (BYTE)std::lround(GetGValue(bottom) + (GetGValue(top) - GetGValue(bottom)) * t);
    BYTE b = (BYTE)std::lround(GetBValue(bottom) + (GetBValue(top) - GetBValue(bottom)) * t);
    return RGB(r, g, b);
}

void CVisualization::UpdateAutoGainCeiling(float& ceilingDb, float currentDb)
{
    // Fast attack / slow release, like a classic AGC: jump up instantly so
    // a sudden loud passage never clips off-screen, but decay gradually so
    // a brief lull doesn't collapse the ceiling and make the very next
    // beat look like a false spike.
    //
    // Tracked per bar rather than one shared ceiling across the whole
    // frame: heavily limited/compressed mixes (loud EDM masters, hard
    // rock/metal) push nearly every frequency band toward the same level
    // simultaneously, so a single global ceiling leaves no headroom
    // difference between bars to show once the mix gets dense - the whole
    // row just pins together. Each bar normalizing against its own recent
    // range keeps texture visible even when the mix as a whole is
    // brickwalled, since individual bands still vary relative to
    // themselves even when they don't vary much relative to each other.
    const float kReleaseDbPerFrame = 0.2f;
    const float kMinCeilingDb = (float)g_Settings.dbFloor + 1.0f;
    const float kMaxCeilingDb = 0.0f;

    if (currentDb > ceilingDb)
        ceilingDb = currentDb;
    else
        ceilingDb = std::max(currentDb, ceilingDb - kReleaseDbPerFrame);

    ceilingDb = std::clamp(ceilingDb, kMinCeilingDb, kMaxCeilingDb);
}

void CVisualization::LogDebugSample(const DebugBarSample& bass, const DebugBarSample& mid, const DebugBarSample& treble,
                                     int effectiveBarCount) const
{
    // effectiveBarCount logged explicitly (not just implied by the highest
    // bar index) so a horizontal-fit-invariant clamp - e.g. a narrow window
    // quietly reducing NumberOfBars=20 down to 7 - is visible on the line
    // itself instead of requiring the reader to notice a suspiciously low
    // index where a higher one was expected.
    //
    // Bars are frequency-anchored (~60Hz/1kHz/8kHz, Code Contract §3)
    // rather than fixed indices (bar1/mid/last): a fixed index samples a
    // different frequency at every bar count or MinFrequency/MaxFrequency
    // setting, making logs from different runs incomparable. pre_raw/pre_db
    // are the aggregated magnitude before the spectral-tilt multiplier;
    // post_raw/post_db are after it - printed separately since tilt's
    // swing across the logged range can be large enough that one number
    // alone is ambiguous about how much of it is tilt.
    wchar_t line[640];
    swprintf_s(line,
        L"bars=%d floor=%.1fdB ceiling=%.1fdB"
        L" | bar[%d]@%.0fHz pre_raw=%.5f pre_db=%.2f post_raw=%.5f post_db=%.2f"
        L" | bar[%d]@%.0fHz pre_raw=%.5f pre_db=%.2f post_raw=%.5f post_db=%.2f"
        L" | bar[%d]@%.0fHz pre_raw=%.5f pre_db=%.2f post_raw=%.5f post_db=%.2f",
        effectiveBarCount, g_Settings.dbFloor, g_Settings.dbCeiling,
        bass.index + 1, bass.freqHz, bass.preRaw, bass.preDb, bass.postRaw, bass.postDb,
        mid.index + 1, mid.freqHz, mid.preRaw, mid.preDb, mid.postRaw, mid.postDb,
        treble.index + 1, treble.freqHz, treble.preRaw, treble.preDb, treble.postRaw, treble.postDb);
    AppendDebugLog(line);
}

// ============================================================================
// TEMP DIAGNOSTIC - frame-cadence measurement. Exists only to settle a units
// question (is the Draw call rate stable, and does it vary with drawable
// area?). Not part of the Code Contract - delete this method, its call site
// in Draw(), and the m_Cadence*/m_LastDrawQpc/m_LastPlaybackActive members in
// Visualization.h once that question is answered. m_QpcFrequency is now
// permanent (frame-rate independence depends on it) - do not delete it.
//
// Called once per Draw() when DebugLogging is on (see Draw()'s call site,
// which also resets m_CadenceActive when logging is off so a later re-enable
// starts a fresh window instead of measuring across the disabled gap). now
// is the single QPC read Draw() already took for dt - reused here rather
// than calling QueryPerformanceCounter a second time this frame.
// Writes distinctly-prefixed lines to the same %TEMP%\AuraBars_debug.log
// as LogDebugSample, via AppendDebugLog:
//   CADENCE-INIT       once, the first Draw() after logging (re)starts - QPC frequency
//   CADENCE-NOPLAYBACK edge-triggered - Draw fired while the player isn't in the Playing state, or resumed
//   CADENCE            once/sec - frame count and interval stats for the second just completed
// ============================================================================
void CVisualization::LogCadenceSample(LARGE_INTEGER now)
{
    int playerState = AIMP_PLAYER_STATE_STOPPED;
    IAIMPServicePlayer* player = nullptr;
    if (g_Core && SUCCEEDED(g_Core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player)) && player)
    {
        playerState = player->GetState();
        player->Release();
    }
    const bool playbackActive = (playerState == AIMP_PLAYER_STATE_PLAYING);

    if (!m_CadenceActive)
    {
        // Just (re)started: open a fresh window rather than computing an
        // interval against a stale m_LastDrawQpc left over from before
        // logging was off. m_QpcFrequency itself was already captured once
        // in Initialize() - not re-read here anymore.
        wchar_t initLine[128];
        swprintf_s(initLine, L"CADENCE-INIT qpc_freq=%lld", (long long)m_QpcFrequency.QuadPart);
        AppendDebugLog(initLine);

        m_CadenceActive = true;
        m_LastDrawQpc = now;
        // First window closes exactly one QPC-frequency's worth of ticks
        // (== 1000ms) from now; every later window advances this same target
        // by that fixed tick count rather than resetting to "now" at emit
        // time, so the cadence stays pinned to real wall-clock seconds
        // instead of drifting later with each window's processing/rounding
        // slack.
        m_CadenceNextEmitQpc.QuadPart = now.QuadPart + m_QpcFrequency.QuadPart;
        m_CadenceFrameCount = 0;
        m_CadenceSumMs = 0.0;
        m_CadenceSumSqMs = 0.0;
        m_LastPlaybackActive = playbackActive;

        if (!playbackActive)
            AppendDebugLog(L"CADENCE-NOPLAYBACK Draw firing with no active playback (state at logging start)");
        return;
    }

    if (playbackActive != m_LastPlaybackActive)
    {
        m_LastPlaybackActive = playbackActive;
        AppendDebugLog(playbackActive
            ? L"CADENCE-NOPLAYBACK playback resumed, Draw continues"
            : L"CADENCE-NOPLAYBACK Draw firing with no active playback");
    }

    if (m_QpcFrequency.QuadPart <= 0)
        return; // shouldn't happen on any real Windows box, but guards the divide below

    const double intervalMs = (double)(now.QuadPart - m_LastDrawQpc.QuadPart) * 1000.0 / (double)m_QpcFrequency.QuadPart;
    m_LastDrawQpc = now;

    if (m_CadenceFrameCount == 0)
        m_CadenceMinMs = m_CadenceMaxMs = intervalMs;
    else
    {
        m_CadenceMinMs = std::min(m_CadenceMinMs, intervalMs);
        m_CadenceMaxMs = std::max(m_CadenceMaxMs, intervalMs);
    }
    m_CadenceSumMs += intervalMs;
    m_CadenceSumSqMs += intervalMs * intervalMs;
    m_CadenceFrameCount++;

    // Wall-clock trigger, not a frame count: fires once `now` reaches the
    // target set when the previous window closed (or when logging started).
    if (now.QuadPart >= m_CadenceNextEmitQpc.QuadPart)
    {
        if (m_CadenceFrameCount > 0)
        {
            const double mean = m_CadenceSumMs / m_CadenceFrameCount;
            // Streaming variance (E[x^2] - E[x]^2) can go slightly negative
            // from float rounding when interval samples barely vary; clamp
            // before sqrt.
            const double variance = std::max(0.0, m_CadenceSumSqMs / m_CadenceFrameCount - mean * mean);
            const double stddev = std::sqrt(variance);

            const wchar_t* modeStr = (g_Settings.channelMode == ChannelMode::Stereo) ? L"Stereo" : L"Mono";
            wchar_t line[256];
            swprintf_s(line,
                L"CADENCE frames=%d mean_ms=%.2f min_ms=%.2f max_ms=%.2f stddev_ms=%.2f w=%d h=%d mode=%s bars=%d",
                m_CadenceFrameCount, mean, m_CadenceMinMs, m_CadenceMaxMs, stddev,
                m_Width, m_Height, modeStr, m_LastEffectiveBarCount);
            AppendDebugLog(line);
        }

        m_CadenceFrameCount = 0;
        m_CadenceSumMs = 0.0;
        m_CadenceSumSqMs = 0.0;

        // Advance by exactly one second of ticks, possibly more than once if
        // Draw() stalled past a whole window - keeps the target anchored to
        // wall-clock second boundaries instead of re-basing off "now" and
        // losing that alignment.
        do
        {
            m_CadenceNextEmitQpc.QuadPart += m_QpcFrequency.QuadPart;
        } while (m_CadenceNextEmitQpc.QuadPart <= now.QuadPart);
    }
}

void CVisualization::ComputeBarLayout(const RECT& area, const TAIMPVisualData* data, int spectrumChannel,
                                       std::vector<float>& peakState, std::vector<float>& velocityState,
                                       std::vector<float>& holdState, std::vector<float>& autoGainState,
                                       std::vector<float>& smoothedState,
                                       bool emitDebugLog, double dt, std::vector<BarLayout>& out)
{
    // Shared with BuildEnvironmentSection() (OptionsFrame.cpp) via
    // BarGeometry.h/.cpp, so EnvEffectiveBarWidth/EnvEffectiveBarCount in a
    // Copy snapshot can never diverge from what actually gets laid out
    // here. Enforces the row-fit invariant (bar row must not exceed the
    // drawable width) by reducing spacing, then bar count, below the
    // user's configured values when needed - those stored settings
    // themselves are untouched; this is a render-time-only adjustment.
    const int drawableWidth = (int)(area.right - area.left);
    const BarGeometry geometry = ComputeBarGeometry(drawableWidth, g_Settings.barCount, g_Settings.barSpacing);
    const int n = geometry.effectiveBarCount;

    // Frame-rate independence (Code Contract §7 step 2): converted once per
    // Draw call, not once per bar - dt and the two configured coefficients
    // are the same for every bar in this region, so ConvertSmoothingCoefficient
    // only needs to run twice here rather than 2*n times in the loop below.
    const double effectiveAttack = ConvertSmoothingCoefficient(g_Settings.barSmoothingAttack, dt);
    const double effectiveDecay = ConvertSmoothingCoefficient(g_Settings.barSmoothingDecay, dt);

    // TEMP DIAGNOSTIC - feeds LogCadenceSample's "bars=" field with the
    // count actually rendered (post fit-invariant), not g_Settings.barCount.
    // Remove when the m_LastEffectiveBarCount member is removed.
    m_LastEffectiveBarCount = n;

    if ((int)peakState.size() != n)
        peakState.assign(n, (float)area.bottom);
    // Peak marker gravity (Code Contract §7 step 3): velocity/hold reset
    // to "no fall in progress, nothing to hold" alongside a freshly-sized
    // position array - a bar-count change is exactly the same kind of
    // discontinuity as a resize for this state.
    if ((int)velocityState.size() != n)
        velocityState.assign(n, 0.0f);
    if ((int)holdState.size() != n)
        holdState.assign(n, 0.0f);
    if ((int)autoGainState.size() != n)
        autoGainState.assign(n, (float)g_Settings.dbCeiling);
    if ((int)smoothedState.size() != n)
        smoothedState.assign(n, 0.0f);

    out.clear();
    out.reserve(n);

    const int spacing = geometry.effectiveSpacing;
    const int totalSpacing = spacing * (n - 1);
    const int availWidth = std::max(0, drawableWidth - totalSpacing);
    const int totalHeight = area.bottom - area.top;

    // Per-bar width via cumulative rounding rather than a single truncated
    // availWidth/n: each bar's boundary is independently rounded from the
    // running total, so the few leftover pixels from the division spread
    // out one-per-bar across the row instead of all landing on whichever
    // bar's rect gets clamped to area.right (previously the last bar,
    // which came out visibly wider with an inconsistent gap before it).
    auto widthBoundary = [availWidth, n](int i) -> int
    {
        return (int)std::llround((double)availWidth * i / n);
    };

    // Frequency-based bar edges (Code Contract §1/§3): geometric in
    // *frequency* between MinFrequency/MaxFrequency, then converted to bin
    // indices via FrequencyToBin (FrequencyMap.h/.cpp) - never spaced by
    // bin index directly. The previous approach spaced edges geometrically
    // over the raw bin index [0,255], which double-warped the axis on top
    // of AIMP's own non-linear bin->frequency mapping and caused a
    // descending staircase with a dead right edge. Do not restore
    // index-based grouping in any form.
    const double fMin = (double)g_Settings.minFrequency;
    const double fMax = (double)g_Settings.maxFrequency;
    const double freqRatio = fMax / fMin;
    auto barFreqLo = [fMin, freqRatio, n](int i) -> double { return fMin * std::pow(freqRatio, (double)i / n); };
    auto barFreqHi = [fMin, freqRatio, n](int i) -> double { return fMin * std::pow(freqRatio, (double)(i + 1) / n); };

    struct BarFreqBins { int lo; int hi; double centerHz; };

    // Shared by the main per-bar loop below and the debug-reference search:
    // both must agree on exactly which bins a given bar reads, or the
    // debug log's frequency label wouldn't match what's actually
    // aggregated for that bar.
    auto binRangeForBar = [&](int i) -> BarFreqBins
    {
        double freqLo = barFreqLo(i);
        double freqHi = barFreqHi(i);
        double freqCenter = std::sqrt(freqLo * freqHi);

        int binLo = std::clamp((int)std::lround(FrequencyToBin(freqLo)), 0, (int)AIMP_VISUAL_SPECTRUM_MAX - 1);
        int binHi = std::clamp((int)std::lround(FrequencyToBin(freqHi)) - 1, 0, (int)AIMP_VISUAL_SPECTRUM_MAX - 1);

        // Below ~160Hz AIMP allocates under one bin per bar (the bottom
        // three octaves share 9 bins total) - a bar narrower than one bin
        // resolves to an empty or inverted range here. Read the single
        // nearest bin instead of interpolating/dithering fake detail;
        // adjacent low bars sometimes reading the same bin and moving
        // identically is the data source's limit, not a bug (Code
        // Contract §3).
        if (binHi < binLo)
        {
            int single = std::clamp((int)std::lround(FrequencyToBin(freqCenter)), 0, (int)AIMP_VISUAL_SPECTRUM_MAX - 1);
            binLo = binHi = single;
        }

        return { binLo, binHi, freqCenter };
    };

    // Debug-logging reference bars (Code Contract §3): frequency-anchored
    // (~60Hz/1kHz/8kHz) rather than fixed indices, so samples are
    // comparable across bar counts and MinFrequency/MaxFrequency settings -
    // a fixed index (e.g. "bar 20") sampled a different frequency at every
    // configuration. freqHz comes from BinToFrequency on each bar's actual
    // (post-quantization) bin range, so the printed frequency matches what
    // was genuinely read, not the idealized pre-quantization bar edge.
    // Search cost is O(n) and only runs when debug logging is on.
    DebugBarSample debugBass, debugMid, debugTreble;
    if (emitDebugLog && g_Settings.debugLogging)
    {
        auto findNearestBar = [&](double targetHz) -> DebugBarSample
        {
            DebugBarSample best;
            double bestDist = 1e18;
            for (int j = 0; j < n; ++j)
            {
                BarFreqBins bf = binRangeForBar(j);
                double actualFreq = BinToFrequency((bf.lo + bf.hi) / 2.0);
                double dist = std::abs(actualFreq - targetHz);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best.index = j;
                    best.freqHz = actualFreq;
                }
            }
            return best;
        };
        debugBass = findNearestBar(60.0);
        debugMid = findNearestBar(1000.0);
        debugTreble = findNearestBar(8000.0);
    }

    // Source channel: Spectrum[0]/[1] are the two channels AIMP's own
    // VisualSpectrumDemo draws; Spectrum[2] is undocumented and unused by
    // any reference code, and empirically runs far hotter than a real
    // magnitude should - almost certainly a raw L+R sum rather than a
    // properly scaled mono value. Mono mode averages the two documented
    // channels; Stereo mode reads the caller's single requested channel so
    // each half genuinely reflects just that channel.
    auto binValue = [data, spectrumChannel](int b) -> float
    {
        return (spectrumChannel < 0)
            ? 0.5f * (data->Spectrum[0][b] + data->Spectrum[1][b])
            : data->Spectrum[spectrumChannel][b];
    };

    int x = area.left;
    for (int i = 0; i < n; ++i)
    {
        BarFreqBins bf = binRangeForBar(i);
        int binLo = bf.lo;
        int binHi = bf.hi;
        double freqCenter = bf.centerHz;

        // Peak = max of the bin range (default): with frequency-correct
        // bins the range still widens toward the higher bars, and a single
        // loud bin anywhere in it should register - that's what "peak" is
        // for. Average = arithmetic mean, smooths across the range. Sum =
        // total, which scales with bin count so upper bars read much
        // hotter than lower ones; kept for experimentation, not the
        // default (Code Contract §2/§3).
        float aggregated;
        switch (g_Settings.binAggregation)
        {
            case BinAggregation::Peak:
            {
                float m = binValue(binLo);
                for (int b = binLo + 1; b <= binHi; ++b)
                    m = std::max(m, binValue(b));
                aggregated = m;
                break;
            }
            case BinAggregation::Sum:
            {
                float s = 0.0f;
                for (int b = binLo; b <= binHi; ++b)
                    s += binValue(b);
                aggregated = s;
                break;
            }
            case BinAggregation::Average:
            default:
            {
                float s = 0.0f;
                for (int b = binLo; b <= binHi; ++b)
                    s += binValue(b);
                aggregated = s / (float)(binHi - binLo + 1);
                break;
            }
        }

        // Tilt: linear multiplier so it works identically in both
        // amplitude modes, applied right after aggregation and before
        // smoothing so smoothing state holds tilted values (Code Contract
        // §3 pipeline order: group -> aggregate -> tilt -> smooth ->
        // amplitude scale -> peak markers). 0.0 disables it (multiplier
        // == 1 at every frequency).
        double gainDb = g_Settings.spectralTilt * std::log2(freqCenter / 1000.0);
        float tiltMultiplier = (float)std::pow(10.0, gainDb / 20.0);
        float rawValue = aggregated * tiltMultiplier;
        float rawDb = LinearToDb(rawValue);

        if (emitDebugLog && g_Settings.debugLogging)
        {
            // pre = aggregated (before tilt), post = rawValue (after) -
            // both logged since tilt's swing can make one alone ambiguous.
            if (i == debugBass.index)
            {
                debugBass.preRaw = aggregated; debugBass.preDb = LinearToDb(aggregated);
                debugBass.postRaw = rawValue; debugBass.postDb = rawDb;
            }
            if (i == debugMid.index)
            {
                debugMid.preRaw = aggregated; debugMid.preDb = LinearToDb(aggregated);
                debugMid.postRaw = rawValue; debugMid.postDb = rawDb;
            }
            if (i == debugTreble.index)
            {
                debugTreble.preRaw = aggregated; debugTreble.preDb = LinearToDb(aggregated);
                debugTreble.postRaw = rawValue; debugTreble.postDb = rawDb;
            }
        }

        // Bar smoothing sits before the Linear/Log scaling branch (and
        // before auto-gain ceiling tracking, which feeds that same
        // branch), so both scaling modes and the ceiling all see the same
        // filtered value - the ceiling never hunts against raw transients
        // while the bar height lags smoothly behind it. Peak markers track
        // this for free: they're derived from the resulting scaled bar
        // height below, not a separate raw-magnitude read.
        //
        // rate uses effectiveAttack/effectiveDecay (Code Contract §7 step
        // 2), not the raw configured coefficients directly - see
        // ConvertSmoothingCoefficient above the per-bar loop.
        float scalingInput = rawValue;
        if (g_Settings.barSmoothing)
        {
            float& smoothed = smoothedState[i];
            double rate = (rawValue >= smoothed) ? effectiveAttack : effectiveDecay;
            smoothed += (float)rate * (rawValue - smoothed);
            scalingInput = smoothed;
        }
        float scalingDb = LinearToDb(scalingInput);

        float ceilingDb = (float)g_Settings.dbCeiling;
        if (g_Settings.autoGainCeiling && g_Settings.amplitudeScaling == AmplitudeScaling::Logarithmic)
        {
            UpdateAutoGainCeiling(autoGainState[i], scalingDb);
            ceilingDb = autoGainState[i];
        }

        float value = NormalizeMagnitude(scalingInput, ceilingDb);

        // Capped by the same top clearance as the peak marker below - a
        // bar left free to reach area.top exactly would put its own top
        // closer to the border than the peak's clamped floor, forcing the
        // peak numerically below the bar (reintroducing the "bar taller
        // than its own peak marker" bug) on every near-maxed frame. Capping
        // both by the same margin keeps them consistent instead of fighting.
        int barHeightPx = (int)std::lround(value * totalHeight);
        barHeightPx = std::clamp(barHeightPx, 0, (int)std::max(0.0f, totalHeight - kPeakTopClearancePx));

        int barWidth = std::max(1, widthBoundary(i + 1) - widthBoundary(i));

        RECT barRect;
        barRect.left = x;
        barRect.right = (i == n - 1) ? area.right : x + barWidth;
        barRect.bottom = area.bottom;
        barRect.top = area.bottom - barHeightPx;

        // Peak marker gravity (Code Contract §7 step 3), runs after
        // barRect.top - i.e. after this frame's smoothed bar value - is
        // already computed above (Code Contract §3 frame-ordering rule:
        // getting this backwards caused two shipped bugs previously).
        //
        // Coordinate note: peakY is device Y like barRect.top - smaller is
        // higher/louder, larger is toward the floor. The contract's motion
        // model is phrased in "height above the floor" terms (position
        // *decreases* as the marker falls); translated to device Y, a fall
        // *increases* peakY, and a re-trigger toward a taller bar decreases
        // it. velocity is stored as a non-negative fall-speed magnitude in
        // both modes, never a signed device-Y rate.
        float& peakY = peakState[i];
        float& velocity = velocityState[i];
        float& holdRemainingMs = holdState[i];
        float barTopY = (float)barRect.top;

        if (barTopY <= peakY)
        {
            // Re-trigger: the bar has caught up to (or passed) the marker.
            // Snap, reset velocity and hold, nothing else this frame -
            // rejected: no overshoot above the bar, no bounce.
            peakY = barTopY;
            velocity = 0.0f;
            holdRemainingMs = (float)g_Settings.peakHoldTime;
        }
        else if (holdRemainingMs > 0.0f)
        {
            // Hold: marker stays exactly where it is, in both fall modes.
            holdRemainingMs = std::max(0.0f, holdRemainingMs - (float)(dt * 1000.0));
        }
        else if (g_Settings.peakFallMode == PeakFallMode::Constant)
        {
            peakY += (float)(g_Settings.peakFallSpeedPxPerSec * dt);
        }
        else // Gravity
        {
            velocity += (float)(g_Settings.peakGravity * dt);
            // Terminal velocity - a full-height fall in a quarter second.
            // Internal only: not a user setting, not persisted. Without
            // it a marker falling the height of a very tall (e.g. 4K)
            // window reaches an absurd speed before landing.
            const float terminalVelocity = 4.0f * (float)totalHeight;
            velocity = std::min(velocity, terminalVelocity);
            peakY += velocity * (float)dt;
        }

        // Clamp to the floor; velocity resets to 0 on landing so a later
        // fall (after a paused/quiet stretch) starts clean rather than
        // carrying leftover speed into what should read as a fresh drop.
        if (peakY >= (float)area.bottom)
        {
            peakY = (float)area.bottom;
            velocity = 0.0f;
        }

        peakY = std::max(peakY, (float)area.top + kPeakTopClearancePx);

        out.push_back({ barRect, rawValue, rawDb, peakY });

        x += barWidth + spacing;
    }

    if (emitDebugLog && g_Settings.debugLogging)
    {
        ULONGLONG now = GetTickCount64();
        if (now - m_LastDebugLogTick >= 1000)
        {
            m_LastDebugLogTick = now;
            LogDebugSample(debugBass, debugMid, debugTreble, n);

            // TEMP DIAGNOSTIC - frame-rate-independence verification (Code
            // Contract §7 step 2). Confirms effective == configured at
            // ~32fps (dt ~= 1/32) and that effective rises above configured
            // at lower frame rates. Remove this block once that's signed
            // off; it's not part of the Code Contract.
            wchar_t smoothLine[192];
            swprintf_s(smoothLine,
                L"SMOOTHDT dt_ms=%.2f attack_cfg=%.4f attack_eff=%.4f decay_cfg=%.4f decay_eff=%.4f",
                dt * 1000.0, g_Settings.barSmoothingAttack, effectiveAttack, g_Settings.barSmoothingDecay, effectiveDecay);
            AppendDebugLog(smoothLine);
        }
    }
}

void CVisualization::DrawBarVisuals(HDC dc, const RECT& area, const std::vector<BarLayout>& bars)
{
    for (const BarLayout& bar : bars)
    {
        if (g_Settings.backgroundBarsEnabled)
            DrawBackgroundBarLayer(dc, bar.rect, area);

        if (g_Settings.barStyle == BarStyle::LED)
            DrawLedBar(dc, bar.rect, area);
        else
            DrawSmoothBar(dc, bar.rect, area);

        if (g_Settings.peakMarkers)
            DrawPeakMarker(dc, bar.rect, area, bar.peakY);
    }
}

void CVisualization::DrawBackgroundBarLayer(HDC dc, const RECT& barRect, const RECT& area)
{
    if (g_Settings.barStyle == BarStyle::LED)
    {
        // Same segment grid as DrawLedBar, just with no litTopThreshold -
        // every segment top-to-bottom is drawn, representing the always-
        // present "unlit" LEDs of a hardware meter.
        const int segH = std::max(1, g_Settings.segmentHeight);
        const int gap = std::max(1, g_Settings.segmentGap);
        const int step = segH + gap;

        for (int y = area.bottom; y > area.top; y -= step)
        {
            int segTop = std::max((int)area.top, y - segH);
            RECT segRect = { barRect.left, segTop, barRect.right, y };

            COLORREF color;
            if (g_Settings.backgroundBarColorMode == ColorMode::Gradient)
                color = ColorAtHeight(g_Settings.backgroundBarGradientTop, g_Settings.backgroundBarGradientBottom, segTop, area);
            else
                color = g_Settings.backgroundBarColorSolid;

            FillSolid(dc, segRect, color);
        }
    }
    else
    {
        RECT fullRect = { barRect.left, area.top, barRect.right, area.bottom };
        if (g_Settings.backgroundBarColorMode == ColorMode::Gradient)
            GradientFillVert(dc, fullRect, g_Settings.backgroundBarGradientTop, g_Settings.backgroundBarGradientBottom);
        else
            FillSolid(dc, fullRect, g_Settings.backgroundBarColorSolid);
    }
}

void CVisualization::DrawLedBar(HDC dc, const RECT& barRect, const RECT& area)
{
    const int segH = std::max(1, g_Settings.segmentHeight);
    const int gap = std::max(1, g_Settings.segmentGap);
    const int step = segH + gap;
    const int litTopThreshold = barRect.top; // segments whose top is at/above this are lit

    for (int y = area.bottom; y > area.top; y -= step)
    {
        int segTop = std::max((int)area.top, y - segH);
        if (segTop < litTopThreshold)
            break; // this and all further-up segments are unlit

        RECT segRect = { barRect.left, segTop, barRect.right, y };

        COLORREF color;
        if (g_Settings.barColorMode == ColorMode::Gradient)
            color = ColorAtHeight(g_Settings.barGradientTop, g_Settings.barGradientBottom, segTop, area);
        else
            color = g_Settings.barColorSolid;

        FillSolid(dc, segRect, color);
    }
}

void CVisualization::DrawSmoothBar(HDC dc, const RECT& barRect, const RECT& area)
{
    if (barRect.top >= barRect.bottom)
        return;

    if (g_Settings.barColorMode == ColorMode::Gradient)
    {
        // Clip to the visible (lit) portion of the bar, but paint using a
        // gradient anchored to the full column height, so a bar's color at
        // a given row stays fixed regardless of its current amplitude.
        HRGN clip = CreateRectRgnIndirect(&barRect);
        SelectClipRgn(dc, clip);
        RECT fullColumn = { barRect.left, area.top, barRect.right, area.bottom };
        GradientFillVert(dc, fullColumn, g_Settings.barGradientTop, g_Settings.barGradientBottom);
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
    }
    else
    {
        FillSolid(dc, barRect, g_Settings.barColorSolid);
    }
}

void CVisualization::DrawPeakMarker(HDC dc, const RECT& barRect, const RECT& area, float peakTopY)
{
    RECT r;
    r.left = barRect.left;
    r.right = barRect.right;

    // Peak marker smooth motion (LED only) makes the LED peak behave
    // exactly like Smooth style's already does - peakTopY itself is
    // already a continuous, style-independent float (see ComputeBarLayout),
    // so no separate tracking is needed here, just the same render branch.
    if (g_Settings.barStyle == BarStyle::Smooth ||
        (g_Settings.barStyle == BarStyle::LED && g_Settings.peakSmoothMotion))
    {
        r.top = (int)std::lround(peakTopY);
        r.bottom = r.top + g_Settings.peakMarkerThickness;
    }
    else
    {
        const int segH = std::max(1, g_Settings.segmentHeight);
        const int gap = std::max(1, g_Settings.segmentGap);
        const int step = segH + gap;
        const int segs = g_Settings.peakMarkerHeightSegments;
        const int markerHeight = segH * segs + gap * (segs - 1);

        // Snap the marker onto the segment grid so it reads as a lit LED row.
        int offsetFromBottom = area.bottom - (int)std::lround(peakTopY);
        int segIndex = step > 0 ? offsetFromBottom / step : 0;
        int snappedBottom = area.bottom - segIndex * step;

        r.bottom = snappedBottom;
        r.top = r.bottom - markerHeight;
    }

    // The decisive clamp: LED's r.top/r.bottom above come from segment-grid
    // snapping anchored to area.bottom, not from peakTopY directly, so for
    // a maxed bar the whole rect can land above the clearance line - both
    // edges, not just the top. Clamping only r.top in that case would
    // crush the rect to zero height (r.top pushed down to/past r.bottom)
    // and the marker would silently vanish instead of shrinking. Shifting
    // the whole rect down instead preserves its height and guarantees the
    // clearance regardless of panel height or bar style.
    const int clearanceTop = area.top + (int)std::lround(kPeakTopClearancePx);
    if (r.top < clearanceTop)
    {
        int shift = clearanceTop - r.top;
        r.top += shift;
        r.bottom += shift;
    }

    // Unlike the top, a marker resting at true silence is expected to sit
    // flush against the bottom border, not float above it with a gap - so
    // this clamps r.bottom to touch area.bottom exactly rather than
    // stopping short of it. It still has to preserve height rather than
    // just clamping r.bottom in place (which would crush the rect to zero
    // height when r.top is already at/past area.bottom and make the
    // marker vanish), so r.top is pulled up by the same amount instead.
    if (r.bottom > area.bottom)
    {
        int height = r.bottom - r.top;
        r.bottom = area.bottom;
        r.top = r.bottom - height;
    }

    if (r.top >= r.bottom)
        return;

    FillSolid(dc, r, g_Settings.peakMarkerColor);
}
