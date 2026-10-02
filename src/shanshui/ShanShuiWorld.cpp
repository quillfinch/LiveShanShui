// Live Shan Shui - library port, part 5: the world. This is the port of the
// original's mountplanner/chunkloader/MEM streaming machinery: a noise-field
// planner decides where mountains, distant ridges, flat-topped mounts, boats
// (and this port's suns and bird flocks) go; 512-unit chunks are generated
// strictly left-to-right through one global RNG stream, which makes the whole
// infinite scroll reproducible from a single seed.
#include "ShanShuiInternal.h"
#include <cmath>
#include <algorithm>

namespace lp::ss {

using detail::kPiF;
using detail::kTauF;
using detail::Stroke;

namespace {

// --- planner (port of mountplanner) ------------------------------------------

struct PlanItem {
    std::string tag;
    float x = 0, y = 0, h = 0;
};

// Local maximum of the noise field over a (2r+1)^2 neighborhood.
bool LocMax(float x, float y, float r) {
    auto f = [](float fx, float fy) {
        return std::max(Noise(fx * 0.03f) - 0.55f, 0.0f) * 2.0f;
    };
    float z0 = f(x, y);
    if (z0 <= 0.3f) return false;
    for (int i = (int)(x - r); i < (int)(x + r); ++i)
        for (int j = (int)(y - r); j < (int)(y + r); ++j)
            if (f((float)i, (float)j) > z0) return false;
    return true;
}

} // namespace

// --- extras (this port's own additions, in the original's spirit) --------------

void sun(Sink& s, float x, float y, float r, const Paint& p) {
    // A pale wash disc with a thin ink rim, low on contrast like a real fan.
    std::vector<Vec2> disc;
    const int n = 40;
    for (int i = 0; i < n; ++i) {
        float a = kTauF * (float)i / (float)n;
        disc.push_back(Vec2(x + std::cos(a) * r, y + std::sin(a) * r));
    }
    Color wash((p.paper.r * 0.75f + 0.25f), (p.paper.g * 0.72f + 0.22f),
               (p.paper.b * 0.55f + 0.30f), 0.55f);
    s.Poly(disc, true, wash, true, InkAlpha(p, 0.25f), 2.0f);
}

void birdFlock(Sink& s, float x, float y, const Paint& p) {
    int count = 3 + (int)(R() * 5.0f);
    for (int i = 0; i < count; ++i) {
        float bx = x + (R() - 0.5f) * 180.0f;
        float by = y + (R() - 0.5f) * 90.0f;
        float w = 6.0f + R() * 6.0f;
        float dip = w * 0.35f;
        Color c = InkAlpha(p, 0.45f + R() * 0.3f);
        // Two strokes forming the classic "V with dropped wings".
        s.Poly({Vec2(bx - w, by - dip), Vec2(bx, by), Vec2(bx + w, by - dip * 0.8f)}, false,
               Color(), true, c, 1.6f);
    }
}

void sealStamp(Sink& s, float x, float y, float size) {
    // The collector's red seal: a square stamp with pale glyph marks.
    Color vermillion = Color::Hex(0x9e2b25);
    vermillion.a = 0.88f;
    std::vector<Vec2> sq = {
        Vec2(x, y), Vec2(x + size, y), Vec2(x + size, y + size), Vec2(x, y + size)};
    s.Poly(sq, true, vermillion, true, WithAlpha(vermillion, 0.95f), 2.0f);
    // Glyph suggestion: pale strokes inside, like seal script seen from afar.
    Color mark = Color(1, 1, 1, 0.55f);
    float m = size * 0.16f;
    float x0 = x + m, x1 = x + size - m;
    float y0 = y + m, y1 = y + size - m;
    float colw = (x1 - x0 - m) / 2.0f;
    for (int r = 0; r < 3; ++r) {
        float ry = y0 + m + (y1 - y0 - m) * ((float)r / 3.0f);
        s.Poly({Vec2(x0, ry), Vec2(x0 + colw * (r == 1 ? 1.0f : 0.85f), ry + m * 0.4f)}, false,
               Color(), true, mark, size * 0.07f);
        s.Poly({Vec2(x0 + colw + m, ry + m * 0.4f * (r == 0 ? -1.0f : 1.0f)),
                Vec2(x1, ry)}, false, Color(), true, mark, size * 0.07f);
    }
}

// --- paper -----------------------------------------------------------------------

void PaperTile(std::vector<uint32_t>& out, int size, uint32_t seed) {
    // Port of the original's paper generator: warm, slightly noisy fibers.
    // Uses private RNG state so the paper never disturbs the world stream.
    out.assign((size_t)size * size, 0);
    Perlin per;
    per.Seed(seed ^ 0x9e3779b9u);
    uint32_t lcg = seed | 1u;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            float c = 245.0f + per.Noise((float)i * 0.1f, (float)j * 0.1f) * 10.0f;
            lcg = 1664525u * lcg + 1013904223u;
            c -= (float)(lcg >> 24) / 255.0f * 20.0f;
            float r = c;
            float g = c * 0.95f;
            float b = c * 0.85f;
            uint32_t rr = (uint32_t)std::min(255.0f, std::max(0.0f, r));
            uint32_t gg = (uint32_t)std::min(255.0f, std::max(0.0f, g));
            uint32_t bb = (uint32_t)std::min(255.0f, std::max(0.0f, b));
            // Little-endian BGRA: byte0=B, byte1=G, byte2=R, byte3=A.
            out[(size_t)j * size + i] = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        }
    }
}

