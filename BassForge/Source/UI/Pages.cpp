#include "Pages.h"

namespace bf::ui
{
    //==========================================================================
    Group::Group(PageBase& o, const juce::String& title, int c) : panel(title), cols(c), owner(o)
    {
        owner.addAndMakeVisible(panel);
    }

    ParamKnob& Group::knob(const juce::String& id, const juce::String& title, int dest, ModMapper m)
    {
        auto k = std::make_unique<ParamKnob>(owner.apvts, id, title, &owner.ring, dest, std::move(m));
        auto& r = *k; panel.addAndMakeVisible(r); items.push_back(&r); owner.own(std::move(k));
        return r;
    }

    ParamToggle& Group::toggle(const juce::String& id, const juce::String& title)
    {
        auto k = std::make_unique<ParamToggle>(owner.apvts, id, title);
        auto& r = *k; panel.addAndMakeVisible(r); items.push_back(&r); owner.own(std::move(k));
        return r;
    }

    ParamCombo& Group::combo(const juce::String& id, const juce::String& title)
    {
        auto k = std::make_unique<ParamCombo>(owner.apvts, id, title);
        auto& r = *k; panel.addAndMakeVisible(r); items.push_back(&r); owner.own(std::move(k));
        return r;
    }

    //==========================================================================
    OscillatorPage::OscillatorPage(Apvts& a, ModRingModel& r, const WavetableBank& bank, const ModMonitor& mon)
        : PageBase(a, r)
    {
        for (int i = 0; i < kNumOsc; ++i)
        {
            auto& g = addGroup("Oscillator " + juce::String(i + 1), 5);
            osc[(size_t) i] = &g;
            g.toggle(ids::osc(i, "on"), "On");
            g.knob(ids::osc(i, "wt"),    "WT Pos", DstOsc1Wt + i,    mapper::linear(1.0));
            g.knob(ids::osc(i, "semi"),  "Semi",   DstOsc1Pitch + i, mapper::semis(modscale::kOscPitchSemis));
            g.knob(ids::osc(i, "fine"),  "Fine");
            g.knob(ids::osc(i, "level"), "Level",  DstOsc1Level + i, mapper::linear(1.0));
            g.knob(ids::osc(i, "pan"),   "Pan");
            g.knob(ids::osc(i, "uni"),   "Unison");
            g.knob(ids::osc(i, "det"),   "Detune");
            g.knob(ids::osc(i, "spread"),"Spread");
            g.knob(ids::osc(i, "blend"), "Blend");
            g.combo(ids::osc(i, "warp"), "Warp");
            g.knob(ids::osc(i, "wamt"),  "Warp Amt");
            g.knob(ids::osc(i, "waux"),  "Warp Aux");

            views[(size_t) i] = std::make_unique<WavetableView>(bank, a, mon, i);
            g.panel.addAndMakeVisible(*views[(size_t) i]);
        }

        sub = &addGroup("Sub", 4);
        sub->toggle(ids::subOn, "On");
        sub->combo(ids::subWave, "Wave");
        sub->combo(ids::subOct, "Octave");
        sub->knob(ids::subLevel, "Level", DstSubLevel, mapper::linear(1.0));
        sub->knob(ids::subPhase, "Phase");
        sub->toggle(ids::subReset, "Phase Reset");
        sub->toggle(ids::subDirect, "Direct Out");
        sub->toggle(ids::subFollowMod, "Follow Mod");

        noise = &addGroup("Noise", 2);
        noise->toggle(ids::noiseOn, "On");
        noise->combo(ids::noiseType, "Type");
        noise->knob(ids::noiseLevel, "Level", DstNoiseLevel, mapper::linear(1.0));
        noise->knob(ids::noiseWidth, "Width");

        sample = &addGroup("Sample Attack", 5);
        sample->toggle(ids::smpOn, "On");
        sample->combo(ids::smpSlot, "Slot");
        sample->knob(ids::smpLevel, "Level");
        sample->knob(ids::smpPitch, "Pitch");
        sample->knob(ids::smpPitchTrack, "Pitch Trk");
        sample->toggle(ids::smpLoop, "Loop");
        sample->knob(ids::smpLoopStart, "Loop Start");
        sample->knob(ids::smpLoopEnd, "Loop End");
        sample->knob(ids::smpDecay, "Decay");
    }

