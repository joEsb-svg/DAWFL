#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "Simd.h"

namespace bf
{
    //==========================================================================
    enum class FilterType : int
    {
        LP12 = 0, LP24, LP48,        // ZDF (TPT-SVF) passa-basso a 12/24/48 dB/ott
        HP12, HP24, HP48,            // ZDF passa-alto
        Comb,                        // comb a feedback +/-, con pitch tracking
        Vowel                        // banco di 4 formanti (A-E-I-O-U morphabili)
    };

    enum class FilterRouting : int { Serial = 0, Parallel, Morph };

    /**
     * Dual Filter Engine STEREO per UNA voce (senza allocazioni dopo prepare()).
     *
     * ----- Architettura SIMD (4 lane float; SSE2 / NEON / scalare, vedi Simd.h) -----
     *  ZDF 12/24/48 : i 4 stadi biquad-SVF Butterworth stanno nei 4 LANE di un registro, in PIPELINE:
     *                 lo stadio s riceve l'uscita dello stadio s-1 del campione precedente. Una cascata di
     *                 sistemi LTI con ritardi di 1 campione in mezzo ha lo STESSO modulo di risposta
     *                 (cambia solo la latenza: <= 3 campioni), ma un campione costa UNA sola passata vettoriale
     *                 invece di 4 passate scalari in dipendenza seriale.
     *  Vowel        : i 4 formanti (F1..F4) sono 4 band-pass SVF in parallelo, uno per lane.
     *  Comb         : l'anello di feedback ha ritardo D >= 5 campioni => 4 campioni consecutivi sono
     *                 indipendenti e si calcolano insieme (4 tap FIR frazionari + damping, letti con loadu da
     *                 un buffer circolare "doppio" senza wrap).
     *  Nonlinearita': drive (soft-clip cubico) e limiter a ginocchio morbido sono polinomiali (no tanh, no divisioni).
     *
     * ----- Feedback Loop + Limiter -----
     *  ZDF/Vowel: x' = x + fb * y_prev (y_prev = uscita GIA' limitata) => risonanze estreme / auto-oscillazione
     *  restano confinate; ogni unita' chiude con softLimit() (|y| <= 1). Comb: fb e' il feedback del comb
     *  (con soft-clip nel ciclo e limiter in uscita); fb negativo = comb "dispari" (voce cava/ottava sotto).
     *
     * ----- Routing -----
     *  Serial   : F1 -> F2.   Parallel: (F1 + F2) / 2.   Morph: crossfade lineare F1 -> F2 con `morph` 0..1
     *  (un filtro spento vale "bypass" nel Morph e nel Serial, "assente" nel Parallel).
     *
     * I parametri si impostano a ogni blocco di controllo con setParams(); durante process() vengono
     * interpolati (cutoff in scala logaritmica) a passi di kSlice campioni: niente zipper noise.
     */
    class DualFilterEngine
    {
    public:
        static constexpr int kMaxBlock = 64;
        static constexpr int kSlice    = 8;     // granularita' di aggiornamento dei coefficienti (multiplo di 4)

        struct FilterParams
        {
            bool       on         = true;
            FilterType type       = FilterType::LP24;
            float      cutoffHz   = 1000.0f;    // Comb: frequenza libera quando pitchTrack = 0
            float      resonance  = 0.2f;       // 0..1 (ZDF: Q dell'ultimo stadio; Vowel: Q dei formanti; Comb: brillantezza)
            float      drive      = 0.0f;       // 0..1 saturazione in ingresso
            float      feedback   = 0.0f;       // -1..1
            float      pitchTrack = 0.0f;       // 0..1  (solo Comb) 1 = la frequenza del comb segue la nota
            float      tuneSemis  = 0.0f;       // -24..24 (solo Comb) intonazione rispetto alla nota quando pitchTrack > 0
            float      vowel      = 0.0f;       // 0..1  (solo Vowel) A-E-I-O-U
            float      formantShift = 0.0f;     // -12..12 semitoni (solo Vowel)
        };

        struct Params
        {
            std::array<FilterParams, 2> f {};
            FilterRouting routing = FilterRouting::Serial;
            float morph  = 0.5f;                // 0 = solo F1, 1 = solo F2 (routing Morph)
            float noteHz = 110.0f;              // per il pitch tracking del Comb (bend/mod inclusi)
        };

        void prepare(double sampleRate, int maxBlockSize);   // NON real-time (alloca i buffer dei comb)
        void reset() noexcept;

        /** Target per il PROSSIMO process(): i valori vengono interpolati lungo il blocco. */
        void setParams(const Params& p) noexcept;

        /** In-place, n <= kMaxBlock. */
        void process(float* L, float* R, int n) noexcept;

        static const char* simdBackend() noexcept { return simd::kBackendName; }

    private:
        //----------------------------------------------------------------------
        struct ZdfUnit
        {
            simd::f4 a1{}, a2{}, a3{}, m0{}, m1{}, m2{};
            simd::f4 ic1[2]{}, ic2[2]{}, prev[2]{};
            float    fbState[2] = { 0.f, 0.f };
            int      tap = 0;

            void reset() noexcept;
            void configure(FilterType t, float cutoffHz, float res, double sr) noexcept;
            void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                         float fb, float drive) noexcept;
            template <int Tap>
            void run(const float* inL, const float* inR, float* outL, float* outR, int n,
                     float fb, float dg, float dnorm, bool driveOn) noexcept;
        };

        struct VowelUnit
        {
            simd::f4 a1{}, a2{}, a3{}, gk{};
            simd::f4 ic1[2]{}, ic2[2]{};
            float    fbState[2] = { 0.f, 0.f };

            void reset() noexcept;
            void configure(float vowel01, float shiftSemis, float res, double sr) noexcept;
            void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                         float fb, float drive) noexcept;
        };

        struct CombUnit
        {
            static constexpr int kN = 16384;                 // storia per canale (potenza di 2, multiplo di 4)
            std::vector<float> hist[2];                      // 2*kN + 16 (scrittura doppia => lettura senza wrap)
            int   wp = 0;
            float t0 = 0, t1 = 1, t2 = 0, t3 = 0;            // pesi dei 4 tap a ritardo D0-1, D0, D0+1, D0+2
            int   d0 = 8;
            float outGain = 1.0f;

            void allocate();
            void reset() noexcept;
            void configure(float freqHz, float fb, float brightness, double sr) noexcept;
            void process(const float* inL, const float* inR, float* outL, float* outR, int n,
                         float fb, float drive) noexcept;
        };

        struct Unit
        {
            ZdfUnit zdf; VowelUnit vowel; CombUnit comb;
            FilterType activeType = FilterType::LP24;
            bool wasOn = false;

            void resetAll() noexcept { zdf.reset(); vowel.reset(); comb.reset(); }
        };

        void runUnit(int idx, const FilterParams& p, float noteHz,
                     const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

        double sr = 44100.0;
        std::array<Unit, 2> units;
        Params cur{}, tgt{};
        bool   snap = true;

        // scratch per Parallel/Morph (una slice)
        std::array<float, kSlice> aL{}, aR{}, bL{}, bR{};
    };
}
