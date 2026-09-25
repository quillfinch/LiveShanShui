// LivePaper - "Anime Sakura".
//
// An anime-styled scene: a banded pastel sky with a large low sun, a dark cherry
// tree silhouette in the foreground, drifting sakura petals, floating light motes,
// and a subtle speed-line/vignette frame. Deliberately stylised rather than
// photoreal - fewer shades, stronger silhouettes, warmer highlights.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>


namespace lp::scenes {

namespace {

struct Petal {
    float x, y, vx, vy, spin, spinSpeed, size, hue, sway, swayPhase;
};
struct Mote { float x, y, r, phase, speed; };
struct CloudBand { float y, height, alpha, phase, speed; };
struct Tip { float x, y, size; };

} // namespace

class AnimeScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Anime Sakura"; }
    const wchar_t* Description() const override {
        return L"Pastel sunset, cherry blossom petals and floating light.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Petals";
            case 1: return L"Drift";
            case 2: return L"Glow";
            case 3: return L"Sky tint";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildPetals(ctx);
        BuildMotes(ctx);
        BuildBands(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);

        const float drift = 0.3f + m_drift * 1.7f;
        for (auto& p : m_petals) {
            p.swayPhase += dt * p.sway;
            p.x += (p.vx + std::sin(p.swayPhase) * 26.0f) * drift * dt;
            p.y += p.vy * drift * dt;
            p.spin += p.spinSpeed * dt;
            if (p.y > ctx.height + 24.0f) {
                p.y = -24.0f;
                p.x = (float)((int)(p.x * 37.0f + p.swayPhase * 613.0f) % std::max(1, (int)ctx.width));
                if (p.x < 0) p.x += ctx.width;
            }
            if (p.x > ctx.width + 40.0f) p.x = -30.0f;
            if (p.x < -40.0f) p.x = ctx.width + 30.0f;
        }
        for (auto& mo : m_motes) {
            mo.phase += dt * mo.speed;
            mo.y -= dt * (6.0f + mo.r * 2.0f);
            mo.x += std::sin(mo.phase) * 8.0f * dt;
            if (mo.y < -10.0f) {
                mo.y = ctx.height + 10.0f;
                mo.x = (float)((int)(mo.x * 53.0f + mo.phase * 311.0f) % std::max(1, (int)ctx.width));
                if (mo.x < 0) mo.x += ctx.width;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;
        const float tint = m_tint;

        // --- stylised sky: few, wide bands (anime backgrounds love flat bands) --
        Color c1 = Color::Hsv(0.62f - tint * 0.07f, 0.42f, 0.66f);
        Color c2 = Color::Hsv(0.72f - tint * 0.05f, 0.40f, 0.82f);
        Color c3 = Color::Hsv(0.92f - tint * 0.04f, 0.45f, 0.96f);
        Color c4 = Color::Hsv(0.05f + tint * 0.04f, 0.35f, 1.00f);

        D2D1_GRADIENT_STOP sky[5];
        sky[0].position = 0.00f; sky[0].color = D2D1::ColorF(c1.r, c1.g, c1.b, 1);
        sky[1].position = 0.34f; sky[1].color = D2D1::ColorF(c2.r, c2.g, c2.b, 1);
        sky[2].position = 0.56f; sky[2].color = D2D1::ColorF(c3.r, c3.g, c3.b, 1);
        sky[3].position = 0.74f; sky[3].color = D2D1::ColorF(c4.r, c4.g, c4.b, 1);
        sky[4].position = 1.00f; sky[4].color = D2D1::ColorF(0.42f, 0.34f, 0.52f, 1);
        draw::VerticalGradient(dc, w, h, sky, 5);

        // --- the big low sun: hard disc, soft halo ---------------------------
        const float sunX = w * 0.62f, sunY = h * 0.60f;
        draw::RadialGlow(dc, brush, sunX, sunY, h * 0.60f,
                         Color::Hsv(0.06f, 0.30f, 1.0f, 0.20f + 0.35f * m_glow), 1.0f, 26);
        draw::Circle(dc, brush, sunX, sunY, h * 0.075f, Color(1.0f, 0.90f, 0.78f, 0.75f));
        draw::Circle(dc, brush, sunX, sunY, h * 0.052f, Color(1.0f, 0.96f, 0.90f, 0.92f));

        // --- horizontal cloud bands ------------------------------------------
        for (const auto& b : m_bands) {
            float y = b.y + std::sin(ctx.time * 0.10f * b.speed + b.phase) * 6.0f;
            float a = b.alpha * (0.7f + 0.3f * std::sin(ctx.time * 0.16f + b.phase));
            Color cc = Color::Hsv(0.92f - tint * 0.04f, 0.22f, 1.0f, a);
            draw::RoundedRect(dc, brush, -20.0f, y, w + 40.0f, b.height, b.height * 0.5f, cc);
        }

        // --- distant hills (two flat layers) ---------------------------------
        {
            // NB: `near`/`far` are Win32 macros, so these locals need other names.
            Color farTone = Color::Hsv(0.70f - tint * 0.04f, 0.35f, 0.55f);
            Color nearTone = Color::Hsv(0.68f - tint * 0.03f, 0.40f, 0.34f);
            FillHill(dc, brush, h * 0.80f, h * 0.06f, farTone, 2.1f, 0.0f);
            FillHill(dc, brush, h * 0.87f, h * 0.05f, nearTone, 3.3f, 40.0f);
        }

        // --- cherry tree silhouette (foreground, top-left corner) -------------
        // The tree is a tapered trunk plus a recursive branch walk that records its
        // twig tips, so blossom clusters land on real branches rather than in a
        // disc scattered near the corner.
        {
            Color trunk = Color(0.13f, 0.09f, 0.16f, 1.0f);
            Color blossom = Color::Hsv(0.95f - tint * 0.03f, 0.38f, 1.0f, 0.55f);
            m_tips.clear();
            const float rootX = w * 0.03f;
            // Trunk: a thick tapered quad from below the frame up to the first fork.
            float forkY = h * 0.66f;
            DrawTaperedQuad(dc, brush, rootX, h * 1.04f, w * 0.038f,
                            rootX + w * 0.075f, forkY, w * 0.017f, trunk);
            // The first limb leans across the frame so the bloom fills the upper-left
            // rather than growing as a vertical pole.
            DrawBranch(dc, brush, rootX + w * 0.075f, forkY, w * 0.017f, h * 0.24f,
                       0.95f, 0, trunk, blossom);

            // Blossom clusters at the recorded tips, plus a few filling the crown.
            for (const auto& tip : m_tips) {
                float r = tip.size * 5.0f;
                draw::Circle(dc, brush, tip.x, tip.y, r, Color(blossom.r, blossom.g, blossom.b, 0.34f));
                draw::Circle(dc, brush, tip.x - r * 0.2f, tip.y - r * 0.25f, r * 0.6f,
                             Color(1.0f, 0.84f, 0.90f, 0.30f));
                draw::Circle(dc, brush, tip.x, tip.y, r * 0.28f, Color(1.0f, 0.93f, 0.96f, 0.42f));
            }
        }

        // --- floating light motes --------------------------------------------
        for (const auto& mo : m_motes) {
            float a = 0.25f + 0.5f * (0.5f + 0.5f * std::sin(mo.phase));
            Color c = Color::Hsv(0.12f, 0.25f, 1.0f, a * (0.6f + 0.6f * m_glow));
            draw::RadialGlow(dc, brush, mo.x, mo.y, mo.r * 5.0f, c, 1.0f, 6);
            draw::Circle(dc, brush, mo.x, mo.y, mo.r * 0.7f, Color(1, 1, 1, a * 0.8f));
        }

        // --- petals ----------------------------------------------------------
        for (const auto& p : m_petals) {
            // A petal is a small rotated ellipse; we approximate the rotation by
            // squashing it with the spin, which reads correctly at these sizes.
            float squash = 0.35f + 0.65f * std::fabs(std::cos(p.spin));
            float rx = p.size * (0.55f + 0.45f * squash);
            float ry = p.size * (0.35f + 0.65f * (1.0f - squash * 0.5f));
            Color c = Color::Hsv(0.95f + p.hue * 0.04f, 0.30f + p.hue * 0.20f, 1.0f, 0.80f);
            draw::Circle(dc, brush, p.x, p.y, (rx + ry) * 0.5f, c);
            // A slightly brighter core gives the petal some depth.
            draw::Circle(dc, brush, p.x - rx * 0.15f, p.y - ry * 0.15f,
                         (rx + ry) * 0.22f, Color(1.0f, 0.93f, 0.96f, 0.55f));
        }

        // --- anime-style vignette + corner framing ---------------------------
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.25f, 0.10f, 0.20f, 0.30f);
        draw::VerticalGradient(dc, w, h, vig, 2);

        // Soft letterbox bars keep the composition cinematic behind icons.
        float barH = h * 0.035f;
        draw::RoundedRect(dc, brush, 0, 0, w, barH, 0, Color(0.05f, 0.02f, 0.06f, 0.35f));
        draw::RoundedRect(dc, brush, 0, h - barH, w, barH, 0, Color(0.05f, 0.02f, 0.06f, 0.35f));
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_petalAmount = ctx.config->sceneParam[0];
        m_drift = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_tint = ctx.config->sceneParam[3];
    }

    void BuildPetals(const SceneCtx& ctx) {
        m_petals.clear();
        int want = (int)(40 + 130 * m_petalAmount);
        want = std::min(want, 220);
        Rng rng(112358u + ctx.variation * 7919u);
        m_petals.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Petal p{};
            p.x = rng.Range(0.0f, ctx.width);
            p.y = rng.Range(-ctx.height, ctx.height);
            p.vx = rng.Range(-6.0f, 18.0f);
            p.vy = rng.Range(16.0f, 52.0f);
            p.spin = rng.Range(0.0f, kTau);
            p.spinSpeed = rng.Range(-2.2f, 2.2f);
            p.size = rng.Range(3.5f, 9.0f);
            p.hue = rng.Unit();
            p.sway = rng.Range(0.6f, 2.0f);
            p.swayPhase = rng.Range(0.0f, kTau);
            m_petals.push_back(p);
        }
    }

