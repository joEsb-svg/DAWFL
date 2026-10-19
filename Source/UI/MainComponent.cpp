#include "MainComponent.h"
#include "Theme.h"
#include "../Engine/ProjectIO.h"

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
        engine.selectPattern ((engine.getCurrentPattern() + d + SequencerEngine::numPatterns) % SequencerEngine::numPatterns);
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

    // --- Channel rack
    for (int c = 0; c < SequencerEngine::numChannels; ++c)
    {
        auto* row = rows.add (new ChannelRow (engine, c));
        row->onOpenEditor = [this] (int ch) { Haptics::tick (this); openEditor (ch); };
        row->onTap = [this] { Haptics::tick (this); };
        row->onOpenPianoRoll = [this] (int ch) { Haptics::tick (this); openPianoRoll (ch); };
        row->buildSoundMenu  = [this] (int ch) { return buildSoundMenu (ch); };
        row->handleSoundMenu = [this] (int ch, int result) { handleSoundMenu (ch, result); };
        addAndMakeVisible (row);
    }

    editor.onClose = [this] { editor.setVisible (false); };
    pianoRoll.onClose = [this] { pianoRoll.setVisible (false); };
    pianoRoll.getGrid().onEdit = [this] { Haptics::tick (this); };
    addChildComponent (pianoRoll);
    editor.buildSoundMenu  = [this] (int ch) { return buildSoundMenu (ch); };
    editor.handleSoundMenu = [this] (int ch, int result) { handleSoundMenu (ch, result); };
    addChildComponent (editor);

    ruler.getColumnBounds = [this] (int i)
    {
        auto* row = rows.getFirst();
        return row->getPadBounds (i).translated ((float) (row->getX() - ruler.getX()), 0.0f);
    };
    addAndMakeVisible (ruler);

    // --- Ripristina l'ultima sessione
    projectName = untitled;
    if (ProjectIO::loadFromFile (engine, ProjectIO::getAutosaveFile()))
    {
        const auto json = juce::JSON::parse (ProjectIO::getAutosaveFile().loadFileAsString());
        projectName = json.getProperty ("projectName", untitled).toString();
    }
    resolveSamples();
    syncControlsFromEngine();
    lastSnapshot = takeSnapshot();

    // --- Audio: 0 ingressi, 2 uscite (stereo)
    audioError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engine);
    loadSettings();

    startTimerHz (40);
    setSize (900, 440);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSession();
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
        logo.append ("DAW", Theme::font (top.getHeight() * 0.78f, true), Theme::textBright);
        logo.append ("FL",  Theme::font (top.getHeight() * 0.78f, true), Theme::accent);
        logo.setJustification (juce::Justification::bottomLeft);
        logo.draw (g, top);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (11.5f));
        g.drawFittedText (projectName, r.toNearestInt(), juce::Justification::topLeft, 1);
    }

    Theme::drawCaption (g, "Swing",  swingCaption);
    Theme::drawCaption (g, "Kit",    kitCaption);
    Theme::drawCaption (g, "Master", masterCaption);
}

void MainComponent::resized()
{
    auto full = getContentArea();
    portraitLayout = full.getWidth() < full.getHeight();
    patternDisplay.caption = portraitLayout ? "Pat" : "Pattern";

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

    logoArea = header.removeFromLeft (88).toFloat();
    header.removeFromLeft (4);
    fileButton.setBounds (header.removeFromLeft (50));
    header.removeFromLeft (6);
    undoButton.setBounds (header.removeFromLeft (h));
    header.removeFromLeft (3);
    redoButton.setBounds (header.removeFromLeft (h));
    header.removeFromLeft (10);
    playButton.setBounds (header.removeFromLeft (juce::roundToInt (h * 1.25)));
    header.removeFromLeft (6);
    bpmDisplay.setBounds (header.removeFromLeft (100));
    header.removeFromLeft (6);
    patternDisplay.setBounds (header.removeFromLeft (100));

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

    full.removeFromTop (4);
    auto line2 = full.removeFromTop (lineH).reduced (0, 2);
    const int h2 = line2.getHeight();
    playButton.setBounds (line2.removeFromLeft (h2));
    line2.removeFromLeft (4);
    const int lcdW = juce::jmin (96, (line2.getWidth() - 3 * 40) / 2 - 4);
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
        rack.removeFromTop (4);
        pageSwitch.setBounds (rack.removeFromTop (24).reduced (0, 1));
    }
    else
    {
        pageSwitch.setBounds (bar.removeFromLeft (100 + (getVisibleSteps() < 16 ? 100 : 0)).reduced (0, 1));
        bar.removeFromLeft (8);
        metroButton.setBounds (bar.removeFromLeft (34));
    }

    // Righello allineato agli step, poi le righe
    auto rulerArea = rack.removeFromTop (16);
    rack.removeFromTop (2);

    const int gap = 4;
    const int rowH = juce::jmin (portrait ? 84 : 90,
                                 (rack.getHeight() - gap * (SequencerEngine::numChannels - 1)) / SequencerEngine::numChannels);
    auto rowsArea = rack;

    for (auto* row : rows)
    {
        row->setLayout (portrait ? 8 : 16, portrait);
        row->setBounds (rack.removeFromTop (rowH));
        rack.removeFromTop (gap);
    }

    ruler.setBounds (rows.getFirst()->getX(), rulerArea.getY(), rows.getFirst()->getWidth(), rulerArea.getHeight());
    editor.setBounds (rowsArea.withTop (rulerArea.getY()).expanded (2, 2));
    pianoRoll.setBounds (full);   // il piano roll copre tutto il channel rack, barra compresa
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
    if (editor.isVisible())
        editor.refreshFromEngine();
    masterKnob.setValue (engine.getMasterVolume(), juce::dontSendNotification);
    kitButton.setButtonText (getKitLabel());
    bpmDisplay.repaint();
    updateViews();
}

