#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

//==============================================================================
/**
    PolyForge: il synth polifonico di JLoop (pad, lead, plucks, accordi, stab).

    - 2 oscillatori (saw, quadra/pulse, triangolo, sinusoide) con unison fino a 7 voci e detune,
      sub-oscillatore e rumore, apertura stereo
    - filtro LP 24 / LP 12 / HP / BP con risonanza, drive, keytrack, inviluppo e velocity
    - inviluppi ADSR per ampiezza e filtro
    - LFO (5 forme, libero o a tempo) su pitch, filtro, volume e PWM
    - Poly (12 voci), Mono e Legato con glide
    - effetti: chorus, delay ping-pong a tempo, riverbero
    - preset di fabbrica per genere + preset dell'utente

    Tutto il DSP e' pensato per il telefono: oscillatori polyBLEP, coefficienti del filtro
    aggiornati ogni 16 campioni, nessuna allocazione nel thread audio.
*/
namespace pf
{
    inline constexpr int maxVoices  = 12;
    inline constexpr int maxUnison  = 7;
    inline constexpr int controlRate = 16;     // campioni tra un aggiornamento dei coefficienti e l'altro

    namespace ids
    {
        inline constexpr const char* osc1Wave = "osc1Wave";    inline constexpr const char* osc1Pw = "osc1Pw";
        inline constexpr const char* osc1Uni  = "osc1Uni";     inline constexpr const char* osc1Det = "osc1Det";
        inline constexpr const char* osc1Lvl  = "osc1Lvl";
        inline constexpr const char* osc2Wave = "osc2Wave";    inline constexpr const char* osc2Pw = "osc2Pw";
        inline constexpr const char* osc2Semi = "osc2Semi";    inline constexpr const char* osc2Fine = "osc2Fine";
        inline constexpr const char* osc2Uni  = "osc2Uni";     inline constexpr const char* osc2Det = "osc2Det";
        inline constexpr const char* osc2Lvl  = "osc2Lvl";
        inline constexpr const char* subLvl   = "subLvl";      inline constexpr const char* noiseLvl = "noiseLvl";
        inline constexpr const char* spread   = "spread";

        inline constexpr const char* fltType  = "fltType";     inline constexpr const char* cutoff = "cutoff";
        inline constexpr const char* reso     = "reso";        inline constexpr const char* fltDrive = "fltDrive";
        inline constexpr const char* keyTrack = "keyTrack";    inline constexpr const char* fltEnv = "fltEnv";
        inline constexpr const char* fltVel   = "fltVel";

        inline constexpr const char* ampA = "ampA"; inline constexpr const char* ampD = "ampD";
        inline constexpr const char* ampS = "ampS"; inline constexpr const char* ampR = "ampR";
        inline constexpr const char* fltA = "fltA"; inline constexpr const char* fltD = "fltD";
        inline constexpr const char* fltS = "fltS"; inline constexpr const char* fltR = "fltR";
        inline constexpr const char* velSens = "velSens";

        inline constexpr const char* lfoWave = "lfoWave";     inline constexpr const char* lfoRate = "lfoRate";
        inline constexpr const char* lfoSync = "lfoSync";     inline constexpr const char* lfoDiv = "lfoDiv";
        inline constexpr const char* lfoPitch = "lfoPitch";   inline constexpr const char* lfoCut = "lfoCut";
        inline constexpr const char* lfoAmp = "lfoAmp";       inline constexpr const char* lfoPwm = "lfoPwm";

        inline constexpr const char* mode  = "mode";          inline constexpr const char* glide = "glide";

