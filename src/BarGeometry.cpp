#include "BarGeometry.h"
#include <algorithm>

namespace
{
    // Floor division, never negative - the same shape of computation the
    // renderer and the diagnostics both need for "how wide would a bar be
    // at this count/spacing". Deliberately NOT clamped up to 1 here: the
    // whole point is to let the caller see a true (possibly 0 or negative-
    // before-clamping) result so it knows whether spacing/count still need
    // to be reduced, rather than a floor masking that fact.
    int NaiveBarWidth(int drawableWidth, int count, int spacing)
    {
        int totalSpacing = spacing * (count - 1);
        int availWidth = std::max(0, drawableWidth - totalSpacing);
        return availWidth / count;
    }
}

BarGeometry ComputeBarGeometry(int drawableWidth, int requestedBarCount, int requestedSpacing)
{
    const int dw = std::max(0, drawableWidth);
    int n = std::clamp(requestedBarCount, 8, 128);
    int spacing = std::clamp(requestedSpacing, 0, 20);

    int width = NaiveBarWidth(dw, n, spacing);

    // Phase 1: squeeze spacing toward its floor of 1 before ever touching
    // bar count - the less disruptive change, since it doesn't remove
    // anything from the row, just packs it tighter. Because width comes
    // from floor division, n*width + (n-1)*spacing <= dw holds by
    // construction at every iteration, not just once this loop stops - the
    // fit invariant is a consequence of how width is computed here, not a
    // separate check bolted on afterward.
    while (width < 1 && spacing > 1)
    {
        --spacing;
        width = NaiveBarWidth(dw, n, spacing);
    }

    // Phase 2: spacing is now at its floor (or was never the problem). If
    // bars would still be uncomfortably thin, start dropping bar count -
    // the more disruptive change, so it's the last resort. Same floor-
    // division guarantee keeps the invariant satisfied throughout.
    while (width < 7 && n > 1)
    {
        --n;
        width = NaiveBarWidth(dw, n, spacing);
    }

    // Absolute last-resort floor: only reachable when dw itself is 0 (a
    // zero-width drawable area has nothing meaningful to render regardless
    // of any of this math). Visualization.cpp's Draw() also clips bar/peak
    // drawing to the drawable rect as a backstop, so this can never paint
    // outside it even in that degenerate case.
    width = std::max(1, width);

    BarGeometry g;
    g.effectiveBarCount = n;
    g.effectiveSpacing = spacing;
    g.effectiveBarWidth = width;
    return g;
}
