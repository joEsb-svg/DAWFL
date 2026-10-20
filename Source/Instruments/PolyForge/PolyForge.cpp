#include "../../Engine/Performance.h"
#include "PolyForge.h"
#include "PolyForgeEditor.h"

namespace pf
{
    //==========================================================================
    // Parametri

    static const juce::StringArray waveNames   { "Saw", "Quadra", "Triangolo", "Sinusoide" };
    static const juce::StringArray filterNames { "LP 24", "LP 12", "HP 12", "Passa-banda" };
    static const juce::StringArray lfoNames    { "Sinusoide", "Triangolo", "Saw", "Quadra", "Casuale" };
    static const juce::StringArray divNames    { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T", "1/8 ." };
    static const juce::StringArray modeNames   { "Poly", "Mono", "Legato" };
    static const juce::StringArray delayNames  { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
    static constexpr double divBeats[]   { 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 0.75 };
    static constexpr double delayBeats[] { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        // Testo dei valori, leggibile: 35%, 1.2 kHz, 250 ms, -3.0 dB, +7 ct
        auto format = [] (const juce::String& unit) -> std::function<juce::String (float, int)>
        {
            if (unit == "Hz") return [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                                              : juce::String (v, v < 10.0f ? 2 : 0) + " Hz"; };
            if (unit == "s")  return [] (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; };
            if (unit == "dB") return [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
            if (unit == "ct") return [] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct"; };
            if (unit == "bi") return [] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
            return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        };

        auto flt = [&p, format] (const char* id, const char* name, float lo, float hi, float def, float centre = -1.0f, const char* unit = "%")
        {
            NormalisableRange<float> r (lo, hi);
            if (centre > 0.0f) r.setSkewForCentre (centre);
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                                AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (format (unit))));
        };
        auto pct = [&flt] (const char* id, const char* name, float def) { flt (id, name, 0.0f, 1.0f, def); };
        auto choice = [&p] (const char* id, const char* name, const StringArray& opts, int def)
        {
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, opts, def));
        };
        auto integer = [&p] (const char* id, const char* name, int lo, int hi, int def)
        {
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
        };

        choice  (ids::osc1Wave, "Osc 1 Onda", waveNames, 0);
        flt     (ids::osc1Pw, "Osc 1 PW", 0.05f, 0.95f, 0.5f);
        integer (ids::osc1Uni, "Osc 1 Unison", 1, maxUnison, 1);
        pct     (ids::osc1Det, "Osc 1 Detune", 0.25f);
        pct     (ids::osc1Lvl, "Osc 1 Livello", 0.8f);

