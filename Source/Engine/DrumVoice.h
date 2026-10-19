#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

//==============================================================================
/** Generatore di rumore bianco velocissimo e sicuro per il thread audio. */
struct FastNoise
{
    uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) state * (2.0f / 4294967295.0f) - 1.0f;
    }
};

//==============================================================================
/** Filtro State-Variable (topologia TPT): passa-basso, passa-banda, passa-alto. */
struct SVFilter
{
    void set (double sampleRate, double cutoff, double q) noexcept
    {
        const double g = std::tan (juce::MathConstants<double>::pi
                                   * juce::jlimit (20.0, sampleRate * 0.45, cutoff) / sampleRate);
        k  = (float) (1.0 / q);
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) g * a1;
        a3 = (float) g * a2;
    }

    void reset() noexcept   { ic1 = ic2 = 0.0f; }

    void process (float in, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = in - k * v1 - v2;
    }

    float highPass (float in) noexcept  { float l, b, h; process (in, l, b, h); return h; }
    float bandPass (float in) noexcept  { float l, b, h; process (in, l, b, h); return b; }

    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
enum class DrumType { kick, snare, clap, closedHat, openHat, tom };

//==============================================================================
/**
    Una voce di batteria sintetizzata (nessun campione audio: tutto calcolato).
    Tutto qui dentro e' sicuro per il thread audio: niente allocazioni.
*/
class DrumVoice
{
public:
    void prepare (double newSampleRate, DrumType newType)
    {
        sr = newSampleRate;
        type = newType;
        active = false;

        c_0_002 = coef (0.002);
        c_0_003 = coef (0.003);
        c_0_004 = coef (0.004);
        c_0_0045 = coef (0.0045);
        c_0_02 = coef (0.02);
        c_0_032 = coef (0.032);
        c_0_055 = coef (0.055);
        c_0_06 = coef (0.06);
        c_0_11 = coef (0.11);
        c_0_13 = coef (0.13);
        c_0_16 = coef (0.16);
        c_kickAmp = coef (0.17);
        hatDecay = coef (type == DrumType::openHat ? 0.32 : 0.045);

        switch (type)
        {
            case DrumType::snare:     filterA.set (sr, 1800.0, 0.7);  break;
            case DrumType::clap:      filterA.set (sr, 1150.0, 1.4);  break;
            case DrumType::closedHat:
            case DrumType::openHat:   filterA.set (sr, 7200.0, 0.8);
                                      filterB.set (sr, 10000.0, 0.9); break;
            case DrumType::kick:
            case DrumType::tom:       filterA.set (sr, 1200.0, 0.7);  break;
        }
    }

    void trigger (float velocity)
    {
        active    = true;
        level     = velocity;
        sampleCount = 0;
        phase     = 0.0;
        pitchEnv  = 1.0f;
        ampEnv    = 1.0f;
        clickEnv  = 1.0f;
        noiseEnv  = 1.0f;
        chokeGain = 1.0f;
        choking   = false;
        clapTailStarted = false;
        filterA.reset();
        filterB.reset();

        for (auto& p : hatPhases)
            p = 0.0;
    }

    /** Sfuma velocemente la voce (es. l'hi-hat chiuso "strozza" quello aperto). */
    void choke()                    { if (active) choking = true; }

    bool isActive() const noexcept  { return active; }

    float render() noexcept
    {
        if (! active)
            return 0.0f;

        float out = 0.0f;

        switch (type)
        {
            case DrumType::kick:      out = renderKick();  break;
            case DrumType::snare:     out = renderSnare(); break;
            case DrumType::clap:      out = renderClap();  break;
            case DrumType::closedHat: out = renderHat(); break;
            case DrumType::openHat:   out = renderHat();  break;
            case DrumType::tom:       out = renderTom();   break;
        }

        if (choking)
        {
            chokeGain *= c_0_004;
            if (chokeGain < 0.0005f)
                active = false;
        }

        ++sampleCount;
        return out * level * chokeGain;
    }

private:
    /** Coefficiente per un decadimento esponenziale con costante di tempo "tau" secondi. */
    float coef (double tau) const noexcept    { return (float) std::exp (-1.0 / (tau * sr)); }

    double seconds() const noexcept           { return (double) sampleCount / sr; }

