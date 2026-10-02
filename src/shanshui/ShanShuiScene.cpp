// Live Shan Shui - scene implementation. See ShanShuiScene.h.
#include "ShanShuiScene.h"
#include "../gfx/GfxDevice.h"
#include <algorithm>
#include <cmath>

namespace {

} // namespace

namespace lp {

namespace {

// Painting world constants (the original's view was 800 world-units tall).
constexpr float kWindy = 800.0f;
constexpr int kTileCount = 3;

// Brush-layering exponents: raising alpha through 1-(1-a)^k matches stacking k
// translucent passes of the same stroke, which is how layered ink actually
// darkens (plain source-over converges far too slowly to ever read black).
// Real ink lines sit darker than tonal washes, so crisp strokes take a much
// stronger exponent than filled washes.
constexpr float kInkLayerFill = 1.2f;
constexpr float kInkLayerStroke = 2.35f;

// A tile bake may spend at most this much time per frame; whatever is left
// resumes on the next update. This is what turns a one-off ~10 ms hitch into
// work that is invisible next to the frame budget.
constexpr double kSliceBudgetSec = 0.0025;

double NowSec() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER n;
    QueryPerformanceCounter(&n);
    return (double)n.QuadPart / (double)freq.QuadPart;
}

struct Tile {
    ID2D1Bitmap1* bmp = nullptr;
    float worldX = 0;
    bool valid = false;
    bool baking = false;       // partially painted; nextItem is the resume point
    size_t nextItem = 0;
    void Release() {
        if (bmp) bmp->Release();
        bmp = nullptr;
        valid = false;
        baking = false;
        nextItem = 0;
    }
};

} // namespace

class ShanShuiScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Shan Shui"; }
    const wchar_t* Description() const override {
        return L"An endless hand-painted Chinese landscape scroll.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Scroll speed";
            case 1: return L"Mist";
            case 2: return L"Vegetation";
            case 3: return L"Ink";
            default: return L"";
        }
    }
    bool SupportsCustomColor() const override { return true; }

    void Configure(const SceneCtx& ctx) override;
    void Update(const SceneCtx& ctx, float dt) override;
    void Draw(const SceneCtx& ctx) override;

private:
    void ReadParams(const SceneCtx& ctx);
    void EnsureBitmaps(const SceneCtx& ctx);
    // Paints one bounded slice of a tile. Returns true when the tile is complete.
    bool BakeTileStep(const SceneCtx& ctx, int index, float worldX, double deadline);
    void DrawShape(ID2D1Factory* factory, const ss::Shape& sh, float inkScale);

    ss::World world_;
    ss::Paint paint_;

    float camX = 0;              // world units at the view's left edge
    float speed = 24.0f;         // world units / second
    float mist = 0.5f;
    float inkAmt = 0.8f;         // global ink alpha multiplier
    float viewScale = 1.0f;      // screen px per world unit
    float tileWorldW = 1422.0f;  // world units covered by one tile
    uint32_t lastVariation = 0xFFFFFFFFu;

    // What the live world was BUILT from. Everything else in the config (scroll
    // speed, mist, ink, dim...) can be applied in place; any change here needs a
    // full world rebuild (colors are baked into shapes at generation time).
    float appliedVeg = -1.0f;
    unsigned appliedColorKey = 0xFFFFFFFFu;
    bool worldLive = false;
    // Ink/mist values the current tile bitmaps were painted with; a mismatch
    // marks them for an (amortized) re-bake instead of a rebuild.
    float tileInk = -1.0f, tileMist = -1.0f;

    Tile tiles[kTileCount];
    ID2D1Bitmap* paperBmp = nullptr;   // 256x256 paper swatch, tiled at bake time
    uint32_t paperForVariation = 0;
    float paperForScale = 0;

    // Scratch pointers used only inside a bake (set/cleared around the loop).
    ID2D1DeviceContext* bakeDc = nullptr;
    ID2D1SolidColorBrush* bakeBrush = nullptr;
};

Scene* CreateShanShuiScene() { return new ShanShuiScene(); }

