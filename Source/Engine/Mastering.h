#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "Mixer.h"   // mixer::Biquad

//==============================================================================
/**
    Mastering di JLoop: l'ultimo passaggio prima dell'esportazione (e, se acceso, anche dal vivo).

    Catena:  guadagno -> EQ (bassi, presenza, aria; sotto i 25 Hz si taglia) -> compressione "colla"
             -> calore (saturazione) -> stereo (bassi in mono) -> limiter con anticipo e tetto

    "Automatico": si calcola la canzone senza mastering, si misura il volume percepito (LUFS, come
    Spotify e YouTube), l'equilibrio tra bassi e alti, la dinamica e l'apertura stereo; poi si regolano
    i controlli secondo lo stile scelto e il guadagno per arrivare esattamente al volume voluto.
    Dopo si puo' cambiare tutto a mano.
*/
namespace mastering
{
    //==========================================================================
    /** Gli stili: valori di partenza e il "suono di riferimento" per l'analisi. */
    struct Style
    {
        const char* name;
        const char* detail;
        float loudness, glue, warmth, width;
        float lowTarget, highTarget;   // equilibrio voluto (dB dei bassi e degli alti rispetto ai medi)
    };

    inline constexpr int numStyles = 6;

    inline const Style& style (int i)
    {
        static const std::array<Style, numStyles> styles =
        {{
            { "Bilanciato", "Pulito e naturale, va bene quasi per tutto", -12.0f, 0.40f, 0.20f, 1.05f, 6.0f, -12.0f },
            { "Streaming", "Per Spotify, YouTube, Apple Music (-14 LUFS)", -14.0f, 0.30f, 0.15f, 1.00f, 6.0f, -12.0f },
            { "Club / EDM", "Forte e compatto, per casse grandi", -8.5f, 0.60f, 0.30f, 1.10f, 8.0f, -11.0f },
            { "Hip hop / Trap", "Bassi pieni, alti morbidi", -9.5f, 0.50f, 0.35f, 1.00f, 9.0f, -13.0f },
            { "Caldo", "Morbido, analogico, meno alti", -12.0f, 0.45f, 0.55f, 1.00f, 6.5f, -15.0f },
            { "Brillante", "Aperto e luminoso", -11.0f, 0.40f, 0.15f, 1.15f, 5.0f, -9.5f },
        }};
        return styles[(size_t) juce::jlimit (0, numStyles - 1, i)];
    }

    //==========================================================================
    /** Le impostazioni (scritte dalla UI, lette dal thread audio). */
    struct Params
    {
        std::atomic<bool>  enabled  { false };
        std::atomic<int>   styleIndex { 0 };
        std::atomic<float> loudness { -12.0f };   // LUFS voluti
        std::atomic<float> gainDb   { 0.0f };     // guadagno d'ingresso (lo calcola l'analisi)
        std::atomic<float> lowDb    { 0.0f }, presenceDb { 0.0f }, airDb { 0.0f };
        std::atomic<float> glue     { 0.4f };     // compressione 0..1
        std::atomic<float> warmth   { 0.2f };     // saturazione 0..1
        std::atomic<float> width    { 1.0f };     // 0..1.5 (1 = invariato)
        std::atomic<float> ceiling  { -1.0f };    // dBFS

        // Risultati dell'ultima analisi (solo UI)
        bool  analysed = false;
        float measuredLufs = 0.0f, measuredPeakDb = 0.0f, resultLufs = 0.0f, analysedLoudness = 0.0f;

        void applyStyle (int i)
        {
            const auto& s = style (i);
            styleIndex.store (i);
            loudness.store (s.loudness);
            glue.store (s.glue);
            warmth.store (s.warmth);
            width.store (s.width);
        }

        void reset()
        {
            enabled.store (false);
            applyStyle (0);
            gainDb.store (0.0f);
            lowDb.store (0.0f); presenceDb.store (0.0f); airDb.store (0.0f);
            ceiling.store (-1.0f);
            analysed = false;
            measuredLufs = measuredPeakDb = resultLufs = analysedLoudness = 0.0f;
        }

