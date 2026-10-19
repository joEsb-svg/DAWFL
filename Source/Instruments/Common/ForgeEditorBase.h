#pragma once

#include <JuceHeader.h>
#include "UI/KnobKit.h"
#include "UI/CustomLookAndFeel.h"

//==============================================================================
/**
    Base comune delle interfacce dei synth "Forge" (PolyForge, FMForge), nello stile di BassForge:
    logo, schede in alto, pannelli con manopole grandi, disegni (inviluppi, algoritmi).
    Si adatta alla dimensione data dall'host: in orizzontale i pannelli sono affiancati,
    in verticale uno sotto l'altro.
*/
class ForgeEditorBase : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    ForgeEditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state,
                     const juce::String& logoLeft, const juce::String& logoRight, juce::Colour accent,
                     const juce::StringArray& tabNames);
    ~ForgeEditorBase() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showTab (int index);
    int getTab() const          { return tab; }

    /** Disegno di un inviluppo ADSR letto dai parametri. */
    class AdsrView : public juce::Component
    {
    public:
        AdsrView (juce::AudioProcessorValueTreeState&, juce::String a, juce::String d, juce::String s, juce::String r, juce::Colour);
        void paint (juce::Graphics&) override;
        juce::uint64 signature() const;
        juce::uint64 shown = 0;

    private:
        float value (int i) const;
        juce::AudioProcessorValueTreeState& apvts;
        std::array<juce::String, 4> ids;
        juce::Colour colour;
    };

protected:
    struct Section
    {
        std::unique_ptr<bf::ui::Panel> panel;
        std::vector<juce::Component*> items;
        juce::Component* view = nullptr;     // disegno (sopra o a sinistra delle manopole)
        float viewShare = 0.38f;
        float weight = 1.0f;
    };

    Section& addSection (int tabIndex, const juce::String& title, float weight = 1.0f);
    void knob (Section&, const juce::String& id, const juce::String& title);
    void combo (Section&, const juce::String& id, const juce::String& title);
    void toggle (Section&, const juce::String& id, const juce::String& title);
    void setView (Section&, std::unique_ptr<juce::Component> view, float share = 0.38f);
    void adsr (Section&, const juce::String& a, const juce::String& d, const juce::String& s, const juce::String& r, juce::Colour);

    /** Ogni 1/15 s: le viste che dipendono dai parametri si ridisegnano se serve. */
    virtual void refreshViews() {}

    juce::AudioProcessorValueTreeState& apvts;
    juce::Colour accent;

private:
    void timerCallback() override;

    bf::ui::CustomLookAndFeel laf;
    juce::String logoA, logoB;
    juce::OwnedArray<juce::TextButton> tabs;
    std::vector<std::vector<std::unique_ptr<Section>>> sections;
    std::vector<std::unique_ptr<juce::Component>> owned;
    int tab = 0;
    static constexpr int headerHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeEditorBase)
};