    void BuildMotes(const SceneCtx& ctx) {
        m_motes.clear();
        Rng rng(141421u + ctx.variation * 7919u);
        int want = (int)(16 + 40 * m_glow);
        for (int i = 0; i < want; ++i) {
            Mote m{};
            m.x = rng.Range(0.0f, ctx.width);
            m.y = rng.Range(0.0f, ctx.height);
            m.r = rng.Range(1.4f, 4.2f);
            m.phase = rng.Range(0.0f, kTau);
            m.speed = rng.Range(0.5f, 1.8f);
            m_motes.push_back(m);
        }
    }

    void BuildBands(const SceneCtx& ctx) {
        m_bands.clear();
        Rng rng(173205u + ctx.variation * 7919u);
        for (int i = 0; i < 7; ++i) {
            CloudBand b{};
            b.y = ctx.height * rng.Range(0.12f, 0.62f);
            b.height = ctx.height * rng.Range(0.012f, 0.038f);
            b.alpha = rng.Range(0.10f, 0.28f);
            b.phase = rng.Range(0.0f, kTau);
            b.speed = rng.Range(0.6f, 1.8f);
            m_bands.push_back(b);
        }
    }

    void FillHill(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                  float baseY, float amp, const Color& color, float freq, float phase) {
        const int samples = 40;
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            auto hillY = [&](int i) {
                float t = (float)i / (float)samples;
                float n = Fbm(t * freq + phase, phase * 0.5f, 2);
                return baseY + n * amp;
            };
            sink->BeginFigure(D2D1::Point2F(0, hillY(0)), D2D1_FIGURE_BEGIN_FILLED);
            for (int i = 1; i <= samples; ++i)
                sink->AddLine(D2D1::Point2F((float)i / (float)samples * m_lastW, hillY(i)));
            sink->AddLine(D2D1::Point2F(m_lastW, m_lastH + 10.0f));
            sink->AddLine(D2D1::Point2F(0, m_lastH + 10.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
            dc->FillGeometry(geo, brush, nullptr);
        }
        geo->Release();
        factory->Release();
    }

    // Fills a tapered quad; used for the trunk so it has real thickness.
    void DrawTaperedQuad(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                         float x0, float y0, float w0, float x1, float y1, float w1,
                         const Color& color) {
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            sink->BeginFigure(D2D1::Point2F(x0 - w0 * 0.5f, y0), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x0 + w0 * 0.5f, y0));
            sink->AddLine(D2D1::Point2F(x1 + w1 * 0.5f, y1));
            sink->AddLine(D2D1::Point2F(x1 - w1 * 0.5f, y1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
            dc->FillGeometry(geo, brush, nullptr);
        }
        geo->Release();
        factory->Release();
    }

    // Records a twig tip so blossom clusters can be drawn on top of the branches.
    void DrawBranch(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                    float x, float y, float width, float length, float tilt,
                    int depth, const Color& trunk, const Color& blossom) {
        if (length < 6.0f || depth > 5) return;
        float topX = x + tilt * length;
        float topY = y - length;
        // Taper each segment so the tree thickens toward its base. A constant-width
        // stroke is what made the earlier version read as a bare pole.
        draw::Line(dc, brush, x, y, topX, topY, std::max(1.1f, width), trunk);
        float nextLen = length * 0.72f;
        float nextW = width * 0.60f;
        DrawBranch(dc, brush, topX, topY, nextW, nextLen, tilt - 0.30f, depth + 1, trunk, blossom);
        DrawBranch(dc, brush, topX, topY, nextW, nextLen, tilt + 0.26f, depth + 1, trunk, blossom);
        if (depth >= 2) {
            DrawBranch(dc, brush, topX, topY, nextW * 0.8f, nextLen * 0.85f, tilt * 0.35f,
                       depth + 1, trunk, blossom);
        }
        if (depth >= 2) {
            Tip t{};
            t.x = topX;
            t.y = topY;
            t.size = std::max(1.4f, width * 0.8f);
            m_tips.push_back(t);
        }
    }

    std::vector<Petal> m_petals;
    std::vector<Mote> m_motes;
    std::vector<CloudBand> m_bands;
    std::vector<Tip> m_tips;       // scratch, rebuilt each frame
    float m_petalAmount = 0.5f, m_drift = 0.5f, m_glow = 0.5f, m_tint = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateAnime() { return new AnimeScene(); }

} // namespace lp::scenes
