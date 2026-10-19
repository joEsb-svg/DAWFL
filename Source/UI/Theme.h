#pragma once

#include <JuceHeader.h>

/**
    Colori e misure dell'interfaccia, tutti in un posto solo.
    Stile: grigi scuri e freddi, contrasti bassi, un solo colore d'accento (arancio)
    usato con parsimonia, angoli quasi squadrati. Come una DAW da studio.
*/
namespace Theme
{
    // Superfici (dalla piu' profonda alla piu' in rilievo)
    const juce::Colour bg           { 0xff16191c };
    const juce::Colour panel        { 0xff1f2327 };
    const juce::Colour raised       { 0xff2a2f34 };
    const juce::Colour raisedHi     { 0xff353b41 };
    const juce::Colour border       { 0xff0d0f11 };
    const juce::Colour edgeLight    { 0xff3c434a };   // filo chiaro in alto sui rilievi

    // Testo
    const juce::Colour text         { 0xffc9ced3 };
    const juce::Colour textBright   { 0xffeceff1 };
    const juce::Colour textDim      { 0xff737c85 };

    // Accenti
    const juce::Colour accent       { 0xfff0922b };   // arancio, solo per cio' che e' attivo/selezionato
    const juce::Colour led          { 0xff7ed957 };   // verde LED (canale attivo, play)
    const juce::Colour lcdBg        { 0xff0b0d0f };
    const juce::Colour lcdText      { 0xfff0922b };

    // Step del sequencer
    const juce::Colour stepOffA     { 0xff2b3036 };   // gruppi di 4 step alternati, come in FL
    const juce::Colour stepOffB     { 0xff342b2f };
    const juce::Colour stepOn       { 0xffb9c3cc };   // grigio chiaro
    const juce::Colour stepOnHi     { 0xffe4e9ed };

    inline constexpr float corner = 3.0f;

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    /** Etichetta piccola in maiuscolo e un po' spaziata, da pannello hardware. */
    inline void drawCaption (juce::Graphics& g, const juce::String& textToDraw, juce::Rectangle<float> area,
                             juce::Justification just = juce::Justification::centred)
    {
        auto f = font (10.5f, true).withExtraKerningFactor (0.08f);
        g.setFont (f);
        g.setColour (textDim);
        g.drawText (textToDraw.toUpperCase(), area, just, false);
    }

    /** Superficie in rilievo: gradiente leggerissimo, bordo scuro e filo chiaro in alto. */
    inline void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float radius = corner)
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.06f), r.getTopLeft(), base.darker (0.12f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (edgeLight.withAlpha (0.5f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    }

    /** Superficie incassata (display, campi): piu' scura del pannello. */
    inline void drawSunken (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base = lcdBg, float radius = corner)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (edgeLight.withAlpha (0.25f));
        g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + radius, r.getRight() - radius);
    }
}
