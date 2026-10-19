#pragma once
// =====================================================================================================
//  DirectBus.h - routing interno leggero per i flussi audio "separati" del synth.
//
//  Contiene tre strumenti, tutti pre-allocati (niente heap nel thread audio) e privi di JUCE:
//
//   1. DirectBus      : N corsie stereo (Lane) alle quali le voci SOMMANO il loro segnale (es. il Sub in
//                       Direct-Out). Il processor poi le somma al master dopo filtri e FX, oppure le legge
//                       come ingresso di sidechain. Un blocco audio e' elaborato da UN solo thread, quindi
//                       le corsie sono semplici buffer (nessun atomic necessario, nessun costo).
//   2. FeedbackTap    : registro di ritardo a campione singolo/N campioni per i feedback loop dei filtri
//                       (rende il loop esplicito: y[n] dipende da u[n-D], D >= 1, quindi sempre stabile
//                       da calcolare senza "delay-free loop").
//   3. SpscRing<T,N>  : coda lock-free single-producer/single-consumer (audio thread -> GUI, o viceversa)
//                       per meter, scope e messaggi; push/pop wait-free.
//
//  Ciclo di vita: prepare() dal message thread, poi nel thread audio solo clear()/add*()/mix*().
// =====================================================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace bf
{
    //==========================================================================================
    /** Corsie del bus. SubDirect e' quella usata dal Sub in Direct-Out. */
    enum class Lane : int { SubDirect = 0, Aux1, Aux2, Aux3, Count };

    /** Vista POD (non proprietaria) su una corsia: due puntatori allo stesso blocco. */
    struct DirectBusView
    {
        float* l = nullptr;
        float* r = nullptr;
        bool valid() const noexcept { return l != nullptr && r != nullptr; }
    };

    class DirectBus
    {
    public:
        static constexpr int kNumLanes = (int) Lane::Count;

        /** NON real-time: alloca tutte le corsie per blocchi fino a maxBlockSize campioni. */
        void prepare(int maxBlockSize)
        {
            cap = std::max(1, maxBlockSize);
            storage.assign((std::size_t) kNumLanes * 2u * (std::size_t) cap, 0.0f);
            cleared = 0;
        }

        int capacity() const noexcept { return cap; }

        /** Azzera le prime `n` posizioni di tutte le corsie (da chiamare a inizio blocco). */
        void clear(int n) noexcept
        {
            n = std::min(n, cap);
            for (int k = 0; k < kNumLanes; ++k)
            {
                std::memset(ptr(k, 0), 0, sizeof(float) * (std::size_t) n);
                std::memset(ptr(k, 1), 0, sizeof(float) * (std::size_t) n);
            }
            cleared = n;
        }

        /** Vista su una corsia. Se il blocco e' piu' grande della capacita' ritorna una vista non valida:
         *  il chiamante deve ripiegare sul routing normale (mai scrivere fuori buffer). */
        DirectBusView view(Lane lane, int numSamples) noexcept
        {
            if (numSamples > cap || numSamples > cleared || storage.empty()) return {};
            return { ptr((int) lane, 0), ptr((int) lane, 1) };
        }

        /** Somma una sorgente mono su entrambi i canali (opz. gain). */
        void addMono(Lane lane, const float* src, int n, float gain = 1.0f) noexcept
        {
            auto v = view(lane, n); if (! v.valid() || src == nullptr) return;
            for (int i = 0; i < n; ++i) { const float s = src[i] * gain; v.l[i] += s; v.r[i] += s; }
        }

        void addStereo(Lane lane, const float* srcL, const float* srcR, int n, float gain = 1.0f) noexcept
        {
            auto v = view(lane, n); if (! v.valid() || srcL == nullptr || srcR == nullptr) return;
            for (int i = 0; i < n; ++i) { v.l[i] += srcL[i] * gain; v.r[i] += srcR[i] * gain; }
        }

        /** Somma la corsia in due buffer di destinazione (es. l'uscita master dopo la catena FX). */
        void mixInto(Lane lane, float* dstL, float* dstR, int n, float gain = 1.0f) const noexcept
        {
            if (n > cap || n > cleared || storage.empty()) return;
            const float* l = ptr((int) lane, 0); const float* r = ptr((int) lane, 1);
            if (dstL != nullptr) for (int i = 0; i < n; ++i) dstL[i] += l[i] * gain;
            if (dstR != nullptr) for (int i = 0; i < n; ++i) dstR[i] += r[i] * gain;
        }

        const float* read(Lane lane, int channel) const noexcept { return ptr((int) lane, channel); }

    private:
        float*       ptr(int lane, int ch) noexcept       { return storage.data() + ((std::size_t) lane * 2u + (std::size_t) ch) * (std::size_t) cap; }
        const float* ptr(int lane, int ch) const noexcept { return storage.data() + ((std::size_t) lane * 2u + (std::size_t) ch) * (std::size_t) cap; }

        std::vector<float> storage;     // un unico blocco contiguo: [lane][canale][campioni]
        int cap = 0, cleared = 0;
    };

    //==========================================================================================
    /** Ritardo di feedback di D campioni (1 <= D <= MaxDelay) su un canale. process(x) restituisce il
     *  valore scritto D campioni fa e memorizza x: usalo per chiudere il loop del filtro con
     *  y = f(in + fb * tap.process(y)). Potenza di 2 per l'indirizzamento senza modulo. */
    template <int MaxDelay = 64>
    class FeedbackTap
    {
        static_assert((MaxDelay & (MaxDelay - 1)) == 0 && MaxDelay >= 2, "MaxDelay deve essere una potenza di 2");
    public:
        void setDelay(int d) noexcept { delay = std::max(1, std::min(d, MaxDelay - 1)); }
        void reset() noexcept         { buf.fill(0.0f); w = 0; }

        float process(float x) noexcept
        {
            const float y = buf[(std::size_t) ((w - delay) & (MaxDelay - 1))];
            buf[(std::size_t) w] = x;
            w = (w + 1) & (MaxDelay - 1);
            return y;
        }

    private:
        std::array<float, (std::size_t) MaxDelay> buf{};
        int w = 0, delay = 1;
    };

    //==========================================================================================
    /** Coda SPSC lock-free a capacita' fissa N (potenza di 2). Un solo produttore e un solo consumatore.
     *  push() ritorna false se piena (il dato viene scartato: nel thread audio non si aspetta mai). */
    template <typename T, std::size_t N = 1024>
    class SpscRing
    {
        static_assert((N & (N - 1)) == 0 && N >= 2, "N deve essere una potenza di 2");
    public:
        bool push(const T& v) noexcept
        {
            const auto h = head.load(std::memory_order_relaxed);
            if (h - tail.load(std::memory_order_acquire) == N) return false;
            slots[h & (N - 1)] = v;
            head.store(h + 1, std::memory_order_release);
            return true;
        }

        bool pop(T& out) noexcept
        {
            const auto t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) return false;
            out = slots[t & (N - 1)];
            tail.store(t + 1, std::memory_order_release);
            return true;
        }

        std::size_t size() const noexcept { return head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire); }

    private:
        std::array<T, N> slots{};
        alignas(64) std::atomic<std::size_t> head { 0 };     // scritto solo dal produttore
        alignas(64) std::atomic<std::size_t> tail { 0 };     // scritto solo dal consumatore
    };
}
