// LivePaper - "Lava Flow".
//
// A cooled basalt field split by a network of molten cracks. The cracks are built
// once as random-walk polylines; per frame only their brightness travels (a pulse
// running along the accumulated length), and embers lift off the hottest points.
#include "Scenes.h"
#include <d2d1_1.h>
#include <algorithm>
#include <vector>

namespace lp::scenes {

namespace {

struct Seg {
    float x0, y0, x1, y1;
    float width;
    float dist;      // distance along the crack, drives the travelling pulse
    float jitter;    // per-segment brightness jitter
};

struct Crack {
    float pulseRate;
    float base;      // base brightness
};

struct Ember {
    float x, y;
    float vy;
    float life, maxLife;
    float wobbleAmp, phase;
    float r;
};

} // namespace

class LavaScene final : public Scene {
public:
    const wchar_t* Name() const override { return L"Lava Flow"; }
    const wchar_t* Description() const override {
        return L"Molten cracks pulsing through dark volcanic rock.";
    }
    int ParamCount() const override { return 4; }
    const wchar_t* ParamName(int i) const override {
        switch (i) {
            case 0: return L"Cracks";
            case 1: return L"Flow";
            case 2: return L"Embers";
            case 3: return L"Heat";
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
        m_time += dt * (0.4f + 1.2f * m_flow);

        // Embers rise from the crack joints, fade, and respawn.
        const float w = ctx.width, h = ctx.height;
        Rng rng((uint32_t)(m_time * 613.0f) + 5u + ctx.variation);
        for (auto& e : m_embers) {
            e.y -= e.vy * dt;
            e.x += std::sin(e.phase) * e.wobbleAmp * dt;
            e.phase += dt * 2.2f;
            e.life -= dt;
            if (e.life <= 0.0f && (int)m_spawns.size() > 0) {
                const Seg& s = m_spawns[rng.Int(0, (int)m_spawns.size() - 1)];
                e.x = Lerp(s.x0, s.x1, rng.Unit());
                e.y = Lerp(s.y0, s.y1, rng.Unit());
                e.vy = rng.Range(14.0f, 42.0f);
                e.maxLife = rng.Range(1.6f, 4.2f);
                e.life = e.maxLife;
                e.wobbleAmp = rng.Range(6.0f, 22.0f);
                e.r = rng.Range(1.2f, 3.4f);
            }
        }
        (void)w; (void)h;
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // Cooled rock: near-black with a faint red cast that grows toward the floor.
        D2D1_GRADIENT_STOP rock[3];
        rock[0].position = 0.0f; rock[0].color = D2D1::ColorF(0.024f, 0.016f, 0.014f, 1);
        rock[1].position = 0.62f; rock[1].color = D2D1::ColorF(0.036f, 0.020f, 0.015f, 1);
        rock[2].position = 1.0f; rock[2].color = D2D1::ColorF(0.052f, 0.026f, 0.016f, 1);
        draw::VerticalGradient(dc, w, h, rock, 3);

        // Ambient heat: the whole floor breathes a little.
        float heat = 0.5f + 0.5f * std::sin(m_time * 0.23f);
        draw::RadialGlow(dc, brush, w * 0.5f, h * 1.05f, w * 0.62f,
                         Color::Hsv(0.02f + m_heat * 0.03f, 0.95f, 0.55f,
                                        0.10f + 0.05f * heat), 1.0f, 12);

        // --- cracks ------------------------------------------------------------
        // Brightness = crack base + a pulse travelling along the accumulated length.
        const float hue = 0.015f + m_heat * 0.045f;   // deep red .. orange
        size_t ci = 0;
        for (size_t i = 0; i < m_segs.size(); ++i) {
            if (ci + 1 < m_cracks.size() && i >= m_crackRange[ci + 1]) ++ci;
            const Crack& cr = m_cracks[ci];
            const Seg& s = m_segs[i];
            float pulse = 0.5f + 0.5f * std::sin(s.dist * 0.020f - m_time * cr.pulseRate + s.jitter);
            float glowLevel = (0.55f + 0.65f * m_cracksAmt) * cr.base + pulse * 0.45f;
            // Black-body ramp: dark red floor, orange body, yellow-hot crests.
            Color core = Color::Hsv(hue + 0.055f * pulse, 0.92f, 0.35f + 0.62f * glowLevel);
            Color mid  = Color::Hsv(hue + 0.020f, 0.97f, (0.20f + 0.35f * glowLevel) * 0.8f);
            Color halo = Color::Hsv(hue, 1.0f, 0.55f, 0.10f + 0.10f * glowLevel);
            draw::Line(dc, brush, s.x0, s.y0, s.x1, s.y1, s.width * 5.5f, halo);
            draw::Line(dc, brush, s.x0, s.y0, s.x1, s.y1, s.width * 2.2f, mid.WithAlpha(0.55f));
            draw::Line(dc, brush, s.x0, s.y0, s.x1, s.y1, s.width, core);
            // Weld the joints: a round core-coloured vertex pool per end plus a
            // hot spot that pulses with the segment, so branches read as connected.
            draw::Circle(dc, brush, s.x0, s.y0, s.width * 0.55f, core);
            draw::Circle(dc, brush, s.x1, s.y1, s.width * 0.48f, core);
            if (pulse > 0.72f) {
                Color hot = Color::Hsv(hue + 0.09f, 0.55f, 1.0f, (pulse - 0.72f) * 2.4f);
                draw::RadialGlow(dc, brush, (s.x0 + s.x1) * 0.5f, (s.y0 + s.y1) * 0.5f,
                                 s.width * 6.0f, hot, 1.0f, 6);
            }
        }

        // --- embers --------------------------------------------------------------
        for (const auto& e : m_embers) {
            float t = Clamp01(e.life / e.maxLife);
            float a = t * (1.0f - t) * 4.0f * (0.35f + 0.85f * m_embersAmt);
            if (a <= 0.01f) continue;
            Color c = Color::Hsv(hue + 0.08f, 0.85f, 1.0f);
            draw::RadialGlow(dc, brush, e.x, e.y, e.r * 4.0f, c.WithAlpha(a * 0.5f), 1.0f, 5);
            draw::Circle(dc, brush, e.x, e.y, e.r * 0.55f, Color(1, 0.92f, 0.7f, a));
        }

        // Vignette.
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f; vig[0].color = D2D1::ColorF(0, 0, 0, 0);
        vig[1].position = 1.0f; vig[1].color = D2D1::ColorF(0, 0, 0, 0.44f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReadParams(const SceneCtx& ctx) {
        m_cracksAmt = ctx.config->sceneParam[0];
        m_flow = ctx.config->sceneParam[1];
        m_embersAmt = ctx.config->sceneParam[2];
        m_heat = ctx.config->sceneParam[3];
    }

    void Build(const SceneCtx& ctx) {
        const float w = ctx.width, h = ctx.height;
        Rng rng(0x1A7A0u + ctx.variation * 15485863u);
        m_segs.clear();
        m_cracks.clear();
        m_crackRange.clear();
        m_spawns.clear();

        int crackCount = (int)((5 + 8 * m_cracksAmt) * ctx.density);
        crackCount = std::min(crackCount, 14);
        for (int c = 0; c < crackCount; ++c) {
            m_crackRange.push_back(m_segs.size());
            Crack cr{};
            cr.pulseRate = rng.Range(0.8f, 2.2f);
            cr.base = rng.Range(0.30f, 0.75f);
            m_cracks.push_back(cr);

            // A random walk from a random floor origin, wandering upward-ish.
            float x = rng.Range(0.05f, 0.95f) * w;
            float y = rng.Range(0.55f, 1.02f) * h;
            float angle = -kPi * 0.5f + rng.Range(-0.7f, 0.7f);
            float dist = 0.0f;
            float width = rng.Range(2.8f, 5.4f) * std::max(0.8f, ctx.sceneScale);
            int steps = rng.Int(8, 16);
            for (int s = 0; s < steps; ++s) {
                float len = rng.Range(h * 0.03f, h * 0.075f);
                angle += rng.Range(-0.55f, 0.55f);
                // Keep the walk heading generally up/level so cracks stay on the rock.
                if (std::cos(angle) < -0.2f || std::cos(angle) > 0.2f) angle = -kPi * 0.5f + rng.Range(-0.6f, 0.6f);
                float nx = x + std::cos(angle) * len;
                float ny = y + std::sin(angle) * len;
                if (ny < h * 0.22f || nx < 0 || nx > w) break;
                Seg seg{ x, y, nx, ny, width, dist, rng.Range(0.0f, kTau) };
                m_segs.push_back(seg);
                m_spawns.push_back(seg);
                x = nx; y = ny;
                dist += len;
                width *= 0.94f;
                // Branch: a short offshoot leaves the main vein.
                if (rng.Unit() < 0.34f) {
                    float ba = angle + (rng.Unit() < 0.5f ? 1.0f : -1.0f) * rng.Range(0.7f, 1.2f);
                    float bx = x, by = y, bd = dist;
                    for (int b = 0; b < 4; ++b) {
                        float bl = rng.Range(h * 0.015f, h * 0.045f);
                        ba += rng.Range(-0.4f, 0.4f);
                        float bnx = bx + std::cos(ba) * bl;
                        float bny = by + std::sin(ba) * bl;
                        if (bny < h * 0.22f || bnx < 0 || bnx > w) break;
                        Seg bs{ bx, by, bnx, bny, width * 0.55f, bd, rng.Range(0.0f, kTau) };
                        m_segs.push_back(bs);
                        m_spawns.push_back(bs);
                        bx = bnx; by = bny;
                        bd += bl;
                    }
                }
            }
        }
        m_crackRange.push_back(m_segs.size());

        m_embers.clear();
        int emberCount = (int)((10 + 26 * m_embersAmt) * ctx.density);
        emberCount = std::min(emberCount, 48);
        m_embers.resize((size_t)emberCount);
        for (auto& e : m_embers) {
            e.x = rng.Range(0.0f, w);
            e.y = rng.Range(0.0f, h);
            e.vy = rng.Range(14.0f, 42.0f);
            e.maxLife = rng.Range(1.5f, 4.0f);
            e.life = rng.Range(0.0f, e.maxLife);
            e.wobbleAmp = rng.Range(6.0f, 22.0f);
            e.phase = rng.Range(0.0f, kTau);
            e.r = rng.Range(1.2f, 3.4f);
        }
    }

    std::vector<Seg> m_segs;
    std::vector<Seg> m_spawns;        // subset of m_segs embers lift off from
    std::vector<Crack> m_cracks;
    std::vector<size_t> m_crackRange; // seg index where each crack starts (+ end)
    std::vector<Ember> m_embers;
    float m_time = 0;
    float m_cracksAmt = 0.5f, m_flow = 0.5f, m_embersAmt = 0.5f, m_heat = 0.5f;
    float m_lastW = 1920, m_lastH = 1080;
};

Scene* CreateLava() { return new LavaScene(); }

} // namespace lp::scenes