    void OscillatorPage::resized()
    {
        auto b = getLocalBounds().reduced(6);
        auto top = b.removeFromTop((int) ((float) b.getHeight() * 0.64f));
        const int cw = top.getWidth() / kNumOsc;
        for (int i = 0; i < kNumOsc; ++i)
        {
            auto col_ = top.removeFromLeft(cw).reduced(4);
            auto& g = *osc[(size_t) i];
            g.panel.setBounds(col_);
            auto c = g.panel.content();
            views[(size_t) i]->setBounds(c.removeFromTop((int) ((float) c.getHeight() * 0.40f)).reduced(0, 2));
            layoutGrid(c, g.items, 5);
        }
        b.removeFromTop(4);
        const int w = b.getWidth();
        auto s = b.removeFromLeft((int) ((float) w * 0.36f)).reduced(4);
        auto n = b.removeFromLeft((int) ((float) w * 0.22f)).reduced(4);
        sub->panel.setBounds(s);       sub->layout();
        noise->panel.setBounds(n);     noise->layout();
        sample->panel.setBounds(b.reduced(4)); sample->layout();
    }

    //==========================================================================
    FilterPage::FilterPage(Apvts& a, ModRingModel& r) : PageBase(a, r)
    {
        for (int i = 0; i < kNumFilter; ++i)
        {
            auto& g = addGroup("Filter " + juce::String(i + 1), 5);
            flt[(size_t) i] = &g;
            g.toggle(ids::filt(i, "on"), "On");
            g.combo(ids::filt(i, "type"), "Type");
            g.knob(ids::filt(i, "cutoff"), "Cutoff", DstF1Cutoff + i, mapper::octaves(modscale::kCutoffOct));
            g.knob(ids::filt(i, "res"),    "Resonance", DstF1Res + i, mapper::linear(1.0));
            g.knob(ids::filt(i, "drive"),  "Drive",     DstF1Drive + i, mapper::linear(1.0));
            g.knob(ids::filt(i, "keytrack"), "Keytrack");
            g.knob(ids::filt(i, "fb"),     "Feedback",  DstF1Fb + i, mapper::linear(1.0));
            g.knob(ids::filt(i, "tune"),   "Comb Tune");
            g.knob(ids::filt(i, "vowel"),  "Vowel",     DstF1Vowel + i, mapper::linear(1.0));
            g.knob(ids::filt(i, "fshift"), "Formant Shift");
        }
        routing = &addGroup("Routing", 2);
        routing->combo(ids::filterRouting, "Routing");
        routing->knob(ids::filterMorph, "Morph", DstFilterMorph, mapper::linear(1.0));

        noiseFlt = &addGroup("Noise / Sample Filter", 3);
        noiseFlt->toggle(ids::nfltOn, "On");
        noiseFlt->combo(ids::nfltType, "Type");
        noiseFlt->knob(ids::nfltCutoff, "Cutoff");
        noiseFlt->knob(ids::nfltRes, "Resonance");
        noiseFlt->knob(ids::nfltTrack, "Keytrack");
    }

    void FilterPage::resized()
    {
        auto b = getLocalBounds().reduced(6);
        auto left = b.removeFromLeft((int) ((float) b.getWidth() * 0.70f));
        const int h = left.getHeight() / kNumFilter;
        for (int i = 0; i < kNumFilter; ++i)
        {
            flt[(size_t) i]->panel.setBounds(left.removeFromTop(h).reduced(4));
            flt[(size_t) i]->layout();
        }
        routing->panel.setBounds(b.removeFromTop((int) ((float) b.getHeight() * 0.38f)).reduced(4));
        routing->layout();
        noiseFlt->panel.setBounds(b.reduced(4));
        noiseFlt->layout();
    }

