#pragma once
#include "ModRing.h"
#include "DSP/Wavetable.h"

namespace bf::ui
{
    /**
     * Vista della wavetable di un oscillatore, aggiornata a 30 Hz:
     *   sinistra: cascata 3D (frame in prospettiva, rimozione linee nascoste), cursore cyan sulla posizione WT
     *             e cursore arancione sulla posizione REALE (parametro + modulazione live dalle voci);
     *   destra:   forma d'onda 2D del frame interpolato (cyan) e di quello modulato (arancione).
     * Legge solo il banco immutabile e valori atomici: nessun lock, nessun accesso al thread audio.
     */
    class WavetableView : public juce::Component, private juce::Timer
    {
    public:
        WavetableView(const WavetableBank& bank, Apvts& apvts, const ModMonitor& monitor, int oscIndex);
        ~WavetableView() override { stopTimer(); }
        void paint(juce::Graphics&) override;

    private:
        void timerCallback() override;
        void fillWave(float pos01, float* out, int n) const;
        void buildPath(juce::Path& p, const float* w, int n, juce::Rectangle<float> r, float baselineY, float amp) const;

        const WavetableBank& bank;
        const ModMonitor& monitor;
        std::atomic<float>* wtParam = nullptr;
        int dest;
        int oscIdx;
        float shownBase = -1.f, shownLive = -1.f;
    };
}
