// LivePaper - "Bloom".
//
// Generative gradient blooms: large soft color fields that drift and breathe over
// a near-black ground. All colors come from MakePalette - a reshuffle or a custom
// color changes the entire mood. Blobs are concentric-ellipse stacks (no effects),
// blended additively so overlaps bloom.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Blob {
    float cx, cy;      // base centre, fraction of screen
    float r;           // radius, fraction of min dimension
    int ci;            // palette index
    float dax, day;    // drift amplitude, fraction of screen
    float fx, fy;      // drift frequencies
    float breatheF;
    float phase;
};

} // namespace

class BloomScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Bloom"; }
    const wchar_t* Description() const override {
        return L"Soft generative color blooms drifting in the dark.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Blooms";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Mix";
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
        const float minDim = std::min(w, h);
        Palette pal = MakePalette(ctx);
        float baseH = 0, baseS = 0, baseV = 0;
        pal.c[0].ToHsv(baseH, baseS, baseV);

        // Ground: the palette's base hue, nearly black, so the blooms sing.
        Color ground = Color::Hsv(baseH, baseS * 0.8f, 0.045f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(ground.r * 0.7f, ground.g * 0.7f, ground.b * 0.7f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.004f, 0.003f, 0.008f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& b : m_blobs) {
            float cx = (b.cx + std::sin(m_time * b.fx + b.phase) * b.dax) * w;
            float cy = (b.cy + std::cos(m_time * b.fy + b.phase * 1.37f) * b.day) * h;
            float rad = b.r * minDim * (1.0f + 0.14f * std::sin(m_time * b.breatheF + b.phase));
            // Mix blends each blob toward its palette neighbour.
            Color c = pal.c[b.ci].Mix(pal.c[(b.ci + 1) % Palette::kColors], m_mix * 0.6f);
            float a = (0.12f + 0.14f * m_glow);
            const int rings = 16;
            for (int i = rings; i >= 1; --i) {
                float t = (float)i / rings;
                float aa = a * (1.0f - t) * (1.0f - t);
                if (aa < 0.003f) continue;
                brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, aa));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy),
                                              rad * t, rad * t * 0.92f), brush);
            }
            // A small bright heart sells the bloom.
            brush->SetColor(D2D1::ColorF(1.0f, 0.98f, 0.94f, 0.10f + 0.10f * m_glow));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad * 0.10f, rad * 0.09f), brush);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.32f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_blooms = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_mix = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0xB1004Du + ctx.variation * 7919u);
        m_blobs.clear();
        int want = std::min(6 + (int)(m_blooms * 4.0f), 10);
        m_blobs.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Blob b{};
            b.cx = rng.Range(0.12f, 0.88f);
            b.cy = rng.Range(0.12f, 0.88f);
            b.r = rng.Range(0.17f, 0.33f);
            b.ci = i % Palette::kColors;
            b.dax = rng.Range(0.05f, 0.16f);
            b.day = rng.Range(0.05f, 0.16f);
            b.fx = rng.Range(0.10f, 0.30f);
            b.fy = rng.Range(0.08f, 0.26f);
            b.breatheF = rng.Range(0.25f, 0.60f);
            b.phase = rng.Range(0.0f, kTau);
            m_blobs.push_back(b);
        }
    }

    std::vector<Blob> m_blobs;
    float m_time = 0;
    float m_blooms = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_mix = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateBloom() { return new BloomScene(); }

} // namespace lp::scenes
