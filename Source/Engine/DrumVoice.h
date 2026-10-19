#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>
#include "SoundLibrary.h"

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
/**
    Una voce di batteria sintetizzata, guidata da una "ricetta" (DrumParams).
    Tutto qui dentro e' sicuro per il thread audio: niente allocazioni.
*/
class DrumVoice
{
public:
    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        active = false;
        clickFilter.set (sr, 1200.0, 0.7);
        setParams (params);
    }

    /** Cambia il suono. Va chiamato dal thread audio (o prima che l'audio parta). */
    void setParams (const DrumParams& p)
    {
        params = p;

        cPitch = coef (p.pitchTau);
        cTone  = coef (p.toneTau);
        cClick = coef (p.clickTau);
        cNoise = coef (p.noiseTau);
        cBurst = coef (p.burstTau);
        cChoke = coef (0.004);
        burstSamples = juce::jmax ((int64_t) 1, (int64_t) (p.burstSpacing * sr));

        if (p.highPass > 0.0f) hpFilter.set (sr, p.highPass, 0.7);
        if (p.bandPass > 0.0f) bpFilter.set (sr, p.bandPass, p.bandQ);
    }

    void trigger (float velocity)
    {
        active      = true;
        level       = velocity;
        sampleCount = 0;
        phase       = 0.0;
        pitchEnv    = 1.0f;
        // Gli ingredienti non usati partono gia' "spenti", cosi' la voce sa quando ha finito.
        toneEnv     = params.toneLevel  > 0.0f ? 1.0f : 0.0f;
        clickEnv    = params.clickLevel > 0.0f ? 1.0f : 0.0f;
        noiseEnv    = (params.noiseLevel > 0.0f || params.metalLevel > 0.0f) ? 1.0f : 0.0f;
        burstEnv    = 1.0f;
        chokeGain   = 1.0f;
        choking     = false;
        clickFilter.reset();
        hpFilter.reset();
        bpFilter.reset();

        for (auto& p : metalPhases)
            p = 0.0;
    }

    /** Sfuma velocemente la voce (es. l'hi-hat chiuso "strozza" quello aperto). */
    void choke()                    { if (active) choking = true; }

    bool isActive() const noexcept  { return active; }

    float render() noexcept
    {
        if (! active)
            return 0.0f;

        const auto& p = params;
        float sig = 0.0f;

        // TONO: sinusoide con caduta di pitch
        if (p.toneLevel > 0.0f)
        {
            const double freq = p.toneEnd + (p.toneStart - p.toneEnd) * pitchEnv;
            sig += (float) std::sin (phase) * toneEnv * p.toneLevel;

            phase += juce::MathConstants<double>::twoPi * freq / sr;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            pitchEnv *= cPitch;
            toneEnv  *= cTone;
        }

        // CLICK
        if (p.clickLevel > 0.0f)
        {
            sig += clickFilter.highPass (noise.next()) * clickEnv * p.clickLevel;
            clickEnv *= cClick;
        }

        // RUMORE + METALLO, filtrati
        if (p.noiseLevel > 0.0f || p.metalLevel > 0.0f)
        {
            float src = 0.0f;

            if (p.noiseLevel > 0.0f)
                src += noise.next() * p.noiseLevel;

            if (p.metalLevel > 0.0f)
            {
                static constexpr double freqs[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                float metal = 0.0f;

                for (int i = 0; i < 6; ++i)
                {
                    metalPhases[i] += freqs[i] * p.metalPitch / sr;
                    if (metalPhases[i] >= 1.0)
                        metalPhases[i] -= 1.0;
                    metal += metalPhases[i] < 0.5 ? 1.0f : -1.0f;
                }

                src += metal * (p.metalLevel / 6.0f);
            }

            if (p.highPass > 0.0f) src = hpFilter.highPass (src);
            if (p.bandPass > 0.0f) src = bpFilter.bandPass (src) * 1.6f;

            sig += src * noiseEnvelope();
        }

        // Fine del suono quando tutti gli inviluppi sono spenti
        if (toneEnv < 0.0003f && clickEnv < 0.0003f && noiseEnv < 0.0003f)
            active = false;

        if (choking)
        {
            chokeGain *= cChoke;
            if (chokeGain < 0.0005f)
                active = false;
        }

        ++sampleCount;
        return std::tanh (p.drive * sig) * p.gain * level * chokeGain;
    }

private:
    float coef (double tau) const noexcept    { return (float) std::exp (-1.0 / (juce::jmax (0.0005, tau) * sr)); }

    /** Inviluppo del rumore: normale, oppure "a battiti" per il clap. */
    float noiseEnvelope() noexcept
    {
        const auto& p = params;
        const float env = noiseEnv;

        if (p.bursts > 0 && sampleCount < burstSamples * p.bursts)
        {
            if (sampleCount % burstSamples == 0)
                burstEnv = 1.0f;

            const float e = burstEnv;
            burstEnv *= cBurst;
            return e;
        }

        noiseEnv *= cNoise;
        return env;
    }

    //==============================================================================
    double     sr = 48000.0;
    DrumParams params;
    bool       active = false, choking = false;
    int64_t    sampleCount = 0, burstSamples = 480;
    double     phase = 0.0;
    double     metalPhases[6] {};
    float      level = 1.0f, pitchEnv = 1.0f, toneEnv = 1.0f, clickEnv = 1.0f,
               noiseEnv = 1.0f, burstEnv = 1.0f, chokeGain = 1.0f;

    // Coefficienti di decadimento precalcolati (exp() e' costoso da fare a ogni campione)
    float cPitch = 0.0f, cTone = 0.0f, cClick = 0.0f, cNoise = 0.0f, cBurst = 0.0f, cChoke = 0.0f;

    FastNoise noise;
    SVFilter  clickFilter, hpFilter, bpFilter;
};
