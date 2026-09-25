// LivePaper - "Alpine Peaks".
//
// Layered mountain silhouettes receding into atmospheric haze, with snow-lit ridges,
// a low moon, and a faint star field. Ranges are generated once; only the moon's glow
// and the drifting mist move per frame.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Range {
    std::vector<float> peaks;    // screen y of the ridge at each sample
    float baseY;                 // valley floor
    float snow;                  // height above which the ridge is snow-lit
    Color body;
    Color snowCol;
};

struct Mist { float y, speed, alpha, phase; };

} // namespace

class AlpineScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Alpine Peaks"; }
    const wchar_t* Description() const override {
        return L"Snow-lit mountain ridges under a moonlit sky.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Peaks";
            case 1: return L"Haze";
            case 2: return L"Moon";
            case 3: return L"Tint";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildRanges(ctx);
        BuildMist(ctx);
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
        const float tint = m_tint;

        // Sky: dusk blues that warm toward the horizon.
        D2D1_GRADIENT_STOP sky[4];
        Color top = Color::Hsv(0.60f - tint * 0.04f, 0.42f, 0.34f);
        Color mid = Color::Hsv(0.58f - tint * 0.03f, 0.38f, 0.52f);
        Color low = Color::Hsv(0.55f - tint * 0.02f, 0.30f, 0.76f);
        Color hor = Color::Hsv(0.09f + tint * 0.03f, 0.30f, 0.82f);
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(top.r, top.g, top.b, 1);
        sky[1].position = 0.45f; sky[1].color = D2D1::ColorF(mid.r, mid.g, mid.b, 1);
        sky[2].position = 0.72f; sky[2].color = D2D1::ColorF(low.r, low.g, low.b, 1);
        sky[3].position = 1.00f; sky[3].color = D2D1::ColorF(hor.r, hor.g, hor.b, 1);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // Stars (sparse; kept dim so the peaks stay the subject).
        m_stars.Draw(dc, brush, m_time * 0.3f);

        // Moon + glow.
        const float moonX = w * 0.72f, moonY = h * 0.26f;
        draw::RadialGlow(dc, brush, moonX, moonY, h * 0.42f,
                         Color::Hsv(0.55f, 0.22f, 1.0f, 0.10f + 0.30f * m_moon), 1.0f, 26);
        draw::Circle(dc, brush, moonX, moonY, h * 0.030f, Color(1, 1, 1, 0.80f));
        draw::Circle(dc, brush, moonX - h * 0.006f, moonY - h * 0.006f, h * 0.026f,
                     Color(0.88f, 0.92f, 1.0f, 0.9f));

        // Mountain ranges, far to near.
        for (const auto& rng : m_ranges) {
            FillRange(dc, brush, rng);
        }

        // Drifting mist in the valleys.
        for (const auto& m : m_mist) {
            float y = m.y + std::sin(m_time * 0.10f * m.speed + m.phase) * 6.0f;
            float a = m.alpha * (0.55f + 0.45f * std::sin(m_time * 0.16f + m.phase));
            draw::RoundedRect(dc, brush, -20.0f, y, w + 40.0f, 16.0f, 8.0f,
                              Color(0.92f, 0.96f, 1.0f, std::max(0.0f, a)));
        }

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.02f, 0.03f, 0.06f, 0.36f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_peaks = ctx.config->sceneParam[0];
        m_haze = ctx.config->sceneParam[1];
        m_moon = ctx.config->sceneParam[2];
        m_tint = ctx.config->sceneParam[3];
    }

    void BuildRanges(const SceneCtx& ctx) {
        m_ranges.clear();
        Rng rng(77123u + ctx.variation * 7919u);
        const int layers = 4;
        const int samples = 72;
        for (int layer = 0; layer < layers; ++layer) {
            float depth = (float)layer / (float)(layers - 1);   // 0 far .. 1 near
            Range r{};
            // Peaks become taller and darker as they approach; far ranges are hazy.
            r.baseY = ctx.height * (0.55f + depth * 0.30f);
            float amp = ctx.height * (0.16f - depth * 0.05f) * (0.7f + 0.7f * m_peaks);
            float freq = 1.2f + depth * 1.9f;
            float phase = rng.Range(0.0f, 100.0f);
            r.peaks.resize(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                float t = (float)i / (float)samples;
                float n = Fbm(t * freq * 2.6f + phase, depth * 11.0f, 3);
                r.peaks[i] = r.baseY + n * amp;
            }
            r.snow = r.baseY - amp * 0.42f;   // snow line sits near the ridge tops

            Color farCol = Color::Hsv(0.56f, 0.20f, 0.84f);
            Color nearCol = Color::Hsv(0.58f - m_tint * 0.03f, 0.42f, 0.16f);
            r.body = farCol.Mix(nearCol, depth);
            r.body = r.body.Mix(Color::Hsv(0.56f, 0.12f, 0.92f), (1.0f - depth) * m_haze * 0.6f);
            r.snowCol = Color(0.92f, 0.96f, 1.0f, 0.92f).Mix(r.body, depth * 0.25f);
            m_ranges.push_back(std::move(r));
        }
    }

    void BuildMist(const SceneCtx& ctx) {
        m_mist.clear();
        Rng rng(2718u + ctx.variation * 7919u);
        int count = (int)(3 + 4 * m_haze);
        for (int i = 0; i < count; ++i) {
            Mist m{};
            m.y = ctx.height * rng.Range(0.58f, 0.82f);
            m.speed = rng.Range(0.5f, 1.6f);
            m.alpha = rng.Range(0.04f, 0.12f) * m_haze;
            m.phase = rng.Range(0.0f, kTau);
            m_mist.push_back(m);
        }
    }

    // Fills the area below a ridge polyline, with a lighter band near the peaks that
    // reads as snow-lit ridges.
    void FillRange(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, const Range& rng) {
        if (rng.peaks.size() < 2) return;
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            float step = m_lastW / (float)(rng.peaks.size() - 1);
            sink->BeginFigure(D2D1::Point2F(0, rng.peaks[0]), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < rng.peaks.size(); ++i)
                sink->AddLine(D2D1::Point2F((float)i * step, rng.peaks[i]));
            sink->AddLine(D2D1::Point2F(m_lastW, m_lastH + 10.0f));
            sink->AddLine(D2D1::Point2F(0, m_lastH + 10.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            brush->SetColor(D2D1::ColorF(rng.body.r, rng.body.g, rng.body.b, rng.body.a));
            dc->FillGeometry(geo, brush, nullptr);
        }
        geo->Release();
        factory->Release();

        // Snow band: a second, thin strip hugging the ridge. Approximated with short
        // line segments whose thickness grows where the ridge rises above the snow line.
        float step = m_lastW / (float)(rng.peaks.size() - 1);
        for (size_t i = 0; i + 1 < rng.peaks.size(); ++i) {
            float y0 = rng.peaks[i], y1 = rng.peaks[i + 1];
            float above0 = Clamp01((rng.snow - y0) / (rng.snow - rng.baseY + 1.0f));
            float above1 = Clamp01((rng.snow - y1) / (rng.snow - rng.baseY + 1.0f));
            float amt = (above0 + above1) * 0.5f;
            if (amt <= 0.02f) continue;
            Color c = rng.snowCol;
            c.a = rng.snowCol.a * amt;
            draw::Line(dc, brush, (float)i * step, y0, (float)(i + 1) * step, y1,
                       1.5f + amt * 3.0f, c);
        }
    }

    std::vector<Range> m_ranges;
    std::vector<Mist> m_mist;
    draw::StarField m_stars;
    float m_time = 0;
    float m_peaks = 0.5f, m_haze = 0.5f, m_moon = 0.5f, m_tint = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateAlpine() { return new AlpineScene(); }

} // namespace lp::scenes
