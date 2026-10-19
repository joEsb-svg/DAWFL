#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdint>
#include "SynthSound.h"
#include "Parameters/ParamCache.h"
#include "DSP/Wavetable.h"
#include "DSP/WavetableOscillator.h"
#include "Modulation/ModMonitor.h"
#include "DSP/SubOscillator.h"
#include "DSP/NoiseGenerator.h"
#include "DSP/SampleBank.h"
#include "DSP/DualFilterEngine.h"
#include "Modulation/VoiceModulation.h"

namespace bf
{
    // Una voce: 3 osc wavetable + noise/sample -> Dual Filter Engine -> amp (Env1).
    // Il SUB ha due routing:
    //   - normale   : sommato prima dei filtri insieme agli oscillatori;
    //   - DIRECT-OUT: scritto nel bus diretto (mono) del processor, sommato DOPO gli FX master.
    //
    // Modulazione: VoiceModulation (4 env AHDSR per-campione, 4 LFO/Performer, MIDI, 12 macro, matrice a
    // 12 connessioni) valutata a SUB-FRAME di 16 campioni; pitch, wavetable position e livelli degli osc e
    // l'ampiezza sono applicati con RAMPE LINEARI per-campione (sample-accurate, nessuno zipper/jitter).
    // renderNextBlock() non alloca: tutto lo stato e' pre-allocato in prepare()/costruttore.
    class BassVoice : public juce::SynthesiserVoice
    {
    public:
        BassVoice(const ParamCache& params, const WavetableBank& bank, const SampleBank& samples, std::uint32_t noiseSeed);

        void prepare(double sampleRate, int maxBlockSize);   // NON real-time

        /** Bus mono (1 canale) su cui scrivere il Sub in Direct-Out. nullptr = nessun bus (fallback post-filtri). */
        void setDirectBus(juce::AudioBuffer<float>* bus) noexcept { directBus = bus; }

        /** Pattern del Performer (posseduti dal processor). nullptr = Performer a 0. */
        void setPerformerBank(const PerformerBank* b) noexcept { performers = b; vm.setPerformers(b); }
        /** BPM dell'host (aggiornato dal processor a ogni blocco). nullptr = 120. */
        void setTempoSource(const std::atomic<float>* t) noexcept { tempo = t; }
        /** Mod wheel globale letto al note-on. */
        void setSharedMidi(const SharedMidiState* s) noexcept { shared = s; }
        /** Opzionale: pubblica la modulazione corrente verso la GUI (anelli di modulazione). */
        void setModMonitor(ModMonitor* m) noexcept { monitor = m; }

        bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*>(s) != nullptr; }
        void startNote(int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int newValue) override;
        void controllerMoved(int controllerNumber, int newValue) override;      // CC1 mod wheel, CC74 MPE slide
        void aftertouchChanged(int newAftertouchValue) override;                // poly aftertouch
        void channelPressureChanged(int newChannelPressureValue) override;      // channel pressure / MPE pressure
        void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;

        /** Per i test / la UI di debug. */
        const VoiceModulation& modulation() const noexcept { return vm; }

    private:
        static constexpr int kControlBlock = VoiceModulation::kSubFrame;

        void configureModulation() noexcept;             // legge i parametri APVTS -> VoiceModulation (1x per blocco host)
        void renderControlBlock(float* outL, float* outR, float* direct, int len) noexcept;
        SubOscillator::Params   makeSubParams(float levelMod) const noexcept;
        NoiseGenerator::Params  makeNoiseParams(float levelMod) const noexcept;

        const ParamCache&    pc;
        const WavetableBank& bank;
        const SampleBank&    samples;
        std::uint32_t        noiseSeed;
        juce::AudioBuffer<float>* directBus = nullptr;
        const PerformerBank*       performers = nullptr;
        const std::atomic<float>*  tempo = nullptr;
        const SharedMidiState*     shared = nullptr;
        ModMonitor*                monitor = nullptr;

        double sr = 44100.0;

        VoiceModulation  vm;
        DualFilterEngine filterEngine;                       // ZDF 12/24/48, Comb, Vowel; Serial/Parallel/Morph
        std::array<float, kControlBlock> bufL{}, bufR{}, subBuf{};   // scratch per-blocco (nessuna allocazione)

        SubOscillator  sub;
        NoiseGenerator noiseGen;

        std::array<WavetableOscillator, kNumOsc> wtOsc;       // Step 2: Hermite 3D morph + warp + unison
        std::array<std::array<float, kControlBlock>, kNumOsc> oscL{}, oscR{};   // uscita stereo per osc (scratch)

        int   currentNote = -1;
        float velocity = 0.f;
        float prevBendSemis = 0.f;       // pitch bend a fine sub-frame precedente (per la rampa di pitch)
    };
}
