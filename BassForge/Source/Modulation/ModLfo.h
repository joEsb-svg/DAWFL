#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "ModSync.h"
#include "PerformerModulator.h"

namespace bf
{
    enum class LfoShape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Performer, Count };

    /**
     * LFO / Performer per voce. advance(n) fa avanzare la fase di n campioni (fase in double: nessuna deriva)
     * e restituisce il valore a fine intervallo; la matrice lo interpola linearmente nel sub-frame.
     *
     *  - Forme: Sine, Triangle, Saw su/giu', Square, Sample&Hold, Smooth Random, Performer.
     *  - Rate libero (Hz) oppure sync al tempo host; `setRateScale` = modulazione del rate (moltiplicativa).
     *  - Performer: la fase percorre i `length` step del pattern; in sync `division` e' la durata di UNO step,
     *    in modo libero rateHz sono cicli completi di pattern al secondo.
     *  - Fade-in (ms), offset di fase, uscita bipolare/unipolare.
     *  - Retrigger: fase a 0 a ogni nota; altrimenti fase casuale per voce (free-run).
     */
    class ModLfo
    {
    public:
        struct Params
        {
            LfoShape shape = LfoShape::Sine;
            float    rateHz = 2.0f;
            bool     sync = false;
            int      division = 5;          // indice in kSyncBeats
            float    phaseOffset = 0.0f;    // 0..1
            float    fadeInMs = 0.0f;
            bool     bipolar = true;
            bool     retrigger = true;
            // Performer (shape == Performer): modo di riproduzione, fine One-Shot, Hz per step
            PlayMode playMode = PlayMode::Forward;
            bool     perfEndHold = true;
            bool     perfRatePerStep = false;
        };

        void prepare(double sampleRate, std::uint32_t seed) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 44100.0;
            rng = seed != 0 ? seed : 0x1234567u;
            perf.prepare(sr);
            noteOn();
        }

        void setParams(const Params& p) noexcept   { prm = p; }
        void setTempo(double bpmValue) noexcept    { bpm = std::min(std::max(bpmValue, 20.0), 999.0); }
        void setRateScale(float s) noexcept        { rateScale = std::min(std::max(s, 1.0f / 64.0f), 64.0f); }
        void setPerformer(const PerformerPattern* pat) noexcept { performer = pat; perf.setPattern(pat); }

        void noteOn() noexcept
        {
            elapsed = 0.0;
            phase = prm.retrigger ? 0.0 : (double) rand01();
            held = nextRand(); prevRand = nextRand(); curRand = nextRand();
            if (prm.shape == LfoShape::Performer)
            {
                configurePerformer();
                perf.noteOn(true);
                perfVal = perf.value();
            }
            val = evaluate();
        }

        /** Avanza di n campioni, restituisce il valore a fine intervallo (e lo memorizza). */
        float advance(int n) noexcept
        {
            if (prm.shape == LfoShape::Performer)
            {
                configurePerformer();
                perfVal = perf.advance(n);
                elapsed += (double) n;
                val = evaluate();
                return val;
            }
            phase += cyclesPerSample() * (double) n;
            elapsed += (double) n;
            if (phase >= 1.0)
            {
                const int wraps = std::min((int) phase, 8);
                phase -= std::floor(phase);
                for (int i = 0; i < wraps; ++i) onWrap();
            }
            val = evaluate();
            return val;
        }

        float value() const noexcept { return val; }
        double currentPhase() const noexcept { return phase; }

    private:
        double cyclesPerSample() const noexcept
        {
            double f;
            if (! prm.sync)
                f = (double) prm.rateHz;
            else
                f = (bpm / 60.0) / (double) kSyncBeats[std::min(std::max(prm.division, 0), kNumSyncDivisions - 1)];
            f *= (double) rateScale;
            return std::min(std::max(f, 0.0), 2000.0) / sr;
        }

        std::uint32_t xorshift() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
        float nextRand() noexcept  { return (float) (std::int32_t) xorshift() * (1.0f / 2147483648.0f); }   // -1..1
        float rand01() noexcept    { return 0.5f * nextRand() + 0.5f; }

        void onWrap() noexcept
        {
            held = nextRand();
            prevRand = curRand; curRand = nextRand();
        }

        void configurePerformer() noexcept
        {
            PerformerModulator::Params p;
            p.mode = prm.playMode;  p.endHold = prm.perfEndHold;
            p.sync = prm.sync;      p.division = prm.division;
            p.rateHz = prm.rateHz;  p.rateIsPerStep = prm.perfRatePerStep;
            p.retrigger = prm.retrigger;  p.startPhase = prm.phaseOffset;
            perf.setParams(p);
            perf.setTempo(bpm);
            perf.setRateScale(rateScale);
        }

        float evaluate() const noexcept
        {
            float pp = (float) (phase + (double) prm.phaseOffset);
            pp -= std::floor(pp);
            float v = 0.0f;
            switch (prm.shape)
            {
                case LfoShape::Sine:         v = std::sin(6.28318530718f * pp); break;
                case LfoShape::Triangle:     { float t = pp + 0.25f; t -= std::floor(t); v = 1.0f - 4.0f * std::fabs(t - 0.5f); } break;
                case LfoShape::SawUp:        v = 2.0f * pp - 1.0f; break;
                case LfoShape::SawDown:      v = 1.0f - 2.0f * pp; break;
                case LfoShape::Square:       v = pp < 0.5f ? 1.0f : -1.0f; break;
                case LfoShape::SampleHold:   v = held; break;
                case LfoShape::SmoothRandom: { const float s = pp * pp * (3.0f - 2.0f * pp); v = prevRand + (curRand - prevRand) * s; } break;
                case LfoShape::Performer:    v = perfVal; break;
                default: break;
            }
            if (prm.fadeInMs > 0.0f)
            {
                const double fadeSamples = (double) prm.fadeInMs * 0.001 * sr;
                if (elapsed < fadeSamples) v *= (float) (elapsed / fadeSamples);
            }
            return prm.bipolar ? v : 0.5f * (v + 1.0f);
        }

        double sr = 44100.0, bpm = 120.0, phase = 0.0, elapsed = 0.0;
        float  rateScale = 1.0f, val = 0.0f, held = 0.0f, prevRand = 0.0f, curRand = 0.0f;
        std::uint32_t rng = 0x1234567u;
        Params prm{};
        const PerformerPattern* performer = nullptr;
        PerformerModulator perf{};
        float perfVal = 0.0f;
    };
}
