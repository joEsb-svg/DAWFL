#include "SubOscillator.h"
#include <algorithm>
#include <cmath>

namespace bf
{
namespace
{
    // Residuo PolyBLEP per un salto di altezza J, n = distanza dal salto in campioni (n<0 prima, n>=0 dopo).
    inline double blep(double n, double J) noexcept
    {
        if (n <= -1.0 || n >= 1.0) return 0.0;
        return n < 0.0 ?  0.5 * J * (1.0 + n) * (1.0 + n)
                       : -0.5 * J * (1.0 - n) * (1.0 - n);
    }

    // Residuo PolyBLAMP (integrale del BLEP) per un cambio di pendenza di `dSlope` (valore/campione).
    inline double blamp(double n, double dSlope) noexcept
    {
        if (n <= -1.0 || n >= 1.0) return 0.0;
        const double a = n < 0.0 ? 1.0 + n : 1.0 - n;
        return dSlope * a * a * a * (1.0 / 6.0);
    }

    // Distanza di fase da un evento a tc, in [-0.5, 0.5).
    inline double dist(double t, double tc) noexcept
    {
        double d = t - tc;
        return d - std::floor(d + 0.5);
    }
}

void SubOscillator::prepare(double sampleRate) noexcept
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    (void) SineLUT::get();
    levelCoef = 1.0f - (float) std::exp(-1.0 / (0.005 * sr));
    levelPrimed = false;
    reset();
    setParams(p);
}

void SubOscillator::setParams(const Params& np) noexcept
{
    p = np;
    p.octave     = std::max(-6, std::min(2, p.octave));
    p.level      = std::max(0.0f, std::min(1.0f, p.level));
    p.startPhase = p.startPhase - std::floor(p.startPhase);
    ratio        = std::ldexp(1.0, p.octave);
    if (! levelPrimed) { levelSm = p.level; levelPrimed = true; }
}

void SubOscillator::reset() noexcept
{
    ph = 0.0;
    levelSm = p.level;
}

void SubOscillator::noteOn() noexcept
{
    if (p.phaseMode == SubPhaseMode::Reset)
        ph = p.startPhase;
    levelSm = p.level;
}

float SubOscillator::tick() noexcept
{
    const double inc = refInc * ratio;
    const double dt  = std::min(inc, 0.5);          // sopra 0.5 cicli/campione il BLEP non ha senso (aliasing inevitabile)
    const double t   = ph;
    double y;

    switch (p.wave)
    {
        case SubWave::Sine:
            y = (double) SineLUT::get().sinTurnsCubic(t);
            break;

        case SubWave::Triangle:
        {
            // 0 -> +1 (t=.25) -> -1 (t=.75) -> 0. Spigoli: t=.25 (pendenza +4 -> -4), t=.75 (-4 -> +4), per giro.
            y = t < 0.25 ? 4.0 * t : (t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
            if (dt > 0.0)
                y += blamp(dist(t, 0.25) / dt, -8.0 * dt) + blamp(dist(t, 0.75) / dt, 8.0 * dt);
            break;
        }

        case SubWave::Square:
            y = t < 0.5 ? 1.0 : -1.0;
            if (dt > 0.0)
                y += blep(dist(t, 0.0) / dt, 2.0) + blep(dist(t, 0.5) / dt, -2.0);
            break;

        case SubWave::Saw:
        default:
        {
            double u = t + 0.5; u -= std::floor(u);
            y = 2.0 * u - 1.0;
            if (dt > 0.0)
                y += blep(dist(t, 0.5) / dt, -2.0);
            break;
        }
    }

    levelSm += (p.level - levelSm) * levelCoef;

    ph += inc;
    ph -= std::floor(ph);
    return (float) y * levelSm;
}

void SubOscillator::renderRouted(float* toFilterChain, float* toDirectBus, int n) noexcept
{
    float* dst = p.directOut ? (toDirectBus != nullptr ? toDirectBus : toFilterChain)
                             : (toFilterChain != nullptr ? toFilterChain : toDirectBus);
    if (dst == nullptr) { for (int i = 0; i < n; ++i) (void) tick(); return; }   // la fase avanza comunque
    for (int i = 0; i < n; ++i) dst[i] += tick();
}
}
