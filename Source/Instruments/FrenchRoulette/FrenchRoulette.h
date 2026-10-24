#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "../Common/ForgeFx.h"

//==============================================================================
/**
    FRENCH ROULETTE: synth ibrido per bassi "choppati", lead distorti, arpeggi lo-fi (electro francese 2007-2012).

    - OSC 1 analogico: saw / square / pulse con PWM e deriva (drift) dell'intonazione
    - OSC 2 digitale: 8 wavetable (FM, metalli, formanti, bit...) con morph WAVE e FM da OSC 1
    - SUB dedicato (-1 / -2 ottave) con drive proprio: non passa da filtro e crunch, il basso resta pulito
    - Filtro ladder 24 dB (anche 12 dB, passa-banda, passa-alto) con risonanza che "urla" e inviluppo veloce
    - PUMP: sidechain simulato a tempo su volume e filtro
    - CRUNCH: valvola -> bitcrusher/riduttore di campionamento (blend) -> clip soft/hard
    - STEP MATRIX a 16 step: gate, cutoff, crush e pitch (con glide) per step, a tempo col progetto; Random Chop
*/
namespace fr
{
    inline constexpr int numSteps   = 16;
    inline constexpr int maxVoices  = 6;
    inline constexpr int controlRate = 16;
    inline constexpr int numTables  = 8;
    inline constexpr int maxChunk   = 2048;

    inline const juce::StringArray& tableNames()
    {
        static const juce::StringArray n { "FM METAL", "FM GROWL", "DIGI SAW", "FORMANT", "HOLLOW", "BITWAVE", "HARD SYNC", "FOLD" };
        return n;
    }

    /** Id dei parametri della matrice: lane 'g' gate, 'c' cutoff, 'x' crush, 'p' pitch, 'l' glide. */
    inline juce::String stepId (char lane, int step)   { return juce::String ("m") + lane + juce::String (step); }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct BlockParams
    {
        int o1Wave = 0, o1Oct = 0, o2Table = 0, o2Semi = 0, subOct = 1;
        float o1Pw = 0.5f, o1Pwm = 0.0f, o1Drift = 0.2f, o1Level = 0.8f;
        float o2Wave = 0.0f, o2Fm = 0.0f, o2Fine = 0.0f, o2Level = 0.0f;
        float subLevel = 0.0f, subDrive = 0.0f;
        int fType = 0; float cutoff = 2000.0f, reso = 0.2f, fEnv = 0.4f, fKey = 0.3f, fA = 0.001f, fD = 0.3f, fS = 0.0f, fR = 0.2f;
        float aA = 0.002f, aD = 0.5f, aS = 1.0f, aR = 0.08f;
        int mode = 0; float glide = 0.0f;
        int lfoShape = 0, lfoDiv = 3; float lfoCut = 0.0f, lfoWave = 0.0f, lfoPitch = 0.0f;
        float pump = 0.0f, pumpShape = 0.5f, pumpFilter = 0.0f; int pumpDiv = 1;
        float crunch = 0.0f, tube = 0.0f, bits = 16.0f, rate = 0.0f, clipDrive = 0.0f; int clipMode = 0;
        bool matOn = false; int matLen = 16, matRate = 1, retrig = 1;
        float swing = 0.0f, gateLen = 0.6f, matGlide = 0.06f, matCut = 0.0f, matCrush = 0.0f;
        std::array<bool, numSteps> gate {}, glideStep {};
        std::array<float, numSteps> cut {}, crush {};
        std::array<int, numSteps> pitch {};
        forge::FxParams fx;
    };

    //==========================================================================
    /** Filtro ladder a 4 poli (stile Moog, ZDF) con saturazione: risonanza aggressiva, autooscilla in cima. */
    struct Ladder
    {
        void reset() noexcept               { s.fill (0.0f); }
        void set (float g_, float k_) noexcept
        {
            g = g_; G = g / (1.0f + g); k = k_;
        }
        float process (float x, int type) noexcept;

        std::array<float, 4> s {};
        float g = 0.5f, G = 0.33f, k = 0.0f;
    };

    struct Env
    {
        enum Stage { idle, attack, decay, sustain, release };
        void set (double sr, float a, float d, float s, float r) noexcept;
        void noteOn() noexcept      { stage = attack; }
        void noteOff() noexcept     { if (stage != idle) stage = release; }
        void reset() noexcept       { stage = idle; value = 0.0f; }
        float next() noexcept;
        Stage stage = idle;
        float value = 0.0f, attackInc = 0.01f, decayCoef = 0.99f, releaseCoef = 0.99f, sustainLevel = 1.0f;
        float lastA = -1, lastD = -1, lastS = -1, lastR = -1; double lastSr = 0;
    };

    //==========================================================================
    /** Modulazioni comuni a tutte le voci, calcolate dalla matrice e dagli LFO ogni 16 campioni. */
    struct Mods
    {
        float cutOct = 0.0f;      // spostamento del cutoff in ottave
        float pitch = 0.0f;       // semitoni (matrice + LFO)
        float wave = 0.0f;        // spostamento del morph di OSC 2
        bool retrigFilter = false, retrigAmp = false;
    };