juce::String MainComponent::getKitLabel() const
{
    // Se tutti gli strumenti usano lo stesso kit ne mostra il nome, altrimenti "Misto".
    const int first = engine.getChannelKit (0);
    for (int c = 1; c < SequencerEngine::numChannels; ++c)
        if (engine.getChannelKit (c) != first)
            return "Misto";

    return SoundLibrary::getKitName (first);
}

//==============================================================================
void MainComponent::timerCallback()
{
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

    const bool playingThis = engine.getPlayingPattern() == engine.getCurrentPattern();
    const int column = (playingThis && step >= firstStep && step < firstStep + visible) ? step - firstStep : -1;
    ruler.setState (column, firstStep, visible);
    meter.setLevel (engine.takeOutputPeak());

    if (playButton.getToggleState() != engine.isPlaying())
        playButton.setToggleState (engine.isPlaying(), juce::dontSendNotification);

    if (const auto label = getKitLabel(); kitButton.getButtonText() != label)
        kitButton.setButtonText (label);

    // Undo: cerca modifiche ~8 volte al secondo
    if (++snapshotCounter >= 5)
    {
        snapshotCounter = 0;
        checkForChanges();
    }

    // Autosalvataggio ogni ~15 secondi (solo se qualcosa e' cambiato)
    if (++autosaveCounter >= 40 * 15)
    {
        autosaveCounter = 0;
        saveSession();
    }
}

//==============================================================================
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
    menu.addItem (1, "Nuovo progetto");
    menu.addSubMenu ("Apri", openMenu);
    menu.addItem (2, "Salva", true);
    menu.addItem (3, "Salva con nome...");
    menu.addSubMenu ("Elimina", deleteMenu, ! projects.isEmpty());
    menu.addSeparator();
    menu.addSubMenu ("Impostazioni", createSettingsMenu());
    menu.addItem (4, "Info audio");
    menu.addSeparator();
    menu.addItem (-1, "DAWFL versione " + juce::String (ProjectInfo::versionString), false);

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
                            else if (result == 5000)
                            {
                                Haptics::enabled() = ! Haptics::enabled();
                                self.settings->setValue ("haptics", Haptics::enabled());
                                self.settings->saveIfNeeded();
                            }
                            else if (result > 5000 && result < 6000)
                                self.setBufferSize (result - 5000);
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
        resolveSamples();
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
    engine.resetProject();
    projectName = untitled;
    pageSwitch.setSelected (0);
    modeSwitch.setSelected (0);
    undoStack.clear(); redoStack.clear(); lastSnapshot = takeSnapshot();
    editor.setVisible (false);
    pianoRoll.setVisible (false);
    syncControlsFromEngine();
    saveSession();
    repaint();
}

