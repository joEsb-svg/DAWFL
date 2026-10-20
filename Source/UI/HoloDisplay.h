#pragma once

#include <JuceHeader.h>
#include <functional>
#include <map>
#include "Theme.h"
#include "../Engine/Performance.h"

//==============================================================================
/**
    Il display al centro della barra in alto (stile "vetro + HUD"):
    - vetro scuro con griglia leggera, bordo che sfuma dall'azzurro all'arancione, angoli a mirino
    - TEMPO (cifre arancioni luminose), PATTERN (numero, pallino col colore e nome), POSIZIONE
      (battuta.tempo.step con un anello a 16 tacche che si riempie durante la battuta)
    - dietro, lo spettro dell'audio in uscita, appena accennato
    Comandi come prima: frecce ◀ ▶, trascinamento verticale sul numero, tocco al centro.
*/
class HoloDisplay : public juce::Component
{
public:
    struct Position { int bar = 1, beat = 1, sixteenth = 1, stepInBar = -1; bool playing = false; };

    // Tempo
    std::function<juce::String()> tempoText;
    std::function<void (int)> onTempoStep;
    std::function<void (double)> onTempoDrag;
    std::function<void()> onTempoTap;
    // Pattern
    std::function<int()> patternNumber;          // 0..98
    std::function<juce::String()> patternName;
    std::function<juce::Colour()> patternColour;
    std::function<void (int)> onPatternStep;
    std::function<void()> onPatternTap;
    // Posizione e stato
    std::function<Position()> position;
    std::function<juce::String()> statusText;    // es. "REC Basso" (vuoto = niente)

    /** Spettro (0..1 per banda), aggiornato dal timer dell'interfaccia. */
    void setSpectrum (const float* bands, int n)
    {
        spectrum.assign (bands, bands + n);
        repaint (glassArea().toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = glassArea();
        const float radius = juce::jmin (12.0f, r.getHeight() * 0.2f);
        layoutCells();

        // Vetro
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0c141c), r.getTopLeft(), juce::Colour (0xff140f0b), r.getBottomRight(), false));
        g.fillRoundedRectangle (r, radius);

        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (r.reduced (1.0f), radius);
            g.reduceClipRegion (clip);

            // Griglia
            g.setColour (cyan.withAlpha (0.05f));
            for (float x = r.getX() + 6.0f; x < r.getRight(); x += 12.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            for (float y = r.getY() + 6.0f; y < r.getBottom(); y += 12.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());

            // Spettro
            if (! spectrum.empty())
            {
                const int n = (int) spectrum.size();
                const float bw = (r.getWidth() - 20.0f) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    const float h = juce::jlimit (0.0f, 1.0f, spectrum[(size_t) i]) * r.getHeight() * 0.75f;
                    if (h < 1.0f) continue;
                    auto bar = juce::Rectangle<float> (r.getX() + 10.0f + (float) i * bw + 1.0f, r.getBottom() - h, bw - 2.0f, h);
                    g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.20f), bar.getBottomLeft(), cyan.withAlpha (0.0f), bar.getTopLeft(), false));
                    g.fillRect (bar);
                }
            }
        }

        // Bordo che sfuma
        g.setGradientFill (juce::ColourGradient (cyan, r.getTopLeft(), orange, r.getTopRight(), false));
        g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.5f);

        // Angoli a mirino
        drawBracket (g, r, true, true, cyan);
        drawBracket (g, r, true, false, cyan);
        drawBracket (g, r, false, true, orange);
        drawBracket (g, r, false, false, orange);

        // Separatori
        for (auto x : { tempoCell.getRight(), patternCell.getRight() })
            if (x < r.getRight() - 4.0f)
            {
                juce::ColourGradient grad (cyan.withAlpha (0.0f), x, r.getY() + 8.0f, cyan.withAlpha (0.0f), x, r.getBottom() - 8.0f, false);
                grad.addColour (0.5, cyan.withAlpha (0.35f));
                g.setGradientFill (grad);
                g.fillRect (x - 0.5f, r.getY() + 8.0f, 1.0f, r.getHeight() - 16.0f);
            }

        paintTempo (g);
        paintPattern (g);
        if (! positionCell.isEmpty())
            paintPosition (g);
    }

    void resized() override     { glowCache.clear(); }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        downY = e.position.y;
        accumulated = 0.0;
        dragTarget = tempoCell.contains (e.position) ? 0 : patternCell.contains (e.position) ? 1 : -1;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragTarget != 0 || ! onTempoDrag)
            return;
        const double dy = downY - e.position.y;
        if (! dragged && std::abs (dy) > 6.0)
            dragged = true;
        if (dragged)
        {
            onTempoDrag ((dy - accumulated) / 4.0);
            accumulated = dy;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragged)
            return;
        const auto p = e.position;
        if (tempoCell.contains (p))
        {
            if (p.x < tempoCell.getX() + arrowZone)            { if (onTempoStep) onTempoStep (-1); }
            else if (p.x > tempoCell.getRight() - arrowZone)   { if (onTempoStep) onTempoStep (1); }
            else if (onTempoTap)                                onTempoTap();
        }
        else if (patternCell.contains (p))
        {
            if (p.x < patternCell.getX() + arrowZone)          { if (onPatternStep) onPatternStep (-1); }
            else if (p.x > patternCell.getRight() - arrowZone) { if (onPatternStep) onPatternStep (1); }
            else if (onPatternTap)                              onPatternTap();
        }
        repaint();
    }

    /** Per i menu che si aprono "sotto" il pattern. */
    juce::Rectangle<int> getPatternArea() const     { return patternCell.toNearestInt(); }

