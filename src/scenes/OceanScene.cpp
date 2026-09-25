// LivePaper - "Deep Ocean".
//
// Looking up from below the thermocline: god rays sway down from the surface,
// bubbles rise with a wobble, and a school of fish drifts across as silhouettes.
// Everything is additive-friendly dark teal so the rays bloom without effects.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Ray {
    float xTop;        // fraction of width where the ray enters at the surface
    float sway;        // horizontal sway amplitude (pixels)
    float swayFreq;
    float phase;
    float width;       // stroke width (pixels)
    float alpha;
};

struct Bubble {
    float x, y;
    float r;
    float rise;        // px/s
    float wobbleAmp;
    float wobbleFreq;
    float phase;
    float bright;      // per-bubble visibility (stream membership)
};

struct Mote {
    float x, y;
    float r;
    float fall;        // px/s drift downward as marine snow settles
    float phase;
    float bright;
};

struct Fish {
    float x, y;
    float size;
    float dir;         // +1 right, -1 left
    float speed;
    float bobAmp, bobFreq, phase;
    float depth;       // 0 near .. 1 far (drives alpha and size)
};

} // namespace

class OceanScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Deep Ocean"; }
    const wchar_t* Description() const override {
        return L"God rays, bubbles and drifting fish in deep water.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Rays";
            case 1: return L"Drift";
            case 2: return L"Bubbles";
            case 3: return L"Depth";
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
        m_time += dt * (0.35f + 0.9f * m_drift);

        const float speedScale = 0.4f + 1.1f * m_drift;
        for (auto& b : m_bubbles) {
            b.y -= b.rise * speedScale * dt;
            b.phase += dt * b.wobbleFreq;
            if (b.y < -20.0f) {
                b.y = ctx.height + 20.0f;
                b.x = Fract(b.x * 0.017f + b.phase * 0.01f) * ctx.width;
            }
        }
        for (auto& f : m_fish) {
            f.x += f.dir * f.speed * speedScale * dt * 60.0f;
            f.phase += dt * f.bobFreq;
            f.y += std::sin(f.phase) * f.bobAmp * dt;
            if (f.dir > 0 && f.x > ctx.width + 120.0f) f.x = -120.0f;
            if (f.dir < 0 && f.x < -120.0f) f.x = ctx.width + 120.0f;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Water column: bright near the surface, inky at the bottom.
        D2D1_GRADIENT_STOP water[4];
        float hue = 0.52f + m_depth * 0.10f;   // blue .. teal
        Color topC = Color::Hsv(hue, 0.62f, 0.30f);
        Color midC = Color::Hsv(hue + 0.03f, 0.72f, 0.14f);
        Color deepC = Color::Hsv(hue + 0.05f, 0.78f, 0.045f);
        water[0].position = 0.00f; water[0].color = D2D1::ColorF(topC.r, topC.g, topC.b, 1);
        water[1].position = 0.42f; water[1].color = D2D1::ColorF(midC.r, midC.g, midC.b, 1);
        water[2].position = 1.00f; water[2].color = D2D1::ColorF(deepC.r, deepC.g, deepC.b, 1);
        water[3] = water[2];
        draw::VerticalGradient(dc, w, h, water, 3);

        // Surface shimmer band.
        Color shim = Color::Hsv(hue - 0.02f, 0.35f, 1.0f, 0.16f + 0.10f * m_raysAmt);
        draw::RadialGlow(dc, brush, w * 0.5f, -h * 0.05f, w * 0.75f, shim, 1.0f, 10);

        // --- god rays ---------------------------------------------------------
        // Wide, faint strokes fanning down from above; the sway keeps them alive.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& r : m_rays) {
            float sway = std::sin(m_time * r.swayFreq + r.phase) * r.sway;
            float x0 = r.xTop * w + sway * 0.25f;
            float x1 = r.xTop * w + r.sway + sway;
            float amp = 0.55f + 0.65f * m_raysAmt;
            Color c = Color::Hsv(hue - 0.02f, 0.18f, 1.0f, r.alpha * amp);
            // A wide faint halo stroke under each core keeps the shaft readable
            // even where it crosses the darker water.
            draw::Line(dc, brush, x0, -h * 0.05f, x1, h * 1.02f, r.width * 2.1f,
                       Color::Hsv(hue - 0.02f, 0.18f, 1.0f, r.alpha * amp * 0.35f));
            draw::Line(dc, brush, x0, -h * 0.05f, x1, h * 1.02f, r.width, c);
        }

        // --- fish (silhouettes, far layer first) -------------------------------
        for (int pass = 1; pass >= 0; --pass) {
            for (const auto& f : m_fish) {
                bool farLayer = f.depth > 0.5f;
                if ((pass == 1) != farLayer) continue;
                float a = farLayer ? 0.55f : 0.80f;
                float s = f.size;
                float bob = std::sin(f.phase) * s * 0.10f;
                // Body: a slender ellipse; tail: a thin triangle stroke behind it.
                // Silhouettes darker than the water plus a faint backlit rim, so
                // the school reads at a glance against the bright surface.
                Color body = Color::Hsv(hue + 0.04f, 0.55f, 0.035f, a);
                Color rim = Color::Hsv(hue - 0.02f, 0.30f, 0.85f, a * 0.30f);
                brush->SetColor(D2D1::ColorF(rim.r, rim.g, rim.b, rim.a));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(f.x, f.y + bob), s * 0.58f, s * 0.18f), brush);
                brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, body.a));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(f.x, f.y + bob), s * 0.52f, s * 0.14f), brush);
                float tailX = f.x - f.dir * s * 0.62f;
                draw::Line(dc, brush, f.x - f.dir * s * 0.28f, f.y + bob,
                           tailX, f.y + bob - s * 0.16f, s * 0.08f, body);
                draw::Line(dc, brush, f.x - f.dir * s * 0.28f, f.y + bob,
                           tailX, f.y + bob + s * 0.16f, s * 0.08f, body);
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // --- bubbles -----------------------------------------------------------
        for (const auto& b : m_bubbles) {
            float x = b.x + std::sin(b.phase) * b.wobbleAmp;
            float a = (0.16f + 0.22f * m_bubblesAmt) * b.bright;
            draw::Circle(dc, brush, x, b.y, b.r, Color(0.72f, 0.90f, 1.0f, a));
            draw::Circle(dc, brush, x, b.y, b.r * 0.78f, Color(0.55f, 0.75f, 0.95f, a * 0.5f));
            // A highlight sells the sphere.
            draw::Circle(dc, brush, x - b.r * 0.35f, b.y - b.r * 0.35f, b.r * 0.24f,
                         Color(1, 1, 1, std::min(1.0f, a * 1.6f)));
        }

        // Marine snow: faint drifting detritus that keeps the deep water alive.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& m : m_motes) {
            float mx = m.x + std::sin(m_time * 0.35f + m.phase) * 6.0f;
            float my = m.y - std::fmod(m_time * m.fall, ctx.height + 40.0f);
            if (my < -20.0f) my += ctx.height + 40.0f;
            draw::Circle(dc, brush, mx, my, m.r, Color(0.7f, 0.85f, 0.9f, 0.05f + 0.06f * m.bright));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Depth vignette so icons stay readable at the edges.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.42f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_raysAmt = ctx.config->sceneParam[0];
        m_drift = ctx.config->sceneParam[1];
        m_bubblesAmt = ctx.config->sceneParam[2];
        m_depth = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x0CEA11u + ctx.variation * 7919u);

        m_rays.clear();
        int rayCount = 5 + (int)(m_raysAmt * 5.0f);
        for (int i = 0; i < rayCount; ++i) {
            Ray r{};
            r.xTop = rng.Range(0.02f, 0.98f);
            r.sway = rng.Range(-0.22f, 0.22f) * w;
            r.swayFreq = rng.Range(0.05f, 0.16f);
            r.phase = rng.Range(0.0f, kTau);
            r.width = rng.Range(0.03f, 0.10f) * w;
            r.alpha = rng.Range(0.020f, 0.055f);
            m_rays.push_back(r);
        }

        m_bubbles.clear();
        int bubbleCount = (int)((14 + 46 * m_bubblesAmt) * ctx.density);
        bubbleCount = std::min(bubbleCount, 90);
        m_bubbles.reserve((size_t)bubbleCount);
        // Bubbles are grouped into rising streams (a vent, a fish, decay) rather
        // than a uniform sprinkle - clusters read as "somewhere under water".
        int streams = std::max(3, bubbleCount / 12);
        for (int i = 0; i < bubbleCount; ++i) {
            Bubble b{};
            int stream = i % streams;
            float sx = Fract(0.13f + 0.71f * Fract((float)stream * 0.618f)) * w;
            b.x = sx + rng.Range(-w * 0.02f, w * 0.02f);
            b.y = rng.Range(0.0f, h);
            b.r = rng.Range(1.5f, 6.5f);
            b.rise = 14.0f + b.r * 5.5f;
            b.wobbleAmp = rng.Range(2.0f, 12.0f);
            b.wobbleFreq = rng.Range(0.6f, 1.8f);
            b.phase = rng.Range(0.0f, kTau);
            b.bright = rng.Range(0.55f, 1.15f);
            m_bubbles.push_back(b);
        }

        m_motes.clear();
        int moteCount = (int)(70 * ctx.density);
        m_motes.reserve((size_t)moteCount);
        for (int i = 0; i < moteCount; ++i) {
            Mote m{};
            m.x = rng.Range(0.0f, w);
            m.y = rng.Range(0.0f, h);
            m.r = rng.Range(0.7f, 2.0f);
            m.fall = rng.Range(2.0f, 9.0f);
            m.phase = rng.Range(0.0f, kTau);
            m.bright = rng.Range(0.3f, 1.0f);
            m_motes.push_back(m);
        }

        m_fish.clear();
        int schools = 3;
        for (int s = 0; s < schools; ++s) {
            bool farLayer = s == 0;
            int n = farLayer ? 14 : 9;
            float schoolY = h * rng.Range(0.30f, 0.70f);
            float schoolX = rng.Range(0.0f, w);
            float dir = (s % 2 == 0) ? 1.0f : -1.0f;
            for (int i = 0; i < n; ++i) {
                Fish f{};
                f.depth = farLayer ? 0.8f : 0.2f;
                f.size = farLayer ? rng.Range(9.0f, 14.0f) : rng.Range(16.0f, 26.0f);
                f.x = schoolX + rng.Range(-w * 0.22f, w * 0.22f);
                f.y = schoolY + rng.Range(-h * 0.06f, h * 0.06f);
                f.dir = dir;
                f.speed = rng.Range(0.5f, 1.1f);
                f.bobAmp = rng.Range(4.0f, 12.0f);
                f.bobFreq = rng.Range(0.8f, 2.0f);
                f.phase = rng.Range(0.0f, kTau);
                m_fish.push_back(f);
            }
        }
    }

    std::vector<Ray> m_rays;
    std::vector<Bubble> m_bubbles;
    std::vector<Fish> m_fish;
    std::vector<Mote> m_motes;
    float m_time = 0;
    float m_raysAmt = 0.5f, m_drift = 0.5f, m_bubblesAmt = 0.5f, m_depth = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateOcean() { return new OceanScene(); }

} // namespace lp::scenes