        /** Il guadagno da usare: se il volume voluto e' cambiato dopo l'analisi, si sposta di conseguenza. */
        float effectiveGainDb() const
        {
            if (! analysed)
                return gainDb.load();
            // Col guadagno dell'analisi si ottiene "reached"; piu' piano: si toglie la differenza.
            // Piu' forte: si aggiunge solo oltre il volume chiesto all'analisi (se il mix non ci arrivava, e' gia' al massimo pulito)
            const float target = loudness.load();
            const float reached = juce::jmin (resultLufs, analysedLoudness);
            return target <= reached ? gainDb.load() + (target - reached)
                                     : gainDb.load() + juce::jmax (0.0f, target - analysedLoudness);
        }
    };

    /** Una copia "ferma" dei parametri, letta una volta per blocco. */
    struct Snapshot
    {
        float gainDb = 0, lowDb = 0, presenceDb = 0, airDb = 0, glue = 0.4f, warmth = 0.2f, width = 1, ceiling = -1, loudness = -12;

        static Snapshot from (const Params& p)
        {
            Snapshot s;
            s.gainDb = p.effectiveGainDb();
            s.lowDb = p.lowDb.load(); s.presenceDb = p.presenceDb.load(); s.airDb = p.airDb.load();
            s.glue = p.glue.load(); s.warmth = p.warmth.load(); s.width = p.width.load();
            s.ceiling = juce::jlimit (-6.0f, -0.3f, p.ceiling.load());
            s.loudness = p.loudness.load();
            return s;
        }
    };

