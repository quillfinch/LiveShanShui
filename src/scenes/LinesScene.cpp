// LivePaper - "Lines & Connections".
//
// A dark gradient field where the real desktop icons become nodes in a living graph.
// Free-floating particles drift between them, near neighbours are joined by soft
// bezier filaments, and pulses travel along those filaments.
//
// Resource notes
// --------------
// * Icon halos are static in practice, so each one is painted into a small cached
//   bitmap once and blitted thereafter. Drawing ~40 multi-ring glows per frame was
//   measurable; blitting 40 cached sprites is not.
// * The neighbour graph is rebuilt only when the icon layout revision changes.
// * Particle/connection work is bounded by a hard cap derived from quality.
#include "Scenes.h"
#include "../core/Win32Compat.h"
#include "../core/Log.h"
#include <algorithm>


namespace lp::scenes {

namespace {

struct Node { float x, y, size; bool real; };
struct Particle { float x, y, vx, vy; float hue; };
struct Edge { int a, b; float len; };
struct Pulse { int edge; float t; float speed; };

constexpr int kMaxParticles = 220;
constexpr int kMaxEdges = 420;
constexpr int kMaxPulses = 90;

// Cache key: the halo bitmap is regenerated only when the size changes.
struct HaloCache {
    ID2D1Bitmap1* bmp = nullptr;
    int size = 0;
    ~HaloCache() { if (bmp) bmp->Release(); }
};

} // namespace

class LinesScene final : public Scene {
public:
    ~LinesScene() override { ReleaseHalos(); }

    const wchar_t* Name() const override { return L"Lines & Connections"; }
    const wchar_t* Description() const override {
        return L"A living graph woven around your desktop icons.";
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
        ReleaseHalos();
        BuildNodes(ctx);
        SyncParticles(ctx);
    }

    void OnIconsChanged(const SceneCtx& ctx) override {
        BuildNodes(ctx);
    }

    void Update(const SceneCtx& ctx, float dt) override {
        SyncParticles(ctx);

        // --- particles -------------------------------------------------------
        for (auto& p : m_particles) {
            // Gentle curl-ish drift so motion never looks linear.
            float n = Fbm(p.x * 0.0016f, p.y * 0.0016f + ctx.time * 0.045f, 2);
            float angle = n * kTau * 1.6f;
            float speed = 16.0f + 26.0f * m_speed;
            p.vx = Damp(p.vx, std::cos(angle) * speed, 0.55f, dt);
            p.vy = Damp(p.vy, std::sin(angle) * speed, 0.55f, dt);
            p.x += p.vx * dt;
            p.y += p.vy * dt;

            // Wrap around the edges, preserving the field's density.
            const float pad = 24.0f;
            if (p.x < -pad) p.x = ctx.width + pad;
            if (p.x > ctx.width + pad) p.x = -pad;
            if (p.y < -pad) p.y = ctx.height + pad;
            if (p.y > ctx.height + pad) p.y = -pad;
        }

        // --- pulses ----------------------------------------------------------
        for (auto& pl : m_pulses) {
            pl.t += pl.speed * dt * (0.35f + m_speed * 1.4f);
            if (pl.t >= 1.0f) {
                pl.t = 0.0f;
                // Re-route the pulse so traffic keeps moving around the graph.
                pl.edge = m_edgeCursor % (int)m_edges.size();
                m_edgeCursor = (m_edgeCursor + 7) % (int)m_edges.size();
                pl.speed = 0.25f + 0.5f * Fract(pl.t + m_pulseSeed++ * 0.37f);
            }
        }
    }

