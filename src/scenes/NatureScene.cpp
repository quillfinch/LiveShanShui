// LivePaper - "Nature".
//
// A calm layered landscape: sky gradient with drifting clouds, distant misty ridges,
// a procedural tree line, gentle hills, and falling leaves. Meant to be restful
// behind icons, so contrast stays low and motion stays slow.
//
// Performance: ridges and trees are generated once into cached polylines/silhouettes;
// per frame only clouds, leaves and a soft light bloom move.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>


namespace lp::scenes {

namespace {

struct Cloud { float x, y, scale, speed, alpha; };
struct Leaf  { float x, y, vx, vy, rot, rotSpeed, size, hue; };
struct Tree  { float x, baseY, height, width, tilt, hueShift; };
struct Ridge { std::vector<float> heights; float y; };
struct CanopyBlob { float x, y, r, hueShift; };

} // namespace

class NatureScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Nature"; }
    const wchar_t* Description() const override {
        return L"Misty ridges, drifting clouds and falling leaves.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Leaves";
            case 1: return L"Wind";
            case 2: return L"Haze";
            case 3: return L"Palette";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildRidges(ctx);
        BuildTrees(ctx);
        BuildCanopy(ctx);
        BuildClouds(ctx);
        BuildLeaves(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f)
            Configure(ctx);

        const float wind = (m_wind - 0.5f) * 2.0f;   // -1 .. 1
        for (auto& c : m_clouds) {
            c.x += c.speed * (0.30f + m_wind * 0.9f) * dt * 20.0f;
            if (c.x > ctx.width + c.scale * 260.0f) {
                c.x = -c.scale * 260.0f;
                c.y = ctx.height * (0.05f + 0.16f * (float)((int)(c.scale * 100.0f) % 7) / 7.0f);
            }
        }
        for (auto& l : m_leaves) {
            l.x += (l.vx + wind * 26.0f) * dt;
            l.y += l.vy * dt;
            l.rot += l.rotSpeed * dt;
            // Sway as if catching thermals.
            l.x += std::sin(ctx.time * 1.4f + l.rot * 2.0f) * 9.0f * dt;
            if (l.y > ctx.height + 20.0f) {
                l.y = -20.0f;
                l.x = (float)((int)(l.x * 31.0f + l.rot * 977.0f) % std::max(1, (int)ctx.width));
                if (l.x < 0) l.x += ctx.width;
            }
            if (l.x > ctx.width + 30.0f) l.x = -20.0f;
            if (l.x < -30.0f) l.x = ctx.width + 20.0f;
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Palette shift lets the user warm or cool the whole scene.
        const float p = m_palette;
        Color skyTop    = Color::Hsv(0.56f - p * 0.05f, 0.55f, 0.92f).Mix(Color::Hsv(0.60f, 0.40f, 0.55f), p * 0.6f);
        Color skyMid    = Color::Hsv(0.52f - p * 0.03f, 0.45f, 0.98f);
        Color skyHorizon = Color::Hsv(0.10f + p * 0.04f, 0.35f, 1.00f);

        D2D1_GRADIENT_STOP sky[4];
        sky[0].position = 0.0f;  sky[0].color = D2D1::ColorF(skyTop.r, skyTop.g, skyTop.b, 1);
        sky[1].position = 0.45f; sky[1].color = D2D1::ColorF(skyMid.r, skyMid.g, skyMid.b, 1);
        sky[2].position = 0.68f; sky[2].color = D2D1::ColorF(skyHorizon.r, skyHorizon.g, skyHorizon.b, 1);
        sky[3].position = 1.0f;  sky[3].color = D2D1::ColorF(0.36f, 0.45f, 0.28f, 1);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // Sun bloom low on the horizon.
        float sunX = w * 0.24f, sunY = h * 0.58f;
        draw::RadialGlow(dc, brush, sunX, sunY, h * 0.52f,
                         Color::Hsv(0.13f, 0.30f, 1.0f, 0.30f + 0.25f * m_haze), 1.0f, 24);
        draw::Circle(dc, brush, sunX, sunY, h * 0.030f, Color(1.0f, 0.96f, 0.85f, 0.55f));

        // --- clouds ----------------------------------------------------------
        // These are meant to be haze, not objects: they use elongated low ellipses at
        // low opacity. Round, opaque circles read as soap bubbles against the sky.
        for (const auto& c : m_clouds) {
            float baseY = c.y;
            float baseW = c.scale * 190.0f;
            for (int i = 0; i < 6; ++i) {
                float t = (float)i / 5.0f;
                // Flattened: wide but short, so the cluster reads as a soft bank.
                float rx = baseW * (0.16f + 0.20f * std::sin(t * kPi));
                float ry = c.scale * (5.0f + 7.0f * std::sin(t * kPi));
                float cx = c.x + (t - 0.5f) * baseW;
                float cy = baseY + std::sin(t * 3.0f) * c.scale * 5.0f;
                float edge = 1.0f - std::fabs(t - 0.5f) * 2.0f;
                float alpha = c.alpha * 0.16f * (0.45f + 0.55f * edge);
                // Ellipses need a fill helper; two offset circles approximate one.
                draw::Circle(dc, brush, cx - rx * 0.5f, cy, ry, Color(1, 1, 1, alpha));
                draw::Circle(dc, brush, cx + rx * 0.5f, cy, ry, Color(1, 1, 1, alpha));
                draw::Circle(dc, brush, cx, cy, (rx + ry) * 0.35f, Color(1, 1, 1, alpha * 0.9f));
            }
        }

        // --- ridges, far to near --------------------------------------------
        for (size_t i = 0; i < m_ridges.size(); ++i) {
            const Ridge& r = m_ridges[i];
            float depth = (float)i / (float)std::max<size_t>(1, m_ridges.size() - 1);
            // Distant ridges are pale and hazy; near ones are dark and saturated.
            // (`far`/`near` are Win32 macros, hence the suffix.)
            Color farTone = Color::Hsv(0.52f, 0.22f, 0.86f);
            Color nearTone = Color::Hsv(0.30f - p * 0.04f, 0.45f, 0.36f);
            Color body = farTone.Mix(nearTone, depth);
            body.a = 0.85f + 0.15f * depth;
            // Haze thickens the distance.
            body = body.Mix(Color::Hsv(0.55f, 0.10f, 0.95f), (1.0f - depth) * m_haze * 0.55f);
            FillRidge(dc, brush, r, body);
        }

        // --- ground fog ------------------------------------------------------
        // Drawn BEFORE the trees: it is atmospheric haze sitting in the hills, and
        // drawing it afterwards washed a pale band straight across the trunks.
        D2D1_GRADIENT_STOP fog[2];
        fog[0].position = 0.0f; fog[0].color = D2D1::ColorF(0.85f, 0.92f, 0.88f, 0.00f);
        fog[1].position = 1.0f; fog[1].color = D2D1::ColorF(0.80f, 0.90f, 0.85f, 0.14f * m_haze + 0.05f);
        {
            float bandTop = h * 0.50f, bandH = h * 0.18f;
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(fog, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                gp.startPoint = D2D1::Point2F(0, bandTop);
                gp.endPoint = D2D1::Point2F(0, bandTop + bandH);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                    dc->FillRectangle(D2D1::RectF(0, bandTop, w, bandTop + bandH), gb);
                    gb->Release();
                }
                coll->Release();
            }
        }

        // --- trees: canopy first, then branches over it so limbs stay visible ----
        for (const auto& b : m_canopy) {
            Color base = Color::Hsv(0.30f - p * 0.05f + b.hueShift * 0.04f,
                                    0.34f + b.hueShift * 0.08f,
                                    0.28f + b.hueShift * 0.14f,
                                    0.88f);
            draw::Circle(dc, brush, b.x, b.y, b.r, base);
            // A lighter offset blob gives each cluster a lit side.
            draw::Circle(dc, brush, b.x - b.r * 0.18f, b.y - b.r * 0.24f, b.r * 0.60f,
                         Color(base.r * 1.40f, base.g * 1.32f, base.b * 1.12f, 0.70f));
        }
        for (const auto& t : m_trees) {
            // Lighter, softer trunks: very dark saturate trunks punch holes through
            // the canopy instead of reading as part of the tree.
            Color trunk = Color::Hsv(0.08f, 0.24f, 0.44f + t.hueShift * 0.08f);
            Color foliage = Color::Hsv(0.30f - p * 0.05f + t.hueShift * 0.03f,
                                       0.40f, 0.30f + t.hueShift * 0.08f);
            DrawTree(dc, brush, t, trunk, foliage, 0);
        }

        // --- leaves ----------------------------------------------------------
        for (const auto& l : m_leaves) {
            float s = l.size;
            Color c = Color::Hsv(0.09f + l.hue * 0.13f, 0.55f, 0.85f, 0.75f);
            // A leaf is two arcs; approximating with a rotated ellipse keeps it cheap.
            float cw = s * (0.9f + 0.25f * std::sin(l.rot));
            float ch = s * 0.55f;
            // Rotation via the geometry transform would need a matrix per leaf; instead
            // we vary the aspect with the rotation, which reads the same at this size.
            draw::Circle(dc, brush, l.x, l.y, (cw + ch) * 0.5f, c);
        }

        // --- soft bottom vignette for icon legibility ------------------------
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0.05f, 0.10f, 0.05f, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_leafAmount = ctx.config->sceneParam[0];
        m_wind = ctx.config->sceneParam[1];
        m_haze = ctx.config->sceneParam[2];
        m_palette = ctx.config->sceneParam[3];
    }

