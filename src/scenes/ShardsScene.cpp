// LivePaper - "Shards".
//
// Generative stained glass: seed-placed translucent triangles at several depths,
// each shimmering slowly around its own resting rotation. Depth drives draw
// order, size and alpha, so the field reads as a faceted volume. All colors come
// from the shared palette.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Shard {
    D2D1_POINT_2F v[3];
    float cx, cy;      // centroid
    int ci;
    float depth;       // 0 far .. 1 near
    float rotAmp;      // oscillation, degrees
    float rotF;
    float phase;
};

} // namespace

class ShardsScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Shards"; }
    const wchar_t* Description() const override {
        return L"Faceted generative glass, shimmering in slow rotation.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Density";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Contrast";
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

        // A quiet two-tone ground out of the palette's own hues.
        Color g0 = Color::Hsv(bh, bs * 0.7f, 0.030f);
        Color g1 = Color::Hsv(Fract(bh + 0.5f), bs * 0.5f, 0.055f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(g0.r, g0.g, g0.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(g1.r, g1.g, g1.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        // Facets, far to near; each breathes around its resting rotation.
        for (const auto& sh : m_shards) {
            float rot = sh.rotAmp * std::sin(m_time * sh.rotF + sh.phase);
            dc->SetTransform(D2D1::Matrix3x2F::Rotation(rot, D2D1::Point2F(sh.cx, sh.cy)));
            float shimmer = 0.62f + 0.38f * std::sin(m_time * 0.5f + sh.phase * 2.0f);
            float a = Lerp(0.9f, 0.4f, m_contrast)
                    * Lerp(0.35f, 0.95f, sh.depth) * shimmer;
            Color c = pal.c[sh.ci];
            draw::FillPolygon(dc, brush, sh.v, 3, c.WithAlpha(a));
            // One lit edge per facet, additive, so the lattice catches light.
            dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
            draw::Line(dc, brush, sh.v[0].x, sh.v[0].y, sh.v[1].x, sh.v[1].y,
                       1.4f, Color(1, 1, 1, 0.10f * a * (0.4f + m_glow)));
            dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
            dc->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.38f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_density = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_contrast = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        const float scale = std::min(w, h) / 1080.0f;
        Rng rng(0x57A8D5u + ctx.variation * 7919u);

        m_shards.clear();
        int want = std::min(26 + (int)(m_density * 38.0f), 64);
        m_shards.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Shard sh{};
            float depth = rng.Unit();
            sh.depth = depth;
            sh.cx = rng.Range(-0.04f, 1.04f) * w;
            sh.cy = rng.Range(-0.04f, 1.04f) * h;
            float size = Lerp(50.0f, 170.0f, depth) * scale * rng.Range(0.7f, 1.35f);
            float a0 = rng.Range(0.0f, kTau);
            for (int k = 0; k < 3; ++k) {
                float ang = a0 + kTau * (float)k / 3.0f + rng.Range(-0.5f, 0.5f);
                float rr = size * rng.Range(0.55f, 1.15f);
                sh.v[k] = D2D1::Point2F(sh.cx + std::cos(ang) * rr,
                                        sh.cy + std::sin(ang) * rr);
            }
            sh.ci = rng.Int(0, Palette::kColors - 1);
            sh.rotAmp = rng.Range(1.5f, 5.0f);
            sh.rotF = rng.Range(0.2f, 0.55f);
            sh.phase = rng.Range(0.0f, kTau);
            m_shards.push_back(sh);
        }
        // Far shards first so nearer ones paint over them.
        std::sort(m_shards.begin(), m_shards.end(),
                  [](const Shard& a, const Shard& b) { return a.depth < b.depth; });
    }

    std::vector<Shard> m_shards;
    float m_time = 0;
    float m_density = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_contrast = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateShards() { return new ShardsScene(); }

} // namespace lp::scenes