    void Draw(const SceneCtx& ctx) override {
        ID2D1DeviceContext* dc = ctx.dc;
        ID2D1SolidColorBrush* brush = ctx.white;
        const float w = ctx.width, h = ctx.height;

        // --- background: deep space gradient plus a slow moving bloom --------
        D2D1_GRADIENT_STOP stops[3];
        stops[0].position = 0.0f;
        stops[0].color = D2D1::ColorF(0.035f, 0.043f, 0.086f, 1.0f);
        stops[1].position = 0.55f;
        stops[1].color = D2D1::ColorF(0.055f, 0.030f, 0.098f, 1.0f);
        stops[2].position = 1.0f;
        stops[2].color = D2D1::ColorF(0.012f, 0.020f, 0.043f, 1.0f);
        draw::VerticalGradient(dc, w, h, stops, 3);

        float bloomX = w * 0.5f + std::cos(ctx.time * 0.055f) * w * 0.28f;
        float bloomY = h * 0.42f + std::sin(ctx.time * 0.041f) * h * 0.24f;
        // Kept dim: this wash sits behind the whole desktop and must not fight the
        // icons or reveal the ring structure of the concentric glow.
        Color bloom = Color::Hsv(m_hue, 0.72f, 0.85f, 0.07f + 0.05f * m_glow);
        draw::RadialGlow(dc, brush, bloomX, bloomY, std::max(w, h) * 0.60f, bloom, 1.0f, 30);

        // --- cached icon halos (blitted, not redrawn) ------------------------
        if (m_halo.bmp) {
            for (const auto& n : m_nodes) {
                float half = m_halo.size * 0.5f;
                D2D1_RECT_F dst = D2D1::RectF(n.x - half, n.y - half, n.x + half, n.y + half);
                dc->DrawBitmap(m_halo.bmp, &dst, 1.0f,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
            }
        }

        // --- filaments -------------------------------------------------------
        // Fade everything with distance so the graph reads as a soft web.
        for (const auto& e : m_edges) {
            const Node& a = m_nodes[e.a];
            const Node& b = m_nodes[e.b];
            float t = std::sin(ctx.time * 0.22f + (float)(e.a * 3 + e.b));
            float midx = (a.x + b.x) * 0.5f + t * e.len * 0.12f;
            float midy = (a.y + b.y) * 0.5f + std::cos(ctx.time * 0.19f + (float)e.b) * e.len * 0.12f;
            Color c = Color::Hsv(m_hue + 0.10f * (float)((e.a + e.b) % 5) / 5.0f, 0.65f, 1.0f, 0.30f);
            draw::SoftCurve(dc, brush, a.x, a.y, midx, midy, b.x, b.y, 1.1f, c, m_glow);
        }

        // --- pulses ----------------------------------------------------------
        for (const auto& pl : m_pulses) {
            if (pl.edge < 0 || pl.edge >= (int)m_edges.size()) continue;
            const Edge& e = m_edges[pl.edge];
            const Node& a = m_nodes[e.a];
            const Node& b = m_nodes[e.b];
            float t = pl.t;
            // Ease so the pulse lingers at the nodes.
            float ease = t * t * (3.0f - 2.0f * t);
            float px = Lerp(a.x, b.x, ease);
            float py = Lerp(a.y, b.y, ease);
            float fade = std::sin(t * kPi);   // appear and vanish at the ends
            Color core = Color::Hsv(m_hue + 0.05f, 0.35f, 1.0f, 0.85f * fade);
            draw::RadialGlow(dc, brush, px, py, 26.0f, core, 1.0f, 8);
            draw::Circle(dc, brush, px, py, 1.6f, Color(1, 1, 1, 0.9f * fade));
        }

        // --- particles -------------------------------------------------------
        for (const auto& p : m_particles) {
            float fade = 0.45f + 0.55f * std::sin(ctx.time * 1.1f + p.hue * kTau);
            float r = 1.3f + 1.5f * m_density;
            Color c = Color::Hsv(m_hue + p.hue * 0.26f, 0.55f, 1.0f, 0.30f + 0.45f * fade);
            draw::Circle(dc, brush, p.x, p.y, r, c);
        }

        // --- particle-to-particle constellation, drawn sparingly -------------
        // Only links shorter than `linkDist` and only for a subset of particles:
        // this is the single most expensive part of the scene, so it is bounded.
        int limit = std::min((int)m_particles.size(), 90);
        float linkDist = 118.0f;
        for (int i = 0; i < limit; ++i) {
            const Particle& a = m_particles[i];
            for (int j = i + 1; j < limit; ++j) {
                const Particle& b = m_particles[j];
                float dx = a.x - b.x, dy = a.y - b.y;
                float d2 = dx * dx + dy * dy;
                if (d2 > linkDist * linkDist) continue;
                float d = std::sqrt(d2);
                float alpha = (1.0f - d / linkDist) * 0.20f;
                draw::Line(dc, brush, a.x, a.y, b.x, b.y, 0.8f,
                           Color::Hsv(m_hue + 0.5f, 0.4f, 1.0f, alpha));
            }
        }

        // --- vignette so the desktop icons stay legible ----------------------
        D2D1_GRADIENT_STOP vig[2];
        vig[0].position = 0.0f;
        vig[0].color = D2D1::ColorF(0, 0, 0, 0.0f);
        vig[1].position = 1.0f;
        vig[1].color = D2D1::ColorF(0, 0, 0, 0.35f);
        draw::VerticalGradient(dc, w, h, vig, 2);
    }

private:
    void ReleaseHalos() {
        if (m_halo.bmp) { m_halo.bmp->Release(); m_halo.bmp = nullptr; }
        m_halo.size = 0;
    }

    void ReadParams(const SceneCtx& ctx) {
        m_density = ctx.config->sceneParam[0];
        m_speed = ctx.config->sceneParam[1];
        m_glow = ctx.config->sceneParam[2];
        m_hue = ctx.config->sceneParam[3];
    }

    void BuildNodes(const SceneCtx& ctx) {
        ReadParams(ctx);
        m_nodes.clear();
        m_edges.clear();
        m_pulses.clear();
        m_edgeCursor = 0;

        if (ctx.icons) {
            const auto& anchors = ctx.icons->Anchors();
            m_nodes.reserve(anchors.size());
            for (const auto& a : anchors) {
                Node n;
                n.x = a.x; n.y = a.y;
                n.size = a.size > 8 ? a.size : 48.0f;
                n.real = a.real;
                m_nodes.push_back(n);
            }
        }

        BuildEdges();
        BuildPulses(ctx);
        BuildHalo(ctx);
    }

    // Connects each node to its nearest few neighbours, de-duplicated.
    void BuildEdges() {
        const int n = (int)m_nodes.size();
        if (n < 2) return;
        const int perNode = 2;
        const float maxLen = 420.0f;
        for (int i = 0; i < n && (int)m_edges.size() < kMaxEdges; ++i) {
            // Simple selection of the `perNode` closest nodes.
            int best[perNode] = { -1, -1 };
            float bestD[perNode] = { 1e30f, 1e30f };
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                float dx = m_nodes[i].x - m_nodes[j].x;
                float dy = m_nodes[i].y - m_nodes[j].y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d > maxLen) continue;
                if (d < bestD[0]) { bestD[1] = bestD[0]; best[1] = best[0]; bestD[0] = d; best[0] = j; }
                else if (d < bestD[1]) { bestD[1] = d; best[1] = j; }
            }
            for (int k = 0; k < perNode; ++k) {
                if (best[k] < 0) continue;
                int a = i, b = best[k];
                if (a > b) std::swap(a, b);
                // Skip duplicates.
                bool dup = false;
                for (const auto& e : m_edges) {
                    if (e.a == a && e.b == b) { dup = true; break; }
                }
                if (dup) continue;
                m_edges.push_back({ a, b, bestD[k] });
                if ((int)m_edges.size() >= kMaxEdges) return;
            }
        }
    }

