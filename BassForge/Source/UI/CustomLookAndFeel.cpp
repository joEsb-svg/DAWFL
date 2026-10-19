#include "CustomLookAndFeel.h"

namespace bf::ui
{
    using juce::MathConstants;

    CustomLookAndFeel::CustomLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, col::bg);
        setColour(juce::Label::textColourId, col::text);
        setColour(juce::Slider::textBoxTextColourId, col::text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, col::cyanDim);
        setColour(juce::ComboBox::backgroundColourId, col::panelHi);
        setColour(juce::ComboBox::textColourId, col::text);
        setColour(juce::ComboBox::outlineColourId, col::edge);
        setColour(juce::ComboBox::arrowColourId, col::cyan);
        setColour(juce::PopupMenu::backgroundColourId, col::panel);
        setColour(juce::PopupMenu::textColourId, col::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::cyanDim);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::TextButton::textColourOffId, col::textDim);
        setColour(juce::TextButton::textColourOnId, col::cyan);
    }

    CustomLookAndFeel::KnobGeometry CustomLookAndFeel::knobGeometry(juce::Rectangle<float> b) noexcept
    {
        const float r = juce::jmax(6.0f, juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 6.0f);   // 6 px liberi per l'anello
        return { b.getCentre(), r };
    }

    juce::Slider::SliderLayout CustomLookAndFeel::getSliderLayout(juce::Slider& s)
    {
        juce::Slider::SliderLayout l;
        auto b = s.getLocalBounds();
        if (s.getTextBoxPosition() == juce::Slider::NoTextBox)
        {
            l.sliderBounds = b;
            return l;
        }
        if (s.getTextBoxPosition() == juce::Slider::TextBoxRight)
        {
            l.textBoxBounds = b.removeFromRight(s.getTextBoxWidth());
            l.sliderBounds = b.withTrimmedRight(6);
            return l;
        }
        const int th = s.getTextBoxHeight();
        l.textBoxBounds = b.removeFromBottom(th);
        l.sliderBounds = b;
        return l;
    }

    void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                             float a0, float a1, juce::Slider& s)
    {
        const auto geo = knobGeometry(juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h));
        const auto c = geo.centre; const float r = geo.radius;
        const float thick = juce::jmax(2.5f, r * 0.14f);
        const float arcR = r - thick * 0.5f;
        const bool enabled = s.isEnabled();
        const float ang = a0 + pos * (a1 - a0);

        // corpo
        g.setGradientFill(juce::ColourGradient(col::panelHi.brighter(0.08f), c.x, c.y - r, col::panel.darker(0.3f), c.x, c.y + r, false));
        g.fillEllipse(c.x - r + thick + 2, c.y - r + thick + 2, 2 * (r - thick - 2), 2 * (r - thick - 2));

        // binario
        juce::Path track; track.addCentredArc(c.x, c.y, arcR, arcR, 0, a0, a1, true);
        g.setColour(col::track);
        g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // valore (da 0 se bipolare)
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zeroPos = bipolar ? (float) s.valueToProportionOfLength(0.0) : 0.0f;
        const float from = a0 + zeroPos * (a1 - a0);
        juce::Path val; val.addCentredArc(c.x, c.y, arcR, arcR, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
        const auto vc = enabled ? col::cyan : col::cyanDim;
        g.setColour(vc.withAlpha(0.18f));
        g.strokePath(val, juce::PathStrokeType(thick + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));   // glow
        g.setColour(vc);
        g.strokePath(val, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // indicatore
        const float ir0 = r * 0.30f, ir1 = r - thick - 3.0f;
        juce::Line<float> pointer(c.x + std::sin(ang) * ir0, c.y - std::cos(ang) * ir0,
                                  c.x + std::sin(ang) * ir1, c.y - std::cos(ang) * ir1);
        g.setColour(col::text);
        g.drawLine(pointer, 2.0f);
    }

    void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                             juce::Slider::SliderStyle style, juce::Slider& s)
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s); return; }

        const float cy = (float) y + (float) h * 0.5f, th = 4.0f;
        g.setColour(col::track);
        g.fillRoundedRectangle((float) x, cy - th * 0.5f, (float) w, th, 2.0f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float z = bipolar ? (float) x + (float) s.valueToProportionOfLength(0.0) * (float) w : (float) x;
        const bool neg = pos < z;
        const auto c = neg ? col::orange : col::cyan;
        g.setColour(c);
        g.fillRoundedRectangle(juce::jmin(z, pos), cy - th * 0.5f, std::abs(pos - z), th, 2.0f);
        if (bipolar) { g.setColour(col::textDim); g.fillRect(z - 0.5f, cy - 5.0f, 1.0f, 10.0f); }
        g.setColour(juce::Colours::white);
        g.fillEllipse(pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        g.setColour(c);
        g.drawEllipse(pos - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.5f);
    }

    void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        auto r = b.getLocalBounds().toFloat().reduced(2.0f);
        const bool on = b.getToggleState();
        const float d = juce::jmin(14.0f, r.getHeight());
        auto led = juce::Rectangle<float>(r.getX() + 2.0f, r.getCentreY() - d * 0.5f, d, d);
        if (on) { g.setColour(col::cyan.withAlpha(0.25f)); g.fillEllipse(led.expanded(3.0f)); }
        g.setColour(on ? col::cyan : col::track);
        g.fillEllipse(led);
        g.setColour(highlighted ? col::text : col::edge);
        g.drawEllipse(led, 1.0f);
        g.setColour(on ? col::text : col::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(b.getButtonText(), r.withTrimmedLeft(d + 8.0f), juce::Justification::centredLeft, true);
    }

    void CustomLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool)
    {
        auto r = b.getLocalBounds().toFloat();
        g.setColour(hl ? col::panelHi : juce::Colours::transparentBlack);
        g.fillRoundedRectangle(r.reduced(1.0f), 6.0f);
        if (! b.getClickingTogglesState())          // pulsanti d'azione (Save, Init, Clear...): bordo sottile
        {
            g.setColour(hl ? col::cyan : col::edge);
            g.drawRoundedRectangle(r.reduced(1.5f), 6.0f, 1.0f);
        }
        if (b.getToggleState())
        {
            g.setColour(col::cyan);
            g.fillRoundedRectangle(r.getX() + 10.0f, r.getBottom() - 4.0f, r.getWidth() - 20.0f, 3.0f, 1.5f);
        }
    }

    void CustomLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        const bool tab = b.getClickingTogglesState();
        g.setColour(b.getToggleState() ? col::cyan : (tab ? col::textDim : col::text));
        g.setFont(juce::Font(juce::FontOptions(tab ? 15.0f : 12.5f, tab ? juce::Font::bold : juce::Font::plain)));
        g.drawText(b.getButtonText(), b.getLocalBounds().withTrimmedBottom(3), juce::Justification::centred);
    }

    void CustomLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& cb)
    {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
        g.setColour(down ? col::panelHi.brighter(0.1f) : col::panelHi);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(cb.hasKeyboardFocus(false) ? col::cyan : col::edge);
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour(col::cyan);
        g.fillPath(arrow);
    }

    void CustomLookAndFeel::positionComboBoxText(juce::ComboBox& cb, juce::Label& l)
    {
        l.setBounds(6, 1, cb.getWidth() - 22, cb.getHeight() - 2);
        l.setFont(getComboBoxFont(cb));
    }

    juce::Font CustomLookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::Font(juce::FontOptions(12.5f)); }
    juce::Font CustomLookAndFeel::getLabelFont(juce::Label&)       { return juce::Font(juce::FontOptions(12.0f)); }
    juce::Font CustomLookAndFeel::getPopupMenuFont()               { return juce::Font(juce::FontOptions(13.5f)); }

    void CustomLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
    {
        g.fillAll(col::panel);
        g.setColour(col::edge);
        g.drawRect(0, 0, w, h, 1);
    }
}
