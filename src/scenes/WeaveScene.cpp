// LivePaper - "Weave".
//
// Crisscrossing sinusoidal ribbons in palette colors, drawn under per-ribbon
// rotations so braids run at their own angles across the frame. Overlaps bloom
// additively, and a bright core thread runs down each ribbon. Colors come from
// the shared palette - seed or custom color re-threads the whole weave.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

constexpr int kSamples = 26;

struct Ribbon {
    float angle;     // degrees
    float yBase;     // centre line in the rotated frame, fraction of the span
    float amp;       // undulation, px at 1080p reference
    float freq;
    float phase;
    float speedMul;
    float w0, w1;    // half-width range, px
    int ci;
    float alpha;
};

} // namespace

class WeaveScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Weave"; }
    const wchar_t* Description() const override {
        return L"Ribbon braids crossing in slow, weaving waves of color.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Ribbons";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Blend";
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
        m_time += dt * (0.25f + 0.9f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float span = std::sqrt(w * w + h * h) + 120.0f;   // covers any rotation
        const float ref = std::min(w, h) / 1080.0f;
        Palette pal = MakePalette(ctx);

        float bh = 0, bs = 0, bv = 0;
        pal.c[0].ToHsv(bh, bs, bv);
        Color ground = Color::Hsv(Fract(bh + 0.45f), bs * 0.7f, 0.035f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.004f, 0.010f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(ground.r, ground.g, ground.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& r : m_ribbons) {
            dc->SetTransform(D2D1::Matrix3x2F::Rotation(r.angle, D2D1::Point2F(cx, cy)));
            float yBase = (r.yBase - 0.5f) * span;
            m_top.clear();
            m_bottom.clear();
            for (int k = 0; k <= kSamples; ++k) {
                float u = (float)k / kSamples;
                float x = -span * 0.5f + span * u;
                float yc = yBase
                         + std::sin(u * r.freq * kTau + m_time * r.speedMul + r.phase) * r.amp
                         + Noise1(u * 2.4f + r.phase + m_time * 0.2f * r.speedMul) * r.amp * 0.45f;
                float half = Lerp(r.w0, r.w1, 0.5f + 0.5f * std::sin(u * 4.2f + r.phase * 2.0f)) * ref;
                m_top.push_back(D2D1::Point2F(x, yc - half));
                m_bottom.push_back(D2D1::Point2F(x, yc + half));
            }
            m_band.assign(m_top.begin(), m_top.end());
            for (int k = kSamples; k >= 0; --k)
                m_band.push_back(m_bottom[(size_t)k]);

            float mixT = Fract((float)r.ci / Palette::kColors + m_blend * 0.4f);
            Color c = pal.c[r.ci].Mix(pal.c[(r.ci + 2) % Palette::kColors], mixT * 0.5f);
            draw::FillPolygon(dc, brush, m_band.data(), (unsigned)m_band.size(),
                              c.WithAlpha(r.alpha * (0.9f + 0.9f * m_glow)));
            // Bright core thread.
            for (int k = 1; k <= kSamples; ++k) {
                float y0 = (m_top[(size_t)k - 1].y + m_bottom[(size_t)k - 1].y) * 0.5f;
                float y1 = (m_top[(size_t)k].y + m_bottom[(size_t)k].y) * 0.5f;
                draw::Line(dc, brush, m_top[(size_t)k - 1].x, y0, m_top[(size_t)k].x, y1,
                           1.5f, Color(1, 1, 1, r.alpha * 1.4f));
            }
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.38f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_ribbonsAmt = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_blend = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0x3A7E11 + ctx.variation * 7919u);
        m_ribbons.clear();
        int want = std::min(7 + (int)(m_ribbonsAmt * 5.0f), 12);
        m_ribbons.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Ribbon r{};
            float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            r.angle = sign * (28.0f + rng.Range(0.0f, 34.0f));
            r.yBase = rng.Range(0.12f, 0.88f);
            r.amp = rng.Range(24.0f, 70.0f);
            r.freq = rng.Range(0.5f, 1.5f);
            r.phase = rng.Range(0.0f, kTau);
            r.speedMul = rng.Range(0.5f, 1.4f);
            r.w0 = rng.Range(5.0f, 14.0f);
            r.w1 = rng.Range(34.0f, 90.0f);
            r.ci = i % Palette::kColors;
            r.alpha = rng.Range(0.26f, 0.46f);
            m_ribbons.push_back(r);
        }
        m_top.reserve(kSamples + 1);
        m_bottom.reserve(kSamples + 1);
        m_band.reserve((kSamples + 1) * 2);
    }

    std::vector<Ribbon> m_ribbons;
    std::vector<D2D1_POINT_2F> m_top, m_bottom, m_band;
    float m_time = 0;
    float m_ribbonsAmt = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_blend = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateWeave() { return new WeaveScene(); }

} // namespace lp::scenes