    //==========================================================================
    ModulatorPage::ModulatorPage(Apvts& a, ModRingModel& r, PerformerBank& bank) : PageBase(a, r)
    {
        for (int i = 0; i < kNumEnv; ++i)
        {
            auto& g = addGroup("Envelope " + juce::String(i + 1) + (i == 0 ? "  (Amp)" : ""), 4);
            env[(size_t) i] = &g;
            const auto t = mapper::octaves(modscale::kEnvTimeOct);
            g.knob(ids::env(i, "a"), "Attack",  DstEnv1Time + i, t);
            g.knob(ids::env(i, "h"), "Hold",    DstEnv1Time + i, t);
            g.knob(ids::env(i, "d"), "Decay",   DstEnv1Time + i, t);
            g.knob(ids::env(i, "s"), "Sustain");
            g.knob(ids::env(i, "r"), "Release", DstEnv1Time + i, t);
            g.knob(ids::env(i, "ac"), "Atk Curve", DstEnv1Curve + i, mapper::linear(1.0));
            g.knob(ids::env(i, "dc"), "Dec Curve", DstEnv1Curve + i, mapper::linear(1.0));
            g.knob(ids::env(i, "rc"), "Rel Curve", DstEnv1Curve + i, mapper::linear(1.0));
            envViews[(size_t) i] = std::make_unique<EnvelopeView>(a, i);
            g.panel.addAndMakeVisible(*envViews[(size_t) i]);
        }
        for (int i = 0; i < kNumLfo; ++i)
        {
            auto& g = addGroup("LFO / Performer " + juce::String(i + 1), 5);
            lfo[(size_t) i] = &g;
            g.combo(ids::lfo(i, "shape"), "Shape");
            g.knob(ids::lfo(i, "rate"), "Rate", DstLfo1Rate + i, mapper::octaves(modscale::kLfoRateOct));
            g.toggle(ids::lfo(i, "sync"), "Tempo Sync");
            g.combo(ids::lfo(i, "div"), "Division");
            g.knob(ids::lfo(i, "phase"), "Phase");
            g.knob(ids::lfo(i, "fade"), "Fade In");
            g.toggle(ids::lfo(i, "retrig"), "Retrigger");
            g.combo(ids::lfo(i, "pmode"), "Perf. Mode");
            g.toggle(ids::lfo(i, "phold"), "One-Shot Hold");
            g.toggle(ids::lfo(i, "pstep"), "Rate / Step");
        }
        macros = &addGroup("Macros", 6);
        for (int i = 0; i < kNumMacro; ++i) macros->knob(ids::macro(i), "Macro " + juce::String(i + 1));

        addAndMakeVisible(matrixPanel);
        for (int i = 0; i < kNumSlots; ++i)
        {
            Row row;
            row.idx = std::make_unique<juce::Label>();
            row.idx->setText(juce::String(i + 1), juce::dontSendNotification);
            row.idx->setJustificationType(juce::Justification::centred);
            row.idx->setColour(juce::Label::textColourId, col::orange);
            auto s = std::make_unique<ParamCombo>(a, ids::slot(i, "src"), "");
            auto d = std::make_unique<ParamCombo>(a, ids::slot(i, "dst"), "");
            auto m = std::make_unique<ParamBar>(a, ids::slot(i, "amt"));
            row.src = s.get(); row.dst = d.get(); row.amt = m.get();
            matrixPanel.addAndMakeVisible(*row.idx);
            matrixPanel.addAndMakeVisible(*s); matrixPanel.addAndMakeVisible(*d); matrixPanel.addAndMakeVisible(*m);
            own(std::move(s)); own(std::move(d)); own(std::move(m));
            rows.push_back(std::move(row));
        }

        performer = std::make_unique<PerformerEditor>(bank, a);
        addAndMakeVisible(performerPanel);
        performerPanel.addAndMakeVisible(*performer);
        for (int i = 0; i < kNumLfo; ++i)
        {
            auto& bt = perfTargets[(size_t) i];
            bt.setButtonText("LFO " + juce::String(i + 1));
            bt.setClickingTogglesState(true);
            bt.setRadioGroupId(4712);
            bt.onClick = [this, i] { performer->setTarget(i); };
            performerPanel.addAndMakeVisible(bt);
        }
        perfTargets[0].setToggleState(true, juce::dontSendNotification);

        const char* names[4] = { "Envelopes", "LFO Settings", "Macros & Matrix", "Performer" };
        for (int i = 0; i < 4; ++i)
        {
            auto& bt = viewButtons[(size_t) i];
            bt.setButtonText(names[i]);
            bt.setClickingTogglesState(true);
            bt.setRadioGroupId(4711);
            bt.onClick = [this, i] { showView(i); };
            addAndMakeVisible(bt);
        }
        viewButtons[0].setToggleState(true, juce::dontSendNotification);
        showView(0);
    }