void ShanShuiScene::ReadParams(const SceneCtx& ctx) {
    const Config& cfg = *ctx.config;
    speed = 4.0f + cfg.sceneParam[0] * 60.0f;
    mist = Clamp01(cfg.sceneParam[1]);
    paint_.veg = 0.25f + cfg.sceneParam[2] * 1.75f;
    inkAmt = 0.5f + cfg.sceneParam[3] * 0.85f;

    // Paper and ink: warm defaults, tinted by the custom color when enabled.
    // The ink sits near black (a loaded brush): translucent washes still read
    // as wash, but strokes and dense texture areas darken toward true sumi.
    if (ctx.useCustomColor) {
        float h = 0, s = 0, v = 0;
        ctx.customColor.ToHsv(h, s, v);
        paint_.paper = Color::Hsv(h, Clamp01(s) * 0.22f, 0.955f);
        paint_.ink = Color::Hsv(h, std::min(Clamp01(s), 0.35f), 0.16f);
    } else {
        paint_.paper = Color::Hex(0xf6f1e3);
        paint_.ink = Color::Hex(0x262420);
    }
    paint_.scale = 1.0f;
}

void ShanShuiScene::Configure(const SceneCtx& ctx) {
    ReadParams(ctx);
    float vs = ctx.height / kWindy;
    if (vs <= 0) vs = 1.0f;
    float tww = ctx.width / vs;
    unsigned colorKey = (ctx.useCustomColor ? 0x80000000u : 0u) |
                        ((unsigned)(ctx.customColor.r * 255.0f) << 16) |
                        ((unsigned)(ctx.customColor.g * 255.0f) << 8) |
                        ((unsigned)(ctx.customColor.b * 255.0f));

    bool structural = !worldLive || vs != viewScale || tww != tileWorldW ||
                      ctx.variation != lastVariation || colorKey != appliedColorKey ||
                      paint_.veg != appliedVeg;
    viewScale = vs;
    tileWorldW = tww;
    lastVariation = ctx.variation;
    appliedColorKey = colorKey;
    appliedVeg = paint_.veg;

    if (!structural) {
        // Colors or bake settings moved: keep the world and the camera exactly
        // where they are and repaint the tiles in place (amortized below).
        if (std::fabs(inkAmt - tileInk) > 0.001f || std::fabs(mist - tileMist) > 0.001f) {
            for (Tile& t : tiles) {
                t.valid = false;
                t.baking = false;
                t.nextItem = 0;
            }
            tileInk = inkAmt;
            tileMist = mist;
        }
        return;
    }

    camX = 0;
    uint32_t seed = ctx.variation ^ 0x5A5A5A5Au;
    world_.Reset(seed, paint_);
    worldLive = true;

    for (Tile& t : tiles) t.Release();
    if (paperBmp) { paperBmp->Release(); paperBmp = nullptr; }
    paperForVariation = 0;
    tileInk = inkAmt;
    tileMist = mist;

    world_.Ensure(camX, camX + tileWorldW);
}

void ShanShuiScene::Update(const SceneCtx& ctx, float dt) {
    ReadParams(ctx);
    if (ctx.variation != lastVariation) Configure(ctx);   // structural rebuild; keep going
    camX += speed * dt;
    // Over-generate ~1700 world units past the view plus one full tile of
    // prefetch: a tile may only be baked once every item that could spill into
    // it (features reach ~1600 leftward) exists. Items are immutable after
    // creation, so the tile is then final and seams between tiles stay invisible.
    world_.Ensure(camX, camX + 3.0f * tileWorldW + 1700.0f);
    world_.Trim(camX - 2500.0f);

    // Tiles bake here, not in Draw(): the device wraps BeginScene/EndScene in a
    // D2D BeginDraw/EndDraw pair, and D2D forbids a nested one.
    EnsureBitmaps(ctx);
    int k0 = (int)std::floor(camX / tileWorldW);
    int k1 = (int)std::floor((camX + tileWorldW - 0.001f) / tileWorldW);
    if (k1 < k0) k1 = k0;
    if (k1 - k0 > 1) k0 = k1 - 1;
    // Visible tiles first, then the tile just past the right edge so it is
    // fully painted long before the scroll arrives. Anything the view can see
    // must finish in full, budget or not (it is normally already complete from
    // prefetch); only the prefetch tile ahead of the view may stop mid-slice,
    // which is what keeps a bake from ever hitching a live frame.
    for (int k = k0; k <= k1 + 1; ++k) {
        int slot = ((k % kTileCount) + kTileCount) % kTileCount;
        float wx = (float)k * tileWorldW;
        if (tiles[slot].valid && tiles[slot].worldX == wx) continue;
        double deadline = NowSec() + (k <= k1 ? 1.0 : kSliceBudgetSec);
        if (!BakeTileStep(ctx, slot, wx, deadline)) break;
    }
}