    //==========================================================================
    /** La catena di mastering (nessuna allocazione dopo prepare). */
    class Chain
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            lookahead = juce::jmax (1, (int) std::round (0.005 * sr));
            delay[0].assign ((size_t) lookahead + 1, 0.0f);
            delay[1].assign ((size_t) lookahead + 1, 0.0f);
            peakHold.assign ((size_t) lookahead + 1, 0.0f);
            reset();
        }

        void reset()
        {
            for (auto* b : { &hpf, &low, &presence, &air }) b->reset();
            sideHp.reset();
            compEnv = 0.0f;
            limGain = 1.0f;
            windowMax = 0.0f;
            pos = 0;
            for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
            std::fill (peakHold.begin(), peakHold.end(), 0.0f);
            for (auto& s : shown) s = 999.0f;
        }

        void process (float* L, float* R, int n, const Snapshot& p) noexcept
        {
            if (delay[0].empty())
                return;   // non ancora preparata
            updateFilters (p);

            const float gain = juce::Decibels::decibelsToGain (p.gainDb);
            // Compressione "colla": soglia vicina al volume voluto, rapporto morbido, tempi lenti
            const float threshold = p.loudness + 4.0f - p.glue * 8.0f;
            const float ratio = 1.2f + p.glue * 2.8f;
            const float att = (float) std::exp (-1.0 / (0.025 * sr)), rel = (float) std::exp (-1.0 / (0.200 * sr));
            const float drive = 1.0f + p.warmth * 2.5f, satNorm = 1.0f / std::tanh (drive), satMix = p.warmth * 0.6f;
            const float ceilingGain = juce::Decibels::decibelsToGain (p.ceiling);
            const float limRelease = (float) std::exp (-1.0 / (0.080 * sr));
            // Il clipper tocca solo i picchi piu' alti (oltre l'88% del tetto) e solo con la compressione alta:
            // a volumi normali il suono resta pulito
            const float clipStart = ceilingGain * (0.98f - 0.12f * p.glue), clipRange = ceilingGain * 0.35f;
            const int size = (int) delay[0].size();

            for (int i = 0; i < n; ++i)
            {
                float l = L[i] * gain, r = R[i] * gain;

                // EQ
                l = air.process (presence.process (low.process (hpf.process (l, 0), 0), 0), 0);
                r = air.process (presence.process (low.process (hpf.process (r, 1), 1), 1), 1);

                // Compressione (RMS, canali collegati, ginocchio morbido)
                const float sq = 0.5f * (l * l + r * r);
                compEnv = sq > compEnv ? att * compEnv + (1.0f - att) * sq : rel * compEnv + (1.0f - rel) * sq;
                const float levelDb = 10.0f * std::log10 (compEnv + 1e-12f);
                const float over = levelDb - threshold;
                float reductionDb = 0.0f;
                if (over > 3.0f)        reductionDb = over * (1.0f - 1.0f / ratio);
                else if (over > -3.0f)  reductionDb = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
                const float cg = juce::Decibels::decibelsToGain (-reductionDb);
                l *= cg; r *= cg;

                // Calore
                if (satMix > 0.001f)
                {
                    l += (std::tanh (l * drive) * satNorm - l) * satMix;
                    r += (std::tanh (r * drive) * satNorm - r) * satMix;
                }

                // Stereo: lato allargato/stretto, e niente "lato" sotto i 120 Hz (bassi in mono)
                const float m = 0.5f * (l + r);
                float s = 0.5f * (l - r);
                s = sideHp.process (s, 0) * p.width;
                l = m + s;
                r = m - s;

                // Clipper morbido: smussa solo i picchi piu' alti (le casse), cosi' il limiter lavora meno
                l = softClip (l, clipStart, clipRange);
                r = softClip (r, clipStart, clipRange);

                // Limiter con 5 ms di anticipo: il guadagno scende prima che arrivi il picco
                const float peak = juce::jmax (std::abs (l), std::abs (r));
                const float leaving = peakHold[(size_t) pos];
                peakHold[(size_t) pos] = peak;
                if (peak >= windowMax)
                    windowMax = peak;
                else if (leaving >= windowMax)   // e' uscito il massimo: si ricalcola (succede di rado)
                {
                    windowMax = 0.0f;
                    for (auto v : peakHold) windowMax = juce::jmax (windowMax, v);
                }
                const float target = windowMax > ceilingGain ? ceilingGain / windowMax : 1.0f;
                limGain = target < limGain ? target : limRelease * limGain + (1.0f - limRelease) * target;

                const float dl = delay[0][(size_t) pos], dr = delay[1][(size_t) pos];
                delay[0][(size_t) pos] = l;
                delay[1][(size_t) pos] = r;
                pos = (pos + 1) % size;

                L[i] = juce::jlimit (-ceilingGain, ceilingGain, dl * limGain);
                R[i] = juce::jlimit (-ceilingGain, ceilingGain, dr * limGain);
            }
        }

    private:
        static inline float softClip (float x, float start, float range) noexcept
        {
            const float a = std::abs (x);
            if (a <= start)
                return x;
            const float y = start + range * std::tanh ((a - start) / range);
            return x < 0.0f ? -y : y;
        }

        void updateFilters (const Snapshot& p)
        {
            if (shown[0] == p.lowDb && shown[1] == p.presenceDb && shown[2] == p.airDb)
                return;
            shown[0] = p.lowDb; shown[1] = p.presenceDb; shown[2] = p.airDb;
            const double q = 1.0 / std::sqrt (2.0);
            const double w = juce::MathConstants<double>::twoPi * 25.0 / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            hpf.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
            low.setLowShelf (sr, 90.0, p.lowDb);
            presence.setPeak (sr, 3000.0, 0.7, p.presenceDb);
            air.setHighShelf (sr, juce::jmin (10000.0, sr * 0.4), p.airDb);
            const double ws = juce::MathConstants<double>::twoPi * 120.0 / sr, as = std::sin (ws) / (2.0 * q), cs = std::cos (ws);
            sideHp.set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + as, -2 * cs, 1 - as);
        }

        double sr = 48000.0;
        mixer::Biquad hpf, low, presence, air, sideHp;
        float shown[3] { 999.0f, 999.0f, 999.0f };
        float compEnv = 0.0f, limGain = 1.0f, windowMax = 0.0f;
        int lookahead = 240, pos = 0;
        std::vector<float> delay[2], peakHold;
    };

    //==========================================================================
    /** Volume percepito (LUFS integrati, ITU-R BS.1770: pesatura K, blocchi da 400 ms, gate). */
    inline float measureLufs (const float* L, const float* R, size_t n, double sr)
    {
        if (n < (size_t) (0.4 * sr))
            return -70.0f;

        mixer::Biquad shelf, hp;
        {
            // Pesatura K: mensola +4 dB sopra ~1.7 kHz e passa-alto a ~38 Hz
            shelf.setHighShelf (sr, 1681.0, 4.0);
            const double w = juce::MathConstants<double>::twoPi * 38.0 / sr, alpha = std::sin (w) / (2.0 * 0.5), c = std::cos (w);
            hp.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        }

        const size_t hop = (size_t) (0.1 * sr), block = hop * 4;
        std::vector<double> hopEnergy;
        hopEnergy.reserve (n / hop + 1);
        double acc = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const float l = hp.process (shelf.process (L[i], 0), 0);
            const float r = hp.process (shelf.process (R[i], 1), 1);
            acc += (double) l * l + (double) r * r;
            if (++count == hop)
            {
                hopEnergy.push_back (acc);
                acc = 0.0;
                count = 0;
            }
        }

        std::vector<double> blocks;
        for (size_t b = 0; b + 4 <= hopEnergy.size(); ++b)
            blocks.push_back ((hopEnergy[b] + hopEnergy[b + 1] + hopEnergy[b + 2] + hopEnergy[b + 3]) / (double) block);

        auto lufs = [] (double meanSquare) { return -0.691 + 10.0 * std::log10 (meanSquare + 1e-12); };
        double sum = 0.0; int num = 0;
        for (auto e : blocks)
            if (lufs (e) > -70.0) { sum += e; ++num; }
        if (num == 0)
            return -70.0f;

        const double relGate = lufs (sum / num) - 10.0;
        double sum2 = 0.0; int num2 = 0;
        for (auto e : blocks)
            if (lufs (e) > -70.0 && lufs (e) > relGate) { sum2 += e; ++num2; }
        return (float) lufs (num2 > 0 ? sum2 / num2 : sum / num);
    }

    /** Energia (dB) di bassi (<150 Hz), medi (300 Hz-3 kHz) e alti (>6 kHz), e quota del "lato" stereo. */
    struct Balance { float lowDb = 0, midDb = 0, highDb = 0, sideRatio = 0, peakDb = 0, crestDb = 0; };

    inline Balance measureBalance (const float* L, const float* R, size_t n, double sr)
    {
        mixer::Biquad lp, bpLo, bpHi, hp;
        auto design = [sr] (mixer::Biquad& b, bool high, double f)
        {
            const double q = 0.707, w = juce::MathConstants<double>::twoPi * f / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            if (high) b.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
            else      b.set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        };
        design (lp, false, 150.0);
        design (bpLo, true, 300.0);
        design (bpHi, false, 3000.0);
        design (hp, true, 6000.0);

        double eLow = 0, eMid = 0, eHigh = 0, eMid2 = 0, eSide = 0, eAll = 0;
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            const float lo = lp.process (m, 0), mid = bpHi.process (bpLo.process (m, 0), 0), hi = hp.process (m, 0);
            eLow += (double) lo * lo; eMid += (double) mid * mid; eHigh += (double) hi * hi;
            eMid2 += (double) m * m; eSide += (double) s * s;
            eAll += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
            peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
        }

        Balance b;
        b.lowDb  = (float) (10.0 * std::log10 ((eLow + 1e-12) / (eMid + 1e-12)));
        b.highDb = (float) (10.0 * std::log10 ((eHigh + 1e-12) / (eMid + 1e-12)));
        b.midDb = 0.0f;
        b.sideRatio = (float) (eSide / (eMid2 + eSide + 1e-12));
        b.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        b.crestDb = b.peakDb - (float) (10.0 * std::log10 (eAll / (double) juce::jmax ((size_t) 1, n) + 1e-12));
        return b;
    }

    /**
        Mastering automatico: dato l'audio senza mastering, regola i controlli secondo lo stile
        (gia' impostato in params) e trova il guadagno che porta al volume voluto.
        Gira su un thread a parte; l'audio viene copiato (la catena lo modifica).
    */
    inline void analyse (const std::vector<float>& srcL, const std::vector<float>& srcR, double sr, Params& params,
                         std::function<bool()> shouldStop = {})
    {
        const size_t n = srcL.size();
        const auto& st = style (params.styleIndex.load());

        const float before = measureLufs (srcL.data(), srcR.data(), n, sr);
        const auto bal = measureBalance (srcL.data(), srcR.data(), n, sr);

        // EQ: avvicina l'equilibrio a quello dello stile (con prudenza: al massimo 3 dB)
        params.lowDb.store (juce::jlimit (-3.0f, 3.0f, (st.lowTarget - bal.lowDb) * 0.35f));
        params.airDb.store (juce::jlimit (-3.0f, 3.0f, (st.highTarget - bal.highDb) * 0.35f));
        params.presenceDb.store (0.0f);

        // Compressione: piu' dinamica c'e' (picchi alti rispetto alla media), piu' "colla"
        params.glue.store (juce::jlimit (0.15f, 0.85f, st.glue + (bal.crestDb - 14.0f) * 0.03f));
        // Stereo: allarga un mix stretto, stringe un mix troppo largo
        params.width.store (bal.sideRatio < 0.08f ? juce::jmin (1.3f, st.width + 0.15f)
                          : bal.sideRatio > 0.35f ? juce::jmax (0.8f, st.width - 0.15f) : st.width);

        // Guadagno: si prova la catena e si corregge, fino al volume voluto (pochi giri)
        std::vector<float> L, R;
        auto loudnessWith = [&] (float g)
        {
            L = srcL; R = srcR;
            Chain chain;
            chain.prepare (sr);
            Snapshot snap;
            snap.gainDb = g; snap.lowDb = params.lowDb.load(); snap.presenceDb = params.presenceDb.load();
            snap.airDb = params.airDb.load(); snap.glue = params.glue.load(); snap.warmth = params.warmth.load();
            snap.width = params.width.load(); snap.ceiling = params.ceiling.load(); snap.loudness = params.loudness.load();
            for (size_t pos = 0; pos < n; pos += 4096)
                chain.process (L.data() + pos, R.data() + pos, (int) juce::jmin ((size_t) 4096, n - pos), snap);
            return measureLufs (L.data(), R.data(), n, sr);
        };

        const float target = params.loudness.load();
        float gain = juce::jlimit (-12.0f, 18.0f, target - before);
        float result = loudnessWith (gain);
        for (int iteration = 0; iteration < 5 && std::abs (target - result) >= 0.15f; ++iteration)
        {
            if (shouldStop && shouldStop())
                return;
            const float error = target - result;
            const float nextGain = juce::jlimit (-12.0f, 18.0f, gain + error * (error > 0 ? 1.15f : 1.0f));   // il limiter "mangia" un po'
            const float next = loudnessWith (nextGain);

            // Oltre un certo punto alzare il guadagno schiaccia solo i picchi (e distorce) senza
            // aumentare il volume: ci si ferma al massimo "pulito" che il mix permette
            if (nextGain > gain + 0.5f && (next - result) / (nextGain - gain) < 0.2f)
                break;

            gain = nextGain;
            result = next;
        }

        params.gainDb.store (gain);
        params.measuredLufs = before;
        params.measuredPeakDb = bal.peakDb;
        params.resultLufs = result;
        params.analysedLoudness = params.loudness.load();
        params.analysed = true;
    }
}
