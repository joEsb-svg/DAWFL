#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace bf
{
    /**
     * Modulatori MIDI di una voce: Pitch Bend, Aftertouch (channel pressure / poly AT / MPE pressure),
     * Mod Wheel (CC1) e MPE Slide (CC74). I valori in ingresso arrivano "a scalini" dal MIDI: advance(n)
     * li liscia con un filtro a un polo (costanti di tempo: bend 2 ms, gli altri 6 ms) cosi' la modulazione
     * non fa zipper. In una voce MPE (un canale per nota) questi valori sono per-nota.
     */
    class MidiModulators
    {
    public:
        void prepare(double sampleRate) noexcept { sr = sampleRate > 0.0 ? sampleRate : 44100.0; }

        void setPitchBend14(int v14) noexcept    { bendT = std::min(std::max(((float) v14 - 8192.0f) / 8192.0f, -1.0f), 1.0f); }
        void setPressure(float v01) noexcept     { pressT = clamp01(v01); }
        void setModWheel(float v01) noexcept     { wheelT = clamp01(v01); }
        void setSlide(float v01) noexcept        { slideT = clamp01(v01); }
        void setController(int cc, int value) noexcept
        {
            const float v = (float) value * (1.0f / 127.0f);
            if (cc == 1) setModWheel(v); else if (cc == 74) setSlide(v);
        }

        void snap() noexcept { bend_ = bendT; press_ = pressT; wheel_ = wheelT; slide_ = slideT; }
        void reset() noexcept { bendT = press_ = pressT = wheelT = slideT = bend_ = wheel_ = slide_ = 0.0f; }

        void advance(int n) noexcept
        {
            const float cb = 1.0f - std::exp(-(float) n / (0.002f * (float) sr));
            const float co = 1.0f - std::exp(-(float) n / (0.006f * (float) sr));
            bend_  += (bendT  - bend_)  * cb;
            press_ += (pressT - press_) * co;
            wheel_ += (wheelT - wheel_) * co;
            slide_ += (slideT - slide_) * co;
        }

        float bend() const noexcept      { return bend_; }
        float pressure() const noexcept  { return press_; }
        float modWheel() const noexcept  { return wheel_; }
        float slide() const noexcept     { return slide_; }

    private:
        static float clamp01(float v) noexcept { return std::min(std::max(v, 0.0f), 1.0f); }
        double sr = 44100.0;
        float bendT = 0, pressT = 0, wheelT = 0, slideT = 0, bend_ = 0, press_ = 0, wheel_ = 0, slide_ = 0;
    };

    /** Stato MIDI "globale" scritto dal processor e letto dalle voci al note-on (le voci inattive non
     *  ricevono i CC: senza questo un mod wheel gia' alzato non verrebbe visto dalle note successive). */
    struct SharedMidiState
    {
        std::atomic<float> modWheel { 0.0f };
    };
}
