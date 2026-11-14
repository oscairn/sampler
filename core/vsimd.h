// SPDX-License-Identifier: AGPL-3.0-or-later
// The vector type: 4 float lanes on NEON or SSE2, one lane when scalar (-DPV_SCALAR, or no SIMD
// found). The voice code is written once against this API; W is the number of voices a lane holds.
// (AVX2 would be 8 lanes behind the same names.)
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

#if defined(PV_SCALAR)
#define PV_IMPL "scalar"
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define PV_NEON 1
#define PV_IMPL "neon"
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)  // MSVC defines no __SSE2__
#define PV_SSE2 1
#define PV_IMPL "sse2"
#include <emmintrin.h>
#else
#define PV_SCALAR 1
#define PV_IMPL "scalar"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define PV_INLINE __forceinline
#define PV_NOINLINE __declspec(noinline)
#else
#define PV_INLINE inline __attribute__((always_inline))
#define PV_NOINLINE __attribute__((noinline))
#endif

namespace pv {

#if PV_NEON
constexpr int W = 4;
struct vf { float32x4_t v; };
struct vm { uint32x4_t v; };
PV_INLINE vf splat(float x) { return {vdupq_n_f32(x)}; }
PV_INLINE vf load(const float *p) { return {vld1q_f32(p)}; }
PV_INLINE void store(float *p, vf a) { vst1q_f32(p, a.v); }
PV_INLINE void storeu(float *p, vf a) { vst1q_f32(p, a.v); }
PV_INLINE vf operator+(vf a, vf b) { return {vaddq_f32(a.v, b.v)}; }
PV_INLINE vf operator-(vf a, vf b) { return {vsubq_f32(a.v, b.v)}; }
PV_INLINE vf operator*(vf a, vf b) { return {vmulq_f32(a.v, b.v)}; }
PV_INLINE vf operator/(vf a, vf b) { return {vdivq_f32(a.v, b.v)}; }
PV_INLINE vf operator-(vf a) { return {vnegq_f32(a.v)}; }
PV_INLINE vf fma(vf a, vf b, vf c) { return {vfmaq_f32(c.v, a.v, b.v)}; } // a*b+c
PV_INLINE vf vmin(vf a, vf b) { return {vminq_f32(a.v, b.v)}; }
PV_INLINE vf vmax(vf a, vf b) { return {vmaxq_f32(a.v, b.v)}; }
PV_INLINE vf vabs(vf a) { return {vabsq_f32(a.v)}; }
PV_INLINE vm operator<(vf a, vf b) { return {vcltq_f32(a.v, b.v)}; }
PV_INLINE vm operator>=(vf a, vf b) { return {vcgeq_f32(a.v, b.v)}; }
PV_INLINE vm operator&(vm a, vm b) { return {vandq_u32(a.v, b.v)}; }
PV_INLINE vf sel(vm m, vf a, vf b) { return {vbslq_f32(m.v, a.v, b.v)}; }
PV_INLINE vf and0(vm m, vf a) { return {vreinterpretq_f32_u32(vandq_u32(m.v, vreinterpretq_u32_f32(a.v)))}; }
PV_INLINE vf vfloor(vf a) { return {vrndmq_f32(a.v)}; }
// a * 2^n for integral n
PV_INLINE vf ldexpi(vf a, vf n) { return {vreinterpretq_f32_s32(vaddq_s32(vreinterpretq_s32_f32(a.v), vshlq_n_s32(vcvtq_s32_f32(n.v), 23)))}; }
PV_INLINE float hsum(vf a) { return vaddvq_f32(a.v); }
PV_INLINE bool any(vm m) { return vmaxvq_u32(m.v) != 0; }
#elif PV_SSE2
constexpr int W = 4;
struct vf { __m128 v; };
struct vm { __m128 v; };
PV_INLINE vf splat(float x) { return {_mm_set1_ps(x)}; }
PV_INLINE vf load(const float *p) { return {_mm_load_ps(p)}; }
PV_INLINE void store(float *p, vf a) { _mm_store_ps(p, a.v); }
PV_INLINE void storeu(float *p, vf a) { _mm_storeu_ps(p, a.v); }
PV_INLINE vf operator+(vf a, vf b) { return {_mm_add_ps(a.v, b.v)}; }
PV_INLINE vf operator-(vf a, vf b) { return {_mm_sub_ps(a.v, b.v)}; }
PV_INLINE vf operator*(vf a, vf b) { return {_mm_mul_ps(a.v, b.v)}; }
PV_INLINE vf operator/(vf a, vf b) { return {_mm_div_ps(a.v, b.v)}; }
PV_INLINE vf operator-(vf a) { return {_mm_xor_ps(a.v, _mm_set1_ps(-0.f))}; }
PV_INLINE vf fma(vf a, vf b, vf c) { return {_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)}; }
PV_INLINE vf vmin(vf a, vf b) { return {_mm_min_ps(a.v, b.v)}; }
PV_INLINE vf vmax(vf a, vf b) { return {_mm_max_ps(a.v, b.v)}; }
PV_INLINE vf vabs(vf a) { return {_mm_andnot_ps(_mm_set1_ps(-0.f), a.v)}; }
PV_INLINE vm operator<(vf a, vf b) { return {_mm_cmplt_ps(a.v, b.v)}; }
PV_INLINE vm operator>=(vf a, vf b) { return {_mm_cmpge_ps(a.v, b.v)}; }
PV_INLINE vm operator&(vm a, vm b) { return {_mm_and_ps(a.v, b.v)}; }
PV_INLINE vf sel(vm m, vf a, vf b) { return {_mm_or_ps(_mm_and_ps(m.v, a.v), _mm_andnot_ps(m.v, b.v))}; }
PV_INLINE vf and0(vm m, vf a) { return {_mm_and_ps(m.v, a.v)}; }
PV_INLINE vf vfloor(vf a) {
    __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v));
    return {_mm_sub_ps(t, _mm_and_ps(_mm_cmpgt_ps(t, a.v), _mm_set1_ps(1.f)))};
}
PV_INLINE vf ldexpi(vf a, vf n) { return {_mm_castsi128_ps(_mm_add_epi32(_mm_castps_si128(a.v), _mm_slli_epi32(_mm_cvttps_epi32(n.v), 23)))}; }
PV_INLINE float hsum(vf a) {
    __m128 s = _mm_add_ps(a.v, _mm_movehl_ps(a.v, a.v));
    return _mm_cvtss_f32(_mm_add_ss(s, _mm_shuffle_ps(s, s, 1)));
}
PV_INLINE bool any(vm m) { return _mm_movemask_ps(m.v) != 0; }
#else
constexpr int W = 1;
struct vf { float v; };
struct vm { bool v; };
PV_INLINE vf splat(float x) { return {x}; }
PV_INLINE vf load(const float *p) { return {*p}; }
PV_INLINE void store(float *p, vf a) { *p = a.v; }
PV_INLINE void storeu(float *p, vf a) { *p = a.v; }
PV_INLINE vf operator+(vf a, vf b) { return {a.v + b.v}; }
PV_INLINE vf operator-(vf a, vf b) { return {a.v - b.v}; }
PV_INLINE vf operator*(vf a, vf b) { return {a.v * b.v}; }
PV_INLINE vf operator/(vf a, vf b) { return {a.v / b.v}; }
PV_INLINE vf operator-(vf a) { return {-a.v}; }
PV_INLINE vf fma(vf a, vf b, vf c) { return {a.v * b.v + c.v}; }
PV_INLINE vf vmin(vf a, vf b) { return {a.v < b.v ? a.v : b.v}; }
PV_INLINE vf vmax(vf a, vf b) { return {a.v > b.v ? a.v : b.v}; }
PV_INLINE vf vabs(vf a) { return {std::fabs(a.v)}; }
PV_INLINE vm operator<(vf a, vf b) { return {a.v < b.v}; }
PV_INLINE vm operator>=(vf a, vf b) { return {a.v >= b.v}; }
PV_INLINE vm operator&(vm a, vm b) { return {a.v && b.v}; }
PV_INLINE vf sel(vm m, vf a, vf b) { return {m.v ? a.v : b.v}; }
PV_INLINE vf and0(vm m, vf a) { return {m.v ? a.v : 0.f}; }
PV_INLINE vf vfloor(vf a) { return {std::floor(a.v)}; }
PV_INLINE vf ldexpi(vf a, vf n) {
    int32_t i; std::memcpy(&i, &a.v, 4); i += int32_t(n.v) << 23; float r; std::memcpy(&r, &i, 4); return {r};
}
PV_INLINE float hsum(vf a) { return a.v; }
PV_INLINE bool any(vm m) { return m.v; }
#endif

