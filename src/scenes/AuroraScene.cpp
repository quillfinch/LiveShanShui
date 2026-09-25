// LivePaper - "Aurora".
//
// A night sky over a still lake: northern lights as animated vertical bands, a dense
// twinkling star field, a faint milky-way haze, and a mirrored reflection. Aurora
// bands are drawn with a small number of soft strokes whose vertical extent is driven
// by layered noise, which is far cheaper than a real volumetric effect.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>


namespace lp::scenes {

namespace {

struct Band {
    float baseX;        // horizontal centre
    float width;
    float height;       // vertical reach as a fraction of the sky
    float speed;
    float phase;
    float hue;          // 0 = green, 1 = violet
    float alpha;
    int   strands;
};

struct Star { float x, y, r, phase, speed, warmth; };

} // namespace

class AuroraScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Aurora"; }
    const wchar_t* Description() const override {
        return L"Northern lights over a still lake.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Curtains";
            case 1: return L"Motion";
            case 2: return L"Brightness";
            case 3: return L"Hue";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        m_horizonY = ctx.height * 0.60f;
        BuildStars(ctx);
        BuildBands(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);
        m_time += dt * (0.4f + m_motion * 1.6f);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // --- night sky --------------------------------------------------------
        D2D1_GRADIENT_STOP sky[4];
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(0.016f, 0.020f, 0.055f, 1);
        sky[1].position = 0.40f; sky[1].color = D2D1::ColorF(0.024f, 0.035f, 0.084f, 1);
        sky[2].position = 0.72f; sky[2].color = D2D1::ColorF(0.035f, 0.055f, 0.105f, 1);
        sky[3].position = 1.00f; sky[3].color = D2D1::ColorF(0.020f, 0.030f, 0.062f, 1);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // --- milky-way haze: a diagonal soft band -----------------------------
        for (int i = 0; i < 26; ++i) {
            float t = (float)i / 25.0f;
            float x = w * (0.10f + t * 0.85f);
            float y = h * (0.70f - t * 0.52f);
            draw::RadialGlow(dc, brush, x, y, h * 0.20f,
                             Color::Hsv(0.68f, 0.20f, 0.85f, 0.045f), 1.0f, 8);
        }

        // --- stars ------------------------------------------------------------
        for (const auto& s : m_stars) {
            float tw = 0.45f + 0.55f * std::sin(m_time * s.speed + s.phase);
            float a = 0.25f + 0.65f * tw;
            Color c = s.warmth > 0.7f ? Color(1.0f, 0.92f, 0.82f, a)
                                      : (s.warmth < 0.3f ? Color(0.82f, 0.88f, 1.0f, a)
                                                         : Color(1, 1, 1, a));
            draw::Circle(dc, brush, s.x, s.y, s.r, c);
            if (s.r > 1.5f) draw::RadialGlow(dc, brush, s.x, s.y, s.r * 6.0f, c, 0.35f, 5);
        }

        // --- aurora curtains --------------------------------------------------
        // Drawn twice: once above the horizon and once as a dim mirrored reflection.
        DrawAurora(dc, brush, w, h, 0.0f, 1.0f);
        DrawAurora(dc, brush, w, h, m_horizonY, 0.42f);

        // --- horizon glow where the lights meet the lake ----------------------
        for (int i = 0; i < 30; ++i) {
            float t = (float)i / 29.0f;
            float x = w * t;
            float a = 0.05f * (0.5f + 0.5f * std::sin(m_time * 0.8f + t * 6.0f));
            draw::RadialGlow(dc, brush, x, m_horizonY, h * 0.09f,
                             Color::Hsv(0.40f + m_hue * 0.25f, 0.45f, 1.0f, a * (0.5f + m_brightness)), 1.0f, 6);
        }

        // --- lake surface ------------------------------------------------------
        {
            D2D1_GRADIENT_STOP lake[3];
            lake[0].position = 0.0f; lake[0].color = D2D1::ColorF(0.020f, 0.035f, 0.060f, 1);
            lake[1].position = 0.5f; lake[1].color = D2D1::ColorF(0.012f, 0.022f, 0.040f, 1);
            lake[2].position = 1.0f; lake[2].color = D2D1::ColorF(0.006f, 0.012f, 0.024f, 1);
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(lake, 3, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                gp.startPoint = D2D1::Point2F(0, m_horizonY);
                gp.endPoint = D2D1::Point2F(0, h);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, m_horizonY, w, h), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }

        // Ripples: thin horizontal highlights that break up the reflection.
        for (int i = 0; i < 60; ++i) {
            float t = (float)i / 59.0f;
            float y = m_horizonY + t * (h - m_horizonY);
            float x = std::fmod(t * w * 1.7f + std::sin(m_time * 0.5f + t * 8.0f) * 40.0f, w);
            float len = 30.0f + t * 120.0f;
            float a = (1.0f - t) * 0.06f * (0.5f + 0.5f * std::sin(m_time * 1.5f + t * 20.0f));
            draw::RoundedRect(dc, brush, x - len * 0.5f, y, len, 1.6f, 0.8f,
                              Color(0.75f, 0.95f, 0.85f, std::max(0.0f, a)));
        }

        // --- tree line silhouette at the horizon -------------------------------
        {
            Color dark = Color(0.008f, 0.016f, 0.026f, 1.0f);
            Rng rng(6060u + ctx.variation * 7919u);
            float x = -20.0f;
            while (x < w + 20.0f) {
                float tw = rng.Range(5.0f, 13.0f);
                float th = rng.Range(h * 0.008f, h * 0.034f);
                // Conifers as tapering spikes. A circle at the top of every tree made
                // the treeline read as a row of identical lollipops.
                int layers = 3;
                for (int l = 0; l < layers; ++l) {
                    float lt = (float)l / (float)(layers - 1);
                    float ly = m_horizonY - th * lt;
                    float lw = tw * (0.55f - lt * 0.30f);
                    draw::Line(dc, brush, x - lw, ly, x, m_horizonY - th * (lt + 0.42f),
                               std::max(1.0f, tw * 0.16f), dark);
                    draw::Line(dc, brush, x + lw, ly, x, m_horizonY - th * (lt + 0.42f),
                               std::max(1.0f, tw * 0.16f), dark);
                }
                draw::Line(dc, brush, x, m_horizonY + 2.0f, x, m_horizonY - th,
                           std::max(1.0f, tw * 0.14f), dark);
                x += tw * rng.Range(0.55f, 1.05f);
            }
        }

        // --- vignette ----------------------------------------------------------
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.30f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_curtains = ctx.config->sceneParam[0];
        m_motion = ctx.config->sceneParam[1];
        m_brightness = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void BuildStars(const SceneCtx& ctx) {
        m_stars.clear();
        int want = (int)(160 + 320 * (ctx.width * ctx.height) / (1920.0f * 1080.0f));
        Rng rng(987654u + ctx.variation * 7919u);
        m_stars.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Star s{};
            s.x = rng.Range(0.0f, ctx.width);
            s.y = rng.Range(0.0f, m_horizonY * 1.05f);
            s.r = rng.Range(0.5f, 2.1f);
            s.phase = rng.Range(0.0f, kTau);
            s.speed = rng.Range(0.3f, 2.2f);
            s.warmth = rng.Unit();
            m_stars.push_back(s);
        }
    }

