#pragma once
#include <vector>
#include "FxCommon.h"

namespace bf
{
    struct DimensionParams
    {
        bool  on = false;
        float amount = 0.5f;     // 0..1 pseudo-stereo generato (decorrelazione da M)
        float width = 1.0f;      // 0..2 scala del Side ORIGINALE
        float sizeMs = 12.0f;    // 4..30 ms: ritardo base delle due linee
        float rateHz = 0.6f;     // 0.05..5 Hz: modulazione delle linee
        float monoHz = 120.0f;   // 40..400 Hz: sotto questa frequenza il Side e' azzerato (bassi mono)
        float mix = 1.0f;
    };

    /**
     * Dimension Expander / Spatializer (stile "Dimension D" su Mid/Side).
     *
     *   M = (L+R)/2,  S = (L-R)/2
     *   pseudo-side  = (dA - dB)/2 : due copie di M su linee a ritardo modulato in controfase (LFO sin / -sin)
     *   S' = HP_LR4( S*width + amount*pseudo-side )          <- passa-alto sul SIDE a `monoHz` (24 dB/ott)
     *   L = M + S',  R = M - S'
     *
     * MONO-COMPATIBILE per costruzione: (L+R)/2 = M sempre (la somma mono non cambia, nessuna cancellazione),
     * e sotto monoHz il segnale resta rigorosamente mono (< 120 Hz di default).
     */
    class DimensionExpander
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate;
            const int n = (int) (0.06 * sampleRate) + 16;
            line.assign((size_t) n, 0.0f); size = n; w = 0; phase = 0.0;
            setParams(prm); reset(); mix.snap(0.0f);
        }

        void reset() noexcept
        {
            std::fill(line.begin(), line.end(), 0.0f); w = 0;
            hp.reset();
        }

        void setParams(const DimensionParams& p) noexcept
        {
            prm = p;
            if (p.monoHz != cMono) { cMono = p.monoHz; hp.setHighpass((double) std::min(std::max(p.monoHz, 20.0f), 1000.0f), sr); }
            mix.tgt = p.on ? std::min(std::max(p.mix, 0.0f), 1.0f) : 0.0f;
        }

        void process(float* L, float* R, int n) noexcept
        {
            const bool active = mix.cur > 1.0e-6f || mix.tgt > 1.0e-6f;
            if (active && ! wasActive) reset();
            wasActive = active;
            if (! active) return;

            const float amount = std::min(std::max(prm.amount, 0.0f), 1.0f) * 1.4f;
            const float width = std::min(std::max(prm.width, 0.0f), 2.0f);
            const float base = std::min(std::max(prm.sizeMs, 4.0f), 30.0f) * 0.001f * (float) sr;
            const float depth = base * 0.35f;
            const double dPhase = 2.0 * kPi * (double) std::min(std::max(prm.rateHz, 0.05f), 5.0f) / sr;

            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
                line[(size_t) w] = m;
                const float lfo = (float) std::sin(phase);
                phase += dPhase; if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
                const float dA = read(base + depth * lfo), dB = read(base - depth * lfo);
                const float side = hp.process(s * width + amount * 0.5f * (dA - dB));
                w = w + 1 < size ? w + 1 : 0;

                const float wl = m + side, wr = m - side;
                const float mx = mix.at(i, n);
                L[i] = L[i] * (1.0f - mx) + wl * mx;
                R[i] = R[i] * (1.0f - mx) + wr * mx;
            }
            mix.finish();
        }

    private:
        float read(float delay) const noexcept        // interpolazione lineare, delay >= 1
        {
            float pos = (float) w - std::max(delay, 1.0f);
            if (pos < 0.0f) pos += (float) size;
            int i0 = (int) pos; const float f = pos - (float) i0;
            if (i0 >= size) i0 = size - 1;
            const int i1 = i0 + 1 < size ? i0 + 1 : 0;
            return line[(size_t) i0] * (1.0f - f) + line[(size_t) i1] * f;
        }

        double sr = 44100.0, phase = 0.0;
        float cMono = -1.0f;
        DimensionParams prm{};
        Ramp mix{};
        bool wasActive = false;
        std::vector<float> line;
        int size = 1, w = 0;
        LR4 hp{};
    };
}
