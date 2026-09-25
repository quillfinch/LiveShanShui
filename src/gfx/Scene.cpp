#include "Scene.h"
#include "../scenes/Scenes.h"
#include "../core/Log.h"
#include <algorithm>

namespace lp {

Scene* CreateScene(SceneId id) {
    switch (id) {
        case SceneId::Lines:     return scenes::CreateLines();
        case SceneId::Cyberpunk: return scenes::CreateCyberpunk();
        case SceneId::Nature:    return scenes::CreateNature();
        case SceneId::Beach:     return scenes::CreateBeach();
        case SceneId::Anime:     return scenes::CreateAnime();
        case SceneId::Aurora:    return scenes::CreateAurora();
        case SceneId::Nebula:    return scenes::CreateNebula();
        case SceneId::Alpine:    return scenes::CreateAlpine();
        case SceneId::Fireflies: return scenes::CreateFireflies();
        case SceneId::Mesh:      return scenes::CreateMesh();
        case SceneId::Ocean:     return scenes::CreateOcean();
        case SceneId::Matrix:    return scenes::CreateMatrix();
        case SceneId::Koi:       return scenes::CreateKoi();
        case SceneId::Lava:      return scenes::CreateLava();
        case SceneId::Snow:      return scenes::CreateSnow();
        case SceneId::Rain:      return scenes::CreateRain();
        case SceneId::Desert: return scenes::CreateDesert();
        case SceneId::Storm: return scenes::CreateStorm();
        case SceneId::Fireworks: return scenes::CreateFireworks();
        case SceneId::Galaxy: return scenes::CreateGalaxy();
        case SceneId::Crystal: return scenes::CreateCrystal();
        case SceneId::Gears: return scenes::CreateGears();
        case SceneId::Balloons: return scenes::CreateBalloons();
        case SceneId::Lighthouse: return scenes::CreateLighthouse();
        case SceneId::Savanna: return scenes::CreateSavanna();
        case SceneId::Silk: return scenes::CreateSilk();
        case SceneId::BlackHole: return scenes::CreateBlackHole();
        case SceneId::Kaleido: return scenes::CreateKaleido();
        case SceneId::Flow: return scenes::CreateFlow();
        case SceneId::Plasma: return scenes::CreatePlasma();
        default:                 return scenes::CreateLines();
    }
}

void DestroyScene(Scene* scene) { delete scene; }

namespace draw {

namespace {

inline D2D1_COLOR_F Cf(const Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

} // namespace

void VerticalGradient(ID2D1DeviceContext* dc, float w, float h,
                      const D2D1_GRADIENT_STOP* stops, unsigned count) {
    if (!dc || !w || !h || !count) return;
    ID2D1GradientStopCollection* coll = nullptr;
    if (FAILED(dc->CreateGradientStopCollection(stops, count, &coll)) || !coll) return;
    ID2D1LinearGradientBrush* brush = nullptr;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = D2D1::Point2F(0, 0);
    props.endPoint = D2D1::Point2F(0, h);
    if (SUCCEEDED(dc->CreateLinearGradientBrush(props, coll, &brush)) && brush) {
        dc->FillRectangle(D2D1::RectF(0, 0, w, h), brush);
        brush->Release();
    }
    coll->Release();
}

void RadialGlow(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                float cx, float cy, float radius, const Color& color,
                float intensity, int rings) {
    if (!dc || !brush || radius <= 0 || rings < 1) return;
    // Concentric ellipses with quadratic falloff. Cheap and reads as a soft glow.
    for (int i = rings; i >= 1; --i) {
        float t = (float)i / (float)rings;          // 1 outer .. 0 inner
        float r = radius * t;
        float a = color.a * intensity * (1.0f - t) * (1.0f - t) * 0.55f;
        if (a <= 0.0015f) continue;
        brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, a));
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush);
    }
}

void Circle(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
            float cx, float cy, float r, const Color& color) {
    if (!dc || !brush || r <= 0) return;
    brush->SetColor(Cf(color));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush);
}

void Line(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
          float x0, float y0, float x1, float y1, float width, const Color& color) {
    if (!dc || !brush || width <= 0) return;
    brush->SetColor(Cf(color));
    dc->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush, width, nullptr);
}