private:
    juce::Rectangle<float> glassArea() const        { return getLocalBounds().toFloat().reduced (1.0f); }

    void layoutCells()
    {
        auto r = glassArea().reduced (8.0f, 4.0f);
        const bool showPosition = r.getWidth() > 250.0f;
        arrowZone = juce::jlimit (20.0f, 34.0f, r.getWidth() * 0.06f);
        if (showPosition)
        {
            const float w = r.getWidth();
            const bool compact = w < 480.0f;    // telefono in orizzontale: il nome del pattern va via, resta il pallino
            tempoCell = r.removeFromLeft (w * (compact ? 0.33f : 0.31f));
            patternCell = r.removeFromLeft (w * (compact ? 0.33f : 0.37f));
            positionCell = r;
        }
        else
        {
            tempoCell = r.removeFromLeft (r.getWidth() * 0.45f);
            patternCell = r;
            positionCell = {};
        }
    }

    float captionHeight() const     { return juce::jlimit (9.0f, 12.0f, glassArea().getHeight() * 0.16f); }
    float digitHeight() const       { return juce::jlimit (18.0f, 44.0f, glassArea().getHeight() * 0.52f); }

    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just)
    {
        g.setColour (juce::Colour (0xff7fb8c4));
        g.setFont (Theme::font (captionHeight() * 0.85f, true).withExtraKerningFactor (0.22f));
        g.drawText (text, area, just, false);
    }

    void drawArrows (juce::Graphics& g, juce::Rectangle<float> cell)
    {
        const float s = juce::jmin (6.0f, cell.getHeight() * 0.1f);
        g.setColour (cyan.withAlpha (0.55f));
        const float y = cell.getCentreY() + captionHeight() * 0.3f;
        juce::Path p;
        const float lx = cell.getX() + arrowZone * 0.45f, rx = cell.getRight() - arrowZone * 0.45f;
        p.addTriangle (lx + s * 0.6f, y - s, lx + s * 0.6f, y + s, lx - s * 0.7f, y);
        p.addTriangle (rx - s * 0.6f, y - s, rx - s * 0.6f, y + s, rx + s * 0.7f, y);
        g.fillPath (p);
    }

    /** Testo luminoso: l'alone e' calcolato una volta e tenuto da parte (ridisegnare il blur costa). */
    void drawGlowText (juce::Graphics& g, const juce::String& text, float height, juce::Colour glow,
                       juce::Point<float> centre, bool light = true)
    {
        const auto key = text + "|" + juce::String (height) + "|" + glow.toString();
        auto it = glowCache.find (key);
        if (it == glowCache.end())
        {
            if (glowCache.size() > 40) glowCache.clear();
            auto font = juce::Font (juce::FontOptions (height)).withTypefaceStyle (light ? "Light" : "Regular");
            juce::GlyphArrangement ga;
            ga.addLineOfText (font, text, 0.0f, 0.0f);
            juce::Path path;
            ga.createPath (path);
            const auto bounds = path.getBounds();
            const int pad = 14;
            const float scale = 2.0f;   // nitido sugli schermi ad alta densita'
            juce::Image img (juce::Image::ARGB, (int) ((bounds.getWidth() + pad * 2) * scale) + 1,
                             (int) ((height * 1.3f + pad * 2) * scale) + 1, true);
            {
                juce::Graphics ig (img);
                ig.addTransform (juce::AffineTransform::scale (scale));
                path.applyTransform (juce::AffineTransform::translation (pad - bounds.getX(), pad + height * 0.95f));
                juce::DropShadow (glow, 12, {}).drawForPath (ig, path);
                juce::DropShadow (glow.withAlpha (0.8f), 5, {}).drawForPath (ig, path);
                ig.setColour (juce::Colours::white.interpolatedWith (glow, 0.08f));
                ig.fillPath (path);
            }
            it = glowCache.emplace (key, img).first;
        }
        const auto& img = it->second;
        const float w = (float) img.getWidth() / 2.0f, h = (float) img.getHeight() / 2.0f;
        g.drawImage (img, juce::Rectangle<float> (w, h).withCentre (centre).withY (centre.y - h * 0.5f - height * 0.08f));
    }

    void paintTempo (juce::Graphics& g)
    {
        auto cell = tempoCell;
        drawArrows (g, cell);
        auto inner = cell.reduced (arrowZone, 0.0f);
        drawCaption (g, "TEMPO", inner.removeFromTop (captionHeight() + 6.0f).withTrimmedTop (4.0f), juce::Justification::centred);
        const auto text = tempoText ? tempoText() : juce::String ("120");
        const float dh = digitHeight();
        auto c = inner.getCentre();
        // "BPM" piccolo accanto alle cifre
        g.setColour (juce::Colour (0x99ffc58a));
        g.setFont (Theme::font (juce::jmax (8.0f, dh * 0.24f), true).withExtraKerningFactor (0.15f));
        const float digitsW = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (dh)), text);
        const bool roomForUnit = inner.getWidth() > digitsW + dh * 1.2f;
        if (roomForUnit)
        {
            c.x -= dh * 0.32f;
            g.drawText ("BPM", juce::Rectangle<float> (c.x + digitsW * 0.5f + 4.0f, c.y, dh, dh * 0.4f), juce::Justification::centredLeft, false);
        }
        drawGlowText (g, text, dh, orange, c);
    }

    void paintPattern (juce::Graphics& g)
    {
        auto cell = patternCell;
        drawArrows (g, cell);
        auto inner = cell.reduced (arrowZone, 0.0f);
        drawCaption (g, "PATTERN", inner.removeFromTop (captionHeight() + 6.0f).withTrimmedTop (4.0f), juce::Justification::centred);

        const int number = patternNumber ? patternNumber() : 0;
        const auto numText = juce::String (number + 1).paddedLeft ('0', 2);
        const float dh = digitHeight();
        const auto name = patternName ? patternName() : juce::String();
        const auto colour = patternColour ? patternColour() : cyan;

        // Il nome (con il pallino colorato) solo se c'e' posto, a destra del numero
        const float numW = dh * 1.25f;
        const float chipW = juce::jmin (inner.getWidth() - numW - 6.0f, juce::GlyphArrangement::getStringWidth (Theme::font (12.0f), name) + 30.0f);
        if (chipW > 54.0f && name.isNotEmpty())
        {
            const float total = numW + 6.0f + chipW;
            auto row = inner.withSizeKeepingCentre (total, inner.getHeight());
            drawGlowText (g, numText, dh, cyan, row.removeFromLeft (numW).getCentre());
            row.removeFromLeft (6.0f);
            auto chip = row.withSizeKeepingCentre (row.getWidth(), juce::jmin (22.0f, row.getHeight() * 0.5f));
            g.setColour (colour.withAlpha (0.14f));
            g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
            g.setColour (colour.withAlpha (0.6f));
            g.drawRoundedRectangle (chip.reduced (0.5f), chip.getHeight() * 0.5f, 1.0f);
            auto dot = chip.removeFromLeft (chip.getHeight()).withSizeKeepingCentre (7.0f, 7.0f);
            g.setColour (colour.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (2.5f));
            g.setColour (colour);
            g.fillEllipse (dot);
            g.setColour (Theme::textBright);
            g.setFont (Theme::font (juce::jmin (12.5f, chip.getHeight() * 0.62f)));
            g.drawFittedText (name, chip.withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        }
        else
        {
            drawGlowText (g, numText, dh, cyan, inner.getCentre());
            // Solo il pallino col colore del pattern, sotto il numero
            auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ inner.getCentreX(), inner.getBottom() - 5.0f });
            g.setColour (colour.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (2.5f));
            g.setColour (colour);
            g.fillEllipse (dot);
        }
    }

    void paintPosition (juce::Graphics& g)
    {
        auto cell = positionCell.reduced (4.0f, 0.0f);
        const auto pos = position ? position() : Position();
        const auto status = statusText ? statusText() : juce::String();

        // Anello a 16 tacche
        const float ringD = juce::jmin (cell.getHeight() - 8.0f, 56.0f, cell.getWidth() * 0.36f);
        auto ring = cell.removeFromLeft (ringD).withSizeKeepingCentre (ringD, ringD);
        const auto c = ring.getCentre();
        const float outer = ringD * 0.5f, len = ringD * 0.14f, w = juce::jmax (2.0f, ringD * 0.05f);
        for (int k = 0; k < 16; ++k)
        {
            const bool lit = pos.playing && k <= pos.stepInBar;
            const bool beat = k % 4 == 0;
            juce::Path tick;
            tick.addRoundedRectangle (-w * 0.5f, -outer, w, beat ? len * 1.25f : len, w * 0.5f);
            tick.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * (float) k / 16.0f).translated (c));
            g.setColour (lit ? cyan : juce::Colour (0xff1b3a44));
            g.fillPath (tick);
        }
        g.setColour (orange.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (ringD * 0.46f, ringD * 0.46f).withCentre (c), 1.0f);
        g.setColour (Theme::textBright);
        g.setFont (juce::Font (juce::FontOptions (ringD * 0.26f)).withTypefaceStyle ("Light"));
        g.drawText (pos.playing ? juce::String (pos.beat) : juce::String ("-"), juce::Rectangle<float> (ringD, ringD).withCentre (c), juce::Justification::centred, false);

        cell.removeFromLeft (8.0f);
        auto top = cell.removeFromTop (captionHeight() + 6.0f).withTrimmedTop (4.0f);
        if (status.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffe0483e));
            g.setFont (Theme::font (captionHeight() * 0.9f, true));
            g.drawText (status, top, juce::Justification::centred, true);
        }
        else
            drawCaption (g, top.getWidth() < 90.0f ? "POS" : "POSIZIONE", top, juce::Justification::centred);

        const auto text = pos.playing ? juce::String (pos.bar) + "." + juce::String (pos.beat) + "." + juce::String (pos.sixteenth)
                                      : juce::String ("1.1.1");
        drawGlowText (g, text, digitHeight() * 0.86f, cyan, cell.getCentre());
    }

    static void drawBracket (juce::Graphics& g, juce::Rectangle<float> r, bool left, bool top, juce::Colour colour)
    {
        const float s = juce::jmin (12.0f, r.getHeight() * 0.18f), inset = 4.0f;
        const float x = left ? r.getX() + inset : r.getRight() - inset;
        const float y = top ? r.getY() + inset : r.getBottom() - inset;
        juce::Path p;
        p.startNewSubPath (x, y + (top ? s : -s));
        p.lineTo (x, y);
        p.lineTo (x + (left ? s : -s), y);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    const juce::Colour cyan { 0xff4fe3ff }, orange { 0xfff0922b };
    std::vector<float> spectrum;
    std::map<juce::String, juce::Image> glowCache;
    juce::Rectangle<float> tempoCell, patternCell, positionCell;
    float arrowZone = 28.0f;
    bool dragged = false;
    float downY = 0.0f;
    double accumulated = 0.0;
    int dragTarget = -1;
};

