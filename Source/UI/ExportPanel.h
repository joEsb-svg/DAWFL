#pragma once

#include <JuceHeader.h>
#include <functional>
#include "../Engine/AudioExport.h"
#include "Theme.h"
#include "Widgets.h"

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

        exportButton.onClick = [this] { startExport(); };
        shareButton.onClick  = [this] { share(); };
        saveButton.onClick   = [this] { saveToPhone(); };
        closeButton.onClick  = [this] { if (onClose) onClose(); };
    }

    ~ExportPanel() override     { exporter.reset(); }

    std::function<void()> onClose;
    std::function<juce::var()> getProjectJson;                           // copia del progetto
    std::function<juce::String()> getProjectName;

    void open()
    {
        exporter.reset();
        lastFile = juce::File();
        status = {};
        whatSwitch.setSelected (engine.isSongMode() && engine.getSongLengthSteps() > 0 ? 1 : 0);
        updateState();
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

        const int w = juce::jmin (440, r.getWidth());
        auto row = [&r, w] (juce::Component& c, juce::Rectangle<float>& caption)
        {
            caption = r.removeFromTop (16).toFloat();
            c.setBounds (r.removeFromTop (32).withWidth (w));
            r.removeFromTop (8);
        };

        row (whatSwitch, whatCaption);
        if (loopsSwitch.isVisible())
            row (loopsSwitch, loopsCaption);
        row (formatSwitch, formatCaption);

        auto buttons = r.removeFromBottom (40);
        const int bw = juce::jmin (170, (buttons.getWidth() - 12) / 3);
        exportButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        buttons.removeFromLeft (6);
        shareButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        buttons.removeFromLeft (6);
        saveButton.setBounds (buttons.removeFromLeft (bw).reduced (0, 3));
        r.removeFromBottom (6);
        statusArea = r.toFloat();
    }

private:
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
            stopTimer();
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
        if (exporter == nullptr)
        {
            stopTimer();
            return;
        }

        if (! exporter->isFinished())
        {
            status = "Esportazione in corso...  " + juce::String (juce::roundToInt (exporter->getProgress() * 100.0f)) + "%";
            repaint();
            return;
        }

        stopTimer();
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
    std::unique_ptr<AudioExporter> exporter;
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