        choice  (ids::osc2Wave, "Osc 2 Onda", waveNames, 0);
        flt     (ids::osc2Pw, "Osc 2 PW", 0.05f, 0.95f, 0.5f);
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::osc2Semi, 1 }, "Osc 2 Semitoni", -24, 24, 0,
                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
        flt     (ids::osc2Fine, "Osc 2 Fine", -100.0f, 100.0f, 0.0f, -1.0f, "ct");
        integer (ids::osc2Uni, "Osc 2 Unison", 1, maxUnison, 1);
        pct     (ids::osc2Det, "Osc 2 Detune", 0.25f);
        pct     (ids::osc2Lvl, "Osc 2 Livello", 0.0f);

        pct (ids::subLvl, "Sub", 0.0f);
        pct (ids::noiseLvl, "Rumore", 0.0f);
        pct (ids::spread, "Stereo", 0.6f);

        choice (ids::fltType, "Filtro", filterNames, 0);
        flt (ids::cutoff, "Cutoff", 20.0f, 20000.0f, 12000.0f, 1000.0f, "Hz");
        pct (ids::reso, "Risonanza", 0.15f);
        pct (ids::fltDrive, "Drive", 0.0f);
        pct (ids::keyTrack, "Keytrack", 0.3f);
        flt (ids::fltEnv, "Inviluppo", -1.0f, 1.0f, 0.0f, -1.0f, "bi");
        pct (ids::fltVel, "Velocity filtro", 0.0f);

        flt (ids::ampA, "Attacco", 0.001f, 10.0f, 0.005f, 0.5f, "s");
        flt (ids::ampD, "Decadimento", 0.005f, 10.0f, 0.4f, 0.8f, "s");
        pct (ids::ampS, "Sustain", 1.0f);
        flt (ids::ampR, "Rilascio", 0.005f, 10.0f, 0.25f, 0.8f, "s");
        flt (ids::fltA, "Filtro A", 0.001f, 10.0f, 0.005f, 0.5f, "s");
        flt (ids::fltD, "Filtro D", 0.005f, 10.0f, 0.4f, 0.8f, "s");
        pct (ids::fltS, "Filtro S", 0.0f);
        flt (ids::fltR, "Filtro R", 0.005f, 10.0f, 0.3f, 0.8f, "s");
        pct (ids::velSens, "Velocity", 0.5f);

        choice (ids::lfoWave, "LFO Onda", lfoNames, 0);
        flt (ids::lfoRate, "LFO Velocita'", 0.05f, 20.0f, 2.0f, 2.0f, "Hz");
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::lfoSync, 1 }, "LFO a tempo", false));
        choice (ids::lfoDiv, "LFO Divisione", divNames, 2);
        pct (ids::lfoPitch, "LFO > Pitch", 0.0f);
        pct (ids::lfoCut, "LFO > Filtro", 0.0f);
        pct (ids::lfoAmp, "LFO > Volume", 0.0f);
        pct (ids::lfoPwm, "LFO > PW", 0.0f);

        choice (ids::mode, "Modo", modeNames, 0);
        flt (ids::glide, "Glide", 0.0f, 2.0f, 0.0f, 0.2f, "s");

        pct (ids::chorus, "Chorus", 0.0f);
        pct (ids::dlyMix, "Delay", 0.0f);
        choice (ids::dlyTime, "Tempo delay", delayNames, 2);
        flt (ids::dlyFb, "Feedback", 0.0f, 0.9f, 0.35f);   // percentuale
        pct (ids::revMix, "Riverbero", 0.0f);
        pct (ids::revSize, "Stanza", 0.6f);
        flt (ids::master, "Volume", -24.0f, 6.0f, 0.0f, -1.0f, "dB");

        return { p.begin(), p.end() };
    }

    //==========================================================================
    // Voce

    static inline float cents (float c) noexcept     { return std::exp2 (c / 1200.0f); }
    static inline float mtof (float m) noexcept      { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }

    void Voice::start (int midiNote, float vel, float glideFrom, bool legato, uint32_t seed)
    {
        note = midiNote;
        velocity = vel;
        targetFreq = mtof ((float) midiNote);
        freq = glideFrom > 0.0f ? glideFrom : targetFreq;

        if (! legato || ! active)
        {
            // Fasi casuali per l'unison (suono largo e vivo), sub a zero (attacco pulito)
            uint32_t s = seed;
            for (auto& o : phase)
                for (auto& ph : o)
                {
                    s = s * 1664525u + 1013904223u;
                    ph = (float) (s >> 8) / 16777216.0f;
                }
            subPhase = 0.0f;
            if (! active)
            {
                for (auto& f : filtersL) f.reset();
                for (auto& f : filtersR) f.reset();
            }
            amp.noteOn (false);
            flt.noteOn (false);
        }

        active = true;
        counter = 0;
    }

    void Voice::setTarget (int midiNote, float)
    {
        note = midiNote;
        targetFreq = mtof ((float) midiNote);
    }

    void Voice::render (float* L, float* R, int n, const BlockParams& p, float lfo, float glideCoef)
    {
        if (! active)
            return;

        amp.set (sr, p.ampA, p.ampD, p.ampS, p.ampR);
        flt.set (sr, p.fltA, p.fltD, p.fltS, p.fltR);

        // Glide (in scala logaritmica: suona naturale su ogni intervallo)
        freq = targetFreq * std::pow (freq / targetFreq, glideCoef);

        // Coefficienti del blocco (aggiornati ogni 16 campioni dal chiamante)
        const float vib = std::exp2 (lfo * p.lfoPitch * 0.5f / 12.0f);   // vibrato fino a mezzo tono
        const float f0 = freq * vib;
        const float invSr = (float) (1.0 / sr);

        float dt[2][maxUnison], panL[2][maxUnison], panR[2][maxUnison], gain[2];
        int uni[2];
        for (int o = 0; o < 2; ++o)
        {
            uni[o] = juce::jlimit (1, perf::lowPower() ? perf::lowPowerUnison : maxUnison, p.unison[o]);
            gain[o] = p.level[o] / std::sqrt ((float) uni[o]);
            const float base = (o == 0 ? f0 : f0 * p.osc2Ratio) * invSr;
            for (int u = 0; u < uni[o]; ++u)
            {
                const float pos = uni[o] == 1 ? 0.0f : -1.0f + 2.0f * (float) u / (float) (uni[o] - 1);
                dt[o][u] = juce::jmin (0.45f, base * cents (p.detune[o] * 45.0f * pos));
                const float pan = pos * p.spread;
                panL[o][u] = std::sqrt (0.5f * (1.0f - pan));
                panR[o][u] = std::sqrt (0.5f * (1.0f + pan));
            }
        }
        const float pw0 = juce::jlimit (0.05f, 0.95f, p.pw[0] + lfo * p.lfoPwm * 0.4f);
        const float pw1 = juce::jlimit (0.05f, 0.95f, p.pw[1] + lfo * p.lfoPwm * 0.4f);
        const float subDt = f0 * 0.5f * invSr;

        // Filtro: cutoff modulato da inviluppo, LFO, nota (keytrack) e velocity
        const float octaves = p.fltEnv * flt.value * 7.0f + lfo * p.lfoCut * 3.0f
                            + p.keyTrack * ((float) note - 60.0f) / 12.0f + p.fltVel * (velocity - 1.0f) * 3.0f;
        const float cutoff = juce::jlimit (20.0f, (float) sr * 0.45f, p.cutoff * std::exp2 (octaves));
        const float g = std::tan (juce::MathConstants<float>::pi * cutoff * invSr);
        const float kRes = 2.0f - 1.94f * p.reso;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& fs = ch == 0 ? filtersL : filtersR;
            fs[0].setCoefficients (g, kRes);
            fs[1].setCoefficients (g, 1.3f);
        }

        const float driveGain = 1.0f + p.drive * 5.0f;
        const float driveNorm = 1.0f / std::tanh (driveGain);
        const float velGain = 1.0f - p.velSens * (1.0f - velocity);
        const float trem = 1.0f - p.lfoAmp * (0.5f + 0.5f * lfo);
        const int type = p.filterType;
        const float pws[2] { pw0, pw1 };

        for (int i = 0; i < n; ++i)
        {
            const float a = amp.next();
            flt.next();
            if (amp.stage == Adsr::idle)
            {
                active = false;
                return;
            }

            float l = 0.0f, r = 0.0f;
            for (int o = 0; o < 2; ++o)
            {
                if (gain[o] <= 0.0001f)
                    continue;

                const int w = p.wave[o];
                for (int u = 0; u < uni[o]; ++u)
                {
                    float& t = phase[(size_t) o][(size_t) u];
                    const float x = osc (w, t, dt[o][u], pws[o]) * gain[o];
                    l += x * panL[o][u];
                    r += x * panR[o][u];
                    t += dt[o][u];
                    if (t >= 1.0f) t -= 1.0f;
                }
            }

            if (p.sub > 0.0001f)
            {
                const float s = std::sin (juce::MathConstants<float>::twoPi * subPhase) * p.sub * 0.8f;
                l += s; r += s;
                subPhase += subDt;
                if (subPhase >= 1.0f) subPhase -= 1.0f;
            }

            if (p.noise > 0.0001f)
            {
                noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                const float nz = ((float) noiseState * (2.0f / 4294967295.0f) - 1.0f) * p.noise * 0.5f;
                l += nz; r += nz;
            }

            if (p.drive > 0.001f)
            {
                l = std::tanh (l * driveGain) * driveNorm;
                r = std::tanh (r * driveGain) * driveNorm;
            }

            const int stageType = type == 0 || type == 1 ? 0 : type;
            l = filtersL[0].process (l, stageType);
            r = filtersR[0].process (r, stageType);
            if (type == 0)
            {
                l = filtersL[1].process (l, 0);
                r = filtersR[1].process (r, 0);
            }

            const float out = a * velGain * trem * 0.3f;
            L[i] += l * out;
            R[i] += r * out;
        }
    }

    //==========================================================================
    // Synth

    void Synth::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices)
            v.prepare (sr);
        heldNotes.clear(); heldNotes.reserve (128);
        velocities.clear(); velocities.reserve (128);

        chorusL.assign (4096, 0.0f); chorusR.assign (4096, 0.0f);
        delayL.assign ((size_t) (sr * 3.0) + 1, 0.0f); delayR.assign ((size_t) (sr * 3.0) + 1, 0.0f);
        chorusPos = delayPos = 0;
        delayLp[0] = delayLp[1] = 0.0f;
        reverb.setSampleRate (sr);
        reverb.reset();
        scratch.setSize (2, juce::jmax (512, maxBlock));
    }

    Voice* Synth::findVoice()
    {
        // In "Risparmio" si usano solo le prime voci
        const size_t limit = perf::lowPower() ? (size_t) perf::lowPowerVoices : voices.size();
        Voice* best = nullptr;
        for (size_t i = 0; i < limit; ++i)   // prima una voce libera
            if (! voices[i].isActive())
                return &voices[i];
        for (size_t i = 0; i < limit; ++i)   // poi la piu' vecchia gia' rilasciata
            if (voices[i].isReleasing() && (best == nullptr || voices[i].age < best->age))
                best = &voices[i];
        if (best != nullptr)
            return best;
        for (size_t i = 0; i < limit; ++i)   // infine la piu' vecchia
            if (best == nullptr || voices[i].age < best->age)
                best = &voices[i];
        return best;
    }

    void Synth::noteOn (int note, float velocity)
    {
        seed = seed * 1103515245u + 12345u;
        heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
        heldNotes.push_back (note);
        noteVelocity = velocity;

        if (currentMode == 0)
        {
            // Poly: scivola dall'ultima nota suonata se c'e' il glide
            float from = 0.0f;
            if (currentGlide > 0.0001f && lastFreq > 0.0f)
                from = lastFreq;

            auto* v = findVoice();
            v->age = ++ageCounter;
            v->start (note, velocity, from, false, seed);
            lastFreq = v->getFrequency() > 0.0f ? mtof ((float) note) : lastFreq;
            return;
        }

        // Mono / Legato: una sola voce
        auto& v = voices[0];
        const bool sounding = v.isActive() && ! v.isReleasing();
        if (sounding)
        {
            v.setTarget (note, currentGlide);
            if (currentMode == 1)
                v.start (note, velocity, v.getFrequency(), false, seed);   // Mono: riattacca l'inviluppo
        }
        else
        {
            v.start (note, velocity, currentGlide > 0.0001f && lastFreq > 0.0f ? lastFreq : 0.0f, false, seed);
        }
        if (currentGlide <= 0.0001f)
            v.setTarget (note, 0.0f);
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
            v.setTarget (heldNotes.back(), currentGlide);   // torna alla nota ancora premuta
            lastFreq = mtof ((float) heldNotes.back());
        }
        else
            v.stop();
    }

    void Synth::allNotesOff()
    {
        heldNotes.clear();
        for (auto& v : voices)
            if (v.isActive())
                v.stop();
    }

    void Synth::killAll()
    {
        heldNotes.clear();
        for (auto& v : voices)
            v.kill();
        std::fill (chorusL.begin(), chorusL.end(), 0.0f); std::fill (chorusR.begin(), chorusR.end(), 0.0f);
        std::fill (delayL.begin(), delayL.end(), 0.0f);   std::fill (delayR.begin(), delayR.end(), 0.0f);
        delayLp[0] = delayLp[1] = 0.0f;
        reverb.reset();
    }

    float Synth::lfoValue (int wave, float t)
    {
        switch (wave)
        {
            case 0:  return std::sin (juce::MathConstants<float>::twoPi * t);
            case 1:  return 1.0f - 4.0f * std::abs (t - 0.5f);
            case 2:  return 1.0f - 2.0f * t;
            case 3:  return t < 0.5f ? 1.0f : -1.0f;
            default: return lfoHold;
        }
    }

    void Synth::render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p,
                        double bpm, double ppq, bool playing)
    {
        if (n <= 0)
            return;

        // Modo e glide: se si passa da Poly a Mono si fermano le voci in piu'
        if (p.mode != currentMode)
        {
            for (int i = 1; i < maxVoices; ++i) voices[(size_t) i].stop();
            currentMode = p.mode;
        }
        currentGlide = p.glide;

        auto* L = buffer.getWritePointer (0, start);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : L;

        // LFO: a tempo (agganciato alla posizione della canzone) oppure libero
        const double beats = divBeats[juce::jlimit (0, 7, p.lfoDiv)];
        const double lfoHz = p.lfoSync ? (bpm / 60.0) / beats : (double) p.lfoRate;
        if (p.lfoSync && playing)
            lfoPhase = std::fmod (ppq / beats, 1.0);

        const float glideCoef = p.glide > 0.0001f ? (float) std::exp (-1.0 / (p.glide * 0.35 * sr / controlRate)) : 0.0f;

        for (int pos = 0; pos < n; pos += controlRate)
        {
            const int m = juce::jmin (controlRate, n - pos);
            const float prev = (float) lfoPhase;
            lfoPhase += lfoHz * m / sr;
            if (lfoPhase >= 1.0)
            {
                lfoPhase -= std::floor (lfoPhase);
                lfoRandom = lfoRandom * 1664525u + 1013904223u;
                lfoHold = (float) (lfoRandom >> 8) / 8388608.0f - 1.0f;
            }
            juce::ignoreUnused (prev);
            const float lfo = lfoValue (p.lfoWave, (float) lfoPhase);

            for (auto& v : voices)
                if (v.isActive())
                    v.render (L + pos, R + pos, m, p, lfo, glideCoef);
        }

        // ---- Effetti
        if (p.chorus > 0.001f)
        {
            const int size = (int) chorusL.size();
            for (int i = 0; i < n; ++i)
            {
                chorusL[(size_t) chorusPos] = L[i];
                chorusR[(size_t) chorusPos] = R[i];
                chorusPhase += 0.45 / sr;
                if (chorusPhase >= 1.0) chorusPhase -= 1.0;
                const float modL = (float) std::sin (juce::MathConstants<double>::twoPi * chorusPhase);
                const float modR = (float) std::sin (juce::MathConstants<double>::twoPi * (chorusPhase + 0.25));
                auto read = [&] (const std::vector<float>& buf, float delaySamples)
                {
                    float idx = (float) chorusPos - delaySamples;
                    while (idx < 0) idx += (float) size;
                    const int i0 = (int) idx;
                    const float fr = idx - (float) i0;
                    return buf[(size_t) i0] + fr * (buf[(size_t) ((i0 + 1) % size)] - buf[(size_t) i0]);
                };
                const float base = (float) (0.012 * sr), depth = (float) (0.0035 * sr);
                const float wl = read (chorusL, base + depth * modL);
                const float wr = read (chorusR, base + depth * modR);
                L[i] = L[i] * (1.0f - 0.35f * p.chorus) + wr * 0.7f * p.chorus;
                R[i] = R[i] * (1.0f - 0.35f * p.chorus) + wl * 0.7f * p.chorus;
                chorusPos = (chorusPos + 1) % size;
            }
        }

        if (p.dlyMix > 0.001f)
        {
            const int size = (int) delayL.size();
            const int d = juce::jlimit (1, size - 1, (int) (delayBeats[juce::jlimit (0, 5, p.dlyTime)] * 60.0 / juce::jmax (40.0, bpm) * sr));
            for (int i = 0; i < n; ++i)
            {
                const int rd = (delayPos - d + size) % size;
                const float dl = delayL[(size_t) rd], dr = delayR[(size_t) rd];
                delayLp[0] += 0.3f * (dr - delayLp[0]);
                delayLp[1] += 0.3f * (dl - delayLp[1]);
                delayL[(size_t) delayPos] = (L[i] + R[i]) * 0.5f + delayLp[0] * p.dlyFb;   // ping-pong
                delayR[(size_t) delayPos] = delayLp[1] * p.dlyFb;
                delayPos = (delayPos + 1) % size;
                L[i] += dl * p.dlyMix;
                R[i] += dr * p.dlyMix;
            }
        }

        if (p.revMix > 0.001f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.3f + 0.68f * p.revSize;
            rp.damping = 0.45f;
            rp.width = 1.0f;
            rp.wetLevel = p.revMix * 0.6f;
            rp.dryLevel = 1.0f - p.revMix * 0.35f;
            if (rp.roomSize != lastRev.roomSize || rp.wetLevel != lastRev.wetLevel)
            {
                reverb.setParameters (rp);
                lastRev = rp;
            }
            reverb.processStereo (L, R, n);
        }

        if (std::abs (p.master - 1.0f) > 0.0001f)
            for (int i = 0; i < n; ++i) { L[i] *= p.master; R[i] *= p.master; }
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

    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    static const std::vector<FactoryPreset>& factory()
    {
        using namespace ids;
        // Valori "reali": onde/filtri/divisioni come indice (vedi le liste in alto), tempi in secondi, cutoff in Hz
        static const std::vector<FactoryPreset> list =
        {
            { "Init", {} },

            // --- Pad
            { "Pad Caldo", { { master, -2.5f }, { osc1Uni, 5 }, { osc1Det, 0.35f }, { osc2Wave, 0 }, { osc2Fine, 7 }, { osc2Uni, 3 }, { osc2Det, 0.3f }, { osc2Lvl, 0.6f },
                             { spread, 0.85f }, { cutoff, 1800 }, { reso, 0.15f }, { keyTrack, 0.3f }, { fltEnv, 0.25f }, { fltA, 1.2f }, { fltD, 2.0f },
                             { fltS, 0.4f }, { fltR, 1.5f }, { ampA, 0.8f }, { ampD, 1.0f }, { ampS, 0.9f }, { ampR, 1.6f }, { chorus, 0.5f },
                             { revMix, 0.35f }, { revSize, 0.8f }, { velSens, 0.3f }, { lfoCut, 0.08f }, { lfoRate, 0.3f } } },
            { "Pad Archi", { { master, -1.5f }, { osc1Uni, 7 }, { osc1Det, 0.3f }, { osc2Semi, 12 }, { osc2Lvl, 0.3f }, { osc2Uni, 3 }, { cutoff, 3500 }, { reso, 0.1f },
                             { ampA, 0.45f }, { ampD, 1.0f }, { ampS, 0.9f }, { ampR, 1.2f }, { fltEnv, 0.1f }, { fltA, 0.5f }, { fltS, 0.5f },
                             { lfoPitch, 0.08f }, { lfoRate, 5.0f }, { chorus, 0.4f }, { revMix, 0.35f }, { revSize, 0.75f }, { spread, 0.8f } } },
            { "Pad Scuro", { { master, -3.5f }, { osc1Uni, 5 }, { osc1Det, 0.25f }, { osc2Wave, 1 }, { osc2Semi, -12 }, { osc2Lvl, 0.5f }, { subLvl, 0.3f },
                             { cutoff, 600 }, { reso, 0.35f }, { lfoCut, 0.25f }, { lfoRate, 0.12f }, { ampA, 1.2f }, { ampS, 1.0f }, { ampR, 2.0f },
                             { revMix, 0.45f }, { revSize, 0.9f }, { chorus, 0.3f } } },
            { "Pad Vetro", { { master, -3.5f }, { osc1Wave, 2 }, { osc1Uni, 3 }, { osc1Det, 0.15f }, { osc2Wave, 3 }, { osc2Semi, 19 }, { osc2Lvl, 0.4f }, { cutoff, 9000 },
                             { ampA, 0.6f }, { ampR, 2.0f }, { lfoAmp, 0.15f }, { lfoRate, 4.0f }, { chorus, 0.5f }, { revMix, 0.5f }, { revSize, 0.85f },
                             { dlyMix, 0.2f }, { dlyTime, 3 } } },
            { "Pad Evolutivo", { { osc1Uni, 5 }, { osc2Wave, 1 }, { osc2Semi, 7 }, { osc2Lvl, 0.5f }, { osc2Pw, 0.3f }, { lfoPwm, 0.5f },
                                 { lfoCut, 0.4f }, { lfoSync, 1 }, { lfoDiv, 0 }, { cutoff, 1200 }, { reso, 0.3f }, { ampA, 1.5f }, { ampR, 2.5f },
                                 { revMix, 0.4f }, { revSize, 0.85f }, { chorus, 0.4f } } },

            // --- Lead
            { "Supersaw Lead", { { master, -4.0f }, { osc1Uni, 7 }, { osc1Det, 0.45f }, { osc1Lvl, 0.9f }, { osc2Semi, 12 }, { osc2Uni, 5 }, { osc2Det, 0.4f },
                                 { osc2Lvl, 0.45f }, { spread, 0.9f }, { cutoff, 9000 }, { reso, 0.1f }, { ampA, 0.005f }, { ampD, 0.3f },
                                 { ampS, 0.85f }, { ampR, 0.35f }, { chorus, 0.2f }, { dlyMix, 0.25f }, { dlyTime, 2 }, { revMix, 0.2f }, { velSens, 0.3f } } },
            { "Lead Saw Mono", { { mode, 1 }, { glide, 0.08f }, { osc1Uni, 2 }, { osc1Det, 0.1f }, { osc2Fine, 8 }, { osc2Lvl, 0.7f },
                                 { cutoff, 4500 }, { reso, 0.25f }, { fltEnv, 0.3f }, { fltD, 0.4f }, { fltS, 0.5f }, { lfoPitch, 0.1f },
                                 { lfoRate, 5.5f }, { dlyMix, 0.2f }, { revMix, 0.15f } } },
            { "Lead Quadra", { { master, 2.5f }, { mode, 1 }, { glide, 0.03f }, { osc1Wave, 1 }, { osc2Wave, 1 }, { osc2Pw, 0.25f }, { osc2Semi, 12 }, { osc2Lvl, 0.4f },
                               { cutoff, 7000 }, { lfoPitch, 0.1f }, { lfoRate, 6.0f }, { dlyMix, 0.25f }, { dlyTime, 1 } } },
            { "Lead Morbido", { { master, 2.0f }, { mode, 2 }, { glide, 0.12f }, { osc1Wave, 2 }, { osc2Wave, 3 }, { osc2Semi, 12 }, { osc2Lvl, 0.3f },
                                { cutoff, 5000 }, { ampA, 0.03f }, { ampR, 0.4f }, { lfoPitch, 0.12f }, { lfoRate, 5.0f }, { revMix, 0.25f },
                                { dlyMix, 0.2f }, { dlyTime, 3 } } },
            { "Lead Acido", { { mode, 1 }, { glide, 0.06f }, { cutoff, 350 }, { reso, 0.8f }, { fltDrive, 0.5f }, { fltEnv, 0.7f }, { fltA, 0.001f },
                              { fltD, 0.2f }, { fltS, 0.0f }, { ampA, 0.001f }, { ampD, 0.3f }, { ampS, 0.8f }, { ampR, 0.1f }, { keyTrack, 0.4f },
                              { dlyMix, 0.2f }, { dlyTime, 2 } } },

            // --- Pluck
            { "Pluck EDM", { { master, 6.0f }, { osc1Uni, 5 }, { osc1Det, 0.3f }, { osc2Semi, 12 }, { osc2Uni, 3 }, { osc2Lvl, 0.4f }, { cutoff, 300 }, { reso, 0.2f },
                             { fltEnv, 0.75f }, { fltA, 0.001f }, { fltD, 0.25f }, { fltS, 0.0f }, { fltR, 0.2f }, { ampA, 0.001f }, { ampD, 0.5f },
                             { ampS, 0.0f }, { ampR, 0.3f }, { dlyMix, 0.3f }, { dlyTime, 2 }, { dlyFb, 0.4f }, { revMix, 0.25f }, { spread, 0.8f } } },
            { "Pluck House", { { master, 4.0f }, { osc1Wave, 1 }, { osc1Pw, 0.4f }, { osc1Uni, 3 }, { osc1Det, 0.15f }, { osc2Lvl, 0.5f }, { cutoff, 500 }, { reso, 0.25f },
                               { fltEnv, 0.6f }, { fltA, 0.001f }, { fltD, 0.18f }, { fltS, 0.0f }, { ampA, 0.001f }, { ampD, 0.35f }, { ampS, 0.0f },
                               { ampR, 0.2f }, { revMix, 0.2f }, { chorus, 0.25f } } },
            { "Pluck Mallet", { { master, 6.0f }, { osc1Wave, 3 }, { osc2Wave, 2 }, { osc2Semi, 24 }, { osc2Lvl, 0.25f }, { cutoff, 6000 }, { ampA, 0.001f },
                                { ampD, 0.45f }, { ampS, 0.0f }, { ampR, 0.3f }, { revMix, 0.2f } } },
            { "Arpeggio PWM", { { master, 4.5f }, { osc1Wave, 1 }, { lfoPwm, 0.6f }, { lfoRate, 3.0f }, { ampA, 0.001f }, { ampD, 0.3f }, { ampS, 0.3f }, { ampR, 0.15f },
                                { cutoff, 3000 }, { fltEnv, 0.3f }, { fltD, 0.2f }, { dlyMix, 0.3f }, { dlyTime, 1 }, { revMix, 0.15f } } },

            // --- Stab e accordi
            { "Stab Rave", { { master, -2.5f }, { osc1Uni, 7 }, { osc1Det, 0.5f }, { osc1Lvl, 1.0f }, { osc2Wave, 1 }, { osc2Semi, -12 }, { osc2Lvl, 0.4f },
                             { cutoff, 2500 }, { reso, 0.2f }, { fltEnv, 0.4f }, { fltD, 0.3f }, { ampA, 0.001f }, { ampD, 0.5f }, { ampS, 0.2f },
                             { ampR, 0.2f }, { fltDrive, 0.4f }, { revMix, 0.25f } } },
            { "Stab Organo House", { { master, -2.0f }, { osc1Wave, 3 }, { osc2Wave, 3 }, { osc2Semi, 12 }, { osc2Lvl, 0.6f }, { subLvl, 0.25f }, { ampA, 0.002f },
                                     { ampD, 0.35f }, { ampS, 0.3f }, { ampR, 0.15f }, { fltDrive, 0.3f }, { cutoff, 8000 }, { revMix, 0.15f } } },
            { "Accordi Dub", { { master, 4.0f }, { osc1Uni, 3 }, { osc1Det, 0.2f }, { osc2Fine, -8 }, { osc2Lvl, 0.7f }, { cutoff, 900 }, { reso, 0.3f }, { fltEnv, 0.3f },
                               { fltD, 0.15f }, { fltS, 0.0f }, { ampA, 0.001f }, { ampD, 0.25f }, { ampS, 0.0f }, { ampR, 0.2f }, { dlyMix, 0.45f },
                               { dlyTime, 2 }, { dlyFb, 0.6f }, { revMix, 0.35f }, { revSize, 0.8f } } },
            { "Synth Brass", { { master, -3.5f }, { osc1Uni, 3 }, { osc1Det, 0.25f }, { osc2Fine, 6 }, { osc2Lvl, 0.7f }, { cutoff, 900 }, { fltEnv, 0.45f }, { fltA, 0.08f },
                               { fltD, 0.5f }, { fltS, 0.5f }, { ampA, 0.04f }, { ampD, 0.5f }, { ampS, 0.85f }, { ampR, 0.3f }, { lfoPitch, 0.05f },
                               { lfoRate, 5.0f }, { revMix, 0.2f }, { chorus, 0.25f } } },
            { "Polysynth 80s", { { master, -7.0f }, { osc1Wave, 1 }, { osc1Pw, 0.35f }, { osc1Uni, 2 }, { osc1Det, 0.15f }, { osc2Fine, 10 }, { osc2Lvl, 0.7f },
                                 { cutoff, 2500 }, { reso, 0.25f }, { fltEnv, 0.3f }, { fltD, 0.6f }, { fltS, 0.4f }, { lfoPwm, 0.3f },
                                 { lfoRate, 0.8f }, { chorus, 0.6f }, { revMix, 0.3f }, { ampA, 0.01f }, { ampR, 0.5f } } },
            { "Trance Gate", { { master, -0.5f }, { osc1Uni, 7 }, { osc1Det, 0.35f }, { osc2Semi, 12 }, { osc2Lvl, 0.35f }, { cutoff, 3000 }, { lfoWave, 3 },
                               { lfoSync, 1 }, { lfoDiv, 4 }, { lfoAmp, 1.0f }, { ampA, 0.01f }, { ampR, 0.3f }, { revMix, 0.25f },
                               { dlyMix, 0.2f }, { dlyTime, 2 }, { spread, 0.9f } } },

            // --- Bassi e altro
            { "Basso Poly", { { master, 4.5f }, { mode, 1 }, { osc2Wave, 1 }, { osc2Semi, -12 }, { osc2Lvl, 0.6f }, { subLvl, 0.4f }, { cutoff, 700 }, { reso, 0.25f },
                              { fltEnv, 0.4f }, { fltD, 0.25f }, { fltS, 0.2f }, { ampA, 0.002f }, { ampD, 0.4f }, { ampS, 0.8f }, { ampR, 0.1f },
                              { keyTrack, 0.5f }, { spread, 0.0f } } },
            { "Sub Morbido", { { master, 4.5f }, { mode, 1 }, { osc1Wave, 3 }, { osc1Lvl, 1.0f }, { osc2Wave, 2 }, { osc2Semi, 12 }, { osc2Lvl, 0.15f }, { cutoff, 2000 },
                               { ampA, 0.005f }, { ampR, 0.15f }, { spread, 0.0f } } },
            { "Campanelli", { { osc1Wave, 3 }, { osc2Wave, 2 }, { osc2Semi, 24 }, { osc2Lvl, 0.35f }, { cutoff, 12000 }, { ampA, 0.001f }, { ampD, 2.5f },
                              { ampS, 0.0f }, { ampR, 1.5f }, { revMix, 0.4f }, { revSize, 0.8f }, { dlyMix, 0.2f }, { dlyTime, 3 } } },
        };
        return list;
    }

    int Presets::numFactory()                       { return (int) factory().size(); }
    juce::String Presets::factoryName (int i)       { return factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)].name; }

    void Presets::loadFactory (int i)
    {
        const auto& preset = factory()[(size_t) juce::jlimit (0, numFactory() - 1, i)];
        init();
        for (auto& [id, value] : preset.values)
            set (id, value);
        apvts.state.setProperty ("presetName", preset.name, nullptr);
    }

    juce::File Presets::userDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PolyForge").getChildFile ("Presets");
    }

    juce::Array<juce::File> Presets::userPresets() const
    {
        auto files = userDir().findChildFiles (juce::File::findFiles, false, "*.pfpreset");
        files.sort();
        return files;
    }

    bool Presets::save (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
            return false;
        userDir().createDirectory();
        apvts.state.setProperty ("presetName", clean, nullptr);
        if (auto xml = apvts.copyState().createXml())
            return xml->writeTo (userDir().getChildFile (clean + ".pfpreset"));
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
PolyForgeAudioProcessor::PolyForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PolyForge", pf::createLayout())
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            params[r->getParameterID()] = apvts.getRawParameterValue (r->getParameterID());

    apvts.state.setProperty ("presetName", "Init", nullptr);
}