// --- World -------------------------------------------------------------------------

void World::Reset(uint32_t seed, const Paint& paint) {
    seed_ = seed;
    paint_ = paint;
    GPaint() = paint;
    items_.clear();
    planMtx_.clear();
    planBase_ = 0;
    xmin_ = 0;
    xmax_ = 0;
    Prng::Global().Seed(seed);
    Perlin::Global().Seed(seed ^ 0x5bf03635u);
}

std::vector<WorldItem> World::planChunk(float a, float b) {
    std::vector<PlanItem> plan;
    auto colOf = [&](float x) { return (int)std::floor(x / kXStep); };
    auto ensureCols = [&](int c) {
        int idx = c - planBase_;
        if (idx < 0) {   // shouldn't happen (we only extend right)
            planMtx_.insert(planMtx_.begin(), -idx, 0);
            planBase_ = c;
            idx = 0;
        }
        if (idx >= (int)planMtx_.size()) planMtx_.resize((size_t)idx + 1, 0);
        return idx;
    };

    auto chadd = [&](const PlanItem& r, float mind = 10.0f) {
        for (const auto& k : plan)
            if (std::fabs(k.x - r.x) < mind) return false;
        plan.push_back(r);
        return true;
    };

    auto yr = [](float x) { return Noise(x * 0.01f, kPiF); };
    auto ns = [](float x, float y) {
        return std::max(Noise(x * 0.03f) - 0.55f, 0.0f) * 2.0f;
    };

    const float mwid = 200.0f;
    for (float i = a; i < b; i += kXStep) {
        for (float j = 0; j < yr(i) * 480.0f; j += 30.0f) {
            if (LocMax(i, j, 2.0f)) {
                float xof = i + 2.0f * (R() - 0.5f) * 500.0f;
                float yof = j + 300.0f;
                PlanItem r{"mount", xof, yof, ns(i, j)};
                if (chadd(r)) {
                    int c0 = ensureCols(colOf(xof - mwid));
                    int c1 = ensureCols(colOf(xof + mwid));
                    for (int k = c0; k <= c1; ++k) planMtx_[k] += 1;
                }
            }
        }
        if (std::fmod(std::fabs(i), 1000.0f) < std::max(1.0f, kXStep - 1.0f)) {
            PlanItem r{"distmount", i, 280.0f - R() * 50.0f, 0};
            chadd(r);
        }
    }
    for (float i = a; i < b; i += kXStep) {
        int idx = ensureCols(colOf(i));
        if (planMtx_[idx] == 0) {
            if (R() < 0.01f) {
                for (int j = 0; j < (int)(4.0f * R()); ++j) {
                    PlanItem r{"flatmount", i + 2.0f * (R() - 0.5f) * 700.0f,
                               700.0f - (float)j * 50.0f, 0};
                    chadd(r);
                }
            }
        }
    }
    for (float i = a; i < b; i += kXStep) {
        if (R() < 0.2f) {
            PlanItem r{"boat", i, 300.0f + R() * 390.0f, 0};
            chadd(r, 400.0f);
        }
    }
    // Extras: one sun and a few bird flocks per region of the scroll.
    if (a == 0.0f || (int)(a / kChunkW) % 6 == 0) {
        PlanItem r{"sun", a + R() * kChunkW, 70.0f + R() * 80.0f, 0};
        plan.insert(plan.begin(), r);
    }
    if (R() < 0.3f) {
        PlanItem r{"birds", a + R() * kChunkW, 120.0f + R() * 140.0f, 0};
        chadd(r, 50.0f);
    }

    // Materialize each planned feature into shapes (the original's add() calls).
    // The generators can emit NaN points (pow/log of negative samples, just like
    // in the original JS, which scrubbed them with unNan); sanitize per shape.
    auto sanitize = [](Sink& sink) {
        // Generators can emit NaNs (pow/log of negative samples, exactly like the
        // original JS, which scrubbed them). A pruned shape whose FILL was derived
        // from a dropped point would paint with a NaN color (undefined, reads as
        // black), so any shape touched by a NaN is dropped whole.
        std::vector<Shape> clean;
        clean.reserve(sink.shapes.size());
        for (Shape& sh : sink.shapes) {
            bool ok = std::isfinite(sh.fil.r) && std::isfinite(sh.fil.g) &&
                      std::isfinite(sh.fil.b) && std::isfinite(sh.fil.a) &&
                      std::isfinite(sh.str.r) && std::isfinite(sh.str.g) &&
                      std::isfinite(sh.str.b) && std::isfinite(sh.str.a) &&
                      std::isfinite(sh.wid);
            if (ok) {
                for (const Vec2& v : sh.pts) {
                    if (!std::isfinite(v.x) || !std::isfinite(v.y)) { ok = false; break; }
                }
            }
            size_t need = sh.fill ? 3 : 2;
            if (!ok || sh.pts.size() < need) continue;
            clean.push_back(std::move(sh));
        }
        sink.shapes = std::move(clean);
    };

    std::vector<WorldItem> out;
    int idx = 0;
    for (const auto& pl : plan) {
        WorldItem item;
        item.tag = pl.tag;
        item.x = pl.x;
        item.y = pl.y;
        if (pl.tag == "mount") {
            Mount::mountain(item.sink, pl.x, pl.y, (float)idx * 2.0f * R(), {});
            out.push_back(item);
            WorldItem w;
            w.tag = "water";
            w.x = pl.x;
            w.y = pl.y - 10000.0f;
            water(w.sink, pl.x, pl.y, (float)idx * 2.0f);
            out.push_back(std::move(w));
        } else if (pl.tag == "flatmount") {
            MountOpts o;
            o.wid = 600.0f + R() * 400.0f;
            o.hei = 100.0f;
            o.cho = 0.5f + R() * 0.2f;
            Mount::flatMount(item.sink, pl.x, pl.y, 2.0f * R() * kPiF, o);
            out.push_back(std::move(item));
        } else if (pl.tag == "distmount") {
            Mount::distMount(item.sink, pl.x, pl.y, R() * 100.0f, 150.0f,
                             (RandChoice({500, 1000, 1500})), 5.0f);
            out.push_back(std::move(item));
        } else if (pl.tag == "boat") {
            Arch::boat01(item.sink, pl.x, pl.y, R(), pl.y / 800.0f, RandChoice({true, false}));
            out.push_back(std::move(item));
        } else if (pl.tag == "sun") {
            sun(item.sink, pl.x, pl.y, 55.0f + R() * 30.0f, paint_);
            out.push_back(std::move(item));
        } else if (pl.tag == "birds") {
            birdFlock(item.sink, pl.x, pl.y, paint_);
            out.push_back(std::move(item));
        }
        sanitize(out.back().sink);
        ++idx;
    }
    return out;
}

