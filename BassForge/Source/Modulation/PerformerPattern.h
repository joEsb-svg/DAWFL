#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "ModCurve.h"

namespace bf
{
    /** Interpolazione dello step i verso lo step successivo (su tutta la durata dello step). */
    enum class StepInterp : int { Linear = 0, Exponential, Step, Custom, Count };

    /** Modalita' di riproduzione. */
    enum class PlayMode : int
    {
        Forward = 0,    // loop 0 -> N-1, l'ultimo step interpola verso lo step 0
        PingPong,       // loop 0 -> N-1 -> 0 (estremi visitati una sola volta, periodo = 2(N-1) step)
        OneShot,        // una sola passata (N step; l'ultimo e' tenuto), poi tiene l'ultimo valore o va a 0
        Count
    };

    /**
     * Dati del Performer: 1..32 step. Per ogni step
     *   value  : -1..1
     *   interp : Linear | Exponential | Step | Custom  (come si va da questo step al successivo
     *            nell'ordine di riproduzione: in Ping-Pong al ritorno e' lo step precedente)
     *   curve  : -1..1, tensione della curva (solo Custom): >0 esponenziale, <0 logaritmica, 0 lineare
     *
     * Tutto atomic<float/int> (relaxed): la UI scrive, il thread audio legge, senza lock ne' allocazioni.
     * evaluate() e' la funzione pura (posizione in step -> valore) usata sia dal motore audio sia dalla UI
     * per disegnare la forma d'onda.
     */
    struct PerformerPattern
    {
        static constexpr int kMaxSteps = 32;
        static constexpr float kExpAmount = 0.6f;       // forza dell'interpolazione Exponential

        std::array<std::atomic<float>, kMaxSteps> value{}, curve{};
        std::array<std::atomic<int>, kMaxSteps>   interp{};
        std::atomic<int> length { 16 };

        PerformerPattern() noexcept
        {
            for (int i = 0; i < kMaxSteps; ++i)
                setStep(i, -1.0f + 2.0f * (float) i / (float) (kMaxSteps - 1), StepInterp::Step, 0.0f);
        }
        PerformerPattern(const PerformerPattern&) = delete;
        PerformerPattern& operator=(const PerformerPattern&) = delete;

        void setLength(int n) noexcept  { length.store(std::min(std::max(n, 1), kMaxSteps), std::memory_order_relaxed); }
        int  getLength() const noexcept { return std::min(std::max(length.load(std::memory_order_relaxed), 1), kMaxSteps); }

        void setStep(int i, float v, StepInterp m = StepInterp::Step, float c = 0.0f) noexcept
        {
            if (i < 0 || i >= kMaxSteps) return;
            value[(size_t) i].store(std::min(std::max(v, -1.0f), 1.0f), std::memory_order_relaxed);
            interp[(size_t) i].store((int) m, std::memory_order_relaxed);
            curve[(size_t) i].store(std::min(std::max(c, -1.0f), 1.0f), std::memory_order_relaxed);
        }
        float      getValue(int i) const noexcept  { return value[(size_t) i].load(std::memory_order_relaxed); }
        float      getCurve(int i) const noexcept  { return curve[(size_t) i].load(std::memory_order_relaxed); }
        StepInterp getInterp(int i) const noexcept
        {
            const int m = interp[(size_t) i].load(std::memory_order_relaxed);
            return (m >= 0 && m < (int) StepInterp::Count) ? (StepInterp) m : StepInterp::Step;
        }

        /** Lunghezza del periodo in step per la modalita' data. */
        double periodSteps(PlayMode mode) const noexcept
        {
            const int n = getLength();
            return (mode == PlayMode::PingPong && n >= 2) ? 2.0 * (double) (n - 1) : (double) n;
        }

        /** Forma di transizione t in [0,1] -> [0,1] (estremi fissi, monotona). */
        static float shape(StepInterp m, float t, float c) noexcept
        {
            switch (m)
            {
                case StepInterp::Linear:      return t;
                case StepInterp::Exponential: return curveWarp(t, kExpAmount);
                case StepInterp::Custom:      return curveWarp(t, c);
                default:                      return 0.0f;           // Step: tiene v0 fino al prossimo step
            }
        }

        /**
         * Valore alla posizione x (in STEP dall'inizio del pattern). Real-time safe, senza stato.
         * Forward/PingPong: x viene riportato nel periodo. OneShot: x >= N => valore finale (o 0 se !endHold).
         */
        float evaluate(double x, PlayMode mode, bool endHold = true) const noexcept
        {
            const int n = getLength();
            if (x < 0.0) x = 0.0;
            int i; float t; int j;                       // segmento i -> j
            switch (mode)
            {
                case PlayMode::OneShot:
                    if (x >= (double) n) return endHold ? getValue(n - 1) : 0.0f;
                    i = (int) x; t = (float) (x - (double) i);
                    if (i >= n - 1) return getValue(n - 1);          // ultimo step: tenuto per la sua durata
                    j = i + 1;
                    break;
                case PlayMode::PingPong:
                {
                    // Sequenza di step visitati: 0,1,..,N-1,N-2,..,1 (periodo 2(N-1), ogni step dura UNO step).
                    // L'interpolazione dello step visitato vale "verso il prossimo step nell'ordine di riproduzione".
                    if (n < 2) return getValue(0);
                    const int P = 2 * (n - 1);
                    x -= (double) P * std::floor(x / (double) P);
                    const int s = std::min((int) x, P - 1);
                    t = (float) (x - (double) s);
                    if (s < n) { i = s; j = s < n - 1 ? i + 1 : i - 1; }     // andata (e vertice N-1 -> N-2)
                    else       { i = P - s; j = i - 1; }                     // ritorno
                    break;
                }
                default:
                {
                    const double P = (double) n;
                    x -= P * std::floor(x / P);
                    i = std::min((int) x, n - 1); t = (float) (x - (double) i); j = i + 1 < n ? i + 1 : 0;
                    break;
                }
            }
            t = std::min(std::max(t, 0.0f), 1.0f);
            const float v0 = getValue(i), v1 = getValue(j);
            return v0 + (v1 - v0) * shape(getInterp(i), t, getCurve(i));
        }
    };

    /** I pattern dei kNumLfo modulatori (posseduti dal processor, letti da tutte le voci). */
    template <int N>
    struct PerformerBankT { std::array<PerformerPattern, (size_t) N> p; };
}