void ShanShuiScene::EnsureBitmaps(const SceneCtx& ctx) {
    // One paper swatch; tiles are screen-sized bitmaps rebuilt on demand.
    if (!paperBmp || paperForVariation != lastVariation || paperForScale != viewScale) {
        if (paperBmp) { paperBmp->Release(); paperBmp = nullptr; }
        std::vector<uint32_t> px;
        ss::PaperTile(px, 256, lastVariation ^ 0xC0FFEEu);
        D2D1_BITMAP_PROPERTIES props{};
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        ctx.dc->CreateBitmap(D2D1::SizeU(256, 256), px.data(), 256 * sizeof(uint32_t),
                             props, &paperBmp);
        paperForVariation = lastVariation;
        paperForScale = viewScale;
    }
    for (Tile& t : tiles) {
        if (t.bmp) continue;
        // Only bitmaps carrying D2D1_BITMAP_OPTIONS_TARGET may be SetTarget()ed.
        D2D1_BITMAP_PROPERTIES1 props1{};
        props1.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props1.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props1.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        ctx.dc->CreateBitmap(
            D2D1::SizeU((UINT32)ctx.width, (UINT32)ctx.height), nullptr, 0, &props1, &t.bmp);
        t.valid = false;
    }
}

void ShanShuiScene::DrawShape(ID2D1Factory* factory, const ss::Shape& sh, float inkScale) {
    if (sh.pts.size() < 2) return;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) return;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geo->Open(&sink)) || !sink) { geo->Release(); return; }
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(D2D1::Point2F(sh.pts[0].x, sh.pts[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < sh.pts.size(); ++i)
        sink->AddLine(D2D1::Point2F(sh.pts[i].x, sh.pts[i].y));
    sink->EndFigure(sh.fill ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();

    ID2D1SolidColorBrush* brush = bakeBrush;
    if (sh.fill) {
        Color c = sh.fil;
        // Paper masks stay opaque; ink washes follow the ink control.
        if (!sh.paper) {
            float a = Clamp01(c.a * inkScale);
            c.a = 1.0f - std::pow(1.0f - a, kInkLayerFill);
        }
        brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
        bakeDc->FillGeometry(geo, brush);
    }
    if (sh.stroke && sh.wid > 0) {
        Color c = sh.str;
        float a = Clamp01(c.a * inkScale);
        c.a = 1.0f - std::pow(1.0f - a, kInkLayerStroke);
        brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
        bakeDc->DrawGeometry(geo, brush, sh.wid);
    }
    geo->Release();
}

bool ShanShuiScene::BakeTileStep(const SceneCtx& ctx, int index, float worldX, double deadline) {
    Tile& t = tiles[index];
    if (!t.bmp) return true;

    ID2D1DeviceContext* dc = ctx.dc;
    ID2D1Image* origTarget = nullptr;
    dc->GetTarget(&origTarget);

    // A fresh assignment (or an invalidated re-bake) starts from a cleared
    // bitmap; a resumed slice keeps drawing on top of what is already there.
    // valid MUST be cleared here: the slot may be carrying a stale "complete"
    // flag from the previous tile that lived in it, and a partial tile that
    // still reads valid would be blitted and never resumed.
    bool fresh = !t.baking || t.worldX != worldX;
    if (fresh) {
        t.worldX = worldX;
        t.baking = true;
        t.nextItem = 0;
        t.valid = false;
    }

    dc->SetTarget(t.bmp);
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    if (fresh) {
        dc->Clear(D2D1::ColorF(paint_.paper.r, paint_.paper.g, paint_.paper.b, 1.0f));
        // Paper texture, tiled.
        if (paperBmp) {
            D2D1_SIZE_F sz = paperBmp->GetSize();
            for (float y = 0; y < ctx.height; y += sz.height)
                for (float x = 0; x < ctx.width; x += sz.width)
                    dc->DrawBitmap(paperBmp, D2D1::RectF(x, y, x + sz.width, y + sz.height));
        }
    }

    // The painting: every world item that could reach into this tile, in paint
    // order, replayed through a world->screen transform.
    bakeDc = dc;
    bakeBrush = ctx.white;
    ID2D1Factory* factory = nullptr;
    dc->GetFactory(&factory);
    // Row-vector convention: the LEFT matrix applies first, so this is
    // screen = (world - worldX) * scale.
    dc->SetTransform(D2D1::Matrix3x2F::Translation(-worldX, 0) *
                     D2D1::Matrix3x2F::Scale(viewScale, viewScale));
    std::vector<const ss::WorldItem*> items;
    world_.Collect(worldX, worldX + tileWorldW, items);
    size_t i = std::min(t.nextItem, items.size());
    for (; i < items.size(); ++i) {
        for (const ss::Shape& sh : items[i]->sink.shapes)
            DrawShape(factory, sh, inkAmt);
        if (NowSec() > deadline) { ++i; break; }
    }

    // Mist: two soft paper-colored washes across the mountain flanks, drawn
    // once everything beneath them is in place.
    bool done = i >= items.size();
    if (done) {
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        if (mist > 0.01f) {
            ID2D1GradientStopCollection* coll = nullptr;
            D2D1_GRADIENT_STOP stops[3];
            Color c0 = paint_.paper;
            stops[0].position = 0.0f;
            stops[0].color = D2D1::ColorF(c0.r, c0.g, c0.b, 0);
            stops[1].position = 0.5f;
            stops[1].color = D2D1::ColorF(c0.r, c0.g, c0.b, 0.55f * mist);
            stops[2].position = 1.0f;
            stops[2].color = D2D1::ColorF(c0.r, c0.g, c0.b, 0);
            if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 3, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                const float bands[2][2] = {{240.0f, 400.0f}, {430.0f, 600.0f}};
                for (const auto& band : bands) {
                    gp.startPoint = D2D1::Point2F(0, band[0] * viewScale);
                    gp.endPoint = D2D1::Point2F(0, band[1] * viewScale);
                    if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                        dc->FillRectangle(
                            D2D1::RectF(0, band[0] * viewScale, ctx.width, band[1] * viewScale), gb);
                        gb->Release();
                    }
                }
                coll->Release();
            }
        }
    }

    dc->EndDraw();
    dc->SetTarget(origTarget);
    if (origTarget) origTarget->Release();
    if (factory) factory->Release();
    bakeDc = nullptr;
    bakeBrush = nullptr;
    if (done) {
        t.valid = true;
        t.baking = false;
        t.nextItem = 0;
    } else {
        t.nextItem = i;
    }
    return done;
}

