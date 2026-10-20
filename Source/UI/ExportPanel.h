#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/AudioExport.h"
#include "Theme.h"
#include "Widgets.h"
#include "ChannelEditor.h"   // LabeledKnob

//==============================================================================
/**
    Pannello "Esporta audio": pattern (con ripetizioni) o canzone intera, WAV o OGG.
    Dopo l'esportazione: Condividi (WhatsApp, Drive, mail...) o Salva nel telefono.
*/
class ExportPanel : public juce::Component,
                    private juce::Timer
{
public:
    ExportPanel (SequencerEngine& e, SampleStore& s) : engine (e), samples (s)
    {
        for (auto* b : { &exportButton, &shareButton, &saveButton, &closeButton })
            addAndMakeVisible (b);

        addAndMakeVisible (whatSwitch);
        addAndMakeVisible (loopsSwitch);
        addAndMakeVisible (formatSwitch);
        loopsSwitch.setSelected (2);

        whatSwitch.onChange   = [this] (int) { updateState(); };
        loopsSwitch.onChange  = [this] (int) { updateState(); };
        formatSwitch.onChange = [this] (int) { updateState(); };

        // --- Mastering
        addAndMakeVisible (masterToggle);
        masterToggle.setClickingTogglesState (true);
        masterToggle.onClick = [this] { engine.getMastering().enabled.store (masterToggle.getToggleState()); updateState(); };
        for (auto* b : { &styleButton, &autoButton, &listenButton })
            addAndMakeVisible (b);
        styleButton.onClick = [this] { showStyleMenu(); };
        autoButton.onClick = [this] { startAnalysis(); };
        listenButton.onClick = [this]
        {
            if (engine.isPlaying())
                engine.setPlaying (false);
            else
            {
                engine.setSongMode (songSelected());
                engine.setSongPosition (0);
                engine.setPlaying (true);
            }
            updateState();
        };

        auto db = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
        auto pct = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        addKnob ("Volume", -20.0, -6.0, -12.0, [] (double v) { return juce::String (v, 1) + " LUFS"; }, &mastering::Params::loudness);
        addKnob ("Bassi", -6.0, 6.0, 0.0, db, &mastering::Params::lowDb);
        addKnob ("Presenza", -6.0, 6.0, 0.0, db, &mastering::Params::presenceDb);
        addKnob ("Aria", -6.0, 6.0, 0.0, db, &mastering::Params::airDb);
        addKnob ("Compressione", 0.0, 1.0, 0.4, pct, &mastering::Params::glue);
        addKnob ("Calore", 0.0, 1.0, 0.2, pct, &mastering::Params::warmth);
        addKnob ("Stereo", 0.0, 1.5, 1.0, pct, &mastering::Params::width);
        addKnob ("Tetto", -6.0, -0.3, -1.0, [] (double v) { return juce::String (v, 1) + " dB"; }, &mastering::Params::ceiling);

        exportButton.onClick = [this] { startExport(); };
        shareButton.onClick  = [this] { share(); };
        saveButton.onClick   = [this] { saveToPhone(); };
        closeButton.onClick  = [this] { if (onClose) onClose(); };
    }

    ~ExportPanel() override     { analyser.reset(); exporter.reset(); }

    std::function<void()> onClose;
    std::function<juce::var()> getProjectJson;                           // copia del progetto
    std::function<juce::String()> getProjectName;

    void open()
    {
        exporter.reset();
        lastFile = juce::File();
        status = {};
        whatSwitch.setSelected (engine.isSongMode() && engine.getSongLengthSteps() > 0 ? 1 : 0);
        refreshMastering();
        updateState();
        startTimerHz (10);
    }

    void visibilityChanged() override
    {
        if (! isVisible() && analyser == nullptr && exporter == nullptr)
            stopTimer();
    }

    void startAnalysisForTest()     { startAnalysis(); }

    /** Rilegge le impostazioni del mastering (caricamento, undo). */
    void refreshMastering()
    {
        auto& m = engine.getMastering();
        masterToggle.setToggleState (m.enabled.load(), juce::dontSendNotification);
        styleButton.setButtonText ("Stile: " + juce::String (mastering::style (m.styleIndex.load()).name)
                                   + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")));
        for (auto& k : knobs)
            if (! k.knob->slider.isMouseButtonDown())
                k.knob->setValue ((m.*(k.param)).load());
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), Theme::corner, 1.0f);

        auto title = titleArea;
        g.setColour (Theme::accent);
        g.fillRect (title.removeFromLeft (4.0f).reduced (0.0f, 6.0f));
        title.removeFromLeft (8.0f);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (17.0f, true));
        g.drawFittedText ("Esporta audio", title.toNearestInt(), juce::Justification::centredLeft, 1);

        Theme::drawCaption (g, "Cosa", whatCaption, juce::Justification::centredLeft);
        if (loopsSwitch.isVisible())
            Theme::drawCaption (g, "Ripetizioni del pattern", loopsCaption, juce::Justification::centredLeft);
        Theme::drawCaption (g, "Formato", formatCaption, juce::Justification::centredLeft);

        // Mastering
        Theme::drawSunken (g, masterArea, Theme::bg.withAlpha (0.5f), Theme::corner);
        g.setColour (Theme::textBright);
        g.setFont (Theme::font (14.0f, true));
        g.drawText ("Mastering", masterTitle, juce::Justification::centredLeft, false);
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.0f));
        g.drawFittedText (masteringInfo(), masterInfoArea.toNearestInt(), juce::Justification::centredLeft, 2);
        if (analyser != nullptr && ! analyser->isFinished())
        {
            auto bar = masterInfoArea.removeFromBottom (6.0f);
            Theme::drawSunken (g, bar, Theme::bg, 3.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * analyser->getProgress()).reduced (1.0f), 2.0f);
        }

        // Barra di avanzamento
        if (exporter != nullptr && ! exporter->isFinished())
        {
            auto bar = statusArea.removeFromTop (12.0f).reduced (0.0f, 2.0f);
            Theme::drawSunken (g, bar, Theme::bg, 4.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * exporter->getProgress()).reduced (1.0f), 3.0f);
        }

        g.setColour (Theme::text);
        g.setFont (Theme::font (13.5f));
        g.drawFittedText (status, statusArea.toNearestInt(), juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (34);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        titleArea = top.toFloat();
        r.removeFromTop (6);

        const bool wide = r.getWidth() > 720;
        juce::Rectangle<int> left = r, right;
        if (wide)
        {
            left = r.removeFromLeft (juce::jmin (440, r.getWidth() * 9 / 20));
            r.removeFromLeft (12);
            right = r;
        }

        const int w = juce::jmin (440, left.getWidth());
        auto row = [&left, w] (juce::Component& c, juce::Rectangle<float>& caption)
        {
            caption = left.removeFromTop (16).toFloat();
            c.setBounds (left.removeFromTop (32).withWidth (w));
            left.removeFromTop (8);
        };

        row (whatSwitch, whatCaption);
        if (loopsSwitch.isVisible())
            row (loopsSwitch, loopsCaption);
        row (formatSwitch, formatCaption);

        auto buttons = left.removeFromBottom (40);
        const int bw = juce::jmin (170, (buttons.getWidth() - 12) / 3);
        exportButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        buttons.removeFromLeft (6);
        shareButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        buttons.removeFromLeft (6);
        saveButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        left.removeFromBottom (6);

        if (! wide)
        {
            // In verticale il mastering sta sotto il formato, lo stato sopra i pulsanti
            right = left.removeFromTop (juce::jmin (left.getHeight() - 60, 330));
            left.removeFromTop (8);
        }
        statusArea = left.toFloat();

        // --- Mastering
        masterArea = right.toFloat();
        auto m = right.reduced (10, 8);
        auto head = m.removeFromTop (30);
        masterTitle = head.removeFromLeft (92).toFloat();
        masterToggle.setBounds (head.removeFromLeft (70).reduced (0, 2));
        head.removeFromLeft (8);
        listenButton.setBounds (head.removeFromRight (juce::jmin (90, head.getWidth() / 3)).reduced (0, 2));
        head.removeFromRight (6);
        styleButton.setBounds (head.reduced (0, 2));
        m.removeFromTop (6);
        auto autoRow = m.removeFromTop (32);
        autoButton.setBounds (autoRow.removeFromLeft (juce::jmin (210, autoRow.getWidth() / 2)).reduced (0, 1));
        autoRow.removeFromLeft (10);
        masterInfoArea = autoRow.toFloat();
        m.removeFromTop (8);

        const int cols = m.getWidth() >= 560 ? 8 : 4;
        const int rows = (int) knobs.size() / cols;
        const int kw = m.getWidth() / cols, kh = juce::jmin (100, m.getHeight() / juce::jmax (1, rows));
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i].knob->setBounds (m.getX() + (int) (i % (size_t) cols) * kw, m.getY() + (int) (i / (size_t) cols) * kh, kw, kh);
    }

