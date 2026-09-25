// LivePaper - "Fireflies".
//
// A dark woodland edge at dusk with dozens of warm fireflies drifting and pulsing.
// The trees are generated once as silhouettes; the fireflies wander with low-frequency
// noise so their paths read as organic, never as straight lines.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Firefly {
    float x, y;          // current position (pixels)
    float seedX, seedY;  // slow anchor for the wander field
    float phase;         // pulse phase
    float radius;        // glow radius
    float speed;
    float warm;          // 0 cool green .. 1 warm amber
};

struct Branch {
    float x0, y0, x1, y1, width;
};

} // namespace

class FirefliesScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Fireflies"; }
    const wchar_t* Description() const override {
        return L"Glowing fireflies drifting through a dark forest.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Count";
            case 1: return L"Drift";
            case 2: return L"Glow";
            case 3: return L"Warmth";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildBranches(ctx);
        BuildFireflies(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);
        m_time += dt;

        const float drift = 0.3f + m_drift * 1.6f;
        for (auto& f : m_flies) {
            // Wander via two octaves of noise: smooth, looping, non-repeating.
            float nx = Noise2(f.seedX + m_time * 0.05f * drift, f.seedY * 1.7f) * 2.0f;
            float ny = Noise2(f.seedY + m_time * 0.05f * drift, f.seedX * 1.3f + 40.0f) * 2.0f;
            f.x += nx * f.speed * drift * dt * 18.0f;
            f.y += (ny * 0.6f - 0.15f) * f.speed * drift * dt * 18.0f;
            f.phase += dt * (0.6f + f.speed);

            // Wrap softly.
            const float pad = 40.0f;
            if (f.x < -pad) f.x = ctx.width + pad;
            if (f.x > ctx.width + pad) f.x = -pad;
            if (f.y < -pad) f.y = ctx.height + pad;
            if (f.y > ctx.height + pad) f.y = -pad;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Forest dusk gradient.
        D2D1_GRADIENT_STOP sky[4];
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(0.010f, 0.022f, 0.026f, 1);
        sky[1].position = 0.45f; sky[1].color = D2D1::ColorF(0.018f, 0.040f, 0.040f, 1);
        sky[2].position = 0.72f; sky[2].color = D2D1::ColorF(0.030f, 0.066f, 0.052f, 1);
        sky[3].position = 1.00f; sky[3].color = D2D1::ColorF(0.012f, 0.028f, 0.022f, 1);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // A faint moon haze.
        draw::RadialGlow(dc, brush, w * 0.78f, h * 0.20f, h * 0.28f,
                         Color::Hsv(0.30f, 0.25f, 1.0f, 0.10f), 1.0f, 16);

        // Ground glow where the forest floor catches the last light.
        draw::RadialGlow(dc, brush, w * 0.5f, h * 0.90f, h * 0.5f,
                         Color::Hsv(0.24f, 0.35f, 0.9f, 0.10f), 1.0f, 20);

        // Silhouette branches (drawn as strokes so they read as foliage).
        for (const auto& b : m_branches) {
            Color c = Color(0.004f, 0.010f, 0.010f, 0.92f);
            draw::Line(dc, brush, b.x0, b.y0, b.x1, b.y1, b.width, c);
        }

        // Fireflies, back to front is unnecessary (all glow), so a single pass works.
        for (const auto& f : m_flies) {
            float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(f.phase));
            // Warmth blends amber and green.
            Color amber = Color::Hsv(0.12f, 0.75f, 1.0f);
            Color green = Color::Hsv(0.24f, 0.80f, 0.95f);
            Color body = green.Mix(amber, f.warm);
            float glow = (0.4f + 0.9f * m_glow) * pulse;

            draw::RadialGlow(dc, brush, f.x, f.y, f.radius, body.WithAlpha(0.55f * glow), 1.0f, 8);
            draw::Circle(dc, brush, f.x, f.y, f.radius * 0.20f, Color(1, 1, 0.9f, 0.85f * pulse));
        }

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.46f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_count = ctx.config->sceneParam[0];
        m_drift = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_warmth = ctx.config->sceneParam[3];
    }

    void BuildBranches(const SceneCtx& ctx) {
        m_branches.clear();
        Rng rng(6060u + ctx.variation * 7919u);
        // A few leaning trunks from the edges, plus drooping branches.
        int trunks = 6;
        for (int i = 0; i < trunks; ++i) {
            bool left = (i % 2) == 0;
            float x = left ? ctx.width * rng.Range(-0.02f, 0.10f)
                           : ctx.width * rng.Range(0.90f, 1.02f);
            float lean = left ? 0.5f : -0.5f;
            float top = ctx.height * rng.Range(0.25f, 0.55f);
            Branch b{ x, ctx.height + 20.0f, x + lean * ctx.width * 0.08f, top,
                      rng.Range(6.0f, 16.0f) };
            m_branches.push_back(b);
            // Sub-branches.
            for (int k = 0; k < 3; ++k) {
                float t = 0.35f + 0.20f * k;
                Branch s{ b.x0 + (b.x1 - b.x0) * t, b.y0 + (b.y1 - b.y0) * t,
                          b.x1 + lean * ctx.width * 0.12f * (1.0f - t),
                          b.y1 + ctx.height * 0.06f * k, b.width * 0.45f };
                m_branches.push_back(s);
            }
        }
        // Drooping canopy fronds across the top.
        for (int i = 0; i < 14; ++i) {
            float t = (float)i / 13.0f;
            float x = ctx.width * (0.02f + 0.96f * t);
            Branch b{ x, -10.0f, x + ctx.width * rng.Range(-0.02f, 0.02f),
                      ctx.height * rng.Range(0.05f, 0.16f), rng.Range(3.0f, 7.0f) };
            m_branches.push_back(b);
        }
    }

    void BuildFireflies(const SceneCtx& ctx) {
        m_flies.clear();
        int want = (int)(26 + 70 * m_count);
        want = std::min(want, 120);
        Rng rng(0xF1E5u + ctx.variation * 7919u);
        m_flies.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Firefly f{};
            f.x = rng.Range(0.0f, ctx.width);
            f.y = rng.Range(ctx.height * 0.05f, ctx.height * 0.92f);
            f.seedX = rng.Range(0.0f, 1000.0f);
            f.seedY = rng.Range(0.0f, 1000.0f);
            f.phase = rng.Range(0.0f, kTau);
            f.radius = rng.Range(6.0f, 16.0f);
            f.speed = rng.Range(0.5f, 1.6f);
            f.warm = Clamp01(rng.Unit() + m_warmth - 0.5f);
            m_flies.push_back(f);
        }
    }

    std::vector<Branch> m_branches;
    std::vector<Firefly> m_flies;
    float m_time = 0;
    float m_count = 0.5f, m_drift = 0.5f, m_glow = 0.5f, m_warmth = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateFireflies() { return new FirefliesScene(); }

} // namespace lp::scenes