bool PolyForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void PolyForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
}

pf::BlockParams PolyForgeAudioProcessor::readParams()
{
    using namespace pf::ids;
    pf::BlockParams p;
    p.wave[0] = (int) raw (osc1Wave);   p.wave[1] = (int) raw (osc2Wave);
    p.pw[0] = raw (osc1Pw);             p.pw[1] = raw (osc2Pw);
    p.unison[0] = (int) raw (osc1Uni);  p.unison[1] = (int) raw (osc2Uni);
    p.detune[0] = raw (osc1Det);        p.detune[1] = raw (osc2Det);
    p.level[0] = raw (osc1Lvl);         p.level[1] = raw (osc2Lvl);
    p.osc2Ratio = std::exp2 ((raw (osc2Semi) + raw (osc2Fine) / 100.0f) / 12.0f);
    p.sub = raw (subLvl); p.noise = raw (noiseLvl); p.spread = raw (spread);
    p.filterType = (int) raw (fltType);
    p.cutoff = raw (cutoff); p.reso = raw (reso); p.drive = raw (fltDrive);
    p.keyTrack = raw (keyTrack); p.fltEnv = raw (fltEnv); p.fltVel = raw (fltVel);
    p.ampA = raw (ampA); p.ampD = raw (ampD); p.ampS = raw (ampS); p.ampR = raw (ampR);
    p.fltA = raw (fltA); p.fltD = raw (fltD); p.fltS = raw (fltS); p.fltR = raw (fltR);
    p.velSens = raw (velSens);
    p.lfoWave = (int) raw (lfoWave); p.lfoRate = raw (lfoRate); p.lfoSync = raw (lfoSync) > 0.5f; p.lfoDiv = (int) raw (lfoDiv);
    p.lfoPitch = raw (lfoPitch); p.lfoCut = raw (lfoCut); p.lfoAmp = raw (lfoAmp); p.lfoPwm = raw (lfoPwm);
    p.mode = (int) raw (mode); p.glide = raw (glide);
    p.chorus = raw (chorus); p.dlyMix = raw (dlyMix); p.dlyTime = (int) raw (dlyTime); p.dlyFb = raw (dlyFb);
    p.revMix = raw (revMix); p.revSize = raw (revSize);
    p.master = juce::Decibels::decibelsToGain (raw (master));
    return p;
}

void PolyForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
        synth.render (buffer, done, pos - done, p, bpm, ppq + (double) done / getSampleRate() * bpm / 60.0, playing);
        done = pos;

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                       synth.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                 synth.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())  synth.allNotesOff();
    }

    synth.render (buffer, done, buffer.getNumSamples() - done, p, bpm, ppq + (double) done / getSampleRate() * bpm / 60.0, playing);
}

juce::AudioProcessorEditor* PolyForgeAudioProcessor::createEditor()
{
    return new PolyForgeEditor (*this);
}

void PolyForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PolyForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
