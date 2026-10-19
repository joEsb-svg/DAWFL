#include "ParameterLayout.h"
#include "DSP/SampleBank.h"
#include "Modulation/ModLfo.h"
#include "Modulation/ModSync.h"
#include "FX/MasterFXChain.h"

namespace bf
{
namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Range  = juce::NormalisableRange<float>;
    constexpr int kVer = 1; // versione ParameterID (richiesto da AU)

    // Testo dei valori nella GUI: poche cifre significative, kHz sopra 1000 Hz, interi senza decimali.
    juce::String formatValue(float v, const juce::String& unit, bool isInt)
    {
        const float a = std::abs(v);
        juce::String t;
        if (unit == "Hz" && a >= 1000.0f)     return juce::String(v / 1000.0f, 2) + " kHz";
        if (unit == "s" && a < 1.0f)          return juce::String(juce::roundToInt(v * 1000.0f)) + " ms";
        if (isInt)                            t = juce::String(juce::roundToInt(v));
        else if (a >= 100.0f)                 t = juce::String(v, 0);
        else if (a >= 10.0f)                  t = juce::String(v, 1);
        else                                  t = juce::String(v, 2);
        return unit.isEmpty() ? t : t + " " + unit;
    }

    float parseValue(const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        float v = t.getFloatValue();
        if (t.containsChar('k')) v *= 1000.0f;
        return v;
    }

    void addFloat(Layout& l, const juce::String& id, const juce::String& name,
                  Range r, float def, const juce::String& unit = {})
    {
        const bool isInt = r.interval >= 1.0f;
        l.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, kVer }, name, r, def,
            juce::AudioParameterFloatAttributes()
                .withLabel(unit)
                .withStringFromValueFunction([unit, isInt](float v, int) { return formatValue(v, unit, isInt); })
                .withValueFromStringFunction([](const juce::String& t) { return parseValue(t); })));
    }

    void addBool(Layout& l, const juce::String& id, const juce::String& name, bool def)
    {
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ id, kVer }, name, def));
    }

    void addChoice(Layout& l, const juce::String& id, const juce::String& name,
                   const juce::StringArray& choices, int def)
    {
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ id, kVer }, name, choices, def));
    }

    Range hzRange(float lo, float hi, float centre)
    {
        Range r(lo, hi);
        r.setSkewForCentre(centre);
        return r;
    }

    Range timeRange(float lo, float hi, float centre) { return hzRange(lo, hi, centre); }
}

