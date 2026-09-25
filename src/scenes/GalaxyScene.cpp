// LivePaper - "Spiral Galaxy".
//
// A rotating disc of a couple thousand stars laid out in logarithmic spiral arms,
// seen at an inclination. Stars orbit with differential speed (inner stars complete
// a lap faster), dark dust lanes ride the arms, and a warm core blooms at the
// centre. Positions are polar, so per-frame motion is a couple of sin/cos per star.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct GStar {
    float a0;        // angle at t=0
    float rad;       // orbital radius, fraction of min dimension
    float size;
    float bright;
    float warmth;    // 0 blue rim .. 1 warm core
    float phase;
};

struct Dust {
    float a0;
    float rad;
    float rx, ry;
    float alpha;
};

} // namespace

class GalaxyScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Spiral Galaxy"; }
    const wchar_t* Description() const override {
        return L"A rotating spiral galaxy of thousands of stars, tipped in space.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Arms";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Hue";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        Build(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);
        m_time += dt;
        m_rot += dt * (0.010f + 0.045f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float scale = std::min(w, h);
        const float squash = 0.58f;   // inclination: circle squashed to an ellipse

        // Deep space.
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.004f, 0.010f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.002f, 0.002f, 0.006f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        m_bgStars.Draw(dc, brush, m_time);

        // A couple of remote galaxies for depth.
        draw::Circle(dc, brush, w * 0.14f, h * 0.20f, 7.0f, Color(0.5f, 0.55f, 0.8f, 0.10f));
        draw::Circle(dc, brush, w * 0.86f, h * 0.76f, 5.0f, Color(0.8f, 0.6f, 0.7f, 0.08f));

        // Core bloom, before the stars so the inner disc sits in the light.
        Color coreC = Color::Hsv(Fract(0.09f + (m_hue - 0.5f) * 0.35f), 0.45f, 1.0f);
        draw::RadialGlow(dc, brush, cx, cy, scale * 0.22f, coreC.WithAlpha(0.22f + 0.18f * m_glow), 1.0f, 26);
        draw::RadialGlow(dc, brush, cx, cy, scale * 0.075f, Color(1.0f, 0.97f, 0.9f, 0.60f * (0.6f + 0.6f * m_glow)), 1.0f, 14);

        // The disc is drawn under one rotation transform; positions are computed
        // on the unit circle and squashed to the ellipse manually.
        dc->SetTransform(D2D1::Matrix3x2F::Rotation(24.0f, D2D1::Point2F(cx, cy)));

        // Dust lanes first (they darken the arms beneath the stars).
        for (const auto& d : m_dust) {
            float ang = d.a0 + DifferentialAngle(d.rad);
            float x = cx + std::cos(ang) * d.rad * scale;
            float y = cy + std::sin(ang) * d.rad * scale * squash;
            draw::Circle(dc, brush, x, y, d.rx * scale,
                         Color(0.004f, 0.003f, 0.008f, d.alpha));
        }

        // Stars.
        Color blue = Color::Hsv(Fract(0.58f + m_hue * 0.25f), 0.35f, 1.0f);
        Color warm = Color::Hsv(Fract(0.10f + m_hue * 0.25f), 0.40f, 1.0f);
        for (const auto& s : m_stars) {
            float ang = s.a0 + DifferentialAngle(s.rad);
            float x = cx + std::cos(ang) * s.rad * scale;
            float y = cy + std::sin(ang) * s.rad * scale * squash;
            Color c = blue.Mix(warm, s.warmth);
            float tw = 0.8f + 0.2f * std::sin(m_time * 1.7f + s.phase);
            draw::Circle(dc, brush, x, y, s.size, c.WithAlpha(s.bright * tw));
        }

        dc->SetTransform(D2D1::Matrix3x2F::Identity());

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.34f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_armsAmt = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    // Differential rotation: inner orbits sweep faster (a flat rotation curve
    // would be v=const, so omega ~ 1/r; we soften it to avoid a hard core spin).
    float DifferentialAngle(float rad) const {
        return m_rot * (1.6f / (rad * 2.2f + 0.45f));
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0xCA1A7u + ctx.variation * 7919u);
        m_arms = 2 + (int)(m_armsAmt * 2.9f);   // 2..4 full arms

        m_stars.clear();
        int count = std::min((int)(1500 * ctx.density), 2200);
        m_stars.reserve((size_t)count);
        const float twist = 4.4f;
        for (int i = 0; i < count; ++i) {
            GStar s{};
            int arm = i % m_arms;
            float t = std::pow(rng.Unit(), 0.62f);        // denser toward the core
            s.rad = 0.06f + t * 0.56f;
            float armAngle = (float)arm * kTau / (float)m_arms + t * twist;
            float scatter = rng.Range(-1.0f, 1.0f) * (0.16f + 0.30f * t);
            s.a0 = armAngle + scatter;
            s.size = rng.Range(0.8f, 2.1f) * (1.0f - t * 0.35f);
            s.bright = rng.Range(0.35f, 0.95f) * (1.0f - t * 0.25f);
            s.warmth = Clamp01(1.15f - t * 1.6f + rng.Range(-0.15f, 0.15f));
            s.phase = rng.Range(0.0f, kTau);
            m_stars.push_back(s);
        }

        m_dust.clear();
        int dustCount = std::min((int)(70 * ctx.density), 100);
        m_dust.reserve((size_t)dustCount);
        for (int i = 0; i < dustCount; ++i) {
            Dust d{};
            int arm = i % m_arms;
            float t = 0.25f + rng.Unit() * 0.6f;
            d.rad = 0.10f + t * 0.42f;
            d.a0 = (float)arm * kTau / (float)m_arms + t * twist + rng.Range(-0.08f, 0.08f);
            d.rx = rng.Range(0.020f, 0.045f);
            d.ry = d.rx;
            d.alpha = rng.Range(0.08f, 0.16f);
            m_dust.push_back(d);
        }

        m_bgStars.Build(ctx.width, ctx.height, (int)(130 * ctx.density), 0xCA1B2u + ctx.variation);
        m_rot = Fract((float)(ctx.variation % 97) * 0.061f);   // varied but deterministic start angle
    }

    std::vector<GStar> m_stars;
    std::vector<Dust> m_dust;
    draw::StarField m_bgStars;
    int m_arms = 2;
    float m_rot = 0;
    float m_time = 0;
    float m_armsAmt = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateGalaxy() { return new GalaxyScene(); }

} // namespace lp::scenes
