#include "WavetableOscillator.h"
#include <algorithm>

namespace bf
{
//==============================================================================
// Helper DSP (anonimi, inline)
//==============================================================================
namespace
{
    /** Interpolazione cubica di Hermite (Catmull-Rom) a 4 punti, t in [0,1) tra y0 e y1. */
    inline float hermite(float ym1, float y0, float y1, float y2, float t) noexcept
    {
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    /** Lettura Hermite a 4 punti di una tabella con guardie (-1..N+1). ph in [0,1]. */
    inline float readHermite(const float* tab, float ph) noexcept
    {
        constexpr int N = WavetableBank::kTableSize;
        float pos = ph * (float) N;
        int i = (int) pos;
        if (i >= N) { i -= N; pos -= (float) N; }          // ph == 1.0f per arrotondamento
        const float f = pos - (float) i;
        return hermite(tab[i - 1], tab[i], tab[i + 1], tab[i + 2], f);
    }

    inline float shapeDetune(DetuneCurve c, float a, float k) noexcept
    {
        // a in [0,1] -> [0,1]
        switch (c)
        {
            case DetuneCurve::Logarithmic:  return std::log1p(k * a) / std::log1p(k);   // concava: esterne ravvicinate
            case DetuneCurve::Exponential:  return std::expm1(k * a) / std::expm1(k);   // convessa: interne ravvicinate
            default:                        return a;
        }
    }
}

//==============================================================================
// WavetableOscillator
//==============================================================================
void WavetableOscillator::prepare(double sampleRate, const WavetableBank* bank, std::uint32_t seed)
{
    sr = sampleRate;
    (void) SineLUT::get();                  // forza l'inizializzazione fuori dal thread audio
    setBank(bank);
    rng = seed != 0 ? seed : 0x12345678u;
    morphBank = nullptr;
    morphWt = -1.0f;
    morphMip = -1;
    layoutDirty = true;
    reset();
    setFrequency(baseHz);
}

float WavetableOscillator::nextRand01() noexcept
{
    // xorshift32: nessuna allocazione, nessun lock, deterministico dato il seed
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float) (rng >> 8) * (1.0f / 16777216.0f);
}

void WavetableOscillator::setFrequency(float hz) noexcept
{
    baseHz  = juce::jlimit(1.0f, 0.45f * (float) sr, hz);
    baseInc = (double) baseHz / sr;
}

void WavetableOscillator::setParams(const Params& n) noexcept
{
    // Cambiamenti che richiedono il ricalcolo di detune/pan/gain (costoso: exp2, sin, cos)
    const bool layoutChanged =
           n.unisonVoices != p.unisonVoices || n.detuneCents != p.detuneCents
        || n.detuneCurve != p.detuneCurve   || n.curveShape != p.curveShape
        || n.stereoSpread != p.stereoSpread || n.panJitter != p.panJitter
        || n.blend != p.blend;

    p = n;
    p.unisonVoices = juce::jlimit(1, kMaxUnison, p.unisonVoices);
    p.wtPosition   = juce::jlimit(0.0f, 1.0f, p.wtPosition);
    p.warpAmount   = juce::jlimit(0.0f, 1.0f, p.warpAmount);
    p.warpAux      = juce::jlimit(0.0f, 1.0f, p.warpAux);
    p.stereoSpread = juce::jlimit(0.0f, 1.0f, p.stereoSpread);
    p.panJitter    = juce::jlimit(0.0f, 1.0f, p.panJitter);
    p.blend        = juce::jlimit(0.0f, 1.0f, p.blend);
    p.phaseRandom  = juce::jlimit(0.0f, 1.0f, p.phaseRandom);
    p.curveShape   = juce::jmax(0.01f, p.curveShape);

    if (layoutChanged) layoutDirty = true;
}

void WavetableOscillator::reset() noexcept
{
    phase.fill(0.0);
    panJit.fill(0.0f);
    layoutDirty = true;
}

void WavetableOscillator::retrigger() noexcept
{
    for (int i = 0; i < kMaxUnison; ++i)
    {
        // startPhase + quota casuale scalata da phaseRandom. Con phaseRandom = 0 l'attacco e' sempre identico.
        double ph = (double) p.startPhase + (double) p.phaseRandom * (double) nextRand01();
        phase[(size_t) i] = ph - std::floor(ph);
        panJit[(size_t) i] = nextRand01() * 2.0f - 1.0f;
    }
    layoutDirty = true;     // i jitter di pan cambiano i gain
}

//------------------------------------------------------------------------------
void WavetableOscillator::updateLayout() noexcept
{
    const int N = p.unisonVoices;
    numVoices = N;

    // 1) posizioni normalizzate in [-1,1]
    std::array<float, kMaxUnison> pos{};
    float minAbs = 2.0f;
    for (int i = 0; i < N; ++i)
    {
        pos[(size_t) i] = N == 1 ? 0.0f : 2.0f * (float) i / (float) (N - 1) - 1.0f;
        minAbs = std::min(minAbs, std::abs(pos[(size_t) i]));
    }

    // 2) detune: curva -> cents -> rapporto di frequenza
    maxRatio = 1.0f;
    for (int i = 0; i < N; ++i)
    {
        const float a    = std::abs(pos[(size_t) i]);
        const float sgn  = pos[(size_t) i] < 0.0f ? -1.0f : 1.0f;
        const float cents = sgn * shapeDetune(p.detuneCurve, a, p.curveShape) * p.detuneCents;
        ratio[(size_t) i] = (double) std::exp2(cents * (1.0f / 1200.0f));
        maxRatio = std::max(maxRatio, (float) ratio[(size_t) i]);
    }

    // 3) pesi di blend: voce/i piu' vicine al centro = 1, le altre = blend. Normalizzazione a potenza costante.
    float sumSq = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const bool centre = std::abs(pos[(size_t) i]) <= minAbs + 1.0e-6f;
        weight[(size_t) i] = centre ? 1.0f : p.blend;
        sumSq += weight[(size_t) i] * weight[(size_t) i];
    }
    const float norm = 1.0f / std::sqrt(std::max(sumSq, 1.0e-6f));