    class Voice
    {
    public:
        void prepare (double sampleRate)    { sr = sampleRate; reset(); }
        void reset();
        void start (int note, float vel, float glideFrom, bool retrigger);
        void setTarget (int n)              { note = n; targetPitch = (float) n; }
        void stop()                         { amp.noteOff(); fenv.noteOff(); }
        bool isActive() const               { return amp.stage != Env::idle; }
        bool isReleasing() const            { return amp.stage == Env::release; }
        int getNote() const                 { return note; }
        float getPitch() const              { return pitch; }
        uint32_t age = 0;

        /** Scrive 'n' campioni: 'main' passa da filtro e crunch, 'sub' va diretto. */
        void render (float* main, float* sub, int n, const BlockParams& p, const Mods& m, float glideCoef);

    private:
        double sr = 48000.0;
        int note = 48;
        float velocity = 1.0f, pitch = 48.0f, targetPitch = 48.0f;
        double ph1 = 0.0, ph2 = 0.0, phSub = 0.0;
        float drift = 0.0f, driftTarget = 0.0f, noteDetune = 0.0f, pwmPhase = 0.0f;
        uint32_t rnd = 12345u;
        Env amp, fenv;
        Ladder filter;
    };

    class Synth
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void killAll();
        void render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p, double bpm, double ppq, bool playing);

        /** Step della matrice che sta suonando (-1 = ferma), per la UI. */
        std::atomic<int> currentStep { -1 };

    private:
        Voice* findVoice();
        void crunchBlock (float* x, int n, const BlockParams& p, float extraCrush);

        double sr = 48000.0;
        std::array<Voice, maxVoices> voices;
        uint32_t ageCounter = 0;
        std::vector<int> heldNotes;
        int currentMode = 0;
        float currentGlide = 0.0f, lastPitch = -1.0f;
        std::vector<float> mainBuf, subBuf;

        // Tempo interno (quando il progetto e' fermo la matrice parte dalla nota)
        double freeBeats = 0.0;
        bool wasPlaying = false;
        int lastStep = -1;
        float stepPitch = 0.0f, gateGain = 1.0f, cutSmooth = 0.0f, crushSmooth = 0.0f;
        double lfoPhase = 0.0; float lfoHold = 0.0f; uint32_t lfoRnd = 777u;

        // Crunch
        float holdSample = 0.0f, holdCount = 0.0f, dcX = 0.0f, dcY = 0.0f;
        forge::Effects fx;
    };

    //==========================================================================
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

        /** "Random Chop": nuovo ritmo, salti d'ottava, cutoff e crush per step. */
        void randomChop (uint32_t seed);

    private:
        juce::AudioProcessorValueTreeState& apvts;
    };
}

//==============================================================================
class FrenchRouletteAudioProcessor : public juce::AudioProcessor
{
public:
    FrenchRouletteAudioProcessor();

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
    const juce::String getName() const override     { return "French Roulette"; }
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

    fr::Presets& getPresets()                       { return presets; }
    int getCurrentStep() const                      { return synth.currentStep.load(); }
    juce::AudioProcessorValueTreeState apvts;

private:
    fr::BlockParams readParams();
    std::atomic<float>* ptr (const juce::String& id)  { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; }

    fr::Synth synth;
    fr::Presets presets { apvts };

    struct NamedPtr { std::atomic<float>* p = nullptr; };
    std::vector<std::atomic<float>*> stepPtr;   // 5 lane x 16 step: g, c, x, p, l
    std::atomic<float> *o1Wave {}, *o1Oct {}, *o1Pw {}, *o1Pwm {}, *o1Drift {}, *o1Level {},
                       *o2Table {}, *o2Wave {}, *o2Fm {}, *o2Semi {}, *o2Fine {}, *o2Level {},
                       *subOct {}, *subLevel {}, *subDrive {},
                       *fType {}, *cutoff {}, *reso {}, *fEnv {}, *fKey {}, *fA {}, *fD {}, *fS {}, *fR {},
                       *aA {}, *aD {}, *aS {}, *aR {}, *mode {}, *glide {},
                       *lfoShape {}, *lfoDiv {}, *lfoCut {}, *lfoWave {}, *lfoPitch {},
                       *pump {}, *pumpDiv {}, *pumpShape {}, *pumpFilter {},
                       *crunch {}, *tube {}, *bits {}, *rate {}, *clipMode {}, *clipDrive {},
                       *matOn {}, *matLen {}, *matRate {}, *retrig {}, *swing {}, *gateLen {}, *matGlide {}, *matCut {}, *matCrush {},
                       *chorus {}, *dlyMix {}, *dlyTime {}, *dlyFb {}, *revMix {}, *revSize {}, *master {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrenchRouletteAudioProcessor)
};
