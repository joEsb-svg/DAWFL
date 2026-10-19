#include "FMForge.h"
#include "FMForgeEditor.h"

namespace fm
{
    static const juce::StringArray algoNames { "1: 4>3>2>1", "2: (3+4)>2>1", "3: 3>2>1 + 4>1", "4: 4>3>1 + 2>1",
                                               "5: 2>1 + 4>3", "6: 4>1,2,3", "7: 4>3 + 1 + 2", "8: tutti (organo)" };
    static const juce::StringArray lfoNames  { "Sinusoide", "Triangolo", "Quadra", "Casuale" };
    static const juce::StringArray divNames  { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/8 ." };
    static const juce::StringArray modeNames { "Poly", "Mono", "Legato" };
    static const juce::StringArray delayNames { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
    static constexpr double divBeats[]   { 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 0.75 };
    static constexpr double delayBeats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };

    //==========================================================================
    // Tabella del seno (veloce e precisa abbastanza per l'FM)

    static constexpr int tableSize = 4096;

    static const float* sineTable()
    {
        static const auto table = []
        {
            std::array<float, tableSize + 1> t {};
            for (int i = 0; i <= tableSize; ++i)
                t[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / tableSize);
            return t;
        }();
        return table.data();
    }

    static inline float sineCycles (float phase) noexcept   // fase in giri (qualsiasi valore)
    {
        phase -= std::floor (phase);
        const float idx = phase * (float) tableSize;
        const int i = (int) idx;
        const float f = idx - (float) i;
        const float* t = sineTable();
        return t[i] + f * (t[i + 1] - t[i]);
    }

    static inline float mtof (float m) noexcept     { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        auto pctText  = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
        auto secText  = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; };
        auto hzText   = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (v, v < 10.0f ? 2 : 0) + " Hz"; };

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

        choice ("algo", "Algoritmo", algoNames, 0);
        flt ("feedback", "Feedback", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("bright", "Timbro", { 0.0f, 2.0f }, 1.0f, pctText);
        flt ("tone", "Tono", skewed (500.0f, 20000.0f, 4000.0f), 16000.0f, hzText);

        for (int op = 0; op < numOps; ++op)
        {
            const auto n = "Op " + String (op + 1) + " ";
            choice (opId (op, "Ratio"), n + "Rapporto", ratioNames(), 1);
            flt (opId (op, "Fine"), n + "Fine", { -50.0f, 50.0f }, 0.0f, [] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " ct"; });
            flt (opId (op, "Level"), n + "Livello", { 0.0f, 1.0f }, op == 0 ? 0.9f : (op == 1 ? 0.4f : 0.0f), pctText);
            flt (opId (op, "Vel"), n + "Velocity", { 0.0f, 1.0f }, 0.5f, pctText);
            flt (opId (op, "A"), n + "Attacco", skewed (0.001f, 10.0f, 0.5f), 0.002f, secText);
            flt (opId (op, "D"), n + "Decay", skewed (0.005f, 15.0f, 1.0f), 1.0f, secText);
            flt (opId (op, "S"), n + "Sustain", { 0.0f, 1.0f }, 0.8f, pctText);
            flt (opId (op, "R"), n + "Rilascio", skewed (0.005f, 10.0f, 0.8f), 0.3f, secText);
        }

