#include "PresetManager.h"
#include "PerformerState.h"

namespace bf
{
namespace
{
    constexpr const char* kExt = ".bfpreset";

    juce::String sanitize(const juce::String& n)
    {
        return juce::File::createLegalFileName(n.trim()).replaceCharacters(".", "_").trim();
    }
}

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& a, PerformerBank& p) : apvts(a), performers(p) {}

//==============================================================================
bool PresetManager::set(const juce::String& id, float v)
{
    auto* p = apvts.getParameter(id);
    if (p == nullptr) { jassertfalse; return false; }
    if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
        r->setValueNotifyingHost(r->convertTo0to1(v));
    else
        p->setValueNotifyingHost(v);
    return true;
}

void PresetManager::connect(int slot, int src, int dst, float amount)
{
    set(ids::slot(slot, "src"), (float) src);
    set(ids::slot(slot, "dst"), (float) dst);
    set(ids::slot(slot, "amt"), amount);
}

void PresetManager::clearSlots()
{
    for (int i = 0; i < kNumSlots; ++i) connect(i, SrcNone, DstNone, 0.0f);
}

void PresetManager::resetPatterns()
{
    for (auto& p : performers.p)
    {
        p.setLength(16);
        for (int i = 0; i < PerformerPattern::kMaxSteps; ++i)
            p.setStep(i, -1.0f + 2.0f * (float) i / (float) (PerformerPattern::kMaxSteps - 1), StepInterp::Step, 0.0f);
    }
}

void PresetManager::setPattern(int lfo, std::initializer_list<float> values, StepInterp interp, int length)
{
    if (lfo < 0 || lfo >= kNumLfo) return;
    auto& p = performers.p[(size_t) lfo];
    int i = 0;
    for (float v : values) { if (i >= PerformerPattern::kMaxSteps) break; p.setStep(i++, v, interp, 0.0f); }
    p.setLength(length > 0 ? length : i);
}

void PresetManager::initPatch()
{
    for (auto* p : apvts.processor.getParameters())
        p->setValueNotifyingHost(p->getDefaultValue());
    resetPatterns();
    setName("Init");
}

//==============================================================================
// Preset factory: ogni funzione parte da initPatch() e sovrascrive. Indici delle scelte = ordine nel layout.
namespace
{
    enum { WarpNone, WarpFM, WarpFormant, WarpFold };
    enum { FLp12, FLp24, FLp48, FHp12, FHp24, FHp48, FComb, FVowel };
    enum { LfoSine, LfoTri, LfoSawUp, LfoSawDown, LfoSquare, LfoSH, LfoSmooth, LfoPerformer };
    enum { DrvDiode, DrvTube, DrvFolder };

    struct FactoryDef { const char* name; void (*build)(PresetManager&); };

    // Divisioni del tempo (indice nella lista kSyncNames)
    enum { Div4Bar, Div2Bar, Div1Bar, Div1_2, Div1_2T, Div1_4, Div1_4T, Div1_8, Div1_8T, Div1_16, Div1_16T, Div1_32 };
    // Ottava del sub (indice nella lista: "-3", "-2", "-1", "0")
    enum { SubOctM3, SubOctM2, SubOctM1, SubOct0 };
    enum { SubSine, SubTri, SubSquare, SubSaw };

    /** Base comune: matrice vuota, sub spento, envelope d'ampiezza da basso. */
    void base(PresetManager& m)
    {
        m.clearSlots();
        m.set(ids::subOn, 0);
        m.set(ids::env(0, "a"), 0.002f); m.set(ids::env(0, "d"), 0.3f); m.set(ids::env(0, "s"), 0.8f); m.set(ids::env(0, "r"), 0.12f);
    }

    void sub(PresetManager& m, float level, int wave = SubSine, int oct = SubOctM1, bool direct = true)
    {
        m.set(ids::subOn, 1); m.set(ids::subLevel, level); m.set(ids::subWave, wave); m.set(ids::subOct, oct); m.set(ids::subDirect, direct ? 1 : 0);
    }

