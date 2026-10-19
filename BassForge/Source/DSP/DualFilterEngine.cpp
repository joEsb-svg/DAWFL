#include "DualFilterEngine.h"

namespace bf
{
using namespace simd;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    inline float lerpf(float a, float b, float t) noexcept { return a + t * (b - a); }

    /** Drive pre-filtro: soft-clip cubico con compensazione ~1/sqrt(g). */
    struct Drive
    {
        bool  on = false;
        float g = 1.0f, norm = 1.0f;
        explicit Drive(float amount) noexcept
        {
            on = amount > 0.001f;
            g = 1.0f + amount * 8.0f;
            norm = 1.0f / std::sqrt(g);
        }
    };

    // Formanti di vocale maschile (valori tipici di tabelle pubbliche di sintesi vocale, voce tenore).
    // Ordine: A, E, I, O, U. Frequenze [Hz], banda [Hz], ampiezza [dB].
    constexpr float kF [5][4] = { { 650, 1080, 2650, 2900 }, { 400, 1700, 2600, 3200 }, { 290, 1870, 2800, 3250 },
                                  { 400,  800, 2600, 2800 }, { 350,  600, 2700, 2900 } };
    constexpr float kBW[5][4] = { {  80,   90,  120,  130 }, {  70,   80,  100,  120 }, {  40,   90,  100,  120 },
                                  {  40,   80,  100,  120 }, {  40,   80,  100,  120 } };
    constexpr float kDB[5][4] = { {   0,   -6,   -7,   -8 }, {   0,  -14,  -12,  -14 }, {   0,  -15,  -18,  -20 },
                                  {   0,  -10,  -12,  -12 }, {   0,  -20,  -17,  -14 } };
    constexpr float kVowelMakeup = 3.0f;      // i band-pass stretti tolgono energia: compensazione fissa (il limiter protegge)
}

//==============================================================================
// ZDF cascade (4 stadi nei 4 lane, in pipeline)
//==============================================================================
void DualFilterEngine::ZdfUnit::reset() noexcept
{
    for (int c = 0; c < 2; ++c) { ic1[c] = set1(0.f); ic2[c] = set1(0.f); prev[c] = set1(0.f); fbState[c] = 0.f; }
}

void DualFilterEngine::ZdfUnit::configure(FilterType t, float fc, float res, double sr) noexcept
{
    const bool hp = (t == FilterType::HP12 || t == FilterType::HP24 || t == FilterType::HP48);
    const int stages = (t == FilterType::LP12 || t == FilterType::HP12) ? 1
                     : (t == FilterType::LP24 || t == FilterType::HP24) ? 2 : 4;

    // Q Butterworth per stadio => risposta massimamente piatta a risonanza 0
    static constexpr float q1[1] = { 0.70711f };
    static constexpr float q2[2] = { 0.54120f, 1.30656f };
    static constexpr float q4[4] = { 0.50980f, 0.60134f, 0.89998f, 2.56292f };
    const float* qs = stages == 1 ? q1 : (stages == 2 ? q2 : q4);

    fc = std::min(std::max(fc, 20.0f), 0.45f * (float) sr);
    const float g = std::tan(kPi * fc / (float) sr);
    res = std::min(std::max(res, 0.0f), 1.0f);

    float A1[4], A2[4], A3[4], M0[4], M1[4], M2[4];
    for (int s = 0; s < 4; ++s)
    {
        if (s < stages)
        {
            float Q = qs[s];
            if (s == stages - 1) Q += res * res * 28.0f;       // la risonanza agisce sull'ultimo stadio
            const float k = 1.0f / Q;
            A1[s] = 1.0f / (1.0f + g * (g + k));
            A2[s] = g * A1[s];
            A3[s] = g * A2[s];
            if (hp) { M0[s] = 1.f; M1[s] = -k; M2[s] = -1.f; }
            else    { M0[s] = 0.f; M1[s] = 0.f; M2[s] = 1.f; }
        }
        else { A1[s] = 1.f; A2[s] = 0.f; A3[s] = 0.f; M0[s] = 1.f; M1[s] = 0.f; M2[s] = 0.f; }   // lane inutilizzato = passa-tutto
    }
    a1 = set4(A1[0], A1[1], A1[2], A1[3]);  a2 = set4(A2[0], A2[1], A2[2], A2[3]);  a3 = set4(A3[0], A3[1], A3[2], A3[3]);
    m0 = set4(M0[0], M0[1], M0[2], M0[3]);  m1 = set4(M1[0], M1[1], M1[2], M1[3]);  m2 = set4(M2[0], M2[1], M2[2], M2[3]);
    tap = stages - 1;
}

