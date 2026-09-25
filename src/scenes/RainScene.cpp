// LivePaper - "Rain on Glass".
//
// The view through a wet window at night: a softly defocused city glows behind the
// glass (large low-alpha bokeh orbs), while droplets bead up, stall, then run down
// leaving short-lived trails. The bokeh set is fixed per variation and breathes
// slowly; only the drops move quickly.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Bokeh {
    float x, y, r;
    float hue, sat;
    float breathe;      // slow alpha oscillation phase
    float breatheRate;
    float alpha;
};

struct Drop {
    float x, y;
    float r;
    float vy;           // 0 while beading; positive while running
    float runTimer;     // seconds until a beaded drop starts running
    float trail;        // 0..1 trail fade
    float seed;
};

} // namespace

class RainScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Rain on Glass"; }
    const wchar_t* Description() const override {
        return L"Droplets running down a window over blurred night-city bokeh.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Drops";
            case 1: return L"Rain";
            case 2: return L"Bokeh";
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

        const float w = ctx.width, h = ctx.height;
        Rng rng((uint32_t)(m_time * 719.0f) + 3u + ctx.variation);

        for (auto& d : m_drops) {
            if (d.vy > 0.0f) {
                // Running: accelerate a little, wobble around obstacles, fade the trail.
                d.vy = std::min(d.vy + 60.0f * dt, 190.0f);
                d.y += d.vy * (0.5f + m_rain) * dt;
                d.x += Noise1(d.seed + d.y * 0.01f) * 14.0f * dt;
                d.trail = std::min(1.0f, d.trail + dt * 4.0f);
                d.r *= 1.0f - 0.10f * dt;   // running drops slowly spend themselves
                if (d.y > h + 20.0f || d.r < 2.2f) Respawn(d, rng, w, h);
            } else {
                // Beading: sit, swell faintly, decide when to run.
                d.runTimer -= dt * (0.4f + 1.2f * m_rain);
                d.r += dt * 0.35f;
                if (d.runTimer <= 0.0f) {
                    d.vy = 26.0f;
                    d.trail = 0.0f;
                }
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Night city behind wet glass: near-black with a warm street haze low down.
        float baseHue = Fract(m_hue);
        D2D1_GRADIENT_STOP bg[3];
        Color nightTop = Color::Hsv(baseHue + 0.58f, 0.45f, 0.030f);
        Color nightMid = Color::Hsv(baseHue + 0.58f, 0.42f, 0.045f);
        Color haze     = Color::Hsv(baseHue, 0.62f, 0.10f);
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(nightTop.r, nightTop.g, nightTop.b, 1);
        bg[1].position = 0.62f; bg[1].color = D2D1::ColorF(nightMid.r, nightMid.g, nightMid.b, 1);
        bg[2].position = 1.0f; bg[2].color = D2D1::ColorF(haze.r, haze.g, haze.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 3);

        // --- defocused city (bokeh) --------------------------------------------
        for (const auto& b : m_bokeh) {
            float breathe = 0.75f + 0.25f * std::sin(m_time * b.breatheRate + b.breathe);
            float a = b.alpha * breathe * (0.45f + 0.75f * m_bokehAmt);
            Color c = Color::Hsv(b.hue, b.sat, 1.0f, a);
            // Two concentric passes give a soft edge cheaply (no blur effect).
            draw::Circle(dc, brush, b.x, b.y, b.r, c.WithAlpha(a * 0.35f));
            draw::Circle(dc, brush, b.x, b.y, b.r * 0.66f, c);
            // A brighter core: the light source itself.
            draw::Circle(dc, brush, b.x, b.y, b.r * 0.22f,
                         Color::Hsv(b.hue, b.sat * 0.5f, 1.0f, a * 0.9f));
        }

        // --- glass tint over the city (condensation) ----------------------------
        D2D1_GRADIENT_STOP fog[2];
        fog[0].position = 0.0f; fog[0].color = D2D1::ColorF(0.55f, 0.62f, 0.72f, 0.05f);
        fog[1].position = 1.0f; fog[1].color = D2D1::ColorF(0.55f, 0.62f, 0.72f, 0.012f);
        draw::VerticalGradient(dc, w, h, fog, 2);

        // --- droplets ------------------------------------------------------------
        for (const auto& d : m_drops) {
            if (d.vy > 0.0f && d.trail > 0.05f) {
                // A short vertical streak above the drop: what it wiped clean.
                float tl = d.r * (7.0f + 5.0f * d.trail);
                draw::Line(dc, brush, d.x, d.y - tl, d.x, d.y, d.r * 0.9f,
                           Color(0.62f, 0.70f, 0.80f, 0.10f * d.trail));
                draw::Line(dc, brush, d.x, d.y - tl, d.x, d.y, d.r * 0.35f,
                           Color(0.75f, 0.82f, 0.90f, 0.20f * d.trail));
            }
            // The bead: dark rim, clear body, a highlight.
            draw::Circle(dc, brush, d.x, d.y, d.r * 1.12f, Color(0, 0, 0, 0.16f));
            draw::Circle(dc, brush, d.x, d.y, d.r, Color(0.66f, 0.74f, 0.84f, 0.13f));
            draw::Circle(dc, brush, d.x - d.r * 0.30f, d.y - d.r * 0.35f, d.r * 0.26f,
                         Color(0.9f, 0.95f, 1.0f, 0.30f));
        }

        // Vignette (the window frame's shadow).
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.46f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_dropsAmt = ctx.config->sceneParam[0];
        m_rain = ctx.config->sceneParam[1];
        m_bokehAmt = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Respawn(Drop& d, Rng& rng, float w, float h) {
        d.x = rng.Range(0.0f, w);
        d.y = rng.Range(-0.05f, 0.75f) * h;
        d.r = rng.Range(2.4f, 7.0f);
        d.vy = 0.0f;
        d.runTimer = rng.Range(0.5f, 6.0f);
        d.trail = 0.0f;
        d.seed = rng.Range(0.0f, 500.0f);
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x0B0CAu + ctx.variation * 32452843u);

        m_bokeh.clear();
        int bokehCount = (int)((22 + 26 * m_bokehAmt) * ctx.density);
        bokehCount = std::min(bokehCount, 64);
        m_bokeh.reserve((size_t)bokehCount);
        for (int i = 0; i < bokehCount; ++i) {
            Bokeh b{};
            b.x = rng.Range(-0.02f, 1.02f) * w;
            b.y = rng.Range(0.28f, 1.05f) * h;
            b.r = rng.Range(h * 0.02f, h * 0.075f);
            // Warm street lights with a few cool signs.
            b.hue = Fract(baseHueFor(i) + rng.Range(-0.03f, 0.03f));
            b.sat = rng.Range(0.45f, 0.85f);
            b.breathe = rng.Range(0.0f, kTau);
            b.breatheRate = rng.Range(0.10f, 0.45f);
            b.alpha = rng.Range(0.05f, 0.14f);
            m_bokeh.push_back(b);
        }

        m_drops.clear();
        int dropCount = (int)((32 + 70 * m_dropsAmt) * ctx.density);
        dropCount = std::min(dropCount, 130);
        m_drops.resize((size_t)dropCount);
        for (auto& d : m_drops) Respawn(d, rng, w, h);
    }

    // Palette anchor: mostly warm sodium/amber, every 4th orb cool cyan-blue.
    float baseHueFor(int i) const {
        float warm = m_hue < 0.5f ? 0.075f : 0.10f;
        float cool = Fract(warm + 0.5f + m_hue * 0.2f);
        return (i % 4 == 3) ? cool : warm;
    }

    std::vector<Bokeh> m_bokeh;
    std::vector<Drop> m_drops;
    float m_time = 0;
    float m_dropsAmt = 0.5f, m_rain = 0.5f, m_bokehAmt = 0.5f, m_hue = 0.0f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateRain() { return new RainScene(); }

} // namespace lp::scenes
