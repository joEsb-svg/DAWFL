#pragma once
#include <vector>
#include "FxCommon.h"
#include "../Modulation/ModSync.h"

namespace bf
{
    struct DelayParams
    {
        bool  on = false;
        float timeMs = 250.0f;     // 1..2000
        bool  sync = false;        // true: durata = kSyncBeats[division] al BPM host
        int   division = 5;
        float feedback = 0.35f;    // 0..0.95
        bool  pingPong = false;
        float dampHz = 8000.0f;    // passa-basso nel loop di feedback
        float hpfHz = 150.0f;      // PASSA-ALTO sul canale WET (nel loop i bassi restano, all'uscita no)
        float mix = 0.3f;          // livello del wet (il dry non cambia)
        double bpm = 120.0;
    };

    /**
     * Delay stereo con feedback, ping-pong opzionale, tempo con smoothing (nessun click), soft-clip nel loop
     * e filtro passa-alto (24 dB/ott) sul solo canale WET.
     * Uscita = dry + HPF(wet) * mix.
     */
    class MasterDelay
    {
    public:
        void prepare(double sampleRate)
        {
            sr = sampleRate;
            const int n = (int) (2.05 * sampleRate) + 8;
            for (auto& b : buf) b.assign((size_t) n, 0.0f);
            size = n; w = 0;
            timeCoef = (float) (1.0 - std::exp(-1.0 / (0.08 * sr)));
            curTime = tgtTime = 0.25 * sr;
            setParams(prm); reset(); mix.snap(0.0f);
        }

        void reset() noexcept
        {
            for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& h : hpf) { h[0].reset(); h[1].reset(); }
            lp[0] = lp[1] = 0.0f; w = 0;
        }

        void setParams(const DelayParams& p) noexcept
        {
            prm = p;
            double t = p.sync ? (double) kSyncBeats[std::min(std::max(p.division, 0), kNumSyncDivisions - 1)] * 60.0 / std::max(p.bpm, 20.0)
                              : (double) p.timeMs * 0.001;
            t = std::min(std::max(t, 0.001), 2.0);
            tgtTime = t * sr;
            lpCoef = 1.0f - std::exp(-(float) (2.0 * kPi * (double) std::min(std::max(p.dampHz, 200.0f), 20000.0f) / sr));
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

            const float fb = std::min(std::max(prm.feedback, 0.0f), 0.95f);
            for (int i = 0; i < n; ++i)
            {
                curTime += (tgtTime - curTime) * (double) timeCoef;
                const double dly = std::max(curTime, 1.0);
                const float dL = read(0, dly), dR = read(1, dly);

                // damping nel loop + soft-clip (il feedback non puo' esplodere)
                lp[0] += lpCoef * (dL - lp[0]);
                lp[1] += lpCoef * (dR - lp[1]);
                const float fL = softClip(fb * lp[0]), fR = softClip(fb * lp[1]);

                if (prm.pingPong)
                {
                    buf[0][(size_t) w] = 0.5f * (L[i] + R[i]) + fR;      // R alimenta L
                    buf[1][(size_t) w] = fL;                             // L alimenta R
                }
                else
                {
                    buf[0][(size_t) w] = L[i] + fL;
                    buf[1][(size_t) w] = R[i] + fR;
                }
                w = w + 1 < size ? w + 1 : 0;

                const float wl = hpf[0][1].process(hpf[0][0].process(dL));      // HPF sul wet
                const float wr = hpf[1][1].process(hpf[1][0].process(dR));
                const float mx = mix.at(i, n);
                L[i] += wl * mx;
                R[i] += wr * mx;
            }
            mix.finish();
        }

    private:
        static float softClip(float x) noexcept
        {
            x = std::min(std::max(x, -1.5f), 1.5f);
            return x - (4.0f / 27.0f) * x * x * x;
        }
        float read(int c, double delay) const noexcept
        {
            double pos = (double) w - delay;
            if (pos < 0.0) pos += (double) size;
            int i0 = (int) pos; const float f = (float) (pos - (double) i0);
            if (i0 >= size) i0 = size - 1;
            const int i1 = i0 + 1 < size ? i0 + 1 : 0;
            return buf[(size_t) c][(size_t) i0] * (1.0f - f) + buf[(size_t) c][(size_t) i1] * f;
        }

        double sr = 44100.0;
        DelayParams prm{};
        Ramp mix{};
        bool wasActive = false;
        std::array<std::vector<float>, 2> buf;
        int size = 1, w = 0;
        double curTime = 0, tgtTime = 0; float timeCoef = 0.001f, lpCoef = 0.5f, cHpf = -1.0f;
        float lp[2] = { 0.0f, 0.0f };
        Biquad hpf[2][2];
    };
}
