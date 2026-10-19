#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "DrumVoice.h"

//==============================================================================
/**
    Il motore audio di DAWFL.

    Gira sul thread audio, che e' "real-time": qui dentro niente allocazioni,
    niente lock, niente file. La UI comunica con il motore solo tramite
    variabili atomiche, che si possono leggere e scrivere da thread diversi.

    Struttura (come in FL Studio):
    - i CANALI (strumento, suono, volume, mute) sono del progetto
    - i PATTERN (fino a 8) contengono le note: per ogni canale e step una velocity
      (0 = spento, 1..127 = acceso con quella forza). Lunghezza 16 o 32 step.
    - clock preciso al campione con swing
*/
class SequencerEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int maxSteps         = 32;
    static constexpr int numChannels      = 6;
    static constexpr int numPatterns      = 8;
    static constexpr int closedHatChannel = 3;
    static constexpr int openHatChannel   = 4;
    static constexpr int defaultVelocity  = 100;

    struct ChannelInfo
    {
        const char*  name;
        juce::uint32 colour;
    };

    static const ChannelInfo& getChannelInfo (int index)
    {
        static const ChannelInfo infos[numChannels] =
        {
            { "Kick",     0xffe8833a },
            { "Snare",    0xffd2555a },
            { "Clap",     0xffd8b14a },
            { "Hi-Hat",   0xff4fb3a9 },
            { "Open Hat", 0xff5b8fd6 },
            { "Tom",      0xff9a78d0 },
        };
        return infos[index];
    }

    //==============================================================================
    /** Impostazioni di un canale (valgono per tutti i pattern). */
    struct Channel
    {
        std::atomic<float> volume           { 0.8f };
        std::atomic<bool>  muted            { false };
        std::atomic<int>   kit              { 0 };      // suono scelto dalla libreria (vedi SoundLibrary)
        std::atomic<bool>  previewRequested { false };
        std::atomic<float> activity         { 0.0f };   // per far "lampeggiare" la UI quando suona
    };

    /** Un pattern: velocity per canale e step, e la sua lunghezza. */
    struct Pattern
    {
        std::array<std::array<std::atomic<uint8_t>, maxSteps>, numChannels> velocity;
        std::atomic<int> length { 16 };
    };

    SequencerEngine()   { resetProject(); }

    //==============================================================================
    /** Progetto nuovo: tutto vuoto e valori di default. */
    void resetProject()
    {
        for (int p = 0; p < numPatterns; ++p)
            clearPattern (p);

        static const float defaultVolumes[numChannels] = { 0.8f, 0.8f, 0.7f, 0.6f, 0.55f, 0.75f };

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            ch.volume.store (defaultVolumes[c]);
            ch.muted.store (false);
            ch.kit.store (0);
        }

        bpm.store (120.0);
        swing.store (0.0f);
        masterVolume.store (1.0f);
        currentPattern.store (0);
    }

    //==============================================================================
    // Trasporto

    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay && ! playing.load())
            restartRequested.store (true);

        playing.store (shouldPlay);
    }

    bool isPlaying() const                      { return playing.load(); }

    void setBpm (double newBpm)                 { bpm.store (juce::jlimit (40.0, 240.0, newBpm)); }
    double getBpm() const                       { return bpm.load(); }

    /** Swing 0..1 (0 = dritto, 1 = massimo). Ritarda gli step dispari. */
    void setSwing (float s)                     { swing.store (juce::jlimit (0.0f, 1.0f, s)); }
    float getSwing() const                      { return swing.load(); }

    void setMasterVolume (float v)              { masterVolume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getMasterVolume() const               { return masterVolume.load(); }

    //==============================================================================
    // Pattern

    void selectPattern (int index)              { currentPattern.store (juce::jlimit (0, numPatterns - 1, index)); }
    int getCurrentPattern() const               { return currentPattern.load(); }

    Pattern& getPattern (int index)             { return patterns[(size_t) index]; }
    Pattern& current()                          { return patterns[(size_t) currentPattern.load()]; }
    const Pattern& current() const              { return patterns[(size_t) currentPattern.load()]; }

    int getPatternLength() const                { return current().length.load(); }
    void setPatternLength (int len)             { current().length.store (len > 16 ? 32 : 16); }

    int getStepVelocity (int channel, int step) const
    {
        return current().velocity[(size_t) channel][(size_t) step].load();
    }

    void setStepVelocity (int channel, int step, int vel)
    {
        current().velocity[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (0, 127, vel));
    }

    bool isStepOn (int channel, int step) const { return getStepVelocity (channel, step) > 0; }

    void clearPattern (int index)
    {
        auto& p = patterns[(size_t) index];
        for (auto& row : p.velocity)
            for (auto& v : row)
                v.store (0);
        p.length.store (16);
    }

    void copyPattern (int from, int to)
    {
        if (from == to)
            return;

        auto& src = patterns[(size_t) from];
        auto& dst = patterns[(size_t) to];

        for (int c = 0; c < numChannels; ++c)
            for (int s = 0; s < maxSteps; ++s)
                dst.velocity[(size_t) c][(size_t) s].store (src.velocity[(size_t) c][(size_t) s].load());

        dst.length.store (src.length.load());
    }

    bool isPatternEmpty (int index) const
    {
        for (auto& row : patterns[(size_t) index].velocity)
            for (auto& v : row)
                if (v.load() > 0)
                    return false;
        return true;
    }

    //==============================================================================
    // Canali

    Channel& getChannel (int index)             { return channels[(size_t) index]; }

    void previewChannel (int channel)           { channels[(size_t) channel].previewRequested.store (true); }

    void setChannelKit (int channel, int kit)   { channels[(size_t) channel].kit.store (juce::jlimit (0, SoundLibrary::numKits - 1, kit)); }
    int getChannelKit (int channel) const       { return channels[(size_t) channel].kit.load(); }

    void setAllKits (int kit)
    {
        for (int c = 0; c < numChannels; ++c)
            setChannelKit (c, kit);
    }

    //==============================================================================
    // Stato per la UI

    /** Lo step che sta suonando adesso (-1 se fermo). */
    int getCurrentStep() const                  { return playing.load() ? currentStep.load() : -1; }

    /** Picco d'uscita dall'ultima lettura (0..1), per il meter. */
    float takeOutputPeak()                      { return outputPeak.exchange (0.0f); }

    //==============================================================================
    // Callback audio

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const double sr = device->getCurrentSampleRate();
        sampleRate.store (sr);
        restartRequested.store (true);

        for (int c = 0; c < numChannels; ++c)
        {
            appliedKits[(size_t) c] = channels[(size_t) c].kit.load();
            voices[(size_t) c].setParams (SoundLibrary::get (c, appliedKits[(size_t) c]));
            voices[(size_t) c].prepare (sr);
        }
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const double sr     = sampleRate.load();
        const bool   isOn   = playing.load();
        const float  master = masterVolume.load();

        if (restartRequested.exchange (false))
        {
            stepIndex = -1;
            samplesUntilNextStep = 0.0;   // il primo step parte subito
        }

        // Suoni cambiati dalla UI: aggiorna le ricette delle voci.
        for (int c = 0; c < numChannels; ++c)
        {
            const int kit = channels[(size_t) c].kit.load();
            if (kit != appliedKits[(size_t) c])
            {
                appliedKits[(size_t) c] = kit;
                voices[(size_t) c].setParams (SoundLibrary::get (c, kit));
            }
        }

        // Anteprime richieste dalla UI (tocco sul nome del canale).
        for (int c = 0; c < numChannels; ++c)
            if (channels[(size_t) c].previewRequested.exchange (false))
                triggerChannel (c, 1.0f);

        // Volumi letti una volta per blocco.
        std::array<float, numChannels> gains;
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            gains[(size_t) c] = ch.muted.load() ? 0.0f : ch.volume.load();
        }

        // Durata di un sedicesimo in campioni: (60 / BPM) secondi per quarto, / 4.
        const double samplesPerStep = sr * 60.0 / bpm.load() / 4.0;
        // Swing: gli step pari si allungano e i dispari si accorciano (al massimo +-1/3: la "terzina").
        const double swingAmount = (double) swing.load() / 3.0;
        auto& pattern = patterns[(size_t) currentPattern.load()];
        const int length = pattern.length.load();
        float peak = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            if (isOn)
            {
                if (samplesUntilNextStep <= 0.0)
                {
                    stepIndex = (stepIndex + 1) % length;
                    currentStep.store (stepIndex);

                    for (int c = 0; c < numChannels; ++c)
                    {
                        const int vel = pattern.velocity[(size_t) c][(size_t) stepIndex].load();
                        if (vel > 0)
                            triggerChannel (c, (float) vel / 127.0f);
                    }

                    const bool even = (stepIndex % 2) == 0;
                    samplesUntilNextStep += samplesPerStep * (even ? 1.0 + swingAmount : 1.0 - swingAmount);
                }

                samplesUntilNextStep -= 1.0;
            }

            float mix = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mix += voices[(size_t) c].render() * gains[(size_t) c];

            // Saturazione morbida: alza il volume percepito senza distorsioni brutte.
            const float sample = std::tanh (mix * master * 1.3f);
            peak = juce::jmax (peak, std::abs (sample));

            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    out[s] = sample;
        }

        if (peak > outputPeak.load())
            outputPeak.store (peak);
    }

private:
    void triggerChannel (int c, float velocity)
    {
        // L'hi-hat chiuso "strozza" quello aperto, come su una vera drum machine.
        if (c == closedHatChannel)
            voices[openHatChannel].choke();

        voices[(size_t) c].trigger (velocity);
        channels[(size_t) c].activity.store (velocity);
    }

    //==============================================================================
    // Stato condiviso UI <-> audio
    std::array<Channel, numChannels> channels;
    std::array<Pattern, numPatterns> patterns;
    std::atomic<int>    currentPattern   { 0 };
    std::atomic<bool>   playing          { false };
    std::atomic<bool>   restartRequested { true };
    std::atomic<double> bpm              { 120.0 };
    std::atomic<float>  swing            { 0.0f };
    std::atomic<float>  masterVolume     { 1.0f };
    std::atomic<int>    currentStep      { -1 };
    std::atomic<float>  outputPeak       { 0.0f };
    std::atomic<double> sampleRate       { 48000.0 };

    // Stato usato SOLO dal thread audio
    std::array<DrumVoice, numChannels> voices;
    std::array<int, numChannels> appliedKits {};
    int    stepIndex            = -1;
    double samplesUntilNextStep = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
};
