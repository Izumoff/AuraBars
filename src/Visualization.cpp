#include "Visualization.h"
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
    const float kReferenceMagnitude = 40.0f;

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

    // Appends one timestamped line to %TEMP%\AuraBars_debug.log. Best-effort:
    // a failed open (e.g. locked file) just drops the sample, never crashes
    // the renderer.
    void AppendDebugLog(const wchar_t* line)
    {
        wchar_t path[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, path);
        if (len == 0 || len >= MAX_PATH - 20)
            return;
        wcscat_s(path, L"AuraBars_debug.log");

        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f)
            return;

        time_t t = time(nullptr);
        tm local;
        localtime_s(&local, &t);
        wchar_t timestamp[32];
        wcsftime(timestamp, 32, L"%Y-%m-%d %H:%M:%S", &local);

        fwprintf(f, L"[%s] %s\n", timestamp, line);
        fclose(f);
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
    m_AutoGainCeilingDbLeft.clear();
    m_AutoGainCeilingDbRight.clear();
    EnsureBackBuffer();
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
    EnsureBackBuffer();
    // Stale pixel coordinates from the old size are meaningless.
    m_PeakYLeft.clear();
    m_PeakYRight.clear();
    m_AutoGainCeilingDbLeft.clear();
    m_AutoGainCeilingDbRight.clear();
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
        ComputeBarLayout(leftInner, Data, 0, m_PeakYLeft, m_AutoGainCeilingDbLeft, m_SmoothedLeft, true, leftBars);
        ComputeBarLayout(rightInner, Data, 1, m_PeakYRight, m_AutoGainCeilingDbRight, m_SmoothedRight, false, rightBars);

        bars.reserve(leftBars.size() + rightBars.size());
        bars.insert(bars.end(), leftBars.begin(), leftBars.end());
        bars.insert(bars.end(), rightBars.begin(), rightBars.end());

        borderRects = { leftRect, rightRect };
        innerRects = { leftInner, rightInner };
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
        ComputeBarLayout(inner, Data, -1, m_PeakYLeft, m_AutoGainCeilingDbLeft, m_SmoothedLeft, true, bars);
        borderRects = { barArea };
        innerRects = { inner };
        innerVRange = { area.left, inner.top, area.right, inner.bottom };
    }

    // Grid needs each bar's current top *before* it draws, so it can skip
    // every pixel below that top for that bar's column - LED segment gaps
    // included, since that whole span visually belongs to the bar even
    // where the bar itself doesn't paint every pixel of it. Bounded to
    // each channel's own inner rect (not the whole panel) so grid lines
    // never spill into the margins, the border frame, or - in Stereo - the
    // gap around the separator.
    if (g_Settings.gridLines)
    {
        for (const RECT& r : innerRects)
            DrawGrid(m_MemDC, r, area, bars);
    }

    DrawBarVisuals(m_MemDC, innerVRange, bars);

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

void CVisualization::LogDebugSample(int firstIdx, float firstRaw, float firstDb,
                                     int midIdx, float midRaw, float midDb,
                                     int lastIdx, float lastRaw, float lastDb) const
{
    wchar_t line[320];
    swprintf_s(line,
        L"floor=%.1fdB ceiling=%.1fdB | bar%d raw=%.5f db=%.2f | bar%d raw=%.5f db=%.2f | bar%d raw=%.5f db=%.2f",
        g_Settings.dbFloor, g_Settings.dbCeiling,
        firstIdx + 1, firstRaw, firstDb,
        midIdx + 1, midRaw, midDb,
        lastIdx + 1, lastRaw, lastDb);
    AppendDebugLog(line);
}