    void BuildPulses(const SceneCtx& ctx) {
        if (m_edges.empty()) return;
        int want = (int)(m_edges.size() * (0.22f + 0.45f * m_density));
        want = std::min(want, kMaxPulses);
        want = std::max(want, 4);
        Rng rng(9871u + ctx.variation * 7919u);
        for (int i = 0; i < want; ++i) {
            Pulse p;
            p.edge = i % (int)m_edges.size();
            p.t = rng.Unit();
            p.speed = rng.Range(0.25f, 0.85f);
            m_pulses.push_back(p);
        }
        m_pulseSeed = 1;
        (void)ctx;
    }

    // Pre-paints one icon halo into a bitmap. The halo depends only on the icon cell
    // size and the current parameters, so per-frame cost becomes a single blit.
    //
    // The halo must stay subtle: it sits directly behind a real desktop icon, and a
    // bright halo both hides the icon artwork and makes a dense icon column read as
    // one solid glowing mass.
    void BuildHalo(const SceneCtx& ctx) {
        if (!ctx.dc || m_nodes.empty()) return;
        float cell = m_nodes[0].size;
        for (const auto& n : m_nodes) cell = std::max(cell, n.size);
        int size = (int)(cell * 2.0f);
        size = std::max(32, std::min(size, 384));
        if (m_halo.bmp && m_halo.size == size) return;

        if (m_halo.bmp) { m_halo.bmp->Release(); m_halo.bmp = nullptr; }

        D2D1_SIZE_U bmpSize = D2D1::SizeU((UINT32)size, (UINT32)size);
        ID2D1Bitmap1* bmp = nullptr;
        if (FAILED(abi::CreateBitmap1(ctx.dc, bmpSize, nullptr, 0,
                                      DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED,
                                      D2D1_BITMAP_OPTIONS_TARGET, &bmp)) || !bmp) {
            return;   // fall back to nothing; the scene still draws filaments
        }

        // Render the halo into the bitmap by temporarily retargeting the context.
        // This happens once per layout change, never per frame.
        ID2D1Image* savedTarget = nullptr;
        ctx.dc->GetTarget(&savedTarget);
        ctx.dc->SetTarget((ID2D1Image*)bmp);
        ctx.dc->BeginDraw();
        ctx.dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        ctx.dc->SetTransform(D2D1::Matrix3x2F::Identity());

        float c = size * 0.5f;
        // Peak alpha stays low (~0.18) so the icon remains completely readable.
        Color halo = Color::Hsv(m_hue, 0.55f, 1.0f, 0.10f + 0.10f * m_glow);
        draw::RadialGlow(ctx.dc, ctx.white, c, c, size * 0.46f, halo, 1.0f, 16);
        // A single faint ring marks the icon cell without competing with the icon.
        Color ring = Color::Hsv(m_hue + 0.08f, 0.5f, 1.0f, 0.05f + 0.07f * m_glow);
        ctx.white->SetColor(D2D1::ColorF(ring.r, ring.g, ring.b, ring.a));
        ctx.dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(c, c), size * 0.27f, size * 0.27f),
                            ctx.white, 1.2f, nullptr);

        HRESULT hr = ctx.dc->EndDraw();
        if (savedTarget) { ctx.dc->SetTarget(savedTarget); savedTarget->Release(); }
        if (FAILED(hr)) { bmp->Release(); return; }

        m_halo.bmp = bmp;
        m_halo.size = size;
        LP_LOGD(L"lines: halo cache %dx%d", size, size);
    }

    void SyncParticles(const SceneCtx& ctx) {
        ReadParams(ctx);
        int want = (int)(60 + 150 * m_density);
        want = std::min(want, kMaxParticles);
        if (ctx.width * ctx.height > 1920.0f * 1080.0f) want = std::min(want + 40, kMaxParticles);

        if ((int)m_particles.size() == want && m_fieldW == ctx.width && m_fieldH == ctx.height)
            return;

        // Resize conservatively: keep existing particles so motion stays continuous.
        if (m_particles.empty()) {
            Rng rng(4242u + ctx.variation * 7919u);
            m_particles.reserve((size_t)want);
            for (int i = 0; i < want; ++i) {
                Particle p;
                p.x = rng.Range(0, ctx.width > 0 ? ctx.width : 1920.0f);
                p.y = rng.Range(0, ctx.height > 0 ? ctx.height : 1080.0f);
                p.vx = p.vy = 0;
                p.hue = rng.Unit();
                m_particles.push_back(p);
            }
        } else if ((int)m_particles.size() < want) {
            Rng rng((uint32_t)m_particles.size() * 7919u + 13u);
            while ((int)m_particles.size() < want) {
                Particle p;
                p.x = rng.Range(0, ctx.width > 0 ? ctx.width : 1920.0f);
                p.y = rng.Range(0, ctx.height > 0 ? ctx.height : 1080.0f);
                p.vx = p.vy = 0;
                p.hue = rng.Unit();
                m_particles.push_back(p);
            }
        } else if ((int)m_particles.size() > want) {
            m_particles.resize((size_t)want);
        }
        m_fieldW = ctx.width;
        m_fieldH = ctx.height;
    }

    std::vector<Node> m_nodes;
    std::vector<Particle> m_particles;
    std::vector<Edge> m_edges;
    std::vector<Pulse> m_pulses;
    HaloCache m_halo;
    float m_density = 0.5f, m_speed = 0.5f, m_glow = 0.5f, m_hue = 0.52f;
    float m_fieldW = 0, m_fieldH = 0;
    int m_edgeCursor = 0;
    unsigned m_pulseSeed = 1;
};

Scene* CreateLines() { return new LinesScene(); }

} // namespace lp::scenes