Layout createParameterLayout()
{
    Layout l;

    // ---- 3 Oscillatori Wavetable ----------------------------------------------
    for (int i = 0; i < kNumOsc; ++i)
    {
        const auto n = "Osc " + juce::String(i + 1) + " ";
        addBool (l, ids::osc(i, "on"),    n + "On",       i == 0);
        addFloat(l, ids::osc(i, "wt"),    n + "WT Pos",   { 0.f, 1.f, 0.f }, 0.f);
        addFloat(l, ids::osc(i, "semi"),  n + "Semi",     { -24.f, 24.f, 1.f }, 0.f, "st");
        addFloat(l, ids::osc(i, "fine"),  n + "Fine",     { -100.f, 100.f, 0.01f }, i == 1 ? 7.f : (i == 2 ? -7.f : 0.f), "ct");
        addFloat(l, ids::osc(i, "level"), n + "Level",    { 0.f, 1.f, 0.f }, 0.5f);
        addFloat(l, ids::osc(i, "pan"),   n + "Pan",      { -1.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::osc(i, "uni"),    n + "Unison Voices",  { 1.f, 16.f, 1.f }, 1.f);
        addFloat (l, ids::osc(i, "det"),    n + "Unison Detune",  { 0.f, 100.f, 0.f }, 20.f, "ct");
        addFloat (l, ids::osc(i, "spread"), n + "Unison Spread",  { 0.f, 1.f, 0.f }, 0.5f);
        addFloat (l, ids::osc(i, "blend"),  n + "Unison Blend",   { 0.f, 1.f, 0.f }, 1.f);
        addChoice(l, ids::osc(i, "warp"),   n + "Warp Mode", { "None", "Windowed FM", "Formant", "Sine Folder" }, 0);
        addFloat (l, ids::osc(i, "wamt"),   n + "Warp Amount",    { 0.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::osc(i, "waux"),   n + "Warp Aux",       { 0.f, 1.f, 0.f }, 0.f);
    }

    // ---- Sub -------------------------------------------------------------------
    addBool  (l, ids::subOn,     "Sub On", true);
    addChoice(l, ids::subWave,   "Sub Wave", { "Sine", "Triangle", "Square", "Saw" }, 0);
    addChoice(l, ids::subOct,    "Sub Octave", { "-3", "-2", "-1", "0" }, 2);          // indice - 3 = ottava
    addFloat (l, ids::subLevel,  "Sub Level", { 0.f, 1.f, 0.f }, 0.5f);
    addFloat (l, ids::subPhase,  "Sub Start Phase", { 0.f, 360.f, 1.f }, 0.f, "deg");
    addBool  (l, ids::subReset,  "Sub Phase Reset", true);
    addBool  (l, ids::subDirect, "Sub Direct Out", true);
    addBool  (l, ids::subFollowMod, "Sub Follow Mod", true);

    // ---- Noise -----------------------------------------------------------------
    addBool  (l, ids::noiseOn,    "Noise On", false);
    addChoice(l, ids::noiseType,  "Noise Type", { "White", "Pink" }, 0);
    addFloat (l, ids::noiseLevel, "Noise Level", { 0.f, 1.f, 0.f }, 0.1f);
    addFloat (l, ids::noiseWidth, "Noise Width", { 0.f, 1.f, 0.f }, 1.0f);

    // ---- Sample engine (Sample Attack Transient) ---------------------------------
    const juce::StringArray slotNames{ "Metal Hit", "Kick Click", "Snare Crack", "Foley Tick",
                                       "User 1", "User 2", "User 3", "User 4" };
    jassert(slotNames.size() == SampleBank::kMaxSlots);
    addBool  (l, ids::smpOn,         "Sample On", false);
    addChoice(l, ids::smpSlot,       "Sample Slot", slotNames, 0);
    addFloat (l, ids::smpLevel,      "Sample Level", { 0.f, 1.f, 0.f }, 0.5f);
    addFloat (l, ids::smpPitch,      "Sample Pitch", { -24.f, 24.f, 0.01f }, 0.f, "st");
    addFloat (l, ids::smpPitchTrack, "Sample Pitch Track", { 0.f, 1.f, 0.f }, 0.f);
    addBool  (l, ids::smpLoop,       "Sample Loop", false);
    addFloat (l, ids::smpLoopStart,  "Sample Loop Start", { 0.f, 1.f, 0.f }, 0.f);
    addFloat (l, ids::smpLoopEnd,    "Sample Loop End",   { 0.f, 1.f, 0.f }, 1.f);
    addFloat (l, ids::smpDecay,      "Sample Decay", hzRange(0.f, 2000.f, 150.f), 0.f, "ms");

    // ---- Filtro tonale noise/sample (keytracking) --------------------------------
    addBool  (l, ids::nfltOn,     "Noise Filter On", false);
    addChoice(l, ids::nfltType,   "Noise Filter Type", { "Lowpass", "Bandpass", "Highpass" }, 0);
    addFloat (l, ids::nfltCutoff, "Noise Filter Cutoff", hzRange(20.f, 20000.f, 2000.f), 8000.f, "Hz");
    addFloat (l, ids::nfltRes,    "Noise Filter Resonance", { 0.f, 1.f, 0.f }, 0.2f);
    addFloat (l, ids::nfltTrack,  "Noise Filter Keytrack", { 0.f, 1.f, 0.f }, 0.f);

    // ---- 2 Filtri --------------------------------------------------------------
    const juce::StringArray fTypes{ "LP 12", "LP 24", "LP 48", "HP 12", "HP 24", "HP 48", "Comb", "Vowel" };   // = bf::FilterType
    for (int i = 0; i < kNumFilter; ++i)
    {
        const auto n = "Filter " + juce::String(i + 1) + " ";
        addBool  (l, ids::filt(i, "on"),       n + "On", i == 0);
        addChoice(l, ids::filt(i, "type"),     n + "Type", fTypes, i == 0 ? 1 : 4);
        addFloat (l, ids::filt(i, "cutoff"),   n + "Cutoff", hzRange(20.f, 20000.f, 1000.f), i == 0 ? 2000.f : 40.f, "Hz");
        addFloat (l, ids::filt(i, "res"),      n + "Resonance", { 0.f, 1.f, 0.f }, 0.2f);
        addFloat (l, ids::filt(i, "drive"),    n + "Drive", { 0.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::filt(i, "keytrack"), n + "Keytrack", { 0.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::filt(i, "fb"),       n + "Feedback", { -1.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::filt(i, "tune"),     n + "Comb Tune", { -24.f, 24.f, 0.f }, 0.f, "st");
        addFloat (l, ids::filt(i, "vowel"),    n + "Vowel", { 0.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::filt(i, "fshift"),   n + "Formant Shift", { -12.f, 12.f, 0.f }, 0.f, "st");
    }
    addChoice(l, ids::filterRouting, "Filter Routing", { "Serial", "Parallel", "Morph" }, 0);
    addFloat (l, ids::filterMorph,   "Filter Morph", { 0.f, 1.f, 0.f }, 0.5f);

    // ---- 4 Envelope AHDSR (Env1 = amp per convenzione) ------------------------------
    for (int i = 0; i < kNumEnv; ++i)
    {
        const auto n = "Env " + juce::String(i + 1) + " ";
        addFloat(l, ids::env(i, "a"),  n + "Attack",  timeRange(0.0005f, 10.f, 0.2f), 0.002f, "s");
        addFloat(l, ids::env(i, "h"),  n + "Hold",    timeRange(0.f, 5.f, 0.3f),      0.f,    "s");
        addFloat(l, ids::env(i, "d"),  n + "Decay",   timeRange(0.0005f, 10.f, 0.4f), 0.3f,   "s");
        addFloat(l, ids::env(i, "s"),  n + "Sustain", { 0.f, 1.f, 0.f },              i == 0 ? 0.8f : 0.f);
        addFloat(l, ids::env(i, "r"),  n + "Release", timeRange(0.0005f, 10.f, 0.4f), 0.15f,  "s");
        addFloat(l, ids::env(i, "ac"), n + "Attack Curve",  { -1.f, 1.f, 0.f }, 0.f);   // -1 log .. +1 exp
        addFloat(l, ids::env(i, "dc"), n + "Decay Curve",   { -1.f, 1.f, 0.f }, 0.f);
        addFloat(l, ids::env(i, "rc"), n + "Release Curve", { -1.f, 1.f, 0.f }, 0.f);
    }

    // ---- 4 LFO / Performer -----------------------------------------------------------
    juce::StringArray lfoShapes{ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Random", "Performer" };
    jassert(lfoShapes.size() == (int) LfoShape::Count);
    juce::StringArray syncNames;
    for (int i = 0; i < kNumSyncDivisions; ++i) syncNames.add(kSyncNames[i]);
    for (int i = 0; i < kNumLfo; ++i)
    {
        const auto n = "LFO " + juce::String(i + 1) + " ";
        addFloat (l, ids::lfo(i, "rate"),   n + "Rate", hzRange(0.01f, 50.f, 2.f), 2.f, "Hz");
        addChoice(l, ids::lfo(i, "shape"),  n + "Shape", lfoShapes, 0);
        addBool  (l, ids::lfo(i, "retrig"), n + "Retrigger", true);
        addBool  (l, ids::lfo(i, "sync"),   n + "Tempo Sync", false);
        addChoice(l, ids::lfo(i, "div"),    n + "Division", syncNames, 5);          // 1/4 (Performer: durata di uno step)
        addFloat (l, ids::lfo(i, "phase"),  n + "Phase", { 0.f, 1.f, 0.f }, 0.f);
        addFloat (l, ids::lfo(i, "fade"),   n + "Fade In", timeRange(0.f, 5000.f, 0.3f), 0.f, "ms");
        // Performer (shape = Performer): modo di riproduzione, fine One-Shot, Hz = step/s invece di cicli/s
        addChoice(l, ids::lfo(i, "pmode"),  n + "Performer Mode", { "Forward", "Ping-Pong", "One-Shot" }, 0);
        addBool  (l, ids::lfo(i, "phold"),  n + "One-Shot Hold Last", true);
        addBool  (l, ids::lfo(i, "pstep"),  n + "Rate Per Step", false);
    }

    // ---- 12 Macro --------------------------------------------------------------
    for (int i = 0; i < kNumMacro; ++i)
        addFloat(l, ids::macro(i), "Macro " + juce::String(i + 1), { 0.f, 1.f, 0.f }, 0.f);

    // ---- Matrice di modulazione (kNumSlots slot) ---------------------------------------
    juce::StringArray srcNames, dstNames;
    for (int i = 0; i < SrcCount; ++i) srcNames.add(modSourceName(i));
    for (int i = 0; i < DstCount; ++i) dstNames.add(modDestName(i));

    // Default utili: Env2 -> F1 cutoff, LFO1 -> F1 cutoff, Macro1 -> F1 cutoff, Macro2 -> Osc1 WT
    const int defSrc[kNumSlots] = { SrcEnv2, SrcLfo1, SrcMacro1, SrcMacro1 + 1 };
    const int defDst[kNumSlots] = { DstF1Cutoff, DstF1Cutoff, DstF1Cutoff, DstOsc1Wt };
    const float defAmt[kNumSlots] = { 0.4f };
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto n = "Mod " + juce::String(i + 1) + " ";
        addChoice(l, ids::slot(i, "src"), n + "Source", srcNames, defSrc[i]);
        addChoice(l, ids::slot(i, "dst"), n + "Dest",   dstNames, defDst[i]);
        addFloat (l, ids::slot(i, "amt"), n + "Amount", { -1.f, 1.f, 0.f }, defAmt[i]);
    }

    addFloat(l, ids::pitchBendRange, "Pitch Bend Range", { 0.f, 24.f, 1.f }, 2.f, "st");

    // ---- FX Master ---------------------------------------------------------------
    {
        juce::StringArray orders; for (const char* n : MasterFXChain::kOrderNames) orders.add(n);
        addChoice(l, ids::fxOrder, "FX Order", orders, 0);

        // Multiband Compressor (OTT)
        addBool (l, ids::ottOn,    "OTT On", false);
        addFloat(l, ids::ottDepth, "OTT Depth", { 0.f, 1.f, 0.f }, 0.5f);
        addFloat(l, ids::ottTime,  "OTT Time",  { 0.f, 1.f, 0.f }, 0.5f);
        addFloat(l, ids::ottUp,    "OTT Upward",   { 0.f, 1.f, 0.f }, 1.f);
        addFloat(l, ids::ottDown,  "OTT Downward", { 0.f, 1.f, 0.f }, 1.f);
        addFloat(l, ids::ottLow,   "OTT Low Gain",  { -12.f, 12.f, 0.f }, 0.f, "dB");
        addFloat(l, ids::ottMid,   "OTT Mid Gain",  { -12.f, 12.f, 0.f }, 0.f, "dB");
        addFloat(l, ids::ottHigh,  "OTT High Gain", { -12.f, 12.f, 0.f }, 0.f, "dB");
        addFloat(l, ids::ottX1,    "OTT Xover Low/Mid",  hzRange(40.f, 400.f, 120.f), 120.f, "Hz");
        addFloat(l, ids::ottX2,    "OTT Xover Mid/High", hzRange(800.f, 8000.f, 2500.f), 2500.f, "Hz");
        addFloat(l, ids::ottMix,   "OTT Mix", { 0.f, 1.f, 0.f }, 1.f);

        // Hyper-Drive / Wavefolder
        addBool  (l, ids::drvOn,     "Drive On", false);
        addChoice(l, ids::drvType,   "Drive Type", { "Diode", "Tube", "Folder" }, 0);
        addFloat (l, ids::drvAmount, "Drive Amount", { 0.f, 1.f, 0.f }, 0.3f);
        addFloat (l, ids::drvBias,   "Drive Bias",   { -1.f, 1.f, 0.f }, 0.f);
        addChoice(l, ids::drvStages, "Folder Stages", { "1", "2", "3", "4" }, 1);
        addChoice(l, ids::drvOs,     "Drive Oversampling", { "1x", "2x", "4x" }, 1);
        addFloat (l, ids::drvOut,    "Drive Output", { -24.f, 12.f, 0.f }, 0.f, "dB");
        addFloat (l, ids::drvMix,    "Drive Mix", { 0.f, 1.f, 0.f }, 1.f);

        // Dimension Expander
        addBool (l, ids::dimOn,     "Dimension On", false);
        addFloat(l, ids::dimAmount, "Dimension Amount", { 0.f, 1.f, 0.f }, 0.5f);
        addFloat(l, ids::dimWidth,  "Dimension Width",  { 0.f, 2.f, 0.f }, 1.f);
        addFloat(l, ids::dimSize,   "Dimension Size",   { 4.f, 30.f, 0.f }, 12.f, "ms");
        addFloat(l, ids::dimRate,   "Dimension Rate",   hzRange(0.05f, 5.f, 0.6f), 0.6f, "Hz");
        addFloat(l, ids::dimMono,   "Dimension Mono Below", hzRange(40.f, 400.f, 120.f), 120.f, "Hz");
        addFloat(l, ids::dimMix,    "Dimension Mix", { 0.f, 1.f, 0.f }, 1.f);

        // Master Delay
        juce::StringArray divs; for (int i = 0; i < kNumSyncDivisions; ++i) divs.add(kSyncNames[i]);
        addBool  (l, ids::dlyOn,   "Delay On", false);
        addFloat (l, ids::dlyTime, "Delay Time", hzRange(1.f, 2000.f, 300.f), 250.f, "ms");
        addBool  (l, ids::dlySync, "Delay Sync", false);
        addChoice(l, ids::dlyDiv,  "Delay Division", divs, 5);
        addFloat (l, ids::dlyFb,   "Delay Feedback", { 0.f, 0.95f, 0.f }, 0.35f);
        addBool  (l, ids::dlyPing, "Delay Ping-Pong", false);
        addFloat (l, ids::dlyDamp, "Delay Damping", hzRange(200.f, 20000.f, 4000.f), 8000.f, "Hz");
        addFloat (l, ids::dlyHpf,  "Delay Wet HPF", hzRange(20.f, 2000.f, 200.f), 150.f, "Hz");
        addFloat (l, ids::dlyMix,  "Delay Mix", { 0.f, 1.f, 0.f }, 0.3f);

        // Master Reverb
        addBool (l, ids::revOn,    "Reverb On", false);
        addFloat(l, ids::revSize,  "Reverb Size",    { 0.f, 1.f, 0.f }, 0.5f);
        addFloat(l, ids::revDamp,  "Reverb Damping", { 0.f, 1.f, 0.f }, 0.4f);
        addFloat(l, ids::revWidth, "Reverb Width",   { 0.f, 1.f, 0.f }, 1.f);
        addFloat(l, ids::revPre,   "Reverb Pre-Delay", { 0.f, 100.f, 0.f }, 10.f, "ms");
        addFloat(l, ids::revHpf,   "Reverb Wet HPF", hzRange(20.f, 2000.f, 200.f), 200.f, "Hz");
        addFloat(l, ids::revMix,   "Reverb Mix", { 0.f, 1.f, 0.f }, 0.25f);
    }
    addFloat(l, ids::masterGain,    "Master Gain",      { -60.f, 6.f, 0.f }, 0.f, "dB");
    addBool (l, ids::masterLimiter, "Master Limiter", true);

    return l;
}
}
