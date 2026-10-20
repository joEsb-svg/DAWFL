#pragma once

#include <atomic>

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

    inline constexpr int lowPowerVoices  = 6;   // voci contemporanee per synth
    inline constexpr int lowPowerUnison  = 3;   // voci di unison per oscillatore
}
