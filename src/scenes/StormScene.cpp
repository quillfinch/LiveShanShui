// LivePaper - "Thunderstorm".
//
// A heavy cumulus ceiling over dark country. Lightning is a midpoint-displaced
// polyline struck every few seconds, with a branch, a flicker envelope, and a
// whole-sky flash that decays behind it. Faint rain sheets slant with the wind.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Cloud {
    float x, y;
    float rx, ry;
    float speed;
    float shade;     // 0 base grey .. 1 darker
};

struct Drop {
    float x, y;
    float speed;
    float len;
};

} // namespace

class StormScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Thunderstorm"; }
    const wchar_t* Description() const override {
        return L"Forked lightning flickering under a heavy storm ceiling.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Storm";
            case 1: return L"Frequency";
            case 2: return L"Glow";
            case 3: return L"Wind";
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

        // Clouds crawl sideways; faster with the wind.
        for (auto& c : m_clouds) {
            c.x += c.speed * (0.4f + 1.4f * m_wind) * dt;
            if (c.x - c.rx > ctx.width) c.x = -c.rx;
        }

        // Rain sheet particles.
        const float w = ctx.width, h = ctx.height;
        float slant = -w * (0.06f + 0.10f * m_wind);
        for (auto& d : m_drops) {
            d.x += slant * dt;
            d.y += d.speed * (0.6f + 0.8f * m_wind) * dt;
            if (d.y > h + 10.0f) { d.y = -10.0f; d.x += ctx.width * 0.37f; }
            if (d.x < -20.0f) d.x += w + 40.0f;
            if (d.x > w + 20.0f) d.x -= w + 40.0f;
        }

        // Lightning scheduler: strike, flicker out, wait, strike again.
        m_flash = std::max(0.0f, m_flash - dt * 2.6f);
        if (m_boltLife > 0.0f) {
            m_boltLife -= dt;
        } else {
            m_nextStrike -= dt * (0.35f + 1.9f * m_freq);
            if (m_nextStrike <= 0.0f) {
                Strike(ctx);
                m_nextStrike = 0.5f + Fract(std::sin(m_time * 7.77f) * 321.4321f) * 0.8f;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Storm light: the flash lifts the whole sky for a moment.
        float lift = m_flash * 0.11f;
        D2D1_GRADIENT_STOP sky[3];
        Color topC = Color::Hsv(0.63f, 0.30f, 0.130f + lift);
        Color midC = Color::Hsv(0.60f, 0.24f, 0.190f + lift);
        Color lowC = Color::Hsv(0.58f, 0.20f, 0.240f + lift);
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(topC.r, topC.g, topC.b, 1);
        sky[1].position = 0.55f; sky[1].color = D2D1::ColorF(midC.r, midC.g, midC.b, 1);
        sky[2].position = 1.0f; sky[2].color = D2D1::ColorF(lowC.r, lowC.g, lowC.b, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);

        // Cloud ceiling: heavy ellipses, brighter bellies where the flash hits.
        for (const auto& c : m_clouds) {
            Color cl = Color::Hsv(0.62f, 0.13f, 0.155f - c.shade * 0.050f + lift * 2.2f, 0.94f);
            draw::Circle(dc, brush, c.x, c.y, c.rx, cl);
            draw::Circle(dc, brush, c.x - c.rx * 0.55f, c.y + c.ry * 0.25f, c.rx * 0.7f, cl);
            draw::Circle(dc, brush, c.x + c.rx * 0.55f, c.y + c.ry * 0.2f, c.rx * 0.66f, cl);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& c : m_clouds) {
            draw::Circle(dc, brush, c.x, c.y + c.ry * 0.55f, c.rx * 0.8f,
                         Color::Hsv(0.60f, 0.18f, 1.0f, m_flash * 0.10f));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Distant rain sheets.
        Color rain = Color::Hsv(0.58f, 0.20f, 0.9f, 0.07f);
        float slant = (0.06f + 0.10f * m_wind);
        for (const auto& d : m_drops) {
            draw::Line(dc, brush, d.x, d.y, d.x + d.len * slant, d.y - d.len, 1.0f, rain);
        }

        // Low hills on the horizon ground the scene.
        m_scratch.assign(m_hill.begin(), m_hill.end());
        m_scratch.push_back(D2D1::Point2F(m_hill.back().x, h + 2.0f));
        m_scratch.push_back(D2D1::Point2F(m_hill.front().x, h + 2.0f));
        draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(),
                          Color(0.004f, 0.006f, 0.010f, 1.0f));

        // --- the bolt -----------------------------------------------------
        if (m_boltLife > 0.0f && m_bolt.size() >= 2) {
            float env = Clamp01(m_boltLife / m_boltMaxLife);
            // Flicker: real bolts re-strike along the same channel.
            float flicker = 0.75f + 0.25f * std::sin(m_time * 63.0f + m_boltSeed);
            float coreA = env * flicker;
            Color glowC = Color::Hsv(0.60f, 0.25f, 1.0f);
            dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
            for (size_t i = 1; i < m_bolt.size(); ++i) {
                draw::Line(dc, brush, m_bolt[i - 1].x, m_bolt[i - 1].y,
                           m_bolt[i].x, m_bolt[i].y, 22.0f * m_glow,
                           Color(glowC.r, glowC.g, glowC.b, 0.22f * coreA));
                draw::Line(dc, brush, m_bolt[i - 1].x, m_bolt[i - 1].y,
                           m_bolt[i].x, m_bolt[i].y, 5.0f * m_glow,
                           Color(0.8f, 0.85f, 1.0f, 0.35f * coreA));
                draw::Line(dc, brush, m_bolt[i - 1].x, m_bolt[i - 1].y,
                           m_bolt[i].x, m_bolt[i].y, 1.8f, Color(1, 1, 1, 0.95f * coreA));
            }
            for (size_t i = 1; i < m_branch.size(); ++i) {
                draw::Line(dc, brush, m_branch[i - 1].x, m_branch[i - 1].y,
                           m_branch[i].x, m_branch[i].y, 3.5f * m_glow,
                           Color(0.8f, 0.85f, 1.0f, 0.30f * coreA));
                draw::Line(dc, brush, m_branch[i - 1].x, m_branch[i - 1].y,
                           m_branch[i].x, m_branch[i].y, 1.1f, Color(1, 1, 1, 0.7f * coreA));
            }
            // Ground splash where the channel lands.
            draw::RadialGlow(dc, brush, m_bolt.back().x, m_bolt.back().y, h * 0.10f,
                             glowC.WithAlpha(0.35f * coreA), 1.0f, 8);
            dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        }

        // The flash itself, over everything but the vignette.
        if (m_flash > 0.001f) {
            draw::RoundedRect(dc, brush, 0, 0, w, h, 0,
                              Color(0.85f, 0.90f, 1.0f, m_flash * 0.13f));
        }

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0.01f, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_cloudAmt = ctx.config->sceneParam[0];
        m_freq = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_wind = ctx.config->sceneParam[3];
    }

    // Midpoint displacement: subdivide the channel four times, offsetting each
    // new point along the segment normal. Capacity is reserved up-front, so no
    // allocation happens after warm-up.
    void Strike(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng((uint32_t)(m_time * 911.0f) + 7u + ctx.variation);

        float x0 = rng.Range(0.18f, 0.82f) * w;
        float x1 = Clamp(x0 + rng.Range(-0.22f, 0.22f) * w, w * 0.04f, w * 0.96f);
        float y0 = h * 0.17f;
        float y1 = h * rng.Range(0.82f, 0.93f);
        m_bolt.clear();
        m_bolt.push_back(D2D1::Point2F(x0, y0));
        m_bolt.push_back(D2D1::Point2F(x1, y1));
        for (int it = 0; it < 4; ++it) {
            const size_t n = m_bolt.size();
            for (size_t i = 0; i + 1 < n; i += 2) {   // insert after every even index
                D2D1_POINT_2F a = m_bolt[i], b = m_bolt[i + 1];
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = std::sqrt(dx * dx + dy * dy) + 0.001f;
                float off = rng.Range(-1.0f, 1.0f) * len * 0.16f;
                D2D1_POINT_2F m{ (a.x + b.x) * 0.5f - dy / len * off,
                                 (a.y + b.y) * 0.5f + dx / len * off };
                m_bolt.insert(m_bolt.begin() + (long)(i + 1), m);
            }
        }
        // One branch off the lower third, running outward and down.
        size_t bi = m_bolt.size() * 2 / 3;
        bi -= bi % 2;   // keep it on an original vertex
        D2D1_POINT_2F p = m_bolt[bi];
        float dir = (p.x > w * 0.5f) ? 1.0f : -1.0f;
        m_branch.clear();
        m_branch.push_back(p);
        float bx = p.x, by = p.y;
        for (int k = 0; k < 4; ++k) {
            bx += dir * rng.Range(h * 0.02f, h * 0.05f);
            by += rng.Range(h * 0.02f, h * 0.045f);
            m_branch.push_back(D2D1::Point2F(bx, by));
        }

        m_boltMaxLife = 1.0f;
        m_boltLife = m_boltMaxLife;
        m_boltSeed = rng.Range(0.0f, kTau);
        m_flash = 1.0f;
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x57EA12u + ctx.variation * 7919u);

        m_clouds.clear();
        int clouds = (int)((7 + 7 * m_cloudAmt) * ctx.density);
        clouds = std::min(clouds, 16);
        m_clouds.reserve((size_t)clouds);
        for (int i = 0; i < clouds; ++i) {
            Cloud c{};
            c.x = rng.Range(0.0f, w);
            c.y = h * rng.Range(0.02f, 0.26f);
            c.rx = rng.Range(h * 0.07f, h * 0.17f);
            c.ry = c.rx * rng.Range(0.35f, 0.5f);
            c.speed = rng.Range(4.0f, 12.0f);
            c.shade = rng.Range(0.0f, 1.0f);
            m_clouds.push_back(c);
        }

        m_drops.clear();
        int drops = (int)((30 + 90 * m_wind) * ctx.density);
        drops = std::min(drops, 140);
        m_drops.reserve((size_t)drops);
        for (int i = 0; i < drops; ++i) {
            Drop d{};
            d.x = rng.Range(0.0f, w);
            d.y = rng.Range(h * 0.25f, h);
            d.speed = rng.Range(h * 0.45f, h * 0.75f);
            d.len = rng.Range(h * 0.015f, h * 0.03f);
            m_drops.push_back(d);
        }

        m_hill.clear();
        float phase = rng.Range(0.0f, 70.0f);
        for (int i = 0; i <= 32; ++i) {
            float u = (float)i / 32.0f;
            float n = Noise1(phase + u * 2.4f) * 0.6f + Noise1(phase * 2.2f + u * 5.0f) * 0.4f;
            m_hill.push_back(D2D1::Point2F(u * w, h * 0.925f + n * h * 0.025f));
        }
        m_scratch.reserve(40);
        m_bolt.reserve(40);
        m_branch.reserve(8);
        m_boltLife = 0.0f;
        m_nextStrike = 1.1f;    // first bolt lands ~0.85s in and lives ~1s, so a
        // static render still catches it.
        m_flash = 0.0f;
    }

    std::vector<Cloud> m_clouds;
    std::vector<Drop> m_drops;
    std::vector<D2D1_POINT_2F> m_bolt;
    std::vector<D2D1_POINT_2F> m_branch;
    std::vector<D2D1_POINT_2F> m_hill, m_scratch;
    float m_boltLife = 0.0f, m_boltMaxLife = 1.0f, m_boltSeed = 0.0f;
    float m_flash = 0.0f, m_nextStrike = 1.0f;
    float m_time = 0;
    float m_cloudAmt = 0.5f, m_freq = 0.5f, m_glow = 0.5f, m_wind = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateStorm() { return new StormScene(); }

} // namespace lp::scenes
