#include <JuceHeader.h>
#include "UI/MainComponent.h"

//==============================================================================
class JLoopApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            main->handleCommandLine (commandLine);

       #if ! (JUCE_ANDROID || JUCE_IOS)
        // Solo per i test sul PC: "--fill" usa tutto lo schermo (per provare verticale e tablet)
        if (commandLine.contains ("--fill"))
            mainWindow->setBounds (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt());
       #endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    /** Su Android viene chiamato quando l'app va in background: salviamo subito. */
    void suspended() override
    {
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                main->saveSession();
    }

    /** Tasto Indietro di Android: chiude il pannello aperto (mixer, piano roll...) invece dell'app. */
    bool backButtonPressed() override
    {
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                return main->handleBackButton();
        return false;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff12141a), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_ANDROID || JUCE_IOS
            setFullScreen (true);
           #else
            setResizable (true, true);

            // Sul PC: finestra delle dimensioni di un telefono in orizzontale, mai piu' grande dello schermo.
            auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
            centreWithSize (juce::jmin (900, screen.getWidth()), juce::jmin (440, screen.getHeight()));
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (JLoopApplication)
