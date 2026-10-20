#include "MainComponent.h"
#include "Theme.h"
#include "../Engine/ProjectIO.h"
#include "../Engine/Performance.h"
#include "../Instruments/DrumForge/DrumForgeEditor.h"

namespace
{
    const juce::String untitled = "Senza titolo";

    void setupKnob (juce::Slider& s, double min, double max, double resetValue)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (min, max);
        s.setDoubleClickReturnValue (true, resetValue);
        s.setMouseDragSensitivity (160);
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // anche per finestre e menu

    // --- Strumenti (BassForge, PolyForge...): uno per ogni canale strumento
    instruments = std::make_unique<Instruments::Host> (engine);

    synthPanel = std::make_unique<SynthPanel> (engine);
    synthPanel->onClose   = [this] { synthPanel->closeAll(); synthPanel->setVisible (false); };
    synthPanel->onOpenMix = [this]
    {
        const int c = synthPanel->getChannel();
        synthPanel->closeAll();
        synthPanel->setVisible (false);
        if (engine.isSynth (c))
            openEditor (c);
    };
    synthPanel->onShowPresets = [this] (juce::Component& target) { showPresetMenu (synthPanel->getChannel(), target); };
    addChildComponent (*synthPanel);

    // Scelta dei preset a tutto schermo (al posto del menu a tendina)
    addChildComponent (presetBrowser);
    presetBrowser.onClose = [this] { presetBrowser.setVisible (false); };
    presetBrowser.onChanged = [this]
    {
        const int ch = presetBrowser.getChannel();
        synthPanel->refreshPresetName();
        if (auto* row = rows[ch]) row->repaint();
        if (editor.isVisible()) editor.refreshFromEngine();
    };
    presetBrowser.onOpenInstrument = [this] (int ch) { openSynthPanel (ch); };
    presetBrowser.onSaveRequest = [this] (int ch) { handleSoundMenu (ch, 660); };

    // --- Menu file
    fileButton.setButtonText ("FILE");
    fileButton.onClick = [this] { showFileMenu(); };
    addAndMakeVisible (fileButton);

    // --- Trasporto
    playButton.onClick = [this]
    {
        Haptics::tick (this);
        engine.setPlaying (! engine.isPlaying());
        playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);
    };
    addAndMakeVisible (playButton);

    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);

    bpmDisplay.caption = "Tempo";
    bpmDisplay.getText = [this] { return juce::String (juce::roundToInt (engine.getBpm())); };
    bpmDisplay.onStep  = [this] (int d) { engine.setBpm (std::round (engine.getBpm()) + d); };
    bpmDisplay.onDrag  = [this] (double d) { engine.setBpm (engine.getBpm() + d); };
    bpmDisplay.onCentreTap = [this] { tapTempo(); };   // tocca il numero a tempo: "tap tempo"
    addAndMakeVisible (bpmDisplay);

    // --- Undo / redo / metronomo
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    metroButton.setClickingTogglesState (true);
    metroButton.onClick = [this] { engine.setMetronome (metroButton.getToggleState()); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (metroButton);

    // --- Pattern
    patternDisplay.caption = "Pattern";
    patternDisplay.getText = [this] { return juce::String (engine.getCurrentPattern() + 1).paddedLeft ('0', 2); };
    patternDisplay.onStep  = [this] (int d)
    {
        // Le frecce scorrono i pattern in uso piu' uno nuovo (vuoto) in fondo
        const int count = juce::jmin (SequencerEngine::numPatterns, PatternColours::count (engine) + 1);
        engine.selectPattern ((engine.getCurrentPattern() + d + count) % count);
        updateViews();
    };
    patternDisplay.onCentreTap = [this] { showPatternMenu(); };
    addAndMakeVisible (patternDisplay);

    // --- Modalita' di modifica, lunghezza e pagina
    modeSwitch.onChange   = [this] (int) { updateViews(); };
    lengthSwitch.onChange = [this] (int i)
    {
        engine.setPatternLength (i == 1 ? 32 : 16);
        if (i == 0)
            pageSwitch.setSelected (0);
        updateViews();
    };
    pageSwitch.onChange   = [this] (int) { updateViews(); };
    addAndMakeVisible (modeSwitch);
    addAndMakeVisible (lengthSwitch);
    addAndMakeVisible (pageSwitch);

    // --- Swing, kit, master
    setupKnob (swingKnob, 0.0, 1.0, 0.0);
    swingKnob.onValueChange = [this] { engine.setSwing ((float) swingKnob.getValue()); };
    addAndMakeVisible (swingKnob);

    kitButton.onClick = [this] { showKitMenu(); };
    addAndMakeVisible (kitButton);

    setupKnob (masterKnob, 0.0, 1.5, 1.0);
    masterKnob.onValueChange = [this] { engine.setMasterVolume ((float) masterKnob.getValue()); };
    addAndMakeVisible (masterKnob);

    addAndMakeVisible (meter);

    // --- Channel rack (le righe si creano in rebuildRows, ogni volta che cambiano i canali)
    rackView.setViewedComponent (&rackContent, false);
    rackView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);   // il dito serve agli step
    rackView.setScrollBarsShown (true, false);
    rackView.setScrollBarThickness (6);
    addAndMakeVisible (rackView);

    addButton.setButtonText ("+ Strumento");
    addButton.setTooltip ("Aggiungi uno strumento al channel rack");
    addButton.onClick = [this] { Haptics::tick (this); openBrowser (-1); };
    addAndMakeVisible (addButton);

    // --- Playlist (modalita' canzone) ed esportazione
    songSwitch.onChange = [this] (int i)
    {
        Haptics::tick (this);
        engine.setSongMode (i == 1);
        if (i == 1) openPlaylist(); else playlist.setVisible (false);
    };
    addAndMakeVisible (songSwitch);

    playlist.onClose = [this] { playlist.setVisible (false); };
    playlist.getGrid().onEdit = [this] { Haptics::tick (this); };
    playlist.getGrid().onEditPattern = [this] (int pattern)
    {
        engine.selectPattern (pattern);
        playlist.setVisible (false);
        updateViews();
        for (auto* row : rows) row->repaint();
    };
    addChildComponent (playlist);

    exportPanel.onClose = [this] { exportPanel.setVisible (false); };
    mixerPanel.onClose = [this] { mixerPanel.setVisible (false); };
    addChildComponent (mixerPanel);
    mixerButton.onClick = [this] { if (mixerPanel.isVisible()) mixerPanel.setVisible (false); else openMixer(); };
    addAndMakeVisible (mixerButton);
    cpuBadge.onClick = [this] { showPerformanceMenu(); };
    addAndMakeVisible (cpuBadge);
    exportPanel.getProjectJson = [this] { return ProjectIO::toJson (engine); };
    exportPanel.getProjectName = [this] { return projectName; };
    addChildComponent (exportPanel);

    browser.onClose  = [this] { browser.setVisible (false); };
    browser.onTap    = [this] { Haptics::tick (this); };
    browser.onChoose = [this] (int channel, const SoundChoice& choice) { applySoundChoice (channel, choice); };
    addChildComponent (browser);

    editor.onChangeSound = [this] (int ch) { openBrowser (ch); };
    editor.onClose = [this] { editor.setVisible (false); };
    pianoRoll.onClose = [this] { pianoRoll.setVisible (false); };
    pianoRoll.getGrid().onEdit = [this] { Haptics::tick (this); };
    addChildComponent (pianoRoll);
    editor.buildSoundMenu  = [this] (int ch) { return buildSoundMenu (ch); };
    editor.handleSoundMenu = [this] (int ch, int result) { handleSoundMenu (ch, result); };
    addChildComponent (editor);

    ruler.getColumnBounds = [this] (int i)
    {
        // Il righello e' allineato al contenuto del rack (le righe partono da x = 0)
        return rows.isEmpty() ? juce::Rectangle<float>() : rows.getFirst()->getPadBounds (i);
    };
    addAndMakeVisible (ruler);

    // --- Ripristina l'ultima sessione
    projectName = untitled;
    restoreSession();
    resolveSamples();
    rebuildRows();
    syncControlsFromEngine();
    lastSnapshot = takeSnapshot();

    // --- Audio: 0 ingressi, 2 uscite (stereo)
    audioError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engine);
    deviceManager.addMidiInputDeviceCallback ({}, this);   // tutte le tastiere MIDI attive
    scanMidiInputs();
    loadSettings();

    juce::Desktop::getInstance().addGlobalMouseListener (&touchWatcher);
    startTimerHz (40);
    setSize (900, 440);

    // Presentazione all'avvio (sopra a tutto, poi si toglie da sola)
    splash = std::make_unique<JLoopSplash>();
    splash->onFinished = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr && safe->splash != nullptr)
        {
            safe->removeChildComponent (safe->splash.get());
            safe->splash.reset();
            if (safe->settings != nullptr && ! safe->settings->getBoolValue ("guideShown", false))
                safe->showGuide();
        }
    };
    addAndMakeVisible (*splash);
    splash->setBounds (getLocalBounds());

    if (recoveredFromBackup)
        juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->showMessage ("Sessione recuperata",
                                   "L'ultimo salvataggio automatico era rovinato (forse il telefono si e' spento mentre salvava). "
                                   "Ho riaperto la copia di sicurezza precedente; il file rovinato e' stato messo da parte.");
        });
}

MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (&touchWatcher);
    stopTimer();
    saveSession();
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (&engine);
    deviceManager.closeAudioDevice();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> MainComponent::getContentArea() const
{
    auto area = getLocalBounds();

    // Su Android rispetta notch e barre di sistema.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        area = display->safeAreaInsets.subtractedFrom (area);

    return area.reduced (8, 6);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    // Barra superiore
    g.setColour (Theme::panel);
    g.fillRect (headerArea);
    g.setColour (Theme::border);
    g.drawHorizontalLine ((int) headerArea.getBottom(), headerArea.getX(), headerArea.getRight());

    // Pannello del channel rack
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (rackArea, Theme::corner);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (rackArea, Theme::corner, 1.0f);

    // Logo e nome del progetto
    {
        auto r = logoArea;
        auto top = r.removeFromTop (r.getHeight() * 0.58f);
        juce::AttributedString logo;
        logo.append ("J",    Theme::font (top.getHeight() * 0.82f, true), Theme::accent);
        logo.append ("Loop", Theme::font (top.getHeight() * 0.82f, true), Theme::textBright);
        logo.setJustification (juce::Justification::bottomLeft);
        logo.draw (g, top);

        g.setColour (engine.isRecording() ? juce::Colour (0xffe0483e) : Theme::textDim);
        g.setFont (Theme::font (11.5f, engine.isRecording()));
        const int target = liveTarget.load();
        g.drawFittedText (engine.isRecording() && target >= 0 && target < engine.getNumChannels()
                              ? "REC: " + engine.getChannel (target).name : projectName,
                          r.toNearestInt(), juce::Justification::topLeft, 1);
    }

    Theme::drawCaption (g, "Swing",  swingCaption);
    Theme::drawCaption (g, "Kit",    kitCaption);
    Theme::drawCaption (g, "Master", masterCaption);
}

void MainComponent::resized()
{
    if (guide != nullptr)
        guide->setBounds (getLocalBounds());

    if (splash != nullptr)
    {
        splash->setBounds (getLocalBounds());
        splash->toFront (false);
    }

    auto full = getContentArea();
    portraitLayout = full.getWidth() < full.getHeight();
    patternDisplay.caption = portraitLayout || full.getWidth() < 1000 ? "Pat" : "Pattern";

    if (portraitLayout)
        layoutPortrait (full);
    else
        layoutLandscape (full);

    updateViews();
}

void MainComponent::layoutLandscape (juce::Rectangle<int> full)
{
    // --- Barra superiore (a tutta larghezza)
    const int headerH = juce::jlimit (46, 64, full.getHeight() / 7);
    headerArea = getLocalBounds().removeFromTop (full.getY() + headerH).toFloat();
    auto header = full.removeFromTop (headerH).reduced (0, 5);
    const int h = header.getHeight();

    // Su schermi stretti (telefono in orizzontale, ~800-900 punti) tutto un po' piu' compatto
    const bool narrow = header.getWidth() < 1000;
    logoArea = header.removeFromLeft (narrow ? 72 : 88).toFloat();
    header.removeFromLeft (4);
    fileButton.setBounds (header.removeFromLeft (50));
    header.removeFromLeft (6);
    undoButton.setBounds (header.removeFromLeft (h));
    header.removeFromLeft (3);
    redoButton.setBounds (header.removeFromLeft (h));
    header.removeFromLeft (narrow ? 6 : 10);
    playButton.setBounds (header.removeFromLeft (juce::roundToInt (h * 1.25)));
    header.removeFromLeft (3);
    recordButton.setBounds (header.removeFromLeft (h));
    header.removeFromLeft (6);
    bpmDisplay.setBounds (header.removeFromLeft (narrow ? 88 : 100));
    header.removeFromLeft (6);
    patternDisplay.setBounds (header.removeFromLeft (narrow ? 88 : 100));
    header.removeFromLeft (6);
    const int sw = narrow ? 84 : 92;
    songSwitch.setBounds (header.removeFromLeft (sw).withSizeKeepingCentre (sw, juce::jmin (h, 30)));
    header.removeFromLeft (6);
    mixerButton.setVisible (true);
    mixerButton.setButtonText (narrow ? "MIX" : "MIXER");
    mixerButton.setBounds (header.removeFromLeft (narrow ? 50 : 64).withSizeKeepingCentre (narrow ? 50 : 64, juce::jmin (h, 32)));

    meter.setBounds (header.removeFromRight (8));
    header.removeFromRight (6);

    auto knobWithCaption = [&header, h] (juce::Slider& knob, juce::Rectangle<float>& caption, int width)
    {
        auto area = header.removeFromRight (width);
        caption = area.removeFromBottom (12).toFloat();
        knob.setBounds (area.withSizeKeepingCentre (h - 12, h - 12));
        header.removeFromRight (4);
    };

    knobWithCaption (masterKnob, masterCaption, 48);
    {
        auto area = header.removeFromRight (juce::jlimit (80, 110, header.getWidth() / 2));
        kitCaption = area.removeFromBottom (12).toFloat();
        kitButton.setBounds (area.reduced (0, 1));
        header.removeFromRight (6);
    }
    knobWithCaption (swingKnob, swingCaption, 44);

    full.removeFromTop (6);
    layoutRack (full, false);
}

void MainComponent::layoutPortrait (juce::Rectangle<int> full)
{
    // Due righe in alto: [logo, file, undo, redo ... meter] e [play, tempo, pattern, swing, kit, master]
    const int lineH = 44;
    headerArea = getLocalBounds().removeFromTop (full.getY() + lineH * 2 + 8).toFloat();

    auto line1 = full.removeFromTop (lineH).reduced (0, 4);
    const int h1 = line1.getHeight();
    logoArea = line1.removeFromLeft (88).toFloat();
    meter.setBounds (line1.removeFromRight (8));
    line1.removeFromRight (8);
    redoButton.setBounds (line1.removeFromRight (h1));
    line1.removeFromRight (3);
    undoButton.setBounds (line1.removeFromRight (h1));
    line1.removeFromRight (6);
    fileButton.setBounds (line1.removeFromRight (50));
    line1.removeFromRight (8);
    {
        // Se c'e' posto, il pulsante del mixer (altrimenti e' nel MENU)
        const bool roomForMixer = line1.getWidth() >= 64 + 50 + 6;
        mixerButton.setVisible (roomForMixer);
        mixerButton.setButtonText ("MIX");
        if (roomForMixer)
        {
            mixerButton.setBounds (line1.removeFromRight (50).withSizeKeepingCentre (50, 30));
            line1.removeFromRight (6);
        }
        const int sw = juce::jmin (100, line1.getWidth());
        songSwitch.setBounds (line1.removeFromRight (sw).withSizeKeepingCentre (sw, 30));
    }

    full.removeFromTop (4);
    auto line2 = full.removeFromTop (lineH).reduced (0, 2);
    const int h2 = line2.getHeight();
    playButton.setBounds (line2.removeFromLeft (h2));
    line2.removeFromLeft (3);
    recordButton.setBounds (line2.removeFromLeft (juce::roundToInt (h2 * 0.8)));
    line2.removeFromLeft (4);
    const int lcdW = juce::jmin (92, (line2.getWidth() - 3 * 40) / 2 - 10);
    bpmDisplay.setBounds (line2.removeFromLeft (lcdW));
    line2.removeFromLeft (4);
    patternDisplay.setBounds (line2.removeFromLeft (lcdW));
    line2.removeFromLeft (6);

    auto knobCell = [&line2] (juce::Component& c, juce::Rectangle<float>& caption, int width)
    {
        auto area = line2.removeFromLeft (width);
        caption = area.removeFromBottom (11).toFloat();
        c.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()), area.getHeight()));
    };

    knobCell (swingKnob, swingCaption, 40);
    {
        auto area = line2.removeFromLeft (juce::jmax (40, line2.getWidth() - 50));
        kitCaption = area.removeFromBottom (11).toFloat();
        kitButton.setBounds (area.reduced (2, 1));
    }
    knobCell (masterKnob, masterCaption, 50);

    full.removeFromTop (8);
    layoutRack (full, true);
}

void MainComponent::layoutRack (juce::Rectangle<int> full, bool portrait)
{
    rackArea = full.toFloat();
    auto rack = full.reduced (6, 4);

    // Barra degli strumenti del sequencer (in verticale va su due righe)
    auto bar = rack.removeFromTop (26);
    modeSwitch.setBounds (bar.removeFromLeft (juce::jmin (220, bar.getWidth() - 110)).reduced (0, 1));
    bar.removeFromLeft (6);
    lengthSwitch.setBounds (bar.removeFromLeft (64).reduced (0, 1));
    bar.removeFromLeft (6);

    if (portrait)
    {
        metroButton.setBounds (bar.removeFromLeft (34));
        bar.removeFromLeft (6);
        addButton.setButtonText ("+");
        addButton.setBounds (bar.removeFromLeft (juce::jmax (40, bar.getWidth())).reduced (0, 1));
        rack.removeFromTop (4);
        auto pageRow = rack.removeFromTop (24);
        cpuBadge.setBounds (pageRow.removeFromRight (74));
        pageRow.removeFromRight (6);
        pageSwitch.setBounds (pageRow.reduced (0, 1));
    }
    else
    {
        addButton.setButtonText ("+ Strumento");
        addButton.setBounds (bar.removeFromRight (juce::jmin (120, bar.getWidth() / 3)).reduced (0, 1));
        pageSwitch.setBounds (bar.removeFromLeft (100 + (getVisibleSteps() < 16 ? 100 : 0)).reduced (0, 1));
        bar.removeFromLeft (8);
        metroButton.setBounds (bar.removeFromLeft (34));
        bar.removeFromLeft (8);
        cpuBadge.setBounds (bar.removeFromLeft (juce::jmin (84, bar.getWidth() - 8)));
    }

    // Righello allineato agli step, poi le righe (in un'area che scorre se non stanno tutte)
    auto rulerArea = rack.removeFromTop (16);
    rack.removeFromTop (2);
    rackView.setBounds (rack);

    const int n = juce::jmax (1, rows.size());
    const int gap = 4;
    const int minH = portrait ? 56 : 34, maxH = portrait ? 84 : 90;
    const int rowH = juce::jlimit (minH, maxH, (rack.getHeight() - gap * (n - 1)) / n);
    const int contentH = n * (rowH + gap) - gap;
    const int w = rack.getWidth() - (contentH > rack.getHeight() ? 9 : 0);   // spazio per la barra di scorrimento

    rackContent.setSize (w, contentH);
    int y = 0;
    for (auto* row : rows)
    {
        row->setLayout (portrait ? 8 : 16, portrait);
        row->setBounds (0, y, w, rowH);
        y += rowH + gap;
    }

    ruler.setBounds (rack.getX(), rulerArea.getY(), w, rulerArea.getHeight());
    editor.setBounds (rack.withTop (rulerArea.getY()).expanded (2, 2));
    pianoRoll.setBounds (full);   // il piano roll copre tutto il channel rack, barra compresa
    browser.setBounds (full);
    playlist.setBounds (full);
    exportPanel.setBounds (full);
    mixerPanel.setBounds (full);
    if (synthPanel != nullptr)
        synthPanel->setBounds (getContentArea());   // BassForge usa tutto lo schermo
    presetBrowser.setBounds (getContentArea());
}

