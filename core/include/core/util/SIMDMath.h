#pragma once

// =============================================================================
// SIMDMath — SIMD-accelerated math primitives for the WDF hot path.
//
// Provides float4 wrapper for SSE2 (x86) or scalar fallback.
// Used for parallel evaluation of 3 nonlinear reluctances + 1 spare slot
// in a single SIMD operation (4-wide).
//
// Design decision: SSE2 baseline (available on all x64 CPUs since 2003).
// AVX optional for future 8-wide operations.
//
// Reference: chowdsp_wdf uses XSIMD for portability. We keep it minimal
// here with direct intrinsics + scalar fallback.
// =============================================================================

#include <cmath>
#include <algorithm>

// Detect SIMD availability
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
    #define TRANSFO_HAS_SSE2 1
    #include <emmintrin.h>
    #include <xmmintrin.h>
#else
    #define TRANSFO_HAS_SSE2 0
#endif

namespace transfo {

// ─── float4: 4-wide SIMD float vector ───────────────────────────────────────

#if TRANSFO_HAS_SSE2

struct float4
{
    __m128 v;

    float4() : v(_mm_setzero_ps()) {}
    explicit float4(float s) : v(_mm_set1_ps(s)) {}
    float4(__m128 val) : v(val) {}
    float4(float a, float b, float c, float d) : v(_mm_set_ps(d, c, b, a)) {}

    // Load/Store (aligned)
    static float4 load(const float* ptr)  { return _mm_load_ps(ptr); }
    void store(float* ptr) const          { _mm_store_ps(ptr, v); }

    // Load/Store (unaligned)
    static float4 loadu(const float* ptr) { return _mm_loadu_ps(ptr); }
    void storeu(float* ptr) const         { _mm_storeu_ps(ptr, v); }

    // Extract single element
    float operator[](int i) const
    {
        alignas(16) float tmp[4];
        _mm_store_ps(tmp, v);
        return tmp[i];
    }

    // Arithmetic
    float4 operator+(float4 b) const { return _mm_add_ps(v, b.v); }
    float4 operator-(float4 b) const { return _mm_sub_ps(v, b.v); }
    float4 operator*(float4 b) const { return _mm_mul_ps(v, b.v); }
    float4 operator/(float4 b) const { return _mm_div_ps(v, b.v); }

    float4& operator+=(float4 b) { v = _mm_add_ps(v, b.v); return *this; }
    float4& operator-=(float4 b) { v = _mm_sub_ps(v, b.v); return *this; }
    float4& operator*=(float4 b) { v = _mm_mul_ps(v, b.v); return *this; }

    // Comparison (returns mask)
    float4 operator<(float4 b)  const { return _mm_cmplt_ps(v, b.v); }
    float4 operator>(float4 b)  const { return _mm_cmpgt_ps(v, b.v); }

    // Bitwise (for masks)
    float4 operator&(float4 b) const { return _mm_and_ps(v, b.v); }
    float4 operator|(float4 b) const { return _mm_or_ps(v, b.v); }

    // Select: mask ? a : b
    static float4 select(float4 mask, float4 a, float4 b)
    {
        // SSE4.1: _mm_blendv_ps — fallback for SSE2:
        return _mm_or_ps(_mm_and_ps(mask.v, a.v),
                         _mm_andnot_ps(mask.v, b.v));
    }

    // Horizontal sum (for reductions)
    float hsum() const
    {
        __m128 shuf = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1));
        __m128 sums = _mm_add_ps(v, shuf);
        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        return _mm_cvtss_f32(sums);
    }
};

// Abs
inline float4 abs(float4 a)
{
    static const __m128 signMask = _mm_set1_ps(-0.0f);
    return _mm_andnot_ps(signMask, a.v);
}

// Min/Max
inline float4 min(float4 a, float4 b) { return _mm_min_ps(a.v, b.v); }
inline float4 max(float4 a, float4 b) { return _mm_max_ps(a.v, b.v); }

// Clamp
inline float4 clamp(float4 x, float4 lo, float4 hi)
{
    return min(max(x, lo), hi);
}

// Reciprocal approximation (fast, ~12-bit precision)
inline float4 rcp_approx(float4 a) { return _mm_rcp_ps(a.v); }

#else
// ─── Scalar fallback ────────────────────────────────────────────────────────

struct float4
{
    float v[4];

    float4() : v{0, 0, 0, 0} {}
    explicit float4(float s) : v{s, s, s, s} {}
    float4(float a, float b, float c, float d) : v{a, b, c, d} {}

    static float4 load(const float* ptr)  { float4 r; for (int i=0;i<4;++i) r.v[i]=ptr[i]; return r; }
    static float4 loadu(const float* ptr) { return load(ptr); }
    void store(float* ptr) const  { for (int i=0;i<4;++i) ptr[i]=v[i]; }
    void storeu(float* ptr) const { store(ptr); }

    float operator[](int i) const { return v[i]; }

    float4 operator+(float4 b) const { return {v[0]+b.v[0], v[1]+b.v[1], v[2]+b.v[2], v[3]+b.v[3]}; }
    float4 operator-(float4 b) const { return {v[0]-b.v[0], v[1]-b.v[1], v[2]-b.v[2], v[3]-b.v[3]}; }
    float4 operator*(float4 b) const { return {v[0]*b.v[0], v[1]*b.v[1], v[2]*b.v[2], v[3]*b.v[3]}; }
    float4 operator/(float4 b) const { return {v[0]/b.v[0], v[1]/b.v[1], v[2]/b.v[2], v[3]/b.v[3]}; }

    float4& operator+=(float4 b) { for(int i=0;i<4;++i) v[i]+=b.v[i]; return *this; }
    float4& operator-=(float4 b) { for(int i=0;i<4;++i) v[i]-=b.v[i]; return *this; }
    float4& operator*=(float4 b) { for(int i=0;i<4;++i) v[i]*=b.v[i]; return *this; }

    float hsum() const { return v[0]+v[1]+v[2]+v[3]; }
};

inline float4 abs(float4 a)   { return {std::abs(a.v[0]),std::abs(a.v[1]),std::abs(a.v[2]),std::abs(a.v[3])}; }
inline float4 min(float4 a, float4 b) { return {std::min(a.v[0],b.v[0]),std::min(a.v[1],b.v[1]),std::min(a.v[2],b.v[2]),std::min(a.v[3],b.v[3])}; }
inline float4 max(float4 a, float4 b) { return {std::max(a.v[0],b.v[0]),std::max(a.v[1],b.v[1]),std::max(a.v[2],b.v[2]),std::max(a.v[3],b.v[3])}; }
inline float4 clamp(float4 x, float4 lo, float4 hi) { return min(max(x, lo), hi); }

#endif

// ─── Scalar math helpers (branchless where possible) ────────────────────────

inline float fastTanh(float x)
{
    // Pade [3/3] approximation — branchless, good for |x| < 4
    // tanh(x) ~ x(15 + x^2) / (15 + 6*x^2)  — Pade [1/1] is too rough
    // Using: tanh(x) ~ x*(135135 + x^2*(17325 + x^2*(378 + x^2)))
    //                 / (135135 + x^2*(62370 + x^2*(3150 + 28*x^2)))
    // Simplified Pade [3/3]:
    const float x2 = x * x;
    if (x2 > 16.0f) return (x > 0.0f) ? 1.0f : -1.0f;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return num / den;
}

inline float sign(float x)
{
    return (x >= 0.0f) ? 1.0f : -1.0f;
}

} // namespace transfo
