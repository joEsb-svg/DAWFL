#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
/**
    Il mixer di JLoop (come in FL Studio): una traccia Master e 32 tracce "insert".

    Ogni canale del channel rack manda il suono a una traccia. Ogni traccia ha:
    - volume, panorama, muto, solo e il suo misuratore
    - un equalizzatore a 3 bande (bassi, medi con la frequenza, alti)
    - fino a 6 effetti in fila (compressore, saturazione, filtro, delay, riverbero, chorus,
      bitcrusher, sidechain, ampiezza stereo)

    Il sidechain abbassa il volume della traccia a ogni colpo di un'altra traccia (es. la cassa):
    l'effetto "pompa" della musica elettronica.

    Gli effetti sono oggetti creati dal thread della UI (e mai nel callback audio): i loro parametri
    vivono nello slot della traccia (atomic), cosi' l'interfaccia li puo' cambiare in ogni momento.
*/
namespace mixer
{
    inline constexpr int numInserts = 32;
    inline constexpr int numTracks  = numInserts + 1;   // 0 = Master
    inline constexpr int numSlots   = 6;
    inline constexpr int maxParams  = 6;

    //==========================================================================
    // Tipi di effetto e i loro parametri

    enum FxType { none = 0, compressor, saturation, filter, delay, reverb, chorus, bitcrusher, sidechain, stereo, numFxTypes };

    enum class Unit { percent, dB, ms, hz, ratio, choice, track, bits, times };

    struct ParamDef
    {
        const char* name;
        float minValue, maxValue, defaultValue;
        Unit unit;
        float centre = 0.0f;                 // per le scale "a curva" (frequenze, tempi): il valore a meta' corsa
        const char* choices = nullptr;       // per Unit::choice: voci separate da '|'
    };

    struct FxInfo
    {
        const char* id;                      // nome salvato nei progetti (non cambiarlo)
        const char* name;
        const char* detail;
        int numParams;
        std::array<ParamDef, maxParams> params;
    };

