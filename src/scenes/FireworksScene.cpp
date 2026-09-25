// LivePaper - "Fireworks".
//
// Rockets rise from behind a hill silhouette, burst into shells of particles at
// apex, and the debris falls under gravity with drag. Three shell types keep the
// sky varied: peonies (round), rings, and long-lived willows that sag.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Rocket {
    float x, y, vx, vy;
    float fuse;
    float hue;
    int type;        // 0 peony, 1 ring, 2 willow
};

struct Part {
    float x, y, vx, vy;
    float life, maxLife;
    float r;
    float hue;
    float drag;
    float grav;
};

} // namespace

class FireworksScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Fireworks"; }
    const wchar_t* Description() const override {
        return L"Rockets bursting over a dark hill in shimmering shells.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Frequency";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Palette";
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
        Rng rng((uint32_t)(m_time * 331.0f) + 11u + ctx.variation);

        // --- launch ---
        m_next -= dt * (0.45f + 1.9f * m_freq);
        if (m_next <= 0.0f && (int)m_rockets.size() < 4) {
            m_next = 0.7f + rng.Unit() * 1.6f;
            Rocket r{};
            r.x = w * rng.Range(0.18f, 0.82f);
            r.y = h + 12.0f;
            r.vy = -h * (0.42f + 0.30f * m_speed) * rng.Range(0.85f, 1.15f);
            r.vx = rng.Range(-w * 0.02f, w * 0.02f);
            r.fuse = rng.Range(0.75f, 1.15f);
            r.hue = ShellHue(rng);
            r.type = rng.Int(0, 2);
            m_rockets.push_back(r);
        }

        // --- rockets ---
        for (size_t i = 0; i < m_rockets.size();) {
            Rocket& r = m_rockets[i];
            r.vy += h * 0.10f * dt;          // decelerating climb
            r.x += r.vx * dt;
            r.y += r.vy * dt;
            r.fuse -= dt;
            if (r.fuse <= 0.0f) {
                Explode(r, rng);
                m_rockets[i] = m_rockets.back();
                m_rockets.pop_back();
            } else {
                ++i;
            }
        }

        // --- debris ---
        for (size_t i = 0; i < m_parts.size();) {
            Part& p = m_parts[i];
            float d = std::exp(-p.drag * dt);
            p.vx *= d;
            p.vy = p.vy * d + p.grav * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.life -= dt;
            if (p.life <= 0.0f) {
                m_parts[i] = m_parts.back();
                m_parts.pop_back();
            } else {
                ++i;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        D2D1_GRADIENT_STOP sky[3];
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(0.008f, 0.008f, 0.020f, 1);
        sky[1].position = 0.6f; sky[1].color = D2D1::ColorF(0.014f, 0.016f, 0.038f, 1);
        sky[2].position = 1.0f; sky[2].color = D2D1::ColorF(0.030f, 0.022f, 0.045f, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);
        m_stars.Draw(dc, brush, m_time);

        // Rockets: a bright bead with a short exhaust streak.
        for (const auto& r : m_rockets) {
            draw::Circle(dc, brush, r.x, r.y, 2.4f, Color(1.0f, 0.95f, 0.8f, 0.95f));
            draw::Line(dc, brush, r.x, r.y, r.x - r.vx * 0.05f, r.y + h * 0.035f, 1.6f,
                       Color(1.0f, 0.8f, 0.5f, 0.45f));
        }

        // Debris. Young particles get a glow, old ones shrink to embers.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& p : m_parts) {
            float t = Clamp01(p.life / p.maxLife);
            float a = 0.25f + 0.75f * std::pow(t, 1.5f);
            Color c = Color::Hsv(p.hue, 0.72f, 1.0f, a);
            if (t > 0.45f && m_glow > 0.15f) {
                draw::RadialGlow(dc, brush, p.x, p.y, p.r * (3.0f + 4.0f * m_glow),
                                 c.WithAlpha(a * 0.35f * m_glow), 1.0f, 5);
            }
            // A motion streak sells the velocity.
            draw::Line(dc, brush, p.x, p.y, p.x - p.vx * 0.05f, p.y - p.vy * 0.05f,
                       p.r, c);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Hill silhouette.
        m_scratch.assign(m_hill.begin(), m_hill.end());
        m_scratch.push_back(D2D1::Point2F(m_hill.back().x, h + 2.0f));
        m_scratch.push_back(D2D1::Point2F(m_hill.front().x, h + 2.0f));
        draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(),
                          Color(0.004f, 0.006f, 0.010f, 1.0f));

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.36f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_freq = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_palette = ctx.config->sceneParam[3];
    }

    float ShellHue(Rng& rng) const {
        // Palette 0 = full spectrum, 1 = gold/silver classics, 2 = jewel tones.
        if (m_palette < 0.34f) return rng.Range(0.0f, 1.0f);
        if (m_palette < 0.67f) return (rng.Unit() < 0.5f) ? 0.10f : 0.58f;
        return (rng.Unit() < 0.5f) ? 0.85f + rng.Range(-0.05f, 0.05f) : 0.45f;
    }

    void Explode(const Rocket& r, Rng& rng) {
        const float base = (float)std::min(m_width, m_height);
        int want = (r.type == 1) ? 90 : (r.type == 2 ? 60 : 80);
        want = (int)(want * (0.7f + 0.6f * m_speed));
        for (int i = 0; i < want && (int)m_parts.size() < m_cap; ++i) {
            Part p{};
            float ang;
            float spd = base * (0.30f + 0.22f * m_speed) * rng.Range(0.6f, 1.05f);
            if (r.type == 1) {   // ring: tight circle with slight jitter
                ang = kTau * (float)i / (float)want + rng.Range(-0.03f, 0.03f);
                spd *= rng.Range(0.9f, 1.05f);
            } else {
                ang = rng.Range(0.0f, kTau);
            }
            p.x = r.x;
            p.y = r.y;
            p.vx = std::cos(ang) * spd;
            p.vy = std::sin(ang) * spd;
            p.maxLife = (r.type == 2) ? rng.Range(1.8f, 2.6f) : rng.Range(1.1f, 1.8f);
            p.life = p.maxLife;
            p.r = rng.Range(2.2f, 4.2f);
            p.hue = r.hue + rng.Range(-0.03f, 0.03f);
            p.drag = (r.type == 2) ? 1.1f : 1.6f;
            p.grav = (r.type == 2) ? base * 0.34f : base * 0.16f;
            m_parts.push_back(p);
        }
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        m_width = (int)w;
        m_height = (int)h;
        Rng rng(0xF17E01u + ctx.variation * 7919u);

        m_stars.Build(w, h, (int)(110 * ctx.density), 0xF17E02u + ctx.variation);
        m_cap = std::min(std::max((int)(340 * ctx.density), 160), 560);
        m_parts.clear();
        m_parts.reserve((size_t)m_cap);
        m_rockets.clear();
        m_next = 0.2f;

        // Low rolling hill across the bottom.
        m_hill.clear();
        float phase = rng.Range(0.0f, 90.0f);
        const int steps = 40;
        for (int i = 0; i <= steps; ++i) {
            float u = (float)i / steps;
            float n = Noise1(phase + u * 2.2f) * 0.6f + Noise1(phase * 2.0f + u * 5.0f) * 0.4f;
            m_hill.push_back(D2D1::Point2F(u * w, h * 0.90f + n * h * 0.035f));
        }
    }

    std::vector<Rocket> m_rockets;
    std::vector<Part> m_parts;
    std::vector<D2D1_POINT_2F> m_hill, m_scratch;
    draw::StarField m_stars;
    int m_cap = 400;
    int m_width = 1920, m_height = 1080;
    float m_next = 0.2f;
    float m_time = 0;
    float m_freq = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_palette = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateFireworks() { return new FireworksScene(); }

} // namespace lp::scenes
