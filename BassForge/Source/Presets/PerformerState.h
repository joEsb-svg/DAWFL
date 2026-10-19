#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "Modulation/VoiceModulation.h"

namespace bf
{
    // --- Persistenza dei pattern del Performer (message thread) ---------------------------------
    inline juce::ValueTree performersToTree(const bf::PerformerBank& bank)
    {
        juce::ValueTree t("PERFORMERS");
        for (const auto& p : bank.p)
        {
            juce::StringArray v, c, m;
            for (int s = 0; s < bf::PerformerPattern::kMaxSteps; ++s)
            {
                v.add(juce::String(p.getValue(s), 4));
                c.add(juce::String(p.getCurve(s), 4));
                m.add(juce::String((int) p.getInterp(s)));
            }
            juce::ValueTree n("PATTERN");
            n.setProperty("len", p.getLength(), nullptr);
            n.setProperty("values", v.joinIntoString(" "), nullptr);
            n.setProperty("curves", c.joinIntoString(" "), nullptr);
            n.setProperty("interps", m.joinIntoString(" "), nullptr);
            t.appendChild(n, nullptr);
        }
        return t;
    }

    inline void performersFromTree(bf::PerformerBank& bank, const juce::ValueTree& t)
    {
        for (int i = 0; i < juce::jmin(t.getNumChildren(), bf::kNumLfo); ++i)
        {
            const auto n = t.getChild(i);
            auto& p = bank.p[(size_t) i];
            const auto v = juce::StringArray::fromTokens(n["values"].toString(), " ", "");
            const auto c = juce::StringArray::fromTokens(n["curves"].toString(), " ", "");
            const auto m = juce::StringArray::fromTokens(n["interps"].toString(), " ", "");
            p.setLength((int) n.getProperty("len", 16));
            for (int s = 0; s < juce::jmin(bf::PerformerPattern::kMaxSteps, v.size()); ++s)
                p.setStep(s, v[s].getFloatValue(),
                          (bf::StepInterp) juce::jlimit(0, (int) bf::StepInterp::Count - 1, s < m.size() ? m[s].getIntValue() : (int) bf::StepInterp::Step),
                          s < c.size() ? c[s].getFloatValue() : 0.f);
        }
    }
}
