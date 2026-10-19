#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cstdint>
#include "SampleBank.h"

namespace bf
{
    enum class NoiseType : int { White = 0, Pink };
    enum class ToneFilterType : int { Lowpass = 0, Bandpass, Highpass };

    /**
     * Generatore di rumore + engine di campioni transienti, con filtro tonale a keytracking.
     *
     *  NOISE   : bianco o rosa (Paul Kellet raffinato, -3 dB/ottava), stereo con controllo di Width
     *            (0 = mono, 1 = L/R decorrelati). Livello smussato.
     *  SAMPLE  : one-shot (o loop) di un campione breve dal SampleBank, avviato al note-on.
     *            - Pitch: offset in semitoni + Pitch Track (0 = altezza fissa, 1 = segue la tastiera 1:1
     *              rispetto a C4 = MIDI 60). Interpolazione Hermite a 4 punti con resampling automatico
     *              (sampleRate del campione -> sampleRate host).
     *            - Loop: con Loop attivo il campione cicla tra loopStart/loopEnd finche' la nota e' tenuta;
     *              al note-off esce dal loop e suona fino alla fine (niente click di taglio).
     *            - Decay: inviluppo esponenziale opzionale (0 = nessuno) utile soprattutto in loop.
     *  FILTER  : filtro SVF (LP/BP/HP) applicato a rumore+campione. Il KEYTRACKING sposta il cutoff
     *            di keytrack * (nota - 60) / 12 ottave.
     *
     * Nessuna allocazione dopo prepare(). Il campione in riproduzione viene "catturato" al note-on:
     * cambiare slot a meta' nota influenza solo la nota successiva.
     * Il livello del campione NON include la velocity: la applica la voce sul bus finale.
     * Suggerito: chiamare il render dentro un juce::ScopedNoDenormals (lo fa gia' il processor).
     */
    class NoiseGenerator
    {
    public:
        struct Params
        {
            // --- Noise ---
            bool      noiseOn    = false;
            NoiseType noiseType  = NoiseType::White;
            float     noiseLevel = 0.1f;        // 0..1
            float     noiseWidth = 1.0f;        // 0..1

            // --- Sample ---
            bool  sampleOn         = false;
            int   sampleSlot       = 0;
            float sampleLevel      = 0.5f;      // 0..1
            float samplePitchSemi  = 0.0f;      // -24..+24
            float samplePitchTrack = 0.0f;      // 0..1
            bool  loop             = false;
            float loopStart        = 0.0f;      // 0..1 della lunghezza
            float loopEnd          = 1.0f;      // 0..1
            float decayMs          = 0.0f;      // 0 = disattivato

            // --- Filtro tonale ---
            bool           filterOn   = false;
            ToneFilterType filterType = ToneFilterType::Lowpass;
            float          cutoffHz   = 8000.0f;
            float          resonance  = 0.2f;   // 0..1
            float          keytrack   = 0.0f;   // 0..1 (1 = 1 ottava per ottava)
        };

        /** NON real-time. `seed` diverso per ogni voce => rumori decorrelati tra le voci. */
        void prepare(double sampleRate, int maxBlockSize, const SampleBank* bank, std::uint32_t seed);

        void setParams(const Params& newParams) noexcept;
        void noteOn(int midiNote, float velocity) noexcept;
        void noteOff() noexcept;
        void reset() noexcept;

        /** Un campione stereo (somma di rumore + campione, dopo il filtro tonale). */
        void tick(float& outL, float& outR) noexcept;

        bool isSamplePlaying() const noexcept { return playing; }

    private:
        struct Pink
        {
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            float process(float w) noexcept;
        };

        float nextWhite() noexcept;
        float readSample() const noexcept;
        void  updateDerived() noexcept;
        void  updateLoopBounds() noexcept;

        double sr = 44100.0;
        const SampleBank* bank = nullptr;
        Params p;

        // noise
        std::uint32_t rng = 0x2545F491u;
        Pink pinkL, pinkR;
        float noiseLevelSm = 0.0f, levelCoef = 0.01f;

        // sample
        const SampleData* cur = nullptr;
        double pos = 0.0, step = 1.0;
        bool   playing = false, released = false;
        int    loopStartI = 0, loopEndI = 0, loopLen = 1;
        float  decayGain = 1.0f, decayCoef = 1.0f;

        // filtro
        juce::dsp::StateVariableTPTFilter<float> filter;
        int   currentNote = 60;
    };
}