// Builds a small path geometry for a quadratic bezier. These are transient (one per
// call) but the "lines" scene reuses its own precomputed geometry instead.
void Curve(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
           float x0, float y0, float cx, float cy, float x1, float y1,
           float width, const Color& color) {
    if (!dc || !brush || width <= 0) return;
    ID2D1Factory* factory = nullptr;
    dc->GetFactory(&factory);
    if (!factory) return;

    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink)) && sink) {
        sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_BEZIER_SEGMENT seg{};
        // Convert a quadratic control point into an equivalent cubic.
        seg.point1 = D2D1::Point2F(x0 + (cx - x0) * 0.6667f, y0 + (cy - y0) * 0.6667f);
        seg.point2 = D2D1::Point2F(x1 + (cx - x1) * 0.6667f, y1 + (cy - y1) * 0.6667f);
        seg.point3 = D2D1::Point2F(x1, y1);
        sink->AddBezier(seg);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        sink->Release();
        brush->SetColor(Cf(color));
        dc->DrawGeometry(geo, brush, width, nullptr);
    }
    geo->Release();
    factory->Release();
}

void FillPolygon(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 const D2D1_POINT_2F* pts, unsigned count, const Color& color) {
    if (!dc || !brush || !pts || count < 3) return;
    ID2D1Factory* factory = nullptr;
    dc->GetFactory(&factory);
    if (!factory) return;

    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) { factory->Release(); return; }
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink)) && sink) {
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts + 1, count - 1);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
        brush->SetColor(Cf(color));
        dc->FillGeometry(geo, brush, nullptr);
    }
    geo->Release();
    factory->Release();
}

void SoftCurve(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
               float x0, float y0, float cx, float cy, float x1, float y1,
               float width, const Color& color, float glow) {
    // A wider, dimmer pass underneath fakes a bloom without a blur effect.
    if (glow > 0.01f) {
        Curve(dc, brush, x0, y0, cx, cy, x1, y1, width * 3.4f,
              Color(color.r, color.g, color.b, color.a * 0.16f * glow));
        Curve(dc, brush, x0, y0, cx, cy, x1, y1, width * 1.9f,
              Color(color.r, color.g, color.b, color.a * 0.30f * glow));
    }
    Curve(dc, brush, x0, y0, cx, cy, x1, y1, width, color);
}

void HorizonGrid(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 float w, float horizonY, float floorY,
                 const Color& color, float scroll, float cell) {
    if (!dc || !brush || floorY <= horizonY) return;
    // Vertical lines converge on the vanishing point; spacing grows with distance.
    const float vpx = w * 0.5f;
    const int columns = (int)(w / cell) + 2;
    for (int i = -columns; i <= columns; ++i) {
        float xBottom = vpx + i * cell;
        float xTop = vpx + i * cell * 0.06f;
        float alpha = color.a * (1.0f - std::min(1.0f, std::fabs((float)i) / (float)columns) * 0.75f);
        Line(dc, brush, xTop, horizonY, xBottom, floorY, 1.2f,
             Color(color.r, color.g, color.b, alpha));
    }
    // Horizontal lines scroll toward the viewer with perspective compression.
    float step = 6.0f;
    int guard = 0;
    for (float d = step; d < 400.0f && guard < 64; d += step, ++guard) {
        float t = std::fmod(d + scroll, 400.0f) / 400.0f;
        float y = horizonY + (floorY - horizonY) * (t * t);
        float alpha = color.a * t * 0.9f;
        Line(dc, brush, 0, y, w, y, 1.0f, Color(color.r, color.g, color.b, alpha));
    }
}

void RoundedRect(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                 float x, float y, float w, float h, float radius, const Color& color) {
    if (!dc || !brush) return;
    brush->SetColor(Cf(color));
    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius), brush);
}

void StarField::Build(float width, float height, int count, uint32_t seed) {
    w = width; h = height;
    stars.clear();
    stars.reserve((size_t)count);
    Rng rng(seed);
    for (int i = 0; i < count; ++i) {
        Star s;
        s.x = rng.Range(0, width);
        s.y = rng.Range(0, height);
        s.r = rng.Range(0.6f, 1.9f);
        s.phase = rng.Range(0, kTau);
        s.speed = rng.Range(0.4f, 1.8f);
        stars.push_back(s);
    }
}

void StarField::Draw(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float time) const {
    for (const auto& s : stars) {
        float tw = 0.55f + 0.45f * std::sin(time * s.speed + s.phase);
        Circle(dc, brush, s.x, s.y, s.r, Color(1, 1, 1, 0.30f + 0.55f * tw));
    }
}

} // namespace draw
} // namespace lp