template <int Tap>
void DualFilterEngine::ZdfUnit::run(const float* inL, const float* inR, float* outL, float* outR, int n,
                                    float fb, float dg, float dnorm, bool driveOn) noexcept
{
    const f4 two = set1(2.0f);
    for (int i = 0; i < n; ++i)
    {
        float x0 = inL[i], x1 = inR[i];                            // letti PRIMA di scrivere (in-place sicuro)
        if (driveOn) { x0 = softClip3(x0 * dg) * dnorm; x1 = softClip3(x1 * dg) * dnorm; }
        x0 += fb * fbState[0];                                      // feedback loop: usa l'uscita gia' LIMITATA
        x1 += fb * fbState[1];

        {   // canale L
            const f4 in = shiftIn(x0, prev[0]);                    // [x, y0', y1', y2'] pipeline degli stadi
            const f4 v3 = in - ic2[0];
            const f4 v1 = a1 * ic1[0] + a2 * v3;
            const f4 v2 = ic2[0] + a2 * ic1[0] + a3 * v3;
            ic1[0] = two * v1 - ic1[0];
            ic2[0] = two * v2 - ic2[0];
            const f4 o = m0 * in + m1 * v1 + m2 * v2;
            prev[0] = o;
            const float y = softLimit(lane<Tap>(o));
            fbState[0] = y; outL[i] = y;
        }
        {   // canale R
            const f4 in = shiftIn(x1, prev[1]);
            const f4 v3 = in - ic2[1];
            const f4 v1 = a1 * ic1[1] + a2 * v3;
            const f4 v2 = ic2[1] + a2 * ic1[1] + a3 * v3;
            ic1[1] = two * v1 - ic1[1];
            ic2[1] = two * v2 - ic2[1];
            const f4 o = m0 * in + m1 * v1 + m2 * v2;
            prev[1] = o;
            const float y = softLimit(lane<Tap>(o));
            fbState[1] = y; outR[i] = y;
        }
    }
}

void DualFilterEngine::ZdfUnit::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                                        float fb, float drive) noexcept
{
    const Drive d(drive);
    fb = std::min(std::max(fb, -1.0f), 1.0f) * 0.9f;
    switch (tap)
    {
        case 0:  run<0>(inL, inR, outL, outR, n, fb, d.g, d.norm, d.on); break;
        case 1:  run<1>(inL, inR, outL, outR, n, fb, d.g, d.norm, d.on); break;
        case 2:  run<2>(inL, inR, outL, outR, n, fb, d.g, d.norm, d.on); break;
        default: run<3>(inL, inR, outL, outR, n, fb, d.g, d.norm, d.on); break;
    }
}

//==============================================================================
// Vowel / Formant (4 band-pass SVF in parallelo)
//==============================================================================
void DualFilterEngine::VowelUnit::reset() noexcept
{
    for (int c = 0; c < 2; ++c) { ic1[c] = set1(0.f); ic2[c] = set1(0.f); fbState[c] = 0.f; }
}

