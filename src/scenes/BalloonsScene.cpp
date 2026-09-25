// LivePaper - "Balloons".
//
// Hot-air balloons drifting over hazy morning hills. Each canopy is a striped
// circle: a base fill plus meridian stripes added through axis-aligned clip
// rectangles, which is exact and cheap. Balloons bob on noise and cross the sky
// slowly; soft clouds drift behind them.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Bln {
    float x, baseY;
    float r;
    float phase;
    float bobAmp;
    float drift;
    float baseHue;
};

struct Puff {
    float x, y;
    float r;
    float alpha;
    float speed;
};

} // namespace

class BalloonsScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Balloons"; }
    const wchar_t* Description() const override {
        return L"Striped hot-air balloons drifting over morning hills.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Balloons";
            case 1: return L"Drift";
            case 2: return L"Clouds";
            case 3: return L"Warmth";
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

        const float w = ctx.width;
        for (auto& b : m_balloons) {
            b.x += b.drift * (0.4f + 1.3f * m_drift) * dt;
            if (b.x - b.r > w + 60.0f) b.x = -b.r - 60.0f;
        }
        for (auto& p : m_puffs) {
            p.x += p.speed * (0.4f + 0.9f * m_drift) * dt;
            if (p.x - p.r > w) p.x = -p.r;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Morning sky. The blend is done in RGB (Color::Mix), not in hue space:
        // a hue interpolation from blue to peach would sweep through green.
        D2D1_GRADIENT_STOP sky[3];
        Color topCool = Color::Hex(0x6f9fd8), topWarm = Color::Hex(0x9fb8d0);
        Color lowCool = Color::Hex(0xcfe0ea), lowWarm = Color::Hex(0xffc98a);
        Color top = topCool.Mix(topWarm, m_warmth);
        Color low = lowCool.Mix(lowWarm, m_warmth);
        Color mid = top.Mix(low, 0.55f);
        sky[0].position = 0.0f; sky[0].color = D2D1::ColorF(top.r, top.g, top.b, 1);
        sky[1].position = 0.55f; sky[1].color = D2D1::ColorF(mid.r, mid.g, mid.b, 1);
        sky[2].position = 1.0f; sky[2].color = D2D1::ColorF(low.r, low.g, low.b, 1);
        draw::VerticalGradient(dc, w, h, sky, 3);

        // Low sun haze.
        draw::RadialGlow(dc, brush, w * 0.30f, h * 0.72f, h * 0.5f,
                         low.Mix(Color(1, 1, 1, 0), 0.2f).WithAlpha(0.16f), 1.0f, 16);

        // Clouds: soft clusters, brighter where the light catches them.
        for (const auto& p : m_puffs) {
            float a = p.alpha * (0.30f + 0.55f * m_clouds);
            draw::Circle(dc, brush, p.x, p.y, p.r, Color(0.98f, 0.98f, 1.0f, a * 0.5f));
            draw::Circle(dc, brush, p.x - p.r * 0.6f, p.y + p.r * 0.25f, p.r * 0.7f, Color(0.96f, 0.96f, 1.0f, a * 0.4f));
            draw::Circle(dc, brush, p.x + p.r * 0.65f, p.y + p.r * 0.2f, p.r * 0.65f, Color(0.96f, 0.97f, 1.0f, a * 0.4f));
            draw::Circle(dc, brush, p.x, p.y + p.r * 0.4f, p.r * 0.55f, Color(0.90f, 0.91f, 0.97f, a * 0.35f));
        }

        // Hills, far to near.
        FillHill(dc, brush, m_hillFar, Color::Hsv(0.31f, 0.22f, 0.40f).WithAlpha(0.9f));
        FillHill(dc, brush, m_hillNear, Color::Hsv(0.29f, 0.30f, 0.22f));

        // Balloons, small (far) first.
        std::vector<Bln*> order;
        order.reserve(m_balloons.size());
        for (auto& b : m_balloons) order.push_back(&b);
        std::sort(order.begin(), order.end(),
                  [](const Bln* a, const Bln* c) { return a->r < c->r; });

        for (const Bln* b : order) {
            float y = b->baseY + std::sin(m_time * 0.35f + b->phase) * b->bobAmp;
            DrawBalloon(dc, brush, b->x, y, b->r, b->baseHue);
        }

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.30f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_count = ctx.config->sceneParam[0];
        m_drift = ctx.config->sceneParam[1];
        m_clouds = ctx.config->sceneParam[2];
        m_warmth = ctx.config->sceneParam[3];
    }

    void FillHill(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                  const std::vector<D2D1_POINT_2F>& pts, const Color& color) {
        if (pts.size() < 3 || ctxHeight() < 1.0f) return;
        m_scratch.assign(pts.begin(), pts.end());
        m_scratch.push_back(D2D1::Point2F(pts.back().x, ctxHeight()));
        m_scratch.push_back(D2D1::Point2F(pts.front().x, ctxHeight()));
        draw::FillPolygon(dc, brush, m_scratch.data(), (unsigned)m_scratch.size(), color);
    }

    // DrawBalloon: canopy + taper + ropes + basket. Stripes are meridian bands
    // added through axis-aligned clips over the base circle.
    void DrawBalloon(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                     float x, float y, float r, float baseHue) {
        // Envelope base.
        Color c0 = Color::Hsv(baseHue, 0.62f, 0.85f);
        draw::Circle(dc, brush, x, y, r, c0);
        // Four meridian stripes.
        for (int s = 0; s < 4; ++s) {
            float u = -0.75f + 0.5f * s;   // stripe centre offsets in unit radius
            float half = 0.11f;
            float x0 = x + (u - half) * r, x1 = x + (u + half) * r;
            dc->PushAxisAlignedClip(D2D1::RectF(x0, y - r, x1, y + r),
                                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            Color cs = Color::Hsv(Fract(baseHue + 0.14f + 0.09f * s), 0.62f, 0.88f);
            draw::Circle(dc, brush, x, y, r, cs);
            dc->PopAxisAlignedClip();
        }
        // Crown highlight and a shade at the mouth.
        draw::Circle(dc, brush, x - r * 0.35f, y - r * 0.4f, r * 0.26f, Color(1, 1, 1, 0.10f));
        draw::Circle(dc, brush, x, y + r * 0.55f, r * 0.42f, Color(0, 0, 0, 0.10f));
        // Taper from the envelope mouth down to the basket.
        float mouthY = y + r * 0.92f;
        D2D1_POINT_2F skirt[3] = {
            D2D1::Point2F(x - r * 0.28f, mouthY - r * 0.18f),
            D2D1::Point2F(x + r * 0.28f, mouthY - r * 0.18f),
            D2D1::Point2F(x, mouthY + r * 0.10f),
        };
        draw::FillPolygon(dc, brush, skirt, 3, Color::Hsv(baseHue, 0.55f, 0.40f));
        // Ropes and basket.
        float bkt = mouthY + r * 0.34f;
        Color rope(0.10f, 0.07f, 0.05f, 0.9f);
        draw::Line(dc, brush, x - r * 0.10f, mouthY + r * 0.05f, x - r * 0.09f, bkt, 1.2f, rope);
        draw::Line(dc, brush, x + r * 0.10f, mouthY + r * 0.05f, x + r * 0.09f, bkt, 1.2f, rope);
        draw::RoundedRect(dc, brush, x - r * 0.14f, bkt, r * 0.28f, r * 0.18f, r * 0.04f,
                          Color::Hsv(0.07f, 0.45f, 0.30f));
        draw::RoundedRect(dc, brush, x - r * 0.14f, bkt, r * 0.28f, r * 0.06f, r * 0.03f,
                          Color::Hsv(0.07f, 0.40f, 0.42f, 0.9f));
    }

    // FillHill needs the canvas height; stored at Build time.
    float ctxHeight() const { return m_canvasH; }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        m_canvasH = h;
        Rng rng(0xBA1100u + ctx.variation * 7919u);

        m_balloons.clear();
        int want = std::min((int)(3 + 4 * m_count * ctx.density), 7);
        m_balloons.reserve((size_t)want);
        float scheme = rng.Range(0.0f, 1.0f);
        for (int i = 0; i < want; ++i) {
            Bln b{};
            b.r = std::min(w, h) * rng.Range(0.045f, 0.10f);
            b.x = w * rng.Range(-0.05f, 1.0f);
            b.baseY = h * rng.Range(0.16f, 0.52f);
            b.phase = rng.Range(0.0f, kTau);
            b.bobAmp = h * rng.Range(0.006f, 0.016f);
            b.drift = rng.Range(8.0f, 20.0f) * (rng.Unit() < 0.85f ? 1.0f : -1.0f);
            b.baseHue = Fract(scheme + (float)i * 0.15f + rng.Range(-0.04f, 0.04f));
            m_balloons.push_back(b);
        }

        m_puffs.clear();
        int puffs = (int)((3 + 5 * m_clouds) * ctx.density);
        puffs = std::min(puffs, 9);
        m_puffs.reserve((size_t)puffs);
        for (int i = 0; i < puffs; ++i) {
            Puff p{};
            p.x = rng.Range(0.0f, w);
            p.y = h * rng.Range(0.06f, 0.38f);
            p.r = h * rng.Range(0.03f, 0.075f);
            p.alpha = rng.Range(0.5f, 0.95f);
            p.speed = rng.Range(3.0f, 9.0f);
            m_puffs.push_back(p);
        }

        auto ridge = [&](std::vector<D2D1_POINT_2F>& out, float baseY, float amp, uint32_t seed) {
            out.clear();
            const int steps = 44;
            Rng rr(seed);
            float phase = rr.Range(0.0f, 80.0f);
            for (int i = 0; i <= steps; ++i) {
                float u = (float)i / steps;
                float n = Noise1(phase + u * 2.4f) * 0.6f + Noise1(phase * 2.1f + u * 6.0f) * 0.4f;
                out.push_back(D2D1::Point2F(u * w, baseY + n * amp));
            }
        };
        ridge(m_hillFar, h * 0.66f, h * 0.05f, 411u + ctx.variation);
        ridge(m_hillNear, h * 0.78f, h * 0.04f, 412u + ctx.variation);
    }

    std::vector<Bln> m_balloons;
    std::vector<Puff> m_puffs;
    std::vector<D2D1_POINT_2F> m_hillFar, m_hillNear, m_scratch;
    float m_canvasH = 1080.0f;
    float m_time = 0;
    float m_count = 0.5f, m_drift = 0.5f, m_clouds = 0.5f, m_warmth = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateBalloons() { return new BalloonsScene(); }

} // namespace lp::scenes
