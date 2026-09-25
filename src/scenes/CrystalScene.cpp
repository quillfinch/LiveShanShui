// LivePaper - "Crystal Cave".
//
// A grotto of faceted crystals growing from the cave floor. Each crystal is a
// pentagon (two shade passes fake the facet split), glows from within with a slow
// pulse, and throws sparkles from its edges. Shafts of light fall from above.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Crystal {
    float x, baseY;
    float len, wid;
    float lean;      // degrees
    float hue;
    float phase;     // glow pulse phase
    float pulseRate;
    bool back;       // background crystal: dim, no glow pool
};

struct Sparkle {
    float x, y;
    float phase;
    float speed;
    float size;
};

} // namespace

class CrystalScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Crystal Cave"; }
    const wchar_t* Description() const override {
        return L"Faceted crystals glowing in a dark grotto.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Crystals";
            case 1: return L"Glow";
            case 2: return L"Sparkle";
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
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Cave dark: cold near-black, slightly lifted in the middle.
        D2D1_GRADIENT_STOP bg[3];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.008f, 0.010f, 1);
        bg[1].position = 0.55f; bg[1].color = D2D1::ColorF(0.010f, 0.020f, 0.026f, 1);
        bg[2].position = 1.0f; bg[2].color = D2D1::ColorF(0.004f, 0.010f, 0.012f, 1);
        draw::VerticalGradient(dc, w, h, bg, 3);
        draw::RadialGlow(dc, brush, w * 0.5f, h * 0.55f, std::max(w, h) * 0.5f,
                         Color::Hsv(0.50f, 0.35f, 1.0f, 0.05f), 1.0f, 10);

        // Shafts of light from a crack above, swaying very slowly.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (int b = 0; b < 2; ++b) {
            float topX = w * (0.30f + 0.36f * b) + std::sin(m_time * 0.11f + b * 2.1f) * w * 0.01f;
            float botX = topX + w * (0.05f + 0.03f * b);
            float alpha = (0.016f + 0.008f * std::sin(m_time * 0.23f + b * 1.7f));
            D2D1_POINT_2F beam[4] = {
                D2D1::Point2F(topX - w * 0.030f, -2.0f),
                D2D1::Point2F(topX + w * 0.060f, -2.0f),
                D2D1::Point2F(botX + w * 0.105f, h),
                D2D1::Point2F(botX - w * 0.075f, h),
            };
            draw::FillPolygon(dc, brush, beam, 4, Color::Hsv(0.50f, 0.25f, 1.0f, alpha));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Floor.
        D2D1_GRADIENT_STOP floorG[2];
        Color f0 = Color::Hsv(0.52f, 0.30f, 0.045f);
        Color f1 = Color::Hsv(0.52f, 0.35f, 0.015f);
        floorG[0].position = 0.0f; floorG[0].color = D2D1::ColorF(f0.r, f0.g, f0.b, 1);
        floorG[1].position = 1.0f; floorG[1].color = D2D1::ColorF(f1.r, f1.g, f1.b, 1);
        {
            float fy = h * 0.86f;
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(floorG, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
                props.startPoint = D2D1::Point2F(0, fy);
                props.endPoint = D2D1::Point2F(0, h);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, fy, w, h), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }

        // Glow pools on the floor beneath each front crystal (additive bloom).
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& c : m_crystals) {
            if (c.back) continue;
            float pulse = 0.72f + 0.28f * std::sin(m_time * c.pulseRate + c.phase);
            Color glow = Color::Hsv(c.hue, 0.55f, 1.0f, 0.10f * (0.4f + 1.1f * m_glow) * pulse);
            draw::RadialGlow(dc, brush, c.x, c.baseY, c.len * 0.9f, glow, 1.0f, 14);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Crystals, back row first.
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& c : m_crystals) {
                if ((pass == 1) != c.back) continue;
                float dim = c.back ? 0.45f : 1.0f;
                float pulse = 0.72f + 0.28f * std::sin(m_time * c.pulseRate + c.phase);

                dc->SetTransform(D2D1::Matrix3x2F::Rotation(c.lean, D2D1::Point2F(c.x, c.baseY)));
                // Shaft silhouette: a pentagon - flat base, shoulders, pointed tip.
                float wl = c.wid * 0.6f, wr = c.wid * 0.6f;
                D2D1_POINT_2F shaft[5] = {
                    D2D1::Point2F(c.x - wl, c.baseY),
                    D2D1::Point2F(c.x - wl * 0.75f, c.baseY - c.len * 0.55f),
                    D2D1::Point2F(c.x, c.baseY - c.len),
                    D2D1::Point2F(c.x + wr * 0.75f, c.baseY - c.len * 0.55f),
                    D2D1::Point2F(c.x + wr, c.baseY),
                };
                Color body = Color::Hsv(c.hue, 0.55f, 0.22f * dim, 0.96f);
                draw::FillPolygon(dc, brush, shaft, 5, body);
                // Bright right facet: the light-facing half.
                D2D1_POINT_2F facet[4] = {
                    D2D1::Point2F(c.x, c.baseY - c.len),
                    D2D1::Point2F(c.x + wr * 0.75f, c.baseY - c.len * 0.55f),
                    D2D1::Point2F(c.x + wr, c.baseY),
                    D2D1::Point2F(c.x + wr * 0.1f, c.baseY),
                };
                Color lit = Color::Hsv(c.hue, 0.38f, (0.38f + 0.28f * pulse * m_glow) * dim, 0.95f);
                draw::FillPolygon(dc, brush, facet, 4, lit);
                // Inner luminous core, brightest low where the pool glows.
                Color inner = Color::Hsv(c.hue, 0.30f, 1.0f, (0.14f + 0.16f * m_glow) * pulse * dim);
                draw::RadialGlow(dc, brush, c.x + c.wid * 0.1f, c.baseY - c.len * 0.30f,
                                 c.wid * 0.85f, inner, 1.0f, 9);
                dc->SetTransform(D2D1::Matrix3x2F::Identity());
            }
        }

        // Edge sparkles.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& s : m_sparkles) {
            float tw = std::sin(m_time * s.speed + s.phase);
            if (tw <= 0.0f) continue;
            float a = tw * tw * tw * (0.35f + 0.65f * m_sparkle);
            Color c = Color::Hsv(0.50f, 0.15f, 1.0f, a);
            draw::Circle(dc, brush, s.x, s.y, s.size, c);
            if (s.size > 1.4f) {
                draw::Line(dc, brush, s.x - s.size * 3.0f, s.y, s.x + s.size * 3.0f, s.y, 0.8f, c);
                draw::Line(dc, brush, s.x, s.y - s.size * 3.0f, s.x, s.y + s.size * 3.0f, 0.8f, c);
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.44f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_clusters = ctx.config->sceneParam[0];
        m_glow = ctx.config->sceneParam[1];
        m_sparkle = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0xC895A1u + ctx.variation * 7919u);

        m_crystals.clear();
        int want = (int)((6 + 8 * m_clusters) * ctx.density);
        want = std::min(want, 16);
        m_crystals.reserve((size_t)want);
        float baseHue = Fract(0.48f + (m_hue - 0.5f) * 0.6f);
        for (int i = 0; i < want; ++i) {
            Crystal c{};
            c.back = (i % 3 == 0) && i > 0;
            c.x = w * (0.06f + 0.88f * Fract((float)i * 0.618f + rng.Range(-0.04f, 0.04f)));
            c.baseY = h * (c.back ? rng.Range(0.80f, 0.86f) : rng.Range(0.87f, 0.965f));
            c.len = h * (c.back ? rng.Range(0.10f, 0.20f) : rng.Range(0.16f, 0.38f));
            c.wid = c.len * rng.Range(0.30f, 0.45f);
            c.lean = rng.Range(-14.0f, 14.0f);
            c.hue = Fract(baseHue + rng.Range(-0.07f, 0.07f));
            c.phase = rng.Range(0.0f, kTau);
            c.pulseRate = rng.Range(0.4f, 1.1f);
            m_crystals.push_back(c);
        }

        m_sparkles.clear();
        int sparkles = (int)((14 + 22 * m_sparkle) * ctx.density);
        sparkles = std::min(sparkles, 44);
        m_sparkles.reserve((size_t)sparkles);
        for (int i = 0; i < sparkles && !m_crystals.empty(); ++i) {
            const Crystal& c = m_crystals[rng.Int(0, (int)m_crystals.size() - 1)];
            Sparkle s{};
            // Sparkles sit near the crystal's upper edges.
            float t = rng.Range(0.35f, 0.95f);
            float side = (rng.Unit() < 0.5f) ? -1.0f : 1.0f;
            s.x = c.x + side * c.wid * 0.5f * (1.0f - t * 0.4f) + rng.Range(-3.0f, 3.0f);
            s.y = c.baseY - c.len * t;
            s.phase = rng.Range(0.0f, kTau);
            s.speed = rng.Range(0.8f, 2.4f);
            s.size = rng.Range(0.8f, 2.1f);
            m_sparkles.push_back(s);
        }
    }

    std::vector<Crystal> m_crystals;
    std::vector<Sparkle> m_sparkles;
    float m_time = 0;
    float m_sparkle = 0.5f, m_clusters = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateCrystal() { return new CrystalScene(); }

} // namespace lp::scenes
