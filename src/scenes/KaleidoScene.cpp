// LivePaper - "Kaleidoscope".
//
// A slowly turning mandala. A dozen drifting shapes live inside one wedge; every
// frame that wedge's content is drawn once per segment, plus a copy reflected
// about the wedge bisector (a' = wedge - a, which needs no mirroring transform).
// Additive blending does the rest.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Shape {
    float rad0;      // 0..1 of the mandala radius
    float ang0;      // 0..1 across the wedge
    float size;      // px at reference scale
    float hueOff;
    float wobA;      // angle wobble, fraction of the wedge
    float wobF;
    float radWob;
    float phase;
    int kind;        // 0 disc, 1 diamond, 2 ring
};

} // namespace

class KaleidoScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Kaleidoscope"; }
    const wchar_t* Description() const override {
        return L"A turning mandala of mirrored, drifting shapes.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Segments";
            case 1: return L"Speed";
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
        m_time += dt * (0.3f + 1.1f * m_speed);
        m_spin += dt * (0.02f + 0.06f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float R = std::min(w, h) * 0.46f;
        const int segs = m_segs;
        const float wedge = kTau / (float)segs;
        float baseHue = Fract(0.85f + (m_hue - 0.5f) * 0.6f + m_time * 0.008f);

        // Dark velvet backdrop with a whisper of the current hue.
        D2D1_GRADIENT_STOP bg[2];
        Color tint = Color::Hsv(baseHue, 0.5f, 0.050f);
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(tint.r, tint.g, tint.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.002f, 0.002f, 0.005f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        draw::RadialGlow(dc, brush, cx, cy, R * 0.9f,
                         Color::Hsv(baseHue, 0.4f, 1.0f, 0.04f + 0.04f * m_glow), 1.0f, 20);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (int s = 0; s < segs; ++s) {
            float segAngle = m_spin + (float)s * wedge;
            dc->SetTransform(D2D1::Matrix3x2F::Rotation(segAngle * 57.2958f,
                                                        D2D1::Point2F(cx, cy)));
            for (const auto& sh : m_shapes) {
                float a = (sh.ang0 + sh.wobA * std::sin(m_time * sh.wobF + sh.phase)) * wedge;
                float rad = sh.rad0 * R * (1.0f + sh.radWob * std::sin(m_time * 0.8f + sh.phase));
                float size = sh.size * std::min(w, h) / 1080.0f;
                Color c = Color::Hsv(Fract(baseHue + sh.hueOff), 0.60f, 1.0f,
                                     0.24f + 0.22f * m_glow);
                // The shape, then its mirror about the wedge bisector: a' = wedge - a.
                for (int mir = 0; mir < 2; ++mir) {
                    float aa = mir ? (wedge - a) : a;
                    float x = cx + std::cos(aa) * rad;
                    float y = cy + std::sin(aa) * rad;
                    if (sh.kind == 0) {
                        draw::Circle(dc, brush, x, y, size, c);
                    } else if (sh.kind == 1) {
                        D2D1_POINT_2F dia[4] = {
                            D2D1::Point2F(x, y - size * 1.3f),
                            D2D1::Point2F(x + size, y),
                            D2D1::Point2F(x, y + size * 1.3f),
                            D2D1::Point2F(x - size, y),
                        };
                        draw::FillPolygon(dc, brush, dia, 4, c);
                    } else {
                        brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
                        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), size, size),
                                        brush, size * 0.35f, nullptr);
                    }
                }
            }
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());

        // Eye of the mandala.
        draw::RadialGlow(dc, brush, cx, cy, std::min(w, h) * 0.05f,
                         Color::Hsv(Fract(baseHue + 0.5f), 0.30f, 1.0f,
                                    0.5f + 0.3f * m_glow), 1.0f, 10);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.42f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_segAmt = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0x6A1E20u + ctx.variation * 7919u);
        m_segs = 6 + 2 * (int)(m_segAmt * 2.4f);   // 6, 8, 10
        m_segs = std::min(m_segs, 10);

        m_shapes.clear();
        int want = 10 + rng.Int(0, 4);
        m_shapes.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Shape sh{};
            sh.rad0 = 0.16f + 0.80f * std::pow(rng.Unit(), 0.8f);
            sh.ang0 = 0.12f + 0.76f * rng.Unit();
            sh.size = 6.0f + 16.0f * rng.Unit();
            sh.hueOff = rng.Range(0.0f, 0.25f);
            sh.wobA = rng.Range(0.04f, 0.22f);
            sh.wobF = rng.Range(0.4f, 1.4f);
            sh.radWob = rng.Range(0.05f, 0.22f);
            sh.phase = rng.Range(0.0f, kTau);
            sh.kind = rng.Int(0, 2);
            m_shapes.push_back(sh);
        }
        // Guarantee something lives near the eye for a finished centre.
        m_shapes[0].rad0 = 0.14f;
    }

    std::vector<Shape> m_shapes;
    int m_segs = 8;
    float m_time = 0, m_spin = 0;
    float m_segAmt = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateKaleido() { return new KaleidoScene(); }

} // namespace lp::scenes