    inline const FxInfo& info (int type)
    {
        static const std::array<FxInfo, numFxTypes> infos =
        {{
            { "none", "Vuoto", "", 0, {} },
            { "comp", "Compressore", "Controlla la dinamica: suoni piu' compatti e presenti", 5, {{
                { "Soglia", -40.0f, 0.0f, -18.0f, Unit::dB },
                { "Rapporto", 1.0f, 20.0f, 4.0f, Unit::ratio, 4.0f },
                { "Attacco", 0.1f, 100.0f, 5.0f, Unit::ms, 10.0f },
                { "Rilascio", 10.0f, 1000.0f, 120.0f, Unit::ms, 150.0f },
                { "Guadagno", 0.0f, 24.0f, 4.0f, Unit::dB } }} },
            { "drive", "Saturazione", "Calore e distorsione", 4, {{
                { "Drive", 0.0f, 1.0f, 0.4f, Unit::percent },
                { "Tono", 500.0f, 20000.0f, 12000.0f, Unit::hz, 4000.0f },
                { "Mix", 0.0f, 1.0f, 1.0f, Unit::percent },
                { "Uscita", -12.0f, 6.0f, 0.0f, Unit::dB } }} },
            { "filter", "Filtro", "Passa-basso, passa-alto o passa-banda, con LFO", 5, {{
                { "Tipo", 0.0f, 2.0f, 0.0f, Unit::choice, 0.0f, "Passa-basso|Passa-alto|Passa-banda" },
                { "Cutoff", 20.0f, 20000.0f, 2000.0f, Unit::hz, 1000.0f },
                { "Risonanza", 0.0f, 1.0f, 0.2f, Unit::percent },
                { "LFO", 0.0f, 1.0f, 0.0f, Unit::percent },
                { "Velocita' LFO", 0.05f, 10.0f, 0.5f, Unit::hz, 1.0f } }} },
            { "delay", "Delay", "Eco a tempo", 5, {{
                { "Tempo", 0.0f, 5.0f, 2.0f, Unit::choice, 0.0f, "1/16|1/8|3/16|1/4|3/8|1/2" },
                { "Feedback", 0.0f, 0.95f, 0.4f, Unit::percent },
                { "Mix", 0.0f, 1.0f, 0.3f, Unit::percent },
                { "Ping-pong", 0.0f, 1.0f, 1.0f, Unit::choice, 0.0f, "No|Si'" },
                { "Tono", 500.0f, 20000.0f, 6000.0f, Unit::hz, 4000.0f } }} },
            { "reverb", "Riverbero", "Spazio: stanza, sala, cattedrale", 4, {{
                { "Stanza", 0.0f, 1.0f, 0.6f, Unit::percent },
                { "Smorzamento", 0.0f, 1.0f, 0.5f, Unit::percent },
                { "Ampiezza", 0.0f, 1.0f, 1.0f, Unit::percent },
                { "Mix", 0.0f, 1.0f, 0.3f, Unit::percent } }} },
            { "chorus", "Chorus", "Suono piu' largo e mosso", 3, {{
                { "Velocita'", 0.05f, 5.0f, 0.8f, Unit::hz, 1.0f },
                { "Profondita'", 0.0f, 1.0f, 0.5f, Unit::percent },
                { "Mix", 0.0f, 1.0f, 0.5f, Unit::percent } }} },
            { "crush", "Bitcrusher", "Suono sporco, a 8 bit, lo-fi", 3, {{
                { "Bit", 2.0f, 16.0f, 8.0f, Unit::bits },
                { "Riduzione", 1.0f, 32.0f, 4.0f, Unit::times, 4.0f },
                { "Mix", 0.0f, 1.0f, 1.0f, Unit::percent } }} },
            { "sidechain", "Sidechain", "Abbassa il volume a ogni colpo di un'altra traccia (la cassa)", 4, {{
                { "Sorgente", 1.0f, (float) numInserts, 1.0f, Unit::track },
                { "Quantita'", 0.0f, 1.0f, 0.7f, Unit::percent },
                { "Rilascio", 20.0f, 800.0f, 250.0f, Unit::ms, 200.0f },
                { "Soglia", -48.0f, 0.0f, -30.0f, Unit::dB } }} },
            { "stereo", "Stereo", "Allarga o stringe l'immagine stereo", 1, {{
                { "Ampiezza", 0.0f, 2.0f, 1.0f, Unit::percent } }} },
        }};
        return infos[(size_t) juce::jlimit (0, numFxTypes - 1, type)];
    }

    inline int typeFromId (const juce::String& id)
    {
        for (int t = 0; t < numFxTypes; ++t)
            if (id == info (t).id)
                return t;
        return none;
    }

    /** Valore 0..1 di uno slider <-> valore reale del parametro (scala a curva se c'e' il centro). */
    inline float toNormalised (const ParamDef& p, float v)
    {
        juce::NormalisableRange<float> r (p.minValue, p.maxValue);
        if (p.centre > p.minValue && p.centre < p.maxValue) r.setSkewForCentre (p.centre);
        return r.convertTo0to1 (juce::jlimit (p.minValue, p.maxValue, v));
    }

    inline float fromNormalised (const ParamDef& p, float n)
    {
        juce::NormalisableRange<float> r (p.minValue, p.maxValue);
        if (p.centre > p.minValue && p.centre < p.maxValue) r.setSkewForCentre (p.centre);
        float v = r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
        if (p.unit == Unit::choice || p.unit == Unit::track || p.unit == Unit::bits)
            v = std::round (v);
        return v;
    }

    inline juce::String formatValue (const ParamDef& p, float v)
    {
        switch (p.unit)
        {
            case Unit::percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
            case Unit::dB:      return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
            case Unit::ms:      return v < 1000.0f ? juce::String (v, v < 10.0f ? 1 : 0) + " ms" : juce::String (v / 1000.0f, 2) + " s";
            case Unit::hz:      return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
            case Unit::ratio:   return juce::String (v, v < 10.0f ? 1 : 0) + ":1";
            case Unit::times:   return juce::String (juce::roundToInt (v)) + "x";
            case Unit::bits:    return juce::String (juce::roundToInt (v)) + " bit";
            case Unit::track:   return "Insert " + juce::String (juce::roundToInt (v));
            case Unit::choice:
            {
                const auto list = juce::StringArray::fromTokens (p.choices != nullptr ? p.choices : "", "|", "");
                return list[juce::jlimit (0, list.size() - 1, juce::roundToInt (v))];
            }
        }
        return juce::String (v, 2);
    }

