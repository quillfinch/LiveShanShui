// LivePaper - "Lighthouse".
//
// A night sea under stars. A lighthouse on a rocky point sweeps two opposite
// beams across the sky; the lamp brightens as the main beam comes around. Low
// swells roll past the rocks as noise ridges filled to the bottom.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Swell {
    float baseY;
    float amp;
    float speed;
    float phase;
};

} // namespace

class LighthouseScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Lighthouse"; }
    const wchar_t* Description() const override {
        return L"A lighthouse beam sweeping the night sea from a rocky point.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Rotation";
            case 1: return L"Beam";
            case 2: return L"Waves";
            case 3: return L"Haze";
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
        m_beamAngle += dt * (0.25f + 0.85f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float horizon = h * 0.62f;

        // Night sky.
        D2D1_GRADIENT_STOP sky[2];
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(0.006f, 0.010f, 0.028f, 1);
        sky[1].position = 1.0f; sky[1].color = D2D1::ColorF(0.020f, 0.032f, 0.070f, 1);
        draw::VerticalGradient(dc, w, h, sky, 2);
        m_stars.Draw(dc, brush, m_time);

        // Moon, small and cold.
        draw::RadialGlow(dc, brush, w * 0.18f, h * 0.16f, h * 0.10f,
                         Color(0.75f, 0.82f, 1.0f, 0.16f), 1.0f, 10);
        draw::Circle(dc, brush, w * 0.18f, h * 0.16f, h * 0.018f, Color(0.88f, 0.92f, 1.0f, 0.9f));

        // Sea base below the horizon.
        D2D1_GRADIENT_STOP sea[2];
        sea[0].position = 0.0f; sea[0].color = D2D1::ColorF(0.010f, 0.026f, 0.052f, 1);
        sea[1].position = 1.0f; sea[1].color = D2D1::ColorF(0.003f, 0.009f, 0.022f, 1);
        {
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(sea, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
                props.startPoint = D2D1::Point2F(0, horizon);
                props.endPoint = D2D1::Point2F(0, h);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(props, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, horizon, w, h), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }

        // --- lighthouse position ------------------------------------------------
        const float lx = w * 0.76f;
        const float rockTop = horizon - h * 0.02f;
        const float lampY = rockTop - h * 0.16f;

        // Beams (drawn before the tower so the tower cuts them off at the base).
        // The beam brightens as it sweeps toward the viewer's side.
        float sweep = 0.55f + 0.45f * std::cos(m_beamAngle);
        float len = std::max(w, h) * (0.85f + 0.45f * m_beam);
        float spread = 0.055f;
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (int k = 0; k < 2; ++k) {
            float a = m_beamAngle + (float)k * kPi;
            float alpha = (k == 0 ? 0.15f : 0.08f) * (0.5f + 0.7f * m_beam) * (0.6f + 0.5f * sweep);
            // Two nested wedges give the beam a soft edge.
            for (int w2 = 0; w2 < 2; ++w2) {
                float sp = spread * (w2 == 0 ? 1.8f : 1.0f);
                float aa = alpha * (w2 == 0 ? 0.5f : 1.0f);
                D2D1_POINT_2F wedge[3] = {
                    D2D1::Point2F(lx, lampY),
                    D2D1::Point2F(lx + std::cos(a - sp) * len, lampY + std::sin(a - sp) * len),
                    D2D1::Point2F(lx + std::cos(a + sp) * len, lampY + std::sin(a + sp) * len),
                };
                draw::FillPolygon(dc, brush, wedge, 3, Color(1.0f, 0.95f, 0.78f, aa));
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Haze around the lamp.
        draw::RadialGlow(dc, brush, lx, lampY, h * (0.12f + 0.16f * m_haze),
                         Color(1.0f, 0.93f, 0.72f, 0.08f + 0.08f * m_haze), 1.0f, 20);

        // Swells: noise ridges rolling past, crest-lit where beams would catch.
        float windScroll = m_time;
        for (int i = 0; i < (int)m_swells.size(); ++i) {
            const Swell& sw = m_swells[i];
            m_scratch.clear();
            const int steps = 34;
            for (int s = 0; s <= steps; ++s) {
                float u = (float)s / steps;
                float y = sw.baseY +
                          Noise1(u * 3.0f + windScroll * sw.speed + sw.phase) * sw.amp;
                m_scratch.push_back(D2D1::Point2F(u * w, y));
            }
            float t = (float)i / std::max(1, (int)m_swells.size() - 1);
            Color water = Color::Hsv(0.58f, 0.55f, Lerp(0.13f, 0.05f, t));
            FillToBottom(dc, brush, m_scratch, water);
            Color crest = Color(0.75f, 0.85f, 0.95f, 0.09f + 0.05f * (1.0f - t));
            for (int s = 1; s <= steps; ++s) {
                draw::Line(dc, brush, m_scratch[s - 1].x, m_scratch[s - 1].y,
                           m_scratch[s].x, m_scratch[s].y, 1.2f, crest);
            }
        }

        // Rock the lighthouse stands on.
        D2D1_POINT_2F rock[6] = {
            D2D1::Point2F(lx - w * 0.100f, h),
            D2D1::Point2F(lx - w * 0.078f, rockTop + h * 0.05f),
            D2D1::Point2F(lx - w * 0.045f, rockTop),
            D2D1::Point2F(lx + w * 0.015f, rockTop + h * 0.012f),
            D2D1::Point2F(lx + w * 0.055f, rockTop + h * 0.06f),
            D2D1::Point2F(lx + w * 0.070f, h),
        };
        draw::FillPolygon(dc, brush, rock, 6, Color(0.006f, 0.008f, 0.012f, 1.0f));

        // Tower: a tapered dark silhouette with a lit gallery.
        float baseW = w * 0.030f, topW = baseW * 0.58f, towerH = lampY - rockTop;
        D2D1_POINT_2F tower[4] = {
            D2D1::Point2F(lx - baseW, rockTop),
            D2D1::Point2F(lx - topW, lampY),
            D2D1::Point2F(lx + topW, lampY),
            D2D1::Point2F(lx + baseW, rockTop),
        };
        draw::FillPolygon(dc, brush, tower, 4, Color(0.010f, 0.012f, 0.020f, 1.0f));
        // Gallery deck + lamp room.
        draw::RoundedRect(dc, brush, lx - topW * 1.7f, lampY - h * 0.012f,
                          topW * 3.4f, h * 0.010f, h * 0.004f, Color(0.02f, 0.024f, 0.036f, 1.0f));
        float lampPulse = 0.65f + 0.35f * sweep;
        draw::Circle(dc, brush, lx, lampY - h * 0.022f, h * 0.010f,
                     Color(1.0f, 0.96f, 0.80f, 0.95f * lampPulse));
        // Roof cap.
        D2D1_POINT_2F roof[3] = {
            D2D1::Point2F(lx - topW * 1.2f, lampY - h * 0.030f),
            D2D1::Point2F(lx + topW * 1.2f, lampY - h * 0.030f),
            D2D1::Point2F(lx, lampY - h * 0.048f),
        };
        draw::FillPolygon(dc, brush, roof, 3, Color(0.010f, 0.012f, 0.020f, 1.0f));

        // Moonlit sparkle trail on the water under the lamp side.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        Rng rng((uint32_t)(m_time * 47.0f) + 3u);
        for (int i = 0; i < 26; ++i) {
            float u = rng.Unit();
            float sy = horizon + (h - horizon) * u * u;
            float sx = lx + (rng.Range(-1.0f, 1.0f)) * w * 0.05f * u;
            draw::Line(dc, brush, sx, sy, sx + rng.Range(-6.0f, 6.0f), sy, 1.0f,
                       Color(0.9f, 0.9f, 1.0f, 0.05f * (1.0f - u)));
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.38f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_speed = ctx.config->sceneParam[0];
        m_beam = ctx.config->sceneParam[1];
        m_waves = ctx.config->sceneParam[2];
        m_haze = ctx.config->sceneParam[3];
    }

    void FillToBottom(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                      const std::vector<D2D1_POINT_2F>& pts, const Color& color) {
        if (pts.size() < 3) return;
        m_scratch2.assign(pts.begin(), pts.end());
        m_scratch2.push_back(D2D1::Point2F(pts.back().x, m_canvasH));
        m_scratch2.push_back(D2D1::Point2F(pts.front().x, m_canvasH));
        draw::FillPolygon(dc, brush, m_scratch2.data(), (unsigned)m_scratch2.size(), color);
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        m_canvasH = h;
        Rng rng(0x11071u + ctx.variation * 7919u);

        float horizon = h * 0.62f;
        m_swells.clear();
        int waves = 2 + (int)(m_waves * 2.9f);   // 2..4 ridges
        waves = std::min(waves, 4);
        m_swells.reserve((size_t)waves);
        for (int i = 0; i < waves; ++i) {
            Swell sw{};
            float t = (float)i / std::max(1, waves - 1);
            sw.baseY = horizon + (h - horizon) * Lerp(0.10f, 0.80f, t);
            sw.amp = h * Lerp(0.008f, 0.018f, t);
            sw.speed = Lerp(0.10f, 0.22f, t);
            sw.phase = rng.Range(0.0f, 80.0f);
            m_swells.push_back(sw);
        }

        m_stars.Build(w, h, (int)(120 * ctx.density), 0x11078u + ctx.variation);
        m_scratch.reserve(64);
        m_scratch2.reserve(64);
    }

    std::vector<Swell> m_swells;
    std::vector<D2D1_POINT_2F> m_scratch, m_scratch2;
    draw::StarField m_stars;
    float m_canvasH = 1080.0f;
    float m_time = 0, m_beamAngle = 0;
    float m_speed = 0.5f, m_beam = 0.5f, m_waves = 0.5f, m_haze = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateLighthouse() { return new LighthouseScene(); }

} // namespace lp::scenes
