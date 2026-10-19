#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

namespace bf
{
    /**
     * Banco wavetable 2D/3D (frame x fase) con MIP-MAP band-limited.
     *
     *  - Ogni frame e' una tabella di kTableSize campioni (un ciclo).
     *  - Per ogni frame esistono kNumMips versioni: il livello m contiene al massimo
     *    (kTableSize/2) >> m armoniche (1024, 512, ... , 1). Il livello giusto per una
     *    data frequenza si ottiene con mipForIncrement().
     *  - Ogni tabella ha 3 campioni di guardia (1 prima, 2 dopo) cosi' l'interpolazione
     *    cubica a 4 punti non deve mai fare il wrap degli indici nel loop audio.
     *
     * THREAD-SAFETY: l'oggetto e' IMMUTABILE dopo build()/buildFromFrames(). Va costruito
     * sul message thread (alloca, usa la FFT) e poi condiviso in sola lettura. Per sostituirlo
     * a caldo si crea un nuovo banco, si passa il puntatore all'oscillatore
     * (WavetableOscillator::setBank, atomico) e si distrugge il vecchio solo dopo che il thread
     * audio e' passato oltre (es. juce::ReferenceCountedObjectPtr + deferred release).
     */
    class WavetableBank
    {
    public:
        static constexpr int kTableSize = 2048;                 // campioni per ciclo (potenza di 2, = FFT order 11)
        static constexpr int kFftOrder  = 11;
        static constexpr int kMaxFrames = 256;
        static constexpr int kNumMips   = 11;                   // 1024 ... 1 armoniche
        static constexpr int kStride    = kTableSize + 3;       // + guardie

        /** Banco di default: 64 frame, sweep armonico sine -> saw ricca. */
        void build();

        /** Banco custom. `frames` = numFrames * kTableSize campioni contigui (un ciclo per frame). */
        bool buildFromFrames(const float* frames, int numFrames);

        bool isReady() const noexcept   { return numFrames > 0; }
        int  getNumFrames() const noexcept { return numFrames; }

        /** Puntatore al campione 0 della tabella; sono validi gli indici da -1 a kTableSize+1. */
        const float* getTable(int frame, int mip) const noexcept
        {
            return data.data() + ((size_t) frame * (size_t) kNumMips + (size_t) mip) * (size_t) kStride + 1;
        }

        /** Livello mip corretto per un incremento di fase (freq / sampleRate), senza aliasing sulle armoniche. */
        static int mipForIncrement(float phaseInc) noexcept
        {
            const float x = phaseInc * (float) kTableSize;      // 2^m >= 2048 * inc
            if (x <= 1.0f) return 0;
            const int m = (int) std::ceil(std::log2(x));
            return m < kNumMips - 1 ? m : kNumMips - 1;
        }

        /** Lettura semplice (lineare, mip 0) mantenuta per compatibilita' con BassVoice del passo 1. */
        float read(float frame01, float phase01) const noexcept
        {
            const float fpos = juce::jlimit(0.f, 1.f, frame01) * (float) (numFrames - 1);
            const int   f0   = (int) fpos;
            const int   f1   = f0 + 1 < numFrames ? f0 + 1 : f0;
            const float ff   = fpos - (float) f0;

            const float ppos = phase01 * (float) kTableSize;
            const int   i0   = (int) ppos;
            const float pf   = ppos - (float) i0;

            const float* a = getTable(f0, 0);
            const float* b = getTable(f1, 0);
            const float sa = a[i0] + pf * (a[i0 + 1] - a[i0]);
            const float sb = b[i0] + pf * (b[i0 + 1] - b[i0]);
            return sa + ff * (sb - sa);
        }

    private:
        std::vector<float> data;   // [frame][mip][kStride]
        int numFrames = 0;
    };
}
