#pragma once

#include <atomic>
#include <cmath>

//==============================================================================
/**
    Modalita' "Risparmio" (impostazioni): per i telefoni meno potenti.
    I synth usano meno voci contemporanee e un unison piu' leggero; il suono resta lo stesso
    nelle parti semplici, cambia solo negli accordi molto pieni.
*/
namespace perf
{
    inline std::atomic<bool>& lowPowerFlag()    { static std::atomic<bool> flag { false }; return flag; }
    inline bool lowPower() noexcept             { return lowPowerFlag().load (std::memory_order_relaxed); }

    /** Uno strumento sta per suonare senza note MIDI (es. tocco su un pad): il motore lo deve calcolare. */
    inline std::atomic<unsigned>& wakeCounter()  { static std::atomic<unsigned> c { 0 }; return c; }
    inline void wake() noexcept                    { wakeCounter().fetch_add (1, std::memory_order_relaxed); }

    inline constexpr int lowPowerVoices  = 6;   // voci contemporanee per synth
    inline constexpr int lowPowerUnison  = 3;   // voci di unison per oscillatore

    /**
        Salta gli effetti "con coda" (riverbero, delay...) quando non entra piu' niente:
        dopo 'tailSeconds' di silenzio in ingresso l'uscita e' comunque silenzio, quindi non serve calcolarla.
        Un riverbero acceso su un canale fermo costava quanto uno che suona.
    */
    struct TailGate
    {
        /** true = da elaborare. Guarda il blocco in ingresso (L e R, n campioni). */
        bool active (const float* L, const float* R, int n, double sampleRate, double tailSeconds) noexcept
        {
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = std::fmax (peak, std::fmax (std::fabs (L[i]), std::fabs (R[i])));
            return update (peak, n, sampleRate, tailSeconds);
        }

        bool update (float peak, int n, double sampleRate, double tailSeconds) noexcept
        {
            if (peak > 1.0e-5f)
            {
                silent = 0;
                return true;
            }
            const bool wasActive = silent < (long) (tailSeconds * sampleRate);
            silent += n;
            justStopped = wasActive && silent >= (long) (tailSeconds * sampleRate);
            return wasActive;
        }

        /** true per un blocco solo: l'effetto si e' appena fermato (es. per azzerare i buffer). */
        bool justStopped = false;
        long silent = 1L << 40;     // all'inizio: fermo
    };

    /** Seno veloce (errore < 0.1%) per gli LFO e i modulatori: 'cycles' in giri, qualunque valore. */
    inline float fastSin (float cycles) noexcept
    {
        float x = cycles - std::floor (cycles);            // 0..1
        x = x < 0.5f ? x : x - 1.0f;                       // -0.5..0.5
        const float y = 8.0f * x - 16.0f * x * std::fabs (x);   // parabola
        return 0.225f * (y * std::fabs (y) - y) + y;       // correzione
    }
}
