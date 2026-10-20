#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Mixer.h"

//==============================================================================
/**
    Automazioni: curve nel tempo della canzone (playlist) che muovono un controllo da sole,
    per esempio il filtro che si apre in un build-up o il volume che sfuma alla fine.

    Ogni curva ("corsia") ha un bersaglio e fino a 256 punti (tick della canzone, valore 0..1);
    tra un punto e l'altro il valore cambia in linea retta. Suonano in modalita' SONG.
    I punti sono impacchettati in 32 bit (tick 16 bit | valore 16 bit), letti senza lock dal thread audio.
*/
namespace automation
{
    inline constexpr int maxLanes  = 32;
    inline constexpr int maxPoints = 256;

    enum Kind { none = 0, master, channelParam, trackParam, effectParam, instrumentParam };

    // channelParam: a = canale, b = quale
    enum ChannelParam { chVolume = 0, chPan, chFilter, chReverb, chDelay, chPitch, numChannelParams };
    // trackParam: a = traccia del mixer, b = quale
    enum TrackParam { trVolume = 0, trPan, trEqLow, trEqMid, trEqHigh, numTrackParams };
    // effectParam: a = traccia, b = slot, c = parametro
    // instrumentParam: a = canale, b = indice del parametro dello strumento

    inline const char* channelParamName (int i)
    {
        static const char* names[] = { "Volume", "Panorama", "Filtro", "Riverbero", "Delay", "Intonazione" };
        return names[juce::jlimit (0, numChannelParams - 1, i)];
    }

    inline const char* trackParamName (int i)
    {
        static const char* names[] = { "Volume", "Panorama", "EQ bassi", "EQ medi", "EQ alti" };
        return names[juce::jlimit (0, numTrackParams - 1, i)];
    }

    struct Target
    {
        int kind = none, a = 0, b = 0, c = 0;
        bool operator== (const Target& o) const { return kind == o.kind && a == o.a && b == o.b && c == o.c; }
    };

    struct Point
    {
        int tick = 0;        // 12 tick per step, 192 per battuta
        float value = 0.5f;  // 0..1
    };

    inline uint32_t pack (const Point& p)
    {
        return (uint32_t) juce::jlimit (0, 65535, p.tick) << 16 | (uint32_t) juce::jlimit (0, 65535, juce::roundToInt (p.value * 65535.0f));
    }

    inline Point unpack (uint32_t v)    { return { (int) (v >> 16), (float) (v & 0xffff) / 65535.0f }; }

    /** Volume (0..1 della curva) -> guadagno: scala in dB, da -40 dB a maxDb; in fondo silenzio. */
    inline float volumeGain (float v, float maxDb)
    {
        if (v < 0.01f) return 0.0f;
        return juce::Decibels::decibelsToGain (-40.0f + (maxDb + 40.0f) * v);
    }

    inline float volumeNorm (float gain, float maxDb)
    {
        if (gain <= 0.0001f) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain) + 40.0f) / (maxDb + 40.0f));
    }

    struct Lane
    {
        std::atomic<int> kind { none }, a { 0 }, b { 0 }, c { 0 };
        std::atomic<bool> enabled { true };
        std::atomic<int> count { 0 };
        std::array<std::atomic<uint32_t>, maxPoints> points {};

        Target target() const   { return { kind.load(), a.load(), b.load(), c.load() }; }
        void setTarget (const Target& t)    { a.store (t.a); b.store (t.b); c.store (t.c); kind.store (t.kind); }

        /** Il valore della curva al tick indicato (thread audio; -1 = nessun punto). */
        float valueAt (double tick) const noexcept
        {
            const int n = juce::jmin (count.load(), maxPoints);
            if (n <= 0)
                return -1.0f;
            auto first = unpack (points[0].load());
            if (tick <= first.tick || n == 1)
                return first.value;
            for (int i = 1; i < n; ++i)
            {
                const auto p = unpack (points[(size_t) i].load());
                if (tick <= p.tick)
                {
                    const auto q = unpack (points[(size_t) i - 1].load());
                    const double span = juce::jmax (1, p.tick - q.tick);
                    return q.value + (p.value - q.value) * (float) ((tick - q.tick) / span);
                }
            }
            return unpack (points[(size_t) n - 1].load()).value;
        }
    };
}
