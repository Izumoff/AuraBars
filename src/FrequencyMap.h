#pragma once

// AIMP's Spectrum[] array is not linearly-spaced in frequency - the SDK
// documents a non-uniform bin->frequency mapping that pre-warps the axis.
// The previous bin-to-bar grouping spaced bar edges geometrically over the
// raw bin *index* (0-255), which applied a second warp on top of AIMP's
// own - the double warp was the root cause of the descending-staircase and
// dead-right-edge bugs (Code Contract §1). Grouping is frequency-based
// instead: bar edges are placed at frequencies, then converted to bin
// indices via FrequencyToBin below - never spaced by bin index.
//
// FrequencyToBin/BinToFrequency interpolate log-linearly - linear in bin
// index against log frequency - between the documented points below.
// Interpolating against linear frequency is wrong and visibly misplaces
// bars within each 10-bin segment (Code Contract §3).

// hz is clamped to the table's documented range [20, 20000] before
// mapping. Returns a continuous (non-integer) bin position - round() at
// the call site when an actual Spectrum[] index is needed.
double FrequencyToBin(double hz);

// Inverse of FrequencyToBin. bin is clamped to [0, 255] before mapping.
double BinToFrequency(double bin);