    void ModulatorPage::showView(int v)
    {
        view = v;
        for (int i = 0; i < kNumEnv; ++i) env[(size_t) i]->panel.setVisible(v == 0);
        for (int i = 0; i < kNumLfo; ++i) lfo[(size_t) i]->panel.setVisible(v == 1);
        macros->panel.setVisible(v == 2);
        matrixPanel.setVisible(v == 2);
        performerPanel.setVisible(v == 3);
        resized();
    }

    void ModulatorPage::resized()
    {
        auto b = getLocalBounds().reduced(6);
        auto bar = b.removeFromTop(34);
        for (int i = 0; i < 4; ++i) viewButtons[(size_t) i].setBounds(bar.removeFromLeft(150));

        if (view == 3)
        {
            performerPanel.setBounds(b.reduced(4));
            auto c = performerPanel.content();
            auto tb = c.removeFromTop(28);
            for (auto& t : perfTargets) t.setBounds(tb.removeFromLeft(80).reduced(2));
            performer->setBounds(c);
        }
        else if (view == 0 || view == 1)
        {
            const std::vector<Group*> set = view == 0 ? std::vector<Group*>(env.begin(), env.end()) : std::vector<Group*>(lfo.begin(), lfo.end());
            const int cw = b.getWidth() / 2, ch = b.getHeight() / 2;
            for (int i = 0; i < 4; ++i)
            {
                set[(size_t) i]->panel.setBounds(juce::Rectangle<int>(b.getX() + (i % 2) * cw, b.getY() + (i / 2) * ch, cw, ch).reduced(4));
                if (view == 0)
                {
                    auto c = set[(size_t) i]->panel.content();
                    envViews[(size_t) i]->setBounds(c.removeFromTop((int) ((float) c.getHeight() * 0.42f)).reduced(0, 2));
                    layoutGrid(c, set[(size_t) i]->items, set[(size_t) i]->cols);
                }
                else set[(size_t) i]->layout();
            }
        }
        else
        {
            macros->panel.setBounds(b.removeFromTop((int) ((float) b.getHeight() * 0.27f)).reduced(4));
            macros->cols = 12;
            macros->layout();
            matrixPanel.setBounds(b.reduced(4));
            auto c = matrixPanel.content();
            const int colW = c.getWidth() / 2, rowH = c.getHeight() / 6;
            for (int i = 0; i < kNumSlots; ++i)
            {
                auto cell = juce::Rectangle<int>(c.getX() + (i / 6) * colW, c.getY() + (i % 6) * rowH, colW, rowH).reduced(6, 3);
                auto& r = rows[(size_t) i];
                r.idx->setBounds(cell.removeFromLeft(22));
                const int cw = (int) ((float) cell.getWidth() * 0.30f);
                r.src->setBounds(cell.removeFromLeft(cw));
                r.dst->setBounds(cell.removeFromLeft(cw));
                r.amt->setBounds(cell.reduced(4, 0));
            }
        }
    }

