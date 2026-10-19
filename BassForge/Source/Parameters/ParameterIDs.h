#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Modulation/ModTypes.h"

// Costanti e ID dei parametri. Tutto qui e' costruito SOLO fuori dal thread audio
// (costruttore / createParameterLayout). Il thread audio usa ParamCache (puntatori atomici).
namespace bf
{
    inline constexpr int kNumOsc    = 3;
    inline constexpr int kNumFilter = 2;
    inline constexpr int kNumVoices = 16;
    inline constexpr int kNumSampleSlots = 8;   // = SampleBank::kMaxSlots (4 interni + 4 utente)
    // kNumEnv, kNumLfo, kNumMacro, kNumSlots, ModSource e ModDest: vedi Modulation/ModTypes.h

    namespace ids
    {
        inline juce::String osc(int i, const char* n)    { return "osc" + juce::String(i + 1) + "_" + n; }
        inline juce::String filt(int i, const char* n)   { return "flt" + juce::String(i + 1) + "_" + n; }
        inline juce::String env(int i, const char* n)    { return "env" + juce::String(i + 1) + "_" + n; }
        inline juce::String lfo(int i, const char* n)    { return "lfo" + juce::String(i + 1) + "_" + n; }
        inline juce::String macro(int i)                 { return "macro" + juce::String(i + 1); }
        inline juce::String slot(int i, const char* n)   { return "mod" + juce::String(i + 1) + "_" + n; }

        inline const char* const subOn = "sub_on";
        inline const char* const subWave = "sub_wave";
        inline const char* const subOct = "sub_oct";
        inline const char* const subLevel = "sub_level";
        inline const char* const subPhase = "sub_phase";        // gradi, usata con Phase Reset
        inline const char* const subReset = "sub_reset";        // true = Phase Reset al note-on, false = Free-run
        inline const char* const subDirect = "sub_direct";      // true = Direct-Out (salta filtri e FX master)
        inline const char* const subFollowMod = "sub_follow_mod";   // true = il sub segue la modulazione di pitch

        inline const char* const noiseOn = "noise_on";
        inline const char* const noiseType = "noise_type";
        inline const char* const noiseLevel = "noise_level";
        inline const char* const noiseWidth = "noise_width";

        // Sample engine (Sample Attack Transient)
        inline const char* const smpOn = "smp_on";
        inline const char* const smpSlot = "smp_slot";
        inline const char* const smpLevel = "smp_level";
        inline const char* const smpPitch = "smp_pitch";
        inline const char* const smpPitchTrack = "smp_ptrack";
        inline const char* const smpLoop = "smp_loop";
        inline const char* const smpLoopStart = "smp_loop_start";
        inline const char* const smpLoopEnd = "smp_loop_end";
        inline const char* const smpDecay = "smp_decay";

        // Filtro tonale di noise+sample (con keytracking)
        inline const char* const nfltOn = "nflt_on";
        inline const char* const nfltType = "nflt_type";
        inline const char* const nfltCutoff = "nflt_cut";
        inline const char* const nfltRes = "nflt_res";
        inline const char* const nfltTrack = "nflt_track";

        inline const char* const filterRouting = "flt_routing";
        inline const char* const filterMorph = "flt_morph";
        inline const char* const pitchBendRange = "pb_range";

        // ---- FX master (Step 7) ----
        inline const char* const fxOrder = "fx_order";
        inline const char* const ottOn = "ott_on";
        inline const char* const ottDepth = "ott_depth";
        inline const char* const ottTime = "ott_time";
        inline const char* const ottUp = "ott_up";
        inline const char* const ottDown = "ott_down";
        inline const char* const ottLow = "ott_low_db";
        inline const char* const ottMid = "ott_mid_db";
        inline const char* const ottHigh = "ott_high_db";
        inline const char* const ottX1 = "ott_xover1";
        inline const char* const ottX2 = "ott_xover2";
        inline const char* const ottMix = "ott_mix";
        inline const char* const drvOn = "drv_on";
        inline const char* const drvType = "drv_type";
        inline const char* const drvAmount = "drv_amount";
        inline const char* const drvBias = "drv_bias";
        inline const char* const drvStages = "drv_stages";
        inline const char* const drvOs = "drv_os";
        inline const char* const drvOut = "drv_out_db";
        inline const char* const drvMix = "drv_mix";
        inline const char* const dimOn = "dim_on";
        inline const char* const dimAmount = "dim_amount";
        inline const char* const dimWidth = "dim_width";
        inline const char* const dimSize = "dim_size";
        inline const char* const dimRate = "dim_rate";
        inline const char* const dimMono = "dim_mono_hz";
        inline const char* const dimMix = "dim_mix";
        inline const char* const dlyOn = "dly_on";
        inline const char* const dlyTime = "dly_time";
        inline const char* const dlySync = "dly_sync";
        inline const char* const dlyDiv = "dly_div";
        inline const char* const dlyFb = "dly_fb";
        inline const char* const dlyPing = "dly_ping";
        inline const char* const dlyDamp = "dly_damp";
        inline const char* const dlyHpf = "dly_hpf";
        inline const char* const dlyMix = "dly_mix";
        inline const char* const revOn = "rev_on";
        inline const char* const revSize = "rev_size";
        inline const char* const revDamp = "rev_damp";
        inline const char* const revWidth = "rev_width";
        inline const char* const revPre = "rev_predelay";
        inline const char* const revHpf = "rev_hpf";
        inline const char* const revMix = "rev_mix";
        inline const char* const masterGain = "master_gain";
        inline const char* const masterLimiter = "master_limiter";   // limiter brickwall finale (default: on)
    }
}