        inline constexpr const char* chorus = "chorus";
        inline constexpr const char* dlyMix = "dlyMix";       inline constexpr const char* dlyTime = "dlyTime";
        inline constexpr const char* dlyFb  = "dlyFb";
        inline constexpr const char* revMix = "revMix";       inline constexpr const char* revSize = "revSize";
        inline constexpr const char* master = "master";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==========================================================================
    /** Parametri letti una volta per blocco dal thread audio. */
    struct BlockParams
    {
        int   wave[2] {}, unison[2] { 1, 1 };
        float pw[2] { 0.5f, 0.5f }, detune[2] {}, level[2] { 1.0f, 0.0f };
        float osc2Ratio = 1.0f, sub = 0.0f, noise = 0.0f, spread = 0.5f;
        int   filterType = 0;
        float cutoff = 8000.0f, reso = 0.0f, drive = 0.0f, keyTrack = 0.0f, fltEnv = 0.0f, fltVel = 0.0f;
        float ampA = 0.01f, ampD = 0.3f, ampS = 1.0f, ampR = 0.3f;
        float fltA = 0.01f, fltD = 0.3f, fltS = 0.0f, fltR = 0.3f;
        float velSens = 0.5f;
        float lfoPitch = 0.0f, lfoCut = 0.0f, lfoAmp = 0.0f, lfoPwm = 0.0f;
        int   mode = 0;
        float glide = 0.0f;

        int   lfoWave = 0, lfoDiv = 2;
        bool  lfoSync = false;
        float lfoRate = 2.0f;

        float chorus = 0.0f, dlyMix = 0.0f, dlyFb = 0.35f, revMix = 0.0f, revSize = 0.5f, master = 1.0f;
        int   dlyTime = 2;
    };

    //==========================================================================
    /** Inviluppo ADSR (attacco lineare, decadimento e rilascio esponenziali). */
    struct Adsr
    {
        enum Stage { idle, attack, decay, sustain, release };

