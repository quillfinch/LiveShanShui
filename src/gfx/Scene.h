// LivePaper - scene framework.
//
// A scene draws one frame into the device's scene target. It is constructed once
// (building its persistent caches) and then asked to Draw() on every frame. Scenes
// must not allocate per frame: the frame budget measured on the reference machine is
// ~1.7 ms, and heap churn is the quickest way to blow it.
#pragma once
#include "GfxDevice.h"
#include "../engine/IconAnchors.h"
#include "../core/Config.h"
#include "../core/Math.h"
#include <d2d1_1.h>
#include <string>
#include <vector>

struct ID2D1SolidColorBrush;
struct ID2D1LinearGradientBrush;
struct ID2D1RadialGradientBrush;
struct ID2D1StrokeStyle;

namespace lp {

// Everything a scene needs to draw a frame.
struct SceneCtx {
    gfx::Device* device = nullptr;
    ID2D1DeviceContext* dc = nullptr;
    ID2D1SolidColorBrush* white = nullptr;   // shared brush; scenes retint it

    float width = 0, height = 0;   // scene target size (already quality-scaled)
    float dt = 0;                  // seconds since the previous frame, clamped
    float time = 0;                // seconds since the scene was activated
    float fps = 0;
    float sceneScale = 1.0f;       // internal scale, so scenes can keep detail constant

    const IconAnchors* icons = nullptr;
    const Config* config = nullptr;

    // Quality-derived density multiplier: 0.5 (low) .. 1.4 (high).
    float density = 1.0f;

    // Scene variation seed. Folded into every Rng seed so `--reshuffle` produces a
    // new-but-still-deterministic layout; 0 is the canonical layout used by tests.
    uint32_t variation = 0;

    // The global custom color (config `customColor`, 0xRRGGBB). Scenes that opt in
    // derive their palette from it when useCustomColor is set.
    Color customColor;
    bool useCustomColor = false;
};

class Scene {
public:
    virtual ~Scene() = default;

    // Called on every (re)creation of the render target, and once at startup.
    virtual void Configure(const SceneCtx& ctx) { (void)ctx; }
    virtual void Update(const SceneCtx& ctx, float dt) { (void)ctx; (void)dt; }
    virtual void Draw(const SceneCtx& ctx) = 0;

    // Invalidate caches when the icon layout changes.
    virtual void OnIconsChanged(const SceneCtx& ctx) { (void)ctx; }

    virtual const wchar_t* Name() const = 0;
    virtual const wchar_t* Description() const = 0;

    // Scene-specific short labels for the four configurable parameters, so the
    // control panel can render meaningful sliders.
    virtual const wchar_t* ParamName(int index) const { (void)index; return L""; }
    virtual int ParamCount() const { return 0; }

    // True when the scene builds its colors from ctx.customColor / MakePalette,
    // which makes the panel's COLOR section worth showing.
    virtual bool SupportsCustomColor() const { return false; }
};

// Five harmonious colors derived from the scene context: the custom color when
// it is enabled, otherwise a deterministic palette from the variation seed.
// Scenes index into it for their own looks and set alphas themselves.
struct Palette {
    enum { kColors = 5 };
    Color c[kColors];
};
Palette MakePalette(const SceneCtx& ctx);

// The palette's base hue: from the custom color when enabled, otherwise the
// caller's own (parameter-driven) hue. Lets existing hue-based scenes honor
// the custom color with a one-line change.
float PaletteHue(const SceneCtx& ctx, float fallbackHue);

// Creates the scene for `id`. Never returns null.
Scene* CreateScene(SceneId id);
void DestroyScene(Scene* scene);

// --- shared drawing helpers -------------------------------------------------
// Kept free functions so scenes stay short and consistent.

namespace draw {

// Fills the target with a vertical multi-stop gradient. Rebuilds nothing per call
// beyond a gradient stop collection, which D2D caches internally by value.
void VerticalGradient(ID2D1DeviceContext* dc, float w, float h,
                      const D2D1_GRADIENT_STOP* stops, unsigned count);

// A soft radial glow centred at (cx,cy). Uses many thin ellipses with decreasing
// alpha rather than a blur effect: no intermediate surfaces, no effect graph.
void RadialGlow(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                float cx, float cy, float radius, const Color& color,
                float intensity = 1.0f, int rings = 14);

// Filled circle with straight alpha colour.
void Circle(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
            float cx, float cy, float r, const Color& color);

// Anti-aliased line segment.
void Line(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
          float x0, float y0, float x1, float y1, float width, const Color& color);

// Quadratic bezier approximated through a geometry sink - used by the "lines" scene.
void Curve(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
           float x0, float y0, float cx, float cy, float x1, float y1,
           float width, const Color& color);

// Fills a closed polygon with straight segments. The geometry is transient (built
// per call); reserve it for shapes whose vertices move, not static bulk fills.
void FillPolygon(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 const D2D1_POINT_2F* pts, unsigned count, const Color& color);

// A soft, slightly blurred-looking polyline (drawn as a few offset strokes).
void SoftCurve(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
               float x0, float y0, float cx, float cy, float x1, float y1,
               float width, const Color& color, float glow = 1.0f);

// Perspective grid of horizon lines; used by cyberpunk.
void HorizonGrid(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 float w, float horizonY, float floorY,
                 const Color& color, float scroll, float cell = 48.0f);

// Rounded rectangle helper (panel chrome).
void RoundedRect(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 float x, float y, float w, float h, float radius, const Color& color);

// Deterministic star field, rebuilt only when the surface size changes.
struct StarField {
    struct Star { float x, y, r, phase, speed; };
    std::vector<Star> stars;
    float w = 0, h = 0;
    void Build(float width, float height, int count, uint32_t seed = 12345u);
    void Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float time) const;
};

} // namespace draw
} // namespace lp