void ShanShuiScene::Draw(const SceneCtx& ctx) {
    if (ctx.variation != lastVariation) Configure(ctx);
    EnsureBitmaps(ctx);

    ID2D1DeviceContext* dc = ctx.dc;

    // Which tiles cover the view? (The tiles themselves were baked in Update,
    // which runs outside the device's BeginDraw/EndDraw window.)
    int k0 = (int)std::floor(camX / tileWorldW);
    int k1 = (int)std::floor((camX + tileWorldW - 0.001f) / tileWorldW);
    if (k1 < k0) k1 = k0;
    if (k1 - k0 > 1) k0 = k1 - 1;

    // Blit the visible span.
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    for (int k = k0; k <= k1; ++k) {
        int slot = ((k % kTileCount) + kTileCount) % kTileCount;
        float x = ((float)k * tileWorldW - camX) * viewScale;
        if (tiles[slot].valid) {
            dc->DrawBitmap(tiles[slot].bmp,
                           D2D1::RectF(x, 0, x + tileWorldW * viewScale, (float)ctx.height));
        } else {
            // Never paint black: fall back to plain paper.
            ctx.white->SetColor(D2D1::ColorF(paint_.paper.r, paint_.paper.g, paint_.paper.b, 1.0f));
            dc->FillRectangle(D2D1::RectF(x, 0, x + tileWorldW * viewScale, (float)ctx.height),
                              ctx.white);
        }
    }

    // Collector's seal, bottom-right, screen-fixed like on a real scroll.
    {
        ID2D1SolidColorBrush* brush = ctx.white;
        float sw = 46.0f;
        float sx = ctx.width - sw - 26.0f;
        float sy = (float)ctx.height - sw - 26.0f;
        Color vermillion = Color::Hex(0x9e2b25);
        draw::RoundedRect(dc, brush, sx, sy, sw, sw, 6.0f, vermillion.WithAlpha(0.55f));
        Color mark(1, 1, 1, 0.4f);
        float m = sw * 0.18f;
        for (int r = 0; r < 3; ++r) {
            float ry = sy + m + (sw - 2 * m) * ((float)r / 3.0f);
            draw::Line(dc, brush, sx + m, ry, sx + sw - m, ry + sw * 0.05f, 2.0f, mark);
            draw::Line(dc, brush, sx + sw * 0.5f, ry - sw * 0.06f, sx + sw - m, ry, 2.0f, mark);
        }
    }
}

} // namespace lp