int MainComponent::getVisibleSteps() const
{
    return rows.isEmpty() ? 16 : rows.getFirst()->getVisibleSteps();
}

int MainComponent::getFirstVisibleStep() const
{
    return pageSwitch.getSelected() * getVisibleSteps();
}

//==============================================================================
void MainComponent::updateViews()
{
    const int length  = engine.getPatternLength();
    const int visible = getVisibleSteps();
    lengthSwitch.setSelected (length > 16 ? 1 : 0);

    // Una pagina per ogni gruppo di step visibili (es. 32 step a 16 per volta = 2 pagine)
    juce::StringArray pages;
    for (int first = 0; first < length; first += visible)
        pages.add (juce::String (first + 1) + "-" + juce::String (first + visible));
    pageSwitch.setOptions (pages);
    pageSwitch.setEnabled (pages.size() > 1);
    pageSwitch.setAlpha (pages.size() > 1 ? 1.0f : 0.35f);

    const int firstStep = getFirstVisibleStep();
    const auto mode = (ChannelRow::EditMode) modeSwitch.getSelected();

    for (auto* row : rows)
        row->setView (firstStep, mode);

    undoButton.setEnabled (! undoStack.isEmpty());
    redoButton.setEnabled (! redoStack.isEmpty());

    patternDisplay.repaint();
    repaint (headerArea.toNearestInt());
}

void MainComponent::syncControlsFromEngine()
{
    swingKnob.setValue (engine.getSwing(), juce::dontSendNotification);
    metroButton.setToggleState (engine.isMetronomeOn(), juce::dontSendNotification);
    if (synthPanel != nullptr)
        synthPanel->refreshPresetName();
    if (editor.isVisible())
        editor.refreshFromEngine();
    masterKnob.setValue (engine.getMasterVolume(), juce::dontSendNotification);
    kitButton.setButtonText (getKitLabel());
    bpmDisplay.repaint();
    updateViews();
}

juce::String MainComponent::getKitLabel() const
{
    // Se tutta la batteria sintetizzata usa lo stesso kit ne mostra il nome, altrimenti "Misto".
    int first = -1;
    for (int c = 0; c < engine.getNumChannels(); ++c)
    {
        if (! engine.isDrumSynthChannel (c))
            continue;
        if (first < 0)
            first = engine.getChannelKit (c);
        else if (engine.getChannelKit (c) != first)
            return "Misto";
    }

    return first < 0 ? juce::String ("Kit") : juce::String (SoundLibrary::getKitName (first));
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Canali aggiunti, tolti o spostati (anche da undo e caricamento): ricrea le righe
    if (engine.getStructureVersion() != shownStructure)
        rebuildRows();

    const int step = engine.getCurrentStep();
    const int firstStep = getFirstVisibleStep();
    const int visible = getVisibleSteps();

    const int tick = engine.getPlayingPattern() == engine.getCurrentPattern() ? engine.getCurrentTick() : -1;

    for (auto* row : rows)
    {
        row->refresh (step);
        row->setPlayTick (tick);
    }

    if (pianoRoll.isVisible())
        pianoRoll.getGrid().setPlayTick (tick);

    if (playlist.isVisible())
        playlist.refresh();

    if (songSwitch.getSelected() != (engine.isSongMode() ? 1 : 0))   // undo, caricamento progetto
        songSwitch.setSelected (engine.isSongMode() ? 1 : 0);

    const bool playingThis = engine.getPlayingPattern() == engine.getCurrentPattern();
    const int column = (playingThis && step >= firstStep && step < firstStep + visible) ? step - firstStep : -1;
    ruler.setState (column, firstStep, visible);
    meter.setLevel (engine.takeOutputPeak());

    if (playButton.getToggleState() != engine.isPlaying())
        playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);

    if (const auto label = getKitLabel(); kitButton.getButtonText() != label)
        kitButton.setButtonText (label);

    // Undo: cerca modifiche ~8 volte al secondo
    // (solo dopo un tocco dello schermo, e comunque ogni 3 secondi: costa poco al telefono)
    if (++snapshotCounter >= 120 || (snapshotCounter >= 4 && userTouched.exchange (false)))
    {
        snapshotCounter = 0;
        checkForChanges();
    }

    // Autosalvataggio: 2 secondi dopo una modifica, e comunque ogni 10 secondi (i synth cambiano anche fuori dall'undo)
    if (++autosaveCounter >= 40 * 10 || (sessionDirty && juce::Time::getMillisecondCounter() - dirtySince > 2000))
    {
        autosaveCounter = 0;
        sessionDirty = false;
        saveSession();
    }

    // Registrazione: le note suonate entrano nel pattern
    if (engine.isRecording() || recordButton.getToggleState())
    {
        placeRecordedNotes();
        recordButton.setBlink ((juce::Time::getMillisecondCounter() / 400) % 2 == 0);
        if (recordButton.getToggleState() != engine.isRecording())
            recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
    }

    // Tastiere MIDI collegate (USB o Bluetooth): si attivano da sole
    if (++midiScanCounter >= 80)
    {
        midiScanCounter = 0;
        scanMidiInputs();
    }

    // Processore e scatti audio, 4 volte al secondo
    if (++cpuCounter >= 10)
    {
        cpuCounter = 0;
        updateCpuBadge();
    }
}

//==============================================================================
/**
    Ripristina l'ultima sessione. L'autosalvataggio ha una copia di sicurezza (l'ultima che si e' aperta
    bene): se il file fosse rovinato (telefono spento a meta' scrittura...) si riparte da quella,
    e il file rovinato viene messo da parte invece di essere sovrascritto.
*/
void MainComponent::restoreSession()
{
    const auto file = ProjectIO::getAutosaveFile();
    const auto backup = file.withFileExtension ("bak");

    auto tryLoad = [this] (const juce::File& f)
    {
        if (! f.existsAsFile())
            return false;
        const auto json = juce::JSON::parse (f.loadFileAsString());
        if (! ProjectIO::fromJson (engine, json))
            return false;
        projectName = json.getProperty ("projectName", untitled).toString();
        return true;
    };

    if (tryLoad (file))
    {
        file.copyFileTo (backup);   // questa si e' aperta: diventa la copia di sicurezza
        return;
    }

    // Rovinato (o sparito): si mette da parte e si riparte dalla copia di sicurezza
    const bool damaged = file.existsAsFile();
    if (damaged)
        file.moveFileTo (file.getSiblingFile ("autosave-danneggiato-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".dawfl"));
    if (tryLoad (backup))
    {
        recoveredFromBackup = damaged;
        backup.copyFileTo (file);
    }
    else
        engine.resetProject();
}

void MainComponent::saveSession()
{
    auto json = ProjectIO::toJson (engine);
    if (auto* obj = json.getDynamicObject())
        obj->setProperty ("projectName", projectName);

    const auto text = juce::JSON::toString (json);
    if (text == lastSavedJson)
        return;

    auto file = ProjectIO::getAutosaveFile();
    file.getParentDirectory().createDirectory();
    // replaceWithText scrive prima un file temporaneo e poi lo sostituisce: se il telefono si spegne
    // a meta', il file vecchio resta intero
    if (file.replaceWithText (text))
        lastSavedJson = text;
}

//==============================================================================
void MainComponent::showFileMenu()
{
    juce::PopupMenu openMenu, deleteMenu;
    const auto projects = ProjectIO::listProjects();

    for (int i = 0; i < projects.size(); ++i)
    {
        openMenu.addItem (1000 + i, projects[i]);
        deleteMenu.addItem (2000 + i, projects[i]);
    }

    if (projects.isEmpty())
        openMenu.addItem (-1, "(nessun progetto salvato)", false);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Progetto: " + projectName);
    juce::PopupMenu demoMenu;
    for (int i = 0; i < (int) demos::list().size(); ++i)
        demoMenu.addItem (3000 + i, juce::String (demos::list()[(size_t) i].name) + "  -  " + demos::list()[(size_t) i].description);

    menu.addItem (1, "Nuovo progetto");
    menu.addSubMenu ("Apri", openMenu);
    menu.addSubMenu ("Progetti demo", demoMenu);
    menu.addItem (2, "Salva", true);
    menu.addItem (3, "Salva con nome...");
    menu.addSubMenu ("Elimina", deleteMenu, ! projects.isEmpty());
    menu.addSeparator();
    menu.addItem (7, "Mixer...");
    menu.addItem (6, "Esporta audio (WAV / OGG)...");
    menu.addSeparator();
    menu.addSubMenu ("Impostazioni", createSettingsMenu());
    menu.addItem (4, "Info audio");
    menu.addItem (8, "Guida rapida");
    menu.addSeparator();
    menu.addItem (-1, "JLoop versione " + juce::String (ProjectInfo::versionString), false);

    juce::Component::SafePointer<MainComponent> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton),
                        [safeThis, projects] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& self = *safeThis;

                            if (result == 1)
                                self.newProject();
                            else if (result == 2)
                            {
                                if (self.projectName == untitled)
                                    self.askProjectName ("Salva progetto", [safeThis] (juce::String n) { if (safeThis) safeThis->saveProjectAs (n, true); });
                                else
                                    self.saveProjectAs (self.projectName, false);
                            }
                            else if (result == 3)
                                self.askProjectName ("Salva con nome", [safeThis] (juce::String n) { if (safeThis) safeThis->saveProjectAs (n, true); });
                            else if (result == 4)
                                self.showAudioInfo();
                            else if (result == 6)
                                self.openExport();
                            else if (result == 7)
                                self.openMixer();
                            else if (result == 8)
                                self.showGuide();
                            else if (result >= 3000 && result < 4000)
                                self.loadDemo (result - 3000);
                            else if (result >= 5000 && result < 6000)
                                self.handleSettingsResult (result);
                            else if (result >= 2000)
                                self.deleteProject (projects[result - 2000]);
                            else if (result >= 1000)
                                self.openProject (projects[result - 1000]);
                        });
}

