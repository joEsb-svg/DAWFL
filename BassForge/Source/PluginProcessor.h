#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters/ParameterLayout.h"
#include "Parameters/ParamCache.h"
#include "DSP/Wavetable.h"
#include "FX/MasterFXChain.h"
#include "FX/MasterLimiter.h"
#include "DSP/SampleBank.h"
#include "Synth/SynthSound.h"
#include "Synth/BassVoice.h"
#include "Presets/PresetManager.h"

class BassForgeAudioProcessor : public juce::AudioProcessor
{
public:
    /** Latenza totale dichiarata all'host: FIR di oversampling dell'Hyper-Drive + lookahead del limiter. */
    static constexpr int   kTotalLatency   = bf::MasterFXChain::kLatencySamples + bf::MasterLimiter::kLatency;
    static constexpr float kLimiterCeiling = 0.96605f;      // -0.3 dBFS
    BassForgeAudioProcessor();
    ~BassForgeAudioProcessor() override = default;

    // --- Lifecycle --------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // --- Audio thread -------------------------------------------------------------
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    // --- Editor ----------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Info ------------------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }   // release + FX tail

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // --- Stato -----------------------------------------------------------------------
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // NB: l'ordine dei membri conta (apvts -> cache -> wavetable -> synth).
    juce::AudioProcessorValueTreeState apvts;

    /** Per la UI / il message thread: caricare campioni utente (slot 4..7) con getSampleBank().loadFile(). */
    bf::SampleBank& getSampleBank() noexcept { return sampleBank; }

    /** Per la UI: pattern dei 4 Performer (32 step, valore/curva/glide per step). Thread-safe (atomic).
     *  Salvati e ripristinati insieme allo stato del plugin. */
    bf::PerformerBank& getPerformers() noexcept { return performers; }

    /** Per la GUI (thread-safe in lettura): banco wavetable immutabile, modulazione live, oscilloscopio. */
    const bf::WavetableBank& getWavetables() const noexcept { return wavetables; }
    const bf::ModMonitor&    getModMonitor() const noexcept { return modMonitor; }
    const bf::ScopeBuffer&   getScope() const noexcept { return scope; }
    /** Preset factory/utente (solo message thread). */
    bf::PresetManager&       getPresets() noexcept { return presets; }

private:
    bf::ParamCache   paramCache;
    bf::WavetableBank wavetables;
    bf::SampleBank   sampleBank;
    bf::PerformerBank performers;          // letto da tutte le voci
    bf::PresetManager presets { apvts, performers };
    std::atomic<float> hostBpm { 120.0f }; // aggiornato a ogni blocco dal playhead
    bf::SharedMidiState sharedMidi;        // mod wheel globale
    bf::ModMonitor    modMonitor;          // modulazione live -> GUI
    bf::ScopeBuffer   scope;               // ultimi campioni di uscita -> GUI
    std::array<float, 8192> scopeTmp{};    // mono scratch per lo scope (nessuna allocazione nel callback)
    juce::AudioBuffer<float> directBus;   // mono: Sub in Direct-Out, sommato DOPO gli FX master
    juce::Synthesiser synth;
    bf::MasterFXChain masterFx;
    bf::FxParams      fxParams;            // riempito a ogni blocco da ParamCache (nessuna allocazione)
    bf::SampleDelay   directDelay;         // allinea il Sub Direct-Out alla latenza della catena FX
    bf::MasterLimiter limiter;             // ultimo stadio: brickwall -0.3 dBFS con lookahead
    float            lastMasterGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassForgeAudioProcessor)
};
