#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/SequencerEngine.h"

//==============================================================================
/**
    Una riga del channel rack:
    [LED mute] [volume] [nome + menu ▾] [16 step]

    Tocca il nome per ascoltare; tienilo premuto (o usa il menu ▾) per il menu del canale:
    cambia suono, clona, muto, solo, rinomina, colore, sposta, elimina...
    Trascinando in verticale sul nome si scorre la lista dei canali.

    Modalita' di modifica degli step:
    - STEP: tocca per accendere/spegnere, trascina per "dipingerne" tanti
    - VEL:  trascina in verticale per la forza (velocity)
    - PROB: trascina in verticale per la probabilita' che lo step suoni
    - ROLL: tocca per ripetere il colpo 1, 2, 3 o 4 volte nello step (ratchet)
*/
class ChannelRow : public juce::Component,
                   private juce::Timer
{
public:
    enum class EditMode { step, velocity, probability, ratchet };

    /** Comandi del menu che gestisce la schermata principale. */
    enum Command
    {
        changeSound = 10, editSound = 11, openPianoRoll = 12, rename = 13, solo = 16,
        clone = 17, moveUp = 18, moveDown = 19, remove = 20, openMixer = 21, colourBase = 1400, mixerTrackBase = 3000
    };

    static constexpr int numColours = 12;
    static juce::Colour getPaletteColour (int i);
    static juce::String getPaletteName (int i);

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
    void setPlayTick (int tick);

    /** Posizione della colonna visibile i (0..15), in coordinate locali della riga. */
    juce::Rectangle<float> getPadBounds (int column) const;

    /** Larghezza della parte sinistra (LED, volume, nome), uguale per tutte le righe. */
    float getHeaderWidth() const;

    /** Quanti step mostrare (16, o 8 in verticale) e se usare la versione compatta (senza manopola). */
    void setLayout (int stepsToShow, bool compactHeader);
    int getVisibleSteps() const     { return visibleSteps; }

    int getChannel() const          { return channel; }

    std::function<void (int channel)> onOpenEditor;
    std::function<void (int channel)> onOpenPianoRoll;
    std::function<void (int channel, int command)> onCommand;      // vedi Command
    std::function<void (float deltaY, bool started)> onScrollDrag;  // trascinamento verticale sul nome
    std::function<void()> onLongPress;                               // per la vibrazione

    /** Nome da mostrare sotto quello dello strumento: kit o campione. */
    static juce::String getSoundLabel (SequencerEngine&, int channel);
    std::function<void()> onTap;              // per la vibrazione

    void showRowMenu();

private:
    void timerCallback() override;
    juce::Colour getColour() const  { return juce::Colour (engine.getChannel (channel).colour.load()); }
    juce::Rectangle<float> getPadsArea() const;
    int columnAt (juce::Point<float>) const;
    void applyVerticalValueAt (juce::Point<float>);

    SequencerEngine& engine;
    const int channel;

    juce::Slider volumeKnob;
    juce::Rectangle<float> muteArea, nameArea, menuArea;

    int      firstStep = 0;
    int      visibleSteps = 16;
    bool     compact = false;
    EditMode mode = EditMode::step;
    int      current = -1;
    float    flash = 0.0f;
    bool     paintValue = true;
    int      lastColumn = -1;
    bool     pressOnName = false, nameDragging = false, longPressDone = false;
    juce::uint64 shownLook = 0;
    int      shownKit = -1;
    int      playTick = -1;
    juce::uint64 shownNotesHash = 0;
    void paintNotesPreview (juce::Graphics&, bool muted);
    const void* shownSample = nullptr;
    juce::String shownLabel;
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
