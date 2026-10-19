#pragma once
#include <algorithm>
#include <cmath>

/*
 * Mini-wrapper SIMD a 4 lane float (header-only, nessuna dipendenza).
 *
 *   SSE2   : x86/x64 (sempre disponibile su x86-64, anche MSVC)
 *   NEON   : ARM64 / Apple Silicon  (NON testato in questo ambiente: compilato solo logicamente, vedi note)
 *   Scalar : fallback portabile, usato anche con -DBF_SIMD_FORCE_SCALAR per confrontare i risultati.
 *
 * Volutamente SENZA divisione vettoriale (non esiste su ARMv7): tutte le non-linearita' sono
 * polinomiali / lineari a tratti, quindi identiche (a meno degli arrotondamenti) su ogni backend.
 */
#if defined(BF_SIMD_FORCE_SCALAR)
  #define BF_SIMD_SCALAR 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))
  #define BF_SIMD_SSE2 1
  #include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
  #define BF_SIMD_NEON 1
  #include <arm_neon.h>
#else
  #define BF_SIMD_SCALAR 1
#endif

namespace bf::simd
{
#if defined(BF_SIMD_SSE2)
    inline constexpr const char* kBackendName = "SSE2";

    struct f4 { __m128 v; };
    inline f4 set1(float x) noexcept                        { return { _mm_set1_ps(x) }; }
    inline f4 set4(float a, float b, float c, float d) noexcept { return { _mm_setr_ps(a, b, c, d) }; }
    inline f4 loadu(const float* p) noexcept                { return { _mm_loadu_ps(p) }; }
    inline void storeu(float* p, f4 x) noexcept             { _mm_storeu_ps(p, x.v); }
    inline f4 operator+(f4 a, f4 b) noexcept                { return { _mm_add_ps(a.v, b.v) }; }
    inline f4 operator-(f4 a, f4 b) noexcept                { return { _mm_sub_ps(a.v, b.v) }; }
    inline f4 operator*(f4 a, f4 b) noexcept                { return { _mm_mul_ps(a.v, b.v) }; }
    inline f4 vmin(f4 a, f4 b) noexcept                     { return { _mm_min_ps(a.v, b.v) }; }
    inline f4 vmax(f4 a, f4 b) noexcept                     { return { _mm_max_ps(a.v, b.v) }; }
    inline f4 vabs(f4 x) noexcept                           { return { _mm_andnot_ps(_mm_set1_ps(-0.0f), x.v) }; }
    /** |mag| con il segno di `sgn`. */
    inline f4 copysign(f4 mag, f4 sgn) noexcept
    {
        const __m128 m = _mm_set1_ps(-0.0f);
        return { _mm_or_ps(_mm_andnot_ps(m, mag.v), _mm_and_ps(m, sgn.v)) };
    }
    template <int N> inline float lane(f4 x) noexcept       { return _mm_cvtss_f32(_mm_shuffle_ps(x.v, x.v, _MM_SHUFFLE(N, N, N, N))); }
    inline float hsum(f4 x) noexcept
    {
        __m128 t = _mm_add_ps(x.v, _mm_movehl_ps(x.v, x.v));
        t = _mm_add_ss(t, _mm_shuffle_ps(t, t, 1));
        return _mm_cvtss_f32(t);
    }
    /** [x, p0, p1, p2]: fa scorrere i lane di 1 posto e inserisce x in lane 0 (pipeline degli stadi in cascata). */
    inline f4 shiftIn(float x, f4 p) noexcept
    {
        const __m128 t = _mm_shuffle_ps(p.v, p.v, _MM_SHUFFLE(2, 1, 0, 0));
        return { _mm_move_ss(t, _mm_set_ss(x)) };
    }

#elif defined(BF_SIMD_NEON)
    inline constexpr const char* kBackendName = "NEON";

