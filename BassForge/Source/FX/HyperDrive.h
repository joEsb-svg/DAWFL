#pragma once
#include <array>
#include <cmath>
#include "FxCommon.h"
#include "Oversampler.h"

namespace bf
{
    enum class DriveType : int { Diode = 0, Tube, Folder };

    struct DriveParams
    {
        bool      on = false;
        DriveType type = DriveType::Diode;
        float     amount = 0.3f;     // 0..1  -> 0..40 dB di gain in ingresso
        float     bias = 0.0f;       // -1..1 asimmetria (Diode: soglie diverse; Tube: bias di griglia; Folder: offset)
        int       stages = 2;        // 1..4  (solo Folder)
        int       oversample = 2;    // 1, 2 o 4
        float     outDb = 0.0f;      // livello di uscita
        float     mix = 1.0f;        // 0..1 (dry/wet, il dry e' allineato in latenza)
    };

    /**
     * Hyper-Drive: Diode Clipper / Asymmetric Tube / Sine Wavefolder multi-stadio, con oversampling 1x/2x/4x.
     *
     *  Diode  : y = 1 - exp(-k|x|) con soglie diverse per semionda positiva/negativa (bias): saturazione esponenziale
     *           da diodo, |y| <= 1; con bias 0 solo armoniche dispari, con bias anche le pari.
     *  Tube   : y = tanh(u + 0.3 u^2) con bias di griglia: la semionda positiva satura prima della negativa
     *           (armoniche pari "da triodo"); offset statico sottratto + DC blocker.
     *  Folder : n stadi in cascata y = sin(pi/2 * g_k * y), g_k = 1 + 0.6k: piega l'onda su se' stessa, |y| <= 1.
     *
     * LATENZA COSTANTE: il percorso wet ha la latenza del FIR (0/24/36 campioni per 1x/2x/4x) piu' un ritardo di
     * compensazione fino a kLatency = 36; il dry e' ritardato di kLatency. Cambiare oversampling o bypass non
     * sposta quindi mai la fase rispetto al resto del plugin (il processor ritarda lo stesso il Sub Direct-Out).
     */
    class HyperDrive
    {
    public:
        static constexpr int kLatency = os::kLatency4x;     // 36 campioni
        static constexpr int kChunk = 256;

        void prepare(double sampleRate)
        {
            sr = sampleRate;
            os::halfband();                                  // forza il calcolo dei coefficienti ora
            for (auto& d : dryDelay) d.prepare(kLatency);
            for (auto& d : padDelay) d.prepare(kLatency);
            dcR = 1.0f - (float) (2.0 * kPi * 8.0 / sampleRate);
            reset();
            setParams(prm);
            mix.snap(0.0f);
        }

        void reset() noexcept
        {
            for (auto& c : osc) c.reset();
            for (auto& d : dryDelay) d.reset();
            for (auto& d : padDelay) d.reset();
            dcX.fill(0.0f); dcY.fill(0.0f);
        }

        void setParams(const DriveParams& p) noexcept
        {
            prm = p;
            prm.amount = std::min(std::max(p.amount, 0.0f), 1.0f);
            prm.bias = std::min(std::max(p.bias, -1.0f), 1.0f);
            prm.stages = std::min(std::max(p.stages, 1), 4);
            prm.oversample = p.oversample >= 4 ? 4 : (p.oversample >= 2 ? 2 : 1);
            prm.mix = std::min(std::max(p.mix, 0.0f), 1.0f);
            const int wetLat = prm.oversample == 4 ? os::kLatency4x : (prm.oversample == 2 ? os::kLatency2x : 0);
            for (auto& d : padDelay) d.setDelay(kLatency - wetLat);
            for (auto& d : dryDelay) d.setDelay(kLatency);
            mix.tgt = prm.on ? prm.mix : 0.0f;
        }

