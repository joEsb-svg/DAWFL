#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "Wavetable.h"
#include "SineLUT.h"

namespace bf
{
    //==========================================================================
    /** Algoritmi di warping spettrale applicati all'oscillatore. */
    enum class WarpMode : int
    {
        None = 0,
        WindowedFM,    // PM/FM con modulatore sinusoidale a rapporto intero, finestrato (Hann) sul ciclo
        FormantMod,    // formante a fase sincrona: legge la tabella a rapporto k*f0 sotto una finestra
        SineFolder     // wavefolder sinusoidale (sin(pi/2 * g * (x + bias)))
    };

    /** Distribuzione del detune tra le micro-voci. */
    enum class DetuneCurve : int
    {
        Linear = 0,    // voci equispaziate in cents
        Logarithmic,   // concava: le voci interne si allargano subito, quelle esterne restano ravvicinate
        Exponential    // convessa: voci interne molto vicine, esterne molto distanti (spread "a coda")
    };

    //==========================================================================
    /**
     * Oscillatore wavetable con Unison (fino a 16 micro-voci) e Warping.
     *
     * Pipeline per blocco (tutto senza allocazioni):
     *   1. mip level scelto sulla micro-voce piu' acuta (+ bias se il warp allarga lo spettro)
     *   2. morph 3D: i 4 frame attorno a wtPosition vengono interpolati (Hermite cubico) in UNA
     *      tabella di lavoro (cache: viene ricalcolata solo se cambiano wtPosition/mip/banco)
     *   3. per ogni micro-voce: fase a 64 bit -> warp -> lettura Hermite a 4 punti sulla tabella
     *      -> accumulo stereo con gain/pan costanti-potenza.
     *
     * Uso tipico (per ogni voce di polifonia, per ogni blocco di controllo ~32-64 campioni):
     *     osc.setFrequency(hz); osc.setParams(p); osc.renderAdd(L, R, n, level);
     * e al note-on: osc.retrigger();
     *
     * Thread: setBank() e' l'unico metodo chiamabile da un altro thread (atomico). Tutto il resto
     * va chiamato dal thread audio (o prima che parta). prepare() NON e' real-time.
     */
    class WavetableOscillator
    {
    public:
        static constexpr int kMaxUnison = 16;

        struct Params
        {
            // --- Wavetable 3D ---
            float wtPosition   = 0.0f;                 // 0..1 attraverso i frame

            // --- Warp ---
            WarpMode warpMode  = WarpMode::None;
            float warpAmount   = 0.0f;                 // 0..1  (indice FM / rapporto formante / profondita' fold)
            float warpAux      = 0.0f;                 // 0..1  (rapporto FM / forma finestra formante / bias fold)

            // --- Unison ---
            int   unisonVoices = 1;                    // 1..16
            float detuneCents  = 0.0f;                 // detune della voce piu' esterna (cents, per lato)
            DetuneCurve detuneCurve = DetuneCurve::Linear;
            float curveShape   = 4.0f;                 // "k" per le curve Log/Exp (>0)
            float stereoSpread = 0.0f;                 // 0..1  apertura stereo
            float panJitter    = 0.0f;                 // 0..1  scostamento casuale per-voce (fissato al retrigger)
            float blend        = 1.0f;                 // 0 = solo voce/i centrale/i ... 1 = tutte allo stesso livello

            // --- Fase ---
            float phaseRandom  = 0.0f;                 // 0 = tutte le voci partono da startPhase, 1 = fase casuale
            float startPhase   = 0.0f;                 // 0..1
        };

        /** NON real-time. Inizializza LUT, seme PRNG e banco. */
        void prepare(double sampleRate, const WavetableBank* bank, std::uint32_t seed = 0x9E3779B9u);

        /** Thread-safe. Il banco deve restare vivo finche' il thread audio puo' usarlo. */
        void setBank(const WavetableBank* b) noexcept { bankPtr.store(b, std::memory_order_release); }

        void setFrequency(float hz) noexcept;
        void setParams(const Params& newParams) noexcept;

        /** Al note-on: riparte le fasi (con randomizzazione) e rigenera i jitter di pan. */
        void retrigger() noexcept;
        void reset() noexcept;

        /**
         * Somma `numSamples` campioni in outL/outR (moltiplicati per `gain`). outR puo' essere nullptr (mono:
         * viene sommata la media dei due canali). Il comportamento e' costante entro la chiamata: i parametri
         * si aggiornano tra una chiamata e l'altra (control rate).
         */
        void renderAdd(float* outL, float* outR, int numSamples, float gain = 1.0f) noexcept;

        int getNumUnisonVoices() const noexcept { return numVoices; }

    private:
        struct WarpCtx
        {
            float amount = 0, fmRatio = 1, ratio = 1, mix = 0, comp = 1, sharp = 0, g = 1, bias = 0;
        };

        void  updateLayout() noexcept;
        void  ensureMorph(const WavetableBank& bank, float wt, int mip) noexcept;
        float nextRand01() noexcept;

        template <WarpMode M>
        void renderVoices(float* outL, float* outR, int n, float gain, const WarpCtx& w) noexcept;

        double sr = 44100.0;
        std::atomic<const WavetableBank*> bankPtr { nullptr };

        Params p;
        bool   layoutDirty = true;

        float  baseHz  = 110.0f;
        double baseInc = 110.0 / 44100.0;

        int    numVoices = 1;
        float  maxRatio  = 1.0f;
        std::array<double, kMaxUnison> phase{};
        std::array<double, kMaxUnison> ratio{};     // moltiplicatore di frequenza per micro-voce
        std::array<float,  kMaxUnison> gainL{}, gainR{};
        std::array<float,  kMaxUnison> panJit{};
        std::array<float,  kMaxUnison> weight{};    // peso blend (senza pan)

        // Tabella di lavoro per il morph 3D (stesso layout con guardie: indici -1..N+1)
        std::array<float, WavetableBank::kStride> morph{};
        const WavetableBank* morphBank = nullptr;
        float morphWt = -1.0f;
        int   morphMip = -1;

        std::uint32_t rng = 0x12345678u;
    };
}
