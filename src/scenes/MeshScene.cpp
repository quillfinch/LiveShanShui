// LivePaper - "Flow Mesh".
//
// The modern abstract-wallpaper look: a few large, soft colour fields that drift and
// fold into each other, blended additively so the overlaps bloom. Cheap to render and
// extremely forgiving of any resolution.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Blob {
    float cx, cy;      // centre as a fraction of screen
    float rx, ry;      // ellipse radii as a fraction of screen
    float hue;
    float sat;
    float ax, ay;      // Lissajous amplitudes
    float fx, fy;      // Lissajous frequencies
    float phase;
};

} // namespace

class MeshScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Flow Mesh"; }
    const wchar_t* Description() const override {
        return L"Slowly folding colour fields in an additive gradient mesh.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Speed";
            case 1: return L"Saturation";
            case 2: return L"Glow";
            case 3: return L"Hue";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildBlobs(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);
        m_time += dt * (0.20f + m_speed * 0.85f);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Near-black base: additive blending needs darkness to bloom against.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        D2D1_GRADIENT_STOP base[2];
        base[0].position = 0.0f; base[0].color = D2D1::ColorF(0.008f, 0.006f, 0.020f, 1);
        base[1].position = 1.0f; base[1].color = D2D1::ColorF(0.014f, 0.010f, 0.034f, 1);
        draw::VerticalGradient(dc, w, h, base, 2);

        // Blobs additively blended so overlaps bloom instead of occluding.
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        for (const auto& b : m_blobs) {
            float cx = b.cx * w + std::sin(m_time * b.fx + b.phase) * b.ax * w;
            float cy = b.cy * h + std::cos(m_time * b.fy + b.phase * 1.3f) * b.ay * h;
            float rx = b.rx * w;
            float ry = b.ry * h;
            Color c = Color::Hsv(Fract(b.hue + m_hue), b.sat, 1.0f, 0.16f + 0.20f * m_glow);

            // An ellipse is approximated by drawing a few concentric filled ellipses at
            // falling alpha, which D2D renders cheaply and which smooths the falloff
            // (a hard-edged ellipse would look like a sticker).
            const int rings = 10;
            for (int i = rings; i >= 1; --i) {
                float t = (float)i / (float)rings;
                float a = c.a * (1.0f - t) * (1.0f - t);
                if (a < 0.002f) continue;
                brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, a));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx * t, ry * t), brush);
            }
        }
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

        // Soft grain/vignette so the desktop icons stay readable at the edges.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.01f, 0.00f, 0.03f, 0.42f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_speed = ctx.config->sceneParam[0];
        m_sat = 0.45f + ctx.config->sceneParam[1] * 0.45f;
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void BuildBlobs(const SceneCtx& ctx) {
        m_blobs.clear();
        Rng rng(134845u + ctx.variation * 7919u);
        // A trio of large fields, plus a couple of smaller accents.
        int count = 5;
        float hues[5] = { 0.90f, 0.55f, 0.05f, 0.68f, 0.30f };
        for (int i = 0; i < count; ++i) {
            Blob b{};
            b.cx = rng.Range(0.30f, 0.70f);
            b.cy = rng.Range(0.35f, 0.65f);
            b.rx = rng.Range(0.28f, 0.46f);
            b.ry = rng.Range(0.30f, 0.48f);
            b.hue = hues[i % 5];
            b.sat = 0.55f;
            b.ax = rng.Range(0.08f, 0.22f);
            b.ay = rng.Range(0.06f, 0.18f);
            b.fx = rng.Range(0.10f, 0.30f);
            b.fy = rng.Range(0.08f, 0.24f);
            b.phase = rng.Range(0.0f, kTau);
            m_blobs.push_back(b);
        }
        (void)ctx;
    }

    std::vector<Blob> m_blobs;
    float m_time = 0;
    float m_speed = 0.5f, m_sat = 0.7f, m_glow = 0.5f, m_hue = 0.0f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateMesh() { return new MeshScene(); }

} // namespace lp::scenes
