// LivePaper - "Satin Flow".
//
// Abstract silk: wide translucent ribbons undulating across the frame. Each ribbon
// is a filled band whose centre line is sine + noise and whose width breathes
// along its length; hues drift slowly, and a bright core line runs down each
// ribbon. Overlaps bloom additively.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Ribbon {
    float yBase;     // centre line, fraction of height
    float amp;       // undulation amplitude, fraction of height
    float freq;      // spatial frequency along x
    float phase;
    float speedMul;
    float width0;    // half-width at the thin end, px
    float width1;    // half-width at the wide end, px
    float hueOff;
    float alpha;
};

} // namespace

class SilkScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Satin Flow"; }
    const wchar_t* Description() const override {
        return L"Translucent silk ribbons undulating through soft light.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Speed";
            case 1: return L"Ribbons";
            case 2: return L"Glow";
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
        m_time += dt * (0.35f + 1.1f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        float baseHue = Fract(PaletteHue(ctx, 0.52f + (m_hue - 0.5f) * 0.8f) + m_time * 0.004f);

        // Backdrop: near-black tinted toward the current hue.
        D2D1_GRADIENT_STOP bg[2];
        Color tint = Color::Hsv(baseHue, 0.55f, 0.045f);
        Color tint2 = Color::Hsv(Fract(baseHue + 0.06f), 0.55f, 0.018f);
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(tint.r, tint.g, tint.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(tint2.r, tint2.g, tint2.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        draw::RadialGlow(dc, brush, w * (0.5f + 0.2f * std::sin(m_time * 0.05f)),
                         h * 0.4f, std::max(w, h) * 0.55f,
                         Color::Hsv(baseHue, 0.4f, 1.0f, 0.035f + 0.04f * m_glow), 1.0f, 22);

        // Ribbons, back (dim) to front.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (size_t ri = 0; ri < m_ribbons.size(); ++ri) {
            const Ribbon& r = m_ribbons[ri];
            m_top.clear();
            m_bottom.clear();
            for (int i = 0; i <= kCols; ++i) {
                float u = (float)i / kCols;
                float x = u * w;
                float yc = r.yBase * h
                         + std::sin(u * r.freq * kTau + m_time * r.speedMul + r.phase) * r.amp * h
                         + Noise1(u * 2.2f + m_time * 0.22f * r.speedMul + r.phase * 3.1f) * r.amp * h * 0.6f;
                // Width swells and narrows along the ribbon, like light on fabric.
                float halfW = Lerp(r.width0, r.width1,
                                   0.5f + 0.5f * std::sin(u * 5.2f + r.phase * 2.0f));
                m_top.push_back(D2D1::Point2F(x, yc - halfW));
                m_bottom.push_back(D2D1::Point2F(x, yc + halfW));
            }
            m_band.assign(m_top.begin(), m_top.end());
            for (int i = (int)m_bottom.size() - 1; i >= 0; --i)
                m_band.push_back(m_bottom[(size_t)i]);

            float hue = Fract(baseHue + r.hueOff + m_time * 0.006f);
            Color body = Color::Hsv(hue, 0.55f, 1.0f, r.alpha * (0.35f + 0.5f * m_glow));
            draw::FillPolygon(dc, brush, m_band.data(), (unsigned)m_band.size(), body);

            // Core line: the satin's bright thread.
            for (int i = 1; i <= kCols; ++i) {
                draw::Line(dc, brush, m_top[(size_t)i].x, (m_top[(size_t)i].y + m_bottom[(size_t)i].y) * 0.5f,
                           m_top[(size_t)i - 1].x, (m_top[(size_t)i - 1].y + m_bottom[(size_t)i - 1].y) * 0.5f,
                           1.4f, Color::Hsv(hue, 0.25f, 1.0f, r.alpha * 0.8f));
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.36f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_speed = ctx.config->sceneParam[0];
        m_ribbonAmt = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0x51A133u + ctx.variation * 7919u);

        m_ribbons.clear();
        int want = std::min((int)(5 + 6 * m_ribbonAmt), 11);
        m_ribbons.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Ribbon r{};
            float t = (float)i / std::max(1, want - 1);
            r.yBase = 0.14f + 0.72f * t + rng.Range(-0.04f, 0.04f);
            r.amp = rng.Range(0.05f, 0.14f);
            r.freq = rng.Range(0.6f, 1.6f);
            r.phase = rng.Range(0.0f, kTau);
            r.speedMul = rng.Range(0.5f, 1.4f);
            r.width0 = ctx.height * rng.Range(0.012f, 0.03f);
            r.width1 = ctx.height * rng.Range(0.05f, 0.12f);
            r.hueOff = (float)i * rng.Range(0.05f, 0.12f);
            r.alpha = rng.Range(0.08f, 0.16f) * (i < 2 ? 0.7f : 1.0f);
            m_ribbons.push_back(r);
        }

        m_top.reserve(kCols + 1);
        m_bottom.reserve(kCols + 1);
        m_band.reserve((kCols + 1) * 2);
    }

    static constexpr int kCols = 26;
    std::vector<Ribbon> m_ribbons;
    std::vector<D2D1_POINT_2F> m_top, m_bottom, m_band;
    float m_time = 0;
    float m_speed = 0.5f, m_ribbonAmt = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateSilk() { return new SilkScene(); }

} // namespace lp::scenes