    // 4) pan per-voce: lati alternati (decorrela detune e pan), ampiezza ~ |pos|, piu' jitter casuale
    for (int i = 0; i < N; ++i)
    {
        const float side = (i & 1) != 0 ? 1.0f : -1.0f;
        float pan = p.stereoSpread * (side * std::abs(pos[(size_t) i]) + p.panJitter * panJit[(size_t) i]);
        pan = juce::jlimit(-1.0f, 1.0f, pan);

        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;   // pan law a potenza costante
        const float g   = weight[(size_t) i] * norm * 1.41421356f;                 // centro = guadagno unitario
        gainL[(size_t) i] = g * std::cos(ang);
        gainR[(size_t) i] = g * std::sin(ang);
    }

    layoutDirty = false;
}

//------------------------------------------------------------------------------
void WavetableOscillator::ensureMorph(const WavetableBank& bank, float wt, int mip) noexcept
{
    if (morphBank == &bank && morphMip == mip && std::abs(morphWt - wt) < 1.0e-5f)
        return;

    constexpr int S = WavetableBank::kStride;
    const int nf = bank.getNumFrames();

    const float fpos = wt * (float) (nf - 1);
    const int   f1   = std::min((int) fpos, nf - 1);
    const float t    = fpos - (float) f1;
    const int   f0   = std::max(f1 - 1, 0);
    const int   f2   = std::min(f1 + 1, nf - 1);
    const int   f3   = std::min(f1 + 2, nf - 1);

    // getTable() punta al campione 0; la tabella completa con guardie parte da -1.
    const float* A = bank.getTable(f0, mip) - 1;
    const float* B = bank.getTable(f1, mip) - 1;
    const float* C = bank.getTable(f2, mip) - 1;
    const float* D = bank.getTable(f3, mip) - 1;

    if (t <= 1.0e-6f || f1 == f2)
        for (int j = 0; j < S; ++j) morph[(size_t) j] = B[j];
    else
        for (int j = 0; j < S; ++j) morph[(size_t) j] = hermite(A[j], B[j], C[j], D[j], t);   // Hermite TRA i frame

    morphBank = &bank;
    morphMip  = mip;
    morphWt   = wt;
}

