#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "ModTypes.h"

namespace bf
{
    /**
     * Matrice di modulazione (Source -> Destination, Amount) a sub-frame.
     *
     * Modello temporale (sample-accurate senza costo per-campione):
     *   - le sorgenti vengono aggiornate una volta per SUB-FRAME (VoiceModulation::kSubFrame = 16 campioni,
     *     ~2.7 kHz di control-rate: un LFO fino a ~500 Hz resta pulito);
     *   - evaluate() calcola la somma per ogni destinazione e conserva quella del frame precedente;
     *   - il consumatore legge una RAMPA lineare (Ramp: start -> end) e la applica campione per campione:
     *     continuita' garantita tra frame consecutivi, nessun salto/jitter alle alte frequenze.
     *
     * Le connessioni stanno in una tabella a capienza fissa (kMaxConnections): nessuna allocazione.
     * La tabella si cambia a caldo con setTable() (copia di 12 voci) dal thread audio.
     */
    class ModulationMatrix
    {
    public:
        static constexpr int kMaxConnections = kNumSlots;

        struct Connection
        {
            int   source = SrcNone;
            int   dest   = DstNone;
            float amount = 0.0f;       // -1..1 (negativo = modulazione invertita)
        };

        struct Table
        {
            std::array<Connection, kMaxConnections> c{};
            int count = 0;

            void clear() noexcept { count = 0; }
            /** Aggiunge una connessione; false se la tabella e' piena o gli indici non sono validi. */
            bool add(int src, int dst, float amount) noexcept
            {
                if (count >= kMaxConnections || src <= SrcNone || src >= SrcCount || dst <= DstNone || dst >= DstCount)
                    return false;
                c[(size_t) count++] = { src, dst, std::min(std::max(amount, -1.0f), 1.0f) };
                return true;
            }
            void remove(int index) noexcept
            {
                if (index < 0 || index >= count) return;
                for (int i = index; i + 1 < count; ++i) c[(size_t) i] = c[(size_t) i + 1];
                --count;
            }
        };

        /** Rampa lineare di un valore lungo un sub-frame. */
        struct Ramp
        {
            float start = 0.0f, end = 0.0f;
            /** Valore al campione i (0..n-1) di n: raggiunge `end` all'ultimo campione. */
            float at(int i, int n) const noexcept { return start + (end - start) * ((float) (i + 1) / (float) n); }
            float step(int n) const noexcept      { return (end - start) / (float) n; }
        };

        // --- Configurazione ---------------------------------------------------------------
        void setTable(const Table& t) noexcept { table = t; }
        const Table& getTable() const noexcept { return table; }

        // --- Sorgenti -----------------------------------------------------------------------
        void setSource(int s, float v) noexcept
        {
            if (s > SrcNone && s < SrcCount)
                src[(size_t) s] = (v > -1.0e6f && v < 1.0e6f) ? v : 0.0f;       // scarta NaN/Inf
        }
        float source(int s) const noexcept { return (s >= 0 && s < SrcCount) ? src[(size_t) s] : 0.0f; }

        // --- Valutazione ----------------------------------------------------------------------
        /** Una volta per sub-frame, DOPO l'aggiornamento delle sorgenti. */
        void evaluate() noexcept
        {
            prev = cur;
            cur.fill(0.0f);
            for (int i = 0; i < table.count; ++i)
            {
                const auto& k = table.c[(size_t) i];
                if (k.source > SrcNone && k.source < SrcCount && k.dest > DstNone && k.dest < DstCount)
                    cur[(size_t) k.dest] += src[(size_t) k.source] * k.amount;
            }
            for (auto& v : cur)
                v = std::min(std::max(v, -modscale::kMaxSum), modscale::kMaxSum);
        }

        /** Annulla la rampa in corso (usare dopo noteOn: nessun glide dal valore della nota precedente). */
        void snap() noexcept { prev = cur; }
        void clearValues() noexcept { cur.fill(0.0f); prev.fill(0.0f); }

        // --- Lettura (interfaccia per i consumatori) --------------------------------------------
        /** Somma di modulazione normalizzata a fine sub-frame (0 se la destinazione non e' collegata). */
        float value(int d) const noexcept { return (d > DstNone && d < DstCount) ? cur[(size_t) d] : 0.0f; }
        Ramp  ramp(int d) const noexcept
        {
            return (d > DstNone && d < DstCount) ? Ramp{ prev[(size_t) d], cur[(size_t) d] } : Ramp{};
        }

        // --- Applicazione ai parametri ---------------------------------------------------------------
        /** base + mod * scale, limitato a [lo, hi]  (valore a fine frame). */
        float applyLinear(int d, float base, float scale, float lo, float hi) const noexcept
        {
            return std::min(std::max(base + value(d) * scale, lo), hi);
        }
        /** Come applyLinear ma restituisce la RAMPA start->end del parametro. */
        Ramp rampLinear(int d, float base, float scale, float lo, float hi) const noexcept
        {
            const Ramp r = ramp(d);
            return { std::min(std::max(base + r.start * scale, lo), hi), std::min(std::max(base + r.end * scale, lo), hi) };
        }
        /** Frequenza: base * 2^(mod * ottave), limitata a [lo, hi] Hz. */
        float applyOctaves(int d, float baseHz, float octaves, float lo, float hi) const noexcept
        {
            return std::min(std::max(baseHz * std::exp2(value(d) * octaves), lo), hi);
        }

    private:
        Table table{};
        std::array<float, SrcCount> src{};
        std::array<float, DstCount> cur{}, prev{};
    };
}
