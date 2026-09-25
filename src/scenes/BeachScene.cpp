// LivePaper - "Beach".
//
// A warm shoreline: sunset sky, a low sun with a glitter path across the water,
// layered rolling waves with foam, drifting clouds and gulls. The wave field is the
// only per-frame geometry and is bounded to a fixed number of rows.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>


namespace lp::scenes {

namespace {

struct WaveRow {
    float y0;          // resting screen y at the horizon side
    float amp;         // vertical amplitude
    float wavelength;
    float speed;
    float phase;
    float alpha;
    float foam;        // 0..1 how much foam this row carries
    Color color;
};

struct Gull { float x, y, scale, speed, phase; };
struct Cloud { float x, y, scale, speed, alpha; };

} // namespace

class BeachScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Beach"; }
    const wchar_t* Description() const override {
        return L"Sunset shoreline with rolling surf and gulls.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Surf";
            case 1: return L"Wind";
            case 2: return L"Sun glow";
            case 3: return L"Warmth";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        m_horizonY = ctx.height * 0.46f;
        BuildWaves(ctx);
        BuildClouds(ctx);
        BuildGulls(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);

        const float wind = (m_wind - 0.5f) * 2.0f;
        m_time += dt * (0.6f + m_surf * 1.1f);
        for (auto& c : m_clouds) {
            c.x += c.speed * (0.3f + m_wind) * dt * 18.0f;
            if (c.x > ctx.width + c.scale * 240.0f) {
                c.x = -c.scale * 240.0f;
                c.y = ctx.height * (0.05f + 0.14f * (float)((int)(c.scale * 50.0f) % 5) / 5.0f);
            }
        }
        for (auto& g : m_gulls) {
            g.x += g.speed * (0.5f + m_wind) * dt * 30.0f;
            g.phase += dt * 2.2f;
            g.y += std::sin(g.phase) * 6.0f * dt;
            if (g.x > ctx.width + 60.0f) g.x = -60.0f;
            (void)wind;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float warm = m_warmth;

        // --- sunset sky ------------------------------------------------------
        Color top = Color::Hsv(0.62f - warm * 0.06f, 0.45f, 0.72f);
        Color mid = Color::Hsv(0.90f - warm * 0.05f, 0.55f, 0.95f);
        Color low = Color::Hsv(0.07f + warm * 0.03f, 0.62f, 1.00f);
        D2D1_GRADIENT_STOP sky[4];
        sky[0].position = 0.0f;   sky[0].color = D2D1::ColorF(top.r, top.g, top.b, 1);
        sky[1].position = 0.42f;  sky[1].color = D2D1::ColorF(mid.r, mid.g, mid.b, 1);
        sky[2].position = 0.72f;  sky[2].color = D2D1::ColorF(low.r, low.g, low.b, 1);
        sky[3].position = 1.0f;   sky[3].color = D2D1::ColorF(0.98f, 0.62f, 0.35f, 1);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // --- sun + its glitter path on the water -----------------------------
        const float sunX = w * 0.68f;
        const float sunY = m_horizonY - h * 0.045f;
        draw::RadialGlow(dc, brush, sunX, sunY, h * 0.55f,
                         Color::Hsv(0.09f, 0.45f, 1.0f, 0.24f + 0.30f * m_sunGlow), 1.0f, 26);
        draw::Circle(dc, brush, sunX, sunY, h * 0.035f, Color(1.0f, 0.93f, 0.72f, 0.92f));

        // Glitter: short horizontal dashes widening toward the viewer.
        for (int i = 0; i < 60; ++i) {
            float t = (float)i / 59.0f;
            float gy = m_horizonY + t * (h - m_horizonY) * 0.82f;
            float spread = w * 0.02f + t * w * 0.10f;
            float gx = sunX + std::sin(t * 9.0f + m_time * 1.2f) * spread;
            float dash = 6.0f + t * 34.0f;
            float a = (0.30f - t * 0.20f) * (0.55f + 0.45f * std::sin(m_time * 2.4f + t * 12.0f));
            if (a <= 0.01f) continue;
            draw::RoundedRect(dc, brush, gx - dash * 0.5f, gy, dash, 2.0f + t * 1.6f, 1.5f,
                              Color(1.0f, 0.86f, 0.62f, a * (0.4f + 0.6f * m_sunGlow)));
        }

        // --- clouds ----------------------------------------------------------
        // Built from a wide low core with a few smaller puffs above it. Equal-sized
        // circles in a row read as soap bubbles, so the radii vary strongly.
        for (const auto& c : m_clouds) {
            float baseW = c.scale * 150.0f;
            // Low opacity so the sunset gradient reads through the cloud; opaque puffs
            // end up looking like paper cut-outs stuck onto the sky.
            Color cc = Color(1, 1, 1, c.alpha * 0.22f)
                           .Mix(Color::Hsv(0.06f, 0.35f, 1.0f), 0.45f);
            for (int i = 0; i < 6; ++i) {
                float t = (float)i / 5.0f;
                float w = baseW * (0.55f + 0.45f * std::sin(t * kPi));
                float hh = c.scale * (10.0f + 12.0f * std::sin(t * kPi));
                draw::Circle(dc, brush, c.x + (t - 0.5f) * baseW, c.y + hh * 0.35f,
                             std::max(w, hh) * 0.42f, cc);
            }
            for (int i = 0; i < 3; ++i) {
                float t = 0.22f + (float)i * 0.28f;
                float s = c.scale * (15.0f - (float)i * 2.0f);
                draw::Circle(dc, brush, c.x + (t - 0.5f) * baseW * 0.8f,
                             c.y - s * 0.5f, s, Color(cc.r, cc.g, cc.b, cc.a * 0.80f));
            }
        }

        // --- sea -------------------------------------------------------------
        Color deep = Color::Hsv(0.55f + warm * 0.03f, 0.72f, 0.32f);
        Color shallow = Color::Hsv(0.50f + warm * 0.04f, 0.55f, 0.72f);
        D2D1_GRADIENT_STOP sea[3];
        sea[0].position = 0.0f;  sea[0].color = D2D1::ColorF(deep.r, deep.g, deep.b, 1);
        sea[1].position = 0.65f; sea[1].color = D2D1::ColorF(
            Lerp(deep.r, shallow.r, 0.6f), Lerp(deep.g, shallow.g, 0.6f), Lerp(deep.b, shallow.b, 0.6f), 1);
        sea[2].position = 1.0f;  sea[2].color = D2D1::ColorF(shallow.r, shallow.g, shallow.b, 1);

        {
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(sea, 3, &coll)) && coll) {
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

        // --- waves: back to front, each a filled sine band with a foam crest ---
        for (const auto& row : m_waves) {
            DrawWaveRow(dc, brush, row, w, h);
        }

        // --- wet sand --------------------------------------------------------
        const float sandY = h * 0.86f;
        D2D1_GRADIENT_STOP sand[2];
        Color sandLight = Color::Hsv(0.10f, 0.32f + warm * 0.10f, 0.82f);
        Color sandDark = Color::Hsv(0.08f, 0.42f, 0.50f);
        sand[0].position = 0.0f; sand[0].color = D2D1::ColorF(sandLight.r, sandLight.g, sandLight.b, 1);
        sand[1].position = 1.0f; sand[1].color = D2D1::ColorF(sandDark.r, sandDark.g, sandDark.b, 1);
        {
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(sand, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                gp.startPoint = D2D1::Point2F(0, sandY);
                gp.endPoint = D2D1::Point2F(0, h);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, sandY, w, h), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }
        // Foam line where the surf meets the sand.
        for (int i = 0; i < 3; ++i) {
            float t = (float)i;
            float fy = sandY + 4.0f + t * 7.0f;
            float a = (0.30f - t * 0.08f) * (0.6f + 0.4f * std::sin(m_time * 0.9f + t));
            draw::RoundedRect(dc, brush, 0, fy + std::sin(m_time * 0.7f + t) * 3.0f, w, 2.5f, 1.2f,
                              Color(1, 1, 1, std::max(0.0f, a)));
        }

        // --- gulls -----------------------------------------------------------
        for (const auto& g : m_gulls) {
            float s = g.scale * 6.0f;
            float wing = 0.5f + 0.5f * std::sin(g.phase * 1.6f);
            Color c = Color(0.12f, 0.12f, 0.16f, 0.55f);
            draw::Line(dc, brush, g.x - s, g.y + s * 0.35f * wing, g.x, g.y, 1.6f, c);
            draw::Line(dc, brush, g.x, g.y, g.x + s, g.y + s * 0.35f * wing, 1.6f, c);
        }

        // --- legibility vignette ---------------------------------------------
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.22f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_surf = ctx.config->sceneParam[0];
        m_wind = ctx.config->sceneParam[1];
        m_sunGlow = ctx.config->sceneParam[2];
        m_warmth = ctx.config->sceneParam[3];
    }

    void BuildWaves(const SceneCtx& ctx) {
        m_waves.clear();
        const int rows = 14;
        Rng rng(31415u + ctx.variation * 7919u);
        for (int i = 0; i < rows; ++i) {
            float t = (float)i / (float)(rows - 1);     // 0 horizon .. 1 shore
            WaveRow r{};
            // Perspective: rows bunch up near the horizon and spread toward the shore.
            r.y0 = m_horizonY + (ctx.height * 0.88f - m_horizonY) * (t * t * 0.85f + t * 0.15f);
            r.amp = 2.0f + t * 14.0f;
            r.wavelength = ctx.width * (0.30f - t * 0.16f);
            if (r.wavelength < 60.0f) r.wavelength = 60.0f;
            r.speed = 0.5f + rng.Range(0.0f, 1.2f) * (0.6f + t);
            r.phase = rng.Range(0.0f, kTau);
            r.alpha = 0.28f + t * 0.55f;
            r.foam = t;
            Color deep = Color::Hsv(0.55f, 0.70f, 0.30f + t * 0.22f);
            Color light = Color::Hsv(0.50f, 0.42f, 0.72f + t * 0.20f);
            r.color = deep.Mix(light, t * 0.8f);
            m_waves.push_back(r);
        }
    }

    void BuildClouds(const SceneCtx& ctx) {
        m_clouds.clear();
        Rng rng(2718u + ctx.variation * 7919u);
        int count = (int)(4 + 6 * (ctx.width / 1920.0f));
        for (int i = 0; i < count; ++i) {
            Cloud c{};
            c.x = rng.Range(-200.0f, ctx.width + 200.0f);
            c.y = ctx.height * rng.Range(0.06f, 0.30f);
            c.scale = rng.Range(0.6f, 1.4f);
            c.speed = rng.Range(0.35f, 1.2f);
            c.alpha = rng.Range(0.25f, 0.60f);
            m_clouds.push_back(c);
        }
    }

    void BuildGulls(const SceneCtx& ctx) {
        m_gulls.clear();
        Rng rng(1618u + ctx.variation * 7919u);
        for (int i = 0; i < 6; ++i) {
            Gull g{};
            g.x = rng.Range(0.0f, ctx.width);
            g.y = m_horizonY - ctx.height * rng.Range(0.05f, 0.22f);
            g.scale = rng.Range(0.5f, 1.2f);
            g.speed = rng.Range(0.5f, 1.6f);
            g.phase = rng.Range(0.0f, kTau);
            m_gulls.push_back(g);
        }
    }

    // One cresting wave: a filled sine band plus a bright foam edge on top.
    void DrawWaveRow(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                     const WaveRow& row, float w, float h) {
        const int samples = 48;
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        float bottom = std::min(h + 10.0f, row.y0 + h * 0.10f + 40.0f);

        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            auto waveY = [&](int i) {
                float x = (float)i / (float)samples * w;
                float k = kTau / row.wavelength;
                float y = row.y0
                        + std::sin(x * k + row.phase + m_time * row.speed) * row.amp
                        + std::sin(x * k * 2.3f + row.phase * 1.7f - m_time * row.speed * 0.7f) * row.amp * 0.35f;
                // Low-frequency noise breaks up the otherwise perfectly periodic
                // crest, which is what made the surf look like patterned fabric.
                y += Fbm(x * 0.0032f + row.phase, row.foam * 5.0f + m_time * 0.06f, 2) * row.amp * 0.55f;
                return y;
            };
            sink->BeginFigure(D2D1::Point2F(0, waveY(0)), D2D1_FIGURE_BEGIN_FILLED);
            for (int i = 1; i <= samples; ++i)
                sink->AddLine(D2D1::Point2F((float)i / (float)samples * w, waveY(i)));
            sink->AddLine(D2D1::Point2F(w, bottom));
            sink->AddLine(D2D1::Point2F(0, bottom));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();

            brush->SetColor(D2D1::ColorF(row.color.r, row.color.g, row.color.b, row.alpha));
            dc->FillGeometry(geo, brush, nullptr);

            // Foam along the crest, denser for the rows closer to shore.
            if (row.foam > 0.25f) {
                float foamAlpha = (row.foam - 0.25f) * 0.75f * (0.55f + 0.45f * m_surf);
                for (int i = 0; i < samples; ++i) {
                    float x0 = (float)i / (float)samples * w;
                    float x1 = (float)(i + 1) / (float)samples * w;
                    float y0 = waveY(i), y1 = waveY(i + 1);
                    // Only the rising part of the wave gets foam.
                    float slope = (y1 - y0) / std::max(1.0f, x1 - x0);
                    if (slope > 0.05f) continue;
                    draw::Line(dc, brush, x0, y0, x1, y1,
                               1.6f + row.foam * 2.4f,
                               Color(1, 1, 1, foamAlpha * 0.55f));
                }
                for (int i = 0; i < samples; i += 2) {
                    float x0 = (float)i / (float)samples * w;
                    draw::Circle(dc, brush, x0, waveY(i) + 1.0f, 1.4f + row.foam * 1.8f,
                                 Color(1, 1, 1, foamAlpha * 0.30f));
                }
            }
        }
        geo->Release();
        factory->Release();
    }

    std::vector<WaveRow> m_waves;
    std::vector<Cloud> m_clouds;
    std::vector<Gull> m_gulls;
    float m_horizonY = 0;
    float m_time = 0;
    float m_surf = 0.5f, m_wind = 0.5f, m_sunGlow = 0.5f, m_warmth = 0.5f;
    float m_lastW = 0, m_lastH = 0;
};

Scene* CreateBeach() { return new BeachScene(); }

} // namespace lp::scenes