//------------------------------------------------------------------------------
template <WarpMode M>
void WavetableOscillator::renderVoices(float* outL, float* outR, int n, float gain, const WarpCtx& w) noexcept
{
    const float* tab = morph.data() + 1;           // tab[-1] valido
    const SineLUT& lut = SineLUT::get();

    for (int v = 0; v < numVoices; ++v)
    {
        double ph = phase[(size_t) v];
        const double inc = baseInc * ratio[(size_t) v];
        const float gl = gainL[(size_t) v] * gain;
        const float gr = gainR[(size_t) v] * gain;
        const float gm = 0.5f * (gl + gr);

        for (int i = 0; i < n; ++i)
        {
            const float f = (float) ph;
            float s;

            if constexpr (M == WarpMode::None)
            {
                s = readHermite(tab, f);
            }
            else if constexpr (M == WarpMode::WindowedFM)
            {
                // Finestra di Hann sul ciclo: nulla a fase 0 e 1 => il ciclo resta continuo anche con indice alto.
                const float win = 0.5f - 0.5f * lut.cosTurns(f);
                const float mod = lut.sinTurns(f * w.fmRatio);               // rapporto intero => periodico
                float pp = f + w.amount * win * mod;                         // deviazione di fase in "giri"
                pp -= std::floor(pp);
                s = readHermite(tab, pp);
            }
            else if constexpr (M == WarpMode::FormantMod)
            {
                const float dry = readHermite(tab, f);
                float fp = f * w.ratio;                                       // legge a k*f0 (picco formantico a k*f0)
                fp -= std::floor(fp);
                float win = 0.5f - 0.5f * lut.cosTurns(f);                    // sin^2(pi*f): zero ai bordi => continuo
                win += w.sharp * (win * win - win);                          // 0 = Hann, 1 = Hann^2 (piu' stretta)
                const float wet = readHermite(tab, fp) * win * w.comp;
                s = dry + w.mix * (wet - dry);
            }
            else // SineFolder
            {
                const float x = readHermite(tab, f);
                const float folded = lut.sinTurns(0.25f * w.g * (x + w.bias)); // sin(pi/2 * g * (x+b))
                s = x + w.mix * (folded - x);
            }

            ph += inc;
            ph -= std::floor(ph);

            if (outR != nullptr) { outL[i] += s * gl; outR[i] += s * gr; }
            else                 { outL[i] += s * gm; }
        }
        phase[(size_t) v] = ph;
    }
}

void WavetableOscillator::renderAdd(float* outL, float* outR, int n, float gain) noexcept
{
    const WavetableBank* bank = bankPtr.load(std::memory_order_acquire);
    if (bank == nullptr || ! bank->isReady() || n <= 0)
        return;

    if (layoutDirty) updateLayout();

    // ---- Warp: costanti di blocco + attenuazione sulle note acute (il warp genera armoniche oltre il mip) ----
    const float incMax = (float) (baseInc * (double) maxRatio);
    const float att = juce::jlimit(0.25f, 1.0f, 0.03f / std::max(incMax, 1.0e-6f));
    const float amt = p.warpAmount * att;

    WarpCtx w;
    int mipBias = 0;
    switch (p.warpMode)
    {
        case WarpMode::WindowedFM:
            w.amount  = amt * 1.0f;                                   // fino a 1 ciclo di deviazione
            w.fmRatio = (float) (1 + (int) (p.warpAux * 7.0f + 0.5f)); // 1..8 intero
            mipBias   = amt > 0.0f ? 1 : 0;
            break;
        case WarpMode::FormantMod:
        {
            w.ratio = 1.0f + amt * 15.0f;                              // 1..16
            w.mix   = juce::jlimit(0.0f, 1.0f, amt * 4.0f);            // amount = 0 => bypass esatto
            w.sharp = p.warpAux;
            const float rmsHann = 0.6124f, rmsHann2 = 0.5229f;         // RMS di sin^2 e sin^4 sul ciclo
            w.comp  = 1.0f / (rmsHann + w.sharp * (rmsHann2 - rmsHann));
            mipBias = amt > 0.0f ? (int) std::ceil(std::log2(w.ratio)) : 0;
            break;
        }
        case WarpMode::SineFolder:
            w.g    = 1.0f + amt * 7.0f;                                // fino a ~3 pieghe
            w.bias = p.warpAux * 0.5f;                                 // asimmetria
            w.mix  = juce::jlimit(0.0f, 1.0f, amt * 6.0f);
            mipBias = amt > 0.0f ? 1 : 0;
            break;
        default: break;
    }

    const int mip = juce::jlimit(0, WavetableBank::kNumMips - 1,
                                 WavetableBank::mipForIncrement(incMax) + mipBias);
    ensureMorph(*bank, p.wtPosition, mip);

    switch (p.warpMode)
    {
        case WarpMode::WindowedFM:  renderVoices<WarpMode::WindowedFM>(outL, outR, n, gain, w); break;
        case WarpMode::FormantMod:  renderVoices<WarpMode::FormantMod>(outL, outR, n, gain, w); break;
        case WarpMode::SineFolder:  renderVoices<WarpMode::SineFolder>(outL, outR, n, gain, w); break;
        default:                    renderVoices<WarpMode::None>(outL, outR, n, gain, w);       break;
    }
}
}