    void BuildRidges(const SceneCtx& ctx) {
        m_ridges.clear();
        Rng rng(5150u + ctx.variation * 7919u);
        const int layerCount = 4;
        const int samples = 64;
        for (int layer = 0; layer < layerCount; ++layer) {
            Ridge r{};
            float depth = (float)layer / (float)(layerCount - 1);
            r.y = ctx.height * (0.46f + depth * 0.30f);
            float amp = ctx.height * (0.16f - depth * 0.09f);
            float freq = 1.0f + depth * 1.4f;
            float phase = rng.Range(0.0f, 100.0f);
            r.heights.resize(samples + 1);
            for (int i = 0; i <= samples; ++i) {
                float t = (float)i / (float)samples;
                // Two octaves of smooth noise make believable ridgelines.
                float n = Fbm(t * freq * 2.4f + phase, depth * 7.0f, 3);
                r.heights[i] = r.y + n * amp;
            }
            m_ridges.push_back(std::move(r));
        }
    }

    void BuildTrees(const SceneCtx& ctx) {
        m_trees.clear();
        Rng rng(24680u + ctx.variation * 7919u);
        // Fewer, larger trees: many small ones just add visual noise above the icons.
        int count = (int)(6 + 10 * (ctx.width / 1920.0f));
        float bandY = ctx.height * 0.80f;
        for (int i = 0; i < count; ++i) {
            Tree t{};
            t.x = rng.Range(-40.0f, ctx.width + 40.0f);
            t.baseY = bandY + rng.Range(-ctx.height * 0.05f, ctx.height * 0.12f);
            t.height = ctx.height * rng.Range(0.13f, 0.26f);
            t.width = t.height * rng.Range(0.34f, 0.55f);
            t.tilt = rng.Range(-0.10f, 0.10f);
            t.hueShift = rng.Unit();
            m_trees.push_back(t);
        }
        std::sort(m_trees.begin(), m_trees.end(),
                  [](const Tree& a, const Tree& b) { return a.baseY < b.baseY; });
    }

