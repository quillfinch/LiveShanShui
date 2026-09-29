// LivePaper - "Hive".
//
// Generative hex-cell mosaic. A unit hexagon is built once as a cached path
// geometry (released in the destructor - the one scene that owns COM state);
// every frame each cell just sets a translate+scale transform and fills it.
// Waves of palette color sweep diagonally across the grid, driven by position
// and time, so the seed or a custom color re-tiles the whole honeycomb.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

class HiveScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Hive"; }
    const wchar_t* Description() const override {
        return L"A hex-cell mosaic rippling with waves of color.";
    }
    bool SupportsCustomColor() const override { return true; }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Cell size";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Contrast";
            default: return L"";
        }
    }

    HiveScene() = default;
    ~HiveScene() override {
        if (m_hex) { m_hex->Release(); m_hex = nullptr; }
    }
    HiveScene(const HiveScene&) = delete;
    HiveScene& operator=(const HiveScene&) = delete;

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
        m_time += dt * (0.25f + 0.9f * m_speed);
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        if (!dc || !m_hex) return;
        const float w = ctx.width, h = ctx.height;
        Palette pal = MakePalette(ctx);
        float bh = 0, bs = 0, bv = 0;
        pal.c[0].ToHsv(bh, bs, bv);

        Color ground = Color::Hsv(bh, bs * 0.85f, 0.055f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(ground.r, ground.g, ground.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(ground.r * 0.6f, ground.g * 0.6f, ground.b * 0.6f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);

        // Cells: flat-top hexagons, colour picked by a diagonal position wave.
        for (const auto& c : m_cells) {
            float wave = std::sin(c.x * 0.0032f + c.y * 0.0021f + m_time * 1.1f) * 0.5f + 0.5f;
            int idx = std::min((int)(wave * (float)Palette::kColors), Palette::kColors - 1);
            Color col = pal.c[idx];
            // A second, slower wave modulates brightness so cells shimmer.
            float lift = 0.74f + 0.26f * std::sin(c.x * 0.0018f - c.y * 0.0026f - m_time * 0.6f);
            float a = Lerp(0.95f, 0.55f, m_contrast) * (0.55f + 0.45f * wave) * (0.85f + 0.45f * m_glow) * lift;
            float sz = m_cell * 0.92f;
            // D2D composes row-vector style: the scale must come first so the
            // translation is not scaled along with the unit hexagon.
            dc->SetTransform(D2D1::Matrix3x2F::Scale(sz, sz) *
                             D2D1::Matrix3x2F::Translation(c.x, c.y));
            brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, a));
            dc->FillGeometry(m_hex, brush, nullptr);
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());

        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_cellAmt = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_contrast = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        const float minDim = std::min(w, h);
        Rng rng(0x4017E5u + ctx.variation * 7919u);

        // Cell size: bigger cells for a sparser wall.
        m_cell = minDim * Lerp(0.075f, 0.040f, m_cellAmt);
        float hexW = m_cell * 1.7320508f;   // sqrt(3): flat-to-flat
        float rowH = m_cell * 1.5f;

        m_cells.clear();
        int cols = (int)(w / hexW) + 2;
        int rows = (int)(h / rowH) + 2;
        m_cells.reserve((size_t)(cols * rows));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float x = hexW * (float)c + ((r % 2) ? hexW * 0.5f : 0.0f);
                float y = rowH * (float)r;
                m_cells.push_back(D2D1::Point2F(x, y));
            }
        }
        // A little layout variation per seed: every 7th cell flips its wave phase.
        rng.Range(0.0f, 1.0f);

        // (Re)build the cached unit hexagon on every Configure: Configure always
        // runs right after the device (and its factory) was created or confirmed,
        // so a geometry cached from an earlier factory can never be used here.
        // Flat-top hexagon: vertices at 0, 60, ..., 300 degrees.
        if (m_hex) { m_hex->Release(); m_hex = nullptr; }
        if (ctx.dc) {
            ID2D1Factory* factory = nullptr;
            ctx.dc->GetFactory(&factory);
            if (factory) {
                if (SUCCEEDED(factory->CreatePathGeometry(&m_hex)) && m_hex) {
                    ID2D1GeometrySink* sink = nullptr;
                    if (SUCCEEDED(m_hex->Open(&sink)) && sink) {
                        sink->BeginFigure(D2D1::Point2F(1.0f, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
                        D2D1_POINT_2F pts[5];
                        for (int k = 1; k <= 5; ++k) {
                            float a = kTau * (float)k / 6.0f;
                            pts[k - 1] = D2D1::Point2F(std::cos(a), std::sin(a));
                        }
                        sink->AddLines(pts, 5);
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close();
                        sink->Release();
                    } else {
                        m_hex->Release();
                        m_hex = nullptr;
                    }
                }
                factory->Release();
            }
        }
    }

    std::vector<D2D1_POINT_2F> m_cells;
    ID2D1PathGeometry* m_hex = nullptr;   // unit hexagon, cached
    float m_cell = 60.0f;
    float m_time = 0;
    float m_cellAmt = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_contrast = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateHive() { return new HiveScene(); }

} // namespace lp::scenes
