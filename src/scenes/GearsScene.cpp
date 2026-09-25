// LivePaper - "Clockwork".
//
// Brass gears turning against a dark machine-room backdrop. Each gear's tooth
// ring is a cached polygon in local coordinates, drawn under a rotation transform;
// angular speeds scale inversely with radius so the train reads as meshed, and
// neighbours turn in opposite senses. A slow pool of light wanders across the plate.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Gear {
    float cx, cy;
    float r;
    int teeth;
    float speed;      // rad/s, signed
    float phase;
    std::vector<D2D1_POINT_2F> ring;   // tooth outline, local coords
};

struct Mote {
    float x, y;
    float r;
    float vy;
    float phase;
};

} // namespace

class GearsScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Clockwork"; }
    const wchar_t* Description() const override {
        return L"Brass gears meshing in a dark machine room.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Speed";
            case 1: return L"Density";
            case 2: return L"Glow";
            case 3: return L"Tone";
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
        for (auto& g : m_gears) {
            g.phase += g.speed * (0.25f + 1.3f * m_speed) * dt;
        }
        const float h = ctx.height;
        for (auto& m : m_motes) {
            m.y -= m.vy * dt;
            if (m.y < -8.0f) m.y = h + 8.0f;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float hue = 0.075f + (m_hue - 0.5f) * 0.05f;

        // Machine plate: dark bronze with a faint vertical sheen.
        D2D1_GRADIENT_STOP bg[3];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.030f, 0.020f, 0.011f, 1);
        bg[1].position = 0.5f; bg[1].color = D2D1::ColorF(0.045f, 0.030f, 0.016f, 1);
        bg[2].position = 1.0f; bg[2].color = D2D1::ColorF(0.024f, 0.015f, 0.008f, 1);
        draw::VerticalGradient(dc, w, h, bg, 3);

        // A slow wandering pool of light keeps the plate alive.
        float lx = w * (0.5f + 0.30f * std::sin(m_time * 0.10f));
        float ly = h * (0.45f + 0.25f * std::sin(m_time * 0.073f + 1.3f));
        draw::RadialGlow(dc, brush, lx, ly, std::max(w, h) * 0.55f,
                         Color::Hsv(hue, 0.55f, 1.0f, 0.035f + 0.035f * m_glow), 1.0f, 22);

        // Back plate screws, barely there.
        for (int i = 0; i < 5; ++i) {
            float sx = w * (0.08f + 0.21f * i);
            float sy = h * (i % 2 == 0 ? 0.10f : 0.90f);
            draw::Circle(dc, brush, sx, sy, 3.0f, Color(0, 0, 0, 0.35f));
            draw::Line(dc, brush, sx - 1.6f, sy, sx + 1.6f, sy, 1.0f, Color(1, 1, 1, 0.05f));
        }

        // Gears: back (small, dim) first, then the main train.
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& g : m_gears) {
                bool big = g.r > m_minDim * 0.09f;
                if ((pass == 1) != big) continue;
                float dim = big ? 1.0f : 0.6f;

                dc->SetTransform(D2D1::Matrix3x2F::Rotation(g.phase * 57.2958f,
                                                            D2D1::Point2F(g.cx, g.cy)));
                // Body.
                Color body = Color::Hsv(hue, 0.48f, 0.30f * dim);
                draw::FillPolygon(dc, brush, g.ring.data(), (unsigned)g.ring.size(), body);
                // Rim light on the trailing edge: a thin brighter ring inset.
                Color rim = Color::Hsv(hue, 0.38f, (0.46f + 0.14f * m_glow) * dim);
                brush->SetColor(D2D1::ColorF(rim.r, rim.g, rim.b, 0.8f));
                dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(g.cx, g.cy),
                                              g.r * 0.86f, g.r * 0.86f), brush, g.r * 0.03f, nullptr);
                // Spokes.
                Color dark = Color::Hsv(hue, 0.55f, 0.10f * dim);
                for (int s = 0; s < 4; ++s) {
                    float a = kTau * (float)s / 4.0f + 0.4f;
                    draw::Line(dc, brush, g.cx, g.cy,
                               g.cx + std::cos(a) * g.r * 0.78f,
                               g.cy + std::sin(a) * g.r * 0.78f, g.r * 0.075f, dark);
                }
                // Hub cap.
                draw::Circle(dc, brush, g.cx, g.cy, g.r * 0.16f, dark);
                draw::Circle(dc, brush, g.cx, g.cy, g.r * 0.10f,
                             Color::Hsv(hue, 0.35f, 0.42f * dim, 0.95f));
                dc->SetTransform(D2D1::Matrix3x2F::Identity());
            }
        }

        // Oil-black outline on the largest gear's teeth was skipped for cost;
        // instead a few floating dust motes catch the light.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& m : m_motes) {
            draw::Circle(dc, brush, m.x + std::sin(m.phase) * 8.0f, m.y, m.r,
                         Color::Hsv(hue, 0.3f, 1.0f, 0.05f));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.42f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_speed = ctx.config->sceneParam[0];
        m_density = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        m_minDim = std::min(w, h);
        Rng rng(0x6EA121u + ctx.variation * 7919u);

        m_gears.clear();
        int want = (int)((5 + 5 * m_density) * ctx.density);
        want = std::min(want, 12);
        m_gears.reserve((size_t)want);
        int guard = 0;
        while ((int)m_gears.size() < want && guard++ < 200) {
            Gear g{};
            bool big = (m_gears.size() % 3 == 0);
            g.r = m_minDim * (big ? rng.Range(0.10f, 0.17f) : rng.Range(0.045f, 0.085f));
            g.cx = rng.Range(g.r * 1.2f, w - g.r * 1.2f);
            g.cy = rng.Range(g.r * 1.2f, h - g.r * 1.2f);
            // Keep gears from crowding each other.
            bool ok = true;
            for (const auto& o : m_gears) {
                float dx = o.cx - g.cx, dy = o.cy - g.cy;
                if (std::sqrt(dx * dx + dy * dy) < (o.r + g.r) * 1.06f) { ok = false; break; }
            }
            if (!ok) continue;
            g.teeth = std::min(20, std::max(9, (int)(g.r * 0.12f)));
            // Meshing illusion: small gears spin faster; alternate direction.
            g.speed = (big ? 0.35f : 0.9f) * rng.Range(0.8f, 1.2f) *
                      ((m_gears.size() % 2 == 0) ? 1.0f : -1.0f);
            g.phase = rng.Range(0.0f, kTau);

            // Tooth ring around the gear's own centre, so the single rotation
            // transform (about cx,cy) spins it in place.
            float ri = g.r * 0.86f, ro = g.r;
            g.ring.reserve((size_t)(g.teeth * 4));
            float step = kTau / g.teeth;
            for (int t = 0; t < g.teeth; ++t) {
                float a0 = step * t;
                g.ring.push_back(D2D1::Point2F(g.cx + std::cos(a0) * ri, g.cy + std::sin(a0) * ri));
                g.ring.push_back(D2D1::Point2F(g.cx + std::cos(a0 + step * 0.10f) * ro, g.cy + std::sin(a0 + step * 0.10f) * ro));
                g.ring.push_back(D2D1::Point2F(g.cx + std::cos(a0 + step * 0.40f) * ro, g.cy + std::sin(a0 + step * 0.40f) * ro));
                g.ring.push_back(D2D1::Point2F(g.cx + std::cos(a0 + step * 0.50f) * ri, g.cy + std::sin(a0 + step * 0.50f) * ri));
            }
            m_gears.push_back(g);
        }

        m_motes.clear();
        int motes = (int)(26 * ctx.density);
        m_motes.reserve((size_t)motes);
        for (int i = 0; i < motes; ++i) {
            Mote m{};
            m.x = rng.Range(0.0f, w);
            m.y = rng.Range(0.0f, h);
            m.r = rng.Range(0.8f, 2.0f);
            m.vy = rng.Range(3.0f, 10.0f);
            m.phase = rng.Range(0.0f, kTau);
            m_motes.push_back(m);
        }
    }

    std::vector<Gear> m_gears;
    std::vector<Mote> m_motes;
    float m_minDim = 1080.0f;
    float m_time = 0;
    float m_speed = 0.5f, m_density = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateGears() { return new GearsScene(); }

} // namespace lp::scenes
