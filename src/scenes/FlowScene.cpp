// LivePaper - "Flow Field".
//
// A few hundred particles ride a slowly evolving noise vector field, each keeping
// a short ring-buffer trail that is stroked as fading segments. The result reads
// as wind made visible: streams form, braid and dissolve. Hue follows the local
// flow direction, so currents are readable at a glance.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

constexpr int kTrail = 6;   // ring-buffer points per particle

struct P {
    float x, y;
    float trailX[kTrail];
    float trailY[kTrail];
    int head;
    int sample;
    float speedMul;
    float hueSeed;
};

} // namespace

class FlowScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Flow Field"; }
    const wchar_t* Description() const override {
        return L"Currents of light advected through an evolving vector field.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Density";
            case 1: return L"Speed";
            case 2: return L"Turbulence";
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
        m_time += dt * (0.4f + 1.0f * m_speed);

        const float w = ctx.width, h = ctx.height;
        const float sc = 0.0014f + 0.0022f * m_turb;
        const float v = (70.0f + 130.0f * m_speed);
        for (auto& p : m_parts) {
            // The field angle: two noise look-ups keep it curling without a curl
            // operator - the second octave slides the whole pattern over time.
            float a = Noise2(p.x * sc, p.y * sc + m_time * 0.10f) * kPi * 2.0f
                    + Noise2(p.x * sc * 2.3f + 40.0f, m_time * 0.06f) * 1.7f;
            p.x += std::cos(a) * v * p.speedMul * dt;
            p.y += std::sin(a) * v * p.speedMul * dt;
            bool wrapped = false;
            if (p.x < -20.0f) { p.x += w + 40.0f; wrapped = true; }
            if (p.x > w + 20.0f) { p.x -= w + 40.0f; wrapped = true; }
            if (p.y < -20.0f) { p.y += h + 40.0f; wrapped = true; }
            if (p.y > h + 20.0f) { p.y -= h + 40.0f; wrapped = true; }
            if (wrapped) {
                for (int k = 0; k < kTrail; ++k) { p.trailX[k] = p.x; p.trailY[k] = p.y; }
            }
            // Sample the trail every 3rd update so it spans a useful distance.
            p.sample = (p.sample + 1) % 3;
            if (p.sample == 0) {
                p.head = (p.head + 1) % kTrail;
                p.trailX[p.head] = p.x;
                p.trailY[p.head] = p.y;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        float baseHue = Fract(PaletteHue(ctx, 0.45f + (m_hue - 0.5f) * 0.7f));

        D2D1_GRADIENT_STOP bg[2];
        Color tint = Color::Hsv(baseHue, 0.5f, 0.030f);
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(tint.r, tint.g, tint.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.002f, 0.004f, 0.003f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& p : m_parts) {
            // Trail: oldest -> newest, alpha rising toward the head.
            for (int k = 0; k < kTrail - 1; ++k) {
                int i0 = (p.head + 1 + k) % kTrail;
                int i1 = (p.head + 2 + k) % kTrail;
                float t = (float)(k + 1) / (float)(kTrail - 1);
                float a = t * 0.55f;
                // Hue drifts with position, so currents carry colour.
                float hue = Fract(baseHue + p.hueSeed
                                  + Noise2(p.x * 0.001f, p.y * 0.001f + 7.0f) * 0.12f);
                Color c = Color::Hsv(hue, 0.60f, 1.0f, a);
                draw::Line(dc, brush, p.trailX[i0], p.trailY[i0],
                           p.trailX[i1], p.trailY[i1], 2.0f, c);
            }
            draw::Circle(dc, brush, p.x, p.y, 1.2f,
                         Color::Hsv(Fract(baseHue + p.hueSeed), 0.4f, 1.0f, 0.30f));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_density = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_turb = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0xF10A17u + ctx.variation * 7919u);
        m_parts.clear();
        int want = std::min((int)((240 + 240 * m_density) * ctx.density), 520);
        m_parts.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            P p{};
            p.x = rng.Range(0.0f, ctx.width);
            p.y = rng.Range(0.0f, ctx.height);
            for (int k = 0; k < kTrail; ++k) { p.trailX[k] = p.x; p.trailY[k] = p.y; }
            p.head = 0;
            p.sample = 0;
            p.speedMul = rng.Range(0.6f, 1.5f);
            p.hueSeed = rng.Range(-0.06f, 0.06f);
            m_parts.push_back(p);
        }
    }

    std::vector<P> m_parts;
    float m_time = 0;
    float m_density = 0.5f, m_speed = 0.5f, m_turb = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateFlow() { return new FlowScene(); }

} // namespace lp::scenes