    float sine (double freq) noexcept
    {
        const float v = (float) std::sin (phase);
        phase += juce::MathConstants<double>::twoPi * freq / sr;
        if (phase > juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
        return v;
    }

    void stopWhenSilent (float env) noexcept
    {
        if (env < 0.0003f)
            active = false;
    }

    //==============================================================================
    float renderKick() noexcept
    {
        // Corpo: sinusoide che scende da ~190 Hz a 48 Hz. La saturazione aggiunge armoniche
        // che si sentono anche dagli altoparlanti del telefono (che non riproducono i 48 Hz).
        const double freq = 50.0 + 150.0 * pitchEnv;
        pitchEnv *= c_0_032;

        float body = sine (freq) * ampEnv;
        ampEnv *= c_kickAmp;

        // Click d'attacco: un brevissimo colpo di rumore filtrato.
        float click = filterA.highPass (noise.next()) * clickEnv * 0.5f;
        clickEnv *= c_0_003;

        stopWhenSilent (ampEnv);
        return std::tanh (4.0f * (body + click)) * 0.9f;
    }

    float renderSnare() noexcept
    {
        const double freq = 185.0 + 70.0 * pitchEnv;
        pitchEnv *= c_0_02;

        const float tone = sine (freq) * ampEnv;
        ampEnv *= c_0_055;

        const float rattle = filterA.highPass (noise.next()) * noiseEnv;
        noiseEnv *= c_0_13;

        stopWhenSilent (noiseEnv);
        return std::tanh (1.6f * (0.7f * tone + 0.85f * rattle));
    }

    float renderClap() noexcept
    {
        // Tre "battiti" ravvicinati (10 ms) e poi una coda: e' il trucco del clap analogico.
        const double t = seconds();
        const int burst = (int) (t / 0.010);

        if (burst < 3)
        {
            if (sampleCount % (int64_t) (0.010 * sr) == 0)
                noiseEnv = 1.0f;

            noiseEnv *= c_0_0045;
        }
        else
        {
            if (! clapTailStarted)
            {
                clapTailStarted = true;
                noiseEnv = 0.9f;
            }

            noiseEnv *= c_0_11;
            stopWhenSilent (noiseEnv);
        }

        const float n = filterA.bandPass (noise.next()) * noiseEnv;
        return std::tanh (3.2f * n);
    }

    float renderHat() noexcept
    {
        // Sei onde quadre a frequenze "metalliche" (ricetta della TR-808) + un po' di rumore.
        static constexpr double ratios[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };

        float metal = 0.0f;
        for (int i = 0; i < 6; ++i)
        {
            hatPhases[i] += ratios[i] * 1.7 / sr;
            if (hatPhases[i] >= 1.0)
                hatPhases[i] -= 1.0;
            metal += hatPhases[i] < 0.5 ? 1.0f : -1.0f;
        }

        float sig = metal * (1.0f / 6.0f) + 0.35f * noise.next();
        sig = filterB.bandPass (filterA.highPass (sig));

        const float out = sig * ampEnv * 2.2f;
        ampEnv *= hatDecay;

        stopWhenSilent (ampEnv);
        return std::tanh (out);
    }

    float renderTom() noexcept
    {
        const double freq = 105.0 + 70.0 * pitchEnv;
        pitchEnv *= c_0_06;

        const float body = sine (freq) * ampEnv;
        ampEnv *= c_0_16;

        const float click = filterA.highPass (noise.next()) * clickEnv * 0.25f;
        clickEnv *= c_0_002;

        stopWhenSilent (ampEnv);
        return std::tanh (3.0f * (body + click)) * 0.9f;
    }

    //==============================================================================
    double   sr = 48000.0;
    DrumType type = DrumType::kick;
    bool     active = false;
    bool     choking = false;
    bool     clapTailStarted = false;
    int64_t  sampleCount = 0;
    double   phase = 0.0;
    double   hatPhases[6] {};
    float    level = 1.0f, pitchEnv = 1.0f, ampEnv = 1.0f, clickEnv = 1.0f,
             noiseEnv = 1.0f, chokeGain = 1.0f;

    // Coefficienti di decadimento precalcolati in prepare() (exp() e' costoso da fare a ogni campione)
    float c_0_002 = 0.0f, c_0_003 = 0.0f, c_0_004 = 0.0f, c_0_0045 = 0.0f, c_0_02 = 0.0f, c_0_032 = 0.0f, c_0_055 = 0.0f, c_0_06 = 0.0f, c_0_11 = 0.0f, c_0_13 = 0.0f, c_0_16 = 0.0f, c_kickAmp = 0.0f, hatDecay = 0.0f;

    FastNoise noise;
    SVFilter  filterA, filterB;
};
