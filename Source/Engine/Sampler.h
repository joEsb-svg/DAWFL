#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "DrumVoice.h"

//==============================================================================
/**
    Il campionatore di un canale: fino a 8 note insieme (accordi nel piano roll),
    lettura con interpolazione lineare, fine nota con sfumatura per gli strumenti "tenuti"
    (archi, organo, pad...). Tutto sicuro per il thread audio: niente allocazioni.

    Le regolazioni del canale valgono anche qui: pitch, decay (accorcia), tono (piu' scuro
    o piu' brillante) e drive (saturazione).
*/
class SamplerPool
{
public:
    static constexpr int numVoices = 8;

    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        stopAll();
        lowState = shelfState = 0.0f;
        updateCoefficients();
    }

    /** Campione e regolazioni. Se il campione cambia, le note in corso si fermano. */
    void setSample (const SampleData* newSample, const SoundShape& newShape)
    {
        if (newSample != sample)
            stopAll();

        sample = newSample;
        shape = newShape;
        updateCoefficients();
    }

    const SampleData* getSample() const noexcept     { return sample; }

    /**
        Fa partire una nota.
        @param semitones   distanza dalla nota a cui e' registrato il campione
        @param gateTicks   dopo quanti tick del sequencer sfumare (-1 = suona fino alla fine)
        @param humanize    piccole variazioni di forza e di tempo
    */
    void trigger (float velocity, float semitones, int gateTicks, float humanize, float randomA, float randomB, bool cutPrevious = false)
    {
        if (sample == nullptr || sample->audio.getNumSamples() < 2)
            return;

        // Colpo a step: ferma (in pochi ms) quelli ancora in corso, cosi' le code non si sommano in un rumore di fondo
        if (cutPrevious)
            for (auto& o : voices)
                if (o.active)
                    o.cut = true;

        auto& v = voices[(size_t) pickVoice()];
        float vel = velocity;
        int delay = 0;
        if (humanize > 0.0f)
        {
            vel  *= 1.0f - humanize * 0.22f * (0.5f + 0.5f * randomA);
            delay = (int) (std::abs (randomB) * humanize * 0.006 * sr);
        }

        v.pos       = 0.0;
        v.step      = baseStep * std::pow (2.0, (double) semitones / 12.0);
        v.level     = std::pow (juce::jlimit (0.0f, 1.0f, vel), 1.3f);
        v.env       = 1.0f;
        v.release   = 1.0f;
        v.releasing = false;
        v.cut       = false;
        v.gateTicks = gateTicks;
        v.delay     = delay;
        v.age       = ++ageCounter;
        v.active    = true;
    }

    /** Un tick del sequencer: conta le durate delle note del piano roll. */
    void tick() noexcept
    {
        for (auto& v : voices)
            if (v.active && v.gateTicks > 0 && --v.gateTicks == 0)
                v.releasing = true;
    }

    /** Sfuma tutte le note (stop del trasporto). */
    void releaseAll() noexcept
    {
        for (auto& v : voices)
            if (v.active && v.gateTicks >= 0)
                v.releasing = true;
    }

    void stopAll() noexcept
    {
        for (auto& v : voices)
            v.active = false;
    }

    int activeVoices() const noexcept
    {
        int count = 0;
        for (auto& v : voices)
            count += v.active ? 1 : 0;
        return count;
    }

    bool isActive() const noexcept
    {
        for (auto& v : voices)
            if (v.active)
                return true;
        return false;
    }

    float render() noexcept
    {
        if (sample == nullptr)
            return 0.0f;

        const float* data = sample->audio.getReadPointer (0);
        const int len = sample->audio.getNumSamples();
        float sum = 0.0f;
        bool any = false;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            if (v.delay > 0)
            {
                --v.delay;
                any = true;
                continue;
            }

            const int pos = (int) v.pos;
            if (pos >= len - 1 || v.env < 0.0003f || v.release < 0.0003f)
            {
                v.active = false;
                continue;
            }

            const float frac = (float) (v.pos - pos);
            const float x = data[pos] + frac * (data[pos + 1] - data[pos]);
            v.pos += v.step;

            sum += x * v.level * v.env * v.release;
            v.env *= fade;
            if (v.cut)
                v.release *= cutCoef;
            else if (v.releasing)
                v.release *= releaseCoef;
            any = true;
        }

        if (! any)
        {
            lowState *= 0.0f;
            return 0.0f;
        }

        // Tono: scuro = passa-basso, brillante = esalta gli acuti
        if (shape.tone < -0.01f)
        {
            lowState += lowCoef * (sum - lowState);
            sum = lowState;
        }
        else if (shape.tone > 0.01f)
        {
            shelfState += shelfCoef * (sum - shelfState);
            sum += (sum - shelfState) * shape.tone * 1.2f;
        }

        if (driveAmount > 1.001f)
            sum = std::tanh (sum * driveAmount) * driveNorm;

        return sum;
    }

private:
    struct Voice
    {
        double pos = 0.0, step = 1.0;
        float  level = 1.0f, env = 1.0f, release = 1.0f;
        bool   active = false, releasing = false, cut = false;
        int    gateTicks = -1, delay = 0;
        uint32_t age = 0;
    };

    int pickVoice() noexcept
    {
        int oldest = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            if (! voices[(size_t) i].active)
                return i;
            if (voices[(size_t) i].age < voices[(size_t) oldest].age)
                oldest = i;
        }
        return oldest;   // tutte occupate: ruba la piu' vecchia
    }

    void updateCoefficients()
    {
        const double pitchRatio = std::pow (2.0, (double) shape.pitch / 12.0);
        baseStep = sample != nullptr ? sample->sampleRate / sr * pitchRatio : 1.0;
        fade = shape.decay < 0.999f ? (float) std::exp (-1.0 / (juce::jmax (0.01, (double) shape.decay * 0.5) * sr)) : 1.0f;

        const double rel = sample != nullptr ? juce::jmax (0.005, (double) sample->release) : 0.15;
        releaseCoef = (float) std::exp (-1.0 / (rel * 0.35 * sr));   // ~ -25 dB al tempo di rilascio
        cutCoef = (float) std::exp (-1.0 / (0.006 * sr));             // colpo interrotto: sfuma in ~20 ms senza click

        const double cutoff = 20000.0 * std::pow (2.0, 3.5 * (double) juce::jmin (0.0f, shape.tone));
        lowCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (150.0, sr * 0.45, cutoff) / sr));
        shelfCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 2500.0 / sr));

        driveAmount = 1.0f + 3.0f * shape.drive;
        driveNorm = 1.0f / std::tanh (driveAmount);
    }

    double sr = 48000.0;
    const SampleData* sample = nullptr;
    SoundShape shape;
    std::array<Voice, numVoices> voices {};
    uint32_t ageCounter = 0;
    double baseStep = 1.0;
    float fade = 1.0f, releaseCoef = 0.999f, cutCoef = 0.99f, lowCoef = 1.0f, shelfCoef = 0.3f, driveAmount = 1.0f, driveNorm = 1.0f;
    float lowState = 0.0f, shelfState = 0.0f;
};