    void BuildBands(const SceneCtx& ctx) {
        m_bands.clear();
        Rng rng(1357u + ctx.variation * 7919u);
        int count = (int)(3 + 7 * m_curtains);
        count = std::min(count, 14);
        for (int i = 0; i < count; ++i) {
            Band b{};
            b.baseX = rng.Range(-ctx.width * 0.10f, ctx.width * 1.10f);
            // Wide, strongly varied bands: uniform widths are what make a set of
            // curtains look like repeated objects rather than one sky.
            b.width = ctx.width * rng.Range(0.12f, 0.42f);
            b.height = rng.Range(0.45f, 0.95f);
            b.speed = rng.Range(0.4f, 1.3f);
            b.phase = rng.Range(0.0f, kTau);
            b.hue = rng.Unit();
            b.alpha = rng.Range(0.22f, 0.55f);
            b.strands = rng.Int(3, 7);
            m_bands.push_back(b);
        }
    }

    // `yBase`/`scale` let the same routine render the sky curtains and the dimmer
    // mirrored reflection below the horizon.
    void DrawAurora(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                    float w, float h, float yBase, float scale) {
        (void)yBase;
        const bool reflection = (scale < 1.0f);
        // Above the horizon the curtain rises from the horizon line; the reflection
        // hangs below it and fades out with depth.
        const float bottom = reflection ? h * 0.86f : m_horizonY;

        for (const auto& b : m_bands) {
            float hue = 0.34f + b.hue * 0.30f + m_hue * 0.12f;   // green -> violet
            float sway = std::sin(m_time * b.speed * 0.5f + b.phase) * w * 0.035f;
            float cx = b.baseX + sway;
            float reach = (m_horizonY - h * 0.02f) * b.height;
            float a = b.alpha * (0.55f + 0.65f * m_brightness) * scale
                      * (0.7f + 0.3f * std::sin(m_time * b.speed * 0.8f + b.phase * 2.0f));
            if (a < 0.004f) continue;

            float halfW = b.width * 0.5f;
            const int samples = 36;

            // Height of the curtain at normalised offset u (-1..1 across the band).
            // sin() shaped and raised to a gentle power: a 1D dome whose top edge is
            // continuous. A linear/cosine cutoff produced angular crystalline shards.
            auto curtainTop = [&](float u) {
                // Peaks at the band centre (u == 0) and tapers smoothly to nothing at
                // the edges. `1 - |u|` rises toward the middle; using it raised to a
                // power keeps the flanks soft instead of making a triangle.
                float t01 = 1.0f - std::fabs(u);
                float falloff = std::pow(t01, 0.85f);
                // Two noise octaves at different scales give a soft, folded edge.
                float n = Fbm(u * 2.2f + b.phase, m_time * 0.20f * b.speed, 3) * 0.6f
                        + Fbm(u * 5.5f + b.phase * 2.0f, m_time * 0.33f * b.speed, 2) * 0.4f;
                float height = reach * (0.62f + 0.38f * n) * falloff;
                if (height < 0) height = 0;
                if (reflection) {
                    // Mirror about the horizon and squeeze vertically.
                    return m_horizonY + height * 0.32f;
                }
                return m_horizonY - height;
            };

            // --- the curtain body: one filled shape with a soft, wavy top edge ----
            ID2D1Factory* factory = nullptr;
            dc->GetFactory(&factory);
            if (factory) {
                ID2D1PathGeometry* geo = nullptr;
                if (SUCCEEDED(factory->CreatePathGeometry(&geo)) && geo) {
                    ID2D1GeometrySink* sink = nullptr;
                    if (SUCCEEDED(geo->Open(&sink)) && sink) {
                        sink->BeginFigure(D2D1::Point2F(cx - halfW, bottom), D2D1_FIGURE_BEGIN_FILLED);
                        for (int i = 0; i <= samples; ++i) {
                            float u = (float)i / (float)samples * 2.0f - 1.0f;
                            sink->AddLine(D2D1::Point2F(cx + u * halfW, curtainTop(u)));
                        }
                        sink->AddLine(D2D1::Point2F(cx + halfW, bottom));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close();
                        sink->Release();
                        // The curtain body must be *brighter* than the night sky.
                        // Mixing toward white rather than just raising alpha keeps the
                        // bands luminous instead of turning them into dark silhouettes.
                        Color base = Color::Hsv(hue, 0.62f, 1.0f);
                        Color body = base.Mix(Color(1, 1, 1, 1), 0.35f);
                        body.a = a * 0.80f;
                        brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, body.a));
                        dc->FillGeometry(geo, brush, nullptr);
                    }
                    geo->Release();
                }
                factory->Release();
            }

