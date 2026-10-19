#pragma once
#include <algorithm>
#include <cmath>
#include "ModCurve.h"

namespace bf
{
    /**
     * Envelope AHDSR con curve selezionabili per Attack / Decay / Release (esponenziale <-> logaritmica).
     * Calcolo per-CAMPIONE (tick / render): e' sample-accurate, quindi va bene anche come envelope d'ampiezza.
     *
     *   Attack  : da livello corrente (retrigger senza click) a 1, tempo `attack`, curva attackCurve
     *   Hold    : resta a 1 per `hold` secondi (0 = saltato)
     *   Decay   : da 1 a `sustain`; curva decayCurve (>0: caduta rapida e coda lenta, stile analogico)
     *   Sustain : segue `sustain` con un lisciamento di ~3 ms (nessuno scatto se il knob si muove)
     *   Release : dal livello AL MOMENTO del note-off a 0 (anche durante attacco/decay)
     *
     * Modulazione: setTimeScale() moltiplica tutti i tempi, setCurveOffset() somma un offset alle tre curve.
     * Nessuna allocazione, nessun lock: usabile ovunque nel thread audio.
     */
    class ModEnvelope
    {
    public:
        enum class Stage : int { Idle = 0, Attack, Hold, Decay, Sustain, Release };

        struct Params
        {
            float attack = 0.002f, hold = 0.0f, decay = 0.3f, sustain = 0.8f, release = 0.15f;   // secondi / 0..1
            float attackCurve = 0.0f, decayCurve = 0.0f, releaseCurve = 0.0f;                    // -1 (log) .. +1 (exp)
        };

        static constexpr float kMinTime = 0.0005f;       // 0.5 ms: evita click anche con A/R "a zero"

        void prepare(double sampleRate) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 44100.0;
            sustainCoef = 1.0f - std::exp(-1.0f / (0.003f * (float) sr));
            timeScale = 1.0f;
            updateRates();
            reset();
        }

        void setParams(const Params& p) noexcept
        {
            prm = p;
            prm.sustain = std::min(std::max(p.sustain, 0.0f), 1.0f);
            updateRates();
        }

        void setTimeScale(float s) noexcept
        {
            s = std::min(std::max(s, 1.0f / 64.0f), 64.0f);
            if (s == timeScale) return;
            timeScale = s;
            updateRates();
        }

        void setCurveOffset(float o) noexcept { curveOff = std::min(std::max(o, -2.0f), 2.0f); }

        void noteOn() noexcept  { from = lvl; pos = 0.0f; st = Stage::Attack; }
        void noteOff() noexcept
        {
            if (st == Stage::Idle || st == Stage::Release) return;
            relFrom = lvl; pos = 0.0f; st = Stage::Release;
        }
        void reset() noexcept   { st = Stage::Idle; lvl = 0.0f; pos = 0.0f; from = 0.0f; relFrom = 0.0f; }

        bool  isActive() const noexcept { return st != Stage::Idle; }
        float level() const noexcept    { return lvl; }
        Stage currentStage() const noexcept { return st; }

        float tick() noexcept
        {
            switch (st)
            {
                case Stage::Idle:
                    break;
                case Stage::Attack:
                    pos += aInc;
                    if (pos >= 1.0f) { lvl = 1.0f; pos = 0.0f; st = hInc > 0.0f ? Stage::Hold : Stage::Decay; }
                    else             lvl = from + (1.0f - from) * curveWarp(pos, curve(prm.attackCurve));
                    break;
                case Stage::Hold:
                    lvl = 1.0f;
                    pos += hInc;
                    if (pos >= 1.0f) { pos = 0.0f; st = Stage::Decay; }
                    break;
                case Stage::Decay:
                    pos += dInc;
                    if (pos >= 1.0f) { lvl = prm.sustain; pos = 0.0f; st = Stage::Sustain; }
                    else             lvl = prm.sustain + (1.0f - prm.sustain) * curveWarp(1.0f - pos, curve(prm.decayCurve));
                    break;
                case Stage::Sustain:
                    lvl += (prm.sustain - lvl) * sustainCoef;
                    break;
                case Stage::Release:
                    pos += rInc;
                    if (pos >= 1.0f) { lvl = 0.0f; pos = 0.0f; st = Stage::Idle; }
                    else             lvl = relFrom * curveWarp(1.0f - pos, curve(prm.releaseCurve));
                    break;
            }
            return lvl;
        }

        void render(float* out, int n) noexcept { for (int i = 0; i < n; ++i) out[i] = tick(); }

    private:
        float curve(float c) const noexcept { return std::min(std::max(c + curveOff, -1.0f), 1.0f); }
        float inc(float seconds) const noexcept
        {
            return 1.0f / (std::max(seconds * timeScale, kMinTime) * (float) sr);
        }
        void updateRates() noexcept
        {
            aInc = inc(prm.attack);
            hInc = prm.hold > 0.0f ? inc(prm.hold) : 0.0f;
            dInc = inc(prm.decay);
            rInc = inc(prm.release);
        }

        double sr = 44100.0;
        Params prm{};
        float  timeScale = 1.0f, curveOff = 0.0f, sustainCoef = 0.05f;
        float  aInc = 0.01f, hInc = 0.0f, dInc = 0.01f, rInc = 0.01f;
        Stage  st = Stage::Idle;
        float  lvl = 0.0f, pos = 0.0f, from = 0.0f, relFrom = 0.0f;
    };
}
