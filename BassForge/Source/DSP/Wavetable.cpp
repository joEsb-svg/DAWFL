#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace bf
{
void WavetableBank::build()
{
    constexpr int frames  = 64;
    constexpr int maxHarm = 96;
    const double twoPi = juce::MathConstants<double>::twoPi;

    std::vector<float> src((size_t) frames * kTableSize);

    for (int f = 0; f < frames; ++f)
    {
        const double t = (double) f / (double) (frames - 1);
        const double numHarm = 1.0 + t * (maxHarm - 1);          // 1 .. 96 armoniche
        float* tab = src.data() + (size_t) f * kTableSize;

        // Frame 0 = sinusoide, poi saw progressivamente piu' ricca; pari in crescendo.
        float peak = 1.0e-6f;
        for (int i = 0; i < kTableSize; ++i)
        {
            const double ph = twoPi * (double) i / (double) kTableSize;
            double s = 0.0;
            for (int h = 1; h <= maxHarm; ++h)
            {
                if ((double) h > numHarm + 1.0) break;
                double amp = 1.0 / (double) h;
                if ((double) h > numHarm) amp *= (numHarm + 1.0 - (double) h);
                if (h % 2 == 0) amp *= (0.35 + 0.65 * t);
                s += amp * std::sin(ph * (double) h);
            }
            tab[i] = (float) s;
            peak = std::max(peak, std::abs(tab[i]));
        }
        for (int i = 0; i < kTableSize; ++i) tab[i] /= peak;
    }

    buildFromFrames(src.data(), frames);
}

bool WavetableBank::buildFromFrames(const float* frames, int nf)
{
    if (frames == nullptr || nf < 1 || nf > kMaxFrames)
        return false;

    std::vector<float> out((size_t) nf * kNumMips * kStride, 0.0f);

    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> spec(2 * (size_t) kTableSize);

    for (int f = 0; f < nf; ++f)
    {
        const float* in = frames + (size_t) f * kTableSize;

        // Normalizzazione per-frame sul livello 0 (stesso fattore per tutti i mip del frame,
        // cosi' i livelli restano coerenti in ampiezza e il morph tra frame e' uniforme).
        float peak = 1.0e-9f;
        for (int i = 0; i < kTableSize; ++i) peak = std::max(peak, std::abs(in[i]));
        const float norm = 1.0f / peak;

        // Rimozione DC: un offset sposta il filtro/wavefolder e fa "thump" nei morph.
        double mean = 0.0;
        for (int i = 0; i < kTableSize; ++i) mean += in[i];
        const float dc = (float) (mean / kTableSize);

        for (int m = 0; m < kNumMips; ++m)
        {
            float* base = out.data() + ((size_t) f * kNumMips + (size_t) m) * kStride + 1;

            if (m == 0)
            {
                for (int i = 0; i < kTableSize; ++i) base[i] = (in[i] - dc) * norm;
            }
            else
            {
                std::fill(spec.begin(), spec.end(), 0.0f);
                for (int i = 0; i < kTableSize; ++i) spec[(size_t) i] = (in[i] - dc) * norm;

                fft.performRealOnlyForwardTransform(spec.data(), true);

                // Brickwall con piccolo taper coseno sull'ultimo 15% delle armoniche mantenute (riduce Gibbs).
                const int H = (kTableSize / 2) >> m;
                const int taperStart = (int) std::floor(0.85 * (double) H);
                for (int k = 0; k <= kTableSize / 2; ++k)
                {
                    float g = 1.0f;
                    if (k > H)                          g = 0.0f;
                    else if (H > 4 && k > taperStart)   g = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi
                                                                  * (float) (k - taperStart) / (float) (H - taperStart + 1)));
                    spec[(size_t) 2 * k]     *= g;
                    spec[(size_t) 2 * k + 1] *= g;
                }

                fft.performRealOnlyInverseTransform(spec.data());
                for (int i = 0; i < kTableSize; ++i) base[i] = spec[(size_t) i];
            }

            // Guardie: base[-1], base[N], base[N+1]
            base[-1]         = base[kTableSize - 1];
            base[kTableSize]     = base[0];
            base[kTableSize + 1] = base[1];
        }
    }

    data = std::move(out);
    numFrames = nf;
    return true;
}
}
