// LivePaper - "Black Hole".
//
// An accretion disk of orbiting motes around an event horizon, drawn in three
// layers so the read is right: far-side disk motes behind the hole, the horizon
// itself as pure black, then the near-side disk crossing in front. A photon ring
// hugs the shadow and two shimmering arcs stand in for the lensed far side of
// the disk arcing over and under it. Approaching-side motes are Doppler-brightened.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Mote {
    float a;          // orbital angle
    float r;          // orbital radius, multiples of the horizon radius
    float size;
    float bright;
    float speedMul;
};

} // namespace

class BlackHoleScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Black Hole"; }
    const wchar_t* Description() const override {
        return L"An accretion disk swirling around a silent event horizon.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Disk";
            case 1: return L"Spin";
            case 2: return L"Glow";
            case 3: return L"Tint";
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

        // Keplerian-ish: inner motes sweep around much faster, and everything
        // creeps inward before being recycled at the outer edge.
        const float spin = 0.3f + 1.5f * m_spin;
        for (auto& m : m_motes) {
            m.a += dt * spin * 1.9f * std::pow(1.0f / m.r, 1.5f) * m.speedMul;
            m.r -= dt * 0.010f * spin / m.r;
            if (m.r < 1.14f) {
                m.r = m_rOuter * (0.85f + 0.15f * Fract(m.a * 0.159f));
                m.a = Fract(std::sin(m.a * 12.9898f + m_time) * 43758.5453f) * kTau;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float rh = std::min(w, h) * 0.11f;
        const float squash = 0.30f;
        const float hue = (m_tint < 0.5f) ? Lerp(0.58f, 0.065f, m_tint * 2.0f)
                                          : Lerp(0.065f, 0.11f, (m_tint - 0.5f) * 2.0f);

        // Deep space.
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.003f, 0.003f, 0.008f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.001f, 0.001f, 0.004f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        m_stars.Draw(dc, brush, m_time);

        const D2D1_POINT_2F origin = D2D1::Point2F(cx, cy);
        const float tiltDeg = -10.0f;

        // Pass A: far side of the disk (behind everything).
        dc->SetTransform(D2D1::Matrix3x2F::Rotation(tiltDeg, origin));
        DrawDiskHalf(dc, brush, origin, rh, squash, hue, false);
        dc->SetTransform(D2D1::Matrix3x2F::Identity());

        // Lensed far side: the disk's light folded over the top of the shadow
        // (and a fainter fold beneath), arched OUTSIDE the horizon silhouette.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (int k = 0; k < 2; ++k) {
            float shimmer = 0.7f + 0.3f * std::sin(m_time * (1.3f + 0.4f * k) + k * 2.0f);
            float arcR = rh * (k == 0 ? 1.42f : 1.18f);
            float lift = (k == 0) ? 1.0f : 0.42f;
            Color c = Color::Hsv(Fract(hue + 0.015f), 0.55f, 1.0f,
                                 (0.07f + 0.07f * m_glow) * shimmer * lift);
            for (int i = 0; i < 20; ++i) {
                float t = (float)i / 19.0f;
                float a = Lerp(0.20f, kPi - 0.20f, t);
                float x = cx + std::cos(a) * arcR;
                float y = cy - std::sin(a) * arcR * lift;
                // Constant radius (so neighbours overlap into a continuous
                // ribbon); the taper comes from the alpha envelope.
                float fade = std::sqrt(std::sin(t * kPi));
                draw::Circle(dc, brush, x, y, rh * 0.155f,
                             Color(c.r, c.g, c.b, c.a * fade));
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // A soft ring glow; then the shadow itself erases whatever spilled
        // inside the horizon, keeping it genuinely black.
        Color ringC = Color::Hsv(Fract(hue + 0.02f), 0.35f, 1.0f);
        draw::RadialGlow(dc, brush, cx, cy, rh * 1.30f,
                         ringC.WithAlpha(0.10f + 0.10f * m_glow), 1.0f, 16);
        draw::Circle(dc, brush, cx, cy, rh, Color(0, 0, 0, 1.0f));

        // Photon ring: a thin, fierce circle hugging the shadow.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        brush->SetColor(D2D1::ColorF(ringC.r, ringC.g, ringC.b, 0.55f + 0.40f * m_glow));
        dc->DrawEllipse(D2D1::Ellipse(origin, rh * 1.06f, rh * 1.06f), brush, 2.6f, nullptr);
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Pass B: near side of the disk, crossing in front of the shadow.
        dc->SetTransform(D2D1::Matrix3x2F::Rotation(tiltDeg, origin));
        DrawDiskHalf(dc, brush, origin, rh, squash, hue, true);
        dc->SetTransform(D2D1::Matrix3x2F::Identity());

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_disk = ctx.config->sceneParam[0];
        m_spin = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_tint = ctx.config->sceneParam[3];
    }

    // One continuous near-side orbit band: an elliptical arc from (r,0) through
    // the bottom (near) half to (-r,0), drawn under the current disk transform.
    void DrawNearBand(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                      const D2D1_POINT_2F& origin, float r, float ry,
                      float width, const Color& color) {
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(origin.x + r, origin.y), D2D1_FIGURE_BEGIN_HOLLOW);
            D2D1_ARC_SEGMENT seg{};
            seg.size = D2D1::SizeF(r, ry);
            seg.rotationAngle = 0.0f;
            seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
            seg.arcSize = D2D1_ARC_SIZE_SMALL;
            seg.point = D2D1::Point2F(origin.x, origin.y + ry);
            sink->AddArc(seg);
            seg.point = D2D1::Point2F(origin.x - r, origin.y);
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

    void DrawDiskHalf(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                      const D2D1_POINT_2F& origin, float rh, float squash,
                      float hue, bool nearSide) {
        const float scale = (0.35f + 0.85f * m_disk);   // brightness gate
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        // Continuous near-side orbit bands tie the motes together. Only the
        // near half is drawn: the far half passes behind the shadow.
        if (nearSide) {
            for (int b = 0; b < 3; ++b) {
                float r = rh * (1.35f + 1.05f * b);
                float boost = (b == 0) ? 1.6f : (b == 1) ? 1.1f : 0.8f;
                Color band = Color::Hsv(Fract(hue + 0.01f * b), 0.72f, 1.0f,
                                        (0.17f + 0.15f * m_glow) * scale * boost);
                DrawNearBand(dc, brush, origin, r, r * squash, rh * 0.13f, band);
            }
        }
        for (const auto& m : m_motes) {
            bool isNear = std::sin(m.a) >= 0.0f;
            if (isNear != nearSide) continue;
            // m.r is already in pixels (built as rh * fraction).
            float x = origin.x + std::cos(m.a) * m.r;
            float y = origin.y + std::sin(m.a) * m.r * squash;
            // Doppler brightening on the approaching side (cos(a) > 0).
            float doppler = 1.0f + 0.55f * std::cos(m.a);
            float heat = Clamp01(1.6f - m.r / (rh * 3.0f));   // inner matter is hotter
            Color c = Color::Hsv(Fract(hue - heat * 0.04f),
                                 Lerp(0.75f, 0.20f, heat), 1.0f,
                                 m.bright * doppler * scale);
            draw::Circle(dc, brush, x, y, m.size, c);
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    }

    void Build(const SceneCtx& ctx) {
        Rng rng(0xB1AC80u + ctx.variation * 7919u);
        const float rh = std::min(ctx.width, ctx.height) * 0.11f;
        m_rOuter = rh * 6.5f;

        m_motes.clear();
        int want = std::min((int)(420 * ctx.density * (0.55f + 0.9f * m_disk)), 640);
        m_motes.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Mote m{};
            m.r = rh * (1.18f + std::pow(rng.Unit(), 1.7f) * 5.2f);
            m.a = rng.Range(0.0f, kTau);
            m.size = Lerp(1.6f, 3.6f, Clamp01(2.0f - m.r / (rh * 4.0f))) * rng.Range(0.7f, 1.3f);
            m.bright = rng.Range(0.30f, 0.80f) * (1.8f - m.r / (rh * 6.5f));
            m.speedMul = rng.Range(0.75f, 1.25f);
            m_motes.push_back(m);
        }

        m_stars.Build(ctx.width, ctx.height, (int)(140 * ctx.density), 0xB1AC91u + ctx.variation);
    }

    std::vector<Mote> m_motes;
    draw::StarField m_stars;
    float m_rOuter = 400.0f;
    float m_time = 0;
    float m_disk = 0.5f, m_spin = 0.5f, m_glow = 0.5f, m_tint = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateBlackHole() { return new BlackHoleScene(); }

} // namespace lp::scenes