void MainComponent::showPatternMenu()
{
    const int cur = engine.getCurrentPattern();
    juce::PopupMenu select, copyTo;

    for (int p = 0; p < SequencerEngine::numPatterns; ++p)
    {
        // Il pallino indica i pattern che contengono note
        const auto label = "Pattern " + juce::String (p + 1)
                         + (engine.isPatternEmpty (p) ? juce::String() : juce::String (juce::CharPointer_UTF8 ("   \xe2\x97\x8f")));
        select.addItem (100 + p, label, true, p == cur);

        if (p != cur)
            copyTo.addItem (200 + p, "Pattern " + juce::String (p + 1));
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Pattern " + juce::String (cur + 1));
    menu.addSubMenu ("Vai a", select);
    menu.addSubMenu ("Copia in", copyTo);
    menu.addItem (1, "Svuota questo pattern", ! engine.isPatternEmpty (cur));

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternDisplay),
                        [safeThis, cur] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            auto& e = safeThis->engine;
                            if (result == 1)               e.clearPattern (cur);
                            else if (result >= 200)        { e.copyPattern (cur, result - 200); e.selectPattern (result - 200); }
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

                            safeThis->engine.setAllKits (result - 1);
                            safeThis->engine.previewChannel (0);
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
void MainComponent::openPianoRoll (int channel)
{
    editor.setVisible (false);
    pianoRoll.setChannel (channel);
    pianoRoll.setVisible (true);
    pianoRoll.toFront (false);
}

void MainComponent::openEditor (int channel)
{
    pianoRoll.setVisible (false);
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
    auto json = ProjectIO::toJson (engine);
    if (auto* obj = json.getDynamicObject())
        obj->removeProperty ("currentPattern");
    return juce::JSON::toString (json, true);
}

void MainComponent::applySnapshot (const juce::String& snapshot)
{
    const int pattern = engine.getCurrentPattern();
    ProjectIO::fromJson (engine, juce::JSON::parse (snapshot));
    resolveSamples();
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

    if (const int i = args.indexOf ("--editor"); i >= 0)
        openEditor (juce::jlimit (0, SequencerEngine::numChannels - 1, args[i + 1].getIntValue()));

    if (const int i = args.indexOf ("--roll"); i >= 0)
        openPianoRoll (juce::jlimit (0, SequencerEngine::numChannels - 1, args[i + 1].getIntValue()));

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
    options.folderName = "DAWFL";
    options.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (ProjectIO::getRootFolder().getChildFile ("settings.xml"), options);

    Haptics::enabled() = settings->getBoolValue ("haptics", true);

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
    menu.addSubMenu ("Buffer audio (latenza)", buffers, buffers.getNumItems() > 0);
    return menu;
}

//==============================================================================
// Libreria suoni e campioni

juce::PopupMenu MainComponent::buildSoundMenu (int channel)
{
    auto& ch = engine.getChannel (channel);
    const bool usingSample = ch.sample.load() != nullptr;
    const int kit = engine.getChannelKit (channel);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Sintesi");
    for (int k = 0; k < SoundLibrary::numKits; ++k)
        menu.addItem (100 + k, SoundLibrary::getKitName (k), true, ! usingSample && k == kit);

    menu.addSectionHeader ("Campioni personali");
    menu.addItem (300, "Importa campione (WAV, AIFF, FLAC, OGG, MP3)...");

    const auto samples = sampleStore.listSamples();
    for (int i = 0; i < juce::jmin (samples.size(), 500); ++i)
        menu.addItem (400 + i, SampleStore::displayName (samples[i]), true, usingSample && samples[i] == ch.sampleName);

    return menu;
}

void MainComponent::handleSoundMenu (int channel, int result)
{
    auto& ch = engine.getChannel (channel);

    if (result >= 100 && result < 100 + SoundLibrary::numKits)
    {
        ch.sample.store (nullptr);
        ch.sampleName.clear();
        engine.setChannelKit (channel, result - 100);
        engine.previewChannel (channel);
    }
    else if (result == 300)
    {
        importSample (channel);
    }
    else if (result >= 400)
    {
        const auto samples = sampleStore.listSamples();
        if (result - 400 < samples.size())
            assignSample (channel, samples[result - 400]);
    }

    if (editor.isVisible())
        editor.refreshFromEngine();
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

void MainComponent::importSample (int channel)
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
                                  safeThis->showMessage ("Campione", error);
                              else
                                  safeThis->assignSample (channel, name);
                          });
}

void MainComponent::resolveSamples()
{
    // Dopo un caricamento i nomi dei campioni ci sono, i puntatori no: li ricolleghiamo
    juce::StringArray missing;

    for (int c = 0; c < SequencerEngine::numChannels; ++c)
    {
        auto& ch = engine.getChannel (c);
        if (ch.sampleName.isEmpty())
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