    //==========================================================================
    // ROCK
    void rockPickBass(PresetManager& m)      // basso elettrico suonato col plettro
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.55f); m.set(ids::osc(0, "level"), 0.6f); m.set(ids::osc(0, "uni"), 1);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.35f); m.set(ids::osc(1, "level"), 0.25f); m.set(ids::osc(1, "semi"), 12); m.set(ids::osc(1, "fine"), 4);
        sub(m, 0.45f, SubSine, SubOctM1, false);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 750); m.set(ids::filt(0, "res"), 0.2f); m.set(ids::filt(0, "keytrack"), 0.4f);
        m.set(ids::env(1, "a"), 0.001f); m.set(ids::env(1, "d"), 0.16f); m.set(ids::env(1, "s"), 0.1f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.32f);         // l'attacco del plettro
        m.connect(1, SrcVelocity, DstF1Cutoff, 0.15f);
        m.set(ids::env(0, "d"), 0.8f); m.set(ids::env(0, "s"), 0.55f); m.set(ids::env(0, "r"), 0.09f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvTube); m.set(ids::drvAmount, 0.28f); m.set(ids::drvBias, 0.2f);
        m.set(ids::masterGain, -3.0f);
    }

    void rockFuzzBass(PresetManager& m)      // basso distorto con fuzz
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.9f); m.set(ids::osc(0, "level"), 0.5f);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.9f); m.set(ids::osc(1, "level"), 0.45f); m.set(ids::osc(1, "fine"), 6);
        sub(m, 0.4f, SubSine, SubOctM1, true);
        m.set(ids::filt(0, "type"), FLp12); m.set(ids::filt(0, "cutoff"), 2200); m.set(ids::filt(0, "res"), 0.1f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FHp12); m.set(ids::filt(1, "cutoff"), 70);
        m.set(ids::env(0, "s"), 0.9f); m.set(ids::env(0, "r"), 0.1f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvDiode); m.set(ids::drvAmount, 0.65f); m.set(ids::drvOs, 1); m.set(ids::drvOut, -4.0f);
        m.set(ids::masterGain, -4.0f);
    }

    void rockLead(PresetManager& m)          // lead per melodie e assoli
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 1.0f); m.set(ids::osc(0, "level"), 0.5f); m.set(ids::osc(0, "uni"), 3); m.set(ids::osc(0, "det"), 14);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.7f); m.set(ids::osc(1, "level"), 0.22f); m.set(ids::osc(1, "semi"), 12);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 3200); m.set(ids::filt(0, "res"), 0.25f);
        m.set(ids::env(0, "a"), 0.01f); m.set(ids::env(0, "s"), 0.9f); m.set(ids::env(0, "r"), 0.3f);
        m.set(ids::lfo(0, "shape"), LfoSine); m.set(ids::lfo(0, "rate"), 5.5f); m.set(ids::lfo(0, "fade"), 450.0f);
        m.connect(0, SrcLfo1, DstPitch, 0.006f);            // vibrato leggero che entra piano
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvTube); m.set(ids::drvAmount, 0.35f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8); m.set(ids::dlyMix, 0.18f); m.set(ids::dlyFb, 0.3f);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.18f);
        m.set(ids::masterGain, -6.0f);
    }

    //==========================================================================
    // DRUM & BASS
    void dnbReese(PresetManager& m)          // il reese classico: due saw scordate che "girano"
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.9f); m.set(ids::osc(0, "level"), 0.42f); m.set(ids::osc(0, "uni"), 3); m.set(ids::osc(0, "det"), 18);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.9f); m.set(ids::osc(1, "level"), 0.42f); m.set(ids::osc(1, "fine"), 13); m.set(ids::osc(1, "uni"), 3); m.set(ids::osc(1, "det"), 26);
        sub(m, 0.6f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 850); m.set(ids::filt(0, "res"), 0.2f);
        m.set(ids::lfo(0, "shape"), LfoSmooth); m.set(ids::lfo(0, "rate"), 0.35f);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.18f);
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.15f);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.35f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.35f);
        m.set(ids::masterGain, -1.5f);
    }

    void dnbNeuro(PresetManager& m)          // neurofunk: formanti che si muovono a tempo
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.75f); m.set(ids::osc(0, "level"), 0.55f);
        m.set(ids::osc(0, "warp"), WarpFormant); m.set(ids::osc(0, "wamt"), 0.4f); m.set(ids::osc(0, "uni"), 2); m.set(ids::osc(0, "det"), 10);
        sub(m, 0.5f);
        m.set(ids::filt(0, "type"), FVowel); m.set(ids::filt(0, "cutoff"), 1500); m.set(ids::filt(0, "res"), 0.4f);
        m.set(ids::lfo(0, "shape"), LfoTri); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), Div1_8);
        m.set(ids::lfo(1, "shape"), LfoSH);  m.set(ids::lfo(1, "sync"), 1); m.set(ids::lfo(1, "div"), Div1_16);
        m.connect(0, SrcLfo1, DstF1Vowel, 0.7f);
        m.connect(1, SrcLfo2, DstOsc1Wt, 0.22f);
        m.connect(2, SrcLfo1, DstF1Cutoff, 0.25f);
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.08f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvFolder); m.set(ids::drvAmount, 0.35f); m.set(ids::drvStages, 1); m.set(ids::drvOs, 2);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.6f);
        m.set(ids::masterGain, -5.0f);
    }

    void dnbLiquidSub(PresetManager& m)      // sub morbido e rotondo per la liquid
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.12f); m.set(ids::osc(0, "level"), 0.25f);
        sub(m, 0.9f, SubSine, SubOctM1, true);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 380); m.set(ids::filt(0, "res"), 0.1f);
        m.set(ids::env(1, "d"), 0.25f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.15f);
        m.set(ids::env(0, "a"), 0.004f); m.set(ids::env(0, "d"), 0.5f); m.set(ids::env(0, "s"), 0.85f); m.set(ids::env(0, "r"), 0.18f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.2f); m.set(ids::ottMix, 0.5f);
        m.set(ids::masterGain, -2.0f);
    }

    //==========================================================================
    // DUBSTEP
    void dubWobble(PresetManager& m)         // il wobble classico a 1/8
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.85f); m.set(ids::osc(0, "level"), 0.5f); m.set(ids::osc(0, "uni"), 4); m.set(ids::osc(0, "det"), 20);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.6f); m.set(ids::osc(1, "level"), 0.3f); m.set(ids::osc(1, "semi"), -12);
        sub(m, 0.6f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 280); m.set(ids::filt(0, "res"), 0.55f);
        m.set(ids::lfo(0, "shape"), LfoSine); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), Div1_8);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.75f);
        m.connect(1, SrcMacro1, DstLfo1Rate, 0.25f);         // Macro 1: wobble piu' veloce
        m.set(ids::env(0, "s"), 1.0f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvDiode); m.set(ids::drvAmount, 0.35f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.5f);
        m.set(ids::masterGain, -4.0f);
    }

    void dubGrowl(PresetManager& m)          // growl "parlante" con FM e filtro vocale
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.55f); m.set(ids::osc(0, "level"), 0.55f);
        m.set(ids::osc(0, "warp"), WarpFM); m.set(ids::osc(0, "wamt"), 0.5f); m.set(ids::osc(0, "uni"), 3); m.set(ids::osc(0, "det"), 12);
        sub(m, 0.5f);
        m.set(ids::filt(0, "type"), FVowel); m.set(ids::filt(0, "cutoff"), 1300); m.set(ids::filt(0, "res"), 0.35f);
        m.set(ids::lfo(0, "shape"), LfoTri); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), Div1_16);
        m.set(ids::lfo(1, "shape"), LfoSawDown); m.set(ids::lfo(1, "sync"), 1); m.set(ids::lfo(1, "div"), Div1_4);
        m.connect(0, SrcLfo1, DstF1Vowel, 0.9f);
        m.connect(1, SrcLfo2, DstOsc1Wt, 0.3f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FLp12); m.set(ids::filt(1, "cutoff"), 6000);
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.06f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvFolder); m.set(ids::drvAmount, 0.45f); m.set(ids::drvStages, 2); m.set(ids::drvOs, 2);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.65f);
        m.set(ids::masterGain, -6.0f);
    }

    void dubRiddim(PresetManager& m)         // riddim: suono "a scatti" guidato dal Performer
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.35f); m.set(ids::osc(0, "level"), 0.6f);
        m.set(ids::osc(0, "warp"), WarpFold); m.set(ids::osc(0, "wamt"), 0.55f);
        sub(m, 0.55f, SubSquare, SubOctM1, false);
        m.set(ids::filt(0, "type"), FLp12); m.set(ids::filt(0, "cutoff"), 900); m.set(ids::filt(0, "res"), 0.3f);
        m.set(ids::lfo(0, "shape"), LfoPerformer); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), Div1_16);
        m.setPattern(0, { 1.0f, -0.6f, 0.4f, -0.6f, 1.0f, 0.1f, -0.8f, 0.6f }, StepInterp::Step);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.6f);
        m.connect(1, SrcLfo1, DstAmp, 0.25f);
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.05f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FLp12); m.set(ids::filt(1, "cutoff"), 5000);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvFolder); m.set(ids::drvAmount, 0.22f); m.set(ids::drvOs, 2);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.55f);
        m.set(ids::masterGain, -5.0f);
    }

    //==========================================================================
    // TECHNO
    void technoAcid(PresetManager& m)        // acid alla TB-303: risonanza alta e envelope sul filtro
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 1.0f); m.set(ids::osc(0, "level"), 0.6f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 420); m.set(ids::filt(0, "res"), 0.8f); m.set(ids::filt(0, "drive"), 0.4f);
        m.set(ids::env(1, "a"), 0.001f); m.set(ids::env(1, "d"), 0.22f); m.set(ids::env(1, "s"), 0.0f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.5f);
        m.connect(1, SrcVelocity, DstF1Cutoff, 0.25f);        // accento: piu' forte = piu' aperto
        m.connect(2, SrcMacro1, DstF1Cutoff, 0.4f);          // Macro 1: la manopola "cutoff" da girare
        m.set(ids::env(0, "d"), 0.3f); m.set(ids::env(0, "s"), 0.7f); m.set(ids::env(0, "r"), 0.06f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvDiode); m.set(ids::drvAmount, 0.25f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8T); m.set(ids::dlyMix, 0.12f);
        m.set(ids::masterGain, -2.5f);
    }

    void technoRumble(PresetManager& m)      // rumble: basso scuro, saturo e riverberato
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.18f); m.set(ids::osc(0, "level"), 0.35f);
        sub(m, 0.85f, SubSine, SubOctM1, false);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 200); m.set(ids::filt(0, "res"), 0.15f);
        m.set(ids::env(0, "d"), 0.28f); m.set(ids::env(0, "s"), 0.25f); m.set(ids::env(0, "r"), 0.1f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvTube); m.set(ids::drvAmount, 0.6f); m.set(ids::drvBias, 0.15f);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.35f); m.set(ids::revSize, 0.8f); m.set(ids::revHpf, 40.0f);
        m.set(ids::masterGain, -4.0f);
    }

    void technoStab(PresetManager& m)        // stab ipnotico (accordo di quinta) con delay
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.8f); m.set(ids::osc(0, "level"), 0.45f);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.8f); m.set(ids::osc(1, "level"), 0.35f); m.set(ids::osc(1, "semi"), 7);
        m.set(ids::osc(2, "on"), 1); m.set(ids::osc(2, "wt"), 0.6f); m.set(ids::osc(2, "level"), 0.25f); m.set(ids::osc(2, "semi"), 12);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 1300); m.set(ids::filt(0, "res"), 0.3f);
        m.set(ids::env(1, "d"), 0.15f); m.set(ids::env(1, "s"), 0.0f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.3f);
        m.set(ids::env(0, "d"), 0.18f); m.set(ids::env(0, "s"), 0.0f); m.set(ids::env(0, "r"), 0.15f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8T); m.set(ids::dlyMix, 0.28f); m.set(ids::dlyFb, 0.45f); m.set(ids::dlyPing, 1);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.25f);
        m.set(ids::masterGain, -5.0f);
    }

    //==========================================================================
    // PROGRESSIVE
    void progPluck(PresetManager& m)         // pluck brillante con delay e riverbero
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.7f); m.set(ids::osc(0, "level"), 0.45f); m.set(ids::osc(0, "uni"), 5); m.set(ids::osc(0, "det"), 22);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.5f); m.set(ids::osc(1, "level"), 0.22f); m.set(ids::osc(1, "semi"), 12);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 650); m.set(ids::filt(0, "res"), 0.25f);
        m.set(ids::env(1, "d"), 0.3f); m.set(ids::env(1, "s"), 0.0f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.55f);
        m.set(ids::env(0, "d"), 0.45f); m.set(ids::env(0, "s"), 0.0f); m.set(ids::env(0, "r"), 0.35f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8T); m.set(ids::dlyMix, 0.25f); m.set(ids::dlyPing, 1);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.3f); m.set(ids::revSize, 0.7f);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.4f);
        m.set(ids::masterGain, -6.0f);
    }

    void progRollingBass(PresetManager& m)   // basso corto in controtempo, tipico della progressive
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.6f); m.set(ids::osc(0, "level"), 0.5f); m.set(ids::osc(0, "uni"), 2); m.set(ids::osc(0, "det"), 10);
        sub(m, 0.7f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 380); m.set(ids::filt(0, "res"), 0.3f);
        m.set(ids::env(1, "d"), 0.14f); m.set(ids::env(1, "s"), 0.0f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.35f);
        m.set(ids::env(0, "d"), 0.2f); m.set(ids::env(0, "s"), 0.3f); m.set(ids::env(0, "r"), 0.05f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.3f);
        m.set(ids::masterGain, -3.0f);
    }

    void progPad(PresetManager& m)           // pad ampio e arioso per gli accordi
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.45f); m.set(ids::osc(0, "level"), 0.4f); m.set(ids::osc(0, "uni"), 7); m.set(ids::osc(0, "det"), 28);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.6f); m.set(ids::osc(1, "level"), 0.3f); m.set(ids::osc(1, "fine"), 9); m.set(ids::osc(1, "uni"), 5); m.set(ids::osc(1, "det"), 20);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 1800); m.set(ids::filt(0, "res"), 0.15f);
        m.set(ids::lfo(0, "shape"), LfoSmooth); m.set(ids::lfo(0, "rate"), 0.15f);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.15f);
        m.set(ids::env(0, "a"), 0.6f); m.set(ids::env(0, "d"), 1.0f); m.set(ids::env(0, "s"), 0.85f); m.set(ids::env(0, "r"), 1.5f);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.4f); m.set(ids::revSize, 0.85f);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.6f);
        m.set(ids::masterGain, -7.0f);
    }

    //==========================================================================
    // Archetipi "stile Massive": suoni costruiti con le tecniche tipiche di quel synth
    void dnbHoover(PresetManager& m)         // hoover rave: saw larghissime con caduta di pitch
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 1.0f); m.set(ids::osc(0, "level"), 0.45f); m.set(ids::osc(0, "uni"), 7); m.set(ids::osc(0, "det"), 45); m.set(ids::osc(0, "spread"), 0.9f);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.9f); m.set(ids::osc(1, "level"), 0.35f); m.set(ids::osc(1, "semi"), -12); m.set(ids::osc(1, "uni"), 5); m.set(ids::osc(1, "det"), 35);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 2600); m.set(ids::filt(0, "res"), 0.15f);
        m.set(ids::env(2, "a"), 0.001f); m.set(ids::env(2, "d"), 0.25f); m.set(ids::env(2, "s"), 0.0f);
        m.connect(0, SrcEnv3, DstPitch, 0.1f);               // la nota "cade" in posizione: il marchio dell'hoover
        m.set(ids::env(0, "a"), 0.005f); m.set(ids::env(0, "s"), 0.95f); m.set(ids::env(0, "r"), 0.2f);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.5f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.3f);
        m.set(ids::masterGain, 0.0f);
    }

    void dubScreamLead(PresetManager& m)     // scream lead: FM, filtro comb e vibrato
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.9f); m.set(ids::osc(0, "level"), 0.5f); m.set(ids::osc(0, "warp"), WarpFM); m.set(ids::osc(0, "wamt"), 0.55f);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.8f); m.set(ids::osc(1, "level"), 0.3f); m.set(ids::osc(1, "semi"), 12); m.set(ids::osc(1, "warp"), WarpFormant); m.set(ids::osc(1, "wamt"), 0.5f);
        m.set(ids::filt(0, "type"), FComb); m.set(ids::filt(0, "cutoff"), 900); m.set(ids::filt(0, "res"), 0.45f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FLp24); m.set(ids::filt(1, "cutoff"), 3800);
        m.set(ids::env(2, "a"), 0.001f); m.set(ids::env(2, "d"), 0.12f); m.set(ids::env(2, "s"), 0.0f);
        m.connect(0, SrcEnv3, DstPitch, -0.04f);             // attacco che "sale" sulla nota
        m.set(ids::lfo(0, "shape"), LfoSine); m.set(ids::lfo(0, "rate"), 6.0f); m.set(ids::lfo(0, "fade"), 300.0f);
        m.connect(1, SrcLfo1, DstPitch, 0.008f);
        m.connect(2, SrcMacro1, DstF1Fb, 0.6f);              // Macro 1: piu' "urlo"
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.15f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvFolder); m.set(ids::drvAmount, 0.3f); m.set(ids::drvOs, 2);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.5f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8); m.set(ids::dlyMix, 0.15f);
        m.set(ids::masterGain, -8.0f);
    }

    void technoPerformerSeq(PresetManager& m) // sequenza ritmica: il Performer "taglia" il suono a sedicesimi
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.7f); m.set(ids::osc(0, "level"), 0.55f); m.set(ids::osc(0, "uni"), 2); m.set(ids::osc(0, "det"), 12);
        sub(m, 0.4f, SubSine, SubOctM1, false);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 800); m.set(ids::filt(0, "res"), 0.45f);
        m.set(ids::lfo(0, "shape"), LfoPerformer); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), Div1_16);
        m.setPattern(0, { 1.0f, -1.0f, 0.6f, -1.0f, 1.0f, -1.0f, -1.0f, 0.8f, 1.0f, -1.0f, 0.5f, 0.9f, -1.0f, 1.0f, -1.0f, 0.4f }, StepInterp::Step);
        m.connect(0, SrcLfo1, DstAmp, 0.95f);                // gate a sedicesimi
        m.connect(1, SrcLfo1, DstF1Cutoff, 0.3f);
        m.set(ids::env(0, "s"), 1.0f); m.set(ids::env(0, "r"), 0.08f);
        m.set(ids::dlyOn, 1); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, Div1_8T); m.set(ids::dlyMix, 0.2f); m.set(ids::dlyPing, 1);
        m.set(ids::masterGain, -4.0f);
    }

    void progSupersaw(PresetManager& m)      // supersaw da main room
    {
        base(m);
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 1.0f); m.set(ids::osc(0, "level"), 0.45f); m.set(ids::osc(0, "uni"), 9); m.set(ids::osc(0, "det"), 35); m.set(ids::osc(0, "spread"), 0.9f);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 1.0f); m.set(ids::osc(1, "level"), 0.25f); m.set(ids::osc(1, "semi"), 12); m.set(ids::osc(1, "uni"), 7); m.set(ids::osc(1, "det"), 30);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 5000); m.set(ids::filt(0, "res"), 0.1f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FHp12); m.set(ids::filt(1, "cutoff"), 150);
        m.set(ids::env(0, "a"), 0.01f); m.set(ids::env(0, "s"), 0.9f); m.set(ids::env(0, "r"), 0.4f);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.28f); m.set(ids::revSize, 0.75f);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.4f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.25f);
        m.set(ids::masterGain, 0.0f);
    }

    const FactoryDef kFactory[] = {
        { "Init",                        [](PresetManager&) {} },
        { "Rock - Basso Plettro",        rockPickBass },
        { "Rock - Fuzz Bass",            rockFuzzBass },
        { "Rock - Lead",                 rockLead },
        { "DnB - Reese",                 dnbReese },
        { "DnB - Neuro",                 dnbNeuro },
        { "DnB - Sub Liquido",           dnbLiquidSub },
        { "DnB - Hoover",                dnbHoover },
        { "Dubstep - Wobble",            dubWobble },
        { "Dubstep - Growl",             dubGrowl },
        { "Dubstep - Riddim",            dubRiddim },
        { "Dubstep - Scream Lead",       dubScreamLead },
        { "Techno - Acid",               technoAcid },
        { "Techno - Rumble",             technoRumble },
        { "Techno - Stab",               technoStab },
        { "Techno - Sequenza Performer", technoPerformerSeq },
        { "Progressive - Pluck",         progPluck },
        { "Progressive - Basso Rolling", progRollingBass },
        { "Progressive - Pad",           progPad },
        { "Progressive - Supersaw",      progSupersaw },
    };
}