    void BuildClouds(const SceneCtx& ctx) {
        m_clouds.clear();
        Rng rng(13579u + ctx.variation * 7919u);
        int count = (int)(5 + 7 * (ctx.width / 1920.0f));
        for (int i = 0; i < count; ++i) {
            Cloud c{};
            c.x = rng.Range(-200.0f, ctx.width + 200.0f);
            c.y = ctx.height * rng.Range(0.05f, 0.34f);
            c.scale = rng.Range(0.55f, 1.35f);
            c.speed = rng.Range(0.4f, 1.5f);
            c.alpha = rng.Range(0.30f, 0.70f);
            m_clouds.push_back(c);
        }
    }

    void BuildLeaves(const SceneCtx& ctx) {
        m_leaves.clear();
        int want = (int)(24 + 90 * m_leafAmount);
        want = std::min(want, 160);
        Rng rng(8642u + ctx.variation * 7919u);
        for (int i = 0; i < want; ++i) {
            Leaf l{};
            l.x = rng.Range(0.0f, ctx.width);
            l.y = rng.Range(-ctx.height, ctx.height);
            l.vx = rng.Range(-8.0f, 14.0f);
            l.vy = rng.Range(14.0f, 42.0f);
            l.rot = rng.Range(0.0f, kTau);
            l.rotSpeed = rng.Range(-1.6f, 1.6f);
            l.size = rng.Range(3.0f, 7.5f);
            l.hue = rng.Unit();
            m_leaves.push_back(l);
        }
    }

