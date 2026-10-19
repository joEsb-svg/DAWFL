#pragma once
#include <array>
#include <cstdint>
#include "ModEnvelope.h"
#include "ModLfo.h"
#include "MidiModulators.h"
#include "ModulationMatrix.h"
#include "PerformerPattern.h"

namespace bf
{
    using PerformerBank = PerformerBankT<kNumLfo>;

    /**
     * Tutta la modulazione di UNA voce: 4 envelope AHDSR, 4 LFO/Performer, modulatori MIDI, 12 macro e la
     * matrice. Senza JUCE, senza allocazioni: il thread audio chiama solo advance().
     *
     * Ordine di calcolo per sub-frame (advance):
     *   1. envelope (per-campione, nel buffer envBuffer) e LFO (valore a fine frame) usando tempo/curva/rate
     *      modulati dall'ULTIMA valutazione della matrice  -> nessun loop algebrico;
     *   2. modulatori MIDI lisciati, macro, sorgenti statiche (velocity, keytrack, random);
     *   3. matrice: somma per destinazione, con rampa dal frame precedente.
     * Le sorgenti Env/LFO sono quindi in anticipo di 0 frame per le destinazioni audio; solo la modulazione
     * di Env Time/Curve e LFO Rate ha 1 sub-frame (~0.36 ms) di ritardo.
     */
    class VoiceModulation
    {
    public:
        static constexpr int kSubFrame = 16;

        void prepare(double sampleRate, std::uint32_t seed) noexcept
        {
            sr = sampleRate;
            rng = seed != 0 ? seed : 0xC0FFEEu;
            for (auto& e : env) e.prepare(sampleRate);
            for (int i = 0; i < kNumLfo; ++i) lfos[(size_t) i].prepare(sampleRate, seed * 2654435761u + (std::uint32_t) i * 40503u + 1u);
            midiMod.prepare(sampleRate);
            hardReset();
        }

        ModEnvelope&      envelope(int i) noexcept { return env[(size_t) i]; }
        ModLfo&           lfo(int i) noexcept      { return lfos[(size_t) i]; }
        MidiModulators&   midi() noexcept          { return midiMod; }
        ModulationMatrix& matrix() noexcept        { return mx; }
        const ModulationMatrix& matrix() const noexcept { return mx; }

        void setTempo(double bpm) noexcept { for (auto& l : lfos) l.setTempo(bpm); }
        void setMacro(int i, float v01) noexcept { if (i >= 0 && i < kNumMacro) mx.setSource(SrcMacro1 + i, v01); }
        void setPerformers(const PerformerBank* bank) noexcept
        {
            for (int i = 0; i < kNumLfo; ++i) lfos[(size_t) i].setPerformer(bank != nullptr ? &bank->p[(size_t) i] : nullptr);
        }

        void noteOn(int midiNote, float velocity, int pitchWheel14) noexcept
        {
            mx.setSource(SrcVelocity, velocity);
            mx.setSource(SrcKeytrack, std::min(std::max((float) (midiNote - 60) / 48.0f, -1.0f), 1.0f));
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            mx.setSource(SrcRandom, (float) (std::int32_t) rng * (1.0f / 2147483648.0f));

            midiMod.setPitchBend14(pitchWheel14);
            midiMod.snap();

            for (int i = 0; i < kNumEnv; ++i) { env[(size_t) i].setTimeScale(1.0f); env[(size_t) i].setCurveOffset(0.0f); env[(size_t) i].noteOn(); }
            for (int i = 0; i < kNumLfo; ++i) { lfos[(size_t) i].setRateScale(1.0f); lfos[(size_t) i].noteOn(); }

            publishSources();
            mx.evaluate();
            mx.snap();
            for (auto& b : envBuf) b.fill(0.0f);
        }

        void noteOff() noexcept { for (auto& e : env) e.noteOff(); }

        void hardReset() noexcept
        {
            for (auto& e : env) e.reset();
            midiMod.reset();
            mx.clearValues();
            for (auto& b : envBuf) b.fill(0.0f);
        }

        /** Avanza di n <= kSubFrame campioni. */
        void advance(int n) noexcept
        {
            n = std::min(std::max(n, 1), kSubFrame);

            for (int i = 0; i < kNumEnv; ++i)
            {
                env[(size_t) i].setTimeScale(std::exp2(mx.value(DstEnv1Time + i) * modscale::kEnvTimeOct));
                env[(size_t) i].setCurveOffset(mx.value(DstEnv1Curve + i));
                env[(size_t) i].render(envBuf[(size_t) i].data(), n);
            }
            for (int i = 0; i < kNumLfo; ++i)
            {
                lfos[(size_t) i].setRateScale(std::exp2(mx.value(DstLfo1Rate + i) * modscale::kLfoRateOct));
                lfos[(size_t) i].advance(n);
            }
            midiMod.advance(n);
            publishSources();
            mx.evaluate();
        }

        /** Envelope i: valori per-campione dell'ULTIMO advance (n valori). Env1 = envelope d'ampiezza. */
        const float* envBuffer(int i) const noexcept { return envBuf[(size_t) i].data(); }
        bool ampActive() const noexcept { return env[0].isActive(); }

    private:
        void publishSources() noexcept
        {
            for (int i = 0; i < kNumEnv; ++i) mx.setSource(SrcEnv1 + i, env[(size_t) i].level());
            for (int i = 0; i < kNumLfo; ++i) mx.setSource(SrcLfo1 + i, lfos[(size_t) i].value());
            mx.setSource(SrcPitchBend, midiMod.bend());
            mx.setSource(SrcAftertouch, midiMod.pressure());
            mx.setSource(SrcModWheel, midiMod.modWheel());
            mx.setSource(SrcMpeSlide, midiMod.slide());
        }

        double sr = 44100.0;
        std::uint32_t rng = 0xC0FFEEu;
        std::array<ModEnvelope, kNumEnv> env{};
        std::array<ModLfo, kNumLfo> lfos{};
        MidiModulators midiMod{};
        ModulationMatrix mx{};
        std::array<std::array<float, kSubFrame>, kNumEnv> envBuf{};
    };
}
