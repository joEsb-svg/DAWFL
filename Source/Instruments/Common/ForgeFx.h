#pragma once

#include <JuceHeader.h>
#include "../../Engine/Performance.h"
#include <vector>

//==============================================================================
/**
    Effetti in uscita dei synth "Forge" (chorus stereo, delay ping-pong a tempo, riverbero, volume).
    Nessuna allocazione dopo prepare().
*/
namespace forge
{
    struct FxParams
    {
        float chorus = 0.0f, delayMix = 0.0f, delayFeedback = 0.35f, reverbMix = 0.0f, reverbSize = 0.6f, gain = 1.0f;
        double delayBeats = 0.75;
    };

    class Effects
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            chorusL.assign (4096, 0.0f); chorusR.assign (4096, 0.0f);
            delayL.assign ((size_t) (sr * 3.0) + 1, 0.0f); delayR.assign ((size_t) (sr * 3.0) + 1, 0.0f);
            chorusPos = delayPos = 0;
            delayLp[0] = delayLp[1] = 0.0f;
            reverb.setSampleRate (sr);
            reverb.reset();
        }

        void reset()
        {
            std::fill (chorusL.begin(), chorusL.end(), 0.0f); std::fill (chorusR.begin(), chorusR.end(), 0.0f);
            std::fill (delayL.begin(), delayL.end(), 0.0f);   std::fill (delayR.begin(), delayR.end(), 0.0f);
            delayLp[0] = delayLp[1] = 0.0f;
            reverb.reset();
        }

        void process (float* L, float* R, int n, const FxParams& p, double bpm)
        {
            if (p.chorus > 0.001f)
            {
                const int size = (int) chorusL.size();
                const float base = (float) (0.012 * sr), depth = (float) (0.0035 * sr);
                for (int i = 0; i < n; ++i)
                {
                    chorusL[(size_t) chorusPos] = L[i];
                    chorusR[(size_t) chorusPos] = R[i];
                    chorusPhase += 0.45 / sr;
                    if (chorusPhase >= 1.0) chorusPhase -= 1.0;
                    const float modL = perf::fastSin ((float) chorusPhase);
                    const float modR = perf::fastSin ((float) chorusPhase + 0.25f);
                    const float wl = read (chorusL, base + depth * modL);
                    const float wr = read (chorusR, base + depth * modR);
                    L[i] = L[i] * (1.0f - 0.35f * p.chorus) + wr * 0.7f * p.chorus;
                    R[i] = R[i] * (1.0f - 0.35f * p.chorus) + wl * 0.7f * p.chorus;
                    chorusPos = (chorusPos + 1) % size;
                }
            }

            if (p.delayMix > 0.001f)
            {
                const int size = (int) delayL.size();
                const int d = juce::jlimit (1, size - 1, (int) (p.delayBeats * 60.0 / juce::jmax (40.0, bpm) * sr));
                for (int i = 0; i < n; ++i)
                {
                    const int rd = (delayPos - d + size) % size;
                    const float dl = delayL[(size_t) rd], dr = delayR[(size_t) rd];
                    delayLp[0] += 0.3f * (dr - delayLp[0]);
                    delayLp[1] += 0.3f * (dl - delayLp[1]);
                    delayL[(size_t) delayPos] = (L[i] + R[i]) * 0.5f + delayLp[0] * p.delayFeedback;   // ping-pong
                    delayR[(size_t) delayPos] = delayLp[1] * p.delayFeedback;
                    delayPos = (delayPos + 1) % size;
                    L[i] += dl * p.delayMix;
                    R[i] += dr * p.delayMix;
                }
            }

            if (p.reverbMix > 0.001f)
            {
                juce::Reverb::Parameters rp;
                rp.roomSize = 0.3f + 0.68f * p.reverbSize;
                rp.damping = 0.45f;
                rp.width = 1.0f;
                rp.wetLevel = p.reverbMix * 0.6f;
                rp.dryLevel = 1.0f - p.reverbMix * 0.35f;
                if (rp.roomSize != lastRev.roomSize || rp.wetLevel != lastRev.wetLevel)
                {
                    reverb.setParameters (rp);
                    lastRev = rp;
                }
                reverb.processStereo (L, R, n);
            }

            if (std::abs (p.gain - 1.0f) > 0.0001f)
                for (int i = 0; i < n; ++i) { L[i] *= p.gain; R[i] *= p.gain; }
        }

    private:
        float read (const std::vector<float>& buf, float delaySamples) const
        {
            const int size = (int) buf.size();
            float idx = (float) chorusPos - delaySamples;
            while (idx < 0) idx += (float) size;
            const int i0 = (int) idx;
            const float fr = idx - (float) i0;
            return buf[(size_t) i0] + fr * (buf[(size_t) ((i0 + 1) % size)] - buf[(size_t) i0]);
        }

        double sr = 48000.0;
        std::vector<float> chorusL, chorusR, delayL, delayR;
        int chorusPos = 0, delayPos = 0;
        double chorusPhase = 0.0;
        float delayLp[2] { 0.0f, 0.0f };
        juce::Reverb reverb;
        juce::Reverb::Parameters lastRev;
    };
}
