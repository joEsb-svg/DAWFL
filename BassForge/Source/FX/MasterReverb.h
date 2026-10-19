#pragma once
#include <vector>
#include "FxCommon.h"

namespace bf
{
    struct ReverbParams
    {
        bool  on = false;
        float size = 0.5f;         // 0..1 (tempo di decadimento)
        float damping = 0.4f;      // 0..1 (piu' alto = piu' scuro)
        float width = 1.0f;        // 0..1
        float preDelayMs = 10.0f;  // 0..100
        float hpfHz = 200.0f;      // PASSA-ALTO sul canale WET
        float mix = 0.25f;         // livello del wet (il dry non cambia)
    };

    /**
     * Riverbero stereo di tipo Schroeder/Freeverb: 8 filtri comb con damping + 4 allpass per canale (lunghezze
     * scalate alla frequenza di campionamento, canale destro con spread), pre-delay, e filtro passa-alto
     * (24 dB/ott) sul WET cosi' i bassi restano asciutti e centrati. Uscita = dry + HPF(wet) * mix.
     */
    class MasterReverb
    {
    public:
        static constexpr int kCombs = 8, kAllpass = 4;

        void prepare(double sampleRate)
        {
            sr = sampleRate;
            static constexpr int combLen[kCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static constexpr int apLen[kAllpass] = { 556, 441, 341, 225 };
            const double k = sampleRate / 44100.0;
            for (int c = 0; c < 2; ++c)
            {
                const int spread = c * (int) std::lround(23.0 * k);
                for (int i = 0; i < kCombs; ++i) comb[(size_t) c][(size_t) i].alloc((int) std::lround(combLen[i] * k) + spread);
                for (int i = 0; i < kAllpass; ++i) ap[(size_t) c][(size_t) i].alloc((int) std::lround(apLen[i] * k) + spread);
            }
            pre.prepare((int) (0.1 * sampleRate) + 4);
            setParams(prm); reset(); mix.snap(0.0f);
        }

        void reset() noexcept
        {
            for (auto& ch : comb) for (auto& c : ch) c.clear();
            for (auto& ch : ap) for (auto& a : ch) a.clear();
            pre.reset();
            for (auto& h : hpf) { h[0].reset(); h[1].reset(); }
        }

        void setParams(const ReverbParams& p) noexcept
        {
            prm = p;
            feedback = 0.70f + 0.28f * std::min(std::max(p.size, 0.0f), 1.0f);
            damp = 0.7f * std::min(std::max(p.damping, 0.0f), 1.0f);
            const float wd = std::min(std::max(p.width, 0.0f), 1.0f);
            wet1 = 0.5f * wd + 0.5f; wet2 = 0.5f * (1.0f - wd);
            pre.setDelay((int) (std::min(std::max(p.preDelayMs, 0.0f), 100.0f) * 0.001f * (float) sr));
            if (p.hpfHz != cHpf)
            {
                cHpf = p.hpfHz;
                for (auto& h : hpf) { h[0].setHighpass((double) std::min(std::max(p.hpfHz, 20.0f), 2000.0f), 0.70710678, sr); h[1].setHighpass((double) std::min(std::max(p.hpfHz, 20.0f), 2000.0f), 0.70710678, sr); }
            }
            mix.tgt = p.on ? std::min(std::max(p.mix, 0.0f), 1.0f) : 0.0f;
        }

        void process(float* L, float* R, int n) noexcept
        {
            const bool active = mix.cur > 1.0e-6f || mix.tgt > 1.0e-6f;
            if (! active) { if (wasActive) reset(); wasActive = false; return; }
            wasActive = true;

            for (int i = 0; i < n; ++i)
            {
                const float in = pre.process(0.5f * (L[i] + R[i])) * kInGain;
                float o[2] = { 0.0f, 0.0f };
                for (int c = 0; c < 2; ++c)
                {
                    float s = 0.0f;
                    for (auto& cb : comb[(size_t) c]) s += cb.process(in, feedback, damp);
                    for (auto& a : ap[(size_t) c]) s = a.process(s);
                    o[c] = s;
                }
                const float wl = hpf[0][1].process(hpf[0][0].process(o[0] * wet1 + o[1] * wet2));
                const float wr = hpf[1][1].process(hpf[1][0].process(o[1] * wet1 + o[0] * wet2));
                const float mx = mix.at(i, n);
                L[i] += wl * mx;
                R[i] += wr * mx;
            }
            mix.finish();
        }

    private:
        static constexpr float kInGain = 0.015f * 3.0f;      // calibrato: wet RMS ~ ingresso RMS a size 0.5

        struct Comb
        {
            std::vector<float> b; int idx = 0; float store = 0.0f;
            void alloc(int n) { b.assign((size_t) std::max(n, 2), 0.0f); idx = 0; store = 0.0f; }
            void clear() noexcept { std::fill(b.begin(), b.end(), 0.0f); store = 0.0f; idx = 0; }
            float process(float in, float fb, float damp) noexcept
            {
                const float out = b[(size_t) idx];
                store = out * (1.0f - damp) + store * damp;
                b[(size_t) idx] = in + store * fb;
                if (++idx >= (int) b.size()) idx = 0;
                return out;
            }
        };
        struct Allpass
        {
            std::vector<float> b; int idx = 0;
            void alloc(int n) { b.assign((size_t) std::max(n, 2), 0.0f); idx = 0; }
            void clear() noexcept { std::fill(b.begin(), b.end(), 0.0f); idx = 0; }
            float process(float in) noexcept
            {
                const float bo = b[(size_t) idx];
                const float out = -in + bo;
                b[(size_t) idx] = in + bo * 0.5f;
                if (++idx >= (int) b.size()) idx = 0;
                return out;
            }
        };

        double sr = 44100.0;
        ReverbParams prm{};
        Ramp mix{};
        bool wasActive = false;
        float feedback = 0.84f, damp = 0.28f, wet1 = 1.0f, wet2 = 0.0f, cHpf = -1.0f;
        std::array<std::array<Comb, kCombs>, 2> comb;
        std::array<std::array<Allpass, kAllpass>, 2> ap;
        SampleDelay pre;
        Biquad hpf[2][2];
    };
}
