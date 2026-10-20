#pragma once

#include <JuceHeader.h>
#include <functional>
#include "Theme.h"

//==============================================================================
/**
    Guida rapida: poche schede che mostrano dove sta tutto. Si apre da sola al primo avvio
    (dopo la presentazione) e poi da MENU > Guida rapida. L'ultima scheda propone le demo.
    Si sfoglia con i pulsanti o trascinando il dito a destra/sinistra.
*/
class QuickGuide : public juce::Component
{
public:
    explicit QuickGuide (juce::StringArray demoNames)
    {
        cards =
        {
            { "Benvenuto in JLoop", "Uno studio musicale in tasca, pensato come FL Studio. In pochi passi vedi dove sta tutto: "
                                    "puoi chiudere questa guida quando vuoi e riaprirla da MENU > Guida rapida. "
                                    "Tieni premuta la freccia dell'undo per la cronologia delle modifiche." },
            { "Il channel rack", "Ogni riga e' un suono. Tocca le caselle per accendere i colpi e premi Play. "
                                 "Tieni premuta una casella per riempire la riga con un ritmo pronto (4/4, ottavi, trap...)." },
            { "Suoni e strumenti", "Tocca il nome di un canale per cambiare suono o aprire lo strumento: BassForge, PolyForge, "
                                   "FMForge e la drum machine DrumForge, ognuno con tanti preset. Il + aggiunge un canale." },
            { "Il piano roll", "Per melodie e accordi. Scegli una scala (le note giuste si illuminano), disegna accordi con un tocco "
                               "o usa una progressione pronta. Nel pannello dello strumento, Arp accende l'arpeggiatore." },
            { "Pattern e playlist", "Fino a 99 pattern per progetto. Nella playlist li metti in fila per costruire la canzone; "
                                    "le automazioni muovono volume, filtri ed effetti nel tempo. Con LIVE li lanci al volo, come con un controller. "
                                    "Il tasto rosso registra dalla tastiera MIDI." },
            { "Mixer ed esportazione", "Nel Mixer ogni canale ha la sua traccia con effetti, EQ e sidechain. Da MENU > Esporta "
                                       "il mastering automatico rende il brano pronto da condividere, e puoi ritoccarlo prima." },
            { "Prova una demo", "Il modo piu' veloce per capire: apri una canzone di esempio, premi Play e smonta tutto. "
                                "Oppure parti da zero." },
        };

        for (auto* b : { &backButton, &nextButton, &skipButton })
            addAndMakeVisible (b);
        backButton.onClick = [this] { show (page - 1); };
        nextButton.onClick = [this] { if (page == (int) cards.size() - 1) finish (-1); else show (page + 1); };
        skipButton.onClick = [this] { finish (-1); };

        for (int i = 0; i < demoNames.size(); ++i)
        {
            auto* b = demoButtons.add (new juce::TextButton (demoNames[i]));
            b->onClick = [this, i] { finish (i); };
            addChildComponent (b);
        }
        show (0);
    }

    /** Chiusa: -1 = senza demo, altrimenti l'indice della demo scelta. */
    std::function<void (int demo)> onFinished;

    void show (int newPage)
    {
        page = juce::jlimit (0, (int) cards.size() - 1, newPage);
        const bool last = page == (int) cards.size() - 1;
        backButton.setEnabled (page > 0);
        nextButton.setButtonText (last ? "Parti da zero" : "Avanti");
        skipButton.setVisible (! last);
        for (auto* b : demoButtons)
            b->setVisible (last);
        resized();
        repaint();
    }