    //==========================================================================
    /** Quello che un effetto sa del resto del mixer, blocco per blocco. */
    struct Context
    {
        double sampleRate = 48000.0, bpm = 120.0;
        const float* keyL = nullptr;   // sidechain: l'ingresso della traccia sorgente
        const float* keyR = nullptr;
    };

    /** Un effetto. prepare() e il costruttore girano sul thread della UI; process() nel callback audio. */
    class Effect
    {
    public:
        virtual ~Effect() = default;
        virtual void prepare (double sampleRate) = 0;
        virtual void reset() {}
        virtual void process (float* L, float* R, int n, const float* params, const Context& ctx) = 0;
    };

    std::unique_ptr<Effect> createEffect (int type);

    //==========================================================================
    /** Uno slot effetti di una traccia: il tipo, i parametri e l'oggetto che fa il lavoro. */
    struct Slot
    {
        std::atomic<int> type { none };
        std::atomic<bool> bypass { false };
        std::array<std::atomic<float>, maxParams> params {};
        Effect* effect = nullptr;                 // letto dal thread audio (sotto lo structureLock)
        std::unique_ptr<Effect> owned;            // posseduto qui, cambiato solo sotto lo structureLock
    };

    struct Track
    {
        std::atomic<float> volume { 1.0f };       // guadagno lineare (fader), 0..~1.6
        std::atomic<float> pan    { 0.0f };
        std::atomic<bool>  muted  { false };
        std::atomic<bool>  solo   { false };
        std::atomic<float> eqLow  { 0.0f }, eqMid { 0.0f }, eqMidFreq { 1000.0f }, eqHigh { 0.0f };   // dB, Hz
        std::atomic<float> peakL  { 0.0f }, peakR { 0.0f };   // misuratore (la UI li legge e li azzera)
        std::array<Slot, numSlots> slots;

        // Solo thread della UI
        juce::String name;                        // vuoto = nome automatico (il canale collegato)
        juce::uint32 colour = 0;                  // 0 = colore automatico
    };

