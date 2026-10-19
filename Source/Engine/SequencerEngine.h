#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    Il motore audio di DAWFL (prima versione).

    Gira sul thread audio, che e' "real-time": qui dentro niente allocazioni,
    niente lock, niente file. La UI comunica con il motore solo tramite
    variabili atomiche (steps, playing, bpm), che si possono leggere e
    scrivere da thread diversi in sicurezza.

    Per ora contiene:
    - un clock a 16 step (sedicesimi) con tempo in BPM, preciso al campione
    - un kick sintetizzato (sinusoide con caduta di pitch e di volume)
*/
class SequencerEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numSteps = 16;

    SequencerEngine()
    {
        // Pattern iniziale "four on the floor": kick su ogni quarto.
        for (int i = 0; i < numSteps; ++i)
            steps[(size_t) i].store (i % 4 == 0);
    }

    //==============================================================================
    // Comandi dalla UI (thread dei messaggi)

    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay && ! playing.load())
            restartRequested.store (true);

        playing.store (shouldPlay);
    }

    bool isPlaying() const                  { return playing.load(); }

    void setBpm (double newBpm)             { bpm.store (juce::jlimit (40.0, 240.0, newBpm)); }
    double getBpm() const                   { return bpm.load(); }

    void toggleStep (int index)             { steps[(size_t) index].store (! steps[(size_t) index].load()); }
    bool isStepOn (int index) const         { return steps[(size_t) index].load(); }

    /** Lo step che sta suonando adesso (-1 se fermo). Serve alla UI per il cursore. */
    int getCurrentStep() const              { return playing.load() ? currentStep.load() : -1; }

    double getSampleRate() const            { return sampleRate.load(); }
    int getBufferSize() const               { return bufferSize.load(); }

    //==============================================================================
    // Callback audio

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        sampleRate.store (device->getCurrentSampleRate());
        bufferSize.store (device->getCurrentBufferSizeSamples());
        restartRequested.store (true);
        kickActive = false;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const double sr = sampleRate.load();
        const bool isOn = playing.load();

        if (restartRequested.exchange (false))
        {
            stepIndex = -1;
            samplesUntilNextStep = 0.0;   // il primo step parte subito
        }

        // Durata di un sedicesimo in campioni: (60 / BPM) secondi per quarto, / 4.
        const double samplesPerStep = sr * 60.0 / bpm.load() / 4.0;

        for (int s = 0; s < numSamples; ++s)
        {
            if (isOn)
            {
                if (samplesUntilNextStep <= 0.0)
                {
                    stepIndex = (stepIndex + 1) % numSteps;
                    currentStep.store (stepIndex);

                    if (steps[(size_t) stepIndex].load())
                        triggerKick();

                    samplesUntilNextStep += samplesPerStep;
                }

                samplesUntilNextStep -= 1.0;
            }

            const float sample = renderKickSample (sr);

            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    out[s] = sample;
        }
    }

private:
    //==============================================================================
    void triggerKick()
    {
        kickActive = true;
        kickTime = 0.0;
        kickPhase = 0.0;   // ogni colpo parte dalla stessa fase: suona sempre uguale
    }

    float renderKickSample (double sr)
    {
        if (! kickActive)
            return 0.0f;

        // Pitch: parte alto (150 Hz) e scende velocemente verso 45 Hz -> il "punch" del kick.
        const double freq = 45.0 + 105.0 * std::exp (-kickTime * 30.0);
        // Volume: decadimento esponenziale, circa mezzo secondo di coda.
        const double amp  = std::exp (-kickTime * 7.0);

        const float value = (float) (std::sin (kickPhase) * amp * 0.8);

        kickPhase += juce::MathConstants<double>::twoPi * freq / sr;
        kickTime  += 1.0 / sr;

        if (amp < 0.0005)
            kickActive = false;

        return value;
    }

    //==============================================================================
    // Stato condiviso UI <-> audio
    std::array<std::atomic<bool>, numSteps> steps;
    std::atomic<bool>   playing          { false };
    std::atomic<bool>   restartRequested { true };
    std::atomic<double> bpm              { 120.0 };
    std::atomic<int>    currentStep      { -1 };
    std::atomic<double> sampleRate       { 48000.0 };
    std::atomic<int>    bufferSize       { 0 };

    // Stato usato SOLO dal thread audio
    int    stepIndex            = -1;
    double samplesUntilNextStep = 0.0;
    bool   kickActive           = false;
    double kickTime             = 0.0;
    double kickPhase            = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
};
