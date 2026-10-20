#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "../../Engine/SampleStore.h"

class DrumForgeAudioProcessor;

//==============================================================================
/**
    DrumForge: la drum machine di JLoop.

    - 16 pad, suonati dalle note 36..51 (C2..D#3: la disposizione classica delle drum machine)
    - ogni pad ha un campione (dalla libreria di JLoop o dell'utente) con intonazione, decadimento,
      volume, panorama, filtro passa-basso, mandata al riverbero, velocity e gruppo "choke"
      (es. l'hi-hat chiuso zittisce quello aperto)
    - kit di fabbrica per genere (acustico, 808, 909, house, techno, trap, DnB...) + kit dell'utente
    - uscita con saturazione e riverbero
    Nessuna allocazione nel thread audio: i campioni restano nel SampleStore per tutta la sessione.
*/
namespace df
{
    inline constexpr int numPads   = 16;
    inline constexpr int firstNote = 36;
    inline constexpr int maxVoices = 32;

    /** Id dei parametri di un pad: padId (3, "Tune") -> "pad3Tune". */
    inline juce::String padId (int pad, const char* what)     { return "pad" + juce::String (pad) + what; }

    namespace ids
    {
        inline constexpr const char* master  = "master";
        inline constexpr const char* drive   = "drive";
        inline constexpr const char* revSize = "revSize";
        inline constexpr const char* revMix  = "revLevel";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Un pad di un kit di fabbrica. */
    struct PadDef
    {
        const char* sample = "";     // id nella libreria (senza "lib:")
        const char* label  = "";     // nome del pad ("" = nome del campione)
        float tune = 0.0f, decay = 1.0f, level = 0.0f, pan = 0.0f, cutoff = 20000.0f;
        int   choke = 0;
        float reverb = 0.0f;
    };

    struct KitDef
    {
        const char* name;
        std::array<PadDef, numPads> pads;
    };

    const std::vector<KitDef>& factoryKits();

    //==========================================================================
    /** Kit di fabbrica e dell'utente (file XML con parametri e campioni). */
    class Presets
    {
    public:
        explicit Presets (DrumForgeAudioProcessor& p) : processor (p) {}

        static int numFactory();
        static juce::String factoryName (int i);
        void loadFactory (int i);
        juce::String currentName() const;

        juce::File userDir() const;
        juce::Array<juce::File> userPresets() const;
        bool save (const juce::String& name);
        bool load (const juce::File& file);

    private:
        DrumForgeAudioProcessor& processor;
    };

    //==========================================================================
    /** Il motore: voci che leggono i campioni dei pad. */
    class Engine
    {
    public:
        struct PadParams
        {
            const SampleData* sample = nullptr;
            float rate = 1.0f, decay = 1.0f, gainL = 1.0f, gainR = 1.0f, cutoff = 20000.0f, send = 0.0f, velSens = 0.6f;
            int choke = 0;
        };

        void prepare (double sampleRate);
        void trigger (int pad, float velocity, const PadParams& p);
        void stopAll();
        /** Somma le voci nel buffer stereo (e la mandata del riverbero in sendBuf). */
        void render (float* L, float* R, float* send, int n);
        int activeVoices() const;

    private:
        struct Voice
        {
            const SampleData* sample = nullptr;
            int pad = -1, choke = 0;
            double pos = 0.0, inc = 1.0;
            float gainL = 0.0f, gainR = 0.0f, send = 0.0f;
            float env = 1.0f, envCoef = 1.0f, chokeGain = 1.0f;
            bool choking = false, filtered = false;
            float g = 0.0f, k = 1.2f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
            uint32_t age = 0;
        };

        double sr = 48000.0;
        std::array<Voice, maxVoices> voices;
        uint32_t ageCounter = 0;
        float chokeCoef = 0.99f;
    };
}

//==============================================================================
class DrumForgeAudioProcessor : public juce::AudioProcessor
{
public:
    DrumForgeAudioProcessor();
    ~DrumForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }

    const juce::String getName() const override     { return "DrumForge"; }
    bool acceptsMidi() const override               { return true; }
    bool producesMidi() const override              { return false; }
    bool isMidiEffect() const override              { return false; }
    double getTailLengthSeconds() const override    { return 3.0; }

    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    df::Presets& getPresets()                       { return presets; }

    //==========================================================================
    // Pad (thread della UI)

    /** Assegna un campione al pad ("lib:<id>" o un file dei campioni dell'utente). */
    bool setPadSample (int pad, const juce::String& sampleName, const juce::String& label = {});
    juce::String getPadSample (int pad) const;
    juce::String getPadName (int pad) const;
    void setPadName (int pad, const juce::String& name);

    /** Suona un pad dall'interfaccia (al prossimo blocco audio). */
    void hitPad (int pad, float velocity = 0.9f);
    /** 0..1: quanto e' "acceso" il pad (l'interfaccia lo fa lampeggiare). */
    float getPadActivity (int pad) const;
    void decayPadActivity (float factor);

    /** Ricarica i campioni dei pad dallo stato (dopo un caricamento o un kit). */
    void refreshSamples();
    SampleStore& getSampleStore()                   { return store; }
    int getActiveVoices() const                     { return engine.activeVoices(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    df::Engine::PadParams padParams (int pad) const;

    df::Engine engine;
    df::Presets presets { *this };
    SampleStore store;
    std::array<std::atomic<const SampleData*>, df::numPads> padSamples {};
    std::array<std::atomic<float>, df::numPads> padActivity {};
    std::atomic<uint32_t> pendingHits { 0 };
    std::array<std::atomic<float>, df::numPads> pendingVelocity {};

    struct PadRaw { std::atomic<float>* tune, *decay, *level, *pan, *cutoff, *choke, *vel, *reverb; };
    std::array<PadRaw, df::numPads> raw {};
    std::atomic<float>* masterParam = nullptr, *driveParam = nullptr, *revSizeParam = nullptr, *revLevelParam = nullptr;

    juce::AudioBuffer<float> sendBuffer;
    juce::Reverb reverb;
    juce::Reverb::Parameters lastRev;
    double currentRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumForgeAudioProcessor)
};
