#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParameterIDs.h"
#include "Modulation/VoiceModulation.h"

namespace bf
{
    /**
     * Gestione preset (message thread). Un preset e' lo stesso XML di getStateInformation():
     * parametri APVTS + pattern dei Performer + nome. I preset utente stanno in
     *   <ApplicationData>/BassForge/Presets/ (file .bfpreset)
     * I preset factory sono costruiti in codice (partono da "tutti i default" e sovrascrivono pochi parametri),
     * quindi restano validi anche se in futuro cambiano i default dei parametri non toccati.
     */
    class PresetManager
    {
    public:
        PresetManager(juce::AudioProcessorValueTreeState& apvts, PerformerBank& performers);

        // --- factory ---
        static int numFactory() noexcept;
        static juce::String factoryName(int i);
        void loadFactory(int i);

        // --- utente ---
        juce::File userDir() const;
        juce::Array<juce::File> userPresets() const;
        bool save(const juce::String& name);                 // sovrascrive se esiste; false se il nome non e' valido / I/O fallito
        bool load(const juce::File& file);

        void initPatch();                                    // tutti i parametri ai default + pattern Performer di default
        juce::String currentName() const { return apvts.state.getProperty("presetName", "Init").toString(); }

        // --- helper per costruire preset (usati anche dai test) ---
        bool set(const juce::String& paramId, float realValue);       // false se l'ID non esiste
        void connect(int slot, int src, int dst, float amount);
        void clearSlots();
        void setPattern(int lfo, std::initializer_list<float> values, StepInterp interp, int length = -1);

    private:
        void setName(const juce::String& n) { apvts.state.setProperty("presetName", n, nullptr); }
        void resetPatterns();

        juce::AudioProcessorValueTreeState& apvts;
        PerformerBank& performers;
    };
}
