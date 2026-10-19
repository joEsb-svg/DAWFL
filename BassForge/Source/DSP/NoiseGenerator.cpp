#include "NoiseGenerator.h"
#include <algorithm>
#include <cmath>

namespace bf
{
namespace
{
    constexpr float kNoiseGain = 0.5f;     // noiseLevel = 1 -> bianco uniforme +-0.5 (lascia headroom nella voce)
    constexpr float kPinkCal   = 0.333f;   // calibrazione: RMS(rosa) ~ RMS(bianco) con ingresso uniforme (verificata nei test)
}

//==============================================================================
float NoiseGenerator::Pink::process(float w) noexcept
{
    // Paul Kellet, filtro "refined": pendenza -3 dB/ottava entro +-0.5 dB da ~10 Hz a Nyquist.
    b0 = 0.99886f * b0 + w * 0.0555179f;
    b1 = 0.99332f * b1 + w * 0.0750759f;
    b2 = 0.96900f * b2 + w * 0.1538520f;
    b3 = 0.86650f * b3 + w * 0.3104856f;
    b4 = 0.55000f * b4 + w * 0.5329522f;
    b5 = -0.7616f * b5 - w * 0.0168980f;
    const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
    b6 = w * 0.115926f;
    return out;
}

float NoiseGenerator::nextWhite() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;          // xorshift32
    return (float) (std::int32_t) rng * (1.0f / 2147483648.0f);   // [-1, 1)
}

//==============================================================================
void NoiseGenerator::prepare(double sampleRate, int maxBlockSize, const SampleBank* b, std::uint32_t seed)
{
    sr   = sampleRate > 0.0 ? sampleRate : 44100.0;
    bank = b;
    rng  = seed != 0 ? seed * 2654435761u | 1u : 0x2545F491u;
    for (int i = 0; i < 8; ++i) (void) nextWhite();               // scarta i primi valori (seed "caldo")

    levelCoef = 1.0f - (float) std::exp(-1.0 / (0.005 * sr));
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max(1, maxBlockSize), 2 };
    filter.prepare(spec);
    reset();
    updateDerived();
}

void NoiseGenerator::reset() noexcept
{
    pinkL = {}; pinkR = {};
    noiseLevelSm = 0.0f;
    playing = released = false; cur = nullptr; pos = 0.0; decayGain = 1.0f;
    filter.reset();
}

void NoiseGenerator::setParams(const Params& np) noexcept
{
    p = np;
    p.noiseLevel       = std::clamp(p.noiseLevel, 0.0f, 1.0f);
    p.noiseWidth       = std::clamp(p.noiseWidth, 0.0f, 1.0f);
    p.sampleLevel      = std::clamp(p.sampleLevel, 0.0f, 1.0f);
    p.samplePitchTrack = std::clamp(p.samplePitchTrack, 0.0f, 1.0f);
    p.samplePitchSemi  = std::clamp(p.samplePitchSemi, -48.0f, 48.0f);
    p.keytrack         = std::clamp(p.keytrack, 0.0f, 1.0f);
    updateDerived();
    if (playing) updateLoopBounds();
}

void NoiseGenerator::updateDerived() noexcept
{
    // Filtro tonale: cutoff con keytracking (ottave = keytrack * (nota - 60) / 12)
    const float oct = p.keytrack * (float) (currentNote - 60) / 12.0f;
    const float fc  = std::clamp(p.cutoffHz * std::exp2(oct), 20.0f, 0.45f * (float) sr);
    using FT = juce::dsp::StateVariableTPTFilterType;
    filter.setType(p.filterType == ToneFilterType::Lowpass ? FT::lowpass
                 : p.filterType == ToneFilterType::Bandpass ? FT::bandpass : FT::highpass);
    filter.setCutoffFrequency(fc);
    filter.setResonance(0.707f + p.resonance * p.resonance * 18.0f);

    decayCoef = p.decayMs > 0.0f ? (float) std::exp(-1.0 / ((double) p.decayMs * 0.001 * sr)) : 1.0f;
}