    //==========================================================================
    FxPage::FxPage(Apvts& a, ModRingModel& r) : PageBase(a, r)
    {
        ott = &addGroup("Multiband Compressor (OTT)", 6);
        ott->toggle(ids::ottOn, "On");
        ott->knob(ids::ottDepth, "Depth");
        ott->knob(ids::ottTime, "Time");
        ott->knob(ids::ottUp, "Upward");
        ott->knob(ids::ottDown, "Downward");
        ott->knob(ids::ottMix, "Mix");
        ott->knob(ids::ottLow, "Low Gain");
        ott->knob(ids::ottMid, "Mid Gain");
        ott->knob(ids::ottHigh, "High Gain");
        ott->knob(ids::ottX1, "Xover L/M");
        ott->knob(ids::ottX2, "Xover M/H");

        drive = &addGroup("Hyper-Drive", 4);
        drive->toggle(ids::drvOn, "On");
        drive->combo(ids::drvType, "Type");
        drive->knob(ids::drvAmount, "Amount");
        drive->knob(ids::drvBias, "Bias");
        drive->combo(ids::drvStages, "Fold Stages");
        drive->combo(ids::drvOs, "Oversampling");
        drive->knob(ids::drvOut, "Output");
        drive->knob(ids::drvMix, "Mix");

        dim = &addGroup("Dimension Expander", 4);
        dim->toggle(ids::dimOn, "On");
        dim->knob(ids::dimAmount, "Amount");
        dim->knob(ids::dimWidth, "Width");
        dim->knob(ids::dimMono, "Mono Below");
        dim->knob(ids::dimSize, "Size");
        dim->knob(ids::dimRate, "Rate");
        dim->knob(ids::dimMix, "Mix");

        delay = &addGroup("Master Delay", 5);
        delay->toggle(ids::dlyOn, "On");
        delay->knob(ids::dlyTime, "Time");
        delay->toggle(ids::dlySync, "Sync");
        delay->combo(ids::dlyDiv, "Division");
        delay->toggle(ids::dlyPing, "Ping-Pong");
        delay->knob(ids::dlyFb, "Feedback");
        delay->knob(ids::dlyDamp, "Damping");
        delay->knob(ids::dlyHpf, "Wet HPF");
        delay->knob(ids::dlyMix, "Mix");

        reverb = &addGroup("Master Reverb", 4);
        reverb->toggle(ids::revOn, "On");
        reverb->knob(ids::revSize, "Size");
        reverb->knob(ids::revDamp, "Damping");
        reverb->knob(ids::revWidth, "Width");
        reverb->knob(ids::revPre, "Pre-Delay");
        reverb->knob(ids::revHpf, "Wet HPF");
        reverb->knob(ids::revMix, "Mix");

        chain = &addGroup("Chain Order", 1);
        orderCombo = &chain->combo(ids::fxOrder, "First three stages");
        chainLabel.setJustificationType(juce::Justification::centred);
        chainLabel.setColour(juce::Label::textColourId, col::cyan);
        chainLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        chain->panel.addAndMakeVisible(chainLabel);
        auto update = [this]
        {
            const int i = juce::jlimit(0, MasterFXChain::kNumOrders - 1, orderCombo->combo.getSelectedItemIndex());
            chainLabel.setText(juce::String(MasterFXChain::kOrderNames[i]) + "  >  Delay  >  Reverb", juce::dontSendNotification);
        };
        orderCombo->combo.onChange = update;
        update();
    }

    void FxPage::resized()
    {
        auto b = getLocalBounds().reduced(6);
        auto row1 = b.removeFromTop(b.getHeight() / 2), row2 = b;
        const int w1 = row1.getWidth();
        ott->panel.setBounds(row1.removeFromLeft((int) ((float) w1 * 0.46f)).reduced(4));
        drive->panel.setBounds(row1.removeFromLeft((int) ((float) w1 * 0.30f)).reduced(4));
        dim->panel.setBounds(row1.reduced(4));
        const int w2 = row2.getWidth();
        delay->panel.setBounds(row2.removeFromLeft((int) ((float) w2 * 0.40f)).reduced(4));
        reverb->panel.setBounds(row2.removeFromLeft((int) ((float) w2 * 0.33f)).reduced(4));
        chain->panel.setBounds(row2.reduced(4));
        for (auto* g : { ott, drive, dim, delay, reverb }) g->layout();

        auto c = chain->panel.content();
        orderCombo->setBounds(c.removeFromTop(48).reduced(4, 0));
        chainLabel.setBounds(c.removeFromTop(40));
    }
}
