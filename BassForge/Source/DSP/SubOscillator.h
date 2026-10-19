#pragma once
// =====================================================================================================
//  SubOscillator - oscillatore sub-bass band-limited (Sine / Triangle / Square / Saw).
//
//  * Seno: SineLUT cubica. Square/Saw: PolyBLEP. Triangle: PolyBLAMP (correzione dei due spigoli).
//  * Tutte le forme sono allineate al seno (partono da 0 e salgono), quindi cambiare forma non sposta la fase.
//  * PHASE RESET: con SubPhaseMode::Reset ogni noteOn riparte da `startPhase` -> attacco identico ad ogni
//    nota (fondamentale per kick/sub coerenti). Con FreeRun la fase continua tra una nota e l'altra.
//  * PHASE TRACKING: la frequenza non e' un parametro: arriva come incremento di riferimento
//    (setReferenceIncrement = fondamentale della nota con lo stesso pitch bend/modulazione degli osc
//    principali). Il sub accumula  inc * 2^octave  in doppia precisione, quindi con octave = -1 e'
//    un sub-armonico esatto, senza deriva, anche durante un pitch bend.
//  * DIRECT-OUT: Params::directOut + renderRouted() decidono se il sub entra nella catena filtri (bus A)
//    o va al bus diretto che il processor somma al master dopo i filtri/FX (bus B).
//  * Nessuna allocazione, nessun lock, nessuna dipendenza da JUCE.
// =====================================================================================================
#include <cstdint>
#include "SineLUT.h"

namespace bf
{
    enum class SubWave      : int { Sine = 0, Triangle, Square, Saw };
    enum class SubPhaseMode : int { Reset = 0, FreeRun };

    class SubOscillator
    {
    public:
        struct Params
        {
            SubWave      wave       = SubWave::Sine;
            int          octave     = 0;       // -3..0 tipico (ratio = 2^octave rispetto alla fondamentale)
            float        level      = 1.0f;    // 0..1
            float        startPhase = 0.0f;    // 0..1 giri, usata con Reset
            SubPhaseMode phaseMode  = SubPhaseMode::Reset;
            bool         directOut  = false;   // true: salta i filtri (vedi renderRouted)
        };

        /** NON real-time. Forza la costruzione della LUT fuori dal thread audio. */
        void prepare(double sampleRate) noexcept;

        void setParams(const Params& p) noexcept;

        /** Incremento di fase per campione della fondamentale della nota (cicli/campione = Hz / sr). */
        void setReferenceIncrement(double cyclesPerSample) noexcept { refInc = cyclesPerSample > 0.0 ? cyclesPerSample : 0.0; }

        void noteOn() noexcept;                // Reset: fase = startPhase; sempre: livello agganciato (niente rampa da 0)
        void reset() noexcept;

        /** Un campione (mono). Il livello e' smussato (5 ms) per evitare click ai movimenti di knob. */
        float tick() noexcept;

        /** Somma `n` campioni su `toFilterChain` oppure su `toDirectBus` secondo Params::directOut.
         *  Passare nullptr per un bus non disponibile: il sub ripiega sull'altro (mai perso). */
        void renderRouted(float* toFilterChain, float* toDirectBus, int n) noexcept;

        bool   isDirectOut() const noexcept { return p.directOut; }
        double phase() const noexcept       { return ph; }

    private:
        double sr = 44100.0, ph = 0.0, refInc = 0.0;
        double ratio = 1.0;
        float  levelSm = 0.0f, levelCoef = 0.001f;
        bool   levelPrimed = false;
        Params p;
    };
}
