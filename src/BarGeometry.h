#pragma once

// Shared bar-row geometry math, used by both the renderer
// (Visualization.cpp's ComputeBarLayout) and the "[Environment]" clipboard
// diagnostics (OptionsFrame.cpp's BuildEnvironmentSection) - unified into
// one function so EnvEffectiveBarWidth/EnvEffectiveBarCount can never
// silently diverge from what the renderer actually laid out.
struct BarGeometry
{
    int effectiveBarCount;  // <= the requested bar count; only reduced if squeezing spacing alone can't make the row fit
    int effectiveSpacing;   // <= the requested spacing; reduced toward a floor of 1 before bar count is ever touched
    int effectiveBarWidth;  // >= 1 always; individual rendered bars may vary by ~1px around this via cumulative rounding
};

// drawableWidth: the width bars actually have to lay out in, in px (already
// margin/border-inset). requestedBarCount/requestedSpacing: the user's
// configured "Number of bars" / "Bar spacing" settings - passing the raw,
// unclamped stored values is fine, they're clamped internally to their
// documented ranges (8-128, 0-20) first.
//
// Guarantees the fit invariant
//   effectiveBarCount * effectiveBarWidth + (effectiveBarCount - 1) * effectiveSpacing <= drawableWidth
// by reducing, in order: effectiveSpacing down to a floor of 1, then
// effectiveBarCount down (to a floor of 1) until effectiveBarWidth reaches
// at least 7px. Never touches the caller's stored settings - this is a
// render-time-only adjustment; the settings dialog keeps showing what the
// user configured.
BarGeometry ComputeBarGeometry(int drawableWidth, int requestedBarCount, int requestedSpacing);
