// LivePaper - "Savanna Dusk".
//
// The classic African dusk: a huge banded sun sinking into a violet-orange sky,
// flat-topped acacia silhouettes, swaying grass blades catching the last light,
// and a few birds flapping home. Blades are two-line polylines, cheap to sway.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Blade {
    float x, y;      // base
    float h;         // blade height
    float lean;      // resting lean (px)
    float phase;
    float width;
    bool lit;        // catches the sun
};

struct Tree {
    float x;
    float baseY;
    float scale;
};

struct Bird {
    float x, y;
    float speed;
    float dir;
    float phase;
    float size;
};

} // namespace

class SavannaScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Savanna Dusk"; }
    const wchar_t* Description() const override {
        return L"Banded sun sinking behind acacias and wind-combed grass.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Sun";
            case 1: return L"Wind";
            case 2: return L"Trees";
            case 3: return L"Dusk";
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
        const float w = ctx.width;
        for (auto& b : m_birds) {
            b.x += b.dir * b.speed * (0.6f + 0.8f * m_wind) * dt;
            if (b.dir > 0 && b.x > w + 40.0f) b.x = -40.0f;
            if (b.dir < 0 && b.x < -40.0f) b.x = w + 40.0f;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float groundY = h * 0.74f;

        // Dusk sky: violet overhead into burning orange at the horizon.
        D2D1_GRADIENT_STOP sky[3];
        Color top = Color::Hsv(Lerp(0.78f, 0.70f, m_dusk), 0.42f, 0.24f);
        Color mid = Color::Hsv(0.94f, 0.48f, 0.48f);
        Color low = Color::Hsv(0.045f, 0.85f, 0.85f);
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(top.r, top.g, top.b, 1);
        sky[1].position = 0.55f; sky[1].color = D2D1::ColorF(mid.r, mid.g, mid.b, 1);
        sky[2].position = 1.0f; sky[2].color = D2D1::ColorF(low.r, low.g, low.b, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);

        // Sun: a huge disc half-sunk at the horizon, with haze bands across it.
        float sx = w * 0.60f;
        float sy = groundY - h * 0.02f;
        float sr = h * (0.16f + 0.10f * m_sun);
        draw::RadialGlow(dc, brush, sx, sy, sr * 2.6f,
                         Color::Hsv(0.03f, 0.85f, 1.0f, 0.22f + 0.12f * m_sun), 1.0f, 24);
        draw::Circle(dc, brush, sx, sy, sr, Color::Hsv(0.02f, 0.80f, 1.0f, 0.96f));
        draw::Circle(dc, brush, sx, sy, sr * 0.92f, Color::Hsv(0.035f, 0.88f, 1.0f, 0.98f));
        // The iconic horizontal cloud bands crossing the disc. Each band is a
        // chord of the circle: its width follows the circle's silhouette, so no
        // band ever sticks out past the sun.
        Color band = Color::Hsv(0.93f, 0.50f, 0.45f, 0.85f);
        for (int i = 0; i < 4; ++i) {
            float dy = -sr * 0.15f + sr * 0.34f * i;
            float by = sy + dy;
            float halfW = sr * std::sqrt(std::max(0.0f, 1.0f - (dy / sr) * (dy / sr)));
            if (halfW < sr * 0.05f) continue;
            float thick = h * (0.006f + 0.004f * i);
            draw::RoundedRect(dc, brush, sx - halfW, by, halfW * 2.0f, thick, thick * 0.5f, band);
        }

        // Birds, before the ground so they stay in the sky.
        for (const auto& b : m_birds) {
            float flap = std::sin(m_time * 7.0f + b.phase) * 0.5f;
            float y = b.y + std::sin(m_time * 0.7f + b.phase) * h * 0.008f;
            Color bc(0.02f, 0.012f, 0.02f, 0.9f);
            draw::Line(dc, brush, b.x - b.size, y + flap * b.size * 0.7f, b.x, y, 1.6f, bc);
            draw::Line(dc, brush, b.x, y, b.x + b.size, y + flap * b.size * 0.7f, 1.6f, bc);
        }

        // Plain: flat dark ground with a slightly broken edge.
        {
            m_ground.clear();
            for (int i = 0; i <= 24; ++i) {
                float u = (float)i / 24.0f;
                m_ground.push_back(D2D1::Point2F(u * w, groundY + Noise1(u * 5.0f + 31.0f) * h * 0.004f));
            }
            m_ground.push_back(D2D1::Point2F(w, h));
            m_ground.push_back(D2D1::Point2F(0, h));
            draw::FillPolygon(dc, brush, m_ground.data(), (unsigned)m_ground.size(),
                              Color(0.014f, 0.007f, 0.010f, 1.0f));
        }

        // Acacias, far first.
        for (const auto& t : m_trees) {
            DrawAcacia(dc, brush, t);
        }

        // Grass: backlit blades in rows, swaying with the wind.
        for (const auto& b : m_blades) {
            float sway = std::sin(m_time * (0.9f + Fract(b.phase) * 0.8f) + b.x * 0.02f)
                         * m_wind * b.h * 0.35f;
            float tipX = b.x + b.lean + sway;
            float midX = b.x + b.lean * 0.4f + sway * 0.3f;
            Color c = b.lit
                ? Color::Hsv(0.06f, 0.65f, 0.85f, 0.5f)
                : Color(0.012f, 0.006f, 0.008f, 0.95f);
            draw::Line(dc, brush, b.x, b.y, midX, b.y - b.h * 0.55f, b.width, c);
            draw::Line(dc, brush, midX, b.y - b.h * 0.55f, tipX, b.y - b.h, b.width * 0.8f, c);
        }

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.01f, 0.002f, 0.004f, 0.36f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_sun = ctx.config->sceneParam[0];
        m_wind = ctx.config->sceneParam[1];
        m_treesAmt = ctx.config->sceneParam[2];
        m_dusk = ctx.config->sceneParam[3];
    }

    // Acacia: leaning tapered trunk with forking limbs, flat umbrella canopy.
    void DrawAcacia(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, const Tree& t) {
        Color bark(0.008f, 0.004f, 0.005f, 1.0f);
        float trunkH = 120.0f * t.scale;
        float baseX = t.x, baseY = t.baseY;
        float leanDir = (t.scale > 0.5f) ? 1.0f : -1.0f;

        // Trunk (tapered quad).
        float topX = baseX + leanDir * trunkH * 0.18f;
        float topY = baseY - trunkH;
        D2D1_POINT_2F trunk[4] = {
            D2D1::Point2F(baseX - 8.0f * t.scale, baseY),
            D2D1::Point2F(baseX + 8.0f * t.scale, baseY),
            D2D1::Point2F(topX + 2.6f * t.scale, topY),
            D2D1::Point2F(topX - 2.6f * t.scale, topY),
        };
        draw::FillPolygon(dc, brush, trunk, 4, bark);
        // Forking limbs.
        for (int k = -1; k <= 1; k += 2) {
            draw::Line(dc, brush, topX, topY + trunkH * 0.05f,
                       topX + k * trunkH * 0.34f, topY - trunkH * 0.10f,
                       2.2f * t.scale, bark);
        }
        // Umbrella canopy: flat overlapping discs along one span, then a flat cut.
        float cy = topY - trunkH * 0.14f;
        float span = trunkH * 0.85f;
        Color leaf(0.006f, 0.008f, 0.004f, 1.0f);
        for (int i = 0; i <= 7; ++i) {
            float u = (float)i / 7.0f;
            float cx2 = topX - span * 0.5f + span * u;
            draw::Circle(dc, brush, cx2, cy + std::sin(u * kPi) * -trunkH * 0.03f,
                         trunkH * (0.15f + 0.05f * std::sin(u * kPi)), leaf);
        }
        // Flatten the canopy's bottom edge with a ground-coloured band.
        draw::RoundedRect(dc, brush, topX - span * 0.75f, cy + trunkH * 0.06f,
                          span * 1.5f, trunkH * 0.10f, trunkH * 0.02f,
                          Color(0.014f, 0.007f, 0.010f, 0.96f));
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        float groundY = h * 0.74f;
        Rng rng(0x5A7611u + ctx.variation * 7919u);

        m_trees.clear();
        int want = std::min((int)(2 + 3 * m_treesAmt * ctx.density), 5);
        m_trees.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Tree t{};
            t.x = w * rng.Range(0.05f, 0.95f);
            t.baseY = groundY + h * rng.Range(0.02f, 0.10f);
            t.scale = h / 1080.0f * rng.Range(0.55f, 1.15f) * (i == 0 ? 1.25f : 1.0f);
            m_trees.push_back(t);
        }
        // Keep trees from stacking on the sun.
        // (First tree is the hero tree; push it left of centre.)
        if (!m_trees.empty()) m_trees[0].x = w * rng.Range(0.10f, 0.34f);

        m_blades.clear();
        int blades = (int)((100 + 100 * m_wind) * ctx.density);
        blades = std::min(blades, 190);
        m_blades.reserve((size_t)blades);
        for (int i = 0; i < blades; ++i) {
            Blade b{};
            b.x = rng.Range(-10.0f, w + 10.0f);
            b.y = groundY + h * rng.Range(0.015f, 0.26f);
            float depth = (b.y - groundY) / (h - groundY);
            b.h = h * (0.015f + 0.055f * depth) * rng.Range(0.7f, 1.3f);
            b.lean = rng.Range(-1.0f, 1.0f) * b.h * 0.35f;
            b.phase = rng.Range(0.0f, 10.0f);
            b.width = 1.0f + 1.6f * depth;
            b.lit = rng.Unit() < 0.35f && depth < 0.7f;
            m_blades.push_back(b);
        }

        m_birds.clear();
        int birds = 2 + rng.Int(0, 2);
        m_birds.reserve((size_t)birds);
        for (int i = 0; i < birds; ++i) {
            Bird b{};
            b.x = rng.Range(0.0f, w);
            b.y = h * rng.Range(0.16f, 0.42f);
            b.speed = rng.Range(24.0f, 60.0f);
            b.dir = (rng.Unit() < 0.5f) ? 1.0f : -1.0f;
            b.phase = rng.Range(0.0f, kTau);
            b.size = rng.Range(4.0f, 8.0f);
            m_birds.push_back(b);
        }
    }

    std::vector<Blade> m_blades;
    std::vector<Tree> m_trees;
    std::vector<Bird> m_birds;
    std::vector<D2D1_POINT_2F> m_ground;
    float m_time = 0;
    float m_sun = 0.5f, m_wind = 0.5f, m_treesAmt = 0.5f, m_dusk = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateSavanna() { return new SavannaScene(); }

} // namespace lp::scenes
