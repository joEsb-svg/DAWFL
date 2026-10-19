#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace bf
{
    /** Campione mono immutabile (un transiente breve). */
    struct SampleData
    {
        std::vector<float> data;
        double sampleRate = 48000.0;
        juce::String name;
    };

    /**
     * Banco di campioni brevi per il "Sample Attack Transient".
     *
     *  - Slot 0..3: transienti sintetizzati internamente (Metal Hit, Kick Click, Snare Crack, Foley Tick),
     *    quindi nessun file e' necessario per usare il plugin.
     *  - Slot 4..7: slot utente, caricabili da file/buffer.
     *
     * THREAD-SAFETY (pattern "publish immutabile"):
     *   - setSample()/loadFile()/buildBuiltIns() vanno chiamati dal message thread (allocano).
     *   - Ogni campione e' immutabile e viene pubblicato con uno store atomico (release) nello slot.
     *   - Il thread audio legge con get() (acquire): nessun lock, nessuna allocazione.
     *   - I campioni sostituiti NON vengono liberati finche' il banco vive (cosi' il thread audio non puo'
     *     mai leggere memoria liberata). Sono brevi (<= 2 s mono): il costo e' trascurabile.
     */
    class SampleBank
    {
    public:
        static constexpr int    kMaxSlots   = 8;
        static constexpr int    kNumBuiltIn = 4;
        static constexpr double kMaxSeconds = 2.0;

        SampleBank();

        /** Genera i 4 transienti interni negli slot 0..3. */
        void buildBuiltIns();

        /** Pubblica un campione mono (copiato). Ritorna false se slot/dati non validi. Tronca a kMaxSeconds. */
        bool setSample(int slot, const float* mono, int numSamples, double sampleRate, const juce::String& name);

        /** Carica da file (wav/aiff/flac/ogg...), mixdown a mono, normalizzato a -0.5 dBFS. */
        bool loadFile(int slot, const juce::File& file);

        const SampleData* get(int slot) const noexcept
        {
            return (slot >= 0 && slot < kMaxSlots) ? slots[(size_t) slot].load(std::memory_order_acquire) : nullptr;
        }

    private:
        std::array<std::atomic<const SampleData*>, kMaxSlots> slots;
        std::vector<std::unique_ptr<SampleData>> owned;   // proprieta' (solo message thread)
        juce::CriticalSection ownedLock;
    };
}