void CVisualization::ComputeBarLayout(const RECT& area, const TAIMPVisualData* data, int spectrumChannel,
                                       std::vector<float>& peakState, std::vector<float>& autoGainState,
                                       std::vector<float>& smoothedState,
                                       bool emitDebugLog, std::vector<BarLayout>& out)
{
    const int n = std::clamp(g_Settings.barCount, 8, 128);
    if ((int)peakState.size() != n)
        peakState.assign(n, (float)area.bottom);
    if ((int)autoGainState.size() != n)
        autoGainState.assign(n, (float)g_Settings.dbCeiling);
    if ((int)smoothedState.size() != n)
        smoothedState.assign(n, 0.0f);

    out.clear();
    out.reserve(n);

    const int spacing = g_Settings.barSpacing;
    const int totalSpacing = spacing * (n - 1);
    const int availWidth = std::max(0, (int)(area.right - area.left) - totalSpacing);
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

    // Log-scale bin edges: bar i covers bins [edge(i), edge(i+1)), and the
    // edges grow exponentially rather than linearly. Equal-width linear
    // slices dump a disproportionate share of naturally bass-heavy energy
    // into a large contiguous block of early bars; growing the range
    // exponentially gives bass few, narrow bars (edge(0)=0, edge(1)=1) and
    // treble many, wide ones, like a real spectrum analyzer.
    auto binEdge = [n](int i) -> int
    {
        if (i <= 0)
            return 0;
        if (i >= n)
            return AIMP_VISUAL_SPECTRUM_MAX;
        double t = (double)i / n;
        return (int)std::floor(std::pow((double)AIMP_VISUAL_SPECTRUM_MAX, t));
    };

    // "Bar 20 (or the middle bar)" per the debug-logging spec: bar 20 when
    // there are enough bars, otherwise the middle one.
    const int debugIdxFirst = 0;
    const int debugIdxMid = (n >= 20) ? 19 : n / 2;
    const int debugIdxLast = n - 1;
    float debugRawFirst = 0.0f, debugDbFirst = 0.0f;
    float debugRawMid = 0.0f, debugDbMid = 0.0f;
    float debugRawLast = 0.0f, debugDbLast = 0.0f;

    int x = area.left;
    for (int i = 0; i < n; ++i)
    {
        int binStart = binEdge(i);
        int binEnd = std::max(binStart + 1, binEdge(i + 1));
        binEnd = std::min(binEnd, (int)AIMP_VISUAL_SPECTRUM_MAX);

        // Average, not peak: with log-scale bins the range widens sharply
        // toward the higher bars, and taking the peak over a wide range
        // means a single loud bin anywhere in it pins the bar near max
        // regardless of the region's typical energy. The mean reflects
        // what that frequency range is actually doing.
        //
        // Source channel: Spectrum[0]/[1] are the two channels AIMP's own
        // VisualSpectrumDemo draws; Spectrum[2] is undocumented and unused
        // by any reference code, and empirically runs far hotter than a
        // real magnitude should (bars stayed pinned even at a -20dB
        // ceiling) - almost certainly a raw L+R sum rather than a properly
        // scaled mono value. Mono mode averages the two documented
        // channels; Stereo mode reads the caller's single requested
        // channel so each half genuinely reflects just that channel.
        float sum = 0.0f;
        for (int b = binStart; b < binEnd; ++b)
        {
            if (spectrumChannel < 0)
                sum += 0.5f * (data->Spectrum[0][b] + data->Spectrum[1][b]);
            else
                sum += data->Spectrum[spectrumChannel][b];
        }
        float rawValue = sum / (float)(binEnd - binStart);
        float rawDb = LinearToDb(rawValue);

        if (emitDebugLog && g_Settings.debugLogging)
        {
            if (i == debugIdxFirst) { debugRawFirst = rawValue; debugDbFirst = rawDb; }
            if (i == debugIdxMid)   { debugRawMid = rawValue; debugDbMid = rawDb; }
            if (i == debugIdxLast)  { debugRawLast = rawValue; debugDbLast = rawDb; }
        }

        // Bar smoothing sits before the Linear/Log scaling branch (and
        // before auto-gain ceiling tracking, which feeds that same
        // branch), so both scaling modes and the ceiling all see the same
        // filtered value - the ceiling never hunts against raw transients
        // while the bar height lags smoothly behind it. Peak markers track
        // this for free: they're derived from the resulting scaled bar
        // height below, not a separate raw-magnitude read.
        float scalingInput = rawValue;
        if (g_Settings.barSmoothing)
        {
            float& smoothed = smoothedState[i];
            double rate = (rawValue >= smoothed) ? g_Settings.barSmoothingAttack : g_Settings.barSmoothingDecay;
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

        // Peak marker: snaps up instantly to a louder bar, falls at the
        // configured speed otherwise (both in device pixels/frame). The
        // decay step is clamped to the current frame's own barTopY (not
        // just area.bottom) - without that clamp, a bar rising toward an
        // still-decaying peak faster than the peak falls could leave the
        // decayed peak numerically past (i.e. visually below) the bar's
        // current top for that one frame, which reads as the bar
        // rendering taller than its own peak marker.
        float& peakY = peakState[i];
        float barTopY = (float)barRect.top;
        if (barTopY < peakY)
            peakY = barTopY;
        else
            peakY = std::min(peakY + (float)g_Settings.peakFallSpeed, barTopY);

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
            LogDebugSample(debugIdxFirst, debugRawFirst, debugDbFirst,
                            debugIdxMid, debugRawMid, debugDbMid,
                            debugIdxLast, debugRawLast, debugDbLast);
        }
    }
}

void CVisualization::DrawBarVisuals(HDC dc, const RECT& area, const std::vector<BarLayout>& bars)
{
    for (const BarLayout& bar : bars)
    {
        if (g_Settings.barStyle == BarStyle::LED)
            DrawLedBar(dc, bar.rect, area);
        else
            DrawSmoothBar(dc, bar.rect, area);

        if (g_Settings.peakMarkers)
            DrawPeakMarker(dc, bar.rect, area, bar.peakY);
    }
}

void CVisualization::DrawLedBar(HDC dc, const RECT& barRect, const RECT& area)
{
    const int segH = std::max(1, g_Settings.segmentHeight);
    const int gap = std::max(0, g_Settings.segmentGap);
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

    if (g_Settings.barStyle == BarStyle::Smooth)
    {
        r.top = (int)std::lround(peakTopY);
        r.bottom = r.top + g_Settings.peakMarkerThickness;
    }
    else
    {
        const int segH = std::max(1, g_Settings.segmentHeight);
        const int gap = std::max(0, g_Settings.segmentGap);
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
    r.bottom = std::min(r.bottom, area.bottom);
    if (r.top >= r.bottom)
        return;

    FillSolid(dc, r, g_Settings.peakMarkerColor);
}
