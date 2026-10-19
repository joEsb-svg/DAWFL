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

    void neuroReese(PresetManager& m)
    {
        m.clearSlots();
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.85f); m.set(ids::osc(0, "level"), 0.45f);
        m.set(ids::osc(0, "uni"), 5); m.set(ids::osc(0, "det"), 22);
        m.set(ids::osc(1, "on"), 1); m.set(ids::osc(1, "wt"), 0.80f); m.set(ids::osc(1, "level"), 0.40f);
        m.set(ids::osc(1, "fine"), 9); m.set(ids::osc(1, "uni"), 5); m.set(ids::osc(1, "det"), 30);
        m.set(ids::subLevel, 0.55f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 650); m.set(ids::filt(0, "res"), 0.45f); m.set(ids::filt(0, "drive"), 0.3f);
        m.set(ids::filt(1, "on"), 1); m.set(ids::filt(1, "type"), FHp12); m.set(ids::filt(1, "cutoff"), 90);
        m.set(ids::lfo(0, "shape"), LfoSmooth); m.set(ids::lfo(0, "rate"), 0.7f);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.45f);
        m.connect(1, SrcEnv2, DstF1Cutoff, 0.20f);
        m.connect(2, SrcLfo1, DstOsc1Wt, 0.15f);
        m.set(ids::env(1, "d"), 0.45f); m.set(ids::env(1, "s"), 0.0f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.55f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvTube); m.set(ids::drvAmount, 0.35f); m.set(ids::drvBias, 0.3f); m.set(ids::drvOs, 1);
        m.set(ids::dimOn, 1); m.set(ids::dimAmount, 0.45f);
        m.set(ids::masterGain, -3.0f);
    }

    void dubstepGrowl(PresetManager& m)
    {
        m.clearSlots();
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.55f); m.set(ids::osc(0, "level"), 0.55f);
        m.set(ids::osc(0, "warp"), WarpFM); m.set(ids::osc(0, "wamt"), 0.45f); m.set(ids::osc(0, "uni"), 3); m.set(ids::osc(0, "det"), 12);
        m.set(ids::subLevel, 0.5f);
        m.set(ids::filt(0, "type"), FVowel); m.set(ids::filt(0, "cutoff"), 1200); m.set(ids::filt(0, "res"), 0.35f); m.set(ids::filt(0, "vowel"), 0.2f);
        m.set(ids::lfo(0, "shape"), LfoTri); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), 7);     // 1/8
        m.connect(0, SrcLfo1, DstF1Vowel, 0.85f);
        m.connect(1, SrcLfo1, DstOsc1Wt, 0.2f);
        m.connect(2, SrcMacro1, DstF1Cutoff, 0.3f);
        m.set(ids::macro(0), 0.5f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvFolder); m.set(ids::drvAmount, 0.4f); m.set(ids::drvStages, 2); m.set(ids::drvOs, 2);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.5f);
        m.set(ids::dlyOn, 1); m.set(ids::dlyMix, 0.15f); m.set(ids::dlySync, 1); m.set(ids::dlyDiv, 7);
        m.set(ids::masterGain, -4.0f);
    }

    void dnbRollingSub(PresetManager& m)
    {
        m.clearSlots();
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.12f); m.set(ids::osc(0, "level"), 0.28f);
        m.set(ids::subLevel, 0.9f); m.set(ids::subWave, 0); m.set(ids::subOct, 2); m.set(ids::subDirect, 1);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 320); m.set(ids::filt(0, "res"), 0.15f);
        m.connect(0, SrcEnv2, DstF1Cutoff, 0.12f);
        m.set(ids::env(0, "a"), 0.003f); m.set(ids::env(0, "d"), 0.4f); m.set(ids::env(0, "s"), 0.85f); m.set(ids::env(0, "r"), 0.12f);
        m.set(ids::ottOn, 1); m.set(ids::ottDepth, 0.25f); m.set(ids::ottMix, 0.6f);
        m.set(ids::masterGain, -2.0f);
    }

    void performerWobble(PresetManager& m)
    {
        m.clearSlots();
        m.set(ids::osc(0, "on"), 1); m.set(ids::osc(0, "wt"), 0.7f); m.set(ids::osc(0, "level"), 0.5f); m.set(ids::osc(0, "uni"), 4); m.set(ids::osc(0, "det"), 18);
        m.set(ids::subLevel, 0.5f);
        m.set(ids::filt(0, "type"), FLp24); m.set(ids::filt(0, "cutoff"), 350); m.set(ids::filt(0, "res"), 0.5f);
        m.set(ids::lfo(0, "shape"), LfoPerformer); m.set(ids::lfo(0, "sync"), 1); m.set(ids::lfo(0, "div"), 9);       // 1/16 per step
        m.set(ids::lfo(0, "pmode"), 0);
        m.setPattern(0, { 0.9f, -0.2f, 0.6f, -0.8f, 0.3f, 0.3f, -0.5f, 0.8f }, StepInterp::Linear);
        m.connect(0, SrcLfo1, DstF1Cutoff, 0.7f);
        m.set(ids::drvOn, 1); m.set(ids::drvType, DrvDiode); m.set(ids::drvAmount, 0.3f);
        m.set(ids::revOn, 1); m.set(ids::revMix, 0.12f);
        m.set(ids::masterGain, -3.0f);
    }

    const FactoryDef kFactory[] = {
        { "Init",                 [](PresetManager&) {} },
        { "Neuro Reese",          neuroReese },
        { "Dubstep Growl",        dubstepGrowl },
        { "DnB Rolling Sub",      dnbRollingSub },
        { "Performer Wobble",     performerWobble },
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