        choice ("lfoWave", "LFO Forma", lfoNames, 0);
        flt ("lfoRate", "LFO Velocita'", skewed (0.05f, 20.0f, 3.0f), 5.0f, hzText);
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lfoSync", 1 }, "LFO a tempo", false));
        choice ("lfoDiv", "LFO Divisione", divNames, 2);
        flt ("lfoVib", "Vibrato", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("lfoTrem", "Tremolo", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("lfoBright", "LFO > Timbro", { 0.0f, 1.0f }, 0.0f, pctText);

        choice ("mode", "Modo", modeNames, 0);
        flt ("glide", "Glide", skewed (0.0f, 2.0f, 0.2f), 0.0f, secText);

        flt ("chorus", "Chorus", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("dlyMix", "Delay", { 0.0f, 1.0f }, 0.0f, pctText);
        choice ("dlyTime", "Tempo delay", delayNames, 2);
        flt ("dlyFb", "Feedback delay", { 0.0f, 0.9f }, 0.35f, pctText);
        flt ("revMix", "Riverbero", { 0.0f, 1.0f }, 0.0f, pctText);
        flt ("revSize", "Stanza", { 0.0f, 1.0f }, 0.6f, pctText);
        flt ("master", "Volume", { -24.0f, 6.0f }, 0.0f, [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });

        return { p.begin(), p.end() };
    }

    //==========================================================================
    // Voce

    void Voice::start (int midiNote, float vel, float glideFrom, bool retrigger)
    {
        note = midiNote;
        velocity = vel;
        targetFreq = mtof ((float) midiNote);
        freq = glideFrom > 0.0f ? glideFrom : targetFreq;

        if (retrigger || ! active)
        {
            if (! active)
            {
                phase.fill (0.0f);   // fasi a zero: attacco sempre uguale (tipico dell'FM)
                fb1 = fb2 = 0.0f;
            }
            for (auto& e : env)
                e.noteOn (false);
        }
        active = true;
    }

    void Voice::setTarget (int midiNote)
    {
        note = midiNote;
        targetFreq = mtof ((float) midiNote);
    }

    void Voice::render (float* out, int n, const BlockParams& p, float lfo, float glideCoef)
    {
        if (! active)
            return;

        for (int o = 0; o < numOps; ++o)
            env[(size_t) o].set (sr, p.ops[(size_t) o].a, p.ops[(size_t) o].d, p.ops[(size_t) o].s, p.ops[(size_t) o].r);

        freq = targetFreq * std::pow (freq / targetFreq, glideCoef);
        const float f0 = freq * std::exp2 (lfo * p.lfoVib * 0.5f / 12.0f);
        const auto& alg = algorithm (p.algo);
        const float invSr = (float) (1.0 / sr);

        // Livelli: le portanti escono a volume (livello), i modulatori diventano indice di modulazione
        float inc[numOps], amp[numOps];
        int numCarriers = 0;
        const float brightness = juce::jmax (0.0f, p.bright * (1.0f + lfo * p.lfoBright * 0.6f));
        for (int o = 0; o < numOps; ++o)
        {
            const auto& op = p.ops[(size_t) o];
            inc[o] = juce::jmin (0.49f, f0 * op.ratio * invSr);
            const float velGain = 1.0f - op.vel * (1.0f - velocity);
            const bool carrier = (alg.carriers >> o) & 1;
            if (carrier)
            {
                amp[o] = op.level * velGain;
                ++numCarriers;
            }
            else
            {
                amp[o] = op.level * op.level * 1.15f * brightness * velGain;   // indice in giri (~7 radianti al massimo)
            }
        }
        const float outGain = 0.32f / std::sqrt ((float) juce::jmax (1, numCarriers)) * (1.0f - p.lfoTrem * (0.5f + 0.5f * lfo));
        const float fbAmount = p.feedback * p.feedback * 0.35f;

        for (int i = 0; i < n; ++i)
        {
            float e[numOps];
            for (int o = 0; o < numOps; ++o)
                e[o] = env[(size_t) o].next();

            // Fine nota quando tutte le portanti hanno finito l'inviluppo
            bool sounding = false;
            for (int o = 0; o < numOps; ++o)
                if (((alg.carriers >> o) & 1) && env[(size_t) o].stage != pf::Adsr::idle)
                    sounding = true;
            if (! sounding)
            {
                active = false;
                return;
            }

            // Operatori dal 4 all'1: negli algoritmi i modulatori hanno sempre un numero piu' alto
            float y[numOps] {};
            for (int o = numOps - 1; o >= 0; --o)
            {
                float mod = 0.0f;
                for (int m = o + 1; m < numOps; ++m)
                    if ((alg.modulators[(size_t) o] >> m) & 1)
                        mod += y[m];
                if (o == numOps - 1)
                    mod += (fb1 + fb2) * 0.5f * fbAmount;   // feedback dell'operatore 4 su se stesso

                const float s = sineCycles (phase[(size_t) o] + mod);
                y[o] = s * e[o] * amp[o];
                phase[(size_t) o] += inc[o];
                if (phase[(size_t) o] >= 1.0f) phase[(size_t) o] -= 1.0f;
            }
            fb2 = fb1;
            fb1 = y[numOps - 1] / juce::jmax (0.0001f, amp[numOps - 1]) ;

            float sum = 0.0f;
            for (int o = 0; o < numOps; ++o)
                if ((alg.carriers >> o) & 1)
                    sum += y[o];
            out[i] += sum * outGain;
        }
    }

    //==========================================================================
    // Synth

    void Synth::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices) v.prepare (sr);
        heldNotes.clear(); heldNotes.reserve (128);
        mono.assign ((size_t) juce::jmax (512, maxBlock), 0.0f);
        for (auto& f : toneFilter) f.reset();
        fx.prepare (sr);
        sineTable();
    }

