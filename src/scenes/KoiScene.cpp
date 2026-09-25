// LivePaper - "Koi Pond".
//
// A top-down pond: koi glide along noise-steered paths (drawn with a per-fish
// rotation transform, so the body, tail and spots share one frame), lily pads sit
// on the surface, and expanding rings ripple outward from the fish and the rain of
// things that never quite land.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Koi {
    float x, y;
    float angle;        // radians, heading
    float len;          // body length in pixels
    float speed;
    float turnBias;     // wander seed
    float phase;        // tail wag
    float wagFreq;
    int pattern;        // 0 white+red, 1 orange, 2 gold, 3 white+black
    float rippleIn;     // seconds until the fish kisses the surface
};

struct Pad {
    float x, y, rx, ry, rot;
    float hue;
};

struct Ring {
    float x, y;
    float r;            // current radius
    float maxR;
    float speed;
    float alpha0;
};

} // namespace

class KoiScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Koi Pond"; }
    const wchar_t* Description() const override {
        return L"Koi gliding between lily pads, trailing gentle ripples.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Fish";
            case 1: return L"Drift";
            case 2: return L"Ripples";
            case 3: return L"Palette";
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

        const float speedScale = 0.35f + 1.0f * m_drift;
        const float w = ctx.width, h = ctx.height;

        for (auto& k : m_koi) {
            // Wander: low-frequency noise sets a target heading delta.
            float wander = Noise1(m_time * 0.10f + k.turnBias) * 1.6f;
            float steer = wander;
            // Edge avoidance: steer back toward the middle when near a border.
            float cx = w * 0.5f, cy = h * 0.5f;
            float toC = std::atan2(cy - k.y, cx - k.x);
            float margin = std::min(w, h) * 0.16f;
            bool nearEdge = k.x < margin || k.y < margin || k.x > w - margin || k.y > h - margin;
            if (nearEdge) {
                float d = std::atan2(std::sin(toC) - std::sin(k.angle),
                                     std::cos(toC) - std::cos(k.angle));
                steer = d * 2.4f;
            }
            k.angle += Clamp(steer, -2.6f, 2.6f) * dt;
            k.phase += dt * k.wagFreq * speedScale;
            float v = k.speed * speedScale * (nearEdge ? 1.5f : 1.0f);
            k.x += std::cos(k.angle) * v * dt;
            k.y += std::sin(k.angle) * v * dt;

            // Surface kiss -> ripple.
            k.rippleIn -= dt * (0.6f + m_ripples * 1.4f);
            if (k.rippleIn <= 0.0f && (int)m_rings.size() < kMaxRings) {
                k.rippleIn = 2.5f + Fract(k.turnBias * 3.7f) * 5.0f;
                SpawnRing(k.x, k.y, 40.0f + k.len * 1.6f);
            }
        }

        // Ambient rings.
        m_ringTimer -= dt * (0.4f + m_ripples * 1.8f);
        if (m_ringTimer <= 0.0f && (int)m_rings.size() < kMaxRings) {
            m_ringTimer = 0.7f;
            Rng rng((uint32_t)(m_time * 977.0f) + 17u + ctx.variation);
            SpawnRing(rng.Range(0.05f, 0.95f) * w, rng.Range(0.08f, 0.92f) * h,
                      50.0f + rng.Range(0.0f, 90.0f));
        }

        for (size_t i = 0; i < m_rings.size();) {
            Ring& r = m_rings[i];
            r.r += r.speed * dt;
            if (r.r >= r.maxR) m_rings[i] = m_rings.back(), m_rings.pop_back();
            else ++i;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Pond water: deep green-teal, brighter in the middle where the light falls.
        D2D1_GRADIENT_STOP water[3];
        water[0].position = 0.0f; water[0].color = D2D1::ColorF(0.016f, 0.062f, 0.055f, 1);
        water[1].position = 0.55f; water[1].color = D2D1::ColorF(0.026f, 0.088f, 0.076f, 1);
        water[2].position = 1.0f; water[2].color = D2D1::ColorF(0.014f, 0.052f, 0.048f, 1);
        draw::VerticalGradient(dc, w, h, water, 3);
        draw::RadialGlow(dc, brush, w * 0.42f, h * 0.36f, std::max(w, h) * 0.55f,
                         Color::Hsv(0.42f, 0.30f, 1.0f, 0.10f), 1.0f, 12);

        // Ripple rings first, so fish swim above them.
        for (const auto& r : m_rings) {
            float t = r.r / r.maxR;
            float a = r.alpha0 * (1.0f - t) * (1.0f - t);
            brush->SetColor(D2D1::ColorF(0.75f, 0.95f, 0.9f, a));
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(r.x, r.y), r.r, r.r * 0.82f), brush,
                            Lerp(1.0f, 2.6f, t), nullptr);
        }

        // Lily pads.
        for (const auto& p : m_pads) {
            dc->SetTransform(D2D1::Matrix3x2F::Rotation(p.rot, D2D1::Point2F(p.x, p.y)));
            Color pad = Color::Hsv(p.hue, 0.45f, 0.16f);
            brush->SetColor(D2D1::ColorF(pad.r, pad.g, pad.b, 0.92f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(p.x, p.y), p.rx, p.ry), brush);
            // The classic wedge notch, cut in water colour.
            Color wc(0.016f, 0.062f, 0.055f, 0.95f);
            draw::Line(dc, brush, p.x, p.y, p.x + p.rx, p.y - p.ry * 0.10f, p.ry * 0.22f, wc);
            // A lighter crown where the pad catches light.
            brush->SetColor(D2D1::ColorF(0.30f, 0.44f, 0.25f, 0.20f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(p.x - p.rx * 0.15f, p.y - p.ry * 0.2f),
                                          p.rx * 0.55f, p.ry * 0.45f), brush);
            dc->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // Koi, with a soft shadow on the floor beneath each.
        for (const auto& k : m_koi) {
            float deg = k.angle * 180.0f / kPi;
            dc->SetTransform(D2D1::Matrix3x2F::Rotation(deg, D2D1::Point2F(k.x, k.y)));

            float rx = k.len * 0.5f, ry = k.len * 0.16f;
            Color body(0.93f, 0.91f, 0.88f, 0.96f);
            Color spotA(0.93f, 0.30f, 0.10f, 0.95f);
            Color spotB(0.16f, 0.13f, 0.12f, 0.85f);
            if (k.pattern == 1) { body = Color(0.95f, 0.45f, 0.16f, 0.96f); spotA = Color(0.98f, 0.72f, 0.30f, 0.9f); }
            else if (k.pattern == 2) { body = Color(0.90f, 0.72f, 0.30f, 0.96f); spotA = Color(0.98f, 0.86f, 0.55f, 0.9f); }
            else if (k.pattern == 3) { spotA = Color(0.18f, 0.16f, 0.15f, 0.9f); }

            // Shadow (drawn in the same rotated frame, offset downstream of the light).
            brush->SetColor(D2D1::ColorF(0, 0, 0, 0.22f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x - k.len * 0.08f, k.y + k.len * 0.10f),
                                          rx * 1.02f, ry * 0.95f), brush);

            // Tail: a forked pair of strokes wagging with the swim phase.
            float wag = std::sin(k.phase) * k.len * 0.10f;
            draw::Line(dc, brush, k.x - rx * 0.86f, k.y, k.x - rx - k.len * 0.20f, k.y - ry * 0.9f + wag,
                       k.len * 0.055f, body.WithAlpha(0.85f));
            draw::Line(dc, brush, k.x - rx * 0.86f, k.y, k.x - rx - k.len * 0.20f, k.y + ry * 0.9f + wag,
                       k.len * 0.055f, body.WithAlpha(0.85f));
            // Pectoral fins, tucked against the mid-body so they never read as
            // detached specks.
            brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, 0.60f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x, k.y - ry * 0.80f),
                                          k.len * 0.095f, k.len * 0.038f), brush);
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x, k.y + ry * 0.80f),
                                          k.len * 0.095f, k.len * 0.038f), brush);
            // Body.
            brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, body.a));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x, k.y), rx, ry), brush);
            // Pattern spots.
            brush->SetColor(D2D1::ColorF(spotA.r, spotA.g, spotA.b, spotA.a));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x - rx * 0.35f, k.y - ry * 0.2f),
                                          ry * 0.55f, ry * 0.5f), brush);
            brush->SetColor(D2D1::ColorF(spotB.r, spotB.g, spotB.b, spotB.a));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(k.x + rx * 0.30f, k.y + ry * 0.15f),
                                          ry * 0.42f, ry * 0.38f), brush);

            dc->SetTransform(D2D1::Matrix3x2F::Identity());
        }

        // Vignette: darker corners keep the icons readable.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.45f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    static constexpr int kMaxRings = 22;

    void ReadParams(const SceneCtx& ctx) {
        m_fish = ctx.config->sceneParam[0];
        m_drift = ctx.config->sceneParam[1];
        m_ripples = ctx.config->sceneParam[2];
        m_palette = ctx.config->sceneParam[3];
    }

    void SpawnRing(float x, float y, float maxR) {
        Ring r{};
        r.x = x; r.y = y;
        r.r = 3.0f;
        r.maxR = maxR;
        r.speed = 26.0f;
        r.alpha0 = 0.34f;
        m_rings.push_back(r);
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x40117u + ctx.variation * 6151u);

        m_koi.clear();
        int want = (int)((4 + 8 * m_fish) * ctx.density);
        want = std::min(want, 14);
        m_koi.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Koi k{};
            k.x = rng.Range(w * 0.1f, w * 0.9f);
            k.y = rng.Range(h * 0.1f, h * 0.9f);
            k.angle = rng.Range(0.0f, kTau);
            k.len = rng.Range(64.0f, 118.0f);
            k.speed = rng.Range(24.0f, 55.0f);
            k.turnBias = rng.Range(0.0f, 100.0f);
            k.phase = rng.Range(0.0f, kTau);
            k.wagFreq = rng.Range(4.5f, 7.5f);
            k.pattern = rng.Int(0, 3);
            if (m_palette < 0.35f) k.pattern = (i % 2 == 0) ? 1 : 2;      // orange/gold
            else if (m_palette > 0.65f) k.pattern = (i % 2 == 0) ? 0 : 3; // white/red, white/black
            k.rippleIn = rng.Range(1.0f, 6.0f);
            m_koi.push_back(k);
        }

        m_pads.clear();
        int pads = (int)(3 + 4 * ctx.density);
        for (int i = 0; i < pads; ++i) {
            Pad p{};
            // Bias pads toward the edges so the swimming space stays open.
            bool leftSide = rng.Unit() < 0.5f;
            p.x = leftSide ? rng.Range(0.02f, 0.30f) * w : rng.Range(0.70f, 0.98f) * w;
            p.y = rng.Range(0.05f, 0.95f) * h;
            p.rx = rng.Range(34.0f, 78.0f);
            p.ry = p.rx * rng.Range(0.68f, 0.88f);
            p.rot = rng.Range(0.0f, kTau);
            p.hue = rng.Range(0.28f, 0.36f);
            m_pads.push_back(p);
        }

        m_rings.clear();
        m_rings.reserve(kMaxRings);
        m_ringTimer = 0.5f;
        for (int i = 0; i < 6 && (int)m_rings.size() < kMaxRings; ++i) {
            Ring r{};
            r.x = rng.Range(0.08f, 0.92f) * w;
            r.y = rng.Range(0.10f, 0.90f) * h;
            r.maxR = rng.Range(50.0f, 140.0f);
            r.r = rng.Range(6.0f, r.maxR * 0.85f);
            r.speed = 26.0f;
            r.alpha0 = 0.34f;
            m_rings.push_back(r);
        }
    }

    std::vector<Koi> m_koi;
    std::vector<Pad> m_pads;
    std::vector<Ring> m_rings;
    float m_ringTimer = 0.5f;
    float m_time = 0;
    float m_fish = 0.5f, m_drift = 0.5f, m_ripples = 0.5f, m_palette = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateKoi() { return new KoiScene(); }

} // namespace lp::scenes
