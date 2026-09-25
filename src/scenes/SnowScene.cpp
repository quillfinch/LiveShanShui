// LivePaper - "Snowfall".
//
// A winter dusk: moon, layered hills and a conifer treeline under three parallax
// layers of falling snow. The ground drift is built from cached overlapping mounds,
// and the wind is noise-driven so the flakes drift rather than fall in straight lines.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Flake {
    float x, y;
    float r;
    float speed;       // px/s downward
    float layer;       // 0 far .. 2 near
    float swayAmp;
    float swayFreq;
    float phase;
};

struct Tree {
    float x, baseY;
    float h;           // tree height
    float w;           // skirt width
};

} // namespace

class SnowScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Snowfall"; }
    const wchar_t* Description() const override {
        return L"Layered snow drifting over a moonlit winter treeline.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Snowfall";
            case 1: return L"Wind";
            case 2: return L"Moon";
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
        m_windTime += dt * (0.4f + 1.6f * m_wind);

        const float w = ctx.width, h = ctx.height;
        const float fallScale = 0.5f + 0.8f * m_wind;   // wind also lifts the tempo a touch
        for (auto& f : m_flakes) {
            f.y += f.speed * fallScale * dt;
            f.phase += dt * f.swayFreq;
            // Global wind field plus per-flake sway.
            float wind = (Noise1(m_windTime * 0.35f + f.layer * 7.0f) * 0.6f +
                          Noise1(f.x * 0.002f + m_windTime * 0.2f)) * (26.0f + 70.0f * m_wind);
            f.x += (wind + std::sin(f.phase) * f.swayAmp * 0.3f) * dt;
            if (f.y > h + 8.0f) { f.y = -8.0f; f.x = Fract(f.x / w + 0.017f) * w; }
            if (f.x < -30.0f) f.x += w + 60.0f;
            if (f.x > w + 30.0f) f.x -= w + 60.0f;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Winter dusk sky.
        float duskHue = Lerp(0.60f, 0.76f, m_dusk);   // steel blue .. violet
        D2D1_GRADIENT_STOP sky[4];
        Color zenith = Color::Hsv(duskHue, 0.55f, 0.10f);
        Color upper  = Color::Hsv(duskHue + 0.01f, 0.50f, 0.16f);
        Color horiz  = Color::Hsv(duskHue + 0.03f, 0.42f, 0.30f);
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(zenith.r, zenith.g, zenith.b, 1);
        sky[1].position = 0.45f; sky[1].color = D2D1::ColorF(upper.r, upper.g, upper.b, 1);
        sky[2].position = 0.78f; sky[2].color = D2D1::ColorF(horiz.r, horiz.g, horiz.b, 1);
        sky[3] = sky[2];
        draw::VerticalGradient(dc, w, h, sky, 3);

        m_stars.Draw(dc, brush, m_time);

        // Moon with a halo.
        float mx = w * 0.74f, my = h * 0.16f;
        float moonA = 0.55f + 0.45f * m_moon;
        draw::RadialGlow(dc, brush, mx, my, h * (0.16f + 0.12f * m_moon),
                         Color::Hsv(0.62f, 0.18f, 1.0f, 0.22f * moonA), 1.0f, 14);
        draw::Circle(dc, brush, mx, my, h * 0.035f, Color(0.95f, 0.96f, 1.0f, 0.96f * moonA));
        draw::Circle(dc, brush, mx - h * 0.008f, my + h * 0.006f, h * 0.007f,
                     Color(0.88f, 0.89f, 0.94f, 0.5f * moonA));

        // Far hills, hazier and lighter than the near ground.
        FillRidge(dc, brush, m_hillFar, h, Color::Hsv(duskHue + 0.02f, 0.35f, 0.24f).WithAlpha(0.8f));
        FillRidge(dc, brush, m_hillNear, h, Color::Hsv(duskHue + 0.03f, 0.40f, 0.16f));

        // Ground: a snowfield that catches the moonlight.
        Color snowLit = Color::Hsv(duskHue + 0.02f, 0.16f, 0.66f);
        Color snowShade = Color::Hsv(duskHue + 0.02f, 0.22f, 0.38f);
        D2D1_GRADIENT_STOP ground[2];
        ground[0].position = 0.0f; ground[0].color = D2D1::ColorF(snowLit.r, snowLit.g, snowLit.b, 1);
        ground[1].position = 1.0f; ground[1].color = D2D1::ColorF(snowShade.r, snowShade.g, snowShade.b, 1);
        {
            float g0 = h * 0.80f;
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(ground, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
                props.startPoint = D2D1::Point2F(0, g0);
                props.endPoint = D2D1::Point2F(0, h);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, g0, w, h), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }

        // Treeline on the ground edge.
        for (const auto& t : m_trees) {
            Color treeC = Color::Hsv(duskHue + 0.05f, 0.42f, 0.09f);
            Color snowC = Color::Hsv(duskHue + 0.02f, 0.18f, 0.55f);
            // Three stacked skirts make a conifer; each carries a snow cap.
            for (int s = 0; s < 3; ++s) {
                float sy = t.baseY - t.h * 0.30f * s;         // skirt base line
                float sh = t.h * 0.42f;                       // skirt height
                float sw = t.w * (1.0f - 0.22f * s);          // narrower toward the top
                D2D1_POINT_2F tri[3] = {
                    D2D1::Point2F(t.x, sy - sh),
                    D2D1::Point2F(t.x - sw * 0.5f, sy),
                    D2D1::Point2F(t.x + sw * 0.5f, sy),
                };
                draw::FillPolygon(dc, brush, tri, 3, treeC);
                // Snow cap: a thinner triangle hugging the tip.
                D2D1_POINT_2F cap[3] = {
                    D2D1::Point2F(t.x, sy - sh),
                    D2D1::Point2F(t.x - sw * 0.20f, sy - sh * 0.55f),
                    D2D1::Point2F(t.x + sw * 0.20f, sy - sh * 0.55f),
                };
                draw::FillPolygon(dc, brush, cap, 3, snowC.WithAlpha(0.75f));
            }
        }

        // Snow: far, mid, near layers.
        for (int layer = 0; layer < 3; ++layer) {
            for (const auto& f : m_flakes) {
                if ((int)f.layer != layer) continue;
                float t = f.layer / 2.0f;   // 0 far .. 1 near
                float a = Lerp(0.35f, 0.92f, t);
                // Near flakes get a soft edge so they read as depth of field.
                if (f.layer > 1.5f && f.r > 3.5f) {
                    draw::Circle(dc, brush, f.x, f.y, f.r * 1.9f, Color(1, 1, 1, a * 0.16f));
                }
                draw::Circle(dc, brush, f.x, f.y, f.r, Color(1, 1, 1, a));
            }
        }

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.01f, 0.02f, 0.05f, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_snow = ctx.config->sceneParam[0];
        m_wind = ctx.config->sceneParam[1];
        m_moon = ctx.config->sceneParam[2];
        m_dusk = ctx.config->sceneParam[3];
    }

    // Fills the area below a ridge polyline down to `bottomY`. Uses the member
    // scratch buffer so the per-frame path is allocation-free after warm-up.
    void FillRidge(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   const std::vector<D2D1_POINT_2F>& pts, float bottomY, const Color& color) {
        if (pts.size() < 3) return;
        m_scratch.assign(pts.begin(), pts.end());
        m_scratch.push_back(D2D1::Point2F(pts.back().x, bottomY));
        m_scratch.push_back(D2D1::Point2F(pts.front().x, bottomY));
        draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(), color);
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x5D07u + ctx.variation * 2654435761u);

        m_stars.Build(w, h, (int)(90 * ctx.density), 0x5D07u + ctx.variation);

        m_flakes.clear();
        int flakeCount = (int)((130 + 270 * m_snow) * ctx.density);
        flakeCount = std::min(flakeCount, 480);
        m_flakes.reserve((size_t)flakeCount);
        for (int i = 0; i < flakeCount; ++i) {
            Flake f{};
            f.x = rng.Range(0.0f, w);
            f.y = rng.Range(0.0f, h);
            f.layer = (float)(i % 3);
            float t = f.layer / 2.0f;
            f.r = Lerp(1.0f, 3.6f, t) * rng.Range(0.7f, 1.35f);
            f.speed = Lerp(16.0f, 64.0f, t) * rng.Range(0.8f, 1.25f);
            f.swayAmp = rng.Range(8.0f, 30.0f) * (0.4f + t);
            f.swayFreq = rng.Range(0.5f, 1.6f);
            f.phase = rng.Range(0.0f, kTau);
            m_flakes.push_back(f);
        }

        // Hills as noise ridges (points rebuilt only on resize).
        auto buildRidge = [&](std::vector<D2D1_POINT_2F>& out, float baseY, float amp, uint32_t seed) {
            out.clear();
            const int steps = 48;
            Rng rr(seed);
            float phase = rr.Range(0.0f, 100.0f);
            for (int i = 0; i <= steps; ++i) {
                float t = (float)i / steps;
                float x = t * w;
                float n = Noise1(phase + t * 3.2f) * 0.6f + Noise1(phase * 2.0f + t * 7.0f) * 0.4f;
                out.push_back(D2D1::Point2F(x, baseY + n * amp));
            }
        };
        buildRidge(m_hillFar, h * 0.70f, h * 0.05f, 911u + ctx.variation);
        buildRidge(m_hillNear, h * 0.80f, h * 0.035f, 977u + ctx.variation);

        m_trees.clear();
        int treeCount = (int)((8 + 14 * ctx.density));
        for (int i = 0; i < treeCount; ++i) {
            Tree t{};
            t.x = rng.Range(-0.01f, 1.01f) * w;
            t.baseY = h * rng.Range(0.80f, 0.86f);
            t.h = h * rng.Range(0.07f, 0.16f);
            t.w = t.h * rng.Range(0.42f, 0.60f);
            m_trees.push_back(t);
        }
    }

    std::vector<Flake> m_flakes;
    std::vector<Tree> m_trees;
    std::vector<D2D1_POINT_2F> m_hillFar, m_hillNear, m_scratch;
    draw::StarField m_stars;
    float m_time = 0, m_windTime = 0;
    float m_snow = 0.5f, m_wind = 0.5f, m_moon = 0.5f, m_dusk = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateSnow() { return new SnowScene(); }

} // namespace lp::scenes
