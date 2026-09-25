// LivePaper - "Cyberpunk City".
//
// A rain-slicked neon skyline: layered procedural buildings, animated billboards,
// falling rain, and a reflective wet floor. Everything is drawn with gradients,
// rectangles and lines - no textures, no effects graph.
//
// Performance: the skyline and stars are generated once; per frame we only transform
// and fill rectangles plus a bounded number of rain streaks.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>


namespace lp::scenes {

namespace {

struct Building {
    float x, w, top;          // top = y of the roof
    float hue;                // window colour variation
    int   floors;
    uint32_t windowSeed;      // deterministic lit-window pattern
    bool  antenna;
};

struct Rain {
    float x, y, len, speed, alpha;
};

struct NeonSign {
    float x, y, w, h;
    int   kind;               // 0 = horizontal bar, 1 = block, 2 = vertical
    float hueOffset;
};

struct Car {
    float x, y, speed;
    bool headlights;
};

constexpr int kMaxRain = 900;
constexpr int kMaxBuildings = 90;

} // namespace

class CyberpunkScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Cyberpunk City"; }
    const wchar_t* Description() const override {
        return L"Neon skyline, animated signage and rain over a wet street.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Rain";
            case 1: return L"Neon";
            case 2: return L"Glow";
            case 3: return L"Traffic";
            default: return L"";
        }
    }

    void Configure(const SceneCtx& ctx) override {
        ReadParams(ctx);
        BuildSkyline(ctx);
        BuildSigns(ctx);
        BuildRain(ctx);
        BuildCars(ctx);
        m_lastW = ctx.width;
        m_lastH = ctx.height;
    }

    void Update(const SceneCtx& ctx, float dt) override {
        ReadParams(ctx);
        if (std::abs(ctx.width - m_lastW) > 1.0f || std::abs(ctx.height - m_lastH) > 1.0f) {
            Configure(ctx);
        }

        const float speed = 0.35f + m_traffic * 1.4f;
        for (auto& r : m_rain) {
            r.y += r.speed * speed * dt * 60.0f;
            r.x += r.speed * speed * dt * 12.0f;   // slight wind slant
            if (r.y > ctx.height + r.len) {
                r.y = -r.len - 10.0f;
                r.x = (float)((int)(r.x * 7919.0f) % std::max(1, (int)ctx.width));
                if (r.x < 0) r.x += ctx.width;
            }
            if (r.x > ctx.width + 20.0f) r.x -= ctx.width + 40.0f;
        }
        for (auto& c : m_cars) {
            c.x += c.speed * speed * dt * 60.0f;
            if (c.x > ctx.width + 60.0f) {
                c.x = -60.0f;
                c.y = m_floorY - 10.0f - (float)((int)(c.speed * 13.0f) % 14);
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // --- sky: deep indigo to a hot magenta haze at the horizon -----------
        D2D1_GRADIENT_STOP sky[4];
        sky[0].position = 0.0f;
        sky[0].color = D2D1::ColorF(0.020f, 0.016f, 0.063f, 1.0f);
        sky[1].position = 0.42f;
        sky[1].color = D2D1::ColorF(0.086f, 0.031f, 0.157f, 1.0f);
        sky[2].position = 0.72f;
        sky[2].color = D2D1::ColorF(0.290f, 0.055f, 0.290f, 1.0f);
        sky[3].position = 1.0f;
        sky[3].color = D2D1::ColorF(0.510f, 0.098f, 0.310f, 1.0f);
        draw::VerticalGradient(dc, w, h, sky, 4);

        // A dim second moon behind the smog.
        Color moon = Color::Hsv(0.88f, 0.35f, 1.0f, 0.20f * m_neon);
        draw::RadialGlow(dc, brush, w * 0.78f, h * 0.20f, 190.0f, moon, 1.0f, 20);
        draw::Circle(dc, brush, w * 0.78f, h * 0.20f, 26.0f, Color(1.0f, 0.85f, 0.92f, 0.55f));

        // --- far skyline (parallax layer 1) ---------------------------------
        DrawLayer(dc, brush, w, h, 0.55f, 0.55f, Color(0.10f, 0.07f, 0.20f, 0.85f), false);

        // --- mid skyline (layer 2) with lit windows --------------------------
        DrawLayer(dc, brush, w, h, 0.78f, 0.80f, Color(0.055f, 0.040f, 0.130f, 0.95f), true);

        // --- neon signage ----------------------------------------------------
        for (const auto& s : m_signs) {
            float pulse = 0.62f + 0.38f * std::sin(ctx.time * (1.4f + s.hueOffset) + s.x * 0.01f);
            Color c = Color::Hsv(0.90f - s.hueOffset * 0.55f, 0.85f, 1.0f, (0.35f + 0.65f * pulse) * m_neon);
            if (s.kind == 0) {
                draw::RoundedRect(dc, brush, s.x, s.y, s.w, s.h, s.h * 0.4f, c);
            } else if (s.kind == 1) {
                draw::RoundedRect(dc, brush, s.x, s.y, s.w, s.h, 4.0f, c);
            } else {
                draw::RoundedRect(dc, brush, s.x, s.y, s.w, s.h, s.w * 0.4f, c);
            }
            // Bloom around the sign.
            draw::RadialGlow(dc, brush, s.x + s.w * 0.5f, s.y + s.h * 0.5f,
                             std::max(s.w, s.h) * 1.9f, c, 0.7f * m_glow, 10);
        }

        // --- street level ----------------------------------------------------
        const float floor = m_floorY;
        draw::RoundedRect(dc, brush, 0, floor, w, h - floor, 0,
                          Color(0.02f, 0.02f, 0.05f, 1.0f));

        // Wet-road reflection: the signage colours smeared downward as soft columns.
        for (const auto& s : m_signs) {
            float pulse = 0.62f + 0.38f * std::sin(ctx.time * (1.4f + s.hueOffset) + s.x * 0.01f);
            Color c = Color::Hsv(0.90f - s.hueOffset * 0.55f, 0.85f, 1.0f, 0.16f * pulse * m_neon);
            float rx = s.x + s.w * 0.5f;
            for (int i = 0; i < 6; ++i) {
                float t = (float)i / 6.0f;
                float rw = s.w * (0.7f + t * 1.2f);
                float ry = floor + t * (h - floor) * 0.85f;
                draw::RoundedRect(dc, brush, rx - rw * 0.5f, ry, rw, 4.0f, 2.0f,
                                  Color(c.r, c.g, c.b, c.a * (1.0f - t) * 0.5f));
            }
        }

        // Perspective road grid.
        draw::HorizonGrid(dc, brush, w, floor, h, Color::Hsv(0.92f, 0.9f, 1.0f, 0.18f),
                          (float)std::fmod(ctx.time * 40.0 * (0.4 + m_traffic), 400.0), 54.0f);

        // Traffic streaks along the street.
        for (const auto& c : m_cars) {
            Color body = c.headlights
                ? Color::Hsv(0.55f, 0.6f, 1.0f, 0.75f)
                : Color::Hsv(0.02f, 0.85f, 1.0f, 0.75f);
            draw::RoundedRect(dc, brush, c.x, c.y, 16.0f, 3.0f, 1.5f, body);
            draw::RadialGlow(dc, brush, c.x + 8.0f, c.y + 1.5f, 26.0f, body, 0.55f * m_glow, 8);
        }

        // --- rain (drawn last so it sits over everything) --------------------
        for (const auto& r : m_rain) {
            draw::Line(dc, brush, r.x, r.y, r.x - r.len * 0.18f, r.y + r.len,
                       1.0f, Color(0.75f, 0.85f, 1.0f, r.alpha));
        }

        // --- scanline overlay: cheap CRT feel, also darkens for icon contrast -
        for (float y = 0; y < h; y += 3.0f) {
            draw::RoundedRect(dc, brush, 0, y, w, 1.0f, 0, Color(0, 0, 0, 0.055f));
        }

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.42f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_rainAmount = ctx.config->sceneParam[0];
        m_neon = 0.35f + ctx.config->sceneParam[1] * 1.3f;
        m_glow = ctx.config->sceneParam[2];
        m_traffic = ctx.config->sceneParam[3];
    }

    void BuildSkyline(const SceneCtx& ctx) {
        m_buildings.clear();
        m_floorY = ctx.height * 0.74f;
        Rng rng(20240517u + ctx.variation * 7919u);

        int count = (int)(14 + 34 * (ctx.width / 1920.0f));
        count = std::min(count, kMaxBuildings);
        float x = -40.0f;
        while (x < ctx.width + 40.0f && (int)m_buildings.size() < count) {
            Building b{};
            b.w = rng.Range(ctx.width * 0.022f, ctx.width * 0.062f);
            float heightFactor = rng.Range(0.16f, 0.62f);
            b.top = m_floorY - ctx.height * heightFactor;
            b.x = x;
            b.hue = rng.Unit();
            b.floors = (int)((m_floorY - b.top) / 9.0f);
            b.windowSeed = rng.Next();
            b.antenna = rng.Unit() > 0.82f;
            m_buildings.push_back(b);
            x += b.w + rng.Range(2.0f, 10.0f);
        }
    }

    void BuildSigns(const SceneCtx& ctx) {
        m_signs.clear();
        if (m_buildings.empty()) return;
        Rng rng(777u + ctx.variation * 7919u);
        int count = (int)(m_buildings.size() * 0.55f);
        for (int i = 0; i < count; ++i) {
            const Building& b = m_buildings[rng.Int(0, (int)m_buildings.size() - 1)];
            NeonSign s{};
            s.kind = rng.Int(0, 2);
            s.hueOffset = rng.Unit();
            if (s.kind == 0) {          // horizontal bar
                s.w = b.w * rng.Range(0.5f, 0.9f);
                s.h = rng.Range(4.0f, 7.0f);
            } else if (s.kind == 1) {   // block
                s.w = b.w * rng.Range(0.35f, 0.7f);
                s.h = s.w * rng.Range(0.4f, 0.8f);
            } else {                    // vertical sign
                s.w = rng.Range(5.0f, 9.0f);
                s.h = rng.Range(24.0f, 60.0f);
            }
            s.x = b.x + (b.w - s.w) * 0.5f;
            s.y = b.top + rng.Range(10.0f, std::max(12.0f, (m_floorY - b.top) * 0.6f));
            if (s.y + s.h > m_floorY - 4.0f) s.y = m_floorY - s.h - 6.0f;
            m_signs.push_back(s);
        }
    }

    void BuildRain(const SceneCtx& ctx) {
        m_rain.clear();
        int want = (int)(380 + 520 * m_rainAmount);
        want = std::min(want, kMaxRain);
        want = (int)(want * (ctx.width * ctx.height) / (1920.0f * 1080.0f));
        want = std::max(want, 120);
        Rng rng(31337u + ctx.variation * 7919u);
        m_rain.reserve((size_t)want);
        for (int i = 0; i < want; ++i) {
            Rain r{};
            r.x = rng.Range(-40.0f, ctx.width + 40.0f);
            r.y = rng.Range(-ctx.height, ctx.height);
            r.len = rng.Range(10.0f, 34.0f);
            r.speed = rng.Range(0.6f, 1.7f);
            r.alpha = rng.Range(0.10f, 0.34f);
            m_rain.push_back(r);
        }
    }

    void BuildCars(const SceneCtx& ctx) {
        m_cars.clear();
        Rng rng(9001u + ctx.variation * 7919u);
        for (int i = 0; i < 14; ++i) {
            Car c{};
            c.x = rng.Range(-60.0f, ctx.width);
            c.y = m_floorY - 10.0f - (float)(i % 3) * 13.0f;
            c.speed = rng.Range(1.4f, 4.2f);
            c.headlights = rng.Unit() > 0.5f;
            m_cars.push_back(c);
        }
    }

    // Draws a parallax band of buildings. `windowScale` shortens them for the far layer.
    void DrawLayer(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush, float w, float h,
                   float alpha, float windowScale, const Color& body, bool windows) {
        for (const auto& b : m_buildings) {
            float top = m_floorY - (m_floorY - b.top) * windowScale;
            draw::RoundedRect(dc, brush, b.x, top, b.w, m_floorY - top + 2.0f, 0,
                              Color(body.r, body.g, body.b, body.a * alpha));
            // A thin neon edge on one side of some towers.
            if (windows && (b.windowSeed & 3) == 0) {
                Color edge = Color::Hsv(0.88f - b.hue * 0.4f, 0.8f, 1.0f, 0.35f * m_neon);
                draw::RoundedRect(dc, brush, b.x, top, 2.0f, m_floorY - top, 0, edge);
            }
            if (!windows) continue;

            // Lit windows. The pattern is fixed per building; a slow global flicker
            // animates them without needing per-window state.
            float cellW = 5.0f, cellH = 7.0f;
            int cols = std::max(1, (int)(b.w / cellW) - 1);
            int rows = std::max(1, (int)((m_floorY - top) / cellH) - 1);
            for (int cx = 0; cx < cols; ++cx) {
                for (int cy = 0; cy < rows; ++cy) {
                    // Deterministic per-window bit from the building's seed.
                    uint32_t bit = (b.windowSeed >> ((cx * 7 + cy * 3) & 31)) ^ (uint32_t)(cx * 31 + cy * 17);
                    if ((bit & 7) != 0) continue;   // roughly 1 in 8 windows is lit
                    float wx = b.x + 3.0f + cx * cellW;
                    float wy = top + 4.0f + cy * cellH;
                    Color c = Color::Hsv(b.hue > 0.5f ? 0.13f : 0.55f, 0.55f, 1.0f, 0.55f);
                    draw::RoundedRect(dc, brush, wx, wy, 2.6f, 3.4f, 0.6f, c);
                }
            }
        }
    }

    std::vector<Building> m_buildings;
    std::vector<NeonSign> m_signs;
    std::vector<Rain> m_rain;
    std::vector<Car> m_cars;
    float m_floorY = 0;
    float m_rainAmount = 0.5f, m_neon = 1.0f, m_glow = 0.5f, m_traffic = 0.5f;
    float m_lastW = 0, m_lastH = 0;
};

Scene* CreateCyberpunk() { return new CyberpunkScene(); }

} // namespace lp::scenes
