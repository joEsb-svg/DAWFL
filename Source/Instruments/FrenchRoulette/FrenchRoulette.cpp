#include "../../Engine/Performance.h"
#include "FrenchRoulette.h"
#include "FrenchRouletteEditor.h"

namespace fr
{
    static const juce::StringArray o1Names     { "Saw", "Square", "Pulse" };
    static const juce::StringArray octNames    { "-2", "-1", "0", "+1", "+2" };
    static const juce::StringArray subNames    { "-1", "-2" };
    static const juce::StringArray fTypeNames  { "LP 24", "LP 12", "BP", "HP" };
    static const juce::StringArray modeNames   { "Mono", "Legato", "Poly" };
    static const juce::StringArray lfoNames    { "Sinusoide", "Triangolo", "Dente", "Quadra", "Casuale" };
    static const juce::StringArray divNames    { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/8 ." };
    static const juce::StringArray pumpNames   { "1/2", "1/4", "1/8" };
    static const juce::StringArray clipNames   { "Soft", "Hard" };
    static const juce::StringArray rateNames   { "1/8", "1/16", "1/32" };
    static const juce::StringArray retrigNames { "Off", "Filtro", "Filtro+Amp" };
    static const juce::StringArray delayNames  { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
    static constexpr double divBeats[]   { 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 0.75 };
    static constexpr double pumpBeats[]  { 2.0, 1.0, 0.5 };
    static constexpr double stepBeats[]  { 0.5, 0.25, 0.125 };
    static constexpr double delayBeats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };

    static inline float mtof (float m) noexcept        { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
    static inline float fastTanh (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    static inline uint32_t nextRand (uint32_t& r) noexcept { r = r * 1664525u + 1013904223u; return r; }
    static inline float randBi (uint32_t& r) noexcept     { return (float) (nextRand (r) >> 8) / 8388608.0f - 1.0f; }

    //==========================================================================
    // Wavetable di OSC 2: 8 tipi x 8 fotogrammi (morph), con versioni a banda limitata per ottava

    static constexpr int tableLen = 1024, numFrames = 8, numLevels = 7;

    static float frameShape (int type, float m, float t)
    {
        using juce::MathConstants;
        const float tp = MathConstants<float>::twoPi;
        switch (type)
        {
            case 0: { const float i = 0.3f + 7.0f * m;   // FM METAL: portante 1, modulatori 3 e 7
                      return std::sin (tp * t + i * std::sin (tp * 3.0f * t) + 0.4f * i * m * std::sin (tp * 7.0f * t)); }
            case 1: { const float i = 0.4f + 5.0f * m;   // FM GROWL: modulatore con feedback (ringhio)
                      return std::sin (tp * t + i * std::sin (tp * t + 0.9f * std::sin (tp * t))); }
            case 2: { const float k = 1.0f + 14.0f * m;  // DIGI SAW: risonanza a distorsione di fase (stile CZ)
                      return (1.0f - t) * std::sin (tp * t * k) * 1.4f + (2.0f * t - 1.0f) * (1.0f - m) * 0.6f; }
            case 3: { const float f1 = 2.0f + 5.0f * m, f2 = 6.0f + 9.0f * m;   // FORMANT: due formanti che si muovono ("yoi")
                      const float w = 0.5f - 0.5f * std::cos (tp * t);
                      return w * (std::sin (tp * t * std::round (f1)) + 0.6f * std::sin (tp * t * std::round (f2))) * 1.2f; }
            case 4: { const float pw = 0.5f - 0.45f * m; // HOLLOW: quadra che si stringe
                      return t < pw ? 1.0f : -1.0f; }
            case 5: { const float levels = 16.0f - 13.0f * m;   // BITWAVE: seno a gradini
                      return std::round (std::sin (tp * t) * levels) / levels; }
            case 6: { const float r = 1.0f + 6.0f * m;   // HARD SYNC: dente di sega sincronizzato
                      const float s = t * r; return 2.0f * (s - std::floor (s)) - 1.0f; }
            default: { const float gain = 1.0f + 5.0f * m;   // FOLD: seno ripiegato
                       float x = std::sin (tp * t) * gain;
                       for (int k = 0; k < 6 && std::abs (x) > 1.0f; ++k) x = x > 1.0f ? 2.0f - x : -2.0f - x;
                       return x; }
        }
    }

    struct Tables
    {
        // [tipo][fotogramma][livello][campione]
        std::vector<float> data;

        Tables() : data ((size_t) (numTables * numFrames * numLevels * (tableLen + 1)), 0.0f)
        {
            juce::dsp::FFT fft (10);   // 1024
            std::vector<float> spec ((size_t) tableLen * 2), work ((size_t) tableLen * 2);
            for (int type = 0; type < numTables; ++type)
                for (int f = 0; f < numFrames; ++f)
                {
                    std::fill (spec.begin(), spec.end(), 0.0f);
                    for (int i = 0; i < tableLen; ++i)
                        spec[(size_t) i] = frameShape (type, (float) f / (numFrames - 1), (float) i / tableLen);
                    fft.performRealOnlyForwardTransform (spec.data(), true);
                    spec[0] = spec[1] = 0.0f;   // niente DC

                    float scale = 1.0f;
                    for (int level = 0; level < numLevels; ++level)
                    {
                        const int maxHarm = 400 >> level;
                        work = spec;
                        for (int h = maxHarm + 1; h <= tableLen / 2; ++h)
                            work[(size_t) h * 2] = work[(size_t) h * 2 + 1] = 0.0f;
                        fft.performRealOnlyInverseTransform (work.data());
                        if (level == 0)
                        {
                            float peak = 0.0f;
                            for (int i = 0; i < tableLen; ++i) peak = juce::jmax (peak, std::abs (work[(size_t) i]));
                            scale = peak > 1.0e-6f ? 0.9f / peak : 0.0f;
                        }
                        float* dst = at (type, f, level);
                        for (int i = 0; i < tableLen; ++i) dst[i] = work[(size_t) i] * scale;
                        dst[tableLen] = dst[0];
                    }
                }
        }

        float* at (int type, int frame, int level) noexcept
        {
            return data.data() + (size_t) (((type * numFrames + frame) * numLevels + level) * (tableLen + 1));
        }

        static Tables& get()    { static Tables t; return t; }
    };

    static inline float readTable (const float* t, double phase) noexcept
    {
        const float idx = (float) phase * (float) tableLen;
        const int i = (int) idx;
        const float f = idx - (float) i;
        return t[i] + f * (t[i + 1] - t[i]);
    }

    static inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)         { const float x = t / dt; return x + x - x * x - 1.0f; }
        if (t > 1.0f - dt)  { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
        return 0.0f;
    }

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        auto pctText  = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
        auto bipText  = [] (float v, int) { return (v > 0.005f ? "+" : "") + String (roundToInt (v * 100.0f)) + "%"; };
        auto secText  = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; };
        auto hzText   = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
        auto semiText = [] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)); };

        auto flt = [&p] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> text)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                                AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
        };
        auto skewed = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };
        auto choice = [&p] (const String& id, const String& name, const StringArray& opts, int def)
        {
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def));
        };
        auto boolean = [&p] (const String& id, const String& name, bool def)
        {
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
        };

        // OSC 1, OSC 2, SUB
        choice ("o1Wave", "OSC1 Forma", o1Names, 0);
        choice ("o1Oct", "OSC1 Ottava", octNames, 2);
        flt ("o1Pw", "OSC1 Larghezza", { 0.05f, 0.5f }, 0.3f, pctText);
        flt ("o1Pwm", "OSC1 PWM", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("o1Drift", "OSC1 Drift", { 0.0f, 1.0f }, 0.25f, pctText);
        flt ("o1Level", "OSC1 Livello", { 0.0f, 1.0f }, 0.8f, pctText);
        choice ("o2Table", "OSC2 Tabella", tableNames(), 0);
        flt ("o2Wave", "OSC2 Wave", { 0.0f, 1.0f }, 0.3f, pctText);
        flt ("o2Fm", "OSC2 FM", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("o2Semi", "OSC2 Semitoni", { -24.0f, 24.0f, 1.0f }, 0.0f, semiText);
        flt ("o2Fine", "OSC2 Fine", { -50.0f, 50.0f }, 7.0f, [] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " ct"; });
        flt ("o2Level", "OSC2 Livello", { 0.0f, 1.0f }, 0.0f, pctText);
        choice ("subOct", "Sub Ottava", subNames, 0);
        flt ("subLevel", "Sub Livello", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("subDrive", "Sub Drive", { 0.0f, 1.0f }, 0.0f, pctText);

        // Filtro e inviluppi
        choice ("fType", "Filtro Tipo", fTypeNames, 0);
        flt ("cutoff", "Cutoff", skewed (30.0f, 18000.0f, 1200.0f), 2500.0f, hzText);
        flt ("reso", "Risonanza", { 0.0f, 1.0f }, 0.25f, pctText);
        flt ("fEnv", "Inviluppo filtro", { -1.0f, 1.0f }, 0.35f, bipText);
        flt ("fKey", "Key track", { 0.0f, 1.0f }, 0.3f, pctText);
        flt ("fA", "Filtro Attacco", skewed (0.0005f, 4.0f, 0.2f), 0.001f, secText);
        flt ("fD", "Filtro Decay", skewed (0.005f, 4.0f, 0.4f), 0.25f, secText);
        flt ("fS", "Filtro Sustain", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("fR", "Filtro Rilascio", skewed (0.005f, 4.0f, 0.4f), 0.15f, secText);
        flt ("aA", "Attacco", skewed (0.0005f, 4.0f, 0.2f), 0.002f, secText);
        flt ("aD", "Decay", skewed (0.005f, 4.0f, 0.5f), 0.5f, secText);
        flt ("aS", "Sustain", { 0.0f, 1.0f }, 1.0f, pctText);
        flt ("aR", "Rilascio", skewed (0.005f, 4.0f, 0.4f), 0.08f, secText);
        choice ("mode", "Modo", modeNames, 0);
        flt ("glide", "Glide", skewed (0.0f, 1.0f, 0.15f), 0.0f, secText);

        // LFO
        choice ("lfoShape", "LFO Forma", lfoNames, 0);
        choice ("lfoDiv", "LFO Tempo", divNames, 3);
        flt ("lfoCut", "LFO > Cutoff", { -1.0f, 1.0f }, 0.0f, bipText);
        flt ("lfoWave", "LFO > Wave", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("lfoPitch", "LFO > Pitch", { 0.0f, 1.0f }, 0.0f, pctText);

        // Pump e crunch
        flt ("pump", "Pump", { 0.0f, 1.0f }, 0.0f, pctText);
        choice ("pumpDiv", "Pump Tempo", pumpNames, 1);
        flt ("pumpShape", "Pump Forma", { 0.0f, 1.0f }, 0.5f, pctText);
        flt ("pumpFilter", "Pump > Filtro", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("crunch", "Crunch", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("tube", "Valvola", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("bits", "Bit", { 2.0f, 16.0f }, 8.0f, [] (float v, int) { return String (v, 1) + " bit"; });
        flt ("rate", "Riduzione campioni", { 0.0f, 1.0f }, 0.2f, pctText);
        choice ("clipMode", "Clip", clipNames, 0);
        flt ("clipDrive", "Clip Drive", { 0.0f, 1.0f }, 0.0f, pctText);

        // Step matrix
        boolean ("matOn", "Matrix attiva", false);
        flt ("matLen", "Matrix Step", { 2.0f, 16.0f, 1.0f }, 16.0f, [] (float v, int) { return String (roundToInt (v)); });
        choice ("matRate", "Matrix Tempo", rateNames, 1);
        choice ("retrig", "Retrig", retrigNames, 1);
        flt ("swing", "Swing", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("gateLen", "Lunghezza gate", { 0.1f, 1.0f }, 0.65f, pctText);
        flt ("matGlide", "Glide step", skewed (0.005f, 0.5f, 0.08f), 0.06f, secText);
        flt ("matCut", "Profondita' cutoff", { 0.0f, 1.0f }, 0.5f, pctText);
        flt ("matCrush", "Profondita' crush", { 0.0f, 1.0f }, 0.6f, pctText);
        for (int s = 0; s < numSteps; ++s)
        {
            const auto n = "Step " + String (s + 1) + " ";
            boolean (stepId ('g', s), n + "Gate", true);
            flt (stepId ('c', s), n + "Cutoff", { 0.0f, 1.0f }, 1.0f, pctText);
            flt (stepId ('x', s), n + "Crush", { 0.0f, 1.0f }, 0.0f, pctText);
            flt (stepId ('p', s), n + "Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, semiText);
            boolean (stepId ('l', s), n + "Glide", false);
        }

        // Effetti
        flt ("chorus", "Chorus", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("dlyMix", "Delay", { 0.0f, 1.0f }, 0.0f, pctText);
        choice ("dlyTime", "Tempo delay", delayNames, 2);
        flt ("dlyFb", "Feedback delay", { 0.0f, 0.9f }, 0.35f, pctText);
        flt ("revMix", "Riverbero", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("revSize", "Stanza", { 0.0f, 1.0f }, 0.5f, pctText);
        flt ("master", "Volume", { -24.0f, 6.0f }, 0.0f, [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });

        return { p.begin(), p.end() };
    }

    //==========================================================================
    float Ladder::process (float x, int type) noexcept
    {
        const float beta = 1.0f / (1.0f + g);
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float S = beta * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
        float u = (x * (1.0f + 0.6f * k) - k * S) / (1.0f + k * G4);
        u = fastTanh (u);   // saturazione nel ritorno: la risonanza "urla" senza esplodere

        float in = u, y[4];
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[(size_t) i]) * G;
            y[i] = v + s[(size_t) i];
            s[(size_t) i] = y[i] + v;
            in = y[i];
        }
        switch (type)
        {
            case 1:  return y[1];
            case 2:  return 2.0f * (y[1] - y[2]) * (1.0f + 0.3f * k);
            case 3:  return u - 4.0f * y[0] + 6.0f * y[1] - 4.0f * y[2] + y[3];
            default: return y[3];
        }
    }

    void Env::set (double sr, float a, float d, float s, float r) noexcept
    {
        if (a == lastA && d == lastD && s == lastS && r == lastR && sr == lastSr)
            return;
        lastA = a; lastD = d; lastS = s; lastR = r; lastSr = sr;
        attackInc = (float) (1.0 / (juce::jmax (0.0003, (double) a) * sr));
        decayCoef = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) d) * sr));
        releaseCoef = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) r) * sr));
        sustainLevel = s;
    }

    float Env::next() noexcept
    {
        switch (stage)
        {
            case attack:
                value += attackInc;
                if (value >= 1.0f) { value = 1.0f; stage = decay; }
                break;
            case decay:
                value = sustainLevel + (value - sustainLevel) * decayCoef;
                if (std::abs (value - sustainLevel) < 1.0e-4f) { value = sustainLevel; stage = sustain; }
                break;
            case sustain:
                value = sustainLevel;
                if (value <= 0.0f) stage = idle;
                break;
            case release:
                value *= releaseCoef;
                if (value < 1.0e-4f) { value = 0.0f; stage = idle; }
                break;
            case idle: default:
                break;
        }
        return value;
    }

    //==========================================================================
    void Voice::reset()
    {
        amp.reset(); fenv.reset(); filter.reset();
        ph1 = ph2 = phSub = 0.0;
    }

    void Voice::start (int midiNote, float vel, float glideFrom, bool retrigger)
    {
        const bool wasActive = isActive();
        note = midiNote;
        velocity = vel;
        targetPitch = (float) midiNote;
        pitch = glideFrom >= 0.0f ? glideFrom : targetPitch;
        if (! wasActive)
        {
            ph1 = (double) (nextRand (rnd) >> 8) / 16777216.0;   // analogico: fase libera
            ph2 = phSub = 0.0;
            filter.reset();
            drift = 0.0f;
        }
        noteDetune = randBi (rnd);
        if (retrigger || ! wasActive)
        {
            amp.noteOn();
            fenv.noteOn();
        }
    }

    void Voice::render (float* main, float* sub, int n, const BlockParams& p, const Mods& m, float glideCoef)
    {
        if (! isActive())
            return;

        amp.set (sr, p.aA, p.aD, p.aS, p.aR);
        fenv.set (sr, p.fA, p.fD, p.fS, p.fR);
        if (m.retrigAmp)    amp.noteOn();
        if (m.retrigFilter) fenv.noteOn();

        pitch = glideCoef > 0.0f ? targetPitch + (pitch - targetPitch) * glideCoef : targetPitch;

        // Deriva analogica: cammino casuale lento + piccola stonatura diversa a ogni nota
        if ((nextRand (rnd) & 1023u) < 12u) driftTarget = randBi (rnd);
        drift += (driftTarget - drift) * 0.02f;
        const float driftSemi = p.o1Drift * (drift * 0.12f + noteDetune * 0.06f);

        const float base = pitch + m.pitch;
        const float f1 = mtof (base + (float) (p.o1Oct * 12) + driftSemi);
        const float f2 = mtof (base + (float) p.o2Semi + p.o2Fine * 0.01f);
        const float fs = mtof (base - (p.subOct == 0 ? 12.0f : 24.0f));
        const float dt1 = juce::jmin (0.45f, f1 / (float) sr), dt2 = f2 / (float) sr, dts = fs / (float) sr;

        // Fotogrammi e livello di banda della wavetable
        const float wave = juce::jlimit (0.0f, 1.0f, p.o2Wave + m.wave) * (numFrames - 1);
        const int fr0 = juce::jmin ((int) wave, numFrames - 2);
        const float frac = wave - (float) fr0;
        const float maxHarm = (float) sr * 0.45f / juce::jmax (1.0f, f2);
        int level = 0;
        while (level < numLevels - 1 && (float) (400 >> level) > maxHarm) ++level;
        auto& tables = Tables::get();
        const float* tA = tables.at (p.o2Table, fr0, level);
        const float* tB = tables.at (p.o2Table, fr0 + 1, level);

        // PWM: LFO lento proprio della voce
        pwmPhase += (float) n * 0.6f / (float) sr;
        if (pwmPhase >= 1.0f) pwmPhase -= 1.0f;
        const float pw = p.o1Wave == 1 ? 0.5f : juce::jlimit (0.04f, 0.5f, p.o1Pw - p.o1Pwm * 0.22f * (0.5f + 0.5f * perf::fastSin (pwmPhase)));

        const float l1 = p.o1Level * 0.5f, l2 = p.o2Level * 0.5f;
        const float subLvl = p.subLevel * 0.6f;
        const float subGain = 1.0f + p.subDrive * 6.0f, subNorm = 1.0f / fastTanh (subGain);
        const float vel = 0.55f + 0.45f * velocity;
        const float pm = p.o2Fm * 0.6f;
        const float k = p.reso * 4.1f;
        const float keyOct = p.fKey * (pitch - 60.0f) / 12.0f;

        for (int i = 0; i < n; ++i)
        {
            // Filtro ricalcolato ogni 4 campioni (inviluppi veloci, tan() costa)
            const float fe = fenv.next();
            if ((i & 3) == 0)
            {
                const float hz = juce::jlimit (25.0f, (float) sr * 0.42f, p.cutoff * std::exp2 (fe * p.fEnv * 7.0f + keyOct + m.cutOct));
                filter.set (std::tan (juce::MathConstants<float>::pi * hz / (float) sr), k);
            }

            // OSC 1 analogico (polyBLEP)
            const float t1 = (float) ph1;
            float o1;
            if (p.o1Wave == 0)
                o1 = 2.0f * t1 - 1.0f - polyBlep (t1, dt1);
            else
            {
                float t2 = t1 + 1.0f - pw; t2 -= std::floor (t2);
                o1 = (t1 < pw ? 1.0f : -1.0f) + polyBlep (t1, dt1) - polyBlep (t2, dt1) - (2.0f * pw - 1.0f);
            }
            ph1 += dt1; if (ph1 >= 1.0) ph1 -= 1.0;

            // OSC 2 digitale (wavetable, modulata in fase da OSC 1)
            float o2 = 0.0f;
            if (l2 > 0.0f)
            {
                double rp = ph2 + (double) (pm * o1);
                rp -= std::floor (rp);
                const float a = readTable (tA, rp), b = readTable (tB, rp);
                o2 = a + frac * (b - a);
                ph2 += dt2; if (ph2 >= 1.0) ph2 -= 1.0;
            }

            const float e = amp.next() * vel;
            main[i] += filter.process (o1 * l1 + o2 * l2, p.fType) * e;

            if (subLvl > 0.0f)
            {
                const float s = perf::fastSin ((float) phSub);
                sub[i] += fastTanh (s * subGain) * subNorm * subLvl * e;
                phSub += dts; if (phSub >= 1.0) phSub -= 1.0;
            }
        }
    }

    //==========================================================================
    void Synth::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        Tables::get();
        for (auto& v : voices) v.prepare (sr);
        juce::ignoreUnused (maxBlock);
        mainBuf.assign ((size_t) maxChunk, 0.0f);
        subBuf.assign (mainBuf.size(), 0.0f);
        fx.prepare (sr);
        killAll();
    }

    void Synth::killAll()
    {
        heldNotes.clear();
        for (auto& v : voices) v.reset();
        fx.reset();
        lastStep = -1;
        currentStep.store (-1);
        gateGain = 1.0f;
        holdSample = holdCount = dcX = dcY = 0.0f;
    }

    Voice* Synth::findVoice()
    {
        const int limit = perf::lowPower() ? 4 : maxVoices;
        Voice* best = nullptr;
        for (int i = 0; i < limit; ++i)
            if (! voices[(size_t) i].isActive()) return &voices[(size_t) i];
        for (int i = 0; i < limit; ++i)
            if (voices[(size_t) i].isReleasing() && (best == nullptr || voices[(size_t) i].age < best->age)) best = &voices[(size_t) i];
        if (best != nullptr) return best;
        for (int i = 0; i < limit; ++i)
            if (best == nullptr || voices[(size_t) i].age < best->age) best = &voices[(size_t) i];
        return best;
    }

    void Synth::noteOn (int note, float velocity)
    {
        bool anyActive = false;
        for (auto& v : voices) anyActive = anyActive || (v.isActive() && ! v.isReleasing());
        if (! anyActive)
        {
            freeBeats = 0.0;     // a progetto fermo la matrice riparte dalla nota
            lastStep = -1;
        }

        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        const float from = currentGlide > 0.0001f && lastPitch >= 0.0f ? lastPitch : -1.0f;

        if (currentMode == 2)
        {
            auto* v = findVoice();
            v->age = ++ageCounter;
            v->start (note, velocity, from, true);
        }
        else
        {
            auto& v = voices[0];
            const bool sounding = v.isActive() && ! v.isReleasing();
            if (sounding && currentMode == 1)
                v.setTarget (note);                      // legato: scivola senza riattaccare
            else
                v.start (note, velocity, sounding ? v.getPitch() : from, true);
        }
        lastPitch = (float) note;
    }

    void Synth::noteOff (int note)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        if (currentMode == 2)
        {
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleasing() && v.getNote() == note) v.stop();
            return;
        }
        auto& v = voices[0];
        if (v.getNote() != note || ! v.isActive()) return;
        if (! heldNotes.empty())
        {
            v.setTarget (heldNotes.back());
            lastPitch = (float) heldNotes.back();
        }
        else
            v.stop();
    }

    void Synth::allNotesOff()
    {
        heldNotes.clear();
        for (auto& v : voices) if (v.isActive()) v.stop();
    }

    void Synth::crunchBlock (float* x, int n, const BlockParams& p, float extraCrush)
    {
        const float blend = juce::jlimit (0.0f, 1.0f, p.crunch + extraCrush);
        const bool tubeOn = p.tube > 0.001f, clipOn = p.clipDrive > 0.001f;
        if (! tubeOn && blend <= 0.001f && ! clipOn)
            return;

        // Valvola: saturazione asimmetrica (armoniche pari), volume compensato
        const float drive = 1.0f + p.tube * 10.0f, bias = 0.18f * p.tube;
        const float tBias = fastTanh (drive * bias), tNorm = 0.5f / juce::jmax (0.05f, fastTanh (drive * (0.5f + bias)) - tBias);
        // Bitcrusher: bit e riduzione dei campioni
        const float steps = std::exp2 (p.bits - 1.0f);
        const float hold = 1.0f + p.rate * p.rate * 31.0f;
        const float cd = 1.0f + p.clipDrive * 9.0f, softNorm = 1.0f / fastTanh (cd);

        for (int i = 0; i < n; ++i)
        {
            float y = x[i];
            if (tubeOn)
                y = (fastTanh (drive * (y + bias)) - tBias) * tNorm;
            if (blend > 0.001f)
            {
                holdCount += 1.0f;
                if (holdCount >= hold) { holdCount -= hold; holdSample = y; }
                const float q = std::round (holdSample * steps) / steps;
                y += blend * (q - y);
            }
            if (clipOn)
                y = p.clipMode == 0 ? fastTanh (y * cd) * softNorm * 0.8f : juce::jlimit (-0.8f, 0.8f, y * cd * 0.8f);
            x[i] = y;
        }
    }

    void Synth::render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p, double bpm, double ppq, bool playing)
    {
        if (n <= 0)
            return;
        if (n > maxChunk)
        {
            render (buffer, start, maxChunk, p, bpm, ppq, playing);
            render (buffer, start + maxChunk, n - maxChunk, p, bpm, ppq + bpm / 60.0 / sr * maxChunk, playing);
            return;
        }

        if (p.mode != currentMode)
        {
            for (int i = 1; i < maxVoices; ++i) voices[(size_t) i].stop();
            currentMode = p.mode;
        }
        currentGlide = p.glide;

        bool anyActive = false;
        for (auto& v : voices) anyActive = anyActive || v.isActive();

        const double beatsPerSample = bpm / 60.0 / sr;
        if (! anyActive)
        {
            // Silenzio: niente da calcolare (gli effetti finiscono la coda)
            currentStep.store (-1);
            freeBeats += beatsPerSample * n;
            auto* L = buffer.getWritePointer (0, start);
            auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : L;
            fx.process (L, R, n, p.fx, bpm);
            return;
        }

        std::fill (mainBuf.begin(), mainBuf.begin() + n, 0.0f);
        std::fill (subBuf.begin(), subBuf.begin() + n, 0.0f);

        const float glideCoef = p.glide > 0.0001f ? (float) std::exp (-1.0 / (p.glide * 0.3 * sr / controlRate)) : 0.0f;
        const float stepGlideCoef = (float) std::exp (-1.0 / (juce::jmax (0.002f, p.matGlide) * 0.3 * sr / controlRate));
        const double sBeats = stepBeats[juce::jlimit (0, 2, p.matRate)];
        const double lBeats = divBeats[juce::jlimit (0, 7, p.lfoDiv)];
        const double pBeats = pumpBeats[juce::jlimit (0, 2, p.pumpDiv)];
        const int len = juce::jlimit (2, numSteps, p.matLen);

        std::array<float, maxChunk / controlRate + 2> gateTarget {}, pumpGain {};
        float crushAcc = 0.0f;
        int blocks = 0;

        for (int pos = 0; pos < n; pos += controlRate, ++blocks)
        {
            const int m = juce::jmin (controlRate, n - pos);
            const double beat = playing ? ppq + beatsPerSample * pos : freeBeats + beatsPerSample * pos;
            Mods mods;

            // --- Step matrix
            float gTarget = 1.0f, stepCrush = 0.0f;
            if (p.matOn)
            {
                const double pairLen = sBeats * 2.0;
                const double pair = std::floor (beat / pairLen);
                const double within = beat - pair * pairLen;
                const double boundary = sBeats * (1.0 + p.swing * 0.6);
                const bool second = within >= boundary;
                const double stepFrac = second ? (within - boundary) / (pairLen - boundary) : within / boundary;
                const int step = (int) ((((long long) pair * 2 + (second ? 1 : 0)) % len + len) % len);

                if (step != lastStep)
                {
                    lastStep = step;
                    if (p.gate[(size_t) step] && p.retrig > 0)
                    {
                        mods.retrigFilter = true;
                        mods.retrigAmp = p.retrig == 2;
                    }
                    if (! p.glideStep[(size_t) step])
                        stepPitch = (float) p.pitch[(size_t) step];
                }
                if (p.glideStep[(size_t) step])
                    stepPitch = (float) p.pitch[(size_t) step] + (stepPitch - (float) p.pitch[(size_t) step]) * stepGlideCoef;

                const bool open = p.gate[(size_t) step] && (stepFrac < p.gateLen || (p.gateLen > 0.97f && p.gate[(size_t) ((step + 1) % len)]));
                gTarget = open ? 1.0f : 0.0f;
                mods.pitch = stepPitch;
                cutSmooth += (-(1.0f - p.cut[(size_t) step]) * p.matCut * 6.0f - cutSmooth) * 0.5f;
                mods.cutOct = cutSmooth;
                stepCrush = p.crush[(size_t) step] * p.matCrush;
                currentStep.store (step);
            }
            else
            {
                stepPitch = 0.0f;
                lastStep = -1;
                currentStep.store (-1);
            }
            crushAcc = juce::jmax (crushAcc, stepCrush);

            // --- LFO a tempo
            const double lph = beat / lBeats;
            const float t = (float) (lph - std::floor (lph));
            const int cycle = (int) std::floor (lph);
            if (cycle != (int) lfoPhase) { lfoPhase = cycle; lfoHold = randBi (lfoRnd); }
            const float lfo = p.lfoShape == 0 ? perf::fastSin (t)
                            : p.lfoShape == 1 ? 1.0f - 4.0f * std::abs (t - 0.5f)
                            : p.lfoShape == 2 ? 1.0f - 2.0f * t
                            : p.lfoShape == 3 ? (t < 0.5f ? 1.0f : -1.0f) : lfoHold;
            mods.cutOct += p.lfoCut * lfo * 3.0f;
            mods.wave = p.lfoWave * (0.5f + 0.5f * lfo);
            mods.pitch += p.lfoPitch * lfo * 2.0f;

            // --- Pump (sidechain simulato): giu' di colpo sul battito, poi risale
            float duck = 0.0f;
            if (p.pump > 0.001f)
            {
                const double pp = beat / pBeats;
                const float ph = (float) (pp - std::floor (pp));
                const float att = 0.02f;
                duck = ph < att ? ph / att : std::exp (-(ph - att) * (4.0f + p.pumpShape * 10.0f));
                mods.cutOct -= duck * p.pumpFilter * p.pump * 4.0f;
            }
            pumpGain[(size_t) blocks] = 1.0f - p.pump * duck * 0.95f;
            gateTarget[(size_t) blocks] = gTarget;

            for (auto& v : voices)
                if (v.isActive())
                    v.render (mainBuf.data() + pos, subBuf.data() + pos, m, p, mods, glideCoef);
        }
        freeBeats += beatsPerSample * n;

        crunchBlock (mainBuf.data(), n, p, crushAcc);

        // Gate (morbido, 1 ms: niente click), pump, uscita
        auto* L = buffer.getWritePointer (0, start);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : L;
        const float gateCoef = 1.0f - std::exp (-1.0f / (0.0012f * (float) sr));
        float prevPump = pumpGain[0];
        for (int i = 0; i < n; ++i)
        {
            const int b = i / controlRate;
            gateGain += (juce::jmax (0.0003f, gateTarget[(size_t) b]) - gateGain) * gateCoef;   // mai zero esatto: il motore non spegne lo strumento
            const float pg = prevPump + (pumpGain[(size_t) b] - prevPump) * ((float) (i % controlRate) / controlRate);
            if (i % controlRate == controlRate - 1) prevPump = pumpGain[(size_t) b];
            float x = (mainBuf[(size_t) i] + subBuf[(size_t) i]) * gateGain * pg;
            // DC block
            const float y = x - dcX + 0.9995f * dcY;
            dcX = x; dcY = y;
            L[i] += y * 0.5f;   // -6 dB: margine per gli effetti e il mixer
            R[i] += y * 0.5f;
        }

        fx.process (L, R, n, p.fx, bpm);
    }

    //==========================================================================
    // Preset

    void Presets::set (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    void Presets::init()
    {
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                r->setValueNotifyingHost (r->getDefaultValue());
    }

    /**
        Matrice scritta come testo, 16 caratteri per lane:
        gate  "x.xx" (x = suona, . = muto)
        cut   "9..0" (0-9: 9 = cutoff pieno)
        crush "0..9"
        pitch: lista di semitoni; glide: "g" dove scivolare.
    */
    struct Matrix
    {
        const char* gate = "xxxxxxxxxxxxxxxx";
        const char* cut = "9999999999999999";
        const char* crush = "0000000000000000";
        std::array<int, numSteps> pitch {};
        const char* glide = "................";
    };

    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
        bool matrix = false;
        Matrix m {};
    };

    static const std::vector<FactoryPreset>& factory()
    {
        // Riferimenti utili: o1Wave 0 saw 1 square 2 pulse | o1Oct 2 = 0 | tabelle 0 metal 1 growl 2 digisaw 3 formant 4 hollow 5 bitwave 6 sync 7 fold
        // fType 0 LP24 1 LP12 2 BP 3 HP | mode 0 mono 1 legato 2 poly | lfoDiv 2 = 1/4, 3 = 1/8, 4 = 1/16, 6 = 1/8T | matRate 1 = 1/16
        static const std::vector<FactoryPreset> list =
        {
            { "Init", {} },

            // ---------------- BASSI CHOPPATI
            { "Choppy Bass", { { "o1Wave", 0 }, { "o1Oct", 1 }, { "o1Drift", 0.3f }, { "o1Level", 0.9f },
                               { "o2Table", 0 }, { "o2Wave", 0.35f }, { "o2Level", 0.55f }, { "o2Semi", 0 }, { "o2Fine", 9 }, { "o2Fm", 0.15f },
                               { "subLevel", 0.55f }, { "subOct", 0 }, { "subDrive", 0.2f },
                               { "cutoff", 700 }, { "reso", 0.45f }, { "fEnv", 0.55f }, { "fD", 0.16f }, { "fKey", 0.4f },
                               { "aA", 0.001f }, { "aR", 0.05f },
                               { "tube", 0.45f }, { "crunch", 0.15f }, { "bits", 7 }, { "rate", 0.25f }, { "clipDrive", 0.35f },
                               { "pump", 0.45f }, { "pumpShape", 0.6f },
                               { "matOn", 1 }, { "gateLen", 0.55f }, { "matCut", 0.6f }, { "matCrush", 0.7f }, { "master", 4.7f } },
              true, { "x.xx.xx.x.xxx.x.", "9496738594637282", "0010003000100060", { 0,0,12,0, 0,0,-12,0, 0,0,12,0, 7,0,12,0 }, "..g.....g......." } },

            { "Roulette Growl", { { "o1Wave", 0 }, { "o1Oct", 1 }, { "o1Level", 0.5f },
                                  { "o2Table", 1 }, { "o2Wave", 0.25f }, { "o2Level", 0.9f }, { "o2Fm", 0.25f }, { "o2Semi", -12 }, { "o2Fine", 0 },
                                  { "subLevel", 0.5f }, { "subOct", 0 },
                                  { "fType", 0 }, { "cutoff", 1100 }, { "reso", 0.55f }, { "fEnv", 0.2f }, { "fD", 0.3f },
                                  { "lfoShape", 0 }, { "lfoDiv", 3 }, { "lfoCut", 0.55f }, { "lfoWave", 0.6f },
                                  { "tube", 0.55f }, { "crunch", 0.3f }, { "bits", 6 }, { "rate", 0.15f }, { "clipMode", 1 }, { "clipDrive", 0.4f },
                                  { "mode", 1 }, { "glide", 0.04f }, { "pump", 0.35f },
                                  { "matOn", 1 }, { "gateLen", 0.9f }, { "matCut", 0.3f }, { "master", -0.1f } },
              true, { "xxx.xx.xxxx.xx.x", "9999999999999999", "0000300000060000", { 0,0,0,0, 0,0,0,0, 0,0,0,0, 12,0,0,0 }, "............g..." } },

            { "Fidget Chop", { { "o1Wave", 1 }, { "o1Oct", 1 }, { "o1Level", 0.7f },
                               { "o2Table", 3 }, { "o2Wave", 0.4f }, { "o2Level", 0.7f }, { "o2Semi", 12 }, { "o2Fine", 0 },
                               { "subLevel", 0.45f },
                               { "cutoff", 900 }, { "reso", 0.6f }, { "fEnv", 0.7f }, { "fD", 0.11f }, { "fKey", 0.5f },
                               { "aD", 0.25f }, { "aS", 0.6f }, { "aR", 0.04f },
                               { "tube", 0.3f }, { "crunch", 0.1f }, { "bits", 8 }, { "clipDrive", 0.3f },
                               { "pump", 0.5f }, { "pumpDiv", 1 },
                               { "matOn", 1 }, { "retrig", 2 }, { "swing", 0.25f }, { "gateLen", 0.45f }, { "matCut", 0.5f }, { "master", 3.7f } },
              true, { "x..x..x.x.xx.x.x", "9008007090860707", "0000000000300050", { 0,0,0,12, 0,0,7,0, 0,0,12,5, 0,-5,0,12 }, "................" } },

            { "Rack Saw Bass", { { "o1Wave", 0 }, { "o1Oct", 1 }, { "o1Drift", 0.45f }, { "o1Level", 0.9f },
                                 { "o2Table", 2 }, { "o2Wave", 0.15f }, { "o2Level", 0.65f }, { "o2Fine", -11 },
                                 { "subLevel", 0.6f }, { "subDrive", 0.35f },
                                 { "cutoff", 380 }, { "reso", 0.72f }, { "fEnv", 0.75f }, { "fD", 0.14f }, { "fKey", 0.5f },
                                 { "aA", 0.001f }, { "aR", 0.06f },
                                 { "tube", 0.5f }, { "clipDrive", 0.25f },
                                 { "pump", 0.4f },
                                 { "matOn", 1 }, { "retrig", 1 }, { "gateLen", 0.7f }, { "matCut", 0.35f }, { "master", 1.4f } },
              true, { "xxxxxxxxxxxxxxxx", "9595959595959595", "0000000000000000", { 0,0,12,0, 0,0,12,0, 0,0,12,0, 0,12,0,12 }, "................" } },

            { "Yoi Brostep", { { "o1Wave", 0 }, { "o1Oct", 1 }, { "o1Level", 0.4f },
                               { "o2Table", 3 }, { "o2Wave", 0.1f }, { "o2Level", 0.95f }, { "o2Fm", 0.35f }, { "o2Semi", 0 }, { "o2Fine", 0 },
                               { "subLevel", 0.55f },
                               { "fType", 2 }, { "cutoff", 900 }, { "reso", 0.5f }, { "fEnv", 0.1f },
                               { "lfoShape", 1 }, { "lfoDiv", 6 }, { "lfoCut", 0.7f }, { "lfoWave", 0.85f },
                               { "tube", 0.5f }, { "crunch", 0.2f }, { "bits", 7 }, { "clipMode", 0 }, { "clipDrive", 0.3f }, { "rate", 0.1f },
                               { "mode", 1 }, { "glide", 0.06f },
                               { "matOn", 1 }, { "gateLen", 0.95f }, { "matCut", 0.2f }, { "master", -1.1f } },
              true, { "xxxxxx.xxxxxxx.x", "9999999999999999", "0000000000000000", { 0,0,0,0, 0,0,0,0, 0,0,0,0, -12,-12,0,0 }, "............g.g." } },

            { "Bitcrush Sub", { { "o1Wave", 1 }, { "o1Oct", 1 }, { "o1Level", 0.45f },
                                { "o2Table", 5 }, { "o2Wave", 0.5f }, { "o2Level", 0.5f },
                                { "subLevel", 0.85f }, { "subOct", 0 }, { "subDrive", 0.5f },
                                { "cutoff", 600 }, { "reso", 0.2f }, { "fEnv", 0.4f }, { "fD", 0.2f },
                                { "crunch", 0.55f }, { "bits", 4.5f }, { "rate", 0.45f },
                                { "pump", 0.6f },
                                { "matOn", 1 }, { "gateLen", 0.6f }, { "matCrush", 0.9f }, { "master", 4.8f } },
              true, { "x.x.xxx.x.x.xx.x", "9999999999999999", "0050090005000909", { 0,0,0,0, 0,0,12,0, 0,0,0,0, 0,0,-12,0 }, "................" } },

            { "Electro Stab Bass", { { "o1Wave", 2 }, { "o1Pw", 0.22f }, { "o1Oct", 1 }, { "o1Level", 0.8f },
                                     { "o2Table", 6 }, { "o2Wave", 0.55f }, { "o2Level", 0.6f }, { "o2Semi", 12 },
                                     { "subLevel", 0.5f },
                                     { "cutoff", 500 }, { "reso", 0.5f }, { "fEnv", 0.8f }, { "fD", 0.09f }, { "fKey", 0.4f },
                                     { "aD", 0.18f }, { "aS", 0.0f }, { "aR", 0.05f },
                                     { "tube", 0.55f }, { "clipMode", 1 }, { "clipDrive", 0.45f },
                                     { "pump", 0.35f }, { "master", 1.1f } } },

            // ---------------- LEAD
            { "Screaming Lead", { { "o1Wave", 0 }, { "o1Oct", 2 }, { "o1Drift", 0.35f }, { "o1Level", 0.8f },
                                  { "o2Table", 6 }, { "o2Wave", 0.45f }, { "o2Level", 0.7f }, { "o2Fine", 12 },
                                  { "cutoff", 1600 }, { "reso", 0.6f }, { "fEnv", 0.45f }, { "fD", 0.35f }, { "fS", 0.3f }, { "fKey", 0.6f },
                                  { "lfoShape", 0 }, { "lfoDiv", 2 }, { "lfoPitch", 0.12f },
                                  { "tube", 0.45f }, { "clipMode", 0 }, { "clipDrive", 0.25f }, { "rate", 0.0f },
                                  { "mode", 1 }, { "glide", 0.07f },
                                  { "dlyMix", 0.22f }, { "dlyTime", 2 }, { "revMix", 0.15f }, { "master", -9.1f } } },

            { "Neon Zap Lead", { { "o1Wave", 2 }, { "o1Pw", 0.3f }, { "o1Pwm", 0.8f }, { "o1Oct", 2 }, { "o1Level", 0.85f },
                                 { "o2Table", 0 }, { "o2Wave", 0.6f }, { "o2Level", 0.4f }, { "o2Semi", 12 },
                                 { "cutoff", 2400 }, { "reso", 0.5f }, { "fEnv", 0.5f }, { "fD", 0.2f }, { "fS", 0.4f },
                                 { "tube", 0.35f }, { "crunch", 0.1f }, { "bits", 10 }, { "rate", 0.0f }, { "clipDrive", 0.3f },
                                 { "mode", 1 }, { "glide", 0.05f },
                                 { "matOn", 1 }, { "retrig", 1 }, { "gateLen", 0.8f }, { "matGlide", 0.05f },
                                 { "dlyMix", 0.3f }, { "dlyTime", 1 }, { "revMix", 0.2f }, { "master", -8.5f } },
              true, { "xxxxxxxxxxxxxxxx", "9999999999999999", "0000000000000000", { 0,0,0,0, -12,0,0,0, 0,0,0,0, 12,0,0,0 }, "....g.......g..." } },

            { "Lo-Fi Square Lead", { { "o1Wave", 1 }, { "o1Oct", 2 }, { "o1Level", 0.9f },
                                     { "o2Table", 4 }, { "o2Wave", 0.3f }, { "o2Level", 0.4f }, { "o2Semi", 12 },
                                     { "cutoff", 3200 }, { "reso", 0.3f }, { "fEnv", 0.3f }, { "fD", 0.3f }, { "fS", 0.5f },
                                     { "lfoShape", 0 }, { "lfoDiv", 4 }, { "lfoPitch", 0.08f },
                                     { "crunch", 0.6f }, { "bits", 6 }, { "rate", 0.35f },
                                     { "mode", 1 }, { "glide", 0.03f },
                                     { "dlyMix", 0.25f }, { "dlyTime", 2 }, { "revMix", 0.1f }, { "master", -3.5f } } },

            // ---------------- ARPEGGI E TEXTURE
            { "Granular Arp", { { "o1Wave", 0 }, { "o1Oct", 2 }, { "o1Level", 0.6f },
                                { "o2Table", 0 }, { "o2Wave", 0.5f }, { "o2Level", 0.7f },
                                { "cutoff", 2200 }, { "reso", 0.45f }, { "fEnv", 0.55f }, { "fD", 0.1f }, { "fKey", 0.5f },
                                { "aD", 0.2f }, { "aS", 0.3f },
                                { "crunch", 0.1f }, { "bits", 6 }, { "rate", 0.5f },
                                { "mode", 0 },
                                { "matOn", 1 }, { "retrig", 2 }, { "gateLen", 0.5f }, { "matCut", 0.4f }, { "matCrush", 0.8f },
                                { "dlyMix", 0.35f }, { "dlyTime", 2 }, { "dlyFb", 0.45f }, { "revMix", 0.2f }, { "master", 6.0f } },
              true, { "xxxxxxxxxxxxxxxx", "9693969396939693", "0000300000006000", { 0,12,7,19, 0,12,7,19, 3,15,10,22, 3,15,10,24 }, "................" } },

            { "8-Bit Roulette", { { "o1Wave", 2 }, { "o1Pw", 0.15f }, { "o1Oct", 2 }, { "o1Level", 1.0f },
                                  { "o2Table", 5 }, { "o2Wave", 0.8f }, { "o2Level", 1.0f },
                                  { "cutoff", 6000 }, { "reso", 0.15f }, { "fEnv", 0.2f },
                                  { "aD", 0.15f }, { "aS", 0.5f },
                                  { "crunch", 0.7f }, { "bits", 5 }, { "rate", 0.55f },
                                  { "matOn", 1 }, { "retrig", 2 }, { "gateLen", 0.7f }, { "matRate", 1 },
                                  { "dlyMix", 0.2f }, { "dlyTime", 1 }, { "master", 6.0f } },
              true, { "xxxx.xxxxxxx.xxx", "9999999999999999", "0000000000000000", { 0,12,24,12, 0,7,19,7, 0,12,24,12, 5,17,29,17 }, "................" } },

            { "Filter Disco Chords", { { "o1Wave", 0 }, { "o1Oct", 2 }, { "o1Drift", 0.5f }, { "o1Level", 0.8f },
                                       { "o2Table", 2 }, { "o2Wave", 0.1f }, { "o2Level", 0.6f }, { "o2Fine", 14 },
                                       { "cutoff", 1300 }, { "reso", 0.35f }, { "fEnv", 0.15f }, { "fD", 0.4f }, { "fKey", 0.3f },
                                       { "aA", 0.005f }, { "aR", 0.25f },
                                       { "lfoShape", 0 }, { "lfoDiv", 0 }, { "lfoCut", 0.3f },
                                       { "tube", 0.3f }, { "crunch", 0.15f }, { "bits", 10 },
                                       { "pump", 0.7f }, { "pumpShape", 0.4f }, { "pumpFilter", 0.3f },
                                       { "mode", 2 }, { "chorus", 0.4f }, { "revMix", 0.2f }, { "master", -2.8f } } },

            { "Pump Chord Stab", { { "o1Wave", 0 }, { "o1Oct", 2 }, { "o1Drift", 0.3f }, { "o1Level", 0.8f },
                                   { "o2Table", 6 }, { "o2Wave", 0.3f }, { "o2Level", 0.5f }, { "o2Fine", -10 },
                                   { "cutoff", 1400 }, { "reso", 0.3f }, { "fEnv", 0.5f }, { "fD", 0.18f }, { "fKey", 0.3f },
                                   { "aD", 0.35f }, { "aS", 0.2f }, { "aR", 0.15f },
                                   { "tube", 0.4f }, { "clipDrive", 0.2f },
                                   { "pump", 0.5f },
                                   { "mode", 2 }, { "chorus", 0.3f }, { "dlyMix", 0.15f }, { "dlyTime", 2 }, { "revMix", 0.25f }, { "master", -2.6f } } },
        };
        return list;
    }

    int Presets::numFactory()                    { return (int) factory().size(); }
    juce::String Presets::factoryName (int i)    { return factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)].name; }

    void Presets::loadFactory (int i)
    {
        const auto& p = factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)];
        init();
        for (auto& [id, value] : p.values)
            set (id, value);
        if (p.matrix)
        {
            for (int s = 0; s < numSteps; ++s)
            {
                set (stepId ('g', s), p.m.gate[s] == 'x' ? 1.0f : 0.0f);
                set (stepId ('c', s), (float) (p.m.cut[s] - '0') / 9.0f);
                set (stepId ('x', s), (float) (p.m.crush[s] - '0') / 9.0f);
                set (stepId ('p', s), (float) p.m.pitch[(size_t) s]);
                set (stepId ('l', s), p.m.glide[s] == 'g' ? 1.0f : 0.0f);
            }
        }
        apvts.state.setProperty ("presetName", p.name, nullptr);
    }

    void Presets::randomChop (uint32_t seed)
    {
        uint32_t r = seed * 2654435761u + 12345u;
        auto chance = [&r] (float prob) { return (float) (nextRand (r) >> 8) / 16777216.0f < prob; };
        static const int jumps[] { 12, -12, 7, 5, 24, -5 };
        for (int s = 0; s < numSteps; ++s)
        {
            const bool downbeat = s % 4 == 0;
            set (stepId ('g', s), downbeat || chance (0.6f) ? 1.0f : 0.0f);
            set (stepId ('c', s), downbeat ? 1.0f : 0.25f + 0.75f * (float) (nextRand (r) >> 8) / 16777216.0f);
            set (stepId ('x', s), chance (0.2f) ? 0.4f + 0.6f * (float) (nextRand (r) >> 8) / 16777216.0f : 0.0f);
            const int pitch = ! downbeat && chance (0.3f) ? jumps[(nextRand (r) >> 8) % 6] : 0;
            set (stepId ('p', s), (float) pitch);
            set (stepId ('l', s), pitch != 0 && chance (0.35f) ? 1.0f : 0.0f);
        }
        set ("matOn", 1.0f);
    }

    juce::File Presets::userDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FrenchRoulette").getChildFile ("Presets");
    }

    juce::Array<juce::File> Presets::userPresets() const
    {
        auto files = userDir().findChildFiles (juce::File::findFiles, false, "*.frpreset");
        files.sort();
        return files;
    }

    bool Presets::save (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty()) return false;
        userDir().createDirectory();
        apvts.state.setProperty ("presetName", clean, nullptr);
        if (auto xml = apvts.copyState().createXml())
            return xml->writeTo (userDir().getChildFile (clean + ".frpreset"));
        return false;
    }

    bool Presets::load (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
            return false;
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
        return true;
    }
}