    Voice* Synth::findVoice()
    {
        Voice* best = nullptr;
        for (auto& v : voices) if (! v.isActive()) return &v;
        for (auto& v : voices) if (v.isReleasing() && (best == nullptr || v.age < best->age)) best = &v;
        if (best != nullptr) return best;
        for (auto& v : voices) if (best == nullptr || v.age < best->age) best = &v;
        return best;
    }

    void Synth::noteOn (int note, float velocity)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);

        if (currentMode == 0)
        {
            auto* v = findVoice();
            v->age = ++ageCounter;
            v->start (note, velocity, currentGlide > 0.0001f ? lastFreq : 0.0f, true);
            lastFreq = mtof ((float) note);
            return;
        }

        auto& v = voices[0];
        const bool sounding = v.isActive() && ! v.isReleasing();
        if (sounding)
        {
            v.setTarget (note);
            if (currentMode == 1)
                v.start (note, velocity, v.getFrequency(), true);
        }
        else
            v.start (note, velocity, currentGlide > 0.0001f && lastFreq > 0.0f ? lastFreq : 0.0f, true);

        lastFreq = mtof ((float) note);
    }

    void Synth::noteOff (int note)
    {
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        if (currentMode == 0)
        {
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleasing() && v.getNote() == note)
                    v.stop();
            return;
        }

        auto& v = voices[0];
        if (v.getNote() != note || ! v.isActive())
            return;
        if (! heldNotes.empty())
        {
            v.setTarget (heldNotes.back());
            lastFreq = mtof ((float) heldNotes.back());
        }
        else
            v.stop();
    }

    void Synth::allNotesOff()
    {
        heldNotes.clear();
        for (auto& v : voices)
            if (v.isActive()) v.stop();
    }

    void Synth::render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p, double bpm, double ppq, bool playing)
    {
        if (n <= 0)
            return;

        if (p.mode != currentMode)
        {
            for (int i = 1; i < maxVoices; ++i) voices[(size_t) i].stop();
            currentMode = p.mode;
        }
        currentGlide = p.glide;

        auto* L = buffer.getWritePointer (0, start);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : L;
        if ((int) mono.size() < n)
            return;
        std::fill (mono.begin(), mono.begin() + n, 0.0f);

        const double beats = divBeats[juce::jlimit (0, 7, p.lfoDiv)];
        const double lfoHz = p.lfoSync ? (bpm / 60.0) / beats : (double) p.lfoRate;
        if (p.lfoSync && playing)
            lfoPhase = std::fmod (ppq / beats, 1.0);
        const float glideCoef = p.glide > 0.0001f ? (float) std::exp (-1.0 / (p.glide * 0.35 * sr / controlRate)) : 0.0f;

        for (int pos = 0; pos < n; pos += controlRate)
        {
            const int m = juce::jmin (controlRate, n - pos);
            lfoPhase += lfoHz * m / sr;
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= std::floor (lfoPhase);
                lfoRandom = lfoRandom * 1664525u + 1013904223u;
                lfoHold = (float) (lfoRandom >> 8) / 8388608.0f - 1.0f;
            }
            const float t = (float) lfoPhase;
            const float lfo = p.lfoWave == 0 ? std::sin (juce::MathConstants<float>::twoPi * t)
                            : p.lfoWave == 1 ? 1.0f - 4.0f * std::abs (t - 0.5f)
                            : p.lfoWave == 2 ? (t < 0.5f ? 1.0f : -1.0f) : lfoHold;

            for (auto& v : voices)
                if (v.isActive())
                    v.render (mono.data() + pos, m, p, lfo, glideCoef);
        }

        // Tono (passa-basso dolce) e uscita stereo
        const float g = std::tan (juce::MathConstants<float>::pi * juce::jmin (p.tone, (float) sr * 0.45f) / (float) sr);
        toneFilter[0].setCoefficients (g, 1.41f);
        for (int i = 0; i < n; ++i)
        {
            const float x = p.tone < 19500.0f ? toneFilter[0].process (mono[(size_t) i], 0) : mono[(size_t) i];
            L[i] += x;
            R[i] += x;
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

    /** Un operatore: rapporto (testo, es. "3.5"), livello, inviluppo, velocity, fine. */
    struct Op { const char* ratio; float level, a, d, s, r, vel = 0.5f, fine = 0.0f; };
    struct FactoryPreset
    {
        const char* name;
        int algo;
        std::array<Op, numOps> ops;
        std::vector<std::pair<const char*, float>> extra;
    };

    static const std::vector<FactoryPreset>& factory()
    {
        // Algoritmi: 0 = 4>3>2>1, 1 = (3+4)>2>1, 2 = 3>2>1+4>1, 3 = 4>3>1+2>1, 4 = 2>1+4>3, 5 = 4>1,2,3, 6 = 4>3+1+2, 7 = tutti
        static const std::vector<FactoryPreset> list =
        {
            { "Init", 0, { { { "1", 0.9f, 0.002f, 1.0f, 0.8f, 0.3f }, { "1", 0.4f, 0.002f, 1.0f, 0.8f, 0.3f },
                             { "1", 0.0f, 0.002f, 1.0f, 0.8f, 0.3f }, { "1", 0.0f, 0.002f, 1.0f, 0.8f, 0.3f } } }, {} },

            // --- Tastiere
            { "Piano Elettrico", 4, { { { "1", 0.9f, 0.001f, 2.5f, 0.0f, 0.5f, 0.6f }, { "1", 0.45f, 0.001f, 1.8f, 0.0f, 0.4f, 0.8f },
                                        { "1", 0.5f, 0.001f, 1.5f, 0.0f, 0.4f, 0.6f, 3.0f }, { "14", 0.45f, 0.001f, 0.25f, 0.0f, 0.2f, 0.9f } } },
              { { "master", -2.5f }, { "chorus", 0.35f }, { "revMix", 0.2f }, { "tone", 9000 }, { "lfoTrem", 0.1f }, { "lfoRate", 4.5f } } },
            { "Piano Elettrico Morbido", 4, { { { "1", 0.9f, 0.001f, 3.0f, 0.0f, 0.6f, 0.5f }, { "1", 0.35f, 0.001f, 2.0f, 0.0f, 0.4f, 0.8f },
                                                { "1", 0.45f, 0.001f, 2.0f, 0.0f, 0.5f, 0.5f, 4.0f }, { "14", 0.25f, 0.001f, 0.2f, 0.0f, 0.2f, 0.9f } } },
              { { "master", -1.5f }, { "chorus", 0.5f }, { "revMix", 0.25f }, { "tone", 5000 }, { "lfoTrem", 0.25f }, { "lfoRate", 4.0f } } },
            { "Organo FM", 7, { { { "0.5", 0.5f, 0.002f, 1.0f, 1.0f, 0.06f, 0.0f }, { "1", 0.7f, 0.002f, 1.0f, 1.0f, 0.06f, 0.0f },
                                  { "2", 0.5f, 0.002f, 1.0f, 1.0f, 0.06f, 0.0f }, { "3", 0.35f, 0.002f, 1.0f, 1.0f, 0.06f, 0.0f } } },
              { { "master", 1.5f }, { "lfoTrem", 0.15f }, { "lfoRate", 6.5f }, { "chorus", 0.4f } } },
            { "Clavinet FM", 3, { { { "1", 0.9f, 0.001f, 0.6f, 0.3f, 0.05f, 0.6f }, { "3", 0.45f, 0.001f, 0.3f, 0.2f, 0.05f, 0.8f },
                                    { "1", 0.4f, 0.001f, 0.15f, 0.0f, 0.05f, 0.8f }, { "7", 0.25f, 0.001f, 0.05f, 0.0f, 0.05f, 0.9f } } },
              { { "master", 6.0f }, { "tone", 10000 } } },

            // --- Campane e percussioni melodiche
            { "Campana", 4, { { { "1", 0.8f, 0.001f, 4.0f, 0.0f, 2.0f }, { "3.5", 0.55f, 0.001f, 3.0f, 0.0f, 2.0f },
                                { "1", 0.4f, 0.001f, 3.0f, 0.0f, 2.0f, 0.5f, 6.0f }, { "1.41", 0.3f, 0.001f, 2.0f, 0.0f, 1.5f } } },
              { { "master", -1.5f }, { "revMix", 0.4f }, { "revSize", 0.8f } } },
            { "Campane Tubolari", 4, { { { "1", 0.8f, 0.001f, 6.0f, 0.0f, 3.0f }, { "3.5", 0.5f, 0.001f, 4.0f, 0.0f, 3.0f },
                                         { "2", 0.3f, 0.001f, 4.0f, 0.0f, 3.0f }, { "5", 0.3f, 0.001f, 2.5f, 0.0f, 2.0f } } },
              { { "master", -3.0f }, { "revMix", 0.45f }, { "revSize", 0.85f } } },
            { "Glockenspiel FM", 4, { { { "1", 0.8f, 0.001f, 2.0f, 0.0f, 1.0f }, { "8", 0.35f, 0.001f, 0.6f, 0.0f, 0.5f },
                                        { "4", 0.3f, 0.001f, 1.2f, 0.0f, 0.8f }, { "14", 0.3f, 0.001f, 0.3f, 0.0f, 0.3f } } },
              { { "master", 1.5f }, { "revMix", 0.3f } } },
            { "Marimba FM", 0, { { { "1", 0.9f, 0.001f, 0.7f, 0.0f, 0.3f, 0.6f }, { "3.5", 0.35f, 0.001f, 0.05f, 0.0f, 0.05f, 0.8f },
                                   { "1", 0.0f, 0.001f, 1.0f, 0.0f, 0.3f }, { "1", 0.0f, 0.001f, 1.0f, 0.0f, 0.3f } } },
              { { "master", 2.0f }, { "revMix", 0.2f } } },
            { "Vibrafono FM", 4, { { { "1", 0.85f, 0.001f, 3.5f, 0.0f, 1.0f }, { "4", 0.3f, 0.001f, 0.5f, 0.0f, 0.5f },
                                     { "4", 0.2f, 0.001f, 1.0f, 0.0f, 0.8f }, { "1", 0.0f, 0.001f, 1.0f, 0.0f, 0.3f } } },
              { { "master", 2.0f }, { "lfoTrem", 0.35f }, { "lfoRate", 5.5f }, { "revMix", 0.3f } } },
            { "Kalimba FM", 4, { { { "1", 0.9f, 0.001f, 1.0f, 0.0f, 0.4f }, { "5", 0.25f, 0.001f, 0.04f, 0.0f, 0.04f },
                                   { "6", 0.15f, 0.001f, 0.15f, 0.0f, 0.1f }, { "1", 0.0f, 0.001f, 1.0f, 0.0f, 0.3f } } },
              { { "master", 3.5f }, { "revMix", 0.25f } } },
            { "Steel Drum FM", 4, { { { "1", 0.9f, 0.001f, 1.2f, 0.0f, 0.5f }, { "2", 0.3f, 0.001f, 0.3f, 0.0f, 0.3f },
                                      { "2", 0.4f, 0.001f, 0.8f, 0.0f, 0.4f }, { "3", 0.2f, 0.001f, 0.2f, 0.0f, 0.2f } } },
              { { "master", 1.5f }, { "revMix", 0.25f } } },

            // --- Bassi
            { "Basso FM", 0, { { { "1", 0.95f, 0.001f, 1.2f, 0.6f, 0.1f, 0.4f }, { "1", 0.6f, 0.001f, 0.35f, 0.25f, 0.1f, 0.8f },
                                 { "3", 0.3f, 0.001f, 0.15f, 0.0f, 0.1f }, { "1", 0.0f, 0.001f, 1.0f, 0.0f, 0.1f } } },
              { { "master", 4.5f }, { "mode", 1 }, { "glide", 0.02f }, { "tone", 6000 }, { "feedback", 0.2f } } },
            { "Basso Slap FM", 3, { { { "1", 0.95f, 0.001f, 0.8f, 0.3f, 0.08f, 0.5f }, { "1", 0.5f, 0.001f, 0.25f, 0.0f, 0.08f, 0.8f },
                                      { "7", 0.35f, 0.001f, 0.05f, 0.0f, 0.05f, 0.9f }, { "1", 0.3f, 0.001f, 0.1f, 0.0f, 0.05f } } },
              { { "master", 5.5f }, { "mode", 1 } } },
            { "Basso 80s FM", 1, { { { "1", 0.95f, 0.001f, 0.5f, 0.5f, 0.08f, 0.5f }, { "1", 0.55f, 0.001f, 0.3f, 0.3f, 0.08f, 0.7f },
                                     { "2", 0.3f, 0.001f, 0.2f, 0.0f, 0.08f }, { "0.5", 0.3f, 0.001f, 0.4f, 0.2f, 0.08f } } },
              { { "master", 5.5f }, { "mode", 1 } } },
            { "Basso Growl FM", 0, { { { "1", 0.95f, 0.002f, 1.0f, 1.0f, 0.08f }, { "1", 0.7f, 0.002f, 1.0f, 0.7f, 0.08f },
                                       { "0.5", 0.5f, 0.002f, 1.0f, 0.6f, 0.08f }, { "1", 0.4f, 0.002f, 1.0f, 0.8f, 0.08f } } },
              { { "master", 2.0f }, { "mode", 1 }, { "feedback", 0.7f }, { "lfoBright", 0.5f }, { "lfoSync", 1 }, { "lfoDiv", 3 }, { "tone", 4000 } } },
            { "Sub FM", 0, { { { "1", 1.0f, 0.003f, 1.0f, 1.0f, 0.08f, 0.2f }, { "1", 0.15f, 0.003f, 0.5f, 0.3f, 0.08f },
                               { "1", 0.0f, 0.003f, 1.0f, 0.0f, 0.08f }, { "1", 0.0f, 0.003f, 1.0f, 0.0f, 0.08f } } },
              { { "master", 1.0f }, { "mode", 1 }, { "tone", 2500 } } },

            // --- Brass, lead, pad, plucks
            { "Brass FM", 4, { { { "1", 0.85f, 0.03f, 1.0f, 0.9f, 0.25f }, { "1", 0.6f, 0.06f, 0.4f, 0.55f, 0.25f },
                                 { "1", 0.6f, 0.03f, 1.0f, 0.9f, 0.25f, 0.5f, 6.0f }, { "1", 0.55f, 0.07f, 0.4f, 0.5f, 0.25f } } },
              { { "master", -6.0f }, { "lfoVib", 0.1f }, { "lfoRate", 5.0f }, { "revMix", 0.2f }, { "chorus", 0.2f } } },
            { "Lead FM", 0, { { { "1", 0.9f, 0.003f, 1.0f, 0.9f, 0.2f }, { "1", 0.55f, 0.003f, 0.8f, 0.6f, 0.2f },
                                { "2", 0.3f, 0.003f, 0.6f, 0.4f, 0.2f }, { "1", 0.2f, 0.003f, 1.0f, 0.5f, 0.2f } } },
              { { "master", -2.0f }, { "feedback", 0.4f }, { "mode", 2 }, { "glide", 0.06f }, { "lfoVib", 0.12f }, { "lfoRate", 5.5f }, { "dlyMix", 0.25f }, { "revMix", 0.15f } } },
            { "Pad di Vetro FM", 5, { { { "1", 0.6f, 0.8f, 2.0f, 0.9f, 1.5f }, { "2", 0.4f, 1.0f, 2.0f, 0.9f, 1.5f, 0.5f, 5.0f },
                                        { "1", 0.5f, 0.9f, 2.0f, 0.9f, 1.5f, 0.5f, -6.0f }, { "3", 0.25f, 1.5f, 2.0f, 0.4f, 1.5f } } },
              { { "master", -2.5f }, { "chorus", 0.5f }, { "revMix", 0.5f }, { "revSize", 0.85f }, { "lfoVib", 0.05f } } },
            { "Pluck FM", 4, { { { "1", 0.9f, 0.001f, 0.5f, 0.0f, 0.2f }, { "2", 0.5f, 0.001f, 0.12f, 0.0f, 0.1f },
                                 { "2", 0.3f, 0.001f, 0.4f, 0.0f, 0.2f }, { "5", 0.25f, 0.001f, 0.06f, 0.0f, 0.05f } } },
              { { "master", 4.0f }, { "dlyMix", 0.3f }, { "dlyTime", 2 }, { "revMix", 0.25f } } },
            { "Metallo FX", 0, { { { "1", 0.9f, 0.001f, 1.5f, 0.0f, 0.8f }, { "1.41", 0.7f, 0.001f, 1.2f, 0.0f, 0.8f },
                                   { "3.5", 0.5f, 0.001f, 0.8f, 0.0f, 0.6f }, { "11", 0.5f, 0.001f, 0.6f, 0.0f, 0.5f } } },
              { { "master", -0.5f }, { "feedback", 0.5f }, { "revMix", 0.4f } } },
        };
        return list;
    }

    int Presets::numFactory()                    { return (int) factory().size(); }
    juce::String Presets::factoryName (int i)    { return factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)].name; }

    void Presets::loadFactory (int i)
    {
        const auto& p = factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)];
        init();
        set ("algo", (float) p.algo);
        for (int o = 0; o < numOps; ++o)
        {
            const auto& op = p.ops[(size_t) o];
            set (opId (o, "Ratio"), (float) juce::jmax (0, ratioNames().indexOf (op.ratio)));
            set (opId (o, "Level"), op.level);
            set (opId (o, "A"), op.a);
            set (opId (o, "D"), op.d);
            set (opId (o, "S"), op.s);
            set (opId (o, "R"), op.r);
            set (opId (o, "Vel"), op.vel);
            set (opId (o, "Fine"), op.fine);
        }
        for (auto& [id, value] : p.extra)
            set (id, value);
        apvts.state.setProperty ("presetName", p.name, nullptr);
    }

    juce::File Presets::userDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FMForge").getChildFile ("Presets");
    }

    juce::Array<juce::File> Presets::userPresets() const
    {
        auto files = userDir().findChildFiles (juce::File::findFiles, false, "*.fmpreset");
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
            return xml->writeTo (userDir().getChildFile (clean + ".fmpreset"));
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
FMForgeAudioProcessor::FMForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FMForge", fm::createLayout())
{
    const char* opKeys[8] = { "Ratio", "Fine", "Level", "Vel", "A", "D", "S", "R" };
    for (int o = 0; o < fm::numOps; ++o)
        for (int k = 0; k < 8; ++k)
            opPtr[(size_t) o][(size_t) k] = ptr (fm::opId (o, opKeys[k]));

    algoP = ptr ("algo"); feedbackP = ptr ("feedback"); brightP = ptr ("bright"); toneP = ptr ("tone");
    lfoWaveP = ptr ("lfoWave"); lfoRateP = ptr ("lfoRate"); lfoSyncP = ptr ("lfoSync"); lfoDivP = ptr ("lfoDiv");
    lfoVibP = ptr ("lfoVib"); lfoTremP = ptr ("lfoTrem"); lfoBrightP = ptr ("lfoBright");
    modeP = ptr ("mode"); glideP = ptr ("glide");
    chorusP = ptr ("chorus"); dlyMixP = ptr ("dlyMix"); dlyTimeP = ptr ("dlyTime"); dlyFbP = ptr ("dlyFb");
    revMixP = ptr ("revMix"); revSizeP = ptr ("revSize"); masterP = ptr ("master");

    apvts.state.setProperty ("presetName", "Init", nullptr);
}

void FMForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
}