            // --- vertical striations give the curtain its characteristic texture ---
            for (int s = 0; s < b.strands; ++s) {
                float st = (float)s / (float)std::max(1, b.strands - 1);
                float u = (st - 0.5f) * 2.0f;
                float topY = curtainTop(u);
                float sx = cx + u * halfW;
                // Individual strands are shorter than the envelope, which produces the
                // ragged lower tips real aurora curtains have.
                float strandTop = Lerp(topY, bottom, reflection ? 0.06f : 0.16f * Fbm(st * 5.0f, m_time * 0.3f, 2) + 0.04f);
                Color strandBase = Color::Hsv(hue, 0.60f, 1.0f).Mix(Color(1, 1, 1, 1), 0.35f);
                Color c = strandBase;
                c.a = a * (reflection ? 0.45f : 0.70f);
                float thickness = reflection ? 4.0f : 6.0f;
                draw::SoftCurve(dc, brush, sx, bottom, sx + sway * 0.25f,
                                (strandTop + bottom) * 0.5f, sx, strandTop,
                                thickness, c, reflection ? 0.4f : 0.8f);
                if (!reflection) {
                    // A brighter, narrower core strand sells the glow.
                    Color core = Color(1, 1, 1, a * 0.40f);
                    draw::SoftCurve(dc, brush, sx, bottom, sx + sway * 0.25f,
                                    (strandTop + bottom) * 0.5f, sx, strandTop,
                                    2.2f, core, 0.30f);
                }
            }
        }
    }

    std::vector<Band> m_bands;
    std::vector<Star> m_stars;
    float m_horizonY = 0;
    float m_time = 0;
    float m_curtains = 0.5f, m_motion = 0.5f, m_brightness = 0.5f, m_hue = 0.5f;
    float m_lastW = 0, m_lastH = 0;
};

Scene* CreateAurora() { return new AuroraScene(); }

} // namespace lp::scenes