void MainComponent::askProjectName (const juce::String& title, std::function<void (juce::String)> onName)
{
    auto* window = new juce::AlertWindow (title, "Nome del progetto:", juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&lookAndFeel);
    window->addTextEditor ("name", projectName == untitled ? juce::String() : projectName);
    window->addButton ("Salva", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* textEditor = window->getTextEditor ("name"))
        textEditor->setFont (Theme::font (18.0f));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onName] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty())
            onName (name);
    }), true);
}

void MainComponent::saveProjectAs (const juce::String& name, bool confirmOverwrite)
{
    const auto file = ProjectIO::getProjectFile (name);

    if (confirmOverwrite && file.existsAsFile() && name != projectName)
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::NoIcon, "Sovrascrivere?",
                                                                     "Esiste gia' un progetto \"" + name + "\". Vuoi sostituirlo?",
                                                                     "Sostituisci", "Annulla", this);
        juce::AlertWindow::showAsync (options, [safeThis, name] (int result)
        {
            if (safeThis != nullptr && result == 1)
                safeThis->saveProjectAs (name, false);
        });
        return;
    }

    if (file.existsAsFile())
        file.copyFileTo (file.withFileExtension ("bak"));   // la versione precedente, per sicurezza

    if (ProjectIO::saveToFile (engine, file))
    {
        projectName = file.getFileNameWithoutExtension();
        saveSession();
        repaint (logoArea.toNearestInt());
    }
    else
    {
        showMessage ("Errore", "Non sono riuscito a salvare il progetto.");
    }
}

void MainComponent::openProject (const juce::String& name)
{
    if (ProjectIO::loadFromFile (engine, ProjectIO::getProjectFile (name)))
    {
        synthPanel->onClose();
        resolveSamples();
        instruments->resolve();
        projectName = name;
        pageSwitch.setSelected (0);
        undoStack.clear(); redoStack.clear(); lastSnapshot = takeSnapshot();
        syncControlsFromEngine();
        saveSession();
    }
    else
    {
        showMessage ("Errore", "Il progetto \"" + name + "\" non si apre: il file sembra danneggiato.");
    }
}

void MainComponent::deleteProject (const juce::String& name)
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::NoIcon, "Eliminare?",
                                                                 "Il progetto \"" + name + "\" verra' eliminato definitivamente.",
                                                                 "Elimina", "Annulla", this);
    juce::AlertWindow::showAsync (options, [safeThis, name] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        ProjectIO::getProjectFile (name).deleteFile();

        if (safeThis->projectName == name)
        {
            safeThis->projectName = untitled;
            safeThis->repaint();
        }
    });
}

void MainComponent::newProject()
{
    engine.setPlaying (false);
    synthPanel->onClose();
    engine.resetProject();
    instruments->resolve();      // strumenti nuovi, al preset "Init"
    projectName = untitled;
    pageSwitch.setSelected (0);
    modeSwitch.setSelected (0);
    undoStack.clear(); redoStack.clear(); lastSnapshot = takeSnapshot();
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    synthPanel->onClose();
    syncControlsFromEngine();
    saveSession();
    repaint();
}

void MainComponent::loadDemo (int index)
{
    if (index < 0 || index >= (int) demos::list().size())
        return;

    engine.setPlaying (false);
    synthPanel->onClose();
    engine.resetProject();
    engine.setNumChannels (1);          // la demo parte da un canale solo (il primo viene riusato)
    engine.resetChannel (0);
    engine.getChannel (0).mixerTrack.store (1);
    instruments->resolve();

    bool firstUsed = false;
    auto takeChannel = [this, &firstUsed]
    {
        if (! firstUsed) { firstUsed = true; return 0; }
        return engine.addChannel();
    };

    demos::Host host;
    host.addSample = [this, takeChannel] (const char* id, const char* label, float tune, float levelDb, float pan) mutable
    {
        auto* data = sampleStore.load (SampleStore::libraryName (id));
        if (data == nullptr)
            return -1;
        const int channel = takeChannel();
        if (channel < 0)
            return -1;
        auto& ch = engine.getChannel (channel);
        ch.type.store (SequencerEngine::drumOrSample);
        ch.sampleName = SampleStore::libraryName (id);
        ch.sample.store (data);
        if (auto* entry = SampleStore::findLibraryEntry (id))
            ch.colour.store (entry->colour);
        ch.name = juce::String (juce::CharPointer_UTF8 (label));
        ch.customName = true;
        ch.pitch.store (tune);
        ch.pan.store (pan);
        ch.volume.store (0.8f * juce::Decibels::decibelsToGain (levelDb));
        ch.touchSound();
        return channel;
    };
    host.addInstrument = [this, takeChannel] (const char* kind, const char* name, const char* preset) mutable
    {
        auto* info = Instruments::find (kind);
        const int channel = info != nullptr ? takeChannel() : -1;
        if (channel < 0)
            return -1;
        engine.makeInstrumentChannel (channel, info->id, name, info->colour);
        instruments->resolve();
        auto* proc = engine.getInstrument (channel);
        const auto presets = Instruments::factoryPresets (proc);
        if (const int p = presets.indexOf (preset); p >= 0)
            Instruments::loadFactory (proc, p);
        auto& ch = engine.getChannel (channel);
        ch.name = juce::String (juce::CharPointer_UTF8 (name));
        ch.customName = true;
        return channel;
    };

    demos::build (engine, index, host);

    projectName = juce::String ("Demo ") + demos::list()[(size_t) index].name;
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    rebuildRows();
    undoStack.clear(); redoStack.clear(); lastSnapshot = takeSnapshot();
    syncControlsFromEngine();
    openPlaylist();
    saveSession();
    repaint();
}

void MainComponent::showGuide()
{
    if (guide != nullptr)
        return;
    juce::StringArray names;
    for (auto& d : demos::list())
        names.add (d.name);
    guide = std::make_unique<QuickGuide> (names);
    guide->onFinished = [safe = juce::Component::SafePointer<MainComponent> (this)] (int demo)
    {
        if (safe != nullptr)
            juce::MessageManager::callAsync ([safe, demo] { if (safe != nullptr) safe->closeGuide (demo); });
    };
    addAndMakeVisible (*guide);
    guide->setBounds (getLocalBounds());
    guide->toFront (true);
}

void MainComponent::closeGuide (int demo)
{
    if (guide == nullptr)
        return;
    removeChildComponent (guide.get());
    guide.reset();
    if (settings != nullptr)
    {
        settings->setValue ("guideShown", true);
        settings->saveIfNeeded();
    }
    if (demo >= 0)
        loadDemo (demo);
}

void MainComponent::showPatternMenu()
{
    const int cur = engine.getCurrentPattern();
    const int shown = PatternColours::count (engine);
    const int fresh = shown < SequencerEngine::numPatterns ? shown : engine.getFirstFreePattern();
    juce::PopupMenu select, copyTo;

    for (int p = 0; p < shown; ++p)
    {
        // Il pallino indica i pattern che contengono note
        const auto label = engine.getPatternName (p)
                         + (engine.isPatternEmpty (p) ? juce::String() : juce::String (juce::CharPointer_UTF8 ("   \xe2\x97\x8f")));
        select.addColouredItem (100 + p, label, PatternColours::get (p), true, p == cur);

        if (p != cur)
            copyTo.addColouredItem (300 + p, engine.getPatternName (p), PatternColours::get (p));
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (engine.getPatternName (cur) + "   (" + juce::String (shown) + " di " + juce::String (SequencerEngine::numPatterns) + ")");
    menu.addItem (2, "Nuovo pattern vuoto", fresh >= 0);
    menu.addItem (3, "Clona in un pattern nuovo", fresh >= 0 && ! engine.isPatternEmpty (cur));
    menu.addSubMenu ("Vai a", select);
    menu.addSubMenu ("Copia in", copyTo, copyTo.getNumItems() > 0);
    menu.addSeparator();
    menu.addItem (4, "Rinomina");
    menu.addItem (1, "Svuota questo pattern", ! engine.isPatternEmpty (cur));

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternDisplay),
                        [safeThis, cur, fresh] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& e = safeThis->engine;
                            if (result == 1)               e.clearPattern (cur);
                            else if (result == 2)          e.selectPattern (fresh);
                            else if (result == 3)          { e.copyPattern (cur, fresh); e.setPatternName (fresh, {}); e.selectPattern (fresh); }
                            else if (result == 4)
                            {
                                safeThis->askText ("Nome del pattern", e.getPatternName (cur), [safeThis, cur] (juce::String name)
                                {
                                    if (safeThis != nullptr)
                                    {
                                        safeThis->engine.setPatternName (cur, name);
                                        safeThis->patternDisplay.repaint();
                                    }
                                });
                                return;
                            }
                            else if (result >= 300)        { e.copyPattern (cur, result - 300); e.selectPattern (result - 300); }
                            else if (result >= 100)        e.selectPattern (result - 100);

                            safeThis->updateViews();
                            for (auto* row : safeThis->rows)
                                row->repaint();
                        });
}

void MainComponent::showKitMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Kit per tutti gli strumenti");

    for (int k = 0; k < SoundLibrary::numKits; ++k)
        menu.addItem (k + 1, SoundLibrary::getKitName (k));

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitButton),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& e = safeThis->engine;
                            e.setAllKits (result - 1);
                            for (int c = 0; c < e.getNumChannels(); ++c)
                                if (e.isDrumSynthChannel (c)) { e.previewChannel (c); break; }
                            safeThis->kitButton.setButtonText (safeThis->getKitLabel());
                        });
}