void DualFilterEngine::VowelUnit::configure(float vowel01, float shiftSemis, float res, double sr) noexcept
{
    const float pos = std::min(std::max(vowel01, 0.0f), 1.0f) * 4.0f;
    const int   i0  = std::min((int) pos, 3);
    const float fr  = pos - (float) i0;
    const float shift = std::exp2(shiftSemis * (1.0f / 12.0f));
    res = std::min(std::max(res, 0.0f), 1.0f);

    float A1[4], A2[4], A3[4], GK[4];
    for (int j = 0; j < 4; ++j)
    {
        const float lf = lerpf(std::log(kF[i0][j]), std::log(kF[i0 + 1][j]), fr);        // morph in scala log
        const float fb0 = std::exp(lf);
        const float f  = std::min(std::max(fb0 * shift, 40.0f), 0.45f * (float) sr);
        const float bw = lerpf(kBW[i0][j], kBW[i0 + 1][j], fr);
        const float db = lerpf(kDB[i0][j], kDB[i0 + 1][j], fr);

        const float Q = std::max(0.5f, (fb0 / bw) * (0.5f + 3.0f * res));
        const float k = 1.0f / Q;
        const float g = std::tan(kPi * f / (float) sr);
        A1[j] = 1.0f / (1.0f + g * (g + k));
        A2[j] = g * A1[j];
        A3[j] = g * A2[j];
        GK[j] = k * std::pow(10.0f, db * 0.05f) * kVowelMakeup;                           // k*v1 = band-pass a guadagno di picco 1
    }
    a1 = set4(A1[0], A1[1], A1[2], A1[3]); a2 = set4(A2[0], A2[1], A2[2], A2[3]);
    a3 = set4(A3[0], A3[1], A3[2], A3[3]); gk = set4(GK[0], GK[1], GK[2], GK[3]);
}

void DualFilterEngine::VowelUnit::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                                          float fb, float drive) noexcept
{
    const Drive d(drive);
    fb = std::min(std::max(fb, -1.0f), 1.0f) * 0.9f;
    const f4 two = set1(2.0f);

    for (int i = 0; i < n; ++i)
    {
        float x0 = inL[i], x1 = inR[i];
        if (d.on) { x0 = softClip3(x0 * d.g) * d.norm; x1 = softClip3(x1 * d.g) * d.norm; }
        x0 += fb * fbState[0];
        x1 += fb * fbState[1];

        {
            const f4 in = set1(x0);                                // ingresso a tutti i formanti
            const f4 v3 = in - ic2[0];
            const f4 v1 = a1 * ic1[0] + a2 * v3;
            const f4 v2 = ic2[0] + a2 * ic1[0] + a3 * v3;
            ic1[0] = two * v1 - ic1[0];
            ic2[0] = two * v2 - ic2[0];
            const float y = softLimit(hsum(v1 * gk));
            fbState[0] = y; outL[i] = y;
        }
        {
            const f4 in = set1(x1);
            const f4 v3 = in - ic2[1];
            const f4 v1 = a1 * ic1[1] + a2 * v3;
            const f4 v2 = ic2[1] + a2 * ic1[1] + a3 * v3;
            ic1[1] = two * v1 - ic1[1];
            ic2[1] = two * v2 - ic2[1];
            const float y = softLimit(hsum(v1 * gk));
            fbState[1] = y; outR[i] = y;
        }
    }
}

//==============================================================================
// Comb (feedback +/-, ritardo frazionario + damping FIR a 4 tap, 4 campioni per volta)
//==============================================================================
void DualFilterEngine::CombUnit::allocate()
{
    for (auto& h : hist) h.assign((size_t) (2 * kN + 16), 0.0f);
    wp = 0;
}

void DualFilterEngine::CombUnit::reset() noexcept
{
    for (auto& h : hist) std::fill(h.begin(), h.end(), 0.0f);
    wp = 0;
}

void DualFilterEngine::CombUnit::configure(float freqHz, float fb, float brightness, double sr) noexcept
{
    // D >= 5: il tap piu' corto (D0-1) deve essere >= 4 campioni cosi' 4 uscite consecutive sono indipendenti.
    const float D = std::min(std::max((float) sr / std::max(freqHz, 1.0f), 5.0f), (float) (kN - 4));
    d0 = (int) std::floor(D);
    const float w1 = D - (float) d0, w0 = 1.0f - w1;

    // interpolazione lineare (w0 a D0, w1 a D0+1) convoluta con il damping simmetrico [d, 1-2d, d];
    // guadagno DC = 1 e ritardo (baricentro) = D esatto.
    const float b = std::min(std::max(brightness, 0.0f), 1.0f);
    const float d = 0.25f * (1.0f - b) * (1.0f - b);
    t0 = d * w0;
    t1 = (1.0f - 2.0f * d) * w0 + d * w1;
    t2 = d * w0 + (1.0f - 2.0f * d) * w1;
    t3 = d * w1;
    outGain = 1.0f - 0.6f * std::fabs(fb);
}

