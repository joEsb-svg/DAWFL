#pragma once
#include "Wavetable.h"

namespace bf::dsp
{
    /** Interpolazione cubica di Hermite (Catmull-Rom) a 4 punti; t in [0,1) tra y0 e y1. */
    inline float hermite4(float ym1, float y0, float y1, float y2, float t) noexcept
    {
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    /** Lettura Hermite di una tabella con guardie (indici -1..N+1), fase in [0,1]. */
    inline float readTableHermite(const float* tab, float phase01) noexcept
    {
        constexpr int N = WavetableBank::kTableSize;
        float pos = phase01 * (float) N;
        int i = (int) pos;
        if (i >= N) { i -= N; pos -= (float) N; }          // phase == 1.0f per arrotondamento
        const float f = pos - (float) i;
        return hermite4(tab[i - 1], tab[i], tab[i + 1], tab[i + 2], f);
    }
}