void MainComponent::showAudioInfo()
{
    juce::String info;

    if (audioError.isNotEmpty())
        info = "Errore audio: " + audioError;
    else if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const double sr = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        info << "Driver: " << device->getTypeName() << "\n"
             << "Frequenza: " << juce::roundToInt (sr) << " Hz\n"
             << "Buffer: " << buffer << " campioni (" << juce::String (1000.0 * buffer / sr, 1) << " ms)\n"
             << "Latenza d'uscita: " << juce::String (1000.0 * device->getOutputLatencyInSamples() / sr, 1) << " ms";
    }
    else
        info = "Nessun dispositivo audio attivo.";

    showMessage ("Info audio", info);
}

void MainComponent::showMessage (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (title).withMessage (message)
                                                           .withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

//==============================================================================
void MainComponent::openSynthPanel (int channel)
{
    if (! engine.isSynth (channel))
        return;

    instruments->resolve();
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    synthPanel->setBounds (getContentArea());
    setLiveTarget (channel);
    if (auto* drums = dynamic_cast<DrumForgeAudioProcessor*> (engine.getInstrument (channel)))
    {
        // I pad toccati passano dal motore: cosi' si possono registrare
        drums->onPadHit = [this, channel] (int pad, float velocity)
        {
            engine.liveNote (channel, df::firstNote + pad, velocity);
            engine.liveNote (channel, df::firstNote + pad, 0.0f);
            return true;
        };
    }
    synthPanel->open (channel);
    synthPanel->setVisible (true);
    synthPanel->toFront (false);
}

void MainComponent::openPianoRoll (int channel)
{
    editor.setVisible (false);
    playlist.setVisible (false);
    exportPanel.setVisible (false);
    mixerPanel.setVisible (false);
    pianoRoll.getGrid().rowLabel = [this] (int ch, int pitch) -> juce::String
    {
        if (auto* drums = dynamic_cast<DrumForgeAudioProcessor*> (engine.getInstrument (ch)))
            if (const int pad = pitch - df::firstNote; pad >= 0 && pad < df::numPads)
                return juce::String (pad + 1) + "  " + drums->getPadName (pad);
        return {};
    };
    pianoRoll.setChannel (channel);
    pianoRoll.setVisible (true);
    pianoRoll.toFront (false);
}

void MainComponent::openEditor (int channel)
{
    pianoRoll.setVisible (false);
    playlist.setVisible (false);
    exportPanel.setVisible (false);
    mixerPanel.setVisible (false);
    editor.setChannel (channel);
    editor.setVisible (true);
    editor.toFront (false);
}

void MainComponent::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (! tapTimes.isEmpty() && now - tapTimes.getLast() > 2000.0)
        tapTimes.clear();

    tapTimes.add (now);
    if (tapTimes.size() > 5)
        tapTimes.remove (0);

    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.getLast() - tapTimes.getFirst()) / (tapTimes.size() - 1);
        engine.setBpm (std::round (60000.0 / avg));
        bpmDisplay.repaint();
    }
}

//==============================================================================
juce::String MainComponent::takeSnapshot()
{
    // Il pattern selezionato non conta come "modifica": lo togliamo dall'istantanea
    auto json = ProjectIO::toJson (engine, false);   // il synth ha i suoi preset: non entra nell'undo
    if (auto* obj = json.getDynamicObject())
    {
        obj->removeProperty ("currentPattern");
        obj->removeProperty ("songMode");   // cambiare PAT/SONG non e' una modifica da annullare
    }
    return juce::JSON::toString (json, true);
}

void MainComponent::applySnapshot (const juce::String& snapshot)
{
    const int pattern = engine.getCurrentPattern();
    const bool song = engine.isSongMode();
    ProjectIO::fromJson (engine, juce::JSON::parse (snapshot));
    engine.setSongMode (song);
    resolveSamples();
    instruments->resolve();      // riusa gli strumenti che c'erano (stesso id): l'undo non tocca i loro suoni
    engine.selectPattern (pattern);
    lastSnapshot = snapshot;
    syncControlsFromEngine();

    for (auto* row : rows)
        row->repaint();
}

void MainComponent::checkForChanges()
{
    // Aspetta che il dito sia sollevato: un gesto intero (trascinamento, manopola) = un solo undo
    if (juce::Desktop::getInstance().getNumDraggingMouseSources() > 0)
        return;

    const auto now = takeSnapshot();
    if (now == lastSnapshot)
        return;

    if (! sessionDirty)
    {
        sessionDirty = true;
        dirtySince = juce::Time::getMillisecondCounter();
    }

    undoStack.add (lastSnapshot);
    if (undoStack.size() > 60)
        undoStack.remove (0);

    redoStack.clear();
    lastSnapshot = now;
    updateViews();
}

void MainComponent::undo()
{
    checkForChanges();
    if (undoStack.isEmpty())
        return;

    redoStack.add (lastSnapshot);
    const auto previous = undoStack[undoStack.size() - 1];
    undoStack.remove (undoStack.size() - 1);
    applySnapshot (previous);
}

void MainComponent::redo()
{
    if (redoStack.isEmpty())
        return;

    undoStack.add (lastSnapshot);
    const auto next = redoStack[redoStack.size() - 1];
    redoStack.remove (redoStack.size() - 1);
    applySnapshot (next);
}

void MainComponent::handleCommandLine (const juce::String& commandLine)
{
    auto args = juce::StringArray::fromTokens (commandLine, true);

    if (args.contains ("--no-splash") && splash != nullptr)   // test: niente presentazione
    {
        removeChildComponent (splash.get());
        splash.reset();
    }

    if (const int i = args.indexOf ("--editor"); i >= 0)
        openEditor (juce::jlimit (0, engine.getNumChannels() - 1, args[i + 1].getIntValue()));

    if (args.contains ("--synth"))
        openSynthPanel (engine.getSynthChannel());

    if (const int i = args.indexOf ("--synth-tab"); i >= 0)   // test: pagina, sezione (e vista per i modulatori)
    {
        openSynthPanel (engine.getSynthChannel());
        synthPanel->showTab (args[i + 1].getIntValue(), args[i + 2].getIntValue(), args[i + 3].getIntValue());
    }

    if (args.contains ("--synth-reopen"))   // test: apri, chiudi, riapri
    {
        openSynthPanel (engine.getSynthChannel());
        synthPanel->onClose();
        openSynthPanel (engine.getSynthChannel());
        synthPanel->onClose();
        openSynthPanel (engine.getSynthChannel());
    }


    // test: aggiunge canali con suoni della libreria ("--add piano,strings")
    if (const int i = args.indexOf ("--add"); i >= 0)
    {
        for (auto& id : juce::StringArray::fromTokens (args[i + 1], ",", {}))
        {
            SoundChoice c;
            c.kind = SoundChoice::library;
            c.id = id;
            if (auto* entry = SampleStore::findLibraryEntry (id))
            {
                c.name = juce::String (juce::CharPointer_UTF8 (entry->name));
                c.colour = entry->colour;
                applySoundChoice (-1, c);
            }
        }
        rebuildRows();
    }

    if (const int i = args.indexOf ("--browser"); i >= 0)       // test: browser dei suoni (canale, scheda, categoria)
    {
        openBrowser (args[i + 1].getIntValue());
        if (args.size() > i + 3)
            browser.showTabForTest (args[i + 2].getIntValue(), args[i + 3].getIntValue());
    }

    if (const int i = args.indexOf ("--add-instrument"); i >= 0)   // test: aggiunge uno strumento
        if (auto* info = Instruments::find (args[i + 1]))
        {
            SoundChoice c;
            c.kind = SoundChoice::instrument;
            c.id = info->id;
            c.name = info->name;
            c.colour = info->colour;
            applySoundChoice (-1, c);
        }

    if (args.contains ("--mixer-demo"))   // test: qualche effetto nel mixer
    {
        engine.setTrackEffect (1, 0, mixer::compressor);
        engine.setTrackEffect (1, 1, mixer::saturation);
        engine.setTrackEffect (2, 0, mixer::reverb);
        engine.setTrackEffect (4, 0, mixer::filter);
        engine.setTrackEffect (4, 1, mixer::delay);
        engine.setTrackEffect (7, 0, mixer::sidechain);
        engine.setTrackEffect (7, 1, mixer::chorus);
        engine.getTrack (7).eqLow.store (4.0f);
        engine.getTrack (3).volume.store (0.6f);
        engine.getTrack (5).muted.store (true);
        engine.getTrack (5).pan.store (-0.4f);
        engine.getTrack (0).slots[0].type.load();
        engine.setTrackEffect (0, 0, mixer::compressor);
    }

    if (const int i = args.indexOf ("--mixer"); i >= 0)   // test: mixer (traccia degli effetti, -1 = strisce)
        openMixer (args.size() > i + 1 ? args[i + 1].getIntValue() : -1);

    if (const int i = args.indexOf ("--add-kit"); i >= 0)   // test: kit di DrumForge (indice, 0 = DrumForge / 1 = canali separati)
    {
        SoundChoice c;
        c.kind = args[i + 2].getIntValue() == 1 ? SoundChoice::kitChannels : SoundChoice::drumForgeKit;
        c.kit = args[i + 1].getIntValue();
        c.name = "Kit " + df::Presets::factoryName (c.kit);
        c.colour = 0xffff6b5b;
        applySoundChoice (-1, c);
    }

    if (const int i = args.indexOf ("--roll"); i >= 0)
        openPianoRoll (juce::jlimit (0, engine.getNumChannels() - 1, args[i + 1].getIntValue()));

    if (const int i = args.indexOf ("--scale"); i >= 0 && args.size() > i + 2)   // test: scala del progetto
        engine.setScale (args[i + 1].getIntValue(), args[i + 2].getIntValue());
    if (args.contains ("--scale-panel") && pianoRoll.isVisible())
        pianoRoll.showScalePanelForTest();
    if (args.contains ("--prog-panel") && pianoRoll.isVisible())
        pianoRoll.showProgressionPanelForTest();

    if (const int i = args.indexOf ("--open-synth"); i >= 0)       // test: pannello di uno strumento (canale, scheda)
    {
        openSynthPanel (args[i + 1].getIntValue());
        if (args.size() > i + 2)
            if (auto* pfEditor = dynamic_cast<ForgeEditorBase*> (synthPanel->getEditorForTest()))
                pfEditor->showTab (args[i + 2].getIntValue());
    }

    if (args.contains ("--arp-panel") && synthPanel != nullptr && synthPanel->isVisible())   // test: arpeggiatore
        synthPanel->showArpPanelForTest();

    if (const int i = args.indexOf ("--demo"); i >= 0)   // test: progetto demo (indice)
        loadDemo (args[i + 1].getIntValue());
    if (const int i = args.indexOf ("--guide"); i >= 0)  // test: guida rapida (pagina)
    {
        showGuide();
        if (guide != nullptr && args.size() > i + 1)
            guide->show (args[i + 1].getIntValue());
    }

    if (const int i = args.indexOf ("--presets"); i >= 0)   // test: browser dei preset (canale)
        showPresetMenu (args[i + 1].getIntValue(), *this);

    if (const int i = args.indexOf ("--pad-picker"); i >= 0 && synthPanel != nullptr)   // test: suono di un pad di DrumForge
        if (auto* drums = dynamic_cast<DrumForgeEditor*> (synthPanel->getEditorForTest()))
            drums->openSamplePickerForTest (args[i + 1].getIntValue());

    if (const int i = args.indexOf ("--pad"); i >= 0 && synthPanel != nullptr)   // test: pad selezionato in DrumForge
        if (auto* drums = dynamic_cast<DrumForgeEditor*> (synthPanel->getEditorForTest()))
            drums->selectPad (args[i + 1].getIntValue());

    if (const int i = args.indexOf ("--fill-row"); i >= 0 && args.size() > i + 2)   // test: riempimento di una riga (canale, step)
        if (auto* row = rows[args[i + 1].getIntValue()])
            row->showFillPanelForTest (args[i + 2].getIntValue());

    if (const int i = args.indexOf ("--rowmenu"); i >= 0)       // test: menu del canale
        if (auto* row = rows[args[i + 1].getIntValue()])
            row->showRowMenu();

    if (args.contains ("--demo-song"))   // test: una canzone di prova nella playlist
    {
        engine.selectPattern (1); engine.copyPattern (0, 1);
        for (int s = 0; s < 16; s += 2) engine.setStepVelocity (3, s, 0);
        engine.selectPattern (0);
        engine.setClips ({ { 0, 0, 0 }, { 0, 1, 0 }, { 0, 2, 1 }, { 0, 3, 1 }, { 1, 2, 2 }, { 1, 3, 2 }, { 2, 4, 3 }, { 0, 4, 0 }, { 0, 5, 1 } });
        engine.setSongMode (true);
        openPlaylist();
        if (args.contains ("--play")) engine.setPlaying (true);
    }

    if (args.contains ("--automation-demo"))   // test: due automazioni d'esempio
    {
        const int l1 = engine.addLane ({ automation::channelParam, 3, automation::chFilter, 0 });
        engine.setLanePoints (l1, { { 0, 0.15f }, { 4 * 192, 1.0f }, { 5 * 192, 1.0f }, { 6 * 192, 0.3f } });
        const int l2 = engine.addLane ({ automation::master, 0, 0, 0 });
        engine.setLanePoints (l2, { { 0, 0.0f }, { 192, 0.8f }, { 8 * 192, 0.8f } });
    }

    if (args.contains ("--playlist"))
    {
        engine.setSongMode (true);
        openPlaylist();
        if (args.contains ("--automation"))
            playlist.showAutomationForTest();
        if (args.contains ("--automation-picker"))
            playlist.showTargetPickerForTest();
    }

    if (args.contains ("--export"))
    {
        openExport();
        if (args.contains ("--master-auto"))
            exportPanel.startAnalysisForTest();
    }

    if (const int i = args.indexOf ("--scroll"); i >= 0)
        rackView.setViewPosition (0, args[i + 1].getIntValue());

    if (const int i = args.indexOf ("--mode"); i >= 0)
    {
        modeSwitch.setSelected (juce::jlimit (0, 3, args[i + 1].getIntValue()));
        updateViews();
    }
}