    //==========================================================================
    /** Filtro biquad (RBJ), stereo. */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};

        void setLowShelf (double sr, double f, double gainDb)   { shelf (sr, f, gainDb, true); }
        void setHighShelf (double sr, double f, double gainDb)  { shelf (sr, f, gainDb, false); }

        void setPeak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
            const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
            set (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
        }

        void shelf (double sr, double f, double gainDb, bool low)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
            const double c = std::cos (w), s = std::sin (w), alpha = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * alpha;
            if (low)
                set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                     (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
            else
                set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        void reset()    { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

        inline float process (float x, int ch) noexcept
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };

    /** L'equalizzatore a 3 bande di una traccia (stato del thread audio). */
    struct TrackEq
    {
        Biquad low, mid, high;
        float shown[4] { 99.0f, 99.0f, 99.0f, 99.0f };
        bool active = false;

        void update (double sr, float lo, float md, float freq, float hi)
        {
            if (lo == shown[0] && md == shown[1] && freq == shown[2] && hi == shown[3])
                return;
            shown[0] = lo; shown[1] = md; shown[2] = freq; shown[3] = hi;
            low.setLowShelf (sr, 120.0, lo);
            mid.setPeak (sr, juce::jlimit (60.0, sr * 0.4, (double) freq), 0.9, md);
            high.setHighShelf (sr, 8000.0, hi);
            active = std::abs (lo) > 0.05f || std::abs (md) > 0.05f || std::abs (hi) > 0.05f;
        }

        void process (float* L, float* R, int n) noexcept
        {
            if (! active)
                return;
            for (int i = 0; i < n; ++i)
            {
                L[i] = high.process (mid.process (low.process (L[i], 0), 0), 0);
                R[i] = high.process (mid.process (low.process (R[i], 1), 1), 1);
            }
        }

        void reset()    { low.reset(); mid.reset(); high.reset(); for (auto& s : shown) s = 99.0f; }
    };

    //==========================================================================
    // Gli effetti

    class Compressor : public Effect
    {
    public:
        void prepare (double sampleRate) override   { sr = sampleRate; env = 0.0f; }
        void reset() override                         { env = 0.0f; }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const float threshold = p[0], ratio = juce::jmax (1.0f, p[1]);
            const float att = (float) std::exp (-1.0 / (juce::jmax (0.0001, (double) p[2] / 1000.0) * sr));
            const float rel = (float) std::exp (-1.0 / (juce::jmax (0.001, (double) p[3] / 1000.0) * sr));
            const float makeup = juce::Decibels::decibelsToGain (p[4]);

            for (int i = 0; i < n; ++i)
            {
                const float in = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                env = in > env ? att * env + (1.0f - att) * in : rel * env + (1.0f - rel) * in;
                const float levelDb = juce::Decibels::gainToDecibels (env, -100.0f);
                const float over = levelDb - threshold;
                const float gain = over > 0.0f ? juce::Decibels::decibelsToGain (-over * (1.0f - 1.0f / ratio)) : 1.0f;
                L[i] *= gain * makeup;
                R[i] *= gain * makeup;
            }
        }

    private:
        double sr = 48000.0;
        float env = 0.0f;
    };

    class Saturation : public Effect
    {
    public:
        void prepare (double sampleRate) override   { sr = sampleRate; lp[0] = lp[1] = 0.0f; }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const float drive = 1.0f + p[0] * 14.0f;
            // Volume compensato: un segnale a livello tipico (0.3) esce allo stesso livello con qualunque drive
            // (prima il drive alzava il volume fino a +20 dB e il limiter del master schiacciava tutto)
            const float norm = 0.3f / std::tanh (0.3f * drive);
            const float coef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * p[1] / sr);
            const float mix = p[2], out = juce::Decibels::decibelsToGain (p[3]);
            float* ch[2] { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float dry = ch[c][i];
                    float wet = std::tanh (dry * drive) * norm;
                    lp[c] += coef * (wet - lp[c]);
                    wet = lp[c];
                    ch[c][i] = (dry + (wet - dry) * mix) * out;
                }
        }

    private:
        double sr = 48000.0;
        float lp[2] {};
    };

    class Filter : public Effect
    {
    public:
        void prepare (double sampleRate) override   { sr = sampleRate; reset(); }
        void reset() override                         { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; phase = 0.0; }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const int type = juce::roundToInt (p[0]);
            const float k = 2.0f - 1.9f * p[2];
            const double lfoInc = (double) p[4] / sr;
            float* ch[2] { L, R };

            for (int start = 0; start < n; start += 16)   // coefficienti ogni 16 campioni
            {
                const int len = juce::jmin (16, n - start);
                double cutoff = p[1];
                if (p[3] > 0.001f)
                {
                    cutoff *= std::exp2 (std::sin (phase * juce::MathConstants<double>::twoPi) * 3.0 * p[3]);
                    phase += lfoInc * len;
                    if (phase >= 1.0) phase -= 1.0;
                }
                const float g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, cutoff) / sr);
                const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

                for (int c = 0; c < 2; ++c)
                    for (int i = start; i < start + len; ++i)
                    {
                        const float x = ch[c][i];
                        const float v3 = x - s2[c];
                        const float v1 = a1 * s1[c] + a2 * v3;
                        const float v2 = s2[c] + a2 * s1[c] + a3 * v3;
                        s1[c] = 2.0f * v1 - s1[c];
                        s2[c] = 2.0f * v2 - s2[c];
                        ch[c][i] = type == 1 ? x - k * v1 - v2 : type == 2 ? v1 * k : v2;
                    }
            }
        }

    private:
        double sr = 48000.0, phase = 0.0;
        float s1[2] {}, s2[2] {};
    };

    class Delay : public Effect
    {
    public:
        void prepare (double sampleRate) override
        {
            sr = sampleRate;
            buffer[0].assign ((size_t) (sr * 2.6) + 1, 0.0f);
            buffer[1].assign ((size_t) (sr * 2.6) + 1, 0.0f);
            reset();
        }
        void reset() override
        {
            for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
            pos = 0; lp[0] = lp[1] = 0.0f;
        }

        void process (float* L, float* R, int n, const float* p, const Context& ctx) override
        {
            static constexpr double sixteenths[] = { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };
            const double steps = sixteenths[juce::jlimit (0, 5, juce::roundToInt (p[0]))];
            const int size = (int) buffer[0].size();
            const int d = juce::jlimit (1, size - 1, (int) (steps * 15.0 / juce::jmax (40.0, ctx.bpm) * sr));
            const float fb = p[1], mix = p[2];
            const bool ping = p[3] > 0.5f;
            const float coef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * p[4] / sr);

            for (int i = 0; i < n; ++i)
            {
                const int rd = (pos - d + size) % size;
                const float dl = buffer[0][(size_t) rd], dr = buffer[1][(size_t) rd];
                lp[0] += coef * (dl - lp[0]);
                lp[1] += coef * (dr - lp[1]);
                if (ping)
                {
                    buffer[0][(size_t) pos] = (L[i] + R[i]) * 0.5f + lp[1] * fb;
                    buffer[1][(size_t) pos] = lp[0] * fb;
                }
                else
                {
                    buffer[0][(size_t) pos] = L[i] + lp[0] * fb;
                    buffer[1][(size_t) pos] = R[i] + lp[1] * fb;
                }
                pos = (pos + 1) % size;
                L[i] += dl * mix;
                R[i] += dr * mix;
            }
        }

    private:
        double sr = 48000.0;
        std::vector<float> buffer[2];
        int pos = 0;
        float lp[2] {};
    };

    class Reverb : public Effect
    {
    public:
        void prepare (double sampleRate) override   { reverb.setSampleRate (sampleRate); reverb.reset(); }
        void reset() override                         { reverb.reset(); }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = p[0]; rp.damping = p[1]; rp.width = p[2];
            rp.wetLevel = p[3] * 0.7f; rp.dryLevel = 1.0f - p[3] * 0.5f;
            if (rp.roomSize != last.roomSize || rp.damping != last.damping || rp.width != last.width || rp.wetLevel != last.wetLevel)
            {
                reverb.setParameters (rp);
                last = rp;
            }
            reverb.processStereo (L, R, n);
        }

    private:
        juce::Reverb reverb;
        juce::Reverb::Parameters last;
    };

    class Chorus : public Effect
    {
    public:
        void prepare (double sampleRate) override
        {
            sr = sampleRate;
            buffer[0].assign (4096, 0.0f);
            buffer[1].assign (4096, 0.0f);
            pos = 0; phase = 0.0;
        }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const float base = (float) (0.012 * sr), depth = (float) (0.006 * sr) * p[1], mix = p[2];
            const int size = (int) buffer[0].size();
            for (int i = 0; i < n; ++i)
            {
                buffer[0][(size_t) pos] = L[i];
                buffer[1][(size_t) pos] = R[i];
                phase += p[0] / sr;
                if (phase >= 1.0) phase -= 1.0;
                const float mL = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                const float mR = (float) std::sin (juce::MathConstants<double>::twoPi * (phase + 0.25));
                const float wl = read (buffer[0], base + depth * mL, size);
                const float wr = read (buffer[1], base + depth * mR, size);
                L[i] = L[i] * (1.0f - 0.4f * mix) + wr * 0.8f * mix;
                R[i] = R[i] * (1.0f - 0.4f * mix) + wl * 0.8f * mix;
                pos = (pos + 1) % size;
            }
        }

    private:
        float read (const std::vector<float>& b, float delay, int size) const
        {
            float idx = (float) pos - delay;
            while (idx < 0) idx += (float) size;
            const int i0 = (int) idx;
            const float fr = idx - (float) i0;
            return b[(size_t) i0] + fr * (b[(size_t) ((i0 + 1) % size)] - b[(size_t) i0]);
        }

        double sr = 48000.0, phase = 0.0;
        std::vector<float> buffer[2];
        int pos = 0;
    };

    class Bitcrusher : public Effect
    {
    public:
        void prepare (double) override  { counter = 0; held[0] = held[1] = 0.0f; }

        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const float q = std::exp2 (juce::jlimit (2.0f, 16.0f, p[0]) - 1.0f);
            const int hold = juce::jmax (1, juce::roundToInt (p[1]));
            const float mix = p[2];
            for (int i = 0; i < n; ++i)
            {
                if (counter <= 0)
                {
                    held[0] = std::round (L[i] * q) / q;
                    held[1] = std::round (R[i] * q) / q;
                    counter = hold;
                }
                --counter;
                L[i] += (held[0] - L[i]) * mix;
                R[i] += (held[1] - R[i]) * mix;
            }
        }

    private:
        int counter = 0;
        float held[2] {};
    };

    /**
        Sidechain "a colpi": riconosce ogni attacco della traccia sorgente (la cassa) e abbassa il volume,
        che poi risale nel tempo di "Rilascio". Cosi' pompa a tempo anche con casse lunghe (808).
    */
    class Sidechain : public Effect
    {
    public:
        void prepare (double sampleRate) override   { sr = sampleRate; reset(); }
        void reset() override                         { fast = slow = 0.0f; phase = 1.0f; refractory = 0; }

        void process (float* L, float* R, int n, const float* p, const Context& ctx) override
        {
            if (ctx.keyL == nullptr)
                return;
            const float fastAtt = (float) std::exp (-1.0 / (0.0005 * sr)), fastRel = (float) std::exp (-1.0 / (0.010 * sr));
            const float slowCoef = (float) std::exp (-1.0 / (0.060 * sr));
            const float threshold = juce::Decibels::decibelsToGain (p[3]);
            const float amount = p[1];
            const float step = (float) (1.0 / (juce::jmax (0.02, (double) p[2] / 1000.0) * sr));
            const int minGap = (int) (0.06 * sr);

            for (int i = 0; i < n; ++i)
            {
                const float key = juce::jmax (std::abs (ctx.keyL[i]), std::abs (ctx.keyR[i]));
                fast = key > fast ? fastAtt * fast + (1.0f - fastAtt) * key : fastRel * fast;
                slow = slowCoef * slow + (1.0f - slowCoef) * fast;

                // Un attacco: il livello sale di colpo sopra la soglia
                if (refractory > 0)
                    --refractory;
                else if (fast > threshold && fast > slow * 1.6f)
                {
                    phase = 0.0f;
                    refractory = minGap;
                }

                float duck = 0.0f;
                if (phase < 1.0f)
                {
                    const float x = phase;
                    duck = amount * (1.0f - x) * (1.0f - x);
                    phase += step;
                }
                // Discesa morbida (3 ms) per non fare click
                current += (duck - current) * 0.02f;
                L[i] *= 1.0f - current;
                R[i] *= 1.0f - current;
            }
        }

    private:
        double sr = 48000.0;
        float fast = 0.0f, slow = 0.0f, phase = 1.0f, current = 0.0f;
        int refractory = 0;
    };

    class Stereo : public Effect
    {
    public:
        void prepare (double) override {}
        void process (float* L, float* R, int n, const float* p, const Context&) override
        {
            const float w = p[0];
            for (int i = 0; i < n; ++i)
            {
                const float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f * w;
                L[i] = m + s;
                R[i] = m - s;
            }
        }
    };

    inline std::unique_ptr<Effect> createEffect (int type)
    {
        switch (type)
        {
            case compressor: return std::make_unique<Compressor>();
            case saturation: return std::make_unique<Saturation>();
            case filter:     return std::make_unique<Filter>();
            case delay:      return std::make_unique<Delay>();
            case reverb:     return std::make_unique<Reverb>();
            case chorus:     return std::make_unique<Chorus>();
            case bitcrusher: return std::make_unique<Bitcrusher>();
            case sidechain:  return std::make_unique<Sidechain>();
            case stereo:     return std::make_unique<Stereo>();
            default:         return nullptr;
        }
    }
}
