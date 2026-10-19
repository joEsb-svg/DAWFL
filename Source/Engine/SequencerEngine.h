#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include "DrumVoice.h"

//==============================================================================
/**
    Il motore audio di DAWFL.

    Gira sul thread audio, che e' "real-time": qui dentro niente allocazioni,
    niente lock, niente file. La UI comunica con il motore solo tramite
    variabili atomiche, che si possono leggere e scrivere da thread diversi.

    Contiene:
    - un clock a 16 step (sedicesimi) preciso al campione
    - un channel rack di 6 strumenti di batteria sintetizzati
    - volume e mute per canale, volume master con saturazione morbida
*/
class SequencerEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numSteps    = 16;
    static constexpr int numChannels = 6;

    struct ChannelInfo
    {
        const char* name;
        DrumType    type;
        juce::uint32 colour;
    };

    static const ChannelInfo& getChannelInfo (int index)
    {
        static const ChannelInfo infos[numChannels] =
        {
            { "Kick",     DrumType::kick,      0xffff8a1f },
            { "Snare",    DrumType::snare,     0xffff4d6d },
            { "Clap",     DrumType::clap,      0xffffc93c },
            { "Hi-Hat",   DrumType::closedHat, 0xff3fd0c9 },
            { "Open Hat", DrumType::openHat,   0xff4fa3ff },
            { "Tom",      DrumType::tom,       0xffb37bff },
        };
        return infos[index];
    }

    //==============================================================================
    /** Stato di un canale condiviso tra UI e audio. */
    struct Channel
    {
        std::array<std::atomic<bool>, numSteps> steps;
        std::atomic<float> volume           { 0.8f };
        std::atomic<bool>  muted            { false };
        std::atomic<bool>  previewRequested { false };
        std::atomic<float> activity         { 0.0f };   // per far "lampeggiare" la UI quando suona
    };

    SequencerEngine()
    {
        // Un groove di partenza.
        const char* patterns[numChannels] =
        {
            "x...x...x...x...",   // kick
            "....x.......x...",   // snare
            "............x...",   // clap
            "x.x.x.x.x.x.x.x.",   // hi-hat
            "..............x.",   // open hat
            "................",   // tom
        };

        for (int c = 0; c < numChannels; ++c)
            for (int s = 0; s < numSteps; ++s)
                channels[(size_t) c].steps[(size_t) s].store (patterns[c][s] == 'x');

        const float defaultVolumes[numChannels] = { 0.8f, 0.8f, 0.7f, 0.6f, 0.55f, 0.75f };
        for (int c = 0; c < numChannels; ++c)
            channels[(size_t) c].volume.store (defaultVolumes[c]);
    }

    //==============================================================================
    // Comandi dalla UI (thread dei messaggi)

    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay && ! playing.load())
            restartRequested.store (true);

        playing.store (shouldPlay);
    }

    bool isPlaying() const                      { return playing.load(); }

    void setBpm (double newBpm)                 { bpm.store (juce::jlimit (40.0, 240.0, newBpm)); }
    double getBpm() const                       { return bpm.load(); }

    Channel& getChannel (int index)             { return channels[(size_t) index]; }

    void setStep (int channel, int step, bool on)   { channels[(size_t) channel].steps[(size_t) step].store (on); }
    bool isStepOn (int channel, int step) const     { return channels[(size_t) channel].steps[(size_t) step].load(); }

    void previewChannel (int channel)           { channels[(size_t) channel].previewRequested.store (true); }

    void setMasterVolume (float v)              { masterVolume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getMasterVolume() const               { return masterVolume.load(); }

    /** Lo step che sta suonando adesso (-1 se fermo). */
    int getCurrentStep() const                  { return playing.load() ? currentStep.load() : -1; }

    /** Picco d'uscita dall'ultima lettura (0..1), per il meter. */
    float takeOutputPeak()                      { return outputPeak.exchange (0.0f); }

    double getSampleRate() const                { return sampleRate.load(); }
    int getBufferSize() const                   { return bufferSize.load(); }

    //==============================================================================
    // Callback audio

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const double sr = device->getCurrentSampleRate();
        sampleRate.store (sr);
        bufferSize.store (device->getCurrentBufferSizeSamples());
        restartRequested.store (true);

        for (int c = 0; c < numChannels; ++c)
            voices[(size_t) c].prepare (sr, getChannelInfo (c).type);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const double sr = sampleRate.load();
        const bool isOn = playing.load();
        const float master = masterVolume.load();

        if (restartRequested.exchange (false))
        {
            stepIndex = -1;
            samplesUntilNextStep = 0.0;   // il primo step parte subito
        }

        // Anteprime richieste dalla UI (tocco sul nome del canale).
        for (int c = 0; c < numChannels; ++c)
            if (channels[(size_t) c].previewRequested.exchange (false))
                triggerChannel (c);

        // Volumi letti una volta per blocco.
        std::array<float, numChannels> gains;
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            gains[(size_t) c] = ch.muted.load() ? 0.0f : ch.volume.load();
        }

        // Durata di un sedicesimo in campioni: (60 / BPM) secondi per quarto, / 4.
        const double samplesPerStep = sr * 60.0 / bpm.load() / 4.0;
        float peak = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            if (isOn)
            {
                if (samplesUntilNextStep <= 0.0)
                {
                    stepIndex = (stepIndex + 1) % numSteps;
                    currentStep.store (stepIndex);

                    for (int c = 0; c < numChannels; ++c)
                        if (channels[(size_t) c].steps[(size_t) stepIndex].load())
                            triggerChannel (c);

                    samplesUntilNextStep += samplesPerStep;
                }

                samplesUntilNextStep -= 1.0;
            }

            float mix = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                mix += voices[(size_t) c].render() * gains[(size_t) c];

            // Saturazione morbida: alza il volume percepito senza distorsioni brutte.
            const float sample = softClip (mix * master * 1.3f);
            peak = juce::jmax (peak, std::abs (sample));

            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    out[s] = sample;
        }

        if (peak > outputPeak.load())
            outputPeak.store (peak);
    }

private:
    void triggerChannel (int c)
    {
        // L'hi-hat chiuso "strozza" quello aperto, come su una vera drum machine.
        if (getChannelInfo (c).type == DrumType::closedHat)
            voices[4].choke();

        voices[(size_t) c].trigger (1.0f);
        channels[(size_t) c].activity.store (1.0f);
    }

    static float softClip (float x) noexcept
    {
        // Curva morbida tipo "tape": lineare per segnali bassi, si appiattisce verso +-1.
        return std::tanh (x);
    }

    //==============================================================================
    // Stato condiviso UI <-> audio
    std::array<Channel, numChannels> channels;
    std::atomic<bool>   playing          { false };
    std::atomic<bool>   restartRequested { true };
    std::atomic<double> bpm              { 120.0 };
    std::atomic<float>  masterVolume     { 1.0f };
    std::atomic<int>    currentStep      { -1 };
    std::atomic<float>  outputPeak       { 0.0f };
    std::atomic<double> sampleRate       { 48000.0 };
    std::atomic<int>    bufferSize       { 0 };

    // Stato usato SOLO dal thread audio
    std::array<DrumVoice, numChannels> voices;
    int    stepIndex            = -1;
    double samplesUntilNextStep = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
};
