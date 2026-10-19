#pragma once
#include "Palette.h"

namespace bf::ui
{
    /** Look & feel scuro/moderno: manopole ad arco con indicatore, LED per i toggle, slider bipolari, combo piatte. */
    class CustomLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        CustomLookAndFeel();

        /** Geometria condivisa tra la manopola e il suo anello di modulazione (che sta appena fuori dall'arco). */
        struct KnobGeometry { juce::Point<float> centre; float radius; };
        static KnobGeometry knobGeometry(juce::Rectangle<float> bounds) noexcept;

        void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                              float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                              juce::Slider::SliderStyle, juce::Slider&) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getLabelFont(juce::Label&) override;
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
        juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    };
}
