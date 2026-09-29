// LivePaper - small maths and colour helpers used by every scene.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace lp {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float X, float Y) : x(X), y(Y) {}
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
};

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2  Lerp(const Vec2& a, const Vec2& b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) }; }
inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Smoothstep(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline float Sign(float v) { return v < 0 ? -1.0f : 1.0f; }

// Frame-rate independent exponential approach.
inline float Damp(float current, float target, float smoothing, float dt) {
    if (smoothing <= 0) return target;
    return Lerp(current, target, 1.0f - std::exp(-dt / smoothing));
}

inline float Fract(float v) { return v - std::floor(v); }

// Cheap 1D/2D value noise. Deterministic, no tables, plenty for organic motion.
inline float Hash1(uint32_t n) {
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffffu) / 1073741824.0f - 1.0f; // -1..1
}
inline float Hash2(int x, int y) {
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffffu) / 8388608.0f - 1.0f;
}

inline float Noise1(float x) {
    int i = (int)std::floor(x);
    float f = x - (float)i;
    float u = f * f * (3.0f - 2.0f * f);
    return Lerp(Hash1((uint32_t)i), Hash1((uint32_t)(i + 1)), u);
}

inline float Noise2(float x, float y) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float a = Hash2(ix, iy),     b = Hash2(ix + 1, iy);
    float c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
    return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
}

// Fractal brownian motion, 3 octaves by default (cheap enough for per-frame use).
inline float Fbm(float x, float y, int octaves = 3) {
    float sum = 0, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * Noise2(x * freq, y * freq);
        amp *= 0.5f; freq *= 2.03f;
    }
    return sum;
}

// Deterministic LCG so scenes look identical across runs (important for tests).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1u) : s(seed ? seed : 1u) {}
    uint32_t Next() { s = s * 1664525u + 1013904223u; return s; }
    float Unit() { return (float)(Next() >> 8) / 16777216.0f; }        // 0..1
    float Range(float a, float b) { return a + (b - a) * Unit(); }     // a..b
    int   Int(int a, int b) { return a + (int)(Next() % (uint32_t)(b - a + 1)); }
};

// 0xAARRGGBB to match COLORREF-free Direct2D usage.
struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color() = default;
    Color(float R, float G, float B, float A = 1.0f) : r(R), g(G), b(B), a(A) {}
    Color operator*(float s) const { return { r * s, g * s, b * s, a }; }
    Color WithAlpha(float A) const { return { r, g, b, A }; }
    static Color Hex(uint32_t rgb, float a = 1.0f) {
        return { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a };
    }
    static Color Hsv(float h, float s, float v, float a = 1.0f) {
        h = Fract(h) * 6.0f;
        float c = v * s;
        float x = c * (1.0f - std::fabs(fmodf(h, 2.0f) - 1.0f));
        float m = v - c;
        float r = 0, g = 0, b = 0;
        switch ((int)h) {
            case 0: r = c; g = x; break;
            case 1: r = x; g = c; break;
            case 2: g = c; b = x; break;
            case 3: g = x; b = c; break;
            case 4: r = x; b = c; break;
            default: r = c; b = x; break;
        }
        return { r + m, g + m, b + m, a };
    }
    Color Mix(const Color& o, float t) const {
        return { Lerp(r, o.r, t), Lerp(g, o.g, t), Lerp(b, o.b, t), Lerp(a, o.a, t) };
    }
    // Converts the RGB triple to HSV; alpha is ignored. Pairs with Hsv() for
    // round-tripping (Hsv(h, s, v) reproduces r,g,b for a=1 colors).
    void ToHsv(float& h, float& s, float& v) const {
        float mx = std::max(r, std::max(g, b));
        float mn = std::min(r, std::min(g, b));
        float d = mx - mn;
        v = mx;
        s = mx > 0.0f ? d / mx : 0.0f;
        if (d <= 0.0f) { h = 0.0f; return; }
        if (mx == r) h = Fract((g - b) / d / 6.0f);
        else if (mx == g) h = Fract((b - r) / d / 6.0f + 1.0f / 3.0f);
        else h = Fract((r - g) / d / 6.0f + 2.0f / 3.0f);
    }
};

} // namespace lp
