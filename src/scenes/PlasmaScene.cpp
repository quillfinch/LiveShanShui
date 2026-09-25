// LivePaper - "Plasma Ball".
//
// A tesla-style glass orb: a bright electrode core and a handful of jagged
// discharge filaments that snap to new endpoints on the glass, live for a moment,
// and fade. Filaments are midpoint-displaced polylines drawn in three additive
// passes (halo, body, core). Faint glass shading and a room-dark backdrop keep
// the orb sitting in space rather than on it.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Fil {
    std::vector<D2D1_POINT_2F> pts;   // reserved up-front; refilled in place
    float life, maxLife;
    float strength;
    float endA;
    float endR;
};

} // namespace

class PlasmaScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Plasma Ball"; }
    const wchar_t* Description() const override {
        return L"Electric filaments dancing inside a glass orb.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Filaments";
            case 1: return L"Activity";
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
        m_time += dt;

        // Living set: filaments fade out, then snap to a fresh path and endpoint.
        int want = std::min((int)(5 + 9 * m_fils * ctx.density), 16);
        for (size_t i = 0; i < m_fils_.size();) {
            m_fils_[i].life -= dt * (0.7f + 1.6f * m_activity);
            if (m_fils_[i].life <= 0.0f) {
                if ((int)m_fils_.size() > want) {
                    m_fils_[i] = m_fils_.back();
                    m_fils_.pop_back();
                    continue;
                }
                Regenerate(m_fils_[i], ctx, (int)i);
            }
            ++i;
        }
        while ((int)m_fils_.size() < want) {
            Fil f;
            f.pts.reserve(40);
            f.endA = (float)m_fils_.size() * 2.399963f;   // golden-angle spread
            m_fils_.push_back(f);
            Regenerate(m_fils_.back(), ctx, (int)m_fils_.size() - 1);
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.485f;
        const float gr = std::min(w, h) * 0.42f;
        const float hue = Fract(0.75f + (m_hue - 0.5f) * 0.5f);

        // Dark room.
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.004f, 0.009f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.001f, 0.001f, 0.003f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        draw::RadialGlow(dc, brush, cx, cy, gr * 1.7f,
                         Color::Hsv(hue, 0.5f, 1.0f, 0.05f + 0.05f * m_glow), 1.0f, 20);

        // Glass: a whisper of fill, a crisp rim, and two curved reflections.
        draw::Circle(dc, brush, cx, cy, gr, Color(0.75f, 0.80f, 0.95f, 0.025f));
        brush->SetColor(D2D1::ColorF(0.85f, 0.90f, 1.0f, 0.14f));
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), gr, gr), brush, 2.0f, nullptr);
        brush->SetColor(D2D1::ColorF(1, 1, 1, 0.07f));
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - gr * 0.30f, cy - gr * 0.38f),
                                      gr * 0.36f, gr * 0.18f), brush, 3.0f, nullptr);

        // Discharge filaments.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& f : m_fils_) {
            if (f.pts.size() < 2) continue;
            float t = Clamp01(f.life / f.maxLife);
            float env = std::sin(t * kPi);            // ramp in, ramp out
            float flick = 0.8f + 0.2f * std::sin(m_time * 47.0f + f.endA * 9.0f);
            float a = f.strength * env * flick;
            Color halo = Color::Hsv(hue, 0.60f, 1.0f, 0.09f * a);
            Color body = Color::Hsv(Fract(hue + 0.05f), 0.35f, 1.0f, 0.30f * a);
            Color core(1.0f, 0.97f, 1.0f, 0.85f * a);
            for (size_t i = 1; i < f.pts.size(); ++i) {
                draw::Line(dc, brush, f.pts[i - 1].x, f.pts[i - 1].y,
                           f.pts[i].x, f.pts[i].y, 9.0f * m_glow + 3.0f, halo);
                draw::Line(dc, brush, f.pts[i - 1].x, f.pts[i - 1].y,
                           f.pts[i].x, f.pts[i].y, 3.2f, body);
                draw::Line(dc, brush, f.pts[i - 1].x, f.pts[i - 1].y,
                           f.pts[i].x, f.pts[i].y, 1.3f, core);
            }
            // Kiss of light where the filament meets the glass.
            const D2D1_POINT_2F& e = f.pts.back();
            draw::RadialGlow(dc, brush, e.x, e.y, 14.0f,
                             Color::Hsv(hue, 0.35f, 1.0f, 0.5f * a), 1.0f, 6);
        }

        // Electrode: dark sphere wrapped in a fierce core glow.
        float breathe = 0.85f + 0.15f * std::sin(m_time * 1.3f);
        draw::RadialGlow(dc, brush, cx, cy, gr * 0.16f * breathe,
                         Color::Hsv(hue, 0.45f, 1.0f, 0.45f), 1.0f, 18);
        draw::RadialGlow(dc, brush, cx, cy, gr * 0.055f,
                         Color(1, 1, 1, 0.70f), 1.0f, 10);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        draw::Circle(dc, brush, cx, cy, gr * 0.030f, Color(0.05f, 0.04f, 0.07f, 1.0f));

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.45f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_fils = ctx.config->sceneParam[0];
        m_activity = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    // Midpoint displacement from the electrode to a fresh point on the glass.
    void Regenerate(Fil& f, const SceneCtx& ctx, int index) {
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.485f;
        const float gr = std::min(w, h) * 0.42f;
        // Per-filament seed: shared seeds made every bolt identical.
        Rng rng((uint32_t)(m_time * 733.0f) + (uint32_t)(index * 7919u) + 5u + ctx.variation);

        f.endA = rng.Range(0.0f, kTau);
        f.endR = gr * rng.Range(0.86f, 0.99f);
        float ex = cx + std::cos(f.endA) * f.endR;
        float ey = cy + std::sin(f.endA) * f.endR;
        float sx = cx, sy = cy - gr * 0.02f;

        f.pts.clear();
        f.pts.push_back(D2D1::Point2F(sx, sy));
        f.pts.push_back(D2D1::Point2F(ex, ey));
        for (int it = 0; it < 4; ++it) {
            const size_t n = f.pts.size();
            for (size_t i = 0; i + 1 < n; i += 2) {
                D2D1_POINT_2F a = f.pts[i], b = f.pts[i + 1];
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = std::sqrt(dx * dx + dy * dy) + 0.001f;
                float off = rng.Range(-1.0f, 1.0f) * len * 0.22f;
                D2D1_POINT_2F m{ (a.x + b.x) * 0.5f - dy / len * off,
                                 (a.y + b.y) * 0.5f + dx / len * off };
                f.pts.insert(f.pts.begin() + (long)(i + 1), m);
            }
        }
        f.maxLife = rng.Range(0.5f, 1.5f);
        f.life = f.maxLife;
        f.strength = rng.Range(0.45f, 1.0f);
    }

    void Build(const SceneCtx& ctx) {
        m_fils_.clear();
        m_time = Fract((float)(ctx.variation % 53) * 0.117f);
    }

    std::vector<Fil> m_fils_;
    float m_time = 0;
    float m_fils = 0.5f, m_activity = 0.5f, m_glow = 0.5f, m_hue = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreatePlasma() { return new PlasmaScene(); }

} // namespace lp::scenes
