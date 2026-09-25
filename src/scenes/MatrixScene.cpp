// LivePaper - "Digital Rain".
//
// The classic falling-glyph cascade. Trails are quantised cells (rounded rects with
// per-cell brightness, so they read as characters at wallpaper distance); only each
// column's head is drawn as real text, which keeps DrawText calls to ~one per column.
#include "Scenes.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Column {
    float x;
    float head;        // head position in cell units (float, falls with time)
    float speed;       // cells per second
    int trail;         // visible trail length in cells
    unsigned char bright[26];   // per-cell brightness jitter (0..255), head-relative
};

} // namespace

class MatrixScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Digital Rain"; }
    const wchar_t* Description() const override {
        return L"Falling glyph streams cascading down a dark field.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Density";
            case 1: return L"Speed";
            case 2: return L"Glow";
            case 3: return L"Hue";
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
        m_glyphPhase += dt * 6.0f;

        float speedScale = 0.35f + 1.5f * m_speed;
        float rows = ctx.height / m_cell;
        for (auto& c : m_cols) {
            c.head += c.speed * speedScale * dt;
            if (c.head - (float)c.trail > rows + 2.0f) {
                c.head = -(float)(c.trail / 3);
                c.speed = 6.0f + Fract(c.x * 0.013f + m_time * 0.01f) * 14.0f;
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        float hue = Fract(0.33f + (m_hue - 0.5f) * 0.85f);
        Color base = Color::Hsv(hue, 0.85f, 0.85f);
        Color head = Color::Hsv(hue, 0.25f, 1.0f);

        // Near-black base with a faint centre glow so the field has depth.
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(0.004f, 0.010f, 0.006f, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(0.002f, 0.005f, 0.003f, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
        draw::RadialGlow(dc, brush, w * 0.5f, h * 0.45f, w * 0.55f,
                         base.WithAlpha(0.05f + 0.06f * m_glow), 1.0f, 10);

        const float cw = m_cell * 0.72f;   // glyph cell width
        const float ch = m_cell * 0.82f;

        // Trail cells first, then heads, so heads are never overdrawn.
        for (const auto& c : m_cols) {
            float rows = h / m_cell;
            int headCell = (int)c.head;
            for (int k = 0; k < c.trail; ++k) {
                int row = headCell - k;
                if (row < 0 || row > (int)rows) continue;
                float t = 1.0f - (float)k / (float)c.trail;   // 1 at the head
                float jitter = c.bright[k % 26] / 255.0f;
                float a = std::pow(t, 1.6f) * (0.30f + 0.70f * jitter) + t * 0.06f;
                if (a < 0.02f) continue;
                float y = (float)row * m_cell;
                Color col = Color(base.r, base.g, base.b, a * (0.45f + 0.4f * m_glow + 0.35f * t));
                // The cell: a rounded rect with a notch of darker "counter" so it
                // reads as a glyph rather than a plain block.
                draw::RoundedRect(dc, brush, c.x, y + (m_cell - ch) * 0.5f, cw, ch, cw * 0.22f, col);
                if (jitter > 0.55f && k > 0 && k < c.trail - 2) {
                    draw::RoundedRect(dc, brush, c.x + cw * 0.28f, y + m_cell * 0.5f,
                                      cw * 0.44f, ch * 0.30f, cw * 0.10f,
                                      Color(0, 0, 0, a * 0.55f));
                }
            }
        }

        // Heads: one bright glyph per column.
        IDWriteFactory* dw = ctx.device ? ctx.device->DWrite() : nullptr;
        IDWriteTextFormat* fmt = nullptr;
        if (dw) {
            dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                 m_cell * 0.92f, L"en-us", &fmt);
        }
        if (fmt) fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        for (const auto& c : m_cols) {
            float y = (float)(int)c.head * m_cell;
            if (y < -m_cell || y > h) continue;
            // Soft halo behind the head.
            draw::RadialGlow(dc, brush, c.x + cw * 0.5f, y + m_cell * 0.5f, m_cell * 1.1f,
                             head.WithAlpha(0.22f + 0.30f * m_glow), 1.0f, 6);
            if (fmt) {
                brush->SetColor(D2D1::ColorF(head.r, head.g, head.b, 0.96f));
                dc->DrawText(kGlyphs[(unsigned)(c.x * 7.0f + m_glyphPhase + c.head) % 16], 1, fmt,
                             D2D1::RectF(c.x - m_cell, y, c.x + cw + m_cell, y + m_cell),
                             brush, D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
            } else {
                draw::RoundedRect(dc, brush, c.x, y + (m_cell - ch) * 0.5f, cw, ch, cw * 0.22f,
                                  head.WithAlpha(0.95f));
            }
        }
        if (fmt) fmt->Release();

        // Vignette keeps the desktop edges calm for icons.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.40f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_density = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        m_cols.clear();
        m_cell = std::max(16.0f, 22.0f * ctx.sceneScale);
        float spacing = m_cell * Lerp(1.55f, 1.02f, m_density);
        Rng rng(0x0A7A11u + ctx.variation * 104729u);
        float rows = ctx.height / m_cell;
        for (float x = 4.0f; x < ctx.width - spacing; x += spacing) {
            Column c{};
            c.x = x;
            c.speed = rng.Range(6.0f, 20.0f);
            c.trail = rng.Int(10, 22);
            c.head = rng.Range(-rows * 0.25f, rows * 0.92f);
            for (unsigned char& b : c.bright) b = (unsigned char)rng.Int(40, 255);
            m_cols.push_back(c);
        }
    }

    static const wchar_t* kGlyphs[16];

    std::vector<Column> m_cols;
    float m_time = 0, m_glyphPhase = 0;
    float m_cell = 22.0f;
    float m_density = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_hue = 0.0f;
    float m_lastW = 1920, m_lastH = 1080;
};

// Katakana-flavoured set (plus a few digits): the classic look, all system fonts.
const wchar_t* MatrixScene::kGlyphs[16] = {
    L"\u30A2", L"\u30A4", L"\u30A6", L"\u30A8", L"\u30AB", L"\u30AD", L"\u30AF",
    L"\u30B3", L"\u30B7", L"\u30B9", L"\u30BD", L"\u30BF", L"\u30C1", L"\u30C4",
    L"7", L"Z",
};

Scene* CreateMatrix() { return new MatrixScene(); }

} // namespace lp::scenes