void World::Ensure(float xmin, float xmax) {
    (void)xmin;
    while (xmax_ < xmax + kChunkW) {
        std::vector<WorldItem> fresh = planChunk(xmax_, xmax_ + kChunkW);
        xmax_ += kChunkW;
        // Insert in paint order (stable for equal y). Items are heap-allocated
        // so the scene can hold pointers across Ensure calls.
        for (auto& item : fresh) {
            auto it = std::lower_bound(items_.begin(), items_.end(), item.y,
                                       [](const std::unique_ptr<WorldItem>& a, float y) {
                                           return a->y < y;
                                       });
            items_.insert(it, std::unique_ptr<WorldItem>(new WorldItem(std::move(item))));
        }
    }
}

void World::Trim(float keepFrom) {
    items_.erase(
        std::remove_if(items_.begin(), items_.end(),
                       [keepFrom](const std::unique_ptr<WorldItem>& a) {
                           return a->x < keepFrom;
                       }),
        items_.end());
}

void World::Collect(float xmin, float xmax, std::vector<const WorldItem*>& out) const {
    // Generators spill well beyond their anchor (distMount spans ~1500), so the
    // margin has to cover the widest feature.
    const float margin = 1600.0f;
    out.clear();
    for (const auto& item : items_) {
        if (item->x >= xmin - margin && item->x <= xmax + margin) out.push_back(item.get());
    }
}

} // namespace lp::ss