//==============================================================================
/** Calcola lo spettro (bande logaritmiche, 0..1, con discesa morbida) dagli ultimi campioni in uscita. */
class SpectrumAnalyser
{
public:
    static constexpr int order = 9, size = 1 << order, numBands = 28;

    SpectrumAnalyser()
    {
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (size - 1));
    }

    /** samples: 'size' campioni; restituisce le bande aggiornate. */
    const std::array<float, numBands>& process (const float* samples, double sampleRate)
    {
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = samples[i] * window[(size_t) i];
        std::fill (data.begin() + size, data.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (data.data());

        const double nyquist = sampleRate * 0.5;
        for (int b = 0; b < numBands; ++b)
        {
            const double f0 = 40.0 * std::pow (16000.0 / 40.0, (double) b / numBands);
            const double f1 = 40.0 * std::pow (16000.0 / 40.0, (double) (b + 1) / numBands);
            const int i0 = juce::jlimit (1, size / 2 - 1, (int) (f0 / nyquist * size / 2));
            const int i1 = juce::jlimit (i0 + 1, size / 2, (int) (f1 / nyquist * size / 2) + 1);
            float m = 0.0f;
            for (int i = i0; i < i1; ++i)
                m = juce::jmax (m, data[(size_t) i]);
            const float db = juce::Decibels::gainToDecibels (m / (float) size * 4.0f, -80.0f);
            const float v = juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 60.0f);
            bands[(size_t) b] = v > bands[(size_t) b] ? v : bands[(size_t) b] * 0.86f;   // sale subito, scende piano
        }
        return bands;
    }

    void decay()
    {
        for (auto& b : bands) b *= 0.86f;
    }

private:
    juce::dsp::FFT fft { order };
    std::array<float, size> window {};
    std::array<float, size * 2> data {};
    std::array<float, numBands> bands {};
};
