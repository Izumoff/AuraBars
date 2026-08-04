#include "FrequencyMap.h"
#include <algorithm>
#include <cmath>

namespace
{
    struct BinFreqPoint { int bin; double hz; };

    // Vendored verbatim from the AIMP SDK's documented bin->frequency
    // comment block, apiVisuals.pas:54-81, SDK v5.40 build 2650
    // (A:\_Data\Projects\AuraBars\aimp-sdk\Sources\Delphi\apiVisuals.pas).
    // This is a hand-maintained duplicate of upstream documentation, not a
    // derived/computed table - re-check it against the same comment block
    // whenever the AIMP SDK is updated; do not "improve" or re-derive it.
    const BinFreqPoint kTable[26] = {
        {   0,    20.0 },
        {  10,   200.0 },
        {  20,   400.0 },
        {  30,   630.0 },
        {  40,   870.0 },
        {  50,  1100.0 },
        {  60,  1400.0 },
        {  70,  1700.0 },
        {  80,  2100.0 },
        {  90,  2500.0 },
        { 100,  3000.0 },
        { 110,  3500.0 },
        { 120,  4000.0 },
        { 130,  4500.0 },
        { 140,  5200.0 },
        { 150,  6000.0 },
        { 160,  6700.0 },
        { 170,  7600.0 },
        { 180,  8500.0 },
        { 190,  9600.0 },
        { 200, 11000.0 },
        { 210, 12000.0 },
        { 220, 13500.0 },
        { 230, 15000.0 },
        { 240, 16800.0 },
        { 255, 20000.0 },
    };
    constexpr int kTableLast = 25; // index of the final row (bin 255 / 20kHz)
}

double FrequencyToBin(double hz)
{
    hz = std::clamp(hz, kTable[0].hz, kTable[kTableLast].hz);

    for (int i = 0; i < kTableLast; ++i)
    {
        if (hz > kTable[i + 1].hz)
            continue;

        double fLo = kTable[i].hz, fHi = kTable[i + 1].hz;
        int binLo = kTable[i].bin, binHi = kTable[i + 1].bin;
        if (hz <= fLo)
            return (double)binLo;

        // Linear in bin index against log frequency (Code Contract §3) -
        // interpolating against linear frequency visibly misplaces bars
        // within each 10-bin segment.
        double t = (std::log(hz) - std::log(fLo)) / (std::log(fHi) - std::log(fLo));
        return binLo + t * (binHi - binLo);
    }

    return (double)kTable[kTableLast].bin; // hz was clamped to the table max above; unreachable
}

double BinToFrequency(double bin)
{
    bin = std::clamp(bin, (double)kTable[0].bin, (double)kTable[kTableLast].bin);

    for (int i = 0; i < kTableLast; ++i)
    {
        if (bin > kTable[i + 1].bin)
            continue;

        double binLo = kTable[i].bin, binHi = kTable[i + 1].bin;
        double fLo = kTable[i].hz, fHi = kTable[i + 1].hz;
        if (bin <= binLo)
            return fLo;

        // Inverse of FrequencyToBin's interpolation: still linear in bin
        // index against log frequency, solved for frequency instead of bin.
        double t = (bin - binLo) / (binHi - binLo);
        return std::exp(std::log(fLo) + t * (std::log(fHi) - std::log(fLo)));
    }

    return kTable[kTableLast].hz; // bin was clamped to the table max above; unreachable
}