void NoiseGenerator::updateLoopBounds() noexcept
{
    if (cur == nullptr) return;
    const int n = (int) cur->data.size();
    int a = (int) ((double) std::clamp(p.loopStart, 0.0f, 1.0f) * n);
    int b = (int) ((double) std::clamp(p.loopEnd, 0.0f, 1.0f) * n);
    a = std::clamp(a, 0, std::max(0, n - 2));
    b = std::clamp(b, a + 2, n);                      // almeno 2 campioni di loop
    loopStartI = a; loopEndI = b; loopLen = b - a;
}

//==============================================================================
void NoiseGenerator::noteOn(int midiNote, float /*velocity*/) noexcept
{
    currentNote = midiNote;
    released = false;
    decayGain = 1.0f;
    pos = 0.0;

    cur = (p.sampleOn && bank != nullptr) ? bank->get(p.sampleSlot) : nullptr;
    playing = cur != nullptr && cur->data.size() >= 2;
    if (playing)
    {
        const double semis = (double) p.samplePitchSemi + (double) p.samplePitchTrack * (double) (midiNote - 60);
        step = (cur->sampleRate / sr) * std::exp2(semis / 12.0);
        updateLoopBounds();
    }
    filter.reset();
    updateDerived();                                  // il keytracking dipende dalla nuova nota
}

void NoiseGenerator::noteOff() noexcept { released = true; }

//==============================================================================
// Hermite a 4 punti. Fuori dai dati = 0; in loop (e finche' la nota e' tenuta) gli indici si avvolgono
// dentro [loopStart, loopEnd) cosi' il ciclo e' continuo anche attraverso il punto di loop.
float NoiseGenerator::readSample() const noexcept
{
    const auto& d = cur->data;
    const int n = (int) d.size();
    const bool looping = p.loop && ! released;

    auto at = [&](int i) noexcept -> float
    {
        if (looping)
        {
            if (i >= loopEndI)                                         i = loopStartI + (i - loopStartI) % loopLen;
            else if (i < loopStartI && pos >= (double) loopStartI)     i += loopLen;
        }
        return (i >= 0 && i < n) ? d[(size_t) i] : 0.0f;
    };

    const int   i = (int) pos;
    const float f = (float) (pos - (double) i);
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

//==============================================================================
void NoiseGenerator::tick(float& outL, float& outR) noexcept
{
    float l = 0.0f, r = 0.0f;

    // ---- Noise -----------------------------------------------------------------
    noiseLevelSm += ((p.noiseOn ? p.noiseLevel : 0.0f) - noiseLevelSm) * levelCoef;
    if (noiseLevelSm > 1.0e-6f)
    {
        const float wl = nextWhite(), wr = nextWhite();
        float nl, nr;
        if (p.noiseType == NoiseType::Pink) { nl = pinkL.process(wl) * kPinkCal; nr = pinkR.process(wr) * kPinkCal; }
        else                                { nl = wl; nr = wr; }
        nr = nl + p.noiseWidth * (nr - nl);           // width 0 => R == L esatto
        const float g = noiseLevelSm * kNoiseGain;
        l += nl * g; r += nr * g;
    }
    else if (noiseLevelSm != 0.0f) noiseLevelSm = 0.0f;   // niente denormali quando il livello e' a zero

    // ---- Sample ----------------------------------------------------------------
    if (playing)
    {
        float s = readSample() * p.sampleLevel;
        if (p.decayMs > 0.0f) { s *= decayGain; decayGain *= decayCoef; }
        l += s; r += s;

        pos += step;
        if (p.loop && ! released)
        {
            if (pos >= (double) loopEndI) pos = (double) loopStartI + std::fmod(pos - (double) loopStartI, (double) loopLen);
        }
        else if (pos >= (double) cur->data.size())
            playing = false;

        if (p.decayMs > 0.0f && decayGain < 1.0e-4f) playing = false;
    }

    // ---- Filtro tonale -----------------------------------------------------------
    if (p.filterOn)
    {
        l = filter.processSample(0, l);
        r = filter.processSample(1, r);
    }
    outL = l; outR = r;
}
}
