#pragma once
#include <array>
#include <cmath>
#include "FxCommon.h"

namespace bf
{
    struct OttParams
    {
        bool  on = false;
        float depth = 0.5f;      // 0..1: quanto spingono i rapporti di compressione (up e down)
        float time = 0.5f;       // 0..1: 0.5 = tempi base, 1 = 4x piu' lenti, 0 = 4x piu' veloci
        float up = 1.0f;         // 0..1: quantita' di compressione verso l'ALTO (alza i passaggi deboli)
        float down = 1.0f;       // 0..1: quantita' di compressione verso il BASSO (abbassa i picchi)
        float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;       // guadagno di uscita per banda
        float xover1 = 120.0f, xover2 = 2500.0f;               // Hz
        float mix = 1.0f;        // dry/wet parallelo
    };

    /**
     * Compressore multibanda "OTT style": 3 bande (Low / Mid / High) con crossover Linkwitz-Riley 4o ordine
     * (somma delle bande piatta: low e' compensato in fase con l'allpass del 2o crossover), ciascuna con
     * compressione DOWNWARD (sopra soglia) e UPWARD (sotto soglia) contemporanee.
     *
     *   curva statica per banda (livello L in dB):
     *     L > thrDown : guadagno = -(L - thrDown) * (1 - 1/ratioDown)
     *     L < thrUp   : guadagno = +(thrUp - L)   * (1 - 1/ratioUp),  limitato a maxUp e portato a 0 verso il silenzio
     *                   (fade tra -95 e -75 dB: il rumore di fondo non viene gonfiato)
     *   rilevatore di picco COLLEGATO L/R (immagine stereo preservata) con attacco/rilascio per banda; il guadagno
     *   e' calcolato ogni kCtl campioni e interpolato linearmente.
     */
    class MultibandOTT
    {
    public:
        static constexpr int kCtl = 8;
        static constexpr float kThrDownDb = -18.0f, kThrUpDb = -40.0f;

        /** Curva statica (pubblica per i test e per un eventuale display). */
        static float staticGainDb(float levelDb, float ratioDown, float ratioUp, float maxUpDb) noexcept
        {
            float g = 0.0f;
            if (levelDb > kThrDownDb) g -= (levelDb - kThrDownDb) * (1.0f - 1.0f / ratioDown);
            if (levelDb < kThrUpDb)
            {
                const float fade = std::min(std::max((levelDb + 95.0f) * (1.0f / 20.0f), 0.0f), 1.0f);
                g += std::min((kThrUpDb - levelDb) * (1.0f - 1.0f / ratioUp), maxUpDb) * fade;
            }
            return g;
        }

        void prepare(double sampleRate) noexcept { sr = sampleRate; setParams(prm); reset(); mix.snap(0.0f); }

        void reset() noexcept
        {
            for (auto& c : chan) { c.lp1.reset(); c.hp1.reset(); c.lp2.reset(); c.hp2.reset(); c.apLp.reset(); c.apHp.reset(); }
            for (auto& b : band) { b.env = 0.0f; b.gCur = 1.0f; b.gStep = 0.0f; }
            ctlCount = 0;
        }