//==============================================================================
void MainComponent::loadSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.filenameSuffix = ".xml";
    options.folderName = "DAWFL";   // cartella dati storica: non cambia, cosi' progetti e impostazioni restano
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (ProjectIO::getRootFolder().getChildFile ("settings.xml"), options);

    Haptics::enabled() = settings->getBoolValue ("haptics", true);
    perf::lowPowerFlag().store (settings->getBoolValue ("lowPower", false));
    recordQuantize = settings->getIntValue ("recordQuantize", 12);
    cpuBadge.setLowPower (perf::lowPower());

    if (const int buffer = settings->getIntValue ("bufferSize", 0); buffer > 0)
        setBufferSize (buffer);
}

void MainComponent::setBufferSize (int samples)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = samples;

    if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty())
    {
        settings->setValue ("bufferSize", samples);
        settings->saveIfNeeded();
    }
}

void MainComponent::setLiveTarget (int channel)
{
    if (channel >= 0 && channel < engine.getNumChannels() && liveTarget.exchange (channel) != channel && engine.isRecording())
        repaint (logoArea.toNearestInt());
}

void MainComponent::toggleRecording()
{
    Haptics::tick (this);
    if (engine.isRecording())
    {
        engine.setRecording (false);
        recordButton.setToggleState (false, juce::dontSendNotification);
        placeRecordedNotes();
        repaint (logoArea.toNearestInt());
        return;
    }

    // La registrazione va nel pattern: in modalita' SONG si passa a PAT
    if (engine.isSongMode())
    {
        engine.setSongMode (false);
        songSwitch.setSelected (0);
        playlist.setVisible (false);
    }

    if (liveTarget.load() < 0 || liveTarget.load() >= engine.getNumChannels())
        liveTarget.store (juce::jmax (0, engine.getSynthChannel()));

    engine.setRecording (true);
    recordButton.setToggleState (true, juce::dontSendNotification);
    if (! engine.isPlaying())
        engine.setPlaying (true);
    repaint (logoArea.toNearestInt());

    if (! recordHintShown)
    {
        recordHintShown = true;
        showMessage ("Registrazione",
                     "Suona con la tastiera del synth, i pad di DrumForge o una tastiera MIDI collegata: le note entrano nel pattern "
                     "del canale scelto (in alto, sotto il logo). Per cambiare canale tocca il suo nome.\n"
                     "Le note vengono allineate alla griglia (1/16); si cambia da MENU > Impostazioni.");
    }
}

void MainComponent::placeRecordedNotes()
{
    const auto notes = engine.popRecordedNotes();
    if (notes.empty())
        return;

    const int grid = recordQuantize;   // tick: 0 = libero
    for (const auto& r : notes)
    {
        if (r.channel < 0 || r.channel >= engine.getNumChannels())
            continue;
        auto& pat = engine.getPattern (r.pattern);
        const int len = pat.length.load() * SequencerEngine::ticksPerStep;
        int start = r.start, length = r.length;
        if (grid > 0)
        {
            start = (int) std::round ((double) start / grid) * grid % len;
            length = juce::jmax (grid / 2, (int) std::round ((double) length / grid) * grid);
        }

        // Batteria e campioni sulla loro nota naturale, in un canale senza note: diventano step
        const bool drumLike = ! engine.isSynth (r.channel) || dynamic_cast<DrumForgeAudioProcessor*> (engine.getInstrument (r.channel)) != nullptr;
        if (! engine.isSynth (r.channel) && r.pitch == engine.getChannelRootNote (r.channel) && pat.noteCount[(size_t) r.channel].load() == 0)
        {
            const int step = (int) std::round ((double) start / SequencerEngine::ticksPerStep) % pat.length.load();
            pat.velocity[(size_t) r.channel][(size_t) step].store ((uint8_t) r.velocity);
            continue;
        }

        auto list = engine.getNotes (r.pattern, r.channel);
        if (drumLike)
            length = juce::jmax (length, SequencerEngine::ticksPerStep);
        // Un colpo nello stesso punto e sulla stessa nota sostituisce quello vecchio
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const SequencerEngine::NoteData& n) { return n.start == start && n.pitch == r.pitch; }), list.end());
        if ((int) list.size() < SequencerEngine::maxNotes)
            list.push_back ({ start, juce::jlimit (1, 511, length), r.pitch, r.velocity });
        engine.setNotes (r.pattern, r.channel, list);
    }

    for (auto* row : rows)
        row->repaint();
    if (pianoRoll.isVisible())
        pianoRoll.repaint();
    userTouched.store (true);   // un passo di undo
}

void MainComponent::scanMidiInputs()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& d : devices)
        if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
        {
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
            if (! midiDeviceNames.contains (d.name))
                midiDeviceNames.add (d.name);
        }
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Thread MIDI: solo note, verso il canale scelto
    const int target = liveTarget.load();
    if (target < 0)
        return;
    if (m.isNoteOn())
        engine.liveNote (target, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.liveNote (target, m.getNoteNumber(), 0.0f);
    midiActivity.store (true);
}

void MainComponent::updateCpuBadge()
{
    const float load = (float) deviceManager.getCpuUsage();
    const int xruns = deviceManager.getXRunCount();
    const auto now = juce::Time::getMillisecondCounter();
    if (xruns > lastXRuns)
    {
        lastXRuns = xruns;
        lastXRunTime = now;
        ++glitchesThisSession;
    }
    const bool recentGlitch = now - lastXRunTime < 3000 && lastXRunTime != 0;
    cpuBadge.setState (load, recentGlitch);

    // Al limite per un po' mentre suona: un consiglio, una volta sola
    highLoadTicks = (engine.isPlaying() && (load > 0.9f || recentGlitch)) ? highLoadTicks + 1 : 0;
    if (highLoadTicks > 8 && ! performanceHintShown && ! perf::lowPower())
    {
        performanceHintShown = true;
        showMessage ("Il telefono e' al limite",
                     "L'audio potrebbe scattare. Tocca l'indicatore CPU in alto: puoi attivare \"Risparmio\" "
                     "(meno voci nei synth) o aumentare il buffer audio.");
    }
}

