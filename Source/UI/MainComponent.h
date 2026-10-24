#pragma once

#include <JuceHeader.h>
#include "../Engine/SequencerEngine.h"
#include "DawLookAndFeel.h"
#include "Widgets.h"
#include "ChannelRow.h"
#include "ChannelEditor.h"
#include "PianoRoll.h"
#include "SynthPanel.h"
#include "SoundBrowser.h"
#include "Playlist.h"
#include "ExportPanel.h"
#include "MixerPanel.h"
#include "PresetBrowser.h"
#include "JLoopSplash.h"
#include "DemoProjects.h"
#include "QuickGuide.h"
#include "UndoHistory.h"
#include "LivePanel.h"
#include "HoloDisplay.h"
#include "Haptics.h"

//==============================================================================
/** La schermata principale: barra del trasporto + channel rack. */
class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Salva lo stato corrente nel file di autosalvataggio (chiamato anche quando l'app va in background). */
    void saveSession();

    /** Tasto Indietro di Android: chiude il pannello aperto. false = niente da chiudere. */
    bool handleBackButton();

    /** Solo per i test sul PC: "--editor N" apre l'editor del canale N, "--mode N" sceglie la modalita'. */
    void handleCommandLine (const juce::String& commandLine);

private:
    void timerCallback() override;
    juce::Rectangle<int> getContentArea() const;

    // Menu e finestre
    void showFileMenu();
    void showPatternMenu();
    void showKitMenu();
    void askProjectName (const juce::String& title, std::function<void (juce::String)> onName);
    void saveProjectAs (const juce::String& name, bool confirmOverwrite);
    void openProject (const juce::String& name);
    void deleteProject (const juce::String& name);
    void newProject();
    void applyDefaultDrums();
    void loadDemo (int index);
    void showGuide();
    void showUndoHistory();
    void openLive();
    void jumpInHistory (int steps);
    void closeGuide (int demo);
    void showAudioInfo();
    void showMessage (const juce::String& title, const juce::String& message);

    void syncControlsFromEngine();
    void openEditor (int channel);
    void openPianoRoll (int channel);
    int getVisibleSteps() const;
    int getFirstVisibleStep() const;
    void layoutLandscape (juce::Rectangle<int> area);
    void layoutPortrait (juce::Rectangle<int> area);
    void layoutRack (juce::Rectangle<int> rack, bool portrait);

    // Campioni dell'utente
    SampleStore sampleStore;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::PopupMenu buildSoundMenu (int channel);
    void handleSoundMenu (int channel, int result);
    void assignSample (int channel, const juce::String& fileName);
    void importSample (int channel);
    void resolveSamples();

    // Canali: aggiungi / clona / sposta / elimina, menu del canale, browser dei suoni
    void rebuildRows();
    void handleChannelCommand (int channel, int command);
    void openBrowser (int channel);
    void addKitChannels (int kitIndex);
    void applySoundChoice (int channel, const SoundChoice& choice);
    void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onText);
    void hideOverlays();
    void openPlaylist();
    void openExport();
    void openMixer (int track = -1);
    void scrollToChannel (int channel);
    int shownStructure = -1;
    int scrollStartY = 0;

    // Impostazioni dell'app (vibrazione, buffer audio), salvate a parte rispetto ai progetti
    std::unique_ptr<juce::PropertiesFile> settings;
    void loadSettings();
    void setBufferSize (int samples);
    juce::PopupMenu createSettingsMenu();
    void handleSettingsResult (int result);
    void showPerformanceMenu();
    void updateCpuBadge();
    void restoreSession();

    // Registrazione dal vivo e tastiere MIDI
    void toggleRecording();
    void placeRecordedNotes();
    void setLiveTarget (int channel);
    void scanMidiInputs();
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    std::atomic<int> liveTarget { -1 };
    std::atomic<bool> midiActivity { false };
    juce::StringArray midiDeviceNames;
    int recordQuantize = 12, midiScanCounter = 0;
    bool recordHintShown = false;
    int cpuCounter = 0, lastXRuns = 0, highLoadTicks = 0, glitchesThisSession = 0;
    juce::uint32 lastXRunTime = 0, dirtySince = 0;
    bool performanceHintShown = false, sessionDirty = false, recoveredFromBackup = false;
    void tapTempo();

    // Undo / redo: istantanee del progetto prese automaticamente dopo ogni modifica
    juce::String takeSnapshot();
    void applySnapshot (const juce::String& snapshot);
    void checkForChanges();
    void undo();
    void redo();
    juce::StringArray undoStack, redoStack;
    juce::StringArray undoLabels, redoLabels;   // il nome di ogni passo (cronologia)
    juce::String lastSnapshot;
    int snapshotCounter = 0;

    /** Segna che l'utente ha toccato lo schermo (qualsiasi pannello o menu): allora si cercano modifiche. */
    struct TouchWatcher : public juce::MouseListener
    {
        explicit TouchWatcher (std::atomic<bool>& f) : flag (f) {}
        void mouseUp (const juce::MouseEvent&) override    { flag.store (true); }
        void mouseDrag (const juce::MouseEvent&) override  { flag.store (true); }
        std::atomic<bool>& flag;
    };
    std::atomic<bool> userTouched { true };
    TouchWatcher touchWatcher { userTouched };
    juce::Array<double> tapTimes;
    void updateViews();
    juce::String getKitLabel() const;

    SequencerEngine engine;
    std::unique_ptr<Instruments::Host> instruments;      // gli strumenti dei canali (distrutti dopo il pannello)
    std::unique_ptr<SynthPanel> synthPanel;
    std::unique_ptr<JLoopSplash> splash;
    std::unique_ptr<QuickGuide> guide;
    LivePanel livePanel { engine };
    juce::TextButton liveButton { "LIVE" };
    void openSynthPanel (int channel);
    void showPresetMenu (int channel, juce::Component& target);
    juce::AudioDeviceManager deviceManager;
    struct PerfLog
    {
        int blocks = 0, overruns = 0, lockMisses = 0, blockSize = 0, uiStalls = 0, maxVoices = 0, startXRuns = 0;
        float loadSum = 0.0f, maxLoad = 0.0f;
        double startMs = 0.0, lastTimerMs = 0.0, worstUiGapMs = 0.0, playingSeconds = 0.0;
    } perfLog;
    juce::String buildPerformanceReport() const;
    void showPerformanceReport();
    void resetPerformanceLog();
    juce::OpenGLContext openGL;          // Android: lo schermo lo disegna la scheda grafica, non il processore
    juce::String audioError;
    juce::String projectName;
    juce::String lastSavedJson;
    int autosaveCounter = 0;

    DawLookAndFeel lookAndFeel;

    IconButton fileButton { IconButton::Icon::menu };
    juce::TextButton kitButton, mixerButton { "MIXER" };
    CpuBadge cpuBadge;
    PlayButton playButton;
    RecordButton recordButton;
    LcdSelector bpmDisplay, patternDisplay;     // comandi (ora disegnati dentro il display "olografico")
    HoloDisplay holo;
    SpectrumAnalyser analyser;
    std::vector<float> scopeSamples;
    juce::Image logoImage;
    bool holoWasPlaying = false, spectrumActive = false;
    int spectrumCounter = 0;
    juce::String getKitChipText() const;
    bool hasDrumSynthChannels() const;
    SegmentedControl modeSwitch  { { "STEP", "VEL", "PROB", "ROLL" } };
    IconButton undoButton { IconButton::Icon::undo }, redoButton { IconButton::Icon::redo },
               metroButton { IconButton::Icon::metronome };
    ChannelEditor editor { engine };
    PianoRoll pianoRoll { engine };
    SegmentedControl lengthSwitch { { "16", "32" } };
    SegmentedControl pageSwitch  { { "1-16", "17-32" } };
    juce::Slider swingKnob, masterKnob;
    LevelMeter meter;
    StepRuler ruler;
    juce::OwnedArray<ChannelRow> rows;
    juce::Viewport rackView;            // i canali scorrono in verticale quando non stanno tutti
    juce::Component rackContent;
    juce::TextButton addButton;
    SoundBrowser browser { engine, sampleStore };
    Playlist playlist { engine };
    ExportPanel exportPanel { engine, sampleStore };
    MixerPanel mixerPanel { engine };
    PresetBrowser presetBrowser { engine };
    SegmentedControl songSwitch { { "PAT", "SONG" } };

    juce::Rectangle<float> headerArea, logoArea, swingCaption, masterCaption, kitCaption, rackArea;
    bool portraitLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
