// LivePaper - "Strata".
//
// Generative layered waves: a stack of translucent bands, each drawn as a noise +
// sine ridge filled down to the bottom, painted back to front so nearer bands
// overlap farther ones. Every band takes a color from the shared palette, so a
// new seed or a custom color re-tints the whole cliff face.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

constexpr int kSteps = 26;

struct Band {
    float y;         // base line, fraction of height
    float amp;       // fraction of height
    float freq;
    float phase;
    float speedMul;
    int ci;
    float alpha;
};

} // namespace

class StrataScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Strata"; }
    const wchar_t* Description() const override {
        return L"Layered waves of generative color, rolling slowly over each other.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Layers";
            case 1: return L"Speed";
            case 2: return L"Amplitude";
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
        Palette pal = MakePalette(ctx);
        float bh = 0, bs = 0, bv = 0;
        pal.c[0].ToHsv(bh, bs, bv);

        // Deep ground under everything.
        Color deep = Color::Hsv(bh, bs * 0.85f, 0.05f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(deep.r * 0.6f, deep.g * 0.6f, deep.b * 0.6f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(deep.r, deep.g, deep.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        // Bands, top (far) first; each fills down to the bottom of the screen.
        m_curve.resize((size_t)kSteps + 1);
        for (size_t i = 0; i < m_bands.size(); ++i) {
            const Band& b = m_bands[i];
            for (int k = 0; k <= kSteps; ++k) {
                float u = (float)k / kSteps;
                float x = u * w;
                float yc = b.y * h
                         + Noise1(b.phase + u * b.freq * 3.0f + m_time * 0.35f * b.speedMul) * b.amp * h * 0.6f
                         + std::sin(u * b.freq * kTau + m_time * b.speedMul * 0.9f + b.phase) * b.amp * h * 0.4f;
                m_curve[(size_t)k] = D2D1::Point2F(x, yc);
            }
            m_scratch.assign(m_curve.begin(), m_curve.end());
            m_scratch.push_back(D2D1::Point2F(w, h + 2.0f));
            m_scratch.push_back(D2D1::Point2F(0, h + 2.0f));
            // Blend nudges each band toward its neighbour's color.
            Color c = pal.c[b.ci].Mix(pal.c[(b.ci + 1) % Palette::kColors], m_blend * 0.45f);
            draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(),
                              c.WithAlpha(b.alpha));
            // Lit crest where the band catches the light from above.
            Color crest = c.Mix(Color(1, 1, 1, 0), 0.35f);
            for (int k = 1; k <= kSteps; ++k) {
                draw::Line(dc, brush, m_curve[(size_t)k - 1].x, m_curve[(size_t)k - 1].y,
                           m_curve[(size_t)k].x, m_curve[(size_t)k].y, 1.6f,
                           crest.WithAlpha(b.alpha * 0.45f));
            }
        }

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.34f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_layers = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_amp = ctx.config->sceneParam[2];
        m_blend = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0x572A7Au + ctx.variation * 7919u);
        m_bands.clear();
        int want = std::min(8 + (int)(m_layers * 5.0f), 13);
        m_bands.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            float t = (float)i / std::max(1, want - 1);
            Band b{};
            b.y = 0.16f + 0.84f * t + rng.Range(-0.02f, 0.02f);
            b.amp = (0.015f + 0.055f * m_amp) * rng.Range(0.7f, 1.3f);
            b.freq = rng.Range(0.5f, 1.6f);
            b.phase = rng.Range(0.0f, 80.0f);
            b.speedMul = rng.Range(0.5f, 1.4f);
            b.ci = i % Palette::kColors;
            b.alpha = Lerp(0.42f, 0.80f, t);   // nearer bands are more solid
            m_bands.push_back(b);
        }
        m_curve.reserve(kSteps + 1);
        m_scratch.reserve(kSteps + 3);
    }

    std::vector<Band> m_bands;
    std::vector<D2D1_POINT_2F> m_curve, m_scratch;
    float m_time = 0;
    float m_layers = 0.5f, m_speed = 0.5f, m_amp = 0.5f, m_blend = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateStrata() { return new StrataScene(); }

} // namespace lp::scenes
