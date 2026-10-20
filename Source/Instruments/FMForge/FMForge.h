#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <map>
#include <vector>
#include "../PolyForge/PolyForge.h"   // pf::Adsr, pf::Svf
#include "../Common/ForgeFx.h"

//==============================================================================
/**
    FMForge: synth FM a 4 operatori (stile Yamaha DX/TX) per piani elettrici, campane,
    bassi FM, brass, organi, plucks, metalli.

    - 4 operatori sinusoidali: rapporto di frequenza, fine, livello, velocity e inviluppo ADSR ciascuno
    - 8 algoritmi (come i synth FM a 4 operatori), feedback sull'operatore 4
    - "Timbro": quanto i modulatori colorano il suono (piu' alto = piu' brillante)
    - LFO (vibrato, tremolo, timbro), Poly/Mono/Legato con glide, filtro "Tono"
    - effetti: chorus, delay a tempo, riverbero
*/
namespace fm
{
    inline constexpr int numOps = 4;
    inline constexpr int maxVoices = 12;
    inline constexpr int numAlgorithms = 8;
    inline constexpr int controlRate = 16;

    /** Per ogni algoritmo: quali operatori modulano ciascun operatore (bit) e quali sono portanti (uscita). */
    struct Algorithm { std::array<uint8_t, numOps> modulators; uint8_t carriers; };

    inline const Algorithm& algorithm (int index)
    {
        // operatori 1..4 = bit 0..3
        static const Algorithm algos[numAlgorithms] =
        {
            { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 },   // 1: 4 > 3 > 2 > 1
            { { 0b0010, 0b1100, 0,      0 }, 0b0001 },   // 2: (3 + 4) > 2 > 1
            { { 0b1010, 0b0100, 0,      0 }, 0b0001 },   // 3: 3 > 2 > 1, 4 > 1
            { { 0b0110, 0,      0b1000, 0 }, 0b0001 },   // 4: 4 > 3 > 1, 2 > 1
            { { 0b0010, 0,      0b1000, 0 }, 0b0101 },   // 5: 2 > 1, 4 > 3 (due coppie)
            { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 },   // 6: 4 > 1, 2, 3
            { { 0,      0,      0b1000, 0 }, 0b0111 },   // 7: 4 > 3, piu' 1 e 2
            { { 0,      0,      0,      0 }, 0b1111 },   // 8: tutti in uscita (additivo, organo)
        };
        return algos[juce::jlimit (0, numAlgorithms - 1, index)];
    }

    inline const juce::StringArray& ratioNames()
    {
        static const juce::StringArray names { "0.5", "1", "1.41", "1.5", "2", "2.5", "3", "3.5", "4", "4.5", "5", "6", "7",
                                               "8", "9", "10", "11", "12", "13", "14", "15", "16" };
        return names;
    }

    inline float ratioValue (int index)  { return ratioNames()[juce::jlimit (0, ratioNames().size() - 1, index)].getFloatValue(); }

    inline juce::String opId (int op, const char* what)   { return "op" + juce::String (op + 1) + what; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct OpParams
    {
        float ratio = 1.0f, level = 0.0f, vel = 0.5f, a = 0.002f, d = 1.0f, s = 0.8f, r = 0.3f;
    };

    struct BlockParams
    {
        std::array<OpParams, numOps> ops;
        int algo = 0;
        float feedback = 0.0f, bright = 1.0f, tone = 12000.0f;
        int lfoWave = 0, lfoDiv = 2; bool lfoSync = false; float lfoRate = 5.0f;
        float lfoVib = 0.0f, lfoTrem = 0.0f, lfoBright = 0.0f;
        int mode = 0; float glide = 0.0f;
        forge::FxParams fx;
    };

    //==========================================================================
    class Voice
    {
    public:
        void prepare (double sampleRate)    { sr = sampleRate; reset(); }
        void reset()                        { for (auto& e : env) e.reset(); active = false; fb1 = fb2 = 0.0f; }

        void start (int note, float vel, float glideFrom, bool retrigger);
        void setTarget (int note);
        void stop()                         { for (auto& e : env) e.noteOff(); }
        bool isActive() const               { return active; }
        bool isReleasing() const            { return env[0].stage == pf::Adsr::release; }
        int  getNote() const                { return note; }
        float getFrequency() const          { return freq; }
        uint32_t age = 0;

        void render (float* out, int n, const BlockParams& p, float lfo, float glideCoef);

    private:
        double sr = 48000.0;
        bool active = false;
        int note = 60;
        float velocity = 1.0f, freq = 261.6f, targetFreq = 261.6f;
        std::array<pf::Adsr, numOps> env;
        std::array<float, numOps> phase {};
        float fb1 = 0.0f, fb2 = 0.0f;
    };

    class Synth
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void killAll()  { heldNotes.clear(); for (auto& v : voices) v.reset(); fx.reset(); }
        void render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p, double bpm, double ppq, bool playing);

    private:
        Voice* findVoice();
        double sr = 48000.0;
        std::array<Voice, maxVoices> voices;
        uint32_t ageCounter = 0;
        std::vector<int> heldNotes;
        int currentMode = 0;
        float currentGlide = 0.0f, lastFreq = 0.0f;
        double lfoPhase = 0.0;
        float lfoHold = 0.0f;
        uint32_t lfoRandom = 4242u;
        std::array<pf::Svf, 2> toneFilter;
        std::vector<float> mono;
        forge::Effects fx;
    };

    class Presets
    {
    public:
        explicit Presets (juce::AudioProcessorValueTreeState& s) : apvts (s) {}
        static int numFactory();
        static juce::String factoryName (int i);
        void loadFactory (int i);
        juce::String currentName() const    { return apvts.state.getProperty ("presetName", "Init").toString(); }
        juce::File userDir() const;
        juce::Array<juce::File> userPresets() const;
        bool save (const juce::String& name);
        bool load (const juce::File& file);
        void set (const juce::String& id, float value);
        void init();

    private:
        juce::AudioProcessorValueTreeState& apvts;
    };
}

//==============================================================================
class FMForgeAudioProcessor : public juce::AudioProcessor
{
public:
    FMForgeAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override                           { synth.killAll(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }
    const juce::String getName() const override     { return "FMForge"; }
    bool acceptsMidi() const override               { return true; }
    bool producesMidi() const override              { return false; }
    bool isMidiEffect() const override              { return false; }
    double getTailLengthSeconds() const override    { return 4.0; }
    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    fm::Presets& getPresets()                       { return presets; }
    juce::AudioProcessorValueTreeState apvts;

private:
    fm::BlockParams readParams();
    std::atomic<float>* ptr (const juce::String& id)  { return apvts.getRawParameterValue (id); }

    fm::Synth synth;
    fm::Presets presets { apvts };

    // Puntatori ai parametri, cercati una volta sola (niente stringhe nel thread audio)
    std::array<std::array<std::atomic<float>*, 8>, fm::numOps> opPtr {};
    std::atomic<float> *algoP {}, *feedbackP {}, *brightP {}, *toneP {}, *lfoWaveP {}, *lfoRateP {}, *lfoSyncP {}, *lfoDivP {},
                       *lfoVibP {}, *lfoTremP {}, *lfoBrightP {}, *modeP {}, *glideP {}, *chorusP {}, *dlyMixP {}, *dlyTimeP {},
                       *dlyFbP {}, *revMixP {}, *revSizeP {}, *masterP {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FMForgeAudioProcessor)
};