    int getPage() const     { return page; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.7f));
        Theme::drawRaised (g, card.toFloat(), Theme::panel, 8.0f);

        g.setColour (Theme::accent);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (juce::String (page + 1) + " / " + juce::String ((int) cards.size()), counterArea, juce::Justification::centredLeft);

        g.setColour (Theme::textBright);
        g.setFont (Theme::font (titleArea.getHeight() > 30 ? 24.0f : 19.0f, true));
        g.drawFittedText (cards[(size_t) page].title, titleArea, juce::Justification::centredLeft, 1);

        g.setColour (Theme::text);
        g.setFont (Theme::font (15.0f));
        g.drawFittedText (cards[(size_t) page].body, bodyArea, juce::Justification::topLeft, 8, 1.0f);

        drawPicture (g, pictureArea.toFloat());

        // Pallini delle pagine
        const float d = 7.0f, gap = 7.0f;
        const float total = (float) cards.size() * d + (float) (cards.size() - 1) * gap;
        float x = (float) dotsArea.getCentreX() - total * 0.5f;
        for (int i = 0; i < (int) cards.size(); ++i, x += d + gap)
        {
            g.setColour (i == page ? Theme::accent : Theme::raisedHi);
            g.fillEllipse (x, (float) dotsArea.getCentreY() - d * 0.5f, d, d);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = juce::jmin (640, r.getWidth() - 24);
        const int h = juce::jmin (440, r.getHeight() - 24);
        card = juce::Rectangle<int> (w, h).withCentre (r.getCentre());

        auto c = card.reduced (18, 14);
        auto bottom = c.removeFromBottom (40);
        skipButton.setBounds (bottom.removeFromLeft (90).reduced (0, 3));
        nextButton.setBounds (bottom.removeFromRight (130).reduced (0, 3));
        bottom.removeFromRight (8);
        backButton.setBounds (bottom.removeFromRight (90).reduced (0, 3));
        dotsArea = c.removeFromBottom (18);     // i pallini sopra ai pulsanti (sul telefono non c'e' spazio di fianco)
        c.removeFromBottom (4);

        counterArea = c.removeFromTop (16);
        titleArea = c.removeFromTop (h > 380 ? 36 : 28);
        c.removeFromTop (6);

        const bool wide = c.getWidth() > 460;
        const bool last = page == (int) cards.size() - 1;
        if (last)
        {
            bodyArea = c.removeFromTop (60);
            c.removeFromTop (6);
            const int cols = wide ? 3 : 2;
            const int rows = (demoButtons.size() + cols - 1) / cols;
            const int bh = juce::jmin (48, c.getHeight() / juce::jmax (1, rows));
            for (int i = 0; i < demoButtons.size(); ++i)
                demoButtons[i]->setBounds (juce::Rectangle<int> (c.getX() + (i % cols) * c.getWidth() / cols, c.getY() + (i / cols) * bh,
                                                                 c.getWidth() / cols, bh).reduced (4));
            pictureArea = {};
        }
        else if (wide)
        {
            pictureArea = c.removeFromRight (c.getWidth() * 2 / 5).reduced (6);
            c.removeFromRight (10);
            bodyArea = c;
        }
        else
        {
            pictureArea = c.removeFromBottom (c.getHeight() / 2).reduced (10, 4);
            bodyArea = c;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override    { dragStartX = e.x; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int dx = e.x - dragStartX;
        if (std::abs (dx) > 60)
            show (page + (dx < 0 ? 1 : -1));
    }

private:
    struct Card { juce::String title, body; };

    void finish (int demo)
    {
        if (onFinished) onFinished (demo);
    }

    /** Un disegnino per ogni scheda, fatto di forme semplici (niente immagini da caricare). */
    void drawPicture (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (r.isEmpty())
            return;
        g.setColour (Theme::bg);
        g.fillRoundedRectangle (r, 6.0f);
        auto a = r.reduced (12.0f);

        switch (page)
        {
            case 0:
            {
                g.setColour (Theme::accent);
                g.setFont (Theme::font (juce::jmin (a.getHeight() * 0.35f, 46.0f), true));
                g.drawText ("JLoop", a, juce::Justification::centred);
                break;
            }
            case 1:
            {
                static const char* rows[] = { "x...x...x...x...", "....x.......x...", "..x...x...x...x.", "x.x.x.x.x.x.x.x." };
                const float rh = a.getHeight() / 4.0f, sw = a.getWidth() / 16.0f;
                for (int rr = 0; rr < 4; ++rr)
                    for (int s = 0; s < 16; ++s)
                    {
                        const bool on = rows[rr][s] == 'x';
                        g.setColour (on ? Theme::stepOn : ((s / 4) % 2 == 0 ? Theme::stepOffA : Theme::stepOffB));
                        g.fillRoundedRectangle (a.getX() + (float) s * sw + 1.0f, a.getY() + (float) rr * rh + 2.0f, sw - 2.0f, rh - 4.0f, 2.0f);
                    }
                break;
            }
            case 2:
            {
                const int keys = 10;
                const float kw = a.getWidth() / (float) keys;
                for (int k = 0; k < keys; ++k)
                {
                    g.setColour (k == 4 ? Theme::accent : Theme::text);
                    g.fillRect (a.getX() + (float) k * kw + 1.0f, a.getY() + a.getHeight() * 0.4f, kw - 2.0f, a.getHeight() * 0.6f);
                }
                static const bool black[] = { true, true, false, true, true, true, false, true, true };
                for (int k = 0; k < keys - 1; ++k)
                    if (black[k])
                    {
                        g.setColour (Theme::border);
                        g.fillRect (a.getX() + (float) (k + 1) * kw - kw * 0.3f, a.getY() + a.getHeight() * 0.4f, kw * 0.6f, a.getHeight() * 0.36f);
                    }
                g.setColour (Theme::raisedHi);
                for (int i = 0; i < 4; ++i)
                    g.fillEllipse (a.getX() + (float) i * a.getWidth() / 4.0f + 6.0f, a.getY(), a.getHeight() * 0.28f, a.getHeight() * 0.28f);
                break;
            }
            case 3:
            {
                const float rh = a.getHeight() / 8.0f;
                for (int rr = 0; rr < 8; ++rr)
                {
                    g.setColour (rr % 3 == 1 ? Theme::raised : Theme::stepOffA);
                    g.fillRect (a.getX(), a.getY() + (float) rr * rh, a.getWidth(), rh - 1.0f);
                }
                struct N { float x, w; int row; };
                static const N notes[] = { { 0.0f, 0.25f, 5 }, { 0.0f, 0.25f, 3 }, { 0.0f, 0.25f, 1 }, { 0.3f, 0.2f, 6 }, { 0.3f, 0.2f, 4 },
                                           { 0.3f, 0.2f, 2 }, { 0.55f, 0.12f, 1 }, { 0.7f, 0.12f, 2 }, { 0.85f, 0.15f, 0 } };
                for (auto& n : notes)
                {
                    g.setColour (n.x < 0.5f ? Theme::accent : Theme::led);
                    g.fillRoundedRectangle (a.getX() + n.x * a.getWidth(), a.getY() + (float) n.row * rh + 1.0f, n.w * a.getWidth() - 2.0f, rh - 3.0f, 2.0f);
                }
                break;
            }
            case 4:
            {
                const float rh = a.getHeight() / 4.0f, bw = a.getWidth() / 8.0f;
                static const char* lanes[] = { "..xxxxxx", "..xxxx.x", "xxxxxxxx", "....xxxx" };
                const juce::Colour colours[] = { juce::Colour (0xffe8664a), juce::Colour (0xff4aa3e8), juce::Colour (0xff9c6ae8), juce::Colour (0xff59c27a) };
                for (int l = 0; l < 4; ++l)
                    for (int b = 0; b < 8; ++b)
                        if (lanes[l][b] == 'x')
                        {
                            g.setColour (colours[l].withAlpha (0.85f));
                            g.fillRoundedRectangle (a.getX() + (float) b * bw + 1.0f, a.getY() + (float) l * rh + 2.0f, bw - 2.0f, rh - 4.0f, 3.0f);
                        }
                juce::Path p;
                p.startNewSubPath (a.getX(), a.getBottom() - 4.0f);
                p.lineTo (a.getX() + a.getWidth() * 0.35f, a.getY() + 6.0f);
                p.lineTo (a.getRight(), a.getY() + 6.0f);
                g.setColour (Theme::textBright.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (2.0f));
                break;
            }
            case 5:
            {
                const int strips = 6;
                const float sw = a.getWidth() / (float) strips;
                static const float levels[] = { 0.8f, 0.6f, 0.7f, 0.45f, 0.65f, 0.9f };
                for (int s = 0; s < strips; ++s)
                {
                    const float x = a.getX() + (float) s * sw + sw * 0.5f;
                    g.setColour (Theme::raised);
                    g.fillRect (x - 2.0f, a.getY(), 4.0f, a.getHeight());
                    const float y = a.getBottom() - levels[s] * a.getHeight();
                    g.setColour (Theme::led.withAlpha (0.7f));
                    g.fillRect (x + 6.0f, y, 4.0f, a.getBottom() - y);
                    g.setColour (s == strips - 1 ? Theme::accent : Theme::text);
                    g.fillRoundedRectangle (x - sw * 0.3f, y - 5.0f, sw * 0.6f, 10.0f, 2.0f);
                }
                break;
            }
            default: break;
        }
    }

    std::vector<Card> cards;
    int page = 0, dragStartX = 0;
    juce::TextButton backButton { "Indietro" }, nextButton { "Avanti" }, skipButton { "Chiudi" };
    juce::OwnedArray<juce::TextButton> demoButtons;
    juce::Rectangle<int> card, counterArea, titleArea, bodyArea, pictureArea, dotsArea;
};
