#pragma once
#include "ModRing.h"

namespace bf::ui
{
    /** Manopola + titolo + valore, collegata a un parametro APVTS (SliderAttachment). Opzionale: anello di modulazione. */
    class ParamKnob : public juce::Component
    {
    public:
        ParamKnob(Apvts& apvts, const juce::String& paramId, const juce::String& title,
                  ModRingModel* ring = nullptr, int dest = DstNone, ModMapper mapper = {})
        {
            name.setText(title, juce::dontSendNotification);
            name.setJustificationType(juce::Justification::centred);
            name.setColour(juce::Label::textColourId, col::textDim);
            name.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(name);

            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 14);
            slider.setPopupDisplayEnabled(false, false, nullptr);
            slider.setTooltip(title);
            if (ring != nullptr && dest > DstNone) slider.setModulation(ring, dest, mapper ? std::move(mapper) : mapper::linear());
            addAndMakeVisible(slider);
            attachment = std::make_unique<Apvts::SliderAttachment>(apvts, paramId, slider);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            name.setBounds(b.removeFromTop(14));
            slider.setBounds(b);
        }

        ModSlider slider;

    private:
        juce::Label name;
        std::unique_ptr<Apvts::SliderAttachment> attachment;
    };

    /** Interruttore a LED collegato a un parametro bool (ButtonAttachment). */
    class ParamToggle : public juce::Component
    {
    public:
        ParamToggle(Apvts& apvts, const juce::String& paramId, const juce::String& title)
        {
            button.setButtonText(title);
            addAndMakeVisible(button);
            attachment = std::make_unique<Apvts::ButtonAttachment>(apvts, paramId, button);
        }
        void resized() override { button.setBounds(getLocalBounds().withSizeKeepingCentre(getWidth() - 4, juce::jmin(26, getHeight()))); }

        juce::ToggleButton button;

    private:
        std::unique_ptr<Apvts::ButtonAttachment> attachment;
    };

    /** ComboBox collegata a un parametro Choice (ComboBoxAttachment); le voci vengono lette dal parametro. */
    class ParamCombo : public juce::Component
    {
    public:
        ParamCombo(Apvts& apvts, const juce::String& paramId, const juce::String& title)
        {
            name.setText(title, juce::dontSendNotification);
            name.setJustificationType(juce::Justification::centredLeft);
            name.setColour(juce::Label::textColourId, col::textDim);
            name.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(name);
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
                combo.addItemList(p->choices, 1);
            addAndMakeVisible(combo);
            attachment = std::make_unique<Apvts::ComboBoxAttachment>(apvts, paramId, combo);
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced(2, 0);
            if (name.getText().isEmpty()) { combo.setBounds(b.withSizeKeepingCentre(b.getWidth(), juce::jmin(26, b.getHeight()))); return; }
            name.setBounds(b.removeFromTop(14));
            combo.setBounds(b.removeFromTop(24));
        }

        juce::ComboBox combo;

    private:
        juce::Label name;
        std::unique_ptr<Apvts::ComboBoxAttachment> attachment;
    };

    /** Slider orizzontale (barra bipolare) per le ampiezze della matrice. */
    class ParamBar : public juce::Component
    {
    public:
        ParamBar(Apvts& apvts, const juce::String& paramId)
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
            addAndMakeVisible(slider);
            attachment = std::make_unique<Apvts::SliderAttachment>(apvts, paramId, slider);
        }
        void resized() override { slider.setBounds(getLocalBounds()); }

        juce::Slider slider;

    private:
        std::unique_ptr<Apvts::SliderAttachment> attachment;
    };

    /** Pannello con titolo: sfondo arrotondato e un'area contenuto per i controlli. */
    class Panel : public juce::Component
    {
    public:
        explicit Panel(juce::String t) : title(std::move(t)) {}
        void setTitle(const juce::String& t) { title = t; repaint(); }
        juce::Rectangle<int> content() const { return getLocalBounds().reduced(8).withTrimmedTop(22); }

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(col::panel);
            g.fillRoundedRectangle(r, 10.0f);
            g.setColour(col::edge);
            g.drawRoundedRectangle(r, 10.0f, 1.0f);
            g.setColour(col::cyan);
            g.fillRoundedRectangle(r.getX() + 12.0f, r.getY() + 10.0f, 3.0f, 12.0f, 1.5f);
            g.setColour(col::text);
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText(title.toUpperCase(), (int) r.getX() + 20, (int) r.getY() + 6, getWidth() - 40, 20, juce::Justification::centredLeft);
        }

    private:
        juce::String title;
    };

    /** Dispone i componenti in una griglia `cols` colonne (righe = ceil(n/cols)), celle uguali. */
    inline void layoutGrid(juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int cols)
    {
        if (items.empty() || cols <= 0) return;
        const int n = (int) items.size(), rows = (n + cols - 1) / cols;
        const float cw = (float) area.getWidth() / (float) cols, ch = (float) area.getHeight() / (float) rows;
        for (int i = 0; i < n; ++i)
        {
            const int cx = i % cols, cy = i / cols;
            items[(size_t) i]->setBounds(juce::Rectangle<float>(area.getX() + cx * cw, area.getY() + cy * ch, cw, ch).toNearestInt().reduced(1));
        }
    }
}
