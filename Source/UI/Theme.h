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
    // Stile "vetro": grigi-blu scuri, bordi sottili azzurri, luce arancione per cio' che e' attivo
    const juce::Colour bg           { 0xff0d1116 };
    const juce::Colour panel        { 0xff131a21 };
    const juce::Colour raised       { 0xff1c242d };
    const juce::Colour raisedHi     { 0xff27313b };
    const juce::Colour border       { 0xff0d0f11 };
    const juce::Colour edgeLight    { 0xff3c434a };   // filo chiaro in alto sui rilievi

    // Testo
    const juce::Colour text         { 0xffc9ced3 };
    const juce::Colour textBright   { 0xffeceff1 };
    const juce::Colour textDim      { 0xff737c85 };

    // Accenti
    const juce::Colour accent       { 0xfff0922b };   // arancio, solo per cio' che e' attivo/selezionato
    const juce::Colour cyan         { 0xff4fe3ff };   // azzurro "olografico": bordi, griglie, riproduzione
    const juce::Colour led          { 0xff7ed957 };   // verde LED (canale attivo, play)
    const juce::Colour lcdBg        { 0xff070a0d };
    const juce::Colour lcdText      { 0xfff0922b };

    // Step del sequencer
    const juce::Colour stepOffA     { 0xff18202a };   // gruppi di 4 step alternati, come in FL
    const juce::Colour stepOffB     { 0xff221c20 };
    const juce::Colour stepOn       { 0xffb9c3cc };   // grigio chiaro
    const juce::Colour stepOnHi     { 0xffe4e9ed };

    inline constexpr float corner = 6.0f;

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
        g.setGradientFill (juce::ColourGradient (base.brighter (0.07f), r.getTopLeft(), base.darker (0.18f), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (cyan.withAlpha (0.13f));                         // filo azzurro (vetro)
        g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (0.0f, radius - 1.0f), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine ((int) r.getY() + 2, r.getX() + radius, r.getRight() - radius);
    }

    /** Bordo arancione luminoso: cio' che e' acceso o selezionato. */
    inline void drawActiveGlow (juce::Graphics& g, juce::Rectangle<float> r, float radius = corner, juce::Colour colour = accent)
    {
        g.setColour (colour.withAlpha (0.12f));
        g.drawRoundedRectangle (r.expanded (1.0f), radius + 1.0f, 3.0f);
        g.setColour (colour.withAlpha (0.75f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.2f);
    }

    /** Pannello di vetro (channel rack, mixer, playlist): sfondo scuro e bordo azzurro tenue. */
    inline void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff121920), r.getTopLeft(), juce::Colour (0xff0c1116), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (cyan.withAlpha (0.16f));
        g.drawRoundedRectangle (r.reduced (1.5f), radius - 1.0f, 1.0f);
    }

    /** Superficie incassata (display, campi): piu' scura del pannello. */
    inline void drawSunken (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base = lcdBg, float radius = corner)
    {
        g.setColour (base);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (cyan.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (0.0f, radius - 1.0f), 1.0f);
    }
}
