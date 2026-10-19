#pragma once
#include <array>
#include <cstddef>
#include <atomic>
#include "ModTypes.h"

namespace bf
{
    /**
     * Ponte lock-free audio -> GUI: ultimo valore di modulazione normalizzato (somma della matrice, come in
     * ModulationMatrix::value) per ogni destinazione. Lo scrivono le voci (relaxed) a ogni sub-frame; la GUI lo
     * legge a ~30 Hz per muovere il puntino sull'anello di modulazione. Con piu' note vince l'ultima scrittura:
     * e' un indicatore, non un percorso di segnale.
     */
    struct ModMonitor
    {
        std::array<std::atomic<float>, DstCount> live;
        ModMonitor() { clear(); }
        void clear() noexcept { for (auto& v : live) v.store(0.0f, std::memory_order_relaxed); }
        void set(int d, float v) noexcept { live[(std::size_t) d].store(v, std::memory_order_relaxed); }
        float get(int d) const noexcept { return (d > DstNone && d < DstCount) ? live[(std::size_t) d].load(std::memory_order_relaxed) : 0.0f; }
    };

    /** Buffer circolare lock-free (un solo scrittore: il thread audio) per l'oscilloscopio della GUI. */
    class ScopeBuffer
    {
    public:
        static constexpr int kSize = 4096;      // potenza di 2
        void push(const float* mono, int n) noexcept
        {
            int w = writePos.load(std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) { data[(std::size_t) (w & (kSize - 1))] = mono[i]; ++w; }
            writePos.store(w, std::memory_order_release);
        }
        /** Copia gli ultimi `n` campioni (n <= kSize) in out; thread-safe a meno di una lettura "strappata" innocua. */
        void copyLatest(float* out, int n) const noexcept
        {
            const int w = writePos.load(std::memory_order_acquire);
            for (int i = 0; i < n; ++i) out[i] = data[(std::size_t) ((w - n + i) & (kSize - 1))];
        }
    private:
        std::array<float, kSize> data{};
        std::atomic<int> writePos { 0 };
    };
}
