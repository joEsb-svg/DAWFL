#include "SampleBank.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bf
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // xorshift32 deterministico: i transienti interni sono identici ad ogni avvio.
    struct Rng
    {
        std::uint32_t s = 0x1234ABCDu;
        float next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (std::int32_t) s * (1.0f / 2147483648.0f); }
    };

    void normalise(std::vector<float>& v, float peakTarget)
    {
        float pk = 0.f; for (float x : v) pk = std::max(pk, std::abs(x));
        if (pk > 1.0e-9f) { const float g = peakTarget / pk; for (float& x : v) x *= g; }
    }

    // Rampa d'attacco di 0.2 ms: nessun click all'inizio e transiente comunque netto.
    void softStart(std::vector<float>& v, double sr)
    {
        const int n = std::max(1, (int) (0.0002 * sr));
        for (int i = 0; i < n && i < (int) v.size(); ++i) v[(size_t) i] *= (float) i / (float) n;
    }

    std::vector<float> metalHit(double sr)
    {
        std::vector<float> v((size_t) (0.35 * sr)); Rng r;
        const double f[6] = { 523.0, 1117.0, 1671.0, 2893.0, 4417.0, 6841.0 };      // parziali inarmonici
        const double a[6] = { 1.0, 0.8, 0.7, 0.5, 0.35, 0.25 }, tau[6] = { 0.12, 0.09, 0.07, 0.05, 0.035, 0.02 };
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / sr; double s = 0;
            for (int k = 0; k < 6; ++k) s += a[k] * std::exp(-t / tau[k]) * std::sin(2.0 * kPi * f[k] * t);
            s += 1.5 * std::exp(-t / 0.004) * r.next();                              // colpo iniziale
            v[i] = (float) s;
        }
        softStart(v, sr); normalise(v, 0.9f); return v;
    }

    std::vector<float> kickClick(double sr)
    {
        std::vector<float> v((size_t) (0.12 * sr)); Rng r; double ph = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / sr, f = 55.0 + 140.0 * std::exp(-t / 0.018);   // sweep 195 -> 55 Hz
            ph += f / sr;
            v[i] = (float) (std::exp(-t / 0.04) * std::sin(2.0 * kPi * ph) + 0.6 * std::exp(-t / 0.002) * r.next());
        }
        softStart(v, sr); normalise(v, 0.9f); return v;
    }

    std::vector<float> snareCrack(double sr)
    {
        std::vector<float> v((size_t) (0.25 * sr)); Rng r; float prev = 0.f;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / sr; const float w = r.next();
            const float hp = w - prev; prev = w;                                          // passa-alto 1 polo: rumore "frizzante"
            v[i] = (float) (0.8 * std::exp(-t / 0.055) * hp + 0.7 * std::exp(-t / 0.03) * std::sin(2.0 * kPi * 190.0 * t));
        }
        softStart(v, sr); normalise(v, 0.9f); return v;
    }

    std::vector<float> foleyTick(double sr)
    {
        std::vector<float> v((size_t) (0.06 * sr)); Rng r; double y1 = 0, y2 = 0;
        const double w0 = 2.0 * kPi * 3100.0 / sr, rr = 0.97, c1 = 2.0 * rr * std::cos(w0), c2 = -rr * rr;   // risonatore a 3.1 kHz
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double t = (double) i / sr, x = std::exp(-t / 0.008) * r.next();
            const double y = x + c1 * y1 + c2 * y2; y2 = y1; y1 = y;
            v[i] = (float) y;
        }
        softStart(v, sr); normalise(v, 0.9f); return v;
    }
}

SampleBank::SampleBank()
{
    for (auto& s : slots) s.store(nullptr, std::memory_order_relaxed);
}

void SampleBank::buildBuiltIns()
{
    constexpr double sr = 48000.0;
    const struct { const char* name; std::vector<float> (*gen)(double); } defs[kNumBuiltIn] = {
        { "Metal Hit", metalHit }, { "Kick Click", kickClick }, { "Snare Crack", snareCrack }, { "Foley Tick", foleyTick } };
    for (int i = 0; i < kNumBuiltIn; ++i)
    {
        auto v = defs[i].gen(sr);
        setSample(i, v.data(), (int) v.size(), sr, defs[i].name);
    }
}

bool SampleBank::setSample(int slot, const float* mono, int numSamples, double sampleRate, const juce::String& name)
{
    if (slot < 0 || slot >= kMaxSlots || mono == nullptr || numSamples < 2 || sampleRate < 1000.0) return false;

    auto sd = std::make_unique<SampleData>();
    const int n = std::min(numSamples, (int) (kMaxSeconds * sampleRate));
    sd->data.assign(mono, mono + n);
    sd->sampleRate = sampleRate;
    sd->name = name;

    const SampleData* raw = sd.get();
    {
        const juce::ScopedLock sl(ownedLock);
        owned.push_back(std::move(sd));            // mai liberato finche' il banco vive
    }
    slots[(size_t) slot].store(raw, std::memory_order_release);
    return true;
}

bool SampleBank::loadFile(int slot, const juce::File& file)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(file));
    if (rd == nullptr || rd->lengthInSamples < 2 || rd->numChannels < 1) return false;

    const int n = (int) std::min<juce::int64>(rd->lengthInSamples, (juce::int64) (kMaxSeconds * rd->sampleRate));
    const int nch = (int) std::min<unsigned>(rd->numChannels, 2u);
    juce::AudioBuffer<float> buf(nch, n);
    if (! rd->read(&buf, 0, n, 0, true, nch > 1)) return false;

    std::vector<float> mono((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.f; for (int c = 0; c < nch; ++c) s += buf.getSample(c, i);
        mono[(size_t) i] = s / (float) nch;
    }
    normalise(mono, 0.944f);                      // -0.5 dBFS
    return setSample(slot, mono.data(), n, rd->sampleRate, file.getFileNameWithoutExtension());
}
}
