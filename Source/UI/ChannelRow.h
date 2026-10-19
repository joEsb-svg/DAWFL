#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"

//==============================================================================
/**
    Una riga del channel rack:
    [LED mute] [volume] [nome + menu ▾] [16 step]

    Tocca il nome per ascoltare, tienilo premuto (o usa il menu ▾) per aprire l'editor del canale.

    Modalita' di modifica degli step:
    - STEP: tocca per accendere/spegnere, trascina per "dipingerne" tanti
    - VEL:  trascina in verticale per la forza (velocity)
    - PROB: trascina in verticale per la probabilita' che lo step suoni
    - ROLL: tocca per ripetere il colpo 1, 2, 3 o 4 volte nello step (ratchet)
*/
class ChannelRow : public juce::Component
{
public:
    enum class EditMode { step, velocity, probability, ratchet };

    ChannelRow (SequencerEngine& engine, int channelIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Quale pagina di 16 step mostrare e in che modalita' di modifica. */
    void setView (int firstStepToShow, EditMode mode);

    /** Chiamato dal timer della schermata: aggiorna playhead, lampeggio, cambi esterni. */
    void refresh (int currentStep);

    /** Posizione della colonna visibile i (0..15), in coordinate locali della riga. */
    juce::Rectangle<float> getPadBounds (int column) const;

    /** Larghezza della parte sinistra (LED, volume, nome), uguale per tutte le righe. */
    static float getHeaderWidth (float rowWidth);

    static constexpr int visibleSteps = 16;

    std::function<void (int channel)> onOpenEditor;
    std::function<void()> onTap;              // per la vibrazione

private:
    juce::Rectangle<float> getPadsArea() const;
    int columnAt (juce::Point<float>) const;
    void applyVerticalValueAt (juce::Point<float>);
    void showRowMenu();

    SequencerEngine& engine;
    const int channel;
    const juce::Colour colour;

    juce::Slider volumeKnob;
    juce::Rectangle<float> muteArea, nameArea, menuArea;

    int      firstStep = 0;
    EditMode mode = EditMode::step;
    int      current = -1;
    float    flash = 0.0f;
    bool     paintValue = true;
    int      lastColumn = -1;
    bool     pressOnName = false;
    int      shownKit = -1;
    int      shownPattern = -1;

    /** Appunti condivisi tra le righe per "Copia riga" / "Incolla riga". */
    struct RowClipboard
    {
        bool valid = false;
        uint8_t velocity[SequencerEngine::maxSteps] {}, probability[SequencerEngine::maxSteps] {}, ratchet[SequencerEngine::maxSteps] {};
    };
    static RowClipboard clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRow)
};