void DualFilterEngine::CombUnit::process(const float* inL, const float* inR, float* outL, float* outR, int n,
                                         float fb, float drive) noexcept
{
    fb = std::min(std::max(fb, -0.995f), 0.995f);
    const Drive dr(drive);
    const f4 vt0 = set1(t0), vt1 = set1(t1), vt2 = set1(t2), vt3 = set1(t3), vfb = set1(fb), vog = set1(outGain);
    const f4 vdg = set1(dr.g), vdn = set1(dr.norm);

    const float* in[2]  = { inL, inR };
    float*       out[2] = { outL, outR };
    int pEnd = wp;

    for (int ch = 0; ch < 2; ++ch)
    {
        float* h = hist[(size_t) ch].data();
        int p = wp, i = 0;

        while (i < n)
        {
            if ((p & 3) == 0 && n - i >= 4)
            {
                f4 x = loadu(in[ch] + i);
                if (dr.on) x = softClip3(x * vdg) * vdn;

                const float* base = h + p + kN - d0;
                const f4 dl = vt0 * loadu(base + 1) + vt1 * loadu(base) + vt2 * loadu(base - 1) + vt3 * loadu(base - 2);
                const f4 y = x + vfb * softClip3(dl);              // soft-clip nel ciclo: nessun runaway

                storeu(h + p, y);                                  // scrittura doppia => lettura contigua senza wrap
                storeu(h + p + kN, y);
                storeu(out[ch] + i, softLimit(y * vog));
                p += 4; i += 4;
                if (p >= kN) p = 0;
            }
            else
            {
                float x = in[ch][i];
                if (dr.on) x = softClip3(x * dr.g) * dr.norm;

                const int b = p + kN - d0;
                const float dl = t0 * h[b + 1] + t1 * h[b] + t2 * h[b - 1] + t3 * h[b - 2];
                const float y = x + fb * softClip3(dl);

                h[p] = y; h[p + kN] = y;
                out[ch][i] = softLimit(y * outGain);
                ++p; ++i;
                if (p >= kN) p = 0;
            }
        }
        pEnd = p;
    }
    wp = pEnd;
}

//==============================================================================
// Engine
//==============================================================================
void DualFilterEngine::prepare(double sampleRate, int)
{
    sr = sampleRate;
    for (auto& u : units) { u.comb.allocate(); u.resetAll(); u.wasOn = false; }
    snap = true;
}

void DualFilterEngine::reset() noexcept
{
    for (auto& u : units) { u.resetAll(); u.wasOn = false; }
    snap = true;
}

void DualFilterEngine::setParams(const Params& p) noexcept
{
    tgt = p;
}

void DualFilterEngine::runUnit(int idx, const FilterParams& p, float noteHz,
                               const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    Unit& u = units[(size_t) idx];
    if (u.activeType != p.type) { u.resetAll(); u.activeType = p.type; }       // cambio tipo: stato pulito
    if (! u.wasOn)              { u.resetAll(); u.wasOn = true; }              // riaccensione: niente transienti vecchi

    switch (p.type)
    {
        case FilterType::Comb:
        {
            float f = p.cutoffHz;
            if (p.pitchTrack > 0.0f)                                           // PITCH TRACKING: f -> nota * 2^(tune/12)
            {
                const float fT = noteHz * std::exp2(p.tuneSemis * (1.0f / 12.0f));
                f = std::exp(lerpf(std::log(std::max(f, 1.0f)), std::log(std::max(fT, 1.0f)), p.pitchTrack));
            }
            u.comb.configure(f, p.feedback, p.resonance, sr);
            u.comb.process(inL, inR, outL, outR, n, p.feedback, p.drive);
            break;
        }
        case FilterType::Vowel:
            u.vowel.configure(p.vowel, p.formantShift, p.resonance, sr);
            u.vowel.process(inL, inR, outL, outR, n, p.feedback, p.drive);
            break;
        default:
            u.zdf.configure(p.type, p.cutoffHz, p.resonance, sr);
            u.zdf.process(inL, inR, outL, outR, n, p.feedback, p.drive);
            break;
    }
}