        void setParams(const OttParams& p) noexcept
        {
            prm = p;
            const double f1 = std::min(std::max((double) p.xover1, 30.0), 0.4 * sr);
            const double f2 = std::min(std::max((double) p.xover2, f1 * 1.5), 0.45 * sr);
            if (f1 != cf1 || f2 != cf2)
            {
                cf1 = f1; cf2 = f2;
                for (auto& c : chan)
                {
                    c.lp1.setLowpass(f1, sr); c.hp1.setHighpass(f1, sr);
                    c.lp2.setLowpass(f2, sr); c.hp2.setHighpass(f2, sr);
                    c.apLp.setLowpass(f2, sr); c.apHp.setHighpass(f2, sr);
                }
            }
            const float d = std::min(std::max(p.depth, 0.0f), 1.0f);
            const float up = std::min(std::max(p.up, 0.0f), 1.0f), dn = std::min(std::max(p.down, 0.0f), 1.0f);
            ratioDown = 1.0f + 9.0f * d * dn;
            ratioUp = 1.0f + 5.0f * d * up;
            maxUp = 6.0f + 18.0f * d * up;
            const float ts = std::exp2(4.0f * (std::min(std::max(p.time, 0.0f), 1.0f) - 0.5f));
            static constexpr float att[3] = { 18.0f, 12.0f, 6.0f }, rel[3] = { 250.0f, 140.0f, 70.0f };       // ms
            for (int b = 0; b < 3; ++b)
            {
                band[(size_t) b].aCoef = 1.0f - std::exp(-1.0f / (att[b] * 0.001f * ts * (float) sr));
                band[(size_t) b].rCoef = 1.0f - std::exp(-1.0f / (rel[b] * 0.001f * ts * (float) sr));
            }
            outGain[0] = dbToGain(p.lowDb); outGain[1] = dbToGain(p.midDb); outGain[2] = dbToGain(p.highDb);
            mix.tgt = p.on ? std::min(std::max(p.mix, 0.0f), 1.0f) : 0.0f;
        }

        void process(float* L, float* R, int n) noexcept
        {
            const bool active = mix.cur > 1.0e-6f || mix.tgt > 1.0e-6f;
            if (active && ! wasActive) reset();
            wasActive = active;
            if (! active) return;

            for (int i = 0; i < n; ++i)
            {
                float bl[3], br[3];
                split(chan[0], L[i], bl);
                split(chan[1], R[i], br);

                if (ctlCount == 0) updateGains();
                if (++ctlCount >= kCtl) ctlCount = 0;

                float yl = 0.0f, yr = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    auto& bd = band[(size_t) b];
                    const float det = std::max(std::fabs(bl[b]), std::fabs(br[b]));
                    bd.env += (det > bd.env ? bd.aCoef : bd.rCoef) * (det - bd.env);
                    bd.gCur += bd.gStep;
                    const float g = bd.gCur * outGain[(size_t) b];
                    yl += bl[b] * g; yr += br[b] * g;
                }
                const float m = mix.at(i, n);
                L[i] = L[i] * (1.0f - m) + yl * m;
                R[i] = R[i] * (1.0f - m) + yr * m;
            }
            mix.finish();
        }

        /** Guadagno attuale (dB) di una banda, per metering. */
        float bandGainDb(int b) const noexcept { return gainToDb(band[(size_t) b].gCur); }

    private:
        struct Chan { LR4 lp1, hp1, lp2, hp2, apLp, apHp; };
        struct Band { float env = 0, gCur = 1, gStep = 0, aCoef = 0.01f, rCoef = 0.001f; };

        static void split(Chan& c, float x, float* out) noexcept
        {
            const float low = c.lp1.process(x), rest = c.hp1.process(x);
            out[0] = c.apLp.process(low) + c.apHp.process(low);        // low compensato in fase (allpass a xover2)
            out[1] = c.lp2.process(rest);
            out[2] = c.hp2.process(rest);
        }

        void updateGains() noexcept
        {
            for (auto& bd : band)
            {
                const float lvDb = 20.0f * std::log10(bd.env + 1.0e-9f);
                const float tgt = dbToGain(staticGainDb(lvDb, ratioDown, ratioUp, maxUp));
                bd.gStep = (tgt - bd.gCur) * (1.0f / (float) kCtl);
            }
        }

        double sr = 44100.0, cf1 = -1.0, cf2 = -1.0;
        OttParams prm{};
        Ramp mix{};
        bool wasActive = false;
        float ratioDown = 2.0f, ratioUp = 2.0f, maxUp = 12.0f;
        std::array<float, 3> outGain{ 1.0f, 1.0f, 1.0f };
        std::array<Chan, 2> chan{};
        std::array<Band, 3> band{};
        int ctlCount = 0;
    };
}