void MainComponent::showPerformanceMenu()
{
    juce::PopupMenu menu = createSettingsMenu();
    juce::PopupMenu top;
    top.addSectionHeader ("Processore per l'audio: " + juce::String (juce::roundToInt (deviceManager.getCpuUsage() * 100.0)) + "%"
                          + "   scatti: " + juce::String (glitchesThisSession));
    top.addItem (5100, "Risparmio (meno voci nei synth)", true, perf::lowPower());
    top.addItem (5101, "Ferma tutti i suoni (panico)");
    top.addSeparator();
    top.addSubMenu ("Impostazioni", menu);

    juce::Component::SafePointer<MainComponent> safeThis (this);
    top.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cpuBadge), [safeThis] (int result)
    {
        if (safeThis != nullptr && result > 0)
            safeThis->handleSettingsResult (result);
    });
}

void MainComponent::handleSettingsResult (int result)
{
    if (result == 5000)
    {
        Haptics::enabled() = ! Haptics::enabled();
        settings->setValue ("haptics", Haptics::enabled());
        settings->saveIfNeeded();
    }
    else if (result == 5100)
    {
        perf::lowPowerFlag().store (! perf::lowPower());
        settings->setValue ("lowPower", perf::lowPower());
        settings->saveIfNeeded();
        cpuBadge.setLowPower (perf::lowPower());
    }
    else if (result == 5101)
    {
        engine.panic();
        updateViews();
    }
    else if (result >= 5200 && result <= 5203)
    {
        static const int grids[] = { 0, 6, 12, 24 };
        recordQuantize = grids[result - 5200];
        settings->setValue ("recordQuantize", recordQuantize);
        settings->saveIfNeeded();
    }
    else if (result == 5300)
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::bluetoothMidi, [] (bool granted)
        {
            if (granted)
                juce::BluetoothMidiDevicePairingDialogue::open();
        });
    }
    else if (result > 5000 && result < 6000)
        setBufferSize (result - 5000);
}

bool MainComponent::handleBackButton()
{
    // Chiude il pannello in primo piano, come il tasto Indietro di ogni app Android
    if (splash != nullptr)
        return true;
    if (guide != nullptr) { closeGuide (-1); return true; }
    if (presetBrowser.isVisible()) { presetBrowser.setVisible (false); return true; }
    if (mixerPanel.isVisible())
    {
        if (mixerPanel.getEffectsTrack() >= 0) mixerPanel.showEffects (-1);
        else mixerPanel.setVisible (false);
        return true;
    }
    if (synthPanel != nullptr && synthPanel->isVisible())  { synthPanel->onClose(); return true; }
    if (browser.isVisible())     { browser.setVisible (false); return true; }
    if (exportPanel.isVisible()) { exportPanel.setVisible (false); return true; }
    if (pianoRoll.isVisible())   { pianoRoll.setVisible (false); return true; }
    if (editor.isVisible())      { editor.setVisible (false); return true; }
    if (playlist.isVisible())
    {
        playlist.setVisible (false);
        songSwitch.setSelected (engine.isSongMode() ? 1 : 0);
        return true;
    }
    saveSession();
    return false;   // niente da chiudere: l'app va in secondo piano
}

juce::PopupMenu MainComponent::createSettingsMenu()
{
    juce::PopupMenu menu, buffers;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const int current = device->getCurrentBufferSizeSamples();
        const double sr = device->getCurrentSampleRate();

        // Solo dimensioni sensate: da ~1 ms a ~50 ms
        for (int size : device->getAvailableBufferSizes())
            if (size >= 32 && size <= 2048)
                buffers.addItem (5000 + size, juce::String (size) + " campioni  (" + juce::String (1000.0 * size / sr, 1) + " ms)",
                                 true, size == current);
    }

    menu.addItem (5000, "Vibrazione al tocco", true, Haptics::enabled());
    menu.addItem (5100, "Risparmio (meno voci nei synth)", true, perf::lowPower());

    juce::PopupMenu quant;
    quant.addItem (5200, "Libera (come la suoni)", true, recordQuantize == 0);
    quant.addItem (5201, "1/32", true, recordQuantize == 6);
    quant.addItem (5202, "1/16", true, recordQuantize == 12);
    quant.addItem (5203, "1/8", true, recordQuantize == 24);
    menu.addSubMenu ("Registrazione: allinea le note", quant);

    juce::PopupMenu midi;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        midi.addItem (-1, d.name, false, true);
    if (midi.getNumItems() == 0)
        midi.addItem (-1, "(nessuna tastiera MIDI collegata)", false);
    midi.addSeparator();
    midi.addItem (5300, "Collega una tastiera Bluetooth...");
    menu.addSubMenu ("Tastiere MIDI", midi);
    menu.addSubMenu ("Buffer audio (latenza)", buffers, buffers.getNumItems() > 0);
    return menu;
}

//==============================================================================
// Libreria suoni e campioni

juce::PopupMenu MainComponent::buildSoundMenu (int channel)
{
    // Preset dello strumento del canale (gli altri canali scelgono il suono dal browser)
    auto* proc = engine.getInstrument (channel);
    juce::PopupMenu menu;
    menu.addItem (650, "Apri " + engine.getChannel (channel).name + "...");
    menu.addSectionHeader ("Preset di fabbrica");
    const auto current = Instruments::presetName (proc);
    const auto factory = Instruments::factoryPresets (proc);
    for (int i = 0; i < juce::jmin (factory.size(), 50); ++i)
        menu.addItem (600 + i, factory[i], true, factory[i] == current);

    const auto user = Instruments::userPresets (proc);
    if (! user.isEmpty())
    {
        menu.addSectionHeader ("Preset utente");
        for (int i = 0; i < juce::jmin (user.size(), 200); ++i)
            menu.addItem (700 + i, user[i].getFileNameWithoutExtension(), true, user[i].getFileNameWithoutExtension() == current);
    }
    menu.addSeparator();
    menu.addItem (660, "Salva come preset utente...");
    return menu;
}

void MainComponent::handleSoundMenu (int channel, int result)
{
    auto* proc = engine.getInstrument (channel);
    if (proc == nullptr)
        return;

    if (result == 650)
        openSynthPanel (channel);
    else if (result == 660)
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);
        askText ("Salva preset", Instruments::presetName (proc), [safeThis, channel] (juce::String name)
        {
            if (safeThis == nullptr)
                return;
            if (! Instruments::saveUser (safeThis->engine.getInstrument (channel), name))
                safeThis->showMessage ("Preset", "Non riesco a salvare il preset.");
            safeThis->synthPanel->refreshPresetName();
            if (safeThis->presetBrowser.isVisible())
                safeThis->presetBrowser.rebuild();
        });
        return;
    }
    else if (result >= 600 && result < 650)
        Instruments::loadFactory (proc, result - 600);
    else if (result >= 700)
    {
        const auto user = Instruments::userPresets (proc);
        if (result - 700 < user.size())
            Instruments::loadUser (proc, user[result - 700]);
    }

    if (result != 650)
        engine.previewChannel (channel);

    synthPanel->refreshPresetName();
    if (auto* row = rows[channel])
        row->repaint();
    if (editor.isVisible())
        editor.refreshFromEngine();
}

void MainComponent::showPresetMenu (int channel, juce::Component&)
{
    if (engine.getInstrument (channel) == nullptr)
        return;

    instruments->resolve();
    presetBrowser.setBounds (getContentArea());
    presetBrowser.openFor (channel);
    presetBrowser.setVisible (true);
    presetBrowser.toFront (true);
}

void MainComponent::assignSample (int channel, const juce::String& fileName)
{
    if (auto* data = sampleStore.load (fileName))
    {
        auto& ch = engine.getChannel (channel);
        ch.sampleName = fileName;
        ch.sample.store (data);
        engine.previewChannel (channel);

        if (editor.isVisible())
            editor.refreshFromEngine();
    }
    else
    {
        showMessage ("Campione", "Non riesco ad aprire \"" + SampleStore::displayName (fileName) + "\".");
    }
}

void MainComponent::importSample (int channel)   // channel = -1: crea un canale nuovo col campione
{
    chooser = std::make_unique<juce::FileChooser> ("Scegli un campione audio", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    juce::Component::SafePointer<MainComponent> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, channel] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;

                              const auto url = fc.getURLResult();
                              if (url.isEmpty())
                                  return;

                              juce::String error;
                              const auto name = safeThis->sampleStore.importFromUrl (url, error);

                              if (name.isEmpty())
                              {
                                  safeThis->showMessage ("Campione", error);
                                  return;
                              }

                              SoundChoice c;
                              c.kind = SoundChoice::userSample;
                              c.id = name;
                              c.name = SampleStore::displayName (name);
                              c.colour = 0xffc49360;
                              safeThis->applySoundChoice (channel, c);
                          });
}

void MainComponent::resolveSamples()
{
    // Dopo un caricamento i nomi dei campioni ci sono, i puntatori no: li ricolleghiamo
    juce::StringArray missing;

    for (int c = 0; c < engine.getNumChannels(); ++c)
    {
        auto& ch = engine.getChannel (c);
        if (ch.sampleName.isEmpty() || engine.isSynth (c))
        {
            ch.sample.store (nullptr);
            continue;
        }

        auto* data = sampleStore.load (ch.sampleName);
        ch.sample.store (data);
        if (data == nullptr)
            missing.add (SampleStore::displayName (ch.sampleName));
    }

    if (! missing.isEmpty())
        showMessage ("Campioni mancanti", "Questi campioni non si trovano piu': " + missing.joinIntoString (", ")
                                            + ". Quei canali useranno la sintesi.");
}

//==============================================================================
// Canali: righe, menu del canale, browser dei suoni