//==============================================================================
FrenchRouletteAudioProcessor::FrenchRouletteAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FrenchRoulette", fr::createLayout())
{
    o1Wave = ptr ("o1Wave"); o1Oct = ptr ("o1Oct"); o1Pw = ptr ("o1Pw"); o1Pwm = ptr ("o1Pwm"); o1Drift = ptr ("o1Drift"); o1Level = ptr ("o1Level");
    o2Table = ptr ("o2Table"); o2Wave = ptr ("o2Wave"); o2Fm = ptr ("o2Fm"); o2Semi = ptr ("o2Semi"); o2Fine = ptr ("o2Fine"); o2Level = ptr ("o2Level");
    subOct = ptr ("subOct"); subLevel = ptr ("subLevel"); subDrive = ptr ("subDrive");
    fType = ptr ("fType"); cutoff = ptr ("cutoff"); reso = ptr ("reso"); fEnv = ptr ("fEnv"); fKey = ptr ("fKey");
    fA = ptr ("fA"); fD = ptr ("fD"); fS = ptr ("fS"); fR = ptr ("fR");
    aA = ptr ("aA"); aD = ptr ("aD"); aS = ptr ("aS"); aR = ptr ("aR"); mode = ptr ("mode"); glide = ptr ("glide");
    lfoShape = ptr ("lfoShape"); lfoDiv = ptr ("lfoDiv"); lfoCut = ptr ("lfoCut"); lfoWave = ptr ("lfoWave"); lfoPitch = ptr ("lfoPitch");
    pump = ptr ("pump"); pumpDiv = ptr ("pumpDiv"); pumpShape = ptr ("pumpShape"); pumpFilter = ptr ("pumpFilter");
    crunch = ptr ("crunch"); tube = ptr ("tube"); bits = ptr ("bits"); rate = ptr ("rate"); clipMode = ptr ("clipMode"); clipDrive = ptr ("clipDrive");
    matOn = ptr ("matOn"); matLen = ptr ("matLen"); matRate = ptr ("matRate"); retrig = ptr ("retrig"); swing = ptr ("swing");
    gateLen = ptr ("gateLen"); matGlide = ptr ("matGlide"); matCut = ptr ("matCut"); matCrush = ptr ("matCrush");
    chorus = ptr ("chorus"); dlyMix = ptr ("dlyMix"); dlyTime = ptr ("dlyTime"); dlyFb = ptr ("dlyFb");
    revMix = ptr ("revMix"); revSize = ptr ("revSize"); master = ptr ("master");

    for (char lane : { 'g', 'c', 'x', 'p', 'l' })
        for (int s = 0; s < fr::numSteps; ++s)
            stepPtr.push_back (ptr (fr::stepId (lane, s)));

    apvts.state.setProperty ("presetName", "Init", nullptr);
}

void FrenchRouletteAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
}

fr::BlockParams FrenchRouletteAudioProcessor::readParams()
{
    fr::BlockParams p;
    p.o1Wave = (int) o1Wave->load(); p.o1Oct = (int) o1Oct->load() - 2; p.o1Pw = o1Pw->load(); p.o1Pwm = o1Pwm->load();
    p.o1Drift = o1Drift->load(); p.o1Level = o1Level->load();
    p.o2Table = juce::jlimit (0, fr::numTables - 1, (int) o2Table->load()); p.o2Wave = o2Wave->load(); p.o2Fm = o2Fm->load();
    p.o2Semi = juce::roundToInt (o2Semi->load()); p.o2Fine = o2Fine->load(); p.o2Level = o2Level->load();
    p.subOct = (int) subOct->load(); p.subLevel = subLevel->load(); p.subDrive = subDrive->load();
    p.fType = (int) fType->load(); p.cutoff = cutoff->load(); p.reso = reso->load(); p.fEnv = fEnv->load(); p.fKey = fKey->load();
    p.fA = fA->load(); p.fD = fD->load(); p.fS = fS->load(); p.fR = fR->load();
    p.aA = aA->load(); p.aD = aD->load(); p.aS = aS->load(); p.aR = aR->load();
    p.mode = (int) mode->load(); p.glide = glide->load();
    p.lfoShape = (int) lfoShape->load(); p.lfoDiv = (int) lfoDiv->load(); p.lfoCut = lfoCut->load(); p.lfoWave = lfoWave->load(); p.lfoPitch = lfoPitch->load();
    p.pump = pump->load(); p.pumpDiv = (int) pumpDiv->load(); p.pumpShape = pumpShape->load(); p.pumpFilter = pumpFilter->load();
    p.crunch = crunch->load(); p.tube = tube->load(); p.bits = bits->load(); p.rate = rate->load();
    p.clipMode = (int) clipMode->load(); p.clipDrive = clipDrive->load();
    p.matOn = matOn->load() > 0.5f; p.matLen = juce::roundToInt (matLen->load()); p.matRate = (int) matRate->load(); p.retrig = (int) retrig->load();
    p.swing = swing->load(); p.gateLen = gateLen->load(); p.matGlide = matGlide->load(); p.matCut = matCut->load(); p.matCrush = matCrush->load();
    for (int s = 0; s < fr::numSteps; ++s)
    {
        p.gate[(size_t) s]      = stepPtr[(size_t) s]->load() > 0.5f;
        p.cut[(size_t) s]       = stepPtr[(size_t) (16 + s)]->load();
        p.crush[(size_t) s]     = stepPtr[(size_t) (32 + s)]->load();
        p.pitch[(size_t) s]     = juce::roundToInt (stepPtr[(size_t) (48 + s)]->load());
        p.glideStep[(size_t) s] = stepPtr[(size_t) (64 + s)]->load() > 0.5f;
    }
    static constexpr double delayBeats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    p.fx.chorus = chorus->load(); p.fx.delayMix = dlyMix->load(); p.fx.delayFeedback = dlyFb->load();
    p.fx.delayBeats = delayBeats[juce::jlimit (0, 5, (int) dlyTime->load())];
    p.fx.reverbMix = revMix->load(); p.fx.reverbSize = revSize->load();
    p.fx.gain = juce::Decibels::decibelsToGain (master->load());
    return p;
}

void FrenchRouletteAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }

    const auto p = readParams();
    const double beatsPerSample = bpm / 60.0 / getSampleRate();
    int done = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        synth.render (buffer, done, pos - done, p, bpm, ppq + beatsPerSample * done, playing);
        done = pos;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                                    synth.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                              synth.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())   synth.allNotesOff();
    }
    synth.render (buffer, done, buffer.getNumSamples() - done, p, bpm, ppq + beatsPerSample * done, playing);
}

juce::AudioProcessorEditor* FrenchRouletteAudioProcessor::createEditor()    { return new FrenchRouletteEditor (*this); }

void FrenchRouletteAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FrenchRouletteAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
