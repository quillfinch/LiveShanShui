// LivePaper - "Golden Dunes".
//
// A desert at late afternoon: a low fat sun, layered dune ridges receding into
// haze, wind-blown sand streaking across the crests, and a shimmering mirage
// band where the hot air meets the horizon.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Streak {
    float x, y;
    float len;
    float speed;
    float alpha;
    float rise;      // slight upward slope of the streak
    float phase;     // identity for respawn placement
};

} // namespace

class DesertScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Golden Dunes"; }
    const wchar_t* Description() const override {
        return L"Wind-blown sand over layered dunes under a low sun.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Dunes";
            case 1: return L"Wind";
            case 2: return L"Sun";
            case 3: return L"Heat";
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

        const float speedScale = 0.5f + 1.6f * m_wind;
        for (auto& s : m_streaks) {
            s.x += s.speed * speedScale * dt;
            if (s.x - s.len > ctx.width) {
                s.x = -s.len;
                s.y = ctx.height * (0.40f + 0.58f * Fract(std::sin(s.phase * 12.9898f) * 43758.5453f));
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Sky: dusty blue falling into a hot gold horizon. The middle stop is a
        // mauve chosen in hue terms that avoids the murky green an RGB blend
        // from blue to gold otherwise passes through.
        D2D1_GRADIENT_STOP sky[3];
        Color top = Color::Hsv(0.58f, 0.32f, 0.42f);
        Color mid = Color::Hsv(0.82f, 0.22f, 0.62f);
        Color low = Color::Hsv(0.09f, 0.68f, 0.92f);
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(top.r, top.g, top.b, 1);
        sky[1].position = 0.55f; sky[1].color = D2D1::ColorF(mid.r, mid.g, mid.b, 1);
        sky[2].position = 1.0f; sky[2].color = D2D1::ColorF(low.r, low.g, low.b, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);

        // Sun: a fat low disc with a wide warm halo.
        float sx = w * 0.50f, sy = h * 0.30f;
        float sr = h * (0.075f + 0.055f * m_sun);
        draw::RadialGlow(dc, brush, sx, sy, sr * (3.4f + 1.8f * m_sun),
                         Color::Hsv(0.10f, 0.55f, 1.0f, 0.20f + 0.14f * m_sun), 1.0f, 16);
        draw::Circle(dc, brush, sx, sy, sr, Color::Hsv(0.10f, 0.40f, 1.0f, 0.95f));
        draw::Circle(dc, brush, sx, sy, sr * 0.86f, Color(1.0f, 0.96f, 0.82f, 0.95f));

        // Dunes, far to near. Far ridges are lighter (atmospheric haze) and lower.
        const int layers = 3 + (int)(m_dunes * 2.0f);
        for (int l = 0; l < layers; ++l) {
            float t = (float)l / std::max(1, layers - 1);   // 0 far .. 1 near
            float baseY = h * Lerp(0.46f, 0.86f, t);
            float amp = h * Lerp(0.035f, 0.085f, t);
            Color dune = Color::Hsv(Lerp(0.10f, 0.075f, t), Lerp(0.42f, 0.55f, t),
                                    Lerp(0.72f, 0.34f, t));
            // Fill past the layer's own base so nearer ridges never leave sky
            // seams showing through between layers.
            FillDune(dc, brush, m_dunePts[l], h, dune);
            // Sunlit crest line on the left-facing slope.
            Color crest = Color::Hsv(0.10f, 0.45f, 0.98f, 0.20f * (1.0f - t * 0.5f));
            for (size_t i = 1; i < m_dunePts[l].size(); ++i) {
                draw::Line(dc, brush, m_dunePts[l][i - 1].x, m_dunePts[l][i - 1].y,
                           m_dunePts[l][i].x, m_dunePts[l][i].y, 1.6f, crest);
            }
        }

        // Mirage: a bright, slowly breathing band where hot air meets the horizon.
        float shimmer = 0.6f + 0.4f * std::sin(m_time * 1.4f) * std::sin(m_time * 0.53f);
        draw::RoundedRect(dc, brush, 0, h * 0.435f, w, h * 0.022f, h * 0.01f,
                          Color::Hsv(0.11f, 0.30f, 1.0f,
                                     (0.05f + 0.10f * m_heat) * shimmer));

        // Wind-blown sand, additive so the streaks glow against the dunes.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& s : m_streaks) {
            Color c = Color::Hsv(0.11f, 0.25f, 1.0f, s.alpha * (0.4f + 0.6f * m_wind));
            draw::Line(dc, brush, s.x, s.y, s.x - s.len, s.y - s.rise, 1.1f, c);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.03f, 0.01f, 0.00f, 0.38f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    static constexpr int kMaxLayers = 5;

    void ReadParams(const SceneCtx& ctx) {
        m_dunes = ctx.config->sceneParam[0];
        m_wind = ctx.config->sceneParam[1];
        m_sun = ctx.config->sceneParam[2];
        m_heat = ctx.config->sceneParam[3];
    }

    // Fills the area below a ridge polyline down to the screen bottom.
    void FillDune(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                  const std::vector<D2D1_POINT_2F>& pts, float bottomY, const Color& color) {
        if (pts.size() < 3) return;
        m_scratch.assign(pts.begin(), pts.end());
        m_scratch.push_back(D2D1::Point2F(pts.back().x, bottomY + 2.0f));
        m_scratch.push_back(D2D1::Point2F(pts.front().x, bottomY + 2.0f));
        draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(), color);
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0xD35E27u + ctx.variation * 7919u);

        // Ridge polylines: one per layer, rebuilt only here (resize / reshuffle).
        const int layers = 3 + (int)(m_dunes * 2.0f);
        for (int l = 0; l < kMaxLayers; ++l) {
            m_dunePts[l].clear();
            if (l >= layers) continue;
            float t = (float)l / std::max(1, layers - 1);
            float baseY = h * Lerp(0.46f, 0.86f, t);
            float amp = h * Lerp(0.035f, 0.085f, t);
            float phase = rng.Range(0.0f, 100.0f);
            const int steps = 36;
            for (int i = 0; i <= steps; ++i) {
                float u = (float)i / steps;
                // Two octaves of noise make long dune backs with sharp-ish crests.
                float n = Noise1(phase + u * 2.6f) * 0.65f + Noise1(phase * 2.3f + u * 6.0f) * 0.35f;
                m_dunePts[l].push_back(D2D1::Point2F(u * w, baseY + n * amp));
            }
        }

        m_streaks.clear();
        int count = (int)((26 + 70 * m_wind) * ctx.density);
        count = std::min(count, 120);
        m_streaks.reserve((size_t)count);
        for (int i = 0; i < count; ++i) {
            Streak s{};
            s.x = rng.Range(0.0f, w);
            s.y = h * rng.Range(0.42f, 0.97f);
            s.len = rng.Range(h * 0.02f, h * 0.10f);
            s.speed = rng.Range(h * 0.05f, h * 0.16f);
            s.alpha = rng.Range(0.04f, 0.13f);
            s.rise = rng.Range(0.0f, h * 0.008f);
            s.phase = rng.Range(0.0f, 100.0f);
            m_streaks.push_back(s);
        }
    }

    std::vector<D2D1_POINT_2F> m_dunePts[kMaxLayers];
    std::vector<D2D1_POINT_2F> m_scratch;
    std::vector<Streak> m_streaks;
    float m_time = 0;
    float m_dunes = 0.5f, m_wind = 0.5f, m_sun = 0.5f, m_heat = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateDesert() { return new DesertScene(); }

} // namespace lp::scenes