// 2^x (Taylor degree 5 on the fraction: <=1.6e-4 relative, ~0.3 cent), control rate only
PV_INLINE vf exp2v(vf x) {
    x = vmax(vmin(x, splat(30.f)), splat(-30.f));
    vf n = vfloor(x), f = x - n;
    vf p = fma(f, splat(1.3333558e-3f), splat(9.6181291e-3f));
    p = fma(p, f, splat(5.5504109e-2f));
    p = fma(p, f, splat(2.4022650e-1f));
    p = fma(p, f, splat(6.9314718e-1f));
    p = fma(p, f, splat(1.f));
    return ldexpi(p, n);
}

} // namespace pv

// ---- 32.32 fixed-point read positions, int16 tap loads, transposes ----
namespace pv {
#if PV_NEON
struct vp { int64x2_t a, b; };                       // W positions, 32 integer . 32 fraction bits
PV_INLINE vp operator+(vp x, vp y) { return {vaddq_s64(x.a, y.a), vaddq_s64(x.b, y.b)}; }
PV_INLINE vp pload(const int64_t *p) { return {vld1q_s64(p), vld1q_s64(p + 2)}; }
PV_INLINE void pstore(int64_t *p, vp x) { vst1q_s64(p, x.a); vst1q_s64(p + 2, x.b); }
PV_INLINE void pidx(vp x, int32_t *o) { vst1q_s32(o, vcombine_s32(vshrn_n_s64(x.a, 32), vshrn_n_s64(x.b, 32))); }
PV_INLINE vf pfrac(vp x) { // low 32 bits, top 24 of them, as [0,1)
    uint32x4_t lo = vcombine_u32(vmovn_u64(vreinterpretq_u64_s64(x.a)), vmovn_u64(vreinterpretq_u64_s64(x.b)));
    return {vcvtq_n_f32_u32(vshrq_n_u32(lo, 8), 24)};
}
PV_INLINE vp pfix(vf s) { // float (|s| < 128) to 32.32, 24 fraction bits kept
    int32x4_t i = vcvtq_n_s32_f32(s.v, 24);
    return {vshll_n_s32(vget_low_s32(i), 8), vshll_high_n_s32(i, 8)};
}
// a kernel row's offset (the top B fraction bits times `stride`, < 32768) and the next 24 bits as [0,1)
template <int B> PV_INLINE void prow(vp x, int32_t stride, int32_t *row, vf &u) {
    uint32x4_t lo = vcombine_u32(vmovn_u64(vreinterpretq_u64_s64(x.a)), vmovn_u64(vreinterpretq_u64_s64(x.b)));
    vst1q_s32(row, vreinterpretq_s32_u32(vmulq_n_u32(vshrq_n_u32(lo, 32 - B), uint32_t(stride))));
    u = {vcvtq_n_f32_u32(vandq_u32(vshrq_n_u32(lo, 8 - B), vdupq_n_u32(0xffffff)), 24)};
}
PV_INLINE vf ld4(const int16_t *p) { return {vcvtq_f32_s32(vmovl_s16(vld1_s16(p)))}; }
PV_INLINE vf ld4(const float *p) { return {vld1q_f32(p)}; }
PV_INLINE void ld4x2(const int16_t *p, vf &l, vf &r) { int16x4x2_t t = vld2_s16(p); l = {vcvtq_f32_s32(vmovl_s16(t.val[0]))}; r = {vcvtq_f32_s32(vmovl_s16(t.val[1]))}; }
PV_INLINE void ld4x2(const float *p, vf &l, vf &r) { float32x4x2_t t = vld2q_f32(p); l = {t.val[0]}; r = {t.val[1]}; }
PV_INLINE void transpose(vf &a, vf &b, vf &c, vf &d) {
    float32x4_t t0 = vtrn1q_f32(a.v, b.v), t1 = vtrn2q_f32(a.v, b.v), t2 = vtrn1q_f32(c.v, d.v), t3 = vtrn2q_f32(c.v, d.v);
    a.v = vreinterpretq_f32_f64(vtrn1q_f64(vreinterpretq_f64_f32(t0), vreinterpretq_f64_f32(t2)));
    b.v = vreinterpretq_f32_f64(vtrn1q_f64(vreinterpretq_f64_f32(t1), vreinterpretq_f64_f32(t3)));
    c.v = vreinterpretq_f32_f64(vtrn2q_f64(vreinterpretq_f64_f32(t0), vreinterpretq_f64_f32(t2)));
    d.v = vreinterpretq_f32_f64(vtrn2q_f64(vreinterpretq_f64_f32(t1), vreinterpretq_f64_f32(t3)));
}
// lane k of the result = sum of the lanes of the k-th argument
PV_INLINE vf hsum4(vf a, vf b, vf c, vf d) { return {vpaddq_f32(vpaddq_f32(a.v, b.v), vpaddq_f32(c.v, d.v))}; }
PV_INLINE float lane(vf a, int k) { float t[4]; vst1q_f32(t, a.v); return t[k]; }
#elif PV_SSE2
struct vp { __m128i a, b; };
PV_INLINE vp operator+(vp x, vp y) { return {_mm_add_epi64(x.a, y.a), _mm_add_epi64(x.b, y.b)}; }
PV_INLINE vp pload(const int64_t *p) { return {_mm_load_si128((const __m128i *)p), _mm_load_si128((const __m128i *)(p + 2))}; }
PV_INLINE void pstore(int64_t *p, vp x) { _mm_store_si128((__m128i *)p, x.a); _mm_store_si128((__m128i *)(p + 2), x.b); }
PV_INLINE void pidx(vp x, int32_t *o) { _mm_store_ps((float *)o, _mm_shuffle_ps(_mm_castsi128_ps(x.a), _mm_castsi128_ps(x.b), _MM_SHUFFLE(3, 1, 3, 1))); }
PV_INLINE vf pfrac(vp x) {
    __m128i lo = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(x.a), _mm_castsi128_ps(x.b), _MM_SHUFFLE(2, 0, 2, 0)));
    return {_mm_mul_ps(_mm_cvtepi32_ps(_mm_srli_epi32(lo, 8)), _mm_set1_ps(1.f / 16777216.f))};
}
PV_INLINE vp pfix(vf s) {
    __m128i i = _mm_cvtps_epi32(_mm_mul_ps(s.v, _mm_set1_ps(16777216.f))), sg = _mm_srai_epi32(i, 31);
    return {_mm_slli_epi64(_mm_unpacklo_epi32(i, sg), 8), _mm_slli_epi64(_mm_unpackhi_epi32(i, sg), 8)};
}
template <int B> PV_INLINE void prow(vp x, int32_t stride, int32_t *row, vf &u) {
    __m128i lo = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(x.a), _mm_castsi128_ps(x.b), _MM_SHUFFLE(2, 0, 2, 0)));
    _mm_store_si128((__m128i *)row, _mm_madd_epi16(_mm_srli_epi32(lo, 32 - B), _mm_set1_epi32(stride)));  // 16-bit products: phase x stride
    u = {_mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(lo, 8 - B), _mm_set1_epi32(0xffffff))), _mm_set1_ps(1.f / 16777216.f))};
}
PV_INLINE vf ld4(const int16_t *p) { __m128i x = _mm_loadl_epi64((const __m128i *)p); return {_mm_cvtepi32_ps(_mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16))}; }
PV_INLINE vf ld4(const float *p) { return {_mm_loadu_ps(p)}; }
PV_INLINE void ld4x2(const int16_t *p, vf &l, vf &r) {
    __m128i x = _mm_loadu_si128((const __m128i *)p);
    l = {_mm_cvtepi32_ps(_mm_srai_epi32(_mm_slli_epi32(x, 16), 16))}; r = {_mm_cvtepi32_ps(_mm_srai_epi32(x, 16))};
}
PV_INLINE void ld4x2(const float *p, vf &l, vf &r) {
    __m128 x = _mm_loadu_ps(p), y = _mm_loadu_ps(p + 4);
    l = {_mm_shuffle_ps(x, y, _MM_SHUFFLE(2, 0, 2, 0))}; r = {_mm_shuffle_ps(x, y, _MM_SHUFFLE(3, 1, 3, 1))};
}
PV_INLINE void transpose(vf &a, vf &b, vf &c, vf &d) { _MM_TRANSPOSE4_PS(a.v, b.v, c.v, d.v); }
PV_INLINE vf hsum4(vf a, vf b, vf c, vf d) { transpose(a, b, c, d); return {_mm_add_ps(_mm_add_ps(a.v, b.v), _mm_add_ps(c.v, d.v))}; }
PV_INLINE float lane(vf a, int k) { alignas(16) float t[4]; _mm_store_ps(t, a.v); return t[k]; }
#else
struct vp { int64_t a; };
PV_INLINE vp operator+(vp x, vp y) { return {x.a + y.a}; }
PV_INLINE vp pload(const int64_t *p) { return {*p}; }
PV_INLINE void pstore(int64_t *p, vp x) { *p = x.a; }
PV_INLINE void pidx(vp x, int32_t *o) { *o = int32_t(x.a >> 32); }
PV_INLINE vf pfrac(vp x) { return {float(uint32_t(x.a) >> 8) * (1.f / 16777216.f)}; }
template <int B> PV_INLINE void prow(vp x, int32_t stride, int32_t *row, vf &u) { const uint32_t lo = uint32_t(x.a); *row = int32_t(lo >> (32 - B)) * stride; u = {float((lo >> (8 - B)) & 0xffffff) * (1.f / 16777216.f)}; }
PV_INLINE vp pfix(vf s) { return {int64_t(int32_t(std::lrintf(s.v * 16777216.f))) * 256}; }
PV_INLINE float lane(vf a, int) { return a.v; }
#endif
} // namespace pv