    // Fills the area below a ridge polyline down to the bottom of the screen.
    void FillRidge(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   const Ridge& r, const Color& color) {
        if (r.heights.size() < 2) return;
        ID2D1Factory* factory = nullptr;
        dc->GetFactory(&factory);
        if (!factory) return;
        ID2D1PathGeometry* geo = nullptr;
        if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink)) && sink) {
            float w = m_lastW;
            float step = w / (float)(r.heights.size() - 1);
            sink->BeginFigure(D2D1::Point2F(0, r.heights[0]), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 1; i < r.heights.size(); ++i) {
                sink->AddLine(D2D1::Point2F((float)i * step, r.heights[i]));
            }
            sink->AddLine(D2D1::Point2F(w, m_lastH + 10.0f));
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

    // Builds the canopy as a deterministic set of blobs clustered around the crown.
    // Doing this once (rather than deriving it from the recursive branch walk) keeps
    // the foliage dense and the per-frame cost flat.
    void BuildCanopy(const SceneCtx& ctx) {
        m_canopy.clear();
        Rng rng(77123u + ctx.variation * 7919u);
        for (const auto& t : m_trees) {
            // The crown centre sits low enough that the canopy overlaps the trunk top.
            // A visible gap between canopy and trunk is what made trunks look like posts.
            float crownY = t.baseY - t.height * 0.60f;
            float crownRx = t.width * 1.65f;
            float crownRy = t.height * 0.36f;
            int blobs = (int)(9.0f + t.height * 0.12f);
            blobs = std::max(7, std::min(blobs, 22));
            for (int i = 0; i < blobs; ++i) {
                // Polar placement, biased outward at the sides for a leafy silhouette.
                float ang = rng.Range(0.0f, kTau);
                float rad = std::sqrt(rng.Unit());
                CanopyBlob b{};
                b.x = t.x + std::cos(ang) * rad * crownRx;
                b.y = crownY + std::sin(ang) * rad * crownRy + t.height * 0.05f;
                b.r = t.height * rng.Range(0.13f, 0.22f);
                b.hueShift = t.hueShift;
                m_canopy.push_back(b);
            }
            (void)ctx;
        }
    }

    // Recursive tree silhouette. `depth` caps recursion so cost stays bounded.
    void DrawTree(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                  const Tree& t, const Color& trunk, const Color& foliage, int depth) {
        // A real trunk first: a short, chunky, gently tapered quad. A long thin taper
        // reads as a spike once the canopy sits on top of it.
        float trunkH = t.height * 0.34f;
        float trunkTop = t.baseY - trunkH;
        float baseW = t.width * 0.30f;
        float topW = t.width * 0.20f;
        float topX = t.x + t.tilt * trunkH;
        DrawTaperedQuad(dc, brush, t.x, t.baseY, baseW, topX, trunkTop, topW, trunk);

        // Branches: thin strokes only, so overlapping limbs do not merge into blocks.
        DrawBranch(dc, brush, topX, trunkTop, t.width * 0.16f, t.height * 0.50f,
                   t.tilt, 0, trunk, foliage, depth);
    }

    // Fills a tapered quad between a wide base and a narrower top.
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

    void DrawBranch(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                    float x, float y, float halfWidth, float length, float tilt,
                    int depth, const Color& trunk, const Color& foliage, int maxDepth) {
        if (length < 4.0f || depth > maxDepth) return;
        float topX = x + tilt * length;
        float topY = y - length;

        // Branches taper to near hair-width at the tips and are drawn as strokes, not
        // circles: a circle at every node is what made the earlier version look like
        // a scaffold of posts.
        float strokeW = std::max(1.1f, halfWidth * (depth == 0 ? 0.85f : 0.55f));
        Color c = depth <= 1 ? trunk : foliage;
        draw::Line(dc, brush, x, y, topX, topY, strokeW, c);

        float nextLen = length * 0.70f;
        float nextW = halfWidth * 0.58f;
        DrawBranch(dc, brush, topX, topY, nextW, nextLen, tilt - 0.40f, depth + 1, trunk, foliage, maxDepth);
        DrawBranch(dc, brush, topX, topY, nextW, nextLen, tilt + 0.36f, depth + 1, trunk, foliage, maxDepth);
        if (depth >= 2)
            DrawBranch(dc, brush, topX, topY, nextW * 0.75f, nextLen * 0.85f, tilt * 0.4f,
                       depth + 1, trunk, foliage, maxDepth);
    }

    std::vector<Ridge> m_ridges;
    std::vector<Tree> m_trees;
    std::vector<CanopyBlob> m_canopy;
    std::vector<Cloud> m_clouds;
    std::vector<Leaf> m_leaves;
    float m_leafAmount = 0.5f, m_wind = 0.5f, m_haze = 0.5f, m_palette = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateNature() { return new NatureScene(); }

} // namespace lp::scenes