private:
    struct MasterKnob
    {
        std::unique_ptr<LabeledKnob> knob;
        std::atomic<float> mastering::Params::* param;
    };

    void addKnob (const juce::String& name, double lo, double hi, double def, std::function<juce::String (double)> fmt,
                  std::atomic<float> mastering::Params::* param)
    {
        MasterKnob k;
        k.knob = std::make_unique<LabeledKnob> (name, lo, hi, def, std::move (fmt));
        k.param = param;
        k.knob->setColour (Theme::accent);
        k.knob->onChange = [this, param] (double v) { (engine.getMastering().*param).store ((float) v); repaint(); };
        addAndMakeVisible (*k.knob);
        knobs.push_back (std::move (k));
    }

    juce::String masteringInfo() const
    {
        const auto& m = engine.getMastering();
        if (analyser != nullptr && ! analyser->isFinished())
            return "Ascolto la canzone e regolo il mastering...  " + juce::String (juce::roundToInt (analyser->getProgress() * 100.0f)) + "%";
        if (! m.analysed)
            return "Tocca \"Automatico\": JLoop ascolta il mix e regola tutto per lo stile scelto. Poi puoi cambiare a mano.";
        const float target = m.loudness.load();
        const float reached = juce::jmin (m.resultLufs, m.analysedLoudness);
        const float expected = target <= reached ? target : juce::jmin (target, m.resultLufs + juce::jmax (0.0f, target - m.analysedLoudness));
        return "Mix: " + juce::String (m.measuredLufs, 1) + " LUFS (picco " + juce::String (m.measuredPeakDb, 1) + " dB)"
             + juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) + "col mastering circa " + juce::String (expected, 1) + " LUFS"
             + (m.resultLufs < m.analysedLoudness - 0.5f && target > reached + 0.3f ? "\n(piu' forte di cosi' il mix non va senza distorcere)" : "");
    }

    void showStyleMenu()
    {
        juce::PopupMenu menu;
        for (int i = 0; i < mastering::numStyles; ++i)
            menu.addItem (i + 1, juce::String (mastering::style (i).name) + "   -   " + mastering::style (i).detail, true,
                          i == engine.getMastering().styleIndex.load());
        juce::Component::SafePointer<ExportPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleButton), [safe] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            auto& m = safe->engine.getMastering();
            m.applyStyle (result - 1);
            m.analysed = false;            // lo stile cambia il suono: si rifa' l'analisi
            safe->refreshMastering();
            safe->startAnalysis();
        });
    }

    void startAnalysis()
    {
        if (analyser != nullptr && ! analyser->isFinished())
        {
            analyser.reset();
            updateState();
            return;
        }
        if (! getProjectJson)
            return;
        auto s = currentSettings();
        s.analyse = true;
        const int steps = s.song ? engine.getSongLengthSteps() : engine.getPatternLength() * s.loops;
        if (steps <= 0)
        {
            status = "Niente da analizzare: la playlist o il pattern sono vuoti.";
            repaint();
            return;
        }
        analyser = std::make_unique<AudioExporter> (getProjectJson(), samples, s, juce::File());
        analyser->startThread (juce::Thread::Priority::normal);
        startTimerHz (20);
        updateState();
    }

    bool songSelected() const   { return whatSwitch.getSelected() == 1; }

    AudioExporter::Settings currentSettings() const
    {
        AudioExporter::Settings s;
        s.song = songSelected();
        static const int loops[] = { 1, 2, 4, 8 };
        s.loops = loops[juce::jlimit (0, 3, loopsSwitch.getSelected())];
        s.format = formatSwitch.getSelected() == 0 ? AudioExporter::Format::wav : AudioExporter::Format::ogg;
        return s;
    }

    void updateState()
    {
        const bool busy = exporter != nullptr && ! exporter->isFinished();
        loopsSwitch.setVisible (! songSelected());

        if (! busy)
        {
            const auto s = currentSettings();
            const int steps = s.song ? engine.getSongLengthSteps() : engine.getPatternLength() * s.loops;
            const double secs = steps * 60.0 / engine.getBpm() / 4.0;

            if (s.song && steps == 0)
                status = "La playlist e' vuota: metti qualche pattern nella playlist (pulsante SONG) oppure esporta il pattern.";
            else
            {
                const double mb = (s.format == AudioExporter::Format::wav ? 176400.0 : 24000.0) * (secs + 2.5) / 1048576.0;
                status = (s.song ? "Canzone intera" : "Pattern " + juce::String (engine.getCurrentPattern() + 1) + " x " + juce::String (s.loops))
                       + ":  " + formatTime (secs) + "  (circa " + juce::String (mb, mb < 10 ? 1 : 0) + " MB)";
                if (lastFile.existsAsFile())
                    status << "\nUltimo file: " << lastFile.getFileName();
            }

            exportButton.setEnabled (steps > 0);
        }

        shareButton.setEnabled (! busy && lastFile.existsAsFile());
        saveButton.setEnabled (! busy && lastFile.existsAsFile());
        whatSwitch.setEnabled (! busy);
        loopsSwitch.setEnabled (! busy);
        formatSwitch.setEnabled (! busy);
        exportButton.setButtonText (busy ? "Annulla" : "Esporta");
        const bool analysing = analyser != nullptr && ! analyser->isFinished();
        autoButton.setButtonText (analysing ? "Annulla analisi" : juce::String ("Automatico"));
        autoButton.setEnabled (! busy);
        exportButton.setEnabled (exportButton.isEnabled() && ! analysing);
        listenButton.setButtonText (engine.isPlaying() ? "Stop" : "Ascolta");
        for (auto& k : knobs)
            k.knob->setAlpha (engine.getMastering().enabled.load() ? 1.0f : 0.45f);
        resized();
        repaint();
    }

    static juce::String formatTime (double secs)
    {
        const int s = juce::roundToInt (secs);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

    void startExport()
    {
        if (exporter != nullptr && ! exporter->isFinished())
        {
            exporter.reset();       // annulla (ferma il thread e cancella il file)
            status = "Esportazione annullata.";
            updateState();
            return;
        }

        if (! getProjectJson)
            return;

        const auto s = currentSettings();
        auto name = juce::File::createLegalFileName (getProjectName ? getProjectName() : juce::String ("JLoop"));
        if (name.isEmpty()) name = "JLoop";
        if (! s.song) name << " - pattern " << (engine.getCurrentPattern() + 1);

        auto folder = ProjectIO::getRootFolder().getChildFile ("Exports");
        auto file = folder.getChildFile (name + (s.format == AudioExporter::Format::wav ? ".wav" : ".ogg"));

        exporter = std::make_unique<AudioExporter> (getProjectJson(), samples, s, file);
        exporter->startThread (juce::Thread::Priority::normal);
        lastFile = juce::File();
        status = "Esportazione in corso...";
        startTimerHz (20);
        updateState();
    }

    void timerCallback() override
    {
        if (analyser != nullptr)
        {
            if (analyser->isFinished())
            {
                if (analyser->getError().isEmpty())
                {
                    analyser->applyAnalysisTo (engine.getMastering());
                    engine.getMastering().enabled.store (true);
                }
                else
                    status = analyser->getError();
                analyser.reset();
                refreshMastering();
                updateState();
            }
            repaint();
        }

        if (listenButton.getButtonText() != (engine.isPlaying() ? "Stop" : "Ascolta"))
            listenButton.setButtonText (engine.isPlaying() ? "Stop" : "Ascolta");

        if (exporter == nullptr)
        {
            if (analyser == nullptr && ! isVisible())
                stopTimer();
            return;
        }

        if (! exporter->isFinished())
        {
            status = "Esportazione in corso...  " + juce::String (juce::roundToInt (exporter->getProgress() * 100.0f)) + "%";
            repaint();
            return;
        }

        const auto error = exporter->getError();
        if (error.isEmpty())
        {
            lastFile = exporter->getFile();
            exporter.reset();
            updateState();
            status = "Fatto!  " + lastFile.getFileName() + "  (" + juce::File::descriptionOfSizeInBytes (lastFile.getSize())
                   + ")\nOra puoi condividerlo o salvarlo nel telefono.";
        }
        else
        {
            exporter.reset();
            updateState();
            status = error;
        }
        repaint();
    }

    void share()
    {
        if (! lastFile.existsAsFile())
            return;

        shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (lastFile) }, [] (bool, const juce::String&) {}, this);
    }

    void saveToPhone()
    {
        if (! lastFile.existsAsFile())
            return;

        chooser = std::make_unique<juce::FileChooser> ("Salva l'audio",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (lastFile.getFileName()),
                                                       "*" + lastFile.getFileExtension());

        juce::Component::SafePointer<ExportPanel> safeThis (this);
        const auto source = lastFile;
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis, source] (const juce::FileChooser& fc)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto url = fc.getURLResult();
                                  if (url.isEmpty())
                                      return;

                                  bool ok = false;
                                 #if JUCE_ANDROID
                                  if (auto doc = juce::AndroidDocument::fromDocument (url))
                                      if (auto out = doc.createOutputStream())
                                          if (juce::FileInputStream in (source); in.openedOk())
                                              ok = out->writeFromInputStream (in, -1) == source.getSize();
                                 #endif
                                  if (! ok && url.isLocalFile())
                                      ok = source.copyFileTo (url.getLocalFile());

                                  safeThis->status = ok ? "Salvato nel telefono." : "Non sono riuscito a salvare il file.";
                                  safeThis->repaint();
                              });
    }

    SequencerEngine& engine;
    SampleStore& samples;
    std::unique_ptr<AudioExporter> exporter, analyser;
    std::vector<MasterKnob> knobs;
    LedToggle masterToggle;
    juce::TextButton styleButton, autoButton, listenButton { "Ascolta" };
    juce::Rectangle<float> masterArea, masterTitle, masterInfoArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox shareBox;
    juce::File lastFile;
    juce::String status;

    SegmentedControl whatSwitch   { { "Pattern selezionato", "Canzone (playlist)" } };
    SegmentedControl loopsSwitch  { { "1 volta", "2 volte", "4 volte", "8 volte" } };
    SegmentedControl formatSwitch { { "WAV (qualita' CD)", "OGG (leggero)" } };
    juce::TextButton exportButton { "Esporta" }, shareButton { "Condividi" }, saveButton { "Salva nel telefono" }, closeButton { "Chiudi" };
    juce::Rectangle<float> titleArea, whatCaption, loopsCaption, formatCaption, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};
