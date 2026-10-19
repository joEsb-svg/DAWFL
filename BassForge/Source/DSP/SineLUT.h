#pragma once
// =====================================================================================================
//  SineLUT.h - Look-Up Table pre-calcolate per seno e forme d'onda di base.
//
//  Tutto e' header-only, privo di JUCE e privo di allocazioni dopo la costruzione.
//  Le tabelle sono costruite una volta sola (Meyers singleton, thread-safe in C++11):
//  chiamare SineLUT::get() / BasicWaveLUT::get() in prepare() cosi' la costruzione non avviene
//  mai nel thread audio.
//
//  Convenzione di fase: "giri" (turns). t = 1.0 e' un ciclo completo; qualsiasi valore reale e'
//  accettato (negativo, > 1), viene riportato in [0,1). Valori non finiti danno fase 0.
// =====================================================================================================
#include <array>
#include <cmath>
#include <cstddef>

namespace bf
{
    namespace lutdetail
    {
        /** Riporta t in [0,1). NaN/inf -> 0. Gestisce anche l'arrotondamento che darebbe t == 1.0. */
        inline double wrapTurns(double t) noexcept
        {
            t -= std::floor(t);
            return (t >= 0.0 && t < 1.0) ? t : 0.0;
        }

        /** Interpolazione cubica di Catmull-Rom (Hermite a 4 punti) fra y1 e y2, f in [0,1). */
        inline float catmullRom(float y0, float y1, float y2, float y3, float f) noexcept
        {
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
    }

    //==============================================================================================
    /** Tabella sinusoidale a 4096 punti + 3 punti di guardia (uno prima, due dopo) per l'interpolazione
     *  cubica senza rami nel loop.
     *    sinTurns()       lineare : errore massimo ~ 4e-7   (ottima per LFO, warp, wavefolder)
     *    sinTurnsCubic()  cubica  : errore massimo ~ 2e-7   (limitato dalla precisione float, non dall'interpolazione;
     *                               a differenza della lineare e' liscia in derivata: niente armoniche spurie sul sub)
     */
    class SineLUT
    {
    public:
        static constexpr int kSize = 4096;                 // potenza di 2
        static constexpr int kMask = kSize - 1;

        static const SineLUT& get() noexcept { static const SineLUT lut; return lut; }

        /** sin(2*pi*t), interpolazione lineare. */
        float sinTurns(float t) const noexcept { return sinTurns((double) t); }
        float sinTurns(double t) const noexcept
        {
            const double pos = lutdetail::wrapTurns(t) * (double) kSize;
            const int    i   = (int) pos & kMask;
            const float  f   = (float) (pos - std::floor(pos));
            const float* p   = tbl.data() + 1 + i;         // p[0] = sin(i)
            return p[0] + f * (p[1] - p[0]);
        }

        /** cos(2*pi*t), lineare. */
        float cosTurns(float t) const noexcept { return sinTurns((double) t + 0.25); }

        /** sin(2*pi*t), interpolazione cubica (accetta double: fase accurata anche a frequenze basse). */
        float sinTurnsCubic(double t) const noexcept
        {
            const double pos = lutdetail::wrapTurns(t) * (double) kSize;
            const int    i   = (int) pos & kMask;
            const float  f   = (float) (pos - std::floor(pos));
            const float* p   = tbl.data() + 1 + i;
            return lutdetail::catmullRom(p[-1], p[0], p[1], p[2], f);
        }
        float cosTurnsCubic(double t) const noexcept { return sinTurnsCubic(t + 0.25); }

    private:
        SineLUT()
        {
            constexpr double twoPi = 6.283185307179586476925286766559;
            for (int k = -1; k <= kSize + 1; ++k)           // indice tabella = k + 1 (guardie incluse)
                tbl[(std::size_t) (k + 1)] = (float) std::sin(twoPi * (double) k / (double) kSize);
        }
        std::array<float, kSize + 3> tbl{};
    };

    //==============================================================================================
    enum class BasicShape : int { Sine = 0, Triangle, Saw, Square, Count };

    /** Forme d'onda di base a 2048 punti, allineate al seno (tutte partono da 0 e salgono):
     *    Triangle: picco +1 a t=0.25      Saw: rampa -1..+1 con zero a t=0 (salto a t=0.5)
     *    Square: +1 per t in [0,0.5), -1 altrimenti.
     *  ATTENZIONE: Triangle/Saw/Square sono "naive" (NON band-limited): vanno bene per LFO, modulazione
     *  e come tabelle di waveshaping, NON come oscillatori audio ad alta frequenza (per quelli usare
     *  SubOscillator o WavetableOscillator, che sono band-limited). Con Square e Saw usare read()
     *  lineare: l'interpolazione cubica produrrebbe overshoot sui salti. */
    class BasicWaveLUT
    {
    public:
        static constexpr int kSize = 2048;
        static constexpr int kMask = kSize - 1;

        static const BasicWaveLUT& get() noexcept { static const BasicWaveLUT lut; return lut; }

        /** Lettura lineare. */
        float read(BasicShape s, double t) const noexcept
        {
            const double pos = lutdetail::wrapTurns(t) * (double) kSize;
            const int    i   = (int) pos & kMask;
            const float  f   = (float) (pos - std::floor(pos));
            const float* p   = table(s) + 1 + i;
            return p[0] + f * (p[1] - p[0]);
        }

        /** Lettura cubica (consigliata per Sine e Triangle). */
        float readCubic(BasicShape s, double t) const noexcept
        {
            const double pos = lutdetail::wrapTurns(t) * (double) kSize;
            const int    i   = (int) pos & kMask;
            const float  f   = (float) (pos - std::floor(pos));
            const float* p   = table(s) + 1 + i;
            return lutdetail::catmullRom(p[-1], p[0], p[1], p[2], f);
        }

    private:
        static constexpr int kStride = kSize + 3;
        const float* table(BasicShape s) const noexcept { return data.data() + (std::size_t) s * (std::size_t) kStride; }

        static double shapeAt(int shape, double t) noexcept        // t in giri, qualsiasi reale
        {
            t -= std::floor(t);
            switch ((BasicShape) shape)
            {
                case BasicShape::Sine:     return std::sin(6.283185307179586476925286766559 * t);
                case BasicShape::Triangle: return t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
                case BasicShape::Saw: { double u = t + 0.5; u -= std::floor(u); return 2.0 * u - 1.0; }
                case BasicShape::Square:   return t < 0.5 ? 1.0 : -1.0;
                default:                   return 0.0;
            }
        }

        BasicWaveLUT()
        {
            for (int s = 0; s < (int) BasicShape::Count; ++s)
                for (int k = -1; k <= kSize + 1; ++k)
                    data[(std::size_t) (s * kStride + k + 1)] = (float) shapeAt(s, (double) k / (double) kSize);
        }
        std::array<float, (std::size_t) BasicShape::Count * (std::size_t) (kSize + 3)> data{};
    };
}