    struct f4 { float32x4_t v; };
    inline f4 set1(float x) noexcept                        { return { vdupq_n_f32(x) }; }
    inline f4 set4(float a, float b, float c, float d) noexcept { const float t[4] = { a, b, c, d }; return { vld1q_f32(t) }; }
    inline f4 loadu(const float* p) noexcept                { return { vld1q_f32(p) }; }
    inline void storeu(float* p, f4 x) noexcept             { vst1q_f32(p, x.v); }
    inline f4 operator+(f4 a, f4 b) noexcept                { return { vaddq_f32(a.v, b.v) }; }
    inline f4 operator-(f4 a, f4 b) noexcept                { return { vsubq_f32(a.v, b.v) }; }
    inline f4 operator*(f4 a, f4 b) noexcept                { return { vmulq_f32(a.v, b.v) }; }
    inline f4 vmin(f4 a, f4 b) noexcept                     { return { vminq_f32(a.v, b.v) }; }
    inline f4 vmax(f4 a, f4 b) noexcept                     { return { vmaxq_f32(a.v, b.v) }; }
    inline f4 vabs(f4 x) noexcept                           { return { vabsq_f32(x.v) }; }
    inline f4 copysign(f4 mag, f4 sgn) noexcept             { return { vbslq_f32(vdupq_n_u32(0x80000000u), sgn.v, mag.v) }; }
    template <int N> inline float lane(f4 x) noexcept       { return vgetq_lane_f32(x.v, N); }
   #if defined(__aarch64__) || defined(_M_ARM64)
    inline float hsum(f4 x) noexcept                        { return vaddvq_f32(x.v); }          // AArch64
   #else
    // ARMv7 (Android armeabi-v7a): vaddvq_f32 non esiste, somma a coppie
    inline float hsum(f4 x) noexcept
    {
        const float32x2_t s = vadd_f32(vget_low_f32(x.v), vget_high_f32(x.v));
        return vget_lane_f32(vpadd_f32(s, s), 0);
    }
   #endif
    inline f4 shiftIn(float x, f4 p) noexcept               { return { vextq_f32(vdupq_n_f32(x), p.v, 3) }; }   // [x, p0, p1, p2]

#else // scalare
    inline constexpr const char* kBackendName = "Scalar";

    struct f4 { float v[4]; };
    inline f4 set1(float x) noexcept                        { return { { x, x, x, x } }; }
    inline f4 set4(float a, float b, float c, float d) noexcept { return { { a, b, c, d } }; }
    inline f4 loadu(const float* p) noexcept                { return { { p[0], p[1], p[2], p[3] } }; }
    inline void storeu(float* p, f4 x) noexcept             { for (int i = 0; i < 4; ++i) p[i] = x.v[i]; }
    #define BF_F4_BIN(NAME, EXPR) inline f4 NAME(f4 a, f4 b) noexcept { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = (EXPR); return r; }
    BF_F4_BIN(operator+, a.v[i] + b.v[i])
    BF_F4_BIN(operator-, a.v[i] - b.v[i])
    BF_F4_BIN(operator*, a.v[i] * b.v[i])
    BF_F4_BIN(vmin, std::min(a.v[i], b.v[i]))
    BF_F4_BIN(vmax, std::max(a.v[i], b.v[i]))
    BF_F4_BIN(copysign, std::copysign(a.v[i], b.v[i]))
    #undef BF_F4_BIN
    inline f4 vabs(f4 x) noexcept                           { f4 r; for (int i = 0; i < 4; ++i) r.v[i] = std::fabs(x.v[i]); return r; }
    template <int N> inline float lane(f4 x) noexcept       { return x.v[N]; }
    inline float hsum(f4 x) noexcept                        { return (x.v[0] + x.v[2]) + (x.v[1] + x.v[3]); }
    inline f4 shiftIn(float x, f4 p) noexcept               { return { { x, p.v[0], p.v[1], p.v[2] } }; }
#endif

    //==========================================================================
    // Non-linearita' "sicure": nessuna divisione, nessun tanh, uscita sempre limitata.
    //==========================================================================

    /** Limiter a ginocchio morbido: lineare fino a 0.8, ginocchio quadratico C1, |y| <= 1.0 SEMPRE. */
    inline float softLimit(float x) noexcept
    {
        constexpr float T = 0.8f, W = 0.2f;
        const float a = std::fabs(x);
        const float u = std::min(std::max(a - T, 0.0f), 2.0f * W);
        return std::copysign(std::min(a, T) + u - u * u * (1.0f / (4.0f * W)), x);
    }
    inline f4 softLimit(f4 x) noexcept
    {
        constexpr float T = 0.8f, W = 0.2f;
        const f4 a = vabs(x);
        const f4 u = vmin(vmax(a - set1(T), set1(0.0f)), set1(2.0f * W));
        return copysign(vmin(a, set1(T)) + u - u * u * set1(1.0f / (4.0f * W)), x);
    }

    /** Soft-clip cubico x - 4x^3/27 su [-1.5, 1.5] (pendenza 0 ai bordi, picco 1.0): saturazione "da drive". */
    inline float softClip3(float x) noexcept
    {
        x = std::min(std::max(x, -1.5f), 1.5f);
        return x - (4.0f / 27.0f) * x * x * x;
    }
    inline f4 softClip3(f4 x) noexcept
    {
        x = vmin(vmax(x, set1(-1.5f)), set1(1.5f));
        return x - set1(4.0f / 27.0f) * x * x * x;
    }
}
