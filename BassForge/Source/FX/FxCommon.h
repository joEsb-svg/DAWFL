#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Blocchi di base degli FX master. Nessuna dipendenza da JUCE; le allocazioni avvengono solo in prepare().
namespace bf
{
    inline constexpr double kPi = 3.14159265358979323846;

    inline float dbToGain(float db) noexcept { return std::exp2(db * 0.16609640474f); }                 // 10^(db/20)
    inline float gainToDb(float g) noexcept  { return 6.02059991f * std::log2(std::max(g, 1.0e-9f)); }

    /** Protezione finale: NaN -> 0, limite duro a +-lim (default +18 dBFS: solo rete di sicurezza). */
    inline float sanitize(float x, float lim = 8.0f) noexcept
    {
        return (x != x) ? 0.0f : std::min(std::max(x, -lim), lim);
    }

    /** Rampa lineare di un parametro lungo un blocco (niente zipper noise). */
    struct Ramp
    {
        float cur = 0.0f, tgt = 0.0f;
        void snap(float v) noexcept { cur = tgt = v; }
        /** Valore al campione i di n; a fine blocco cur = tgt. */
        float at(int i, int n) const noexcept { return cur + (tgt - cur) * ((float) (i + 1) / (float) n); }
        void finish() noexcept { cur = tgt; }
    };

    /** Biquad TDF-II con stato e coefficienti in double (sicuro anche per crossover a 40-100 Hz). */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        float process(float xf) noexcept
        {
            const double x = (double) xf;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() noexcept { z1 = z2 = 0.0; }

        void setLowpass(double fc, double q, double sr) noexcept  { set(fc, q, sr, false); }
        void setHighpass(double fc, double q, double sr) noexcept { set(fc, q, sr, true); }

    private:
        void set(double fc, double q, double sr, bool hp) noexcept
        {
            fc = std::min(std::max(fc, 5.0), 0.49 * sr);
            const double w0 = 2.0 * kPi * fc / sr, cw = std::cos(w0), al = std::sin(w0) / (2.0 * q), a0 = 1.0 + al;
            if (hp) { b0 = (1.0 + cw) * 0.5 / a0; b1 = -(1.0 + cw) / a0; }
            else    { b0 = (1.0 - cw) * 0.5 / a0; b1 =  (1.0 - cw) / a0; }
            b2 = b0; a1 = -2.0 * cw / a0; a2 = (1.0 - al) / a0;
        }
    };

    /** Linkwitz-Riley del 4o ordine (2 Butterworth in cascata): -6 dB a fc, 24 dB/ott, somma LP+HP piatta. */
    struct LR4
    {
        Biquad s1, s2;
        void setLowpass(double fc, double sr) noexcept  { s1.setLowpass(fc, 0.70710678, sr);  s2.setLowpass(fc, 0.70710678, sr); }
        void setHighpass(double fc, double sr) noexcept { s1.setHighpass(fc, 0.70710678, sr); s2.setHighpass(fc, 0.70710678, sr); }
        float process(float x) noexcept { return s2.process(s1.process(x)); }
        void reset() noexcept { s1.reset(); s2.reset(); }
    };

    /** Ritardo intero in campioni (buffer circolare pre-allocato). */
    class SampleDelay
    {
    public:
        void prepare(int maxDelaySamples)
        {
            int n = 16; while (n <= maxDelaySamples + 1) n <<= 1;
            buf.assign((size_t) n, 0.0f); mask = n - 1; maxD = maxDelaySamples; w = 0; d = 0;
        }
        void setDelay(int samples) noexcept { d = std::min(std::max(samples, 0), maxD); }
        void reset() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
        float process(float x) noexcept
        {
            buf[(size_t) w] = x;
            const float y = buf[(size_t) ((w - d) & mask)];
            w = (w + 1) & mask;
            return y;
        }
    private:
        std::vector<float> buf;
        int mask = 15, maxD = 0, w = 0, d = 0;
    };
}