fm::BlockParams FMForgeAudioProcessor::readParams()
{
    static constexpr double delayBeats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    fm::BlockParams p;
    for (int o = 0; o < fm::numOps; ++o)
    {
        auto& op = p.ops[(size_t) o];
        const auto& q = opPtr[(size_t) o];
        op.ratio = fm::ratioValue ((int) q[0]->load()) * std::exp2 (q[1]->load() / 1200.0f);
        op.level = q[2]->load(); op.vel = q[3]->load();
        op.a = q[4]->load(); op.d = q[5]->load(); op.s = q[6]->load(); op.r = q[7]->load();
    }
    p.algo = (int) algoP->load();
    p.feedback = feedbackP->load(); p.bright = brightP->load(); p.tone = toneP->load();
    p.lfoWave = (int) lfoWaveP->load(); p.lfoRate = lfoRateP->load(); p.lfoSync = lfoSyncP->load() > 0.5f; p.lfoDiv = (int) lfoDivP->load();
    p.lfoVib = lfoVibP->load(); p.lfoTrem = lfoTremP->load(); p.lfoBright = lfoBrightP->load();
    p.mode = (int) modeP->load(); p.glide = glideP->load();
    p.fx.chorus = chorusP->load(); p.fx.delayMix = dlyMixP->load(); p.fx.delayFeedback = dlyFbP->load();
    p.fx.delayBeats = delayBeats[juce::jlimit (0, 5, (int) dlyTimeP->load())];
    p.fx.reverbMix = revMixP->load(); p.fx.reverbSize = revSizeP->load();
    p.fx.gain = juce::Decibels::decibelsToGain (masterP->load());
    return p;
}

void FMForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    int done = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        synth.render (buffer, done, pos - done, p, bpm, ppq, playing);
        done = pos;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                                    synth.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                              synth.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())   synth.allNotesOff();
    }
    synth.render (buffer, done, buffer.getNumSamples() - done, p, bpm, ppq, playing);
}

juce::AudioProcessorEditor* FMForgeAudioProcessor::createEditor()    { return new FMForgeEditor (*this); }

void FMForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FMForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
