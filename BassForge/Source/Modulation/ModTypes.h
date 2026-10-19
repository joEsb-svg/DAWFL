#pragma once
#include <cstdint>

// Tipi condivisi del sistema di modulazione. Nessuna dipendenza da JUCE (testabile in headless).
namespace bf
{
    inline constexpr int kNumEnv   = 4;
    inline constexpr int kNumLfo   = 4;    // ogni "LFO" puo' essere anche un Performer a 32 step
    inline constexpr int kNumMacro = 12;
    inline constexpr int kNumSlots = 12;   // connessioni della matrice

    // --- Sorgenti ---------------------------------------------------------------------
    // Range: Env/Velocity/Aftertouch/ModWheel/Slide/Macro = 0..1 ; LFO/Keytrack/PitchBend/Random = -1..1
    enum ModSource : int
    {
        SrcNone = 0,
        SrcEnv1, SrcEnv2, SrcEnv3, SrcEnv4,
        SrcLfo1, SrcLfo2, SrcLfo3, SrcLfo4,
        SrcVelocity,
        SrcKeytrack,                  // (nota - 60) / 48, limitato a +-1
        SrcPitchBend,                 // -1..1 (rotella normalizzata, prima del bend range)
        SrcAftertouch,                // channel pressure / poly aftertouch / MPE pressure
        SrcModWheel,                  // CC1
        SrcMpeSlide,                  // CC74 (MPE "slide" / timbre)
        SrcRandom,                    // valore casuale -1..1 estratto a ogni nota
        SrcMacro1,                    // ... SrcMacro1 + 11
        SrcCount = SrcMacro1 + kNumMacro
    };

    // --- Destinazioni --------------------------------------------------------------------
    // I gruppi sono contigui: DstF1Cutoff + i, DstOsc1Wt + i, ... (i = indice 0-based).
    enum ModDest : int
    {
        DstNone = 0,
        DstPitch,                              // +-24 st a fondo scala (tutti gli osc + sub se segue la mod)
        DstOsc1Wt, DstOsc2Wt, DstOsc3Wt,
        DstF1Cutoff, DstF2Cutoff,              // +-5 ottave a fondo scala
        DstF1Res, DstF2Res,
        DstSubLevel, DstNoiseLevel,
        DstFilterMorph,
        DstF1Vowel, DstF2Vowel,
        DstOsc1Level, DstOsc2Level, DstOsc3Level,
        DstOsc1Pitch, DstOsc2Pitch, DstOsc3Pitch,   // +-12 st per singolo osc
        DstF1Drive, DstF2Drive,
        DstF1Fb, DstF2Fb,
        DstAmp,                                // guadagno = max(0, 1 + mod)
        DstLfo1Rate, DstLfo2Rate, DstLfo3Rate, DstLfo4Rate,     // x 2^(mod*4)
        DstEnv1Time, DstEnv2Time, DstEnv3Time, DstEnv4Time,     // x 2^(mod*3) su A/H/D/R
        DstEnv1Curve, DstEnv2Curve, DstEnv3Curve, DstEnv4Curve, // offset di curva -1(log)..+1(exp)
        DstCount
    };

    namespace modscale
    {
        inline constexpr float kPitchSemis    = 24.0f;
        inline constexpr float kOscPitchSemis = 12.0f;
        inline constexpr float kCutoffOct     = 5.0f;
        inline constexpr float kLfoRateOct    = 4.0f;
        inline constexpr float kEnvTimeOct    = 3.0f;
        inline constexpr float kMaxSum        = 4.0f;   // somma massima per destinazione (|x|)
    }

    inline const char* modSourceName(int s) noexcept
    {
        static constexpr const char* names[SrcCount] = {
            "None", "Env 1", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
            "Velocity", "Keytrack", "Pitch Bend", "Aftertouch", "Mod Wheel", "MPE Slide", "Random",
            "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6",
            "Macro 7", "Macro 8", "Macro 9", "Macro 10", "Macro 11", "Macro 12" };
        return (s >= 0 && s < SrcCount) ? names[s] : "?";
    }

    inline const char* modDestName(int d) noexcept
    {
        static constexpr const char* names[DstCount] = {
            "None", "Pitch", "Osc1 WT", "Osc2 WT", "Osc3 WT", "F1 Cutoff", "F2 Cutoff", "F1 Res", "F2 Res",
            "Sub Level", "Noise Level", "Filter Morph", "F1 Vowel", "F2 Vowel",
            "Osc1 Level", "Osc2 Level", "Osc3 Level", "Osc1 Pitch", "Osc2 Pitch", "Osc3 Pitch",
            "F1 Drive", "F2 Drive", "F1 Feedback", "F2 Feedback", "Amp",
            "LFO1 Rate", "LFO2 Rate", "LFO3 Rate", "LFO4 Rate",
            "Env1 Time", "Env2 Time", "Env3 Time", "Env4 Time",
            "Env1 Curve", "Env2 Curve", "Env3 Curve", "Env4 Curve" };
        return (d >= 0 && d < DstCount) ? names[d] : "?";
    }
}