void MainComponent::rebuildRows()
{
    instruments->resolve();      // strumenti per i canali nuovi, clonati o caricati
    shownStructure = engine.getStructureVersion();
    rows.clear();

    for (int c = 0; c < engine.getNumChannels(); ++c)
    {
        auto* row = rows.add (new ChannelRow (engine, c));
        row->onOpenEditor = [this] (int ch)
        {
            Haptics::tick (this);
            setLiveTarget (ch);
            if (engine.isSynth (ch)) openSynthPanel (ch); else openEditor (ch);
        };
        row->onTap = [this, c] { Haptics::tick (this); setLiveTarget (c); };
        row->onLongPress = [this] { Haptics::tick (this); };
        row->onOpenPianoRoll = [this] (int ch) { Haptics::tick (this); setLiveTarget (ch); openPianoRoll (ch); };
        row->onCommand = [this] (int ch, int command) { handleChannelCommand (ch, command); };
        row->onScrollDrag = [this] (float delta, bool started)
        {
            if (started)
                scrollStartY = rackView.getViewPositionY();
            else
                rackView.setViewPosition (0, scrollStartY - juce::roundToInt (delta));
        };
        rackContent.addAndMakeVisible (row);
    }

    addButton.setEnabled (engine.getNumChannels() < SequencerEngine::maxChannels);

    if (! getLocalBounds().isEmpty())
        resized();
    for (auto* row : rows)
        row->repaint();
}

void MainComponent::hideOverlays()
{
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    browser.setVisible (false);
}

void MainComponent::openPlaylist()
{
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    browser.setVisible (false);
    exportPanel.setVisible (false);
    mixerPanel.setVisible (false);
    playlist.setBrush (engine.getCurrentPattern());
    playlist.setVisible (true);
    playlist.toFront (false);
}

void MainComponent::openMixer (int track)
{
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    browser.setVisible (false);
    playlist.setVisible (false);
    exportPanel.setVisible (false);
    mixerPanel.open (track);
    mixerPanel.setVisible (true);
    mixerPanel.toFront (false);
}

void MainComponent::openExport()
{
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    browser.setVisible (false);
    playlist.setVisible (false);
    mixerPanel.setVisible (false);
    exportPanel.open();
    exportPanel.setVisible (true);
    exportPanel.toFront (false);
}

void MainComponent::scrollToChannel (int channel)
{
    if (auto* row = rows[channel])
    {
        const int top = rackView.getViewPositionY(), h = rackView.getHeight();
        if (row->getY() < top)
            rackView.setViewPosition (0, row->getY());
        else if (row->getBottom() > top + h)
            rackView.setViewPosition (0, juce::jmin (row->getY(), rackContent.getHeight() - h));
    }
}

void MainComponent::handleChannelCommand (int channel, int command)
{
    if (channel < 0 || channel >= engine.getNumChannels())
        return;

    auto& ch = engine.getChannel (channel);
    juce::Component::SafePointer<MainComponent> safeThis (this);

    // Le modifiche alla struttura ricreano le righe: si fanno appena finito il callback del menu
    auto later = [safeThis] (std::function<void (MainComponent&)> fn)
    {
        juce::MessageManager::callAsync ([safeThis, fn]
        {
            if (safeThis != nullptr)
                fn (*safeThis);
        });
    };

    switch (command)
    {
        case ChannelRow::changeSound:
            if (engine.isSynth (channel))
            {
                if (auto* row = rows[channel])
                    showPresetMenu (channel, *row);
            }
            else
            {
                openBrowser (channel);
            }
            break;

        case ChannelRow::editSound:
            if (engine.isSynth (channel)) openSynthPanel (channel); else openEditor (channel);
            break;

        case ChannelRow::openPianoRoll:
            openPianoRoll (channel);
            break;

        case ChannelRow::openMixer:
            openMixer (engine.getChannelTrack (channel));
            break;

        case ChannelRow::rename:
            askText ("Rinomina canale", ch.name, [safeThis, channel] (juce::String name)
            {
                if (safeThis == nullptr || channel >= safeThis->engine.getNumChannels())
                    return;
                auto& c = safeThis->engine.getChannel (channel);
                c.name = name.substring (0, 32);
                c.customName = true;
                if (auto* row = safeThis->rows[channel])
                    row->repaint();
            });
            break;

        case ChannelRow::solo:
            ch.solo.store (! ch.solo.load());
            break;

        case ChannelRow::clone:
            later ([channel] (MainComponent& m)
            {
                const int copy = m.engine.duplicateChannel (channel);
                m.hideOverlays();
                m.rebuildRows();
                if (copy >= 0)
                {
                    m.scrollToChannel (copy);
                    m.engine.previewChannel (copy);
                }
            });
            break;

        case ChannelRow::moveUp:
        case ChannelRow::moveDown:
            later ([channel, command] (MainComponent& m)
            {
                const int to = channel + (command == ChannelRow::moveUp ? -1 : 1);
                m.engine.moveChannel (channel, to);
                m.hideOverlays();
                m.rebuildRows();
                m.scrollToChannel (to);
            });
            break;

        case ChannelRow::remove:
            later ([channel] (MainComponent& m)
            {
                m.engine.removeChannel (channel);
                m.hideOverlays();
                m.rebuildRows();
            });
            break;

        default:
            break;
    }

    if (auto* row = rows[channel])
        row->repaint();
}

void MainComponent::openBrowser (int channel)
{
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    playlist.setVisible (false);
    exportPanel.setVisible (false);
    mixerPanel.setVisible (false);
    browser.openFor (channel);
    browser.setVisible (true);
    browser.toFront (false);
}

void MainComponent::applySoundChoice (int channel, const SoundChoice& choice)
{
    if (choice.kind == SoundChoice::none)
        return;

    if (choice.kind == SoundChoice::importFile)
    {
        importSample (channel);
        return;
    }

    if (choice.kind == SoundChoice::kitChannels)
    {
        addKitChannels (choice.kit);
        return;
    }

    const bool isNew = channel < 0;
    if (isNew)
    {
        channel = engine.addChannel();
        if (channel < 0)
        {
            showMessage ("Canali", "Hai raggiunto il massimo di " + juce::String (SequencerEngine::maxChannels) + " canali.");
            return;
        }
    }

    auto& ch = engine.getChannel (channel);

    if (choice.kind == SoundChoice::instrument)
    {
        // Nome con un numero se c'e' gia' lo stesso strumento ("PolyForge 2")
        auto name = choice.name;
        int count = 0;
        for (int c = 0; c < engine.getNumChannels(); ++c)
            if (c != channel && engine.isSynth (c) && engine.getChannel (c).instrumentKind == choice.id)
                ++count;
        if (count > 0)
            name << " " << (count + 1);

        engine.makeInstrumentChannel (channel, choice.id, name, choice.colour);
        instruments->resolve();          // crea lo strumento
        if (! ch.customName)
            ch.name = name;
    }
    else if (choice.kind == SoundChoice::drumForgeKit)
    {
        engine.makeInstrumentChannel (channel, "drumforge", choice.name, choice.colour);
        instruments->resolve();
        Instruments::loadFactory (engine.getInstrument (channel), choice.kit);
        if (! ch.customName)
            ch.name = choice.name;
        if (synthPanel != nullptr && synthPanel->isVisible() && synthPanel->getChannel() == channel)
            synthPanel->open (channel);
    }
    else if (choice.kind == SoundChoice::drumSynth)
    {
        engine.makeDrumChannel (channel, choice.drumType);
        engine.setChannelKit (channel, choice.kit);
    }
    else
    {
        const auto file = choice.kind == SoundChoice::library ? SampleStore::libraryName (choice.id) : choice.id;
        auto* data = sampleStore.load (file);
        if (data == nullptr)
        {
            if (isNew) engine.removeChannel (channel);
            showMessage ("Suono", "Non riesco ad aprire \"" + choice.name + "\".");
            return;
        }

        ch.type.store (SequencerEngine::drumOrSample);
        ch.sampleName = file;
        ch.sample.store (data);
        ch.colour.store (choice.colour);
        if (! ch.customName)
            ch.name = choice.name;
        ch.touchSound();
    }

    browser.setVisible (false);
    rebuildRows();
    scrollToChannel (channel);
    engine.previewChannel (channel);

    if (editor.isVisible())
        editor.refreshFromEngine();
}

void MainComponent::addKitChannels (int kitIndex)
{
    const auto& kit = df::factoryKits()[(size_t) juce::jlimit (0, df::Presets::numFactory() - 1, kitIndex)];
    int first = -1;

    for (int pad : SoundBrowser::kitChannelPads)
    {
        const auto& def = kit.pads[(size_t) pad];
        auto* data = sampleStore.load (SampleStore::libraryName (def.sample));
        if (data == nullptr)
            continue;

        const int channel = engine.addChannel();
        if (channel < 0)
        {
            showMessage ("Canali", "Hai raggiunto il massimo di " + juce::String (SequencerEngine::maxChannels) + " canali.");
            break;
        }

        auto& ch = engine.getChannel (channel);
        ch.type.store (SequencerEngine::drumOrSample);
        ch.sampleName = SampleStore::libraryName (def.sample);
        ch.sample.store (data);
        if (auto* entry = SampleStore::findLibraryEntry (def.sample))
            ch.colour.store (entry->colour);
        ch.name = juce::String (juce::CharPointer_UTF8 (def.label));
        ch.customName = false;
        ch.pitch.store (def.tune);
        ch.pan.store (def.pan);
        ch.volume.store (0.8f * juce::Decibels::decibelsToGain (def.level));
        ch.touchSound();
        if (first < 0)
            first = channel;
    }

    browser.setVisible (false);
    rebuildRows();
    if (first >= 0)
    {
        scrollToChannel (first);
        engine.previewChannel (first);
    }
}

void MainComponent::askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onText)
{
    auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("text", initial);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onText] (int result)
    {
        const auto text = window->getTextEditorContents ("text").trim();
        if (result == 1 && text.isNotEmpty() && onText)
            onText (text);
    }), true);
}
