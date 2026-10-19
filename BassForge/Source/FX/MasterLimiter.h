#pragma once
// =====================================================================================================
//  MasterLimiter - limiter brickwall stereo con lookahead, ultimo stadio della catena master.
//
//  Perche': il synth non ha altrimenti alcun controllo sul livello finale. Un accordo reese a piu' voci, o
//  il Sub in Direct-Out sommato dopo gli FX, supera facilmente 0 dBFS e il convertitore/host satura.
//
//  Algoritmo (peak limiter "min-filter + media mobile"), A = kWindow campioni:
//    1. g_target[n] = min(1, ceiling / max(|L|,|R|))            guadagno necessario per il campione n
//    2. m[n]   = min(g_target[n-A+1 .. n])                       minimo sulla finestra
//    3. rel[n] = m[n] se scende (attacco istantaneo), altrimenti recupero esponenziale (release)
//    4. g[n]   = media(rel[n-A+1 .. n])                          rampa liscia: niente click, niente "zipper"
//    5. out[n] = in[n-D] * g[n]   con D = A-1
//  Con D = A-1 ogni campione di picco p e' moltiplicato da una media di valori TUTTI <= g_target[p]:
//  il picco in uscita e' garantito <= ceiling (nessun overshoot), con rampa di attacco di A campioni.
//  Guadagno linkato L/R (immagine stereo intatta). Sotto soglia il guadagno e' 1: segnale identico, ritardato.
//
//  LATENZA: costante, kLatency campioni, anche con il limiter disattivato (altrimenti cambierebbe la
//  latenza dichiarata all'host). Nessuna allocazione, nessun lock; NaN/inf in ingresso -> 0.
// =====================================================================================================
#include <algorithm>
#include <array>
#include <cmath>

namespace bf
{
    class MasterLimiter
    {
    public:
        static constexpr int kWindow  = 33;
        static constexpr int kLatency = kWindow - 1;          // 32 campioni (~0.73 ms a 44.1 kHz)

        void prepare(double sampleRate) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 44100.0;
            relCoef = 1.0f - (float) std::exp(-1.0 / (0.12 * sr));    // release 120 ms
            reset();
        }

        void reset() noexcept
        {
            dl.fill(0.0f); dr.fill(0.0f); gt.fill(1.0f); gr.fill(1.0f);
            w = 0; rel = 1.0f; sum = (double) kWindow; lastGain = 1.0f;
        }

        /** Elabora in-place `n` campioni stereo. `ceiling` lineare (es. 0.966 = -0.3 dBFS). */
        void process(float* L, float* R, int n, float ceiling, bool enabled) noexcept
        {
            ceiling = std::max(0.01f, std::min(ceiling, 1.0f));
            float minGain = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                float xl = L[i], xr = R[i];
                if (! (xl == xl) || std::abs(xl) > 1.0e6f) xl = 0.0f;        // NaN / inf / valori assurdi
                if (! (xr == xr) || std::abs(xr) > 1.0e6f) xr = 0.0f;

                const float pk = std::max(std::abs(xl), std::abs(xr));
                const float target = (enabled && pk > ceiling) ? ceiling / pk : 1.0f;
                gt[(size_t) w] = target;

                float m = 1.0f;                                              // minimo sulle ultime kWindow richieste
                for (int k = 0; k < kWindow; ++k) m = std::min(m, gt[(size_t) ((w - k) & kMask)]);

                rel = m < rel ? m : rel + (m - rel) * relCoef;               // attacco istantaneo, rilascio lento

                const int old = (w - kWindow) & kMask;
                sum += (double) rel - (double) gr[(size_t) old];
                gr[(size_t) w] = rel;
                const float g = (float) (sum * (1.0 / (double) kWindow));

                dl[(size_t) w] = xl; dr[(size_t) w] = xr;
                const int rd = (w - kLatency) & kMask;
                float ol = dl[(size_t) rd] * g, orr = dr[(size_t) rd] * g;
                if (enabled) { ol = std::max(-ceiling, std::min(ol, ceiling)); orr = std::max(-ceiling, std::min(orr, ceiling)); }
                L[i] = ol; R[i] = orr;

                minGain = std::min(minGain, g);
                w = (w + 1) & kMask;
            }
            lastGain = minGain;
        }

        /** Massima riduzione di guadagno nell'ultimo blocco (1 = nessuna), per un eventuale meter. */
        float lastBlockMinGain() const noexcept { return lastGain; }

    private:
        static constexpr int kSize = 64, kMask = kSize - 1;
        static_assert(kSize > kWindow, "buffer circolare troppo piccolo");

        double sr = 44100.0;
        float  relCoef = 0.001f, rel = 1.0f, lastGain = 1.0f;
        double sum = (double) kWindow;
        int    w = 0;
        std::array<float, kSize> dl{}, dr{}, gt{}, gr{};
    };
}
