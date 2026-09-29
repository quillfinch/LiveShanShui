// LivePaper - "Halos".
//
// Generative concentric rings around an off-centre glow. Some rings are solid,
// others carry dashed arcs that rotate at their own speeds and directions. Every
// ring pulls a color from the shared palette, so seeds and custom colors re-tint
// the whole target.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Ring {
    float r;         // radius, fraction of min dimension
    float width;     // stroke width, px at 1080p reference
    int ci;
    float speed;     // rad/s, signed
    int dashes;      // 0 = solid ring, otherwise arc count
    float dashFrac;  // fraction of each gap segment that glows
    float phase;
};

// One stroked elliptical arc from a0 to a1 around (cx,cy), radius r.
void DrawArcStroke(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   float cx, float cy, float r, float a0, float a1,
                   float width, const Color& color) {
    ID2D1Factory* factory = nullptr;
    dc->GetFactory(&factory);
    if (!factory) return;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink)) && sink) {
        sink->BeginFigure(D2D1::Point2F(cx + std::cos(a0) * r, cy + std::sin(a0) * r),
                          D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT seg{};
        seg.point = D2D1::Point2F(cx + std::cos(a1) * r, cy + std::sin(a1) * r);
        seg.size = D2D1::SizeF(r, r);
        seg.rotationAngle = 0.0f;
        seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        seg.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(seg);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
        dc->DrawGeometry(geo, brush, width, nullptr);
    }
    geo->Release();
    factory->Release();
}

} // namespace

class HalosScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Halos"; }
    const wchar_t* Description() const override {
        return L"Concentric rings and rotating arcs around a quiet glow.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Rings";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Spread";
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
        const float cx = w * 0.42f, cy = h * 0.46f;
        const float minDim = std::min(w, h);
        Palette pal = MakePalette(ctx);
        float bh = 0, bs = 0, bv = 0;
        pal.c[0].ToHsv(bh, bs, bv);

        Color ground = Color::Hsv(Fract(bh + 0.5f), bs * 0.5f, 0.030f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.004f, 0.009f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(ground.r, ground.g, ground.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        // Core glow, slightly off-centre so the composition breathes.
        draw::RadialGlow(dc, brush, cx, cy, minDim * 0.10f,
                         pal.c[0].WithAlpha(0.55f), 1.0f, 14);
        draw::RadialGlow(dc, brush, cx, cy, minDim * 0.035f,
                         Color(1, 1, 1, 0.70f), 1.0f, 8);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& r : m_rings) {
            float rad = r.r * minDim;
            float width = r.width * (minDim / 1080.0f);
            Color c = pal.c[r.ci];
            if (r.dashes <= 0) {
                // Solid ring, brightness slowly breathing.
                float breathe = 0.75f + 0.25f * std::sin(m_time * 0.5f + r.phase);
                brush->SetColor(D2D1::ColorF(c.r, c.g, c.b,
                                             (0.20f + 0.30f * m_glow) * breathe));
                dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad),
                                brush, width, nullptr);
            } else {
                // Dashed arcs rotating around the ring.
                float a0 = r.phase + m_time * r.speed;
                float span = kTau / (float)r.dashes * r.dashFrac;
                float fade = 0.55f + 0.45f * std::sin(m_time * 0.7f + r.phase * 3.0f);
                for (int d = 0; d < r.dashes; ++d) {
                    float s = a0 + kTau * (float)d / (float)r.dashes;
                    DrawArcStroke(dc, brush, cx, cy, rad, s, s + span, width,
                                  c.WithAlpha((0.22f + 0.34f * m_glow) * fade));
                }
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_ringsAmt = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_spread = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0xA10C55u + ctx.variation * 7919u);
        m_rings.clear();
        int want = std::min(7 + (int)(m_ringsAmt * 6.0f), 13);
        m_rings.reserve((size_t)want);
        float spread = Lerp(0.34f, 0.62f, m_spread);
        for (int i = 0; i < want; ++i) {
            float t = (float)i / std::max(1, want - 1);
            Ring r{};
            r.r = 0.085f + spread * std::pow(t, 0.85f);
            r.width = Lerp(5.0f, 1.5f, t);
            r.ci = i % Palette::kColors;
            r.speed = (i % 2 == 0 ? 1.0f : -1.0f) * Lerp(0.05f, 0.28f, rng.Unit()) *
                      (0.4f + 1.2f * m_speed);
            r.dashes = (i % 3 == 1) ? 3 + rng.Int(0, 4) : 0;
            r.dashFrac = rng.Range(0.40f, 0.62f);
            r.phase = rng.Range(0.0f, kTau);
            m_rings.push_back(r);
        }
    }

    std::vector<Ring> m_rings;
    float m_time = 0;
    float m_ringsAmt = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_spread = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateHalos() { return new HalosScene(); }

} // namespace lp::scenes
