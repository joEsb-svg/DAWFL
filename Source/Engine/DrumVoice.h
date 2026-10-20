#pragma once

#include <JuceHeader.h>
#include "Performance.h"
#include <cmath>
#include <cstdint>
#include "SoundLibrary.h"
#include "SampleStore.h"

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
/** Le regolazioni del canale che modellano il preset (editor del canale). */
struct SoundShape
{
    float pitch = 0.0f;   // semitoni, -12..+12
    float decay = 1.0f;   // moltiplicatore della durata, 0.25..3
    float tone  = 0.0f;   // -1 (scuro) .. +1 (brillante)
    float drive = 0.0f;   // 0..1, saturazione in piu'
};

/** Applica le regolazioni del canale a una ricetta della libreria. */
inline DrumParams shapeParams (DrumParams p, const SoundShape& s)
{
    const float ratio = std::pow (2.0f, s.pitch / 12.0f);
    p.toneStart *= ratio;
    p.toneEnd   *= ratio;
    p.metalPitch *= ratio;
    if (p.bandPass > 0.0f) p.bandPass *= ratio * std::pow (2.0f, 0.5f * s.tone);
    if (p.highPass > 0.0f) p.highPass *= std::sqrt (ratio) * std::pow (2.0f, s.tone);

    p.toneTau  *= s.decay;
    p.noiseTau *= s.decay;
    p.pitchTau *= std::sqrt (s.decay);

    p.clickLevel *= juce::jmax (0.0f, 1.0f + s.tone);
    p.drive *= 1.0f + 3.0f * s.drive;
    p.gain  /= 1.0f + 0.35f * s.drive;
    return p;
}