        void process(float* L, float* R, int n) noexcept
        {
            float* ch[2] = { L, R };
            for (int base = 0; base < n; base += kChunk)
            {
                const int m = std::min(kChunk, n - base);
                const bool wetActive = mix.cur > 1.0e-6f || mix.tgt > 1.0e-6f;
                if (wetActive && ! wasActive) reset();
                wasActive = wetActive;

                for (int c = 0; c < 2; ++c)
                {
                    float* x = ch[c] + base;
                    std::array<float, kChunk>& dry = dryBuf[(size_t) c];
                    for (int i = 0; i < m; ++i) dry[(size_t) i] = dryDelay[(size_t) c].process(x[i]);   // dry allineato

                    if (! wetActive) { for (int i = 0; i < m; ++i) x[i] = dry[(size_t) i]; continue; }

                    std::array<float, kChunk>& wet = wetBuf[(size_t) c];
                    runWet(c, x, m, wet.data());
                    for (int i = 0; i < m; ++i)
                    {
                        const float w = padDelay[(size_t) c].process(wet[(size_t) i]);
                        const float mx = mix.at(i, m);
                        x[i] = dry[(size_t) i] * (1.0f - mx) + w * mx;
                    }
                }
                mix.finish();
            }
        }

    private:
        // ---- non-linearita' (applicate alla frequenza sovracampionata) -----------------------------------
        static float tanhA(float x) noexcept        // tanh razionale, |y| <= 1
        {
            x = std::min(std::max(x, -3.0f), 3.0f);
            return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
        }

        void shape(float* b, int n) const noexcept
        {
            const float g = dbToGain(prm.amount * 40.0f);
            switch (prm.type)
            {
                case DriveType::Diode:
                {
                    const float kp = std::max(0.2f, 1.0f + 0.6f * prm.bias) * g, kn = std::max(0.2f, 1.0f - 0.6f * prm.bias) * g;
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = b[i];
                        b[i] = x >= 0.0f ? 1.0f - std::exp(-kp * x) : -(1.0f - std::exp(kn * x));
                    }
                    break;
                }
                case DriveType::Tube:
                {
                    const float bias = prm.bias * 0.4f;
                    const float off = tubeCurve(bias);
                    for (int i = 0; i < n; ++i) b[i] = tubeCurve(g * b[i] + bias) - off;
                    break;
                }
                case DriveType::Folder:
                {
                    const float bias = prm.bias * 0.5f;
                    for (int i = 0; i < n; ++i)
                    {
                        float y = g * b[i] + bias;
                        for (int s = 0; s < prm.stages; ++s)
                            y = std::sin(1.57079633f * y * (1.0f + 0.6f * (float) s));
                        b[i] = y;
                    }
                    break;
                }
            }
        }
        static float tubeCurve(float u) noexcept { return tanhA(u + 0.3f * u * u); }

        void runWet(int c, const float* in, int m, float* out) noexcept
        {
            const float outG = dbToGain(prm.outDb);
            const int f = prm.oversample;
            if (f == 1)
            {
                for (int i = 0; i < m; ++i) out[i] = in[i];
                shape(out, m);
            }
            else
            {
                std::array<float, 4 * kChunk>& ob = osBuf[(size_t) c];
                osc[(size_t) c].up(in, m, ob.data(), f);
                shape(ob.data(), m * f);
                osc[(size_t) c].down(ob.data(), m, out, f);
            }
            // DC blocker (le curve asimmetriche generano offset) + livello di uscita
            float& x1 = dcX[(size_t) c]; float& y1 = dcY[(size_t) c];
            for (int i = 0; i < m; ++i)
            {
                const float x = out[i];
                const float y = x - x1 + dcR * y1;
                x1 = x; y1 = y;
                out[i] = y * outG;
            }
        }

        double sr = 44100.0;
        DriveParams prm{};
        Ramp mix{};
        bool wasActive = false;
        float dcR = 0.999f;
        std::array<float, 2> dcX{}, dcY{};
        std::array<OversamplerChannel, 2> osc{};
        std::array<SampleDelay, 2> dryDelay{}, padDelay{};
        std::array<std::array<float, kChunk>, 2> dryBuf{}, wetBuf{};
        std::array<std::array<float, 4 * kChunk>, 2> osBuf{};
    };
}
