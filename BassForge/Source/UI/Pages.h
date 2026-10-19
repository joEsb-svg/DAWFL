#pragma once
#include <deque>
#include "KnobKit.h"
#include "WavetableView.h"
#include "EnvelopeView.h"
#include "PerformerEditor.h"
#include "FX/MasterFXChain.h"

namespace bf::ui
{
    class PageBase;

    /** Un pannello con i suoi controlli (knob / toggle / combo) disposti in griglia. I controlli sono posseduti dalla pagina. */
    class Group
    {
    public:
        Group(PageBase& owner, const juce::String& title, int cols);
        ParamKnob&   knob(const juce::String& id, const juce::String& title, int dest = DstNone, ModMapper m = {});
        ParamToggle& toggle(const juce::String& id, const juce::String& title);
        ParamCombo&  combo(const juce::String& id, const juce::String& title);
        void layout() { layoutGrid(panel.content(), items, cols); }

        Panel panel;
        std::vector<juce::Component*> items;
        int cols;

    private:
        PageBase& owner;
    };

    class PageBase : public juce::Component
    {
    public:
        PageBase(Apvts& a, ModRingModel& r) : apvts(a), ring(r) {}
        Apvts& apvts;
        ModRingModel& ring;
        void own(std::unique_ptr<juce::Component> c) { owned.push_back(std::move(c)); }

    protected:
        Group& addGroup(const juce::String& title, int cols) { groups.emplace_back(*this, title, cols); return groups.back(); }
        std::deque<Group> groups;
        std::vector<std::unique_ptr<juce::Component>> owned;
    };

    /** 3 oscillatori (vista wavetable + unison + warp) e, sotto, Sub / Noise / Sample. */
    class OscillatorPage : public PageBase
    {
    public:
        OscillatorPage(Apvts&, ModRingModel&, const WavetableBank&, const ModMonitor&);
        void resized() override;
    private:
        std::array<Group*, kNumOsc> osc{};
        std::array<std::unique_ptr<WavetableView>, kNumOsc> views;
        Group *sub = nullptr, *noise = nullptr, *sample = nullptr;
    };

    /** Filtro 1/2 (LP/HP 12-48, Comb, Vowel), routing/morph e filtro tonale di noise+sample. */
    class FilterPage : public PageBase
    {
    public:
        FilterPage(Apvts&, ModRingModel&);
        void resized() override;
    private:
        std::array<Group*, kNumFilter> flt{};
        Group *routing = nullptr, *noiseFlt = nullptr;
    };

    /** Envelope / LFO-Performer / Macro + Matrice (selettore a 3 viste). */
    class ModulatorPage : public PageBase
    {
    public:
        ModulatorPage(Apvts&, ModRingModel&, PerformerBank&);
        void resized() override;
    private:
        void showView(int v);
        std::array<Group*, kNumEnv> env{};
        std::array<Group*, kNumLfo> lfo{};
        Group* macros = nullptr;
        Panel matrixPanel { "Modulation Matrix" };
        struct Row { std::unique_ptr<juce::Label> idx; ParamCombo* src; ParamCombo* dst; ParamBar* amt; };
        std::vector<Row> rows;
        std::array<std::unique_ptr<EnvelopeView>, kNumEnv> envViews;
        std::unique_ptr<PerformerEditor> performer;
        Panel performerPanel { "Performer (32 steps)" };
        std::array<juce::TextButton, kNumLfo> perfTargets;
        std::array<juce::TextButton, 4> viewButtons;
        int view = 0;
    };

    /** OTT, Hyper-Drive, Dimension, Delay, Reverb + ordine della catena. */
    class FxPage : public PageBase
    {
    public:
        FxPage(Apvts&, ModRingModel&);
        void resized() override;
    private:
        Group *ott = nullptr, *drive = nullptr, *dim = nullptr, *delay = nullptr, *reverb = nullptr, *chain = nullptr;
        juce::Label chainLabel;
        ParamCombo* orderCombo = nullptr;
    };
}
