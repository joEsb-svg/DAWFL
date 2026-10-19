#pragma once
#include <algorithm>
#include <cmath>
#include "ModSync.h"
#include "PerformerPattern.h"

namespace bf
{
    /**
     * Performer / Stepper a 32 step (stile NI Massive) per UNA voce.
     *
     *  Velocita'  : sync al BPM host (la `division` e' la durata di UNO step) oppure in Hz 0.01 .. 50
     *               (Hz = cicli completi del pattern al secondo, oppure step al secondo con rateIsPerStep).
     *  Riprod.    : Forward | PingPong | OneShot (inviluppo complesso: una passata, poi tiene l'ultimo valore
     *               o va a 0), con Retrigger al Note-On (OneShot riparte sempre) oppure free-run.
     *  Per step   : valore -1..1 e interpolazione Linear / Exponential / Step / Custom (vedi PerformerPattern).
     *  Uscita     : advance(n) -> valore a fine intervallo (per la matrice a sub-frame), render() -> per-campione.
     *               slewMs (opzionale) liscia i gradini dello Step-mode.
     *
     * Nessuna allocazione, nessun lock. La posizione e' un double in STEP: nessuna deriva sui lunghi periodi.
     */
    class PerformerModulator
    {
    public:
        static constexpr float kMinHz = 0.01f, kMaxHz = 50.0f;

        struct Params
        {
            PlayMode mode = PlayMode::Forward;
            bool  endHold = true;           // OneShot: true = tiene l'ultimo valore, false = torna a 0
            bool  sync = false;
            int   division = 9;             // indice in kSyncBeats: durata di uno step
            float rateHz = 1.0f;
            bool  rateIsPerStep = false;    // Hz = step/s invece di cicli/s
            bool  retrigger = true;
            float startPhase = 0.0f;        // 0..1 del periodo, usata al retrigger
            float slewMs = 0.0f;            // 0 = nessun lisciamento
        };

        void prepare(double sampleRate) noexcept { sr = sampleRate > 0.0 ? sampleRate : 44100.0; noteOn(true); }

        void setParams(const Params& p) noexcept   { prm = p; }
        void setTempo(double bpmValue) noexcept    { bpm = std::min(std::max(bpmValue, 20.0), 999.0); }
        void setRateScale(float s) noexcept        { rateScale = std::min(std::max(s, 1.0f / 64.0f), 64.0f); }
        void setPattern(const PerformerPattern* p) noexcept { pat = p; }

        /** Evento MIDI Note-On. Con retrigger (o OneShot) il pattern riparte da startPhase. */
        void noteOn(bool force = false) noexcept
        {
            if (! (force || prm.retrigger || prm.mode == PlayMode::OneShot))
                return;                                          // free-run: la posizione continua
            pos = (double) std::min(std::max(prm.startPhase, 0.0f), 0.999999f) * period();
            finished = false;
            raw = out = pat != nullptr ? pat->evaluate(pos, prm.mode, prm.endHold) : 0.0f;
        }

        /** Avanza di n campioni e restituisce il valore a fine intervallo. */
        float advance(int n) noexcept
        {
            move((double) n);
            raw = pat != nullptr ? pat->evaluate(pos, prm.mode, prm.endHold) : 0.0f;
            out = prm.slewMs > 0.0f ? out + (raw - out) * slewCoef(n) : raw;
            return out;
        }

        /** Uscita PER-CAMPIONE (sample-accurate): scrive n valori. */
        void render(float* dst, int n) noexcept
        {
            const double sps = stepsPerSample();
            const float c = prm.slewMs > 0.0f ? slewCoef(1) : 1.0f;
            for (int i = 0; i < n; ++i)
            {
                moveBy(sps);
                raw = pat != nullptr ? pat->evaluate(pos, prm.mode, prm.endHold) : 0.0f;
                out = prm.slewMs > 0.0f ? out + (raw - out) * c : raw;
                dst[i] = out;
            }
        }

        float  value() const noexcept     { return out; }
        bool   isFinished() const noexcept { return finished; }
        double position() const noexcept  { return pos; }                         // in step
        /** Fase 0..1 nel periodo corrente. */
        float  phase() const noexcept     { return (float) std::min(pos / period(), 1.0); }
        /** Indice dello step corrente (0-based; in PingPong segue andata e ritorno). */
        int currentStep() const noexcept
        {
            const int n = pat != nullptr ? pat->getLength() : 1;
            if (prm.mode == PlayMode::PingPong && n >= 2)
            {
                const int P = 2 * (n - 1), s = std::min((int) pos, P - 1);
                return s < n ? s : P - s;                                         // 0,1,..,N-1,N-2,..,1
            }
            return std::min((int) pos, n - 1);
        }

        /** Step al secondo con i parametri correnti (utile alla UI e ai test). */
        double stepsPerSecond() const noexcept { return stepsPerSample() * sr; }

    private:
        double period() const noexcept { return pat != nullptr ? pat->periodSteps(prm.mode) : 1.0; }

        double stepsPerSample() const noexcept
        {
            double sps;                                          // step al secondo
            if (prm.sync)
            {
                const double beats = (double) kSyncBeats[std::min(std::max(prm.division, 0), kNumSyncDivisions - 1)];
                sps = (bpm / 60.0) / beats;
            }
            else
            {
                const double hz = (double) std::min(std::max(prm.rateHz, kMinHz), kMaxHz);
                sps = prm.rateIsPerStep ? hz : hz * period();
            }
            return std::min(sps * (double) rateScale, 20000.0) / sr;
        }

        float slewCoef(int n) const noexcept
        {
            return 1.0f - std::exp(-(float) n / (prm.slewMs * 0.001f * (float) sr));
        }

        void move(double nSamples) noexcept { moveBy(stepsPerSample() * nSamples); }

        void moveBy(double steps) noexcept
        {
            if (finished) return;
            pos += steps;
            const double P = period();
            if (prm.mode == PlayMode::OneShot)
            {
                if (pos >= P) { pos = P; finished = true; }
            }
            else if (pos >= P)
                pos -= P * std::floor(pos / P);
        }

        double sr = 44100.0, bpm = 120.0, pos = 0.0;
        float  rateScale = 1.0f, raw = 0.0f, out = 0.0f;
        bool   finished = false;
        Params prm{};
        const PerformerPattern* pat = nullptr;
    };
}