namespace
{
    using FP = DualFilterEngine::FilterParams;

    /** Interpolazione dei parametri continui (cutoff in scala log); i discreti vengono dal target. */
    FP mixParams(const FP& a, const FP& b, float t) noexcept
    {
        FP r = b;
        r.cutoffHz     = std::exp(lerpf(std::log(std::max(a.cutoffHz, 1.0f)), std::log(std::max(b.cutoffHz, 1.0f)), t));
        r.resonance    = lerpf(a.resonance, b.resonance, t);
        r.drive        = lerpf(a.drive, b.drive, t);
        r.feedback     = lerpf(a.feedback, b.feedback, t);
        r.pitchTrack   = lerpf(a.pitchTrack, b.pitchTrack, t);
        r.tuneSemis    = lerpf(a.tuneSemis, b.tuneSemis, t);
        r.vowel        = lerpf(a.vowel, b.vowel, t);
        r.formantShift = lerpf(a.formantShift, b.formantShift, t);
        return r;
    }
}

void DualFilterEngine::process(float* L, float* R, int n) noexcept
{
    if (snap) { cur = tgt; snap = false; }

    int pos = 0;
    while (pos < n)
    {
        const int   m   = std::min(kSlice, n - pos);
        const float t0  = (float) pos / (float) n;
        const float t1  = (float) (pos + m) / (float) n;       // i coefficienti della slice sono quelli di fine slice
        const FP f1 = mixParams(cur.f[0], tgt.f[0], t1);
        const FP f2 = mixParams(cur.f[1], tgt.f[1], t1);
        const float noteHz = lerpf(cur.noteHz, tgt.noteHz, t1);

        float* l = L + pos;
        float* r = R + pos;

        if (! f1.on) units[0].wasOn = false;
        if (! f2.on) units[1].wasOn = false;

        if (tgt.routing == FilterRouting::Serial)
        {
            if (f1.on) runUnit(0, f1, noteHz, l, r, l, r, m);
            if (f2.on) runUnit(1, f2, noteHz, l, r, l, r, m);
        }
        else
        {
            if (f1.on) runUnit(0, f1, noteHz, l, r, aL.data(), aR.data(), m);
            if (f2.on) runUnit(1, f2, noteHz, l, r, bL.data(), bR.data(), m);

            if (tgt.routing == FilterRouting::Parallel)
            {
                if (f1.on && f2.on)
                    for (int i = 0; i < m; ++i) { l[i] = 0.5f * (aL[(size_t) i] + bL[(size_t) i]); r[i] = 0.5f * (aR[(size_t) i] + bR[(size_t) i]); }
                else if (f1.on)
                    for (int i = 0; i < m; ++i) { l[i] = aL[(size_t) i]; r[i] = aR[(size_t) i]; }
                else if (f2.on)
                    for (int i = 0; i < m; ++i) { l[i] = bL[(size_t) i]; r[i] = bR[(size_t) i]; }
            }
            else   // Morph: crossfade lineare con rampa per-campione dentro la slice (filtro spento = bypass)
            {
                const float mA = lerpf(cur.morph, tgt.morph, t0), mB = lerpf(cur.morph, tgt.morph, t1);
                const float step = (mB - mA) / (float) m;
                float mm = mA;
                for (int i = 0; i < m; ++i)
                {
                    const float xl = f1.on ? aL[(size_t) i] : l[i], yl = f2.on ? bL[(size_t) i] : l[i];
                    const float xr = f1.on ? aR[(size_t) i] : r[i], yr = f2.on ? bR[(size_t) i] : r[i];
                    const float k = std::min(std::max(mm, 0.0f), 1.0f);
                    l[i] = xl + k * (yl - xl);
                    r[i] = xr + k * (yr - xr);
                    mm += step;
                }
            }
        }
        pos += m;
    }
    cur = tgt;
}
}