int PresetManager::numFactory() noexcept { return (int) (sizeof(kFactory) / sizeof(kFactory[0])); }
juce::String PresetManager::factoryName(int i) { return (i >= 0 && i < numFactory()) ? kFactory[i].name : "?"; }

void PresetManager::loadFactory(int i)
{
    if (i < 0 || i >= numFactory()) return;
    initPatch();
    kFactory[i].build(*this);
    setName(kFactory[i].name);
}

//==============================================================================
juce::File PresetManager::userDir() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("BassForge").getChildFile("Presets");
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userDir().findChildFiles(juce::File::findFiles, false, juce::String("*") + kExt);
    files.sort();
    return files;
}

bool PresetManager::save(const juce::String& rawName)
{
    const auto name = sanitize(rawName);
    if (name.isEmpty()) return false;
    const auto dir = userDir();
    if (! dir.createDirectory().wasOk()) return false;

    setName(name);
    auto state = apvts.copyState();
    state.removeChild(state.getChildWithName("PERFORMERS"), nullptr);
    state.appendChild(performersToTree(performers), nullptr);
    auto xml = state.createXml();
    return xml != nullptr && dir.getChildFile(name + kExt).replaceWithText(xml->toString());
}

bool PresetManager::load(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType())) return false;
    const auto state = juce::ValueTree::fromXml(*xml);
    performersFromTree(performers, state.getChildWithName("PERFORMERS"));
    apvts.replaceState(state);
    setName(file.getFileNameWithoutExtension());
    return true;
}
}
