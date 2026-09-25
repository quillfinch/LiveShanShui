// LivePaper - "Nebula".
//
// A deep-space scene: several soft, slowly drifting nebulae built from layered radial
// glows in complementary hues, a dense twinkling star field, and a few bright stars
// with cross sparkles. Pure additive-look gradients - no textures.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct NebulaCloud {
    float cx, cy;       // centre (fraction of screen)
    float radius;       // base radius (fraction of min dimension)
    float hue;          // primary hue
    float hue2;         // secondary hue for the inner core
    float drift;        // slow orbital speed
    float phase;
};

struct BrightStar { float x, y, r, phase, speed, warm; };

} // namespace

class NebulaScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Nebula"; }
    const wchar_t* Description() const override {
        return L"Soft drifting nebula clouds over a dense star field.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Density";
            case 1: return L"Motion";
            case 2: return L"Brightness";
            case 3: return L"Hue";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildClouds(ctx);
        BuildStars(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);
        m_time += dt * (0.2f + m_motion * 0.9f);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float minDim = std::min(w, h);

        // Deep space base.
        D2D1_GRADIENT_STOP sky[3];
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(0.006f, 0.006f, 0.022f, 1);
        sky[1].position = 0.55f; sky[1].color = D2D1::ColorF(0.020f, 0.014f, 0.050f, 1);
        sky[2].position = 1.00f; sky[2].color = D2D1::ColorF(0.010f, 0.008f, 0.030f, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);

        // Nebulae (drawn first so stars sit on top).
        for (const auto& c : m_clouds) {
            float ox = std::sin(m_time * c.drift + c.phase) * w * 0.035f;
            float oy = std::cos(m_time * c.drift * 0.8f + c.phase) * h * 0.030f;
            float cx = c.cx * w + ox;
            float cy = c.cy * h + oy;
            float R = c.radius * minDim;
            float brightness = 0.5f + 0.65f * m_brightness;

            // Outer haze: several wide, very faint glows.
            draw::RadialGlow(dc, brush, cx, cy, R * 1.30f,
                             Color::Hsv(c.hue, 0.55f, 1.0f, 0.10f * brightness), 1.0f, 22);
            draw::RadialGlow(dc, brush, cx + R * 0.22f, cy - R * 0.16f, R * 0.95f,
                             Color::Hsv(c.hue2, 0.60f, 1.0f, 0.09f * brightness), 1.0f, 20);
            // Mid layers, mixing the two hues for a cloudy look.
            for (int i = 0; i < 7; ++i) {
                float t = (float)i / 6.0f;
                float ang = t * kTau * 2.0f + c.phase;
                float rr = R * (0.25f + 0.55f * t);
                float nx = cx + std::cos(ang) * R * 0.30f * t;
                float ny = cy + std::sin(ang) * R * 0.24f * t;
                Color col = Color::Hsv(c.hue + (c.hue2 - c.hue) * t, 0.60f, 1.0f,
                                       0.10f * brightness * (1.0f - t * 0.5f));
                draw::RadialGlow(dc, brush, nx, ny, rr, col, 1.0f, 14);
            }
            // Bright core.
            draw::RadialGlow(dc, brush, cx, cy, R * 0.30f,
                             Color::Hsv(c.hue2, 0.35f, 1.0f, 0.22f * brightness), 1.0f, 14);
            draw::Circle(dc, brush, cx, cy, R * 0.012f,
                         Color(1, 1, 1, 0.28f * brightness));
        }

        // Star field.
        m_stars.Draw(dc, brush, m_time);

        // Bright stars with a cross sparkle.
        for (const auto& s : m_bright) {
            float tw = 0.55f + 0.45f * std::sin(m_time * s.speed + s.phase);
            float a = (0.45f + 0.55f * tw) * (0.5f + 0.6f * m_brightness);
            Color warm = s.warm ? Color(1.0f, 0.86f, 0.72f, a) : Color(0.82f, 0.90f, 1.0f, a);
            draw::RadialGlow(dc, brush, s.x, s.y, s.r * 7.0f, warm, 1.0f, 6);
            draw::Circle(dc, brush, s.x, s.y, s.r, warm);
            // Cross rays.
            float ray = s.r * (4.0f + tw * 3.0f);
            draw::Line(dc, brush, s.x - ray, s.y, s.x + ray, s.y, 1.0f,
                       Color(warm.r, warm.g, warm.b, warm.a * 0.45f));
            draw::Line(dc, brush, s.x, s.y - ray, s.x, s.y + ray, 1.0f,
                       Color(warm.r, warm.g, warm.b, warm.a * 0.45f));
        }

        // Vignette for icon legibility.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.34f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_density = ctx.config->sceneParam[0];
        m_motion = ctx.config->sceneParam[1];
        m_brightness = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void BuildClouds(const SceneCtx& ctx) {
        m_clouds.clear();
        Rng rng(4224242u + ctx.variation * 7919u);
        int count = (int)(3 + 5 * m_density);
        count = std::min(count, 7);
        for (int i = 0; i < count; ++i) {
            NebulaCloud c{};
            c.cx = rng.Range(0.18f, 0.82f);
            c.cy = rng.Range(0.22f, 0.62f);
            c.radius = rng.Range(0.16f, 0.34f);
            c.hue = Fract(m_hue + rng.Range(-0.06f, 0.06f) + (i % 3) * 0.27f);
            c.hue2 = Fract(c.hue + rng.Range(0.05f, 0.14f));
            c.drift = rng.Range(0.05f, 0.14f);
            c.phase = rng.Range(0.0f, kTau);
            m_clouds.push_back(c);
        }
    }

    void BuildStars(const SceneCtx& ctx) {
        int count = (int)(220 + 420 * (ctx.width * ctx.height) / (1920.0f * 1200.0f));
        count = (int)(count * (0.7f + 0.5f * m_density));
        m_stars.Build(ctx.width, ctx.height, count, 0x5EED5Eu);

        m_bright.clear();
        Rng rng(0xAB0DEu + ctx.variation * 7919u);
        for (int i = 0; i < 18; ++i) {
            BrightStar s{};
            s.x = rng.Range(0.05f, 0.95f) * ctx.width;
            s.y = rng.Range(0.05f, 0.90f) * ctx.height;
            s.r = rng.Range(0.8f, 1.9f);
            s.phase = rng.Range(0.0f, kTau);
            s.speed = rng.Range(0.4f, 1.6f);
            s.warm = rng.Unit() > 0.5f;
            m_bright.push_back(s);
        }
    }

    std::vector<NebulaCloud> m_clouds;
    std::vector<BrightStar> m_bright;
    draw::StarField m_stars;
    float m_time = 0;
    float m_density = 0.5f, m_motion = 0.5f, m_brightness = 0.5f, m_hue = 0.82f;
    float m_lastW = 0, m_lastH = 0;
};

Scene* CreateNebula() { return new NebulaScene(); }

} // namespace lp::scenes
