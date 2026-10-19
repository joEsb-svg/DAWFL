#pragma once
#include <cmath>

namespace bf
{
    /**
     * Deformazione di curva monotona su [0,1] -> [0,1] con estremi fissi (0->0, 1->1), senza exp/log:
     * funzione razionale, quindi economica anche per-campione.
     *   c = 0  : lineare
     *   c > 0  : ESPONENZIALE  (parte lenta, finisce veloce)       y = x / (1 + k(1-x))
     *   c < 0  : LOGARITMICA   (parte veloce, finisce lenta)       y = x(1+k) / (1 + kx)
     * k = 24|c|. Il denominatore e' sempre >= 1: nessuna divisione per zero.
     */
    inline float curveWarp(float x, float c) noexcept
    {
        const float k = 24.0f * std::fabs(c);
        if (k < 1.0e-4f) return x;
        return c > 0.0f ? x / (1.0f + k * (1.0f - x))
                        : x * (1.0f + k) / (1.0f + k * x);
    }
}