//==============================================================================
/**
    Una voce di batteria sintetizzata, guidata da una "ricetta" (DrumParams).
    Tutto qui dentro e' sicuro per il thread audio: niente allocazioni.

    Oltre alla ricetta:
    - la VELOCITY cambia anche il timbro (colpo forte = piu' click, piu' saturo, piu' brillante)
    - HUMANIZE: piccole variazioni casuali di forza, intonazione e tempo a ogni colpo
*/
class DrumVoice
{
public:
    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        active = false;
        pendingDelay = -1;
        clickFilter.set (sr, 1200.0, 0.7);
        setParams (params, toneTilt);
    }

    /** Cambia il suono. Va chiamato dal thread audio (o prima che l'audio parta). */
    void setParams (const DrumParams& p, float newToneTilt = 0.0f)
    {
        params = p;
        toneTilt = newToneTilt;

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

    /**
        Suona un campione invece della sintesi (nullptr = torna alla sintesi).
        Pitch, decay, tono e drive del canale valgono anche per il campione.
    */
    void setSample (const SampleData* newSample, const SoundShape& shape)
    {
        if (newSample != sample)
            active = false;   // il campione vecchio non va piu' letto

        sample = newSample;
        toneTilt = shape.tone;

        if (sample != nullptr)
        {
            sampleStep = sample->sampleRate / sr * std::pow (2.0, shape.pitch / 12.0);
            sampleFade = shape.decay < 0.999f ? coef (shape.decay * 0.5) : 1.0f;
            sampleDrive = 1.0f + 3.0f * shape.drive;
            sampleDriveNorm = 1.0f / std::tanh (sampleDrive);
        }
    }

    bool isSampleMode() const noexcept  { return sample != nullptr; }

    /**
        Fa partire un colpo.
        @param velocity   0..1
        @param humanize   0..1, quanta variazione casuale aggiungere
    */
    void trigger (float velocity, float humanize = 0.0f, float semitones = 0.0f)
    {
        float vel = velocity;
        float detune = semitones != 0.0f ? std::pow (2.0f, semitones / 12.0f) : 1.0f;   // nota del piano roll
        int delay = 0;

        if (humanize > 0.0f)
        {
            vel    *= 1.0f - humanize * 0.22f * (0.5f + 0.5f * random.next());
            detune *= std::pow (2.0f, humanize * 0.25f * random.next() / 12.0f);   // fino a +-1/4 di semitono
            delay   = (int) (std::abs (random.next()) * humanize * 0.006 * sr);    // fino a 6 ms in ritardo
        }

        pendingVelocity = juce::jlimit (0.0f, 1.0f, vel);
        pendingDetune = detune;
        pendingDelay = delay;

        if (delay == 0)
            start();
    }

    /** Sfuma velocemente la voce (es. l'hi-hat chiuso "strozza" quello aperto). */
    void choke()                    { if (active) choking = true; }

    bool isActive() const noexcept  { return active || pendingDelay >= 0; }

    float render() noexcept
    {
        if (pendingDelay > 0 && --pendingDelay == 0)
            start();

        if (! active)
        {
            lpState = 0.0f;
            return 0.0f;
        }

        if (sample != nullptr)
            return renderSample();

        const auto& p = params;
        float sig = 0.0f;

        // TONO: sinusoide con caduta di pitch
        if (p.toneLevel > 0.0f)
        {
            const double freq = (p.toneEnd + (p.toneStart - p.toneEnd) * pitchEnv) * detuneRatio;
            sig += perf::fastSin ((float) (phase * (1.0 / juce::MathConstants<double>::twoPi))) * toneEnv * p.toneLevel;

            phase += juce::MathConstants<double>::twoPi * freq / sr;
            if (phase > juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;

            pitchEnv *= cPitch;
            toneEnv  *= cTone;
        }

        // CLICK (piu' presente nei colpi forti)
        if (p.clickLevel > 0.0f)
        {
            sig += clickFilter.highPass (noise.next()) * clickEnv * p.clickLevel * clickGain;
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
                    metalPhases[i] += freqs[i] * p.metalPitch * detuneRatio / sr;
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

        float out = std::tanh (p.drive * driveGain * sig) * p.gain * level * chokeGain;

        // Passa-basso a un polo: piu' chiuso nei colpi deboli e con "tono" scuro
        lpState += lpCoef * (out - lpState);
        return lpState;
    }

private:
    float renderSample() noexcept
    {
        const auto& audio = sample->audio;
        const int len = audio.getNumSamples();
        const auto pos = (int) samplePos;

        if (pos >= len - 1 || sampleEnv < 0.0003f)
        {
            active = false;
            return 0.0f;
        }

        // Lettura con interpolazione lineare (per pitch diversi da quello originale)
        const float* data = audio.getReadPointer (0);
        const float frac = (float) (samplePos - pos);
        const float x = data[pos] + frac * (data[pos + 1] - data[pos]);
        samplePos += sampleStep * detuneRatio;

        float out = x * sampleEnv * level * chokeGain;
        sampleEnv *= sampleFade;

        if (choking)
        {
            chokeGain *= cChoke;
            if (chokeGain < 0.0005f)
                active = false;
        }

        if (sampleDrive > 1.001f)
            out = std::tanh (out * sampleDrive * driveGain) * sampleDriveNorm;

        lpState += lpCoef * (out - lpState);
        return lpState;
    }

    void start() noexcept
    {
        pendingDelay = -1;
        const float v = pendingVelocity;

        active      = true;
        level       = std::pow (v, 1.3f);                 // curva un po' esponenziale, come le drum machine
        clickGain   = 0.35f + 0.65f * v;
        driveGain   = 0.65f + 0.35f * v;
        detuneRatio = pendingDetune;

        const double cutoff = juce::jlimit (300.0, sr * 0.45,
                                            2200.0 * std::pow (9.0, (double) v) * std::pow (2.0, 1.5 * toneTilt));
        lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / sr));

        sampleCount = 0;
        samplePos   = 0.0;
        sampleEnv   = 1.0f;
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

        for (auto& ph : metalPhases)
            ph = 0.0;
    }

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
    float      toneTilt = 0.0f;
    bool       active = false, choking = false;
    int64_t    sampleCount = 0, burstSamples = 480;
    double     phase = 0.0;
    double     metalPhases[6] {};
    float      level = 1.0f, pitchEnv = 1.0f, toneEnv = 1.0f, clickEnv = 1.0f,
               noiseEnv = 1.0f, burstEnv = 1.0f, chokeGain = 1.0f;
    float      clickGain = 1.0f, driveGain = 1.0f, detuneRatio = 1.0f, lpCoef = 1.0f, lpState = 0.0f;

    // Colpo in attesa (humanize: puo' partire qualche ms dopo)
    int        pendingDelay = -1;
    float      pendingVelocity = 1.0f, pendingDetune = 1.0f;

    // Coefficienti di decadimento precalcolati (exp() e' costoso da fare a ogni campione)
    float cPitch = 0.0f, cTone = 0.0f, cClick = 0.0f, cNoise = 0.0f, cBurst = 0.0f, cChoke = 0.0f;

    // Modalita' campione
    const SampleData* sample = nullptr;
    double samplePos = 0.0, sampleStep = 1.0;
    float  sampleEnv = 1.0f, sampleFade = 1.0f, sampleDrive = 1.0f, sampleDriveNorm = 1.0f;

    FastNoise noise, random { 0x9e3779b9u };
    SVFilter  clickFilter, hpFilter, bpFilter;
};
