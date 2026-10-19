#pragma once
#include "ModRing.h"
#include "Modulation/PerformerPattern.h"
#include "Modulation/VoiceModulation.h"

namespace bf::ui
{
    /**
     * Editor del Performer a 32 step:
     *   - trascina nell'area barre per disegnare i valori (-1..+1); doppio click = 0; Shift = snap a 1/8;
     *   - la striscia sotto le barre mostra l'interpolazione di ogni step (L = Linear, E = Exp, S = Step, C = Custom):
     *     click per scorrerle (Shift = all'indietro);
     *   - lo step selezionato ha la tensione di curva (usata dalla modalita' Custom) e il selettore di interpolazione;
     *   - la linea cyan e' il segnale REALE calcolato da PerformerPattern::evaluate() (la stessa funzione del motore audio)
     *     con la modalita' di riproduzione del LFO; gli step oltre la lunghezza sono sfumati.
     * Scrive direttamente negli atomic del pattern (nessun lock); la persistenza e' gia' in getStateInformation().
     */
    class PerformerEditor : public juce::Component
    {
    public:
        PerformerEditor(PerformerBank& bank, Apvts& apvts);

        /** Sceglie quale dei kNumLfo pattern si sta modificando. */
        void setTarget(int lfoIndex);
        int  getTarget() const noexcept { return lfo; }

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        PerformerPattern& pat() { return bank.p[(size_t) lfo]; }
        const PerformerPattern& pat() const { return bank.p[(size_t) lfo]; }
        juce::Rectangle<float> barsArea() const;
        juce::Rectangle<float> interpArea() const;
        int   stepAt(float x) const;
        float valueAt(float y, bool snap) const;
        void  paintStep(int step, float y, bool snap);
        void  syncControls();
        PlayMode playMode() const;

        PerformerBank& bank;
        Apvts& apvts;
        int lfo = 0;
        int selected = 0;
        int lastPainted = -1;

        juce::Slider lengthSlider, curveSlider;
        juce::ComboBox interpBox;
        juce::Label lengthLabel, curveLabel;
        juce::TextButton clearBtn { "Clear" }, rampUp { "Ramp Up" }, rampDown { "Ramp Down" }, sineBtn { "Sine" }, randBtn { "Random" };
        juce::Random rng;
    };
}