        void set (double sr, float a, float d, float s, float r)
        {
            attackInc = (float) (1.0 / (juce::jmax (0.0005, (double) a) * sr));
            decayCoef = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) d) * sr));    // -40 dB nel tempo indicato
            releaseCoef = (float) std::exp (-4.6 / (juce::jmax (0.001, (double) r) * sr));
            sustainLevel = s;
        }

        void noteOn (bool legato)   { stage = attack; if (! legato) value = juce::jmin (value, 1.0f); }
        void noteOff()              { if (stage != idle) stage = release; }
        void reset()                { stage = idle; value = 0.0f; }
        bool isActive() const       { return stage != idle; }

        float next() noexcept
        {
            switch (stage)
            {
                case attack:
                    value += attackInc;
                    if (value >= 1.0f) { value = 1.0f; stage = decay; }
                    break;
                case decay:
                    value = sustainLevel + (value - sustainLevel) * decayCoef;
                    if (value - sustainLevel < 0.0005f) { value = sustainLevel; stage = sustain; }
                    break;
                case sustain:
                    value = sustainLevel;
                    break;
                case release:
                    value *= releaseCoef;
                    if (value < 0.00005f) { value = 0.0f; stage = idle; }
                    break;
                case idle:
                default:
                    break;
            }
            return value;
        }

        Stage stage = idle;
        float value = 0.0f, attackInc = 0.01f, decayCoef = 0.999f, releaseCoef = 0.999f, sustainLevel = 1.0f;
    };

    //==========================================================================
    /** Filtro State-Variable (TPT), stereo, con uscite LP / BP / HP. */
    struct Svf
    {
        void setCoefficients (float g_, float k_) noexcept
        {
            g = g_; k = k_;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void reset() noexcept   { s1 = s2 = 0.0f; }

        inline float process (float x, int type) noexcept
        {
            const float v3 = x - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            if (type == 2) return x - k * v1 - v2;   // HP
            if (type == 3) return v1 * k;            // BP (guadagno ~1 al picco)
            return v2;                               // LP
        }

        float g = 0.5f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    };

    //==========================================================================
    /** Una voce: due oscillatori con unison, sub, rumore, filtro e due inviluppi. */
    class Voice
    {
    public:
        void prepare (double sampleRate)   { sr = sampleRate; reset(); }

        void reset()
        {
            amp.reset(); flt.reset();
            for (auto& f : filtersL) f.reset();
            for (auto& f : filtersR) f.reset();
            active = false;
        }

        /** @param glideFrom  frequenza da cui scivolare (0 = nessun glide) */
        void start (int midiNote, float vel, float glideFrom, bool legato, uint32_t seed);
        void stop()                 { amp.noteOff(); flt.noteOff(); }
        void kill()                 { reset(); }
        bool isActive() const       { return active; }
        bool isReleasing() const    { return amp.stage == Adsr::release; }
        int  getNote() const        { return note; }
        float getFrequency() const  { return freq; }
        void setTarget (int midiNote, float glideTime);
        uint32_t age = 0;

        /** Aggiunge la voce a L/R. lfo: valore attuale (-1..1). */
        void render (float* L, float* R, int n, const BlockParams& p, float lfo, float glideCoef);

    private:
        static inline float polyBlep (float t, float dt) noexcept
        {
            if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
            return 0.0f;
        }

        static inline float osc (int wave, float t, float dt, float pw) noexcept
        {
            switch (wave)
            {
                case 0:  return 2.0f * t - 1.0f - polyBlep (t, dt);                                   // saw
                case 1:                                                                                // quadra / pulse
                {
                    float y = t < pw ? 1.0f : -1.0f;
                    y += polyBlep (t, dt);
                    float t2 = t + 1.0f - pw; if (t2 >= 1.0f) t2 -= 1.0f;
                    y -= polyBlep (t2, dt);
                    return y - (2.0f * pw - 1.0f);   // senza componente continua (DC)
                }
                case 2:  return 1.0f - 4.0f * std::abs (t - 0.5f);                                    // triangolo
                default: return std::sin (juce::MathConstants<float>::twoPi * t);                     // sinusoide
            }
        }

        double sr = 48000.0;
        bool active = false;
        int note = 60;
        float velocity = 1.0f, freq = 261.6f, targetFreq = 261.6f;
        Adsr amp, flt;
        std::array<std::array<float, maxUnison>, 2> phase {};
        float subPhase = 0.0f;
        std::array<Svf, 2> filtersL, filtersR;   // 2 stadi (24 dB) per canale
        int counter = 0;
        uint32_t noiseState = 22222u;
    };

    //==========================================================================
    /** Il motore del synth: voci, LFO, effetti. */
    class Synth
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void render (juce::AudioBuffer<float>& buffer, int start, int n, const BlockParams& p,
                     double bpm, double ppq, bool playing);

    private:
        Voice* findVoice();
        float lfoValue (int wave, float phase01);

        double sr = 48000.0;
        std::array<Voice, maxVoices> voices;
        uint32_t ageCounter = 0, seed = 1234567u;
        std::vector<int> heldNotes;      // per Mono / Legato (ultima nota premuta)
        std::vector<float> velocities;
        float noteVelocity = 1.0f, currentGlide = 0.0f, lastFreq = 0.0f;
        int currentMode = 0;
        juce::Reverb::Parameters lastRev;
        double lfoPhase = 0.0;
        float lfoHold = 0.0f, lfoLast = 0.0f;
        uint32_t lfoRandom = 98765u;

        // Effetti
        std::vector<float> chorusL, chorusR, delayL, delayR;
        int chorusPos = 0, delayPos = 0;
        double chorusPhase = 0.0;
        float delayLp[2] { 0.0f, 0.0f };
        juce::Reverb reverb;
        juce::AudioBuffer<float> scratch;
    };

    //==========================================================================
    /** Preset: di fabbrica (nel codice) e dell'utente (file XML nella cartella dell'app). */
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

        void set (const juce::String& id, float value);   // valore "reale" (Hz, secondi, indice...)
        void init();

    private:
        juce::AudioProcessorValueTreeState& apvts;
    };
}

//==============================================================================
class PolyForgeAudioProcessor : public juce::AudioProcessor
{
public:
    PolyForgeAudioProcessor();
    ~PolyForgeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }

    const juce::String getName() const override     { return "PolyForge"; }
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

    pf::Presets& getPresets()                       { return presets; }

    juce::AudioProcessorValueTreeState apvts;

private:
    pf::BlockParams readParams();
    float raw (const char* id) const                { return params.at (id)->load(); }

    pf::Synth synth;
    pf::Presets presets { apvts };
    std::map<juce::String, std::atomic<float>*> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyForgeAudioProcessor)
};
