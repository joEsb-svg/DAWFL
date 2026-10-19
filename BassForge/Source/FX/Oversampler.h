#pragma once
#include <array>
#include <cmath>
#include "FxCommon.h"

namespace bf
{
    /**
     * Oversampling 2x / 4x con filtri halfband FIR a fase lineare (49 tappe, finestra di Kaiser beta = 9,
     * ~ -90 dB di reiezione, banda passante piatta fino a ~0.38 fs), in forma POLIFASE (meta' dei calcoli).
     *
     * Latenza (campioni alla frequenza di base), esatta e intera per costruzione (centro c = 24):
     *   2x : c            = 24
     *   4x : c + c/2      = 36
     */
    namespace os
    {
        inline constexpr int kTaps = 49, kCenter = 24;
        inline constexpr int kLatency2x = 24, kLatency4x = 36;

        inline double bessel0(double x) noexcept
        {
            double s = 1.0, t = 1.0; const double q = x * x * 0.25;
            for (int k = 1; k < 40; ++k) { t *= q / ((double) k * (double) k); s += t; }
            return s;
        }

        /** Coefficienti halfband (somma = 1). Calcolati una volta in prepare(). */
        inline const std::array<float, kTaps>& halfband() noexcept
        {
            static const std::array<float, kTaps> h = []
            {
                std::array<double, kTaps> d{};
                double sum = 0.0; const double beta = 9.0, i0b = bessel0(beta);
                for (int k = 0; k < kTaps; ++k)
                {
                    const double m = (double) (k - kCenter);
                    const double sinc = m == 0.0 ? 1.0 : std::sin(kPi * m * 0.5) / (kPi * m * 0.5);
                    const double r = m / (double) kCenter;
                    const double win = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                    d[(size_t) k] = 0.5 * sinc * win;
                    sum += d[(size_t) k];
                }
                std::array<float, kTaps> out{};
                for (int k = 0; k < kTaps; ++k) out[(size_t) k] = (float) (d[(size_t) k] / sum);
                return out;
            }();
            return h;
        }

        /** Interpolatore x2 polifase: per ogni ingresso produce due uscite (y0, y1). */
        struct Up
        {
            std::array<float, 64> x{}; int w = 0;
            void reset() noexcept { x.fill(0.0f); w = 0; }
            void process(float in, float& y0, float& y1) noexcept
            {
                const auto& h = halfband();
                x[(size_t) w] = in;
                float a = 0.0f, b = 0.0f;
                for (int j = 0; j < 25; ++j) a += 2.0f * h[(size_t) (2 * j)] * x[(size_t) ((w - j) & 63)];
                for (int j = 0; j < 24; ++j) b += 2.0f * h[(size_t) (2 * j + 1)] * x[(size_t) ((w - j) & 63)];
                y0 = a; y1 = b; w = (w + 1) & 63;
            }
        };

        /** Decimatore x2 polifase: da due campioni (v[2n], v[2n+1]) a uno. */
        struct Down
        {
            std::array<float, 64> x0{}, x1{}; int w = 0;
            void reset() noexcept { x0.fill(0.0f); x1.fill(0.0f); w = 0; }
            float process(float a, float b) noexcept
            {
                const auto& h = halfband();
                x0[(size_t) w] = a; x1[(size_t) w] = b;
                float y = 0.0f;
                for (int j = 0; j < 25; ++j) y += h[(size_t) (2 * j)] * x0[(size_t) ((w - j) & 63)];
                for (int j = 0; j < 24; ++j) y += h[(size_t) (2 * j + 1)] * x1[(size_t) ((w - j - 1) & 63)];
                w = (w + 1) & 63;
                return y;
            }
        };
    }

    /** Un canale di oversampling (fattore 2 o 4). `out` deve contenere n * factor campioni. */
    struct OversamplerChannel
    {
        os::Up up1, up2;
        os::Down dn1, dn2;

        void reset() noexcept { up1.reset(); up2.reset(); dn1.reset(); dn2.reset(); }

        void up(const float* in, int n, float* out, int factor) noexcept
        {
            if (factor == 2)
                for (int i = 0; i < n; ++i) up1.process(in[i], out[2 * i], out[2 * i + 1]);
            else
                for (int i = 0; i < n; ++i)
                {
                    float a, b; up1.process(in[i], a, b);
                    up2.process(a, out[4 * i], out[4 * i + 1]);
                    up2.process(b, out[4 * i + 2], out[4 * i + 3]);
                }
        }

        void down(const float* osb, int n, float* out, int factor) noexcept
        {
            if (factor == 2)
                for (int i = 0; i < n; ++i) out[i] = dn1.process(osb[2 * i], osb[2 * i + 1]);
            else
                for (int i = 0; i < n; ++i)
                {
                    const float a = dn2.process(osb[4 * i], osb[4 * i + 1]);
                    const float b = dn2.process(osb[4 * i + 2], osb[4 * i + 3]);
                    out[i] = dn1.process(a, b);
                }
        }
    };
}
