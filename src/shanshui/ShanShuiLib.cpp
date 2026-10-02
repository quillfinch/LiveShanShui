// Live Shan Shui - library port, part 1: randomness, noise, utilities,
// triangulation, brush primitives. See ShanShuiLib.h for the map back to the
// original JavaScript (shan-shui-inf by LingDong, MIT).
#include "ShanShuiInternal.h"
#include <cmath>
#include <algorithm>

namespace lp::ss {

// --- Prng -------------------------------------------------------------------

void Prng::Seed(uint32_t x) {
    // The original hashes its (string) seed into [1, m) while dodging the
    // degenerate residues; a numeric seed just needs the same dodge.
    uint64_t y = (uint64_t)x % kM;
    uint64_t z = 0;
    while (y % kP == 0 || y % kQ == 0 || y == 0 || y == 1) {
        y = (y + 7919u + z) % kM;
        z += 1;
    }
    s_ = y;
    for (int i = 0; i < 10; ++i) Next();
}

float Prng::Next() {
    s_ = (s_ * s_) % kM;   // exact: 128-bit product mod m
    return (float)((double)s_ / (double)kM);
}

Prng& Prng::Global() {
    static Prng g;
    return g;
}

float R() { return Prng::Global().Next(); }

// --- Perlin (p5.js port, exactly as in the original) --------------------------

namespace {
using detail::kPiF;
constexpr int kPerlinYWrap = 1 << 4;
constexpr int kPerlinZWrap = 1 << 8;
constexpr int kPerlinSize = 4095;
constexpr int kPerlinOctaves = 4;
constexpr float kPerlinAmpFalloff = 0.5f;

inline float ScaledCosine(float i) { return 0.5f * (1.0f - std::cos(i * kPiF)); }
} // namespace

void Perlin::Seed(uint32_t seed) {
    // The original reseeds the table through a small LCG; same here.
    uint32_t z = seed ? seed : 1u;
    for (int i = 0; i <= kPerlinSize; ++i) {
        z = 1664525u * z + 1013904223u;
        table_[i] = (float)(z >> 8) / 16777216.0f;
    }
    built_ = true;
}

Perlin& Perlin::Global() {
    static Perlin g;
    return g;
}

float Perlin::Noise(float x, float y, float z) {
    if (!built_) Seed(1u);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    if (z < 0) z = -z;
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    float xf = x - (float)xi, yf = y - (float)yi, zf = z - (float)zi;
    float r = 0, ampl = 0.5f;
    for (int o = 0; o < kPerlinOctaves; ++o) {
        int of = xi + (yi << 4) + (zi << 8);
        float rxf = ScaledCosine(xf), ryf = ScaledCosine(yf);
        float n1 = table_[of & kPerlinSize];
        n1 += rxf * (table_[(of + 1) & kPerlinSize] - n1);
        float n2 = table_[(of + kPerlinYWrap) & kPerlinSize];
        n2 += rxf * (table_[(of + kPerlinYWrap + 1) & kPerlinSize] - n2);
        n1 += ryf * (n2 - n1);
        of += kPerlinZWrap;
        n2 = table_[of & kPerlinSize];
        n2 += rxf * (table_[(of + 1) & kPerlinSize] - n2);
        float n3 = table_[(of + kPerlinYWrap) & kPerlinSize];
        n3 += rxf * (table_[(of + kPerlinYWrap + 1) & kPerlinSize] - n3);
        n2 += ryf * (n3 - n2);
        n1 += ScaledCosine(zf) * (n2 - n1);
        r += n1 * ampl;
        ampl *= kPerlinAmpFalloff;
        xi <<= 1; xf *= 2;
        yi <<= 1; yf *= 2;
        zi <<= 1; zf *= 2;
        if (xf >= 1.0f) { xi++; xf--; }
        if (yf >= 1.0f) { yi++; yf--; }
        if (zf >= 1.0f) { zi++; zf--; }
    }
    return r;
}

float Noise(float x, float y, float z) { return Perlin::Global().Noise(x, y, z); }

// --- utilities ----------------------------------------------------------------

Vec2 MidPt(const Vec2* pts, int count) {
    Vec2 acc(0, 0);
    for (int i = 0; i < count; ++i) {
        acc.x += pts[i].x / (float)count;
        acc.y += pts[i].y / (float)count;
    }
    return acc;
}

float PDist(const Vec2& a, const Vec2& b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float Mapval(float v, float a0, float a1, float b0, float b1) {
    return b0 + (b1 - b0) * ((v - a0) / (a1 - a0));
}

void LoopNoise(std::vector<float>& ns) {
    int n = (int)ns.size();
    if (n < 2) return;
    float dif = ns[n - 1] - ns[0];
    float lo = 100.0f, hi = -100.0f;
    for (int i = 0; i < n; ++i) {
        ns[i] += (dif * (float)(n - 1 - i)) / (float)(n - 1);
        lo = std::min(lo, ns[i]);
        hi = std::max(hi, ns[i]);
    }
    for (int i = 0; i < n; ++i) ns[i] = Mapval(ns[i], lo, hi, 0.0f, 1.0f);
}

int RandChoice(int count) { return (int)(((float)count) * R()); }

float NormRand(float m, float M) { return Mapval(R(), 0.0f, 1.0f, m, M); }

float Wtrand(const std::function<float(float)>& f) {
    for (;;) {
        float x = R(), y = R();
        if (y < f(x)) return x;
    }
}

float RandGaussian() {
    return Wtrand([](float x) { return std::exp(-24.0f * (x - 0.5f) * (x - 0.5f)); }) * 2.0f - 1.0f;
}

std::vector<Vec2> Bezmh(const std::vector<Vec2>& pv, float w) {
    std::vector<Vec2> P = pv;
    if (P.size() == 2) P.insert(P.begin() + 1, MidPt(&P[0], 2));
    std::vector<Vec2> out;
    int n = (int)P.size();
    for (int j = 0; j < n - 2; ++j) {
        Vec2 p0 = (j == 0) ? P[j] : MidPt(&P[j], 2);
        Vec2 p1 = P[j + 1];
        Vec2 p2 = (j == n - 3) ? P[j + 2] : MidPt(&P[j + 1], 2);
        const int pl = 20;
        int steps = pl + (j == n - 3 ? 1 : 0);
        for (int i = 0; i < steps; ++i) {
            float t = (float)i / (float)pl;
            float u = (1 - t) * (1 - t) + 2 * t * (1 - t) * w + t * t;
            float bx = ((1 - t) * (1 - t) * p0.x + 2 * t * (1 - t) * p1.x * w + t * t * p2.x) / u;
            float by = ((1 - t) * (1 - t) * p0.y + 2 * t * (1 - t) * p1.y * w + t * t * p2.y) / u;
            out.push_back(Vec2(bx, by));
        }
    }
    return out;
}

std::vector<Vec2> Div(const std::vector<Vec2>& plist, float reso) {
    std::vector<Vec2> rlist;
    if (plist.empty()) return rlist;
    float tl = (float)(plist.size() - 1) * reso;
    for (float i = 0; i < tl; i += 1.0f) {
        const Vec2& lastp = plist[(size_t)std::floor(i / reso)];
        const Vec2& nextp = plist[(size_t)std::ceil(i / reso)];
        float p = std::fmod(i, reso) / reso;
        float nx = lastp.x * (1 - p) + nextp.x * p;
        float ny = lastp.y * (1 - p) + nextp.y * p;
        rlist.push_back(Vec2(nx, ny));
    }
    rlist.push_back(plist.back());
    return rlist;
}

// --- Triangulate (ear clipping, port of PolyTools.triangulate) -----------------

namespace {

bool SegIntersect(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1, Vec2* out) {
    auto line = [](const Vec2& p, const Vec2& q, float* m, float* k) {
        float den = q.x - p.x;
        *m = (den == 0) ? 1e30f : (q.y - p.y) / den;
        *k = p.y - (*m) * p.x;
    };
    float m0, k0, m1, k1;
    line(a0, a1, &m0, &k0);
    line(b0, b1, &m1, &k1);
    float d = m0 - m1;
    if (std::fabs(d) < 1e-9f) return false;
    float x = (k1 - k0) / d;
    float y = m0 * x + k0;
    auto onSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        return std::min(s0.x, s1.x) <= p.x && p.x <= std::max(s0.x, s1.x) &&
               std::min(s0.y, s1.y) <= p.y && p.y <= std::max(s0.y, s1.y);
    };
    if (onSeg(Vec2(x, y), a0, a1) && onSeg(Vec2(x, y), b0, b1)) {
        if (out) *out = Vec2(x, y);
        return true;
    }
    return false;
}

bool PtInPoly(const Vec2& pt, const std::vector<Vec2>& plist) {
    int scount = 0;
    Vec2 far(pt.x + 999.0f, pt.y + 999.0f);
    for (size_t i = 0; i < plist.size(); ++i) {
        const Vec2& np = plist[(i + 1) % plist.size()];
        if (SegIntersect(plist[i], np, pt, far, nullptr)) ++scount;
    }
    return (scount % 2) == 1;
}

bool LnInPoly(const Vec2& l0, const Vec2& l1, const std::vector<Vec2>& plist) {
    const float ep = 0.01f;
    Vec2 c0(l0.x * (1 - ep) + l1.x * ep, l0.y * (1 - ep) + l1.y * ep);
    Vec2 c1(l0.x * ep + l1.x * (1 - ep), l0.y * ep + l1.y * (1 - ep));
    for (size_t i = 0; i < plist.size(); ++i) {
        const Vec2& np = plist[(i + 1) % plist.size()];
        if (SegIntersect(c0, c1, plist[i], np, nullptr)) return false;
    }
    Vec2 mid = MidPt(&l0, 2);
    return PtInPoly(mid, plist);
}

float TriArea(const std::vector<Vec2>& t) {
    float a = PDist(t[0], t[1]), b = PDist(t[1], t[2]), c = PDist(t[2], t[0]);
    float s = (a + b + c) * 0.5f;
    return std::sqrt(std::max(0.0f, s * (s - a) * (s - b) * (s - c)));
}

struct Triangulator {
    static std::vector<std::vector<Vec2>> Shatter(const std::vector<Vec2>& pl, float a) {
        std::vector<std::vector<Vec2>> out;
        if (pl.empty()) return out;
        if (TriArea(pl) < a) { out.push_back(pl); return out; }
        size_t ind = 0;
        float best = -1;
        for (size_t i = 0; i < pl.size(); ++i) {
            float s = PDist(pl[i], pl[(i + 1) % pl.size()]);
            if (s > best) { best = s; ind = i; }
        }
        size_t nind = (ind + 1) % pl.size();
        size_t lind = (ind + 2) % pl.size();
        Vec2 mid((pl[ind].x + pl[nind].x) * 0.5f, (pl[ind].y + pl[nind].y) * 0.5f);
        std::vector<Vec2> t0 = { pl[ind], mid, pl[lind] };
        std::vector<Vec2> t1 = { pl[lind], pl[nind], mid };
        for (auto& t : Shatter(t0, a)) out.push_back(std::move(t));
        for (auto& t : Shatter(t1, a)) out.push_back(std::move(t));
        return out;
    }
    static float SliverRatio(const std::vector<Vec2>& t) {
        float P = PDist(t[0], t[1]) + PDist(t[1], t[2]) + PDist(t[2], t[0]);
        return TriArea(t) / std::max(P, 1e-6f);
    }
    static std::vector<std::vector<Vec2>> Run(std::vector<Vec2> pl, float area,
                                              bool convex, bool optimize) {
        if (pl.size() <= 3) return Shatter(pl, area);
        std::vector<Vec2> bestTri, rest;
        float bestRatio = -1;
        for (size_t i = 0; i < pl.size(); ++i) {
            const Vec2& lp = pl[(i + pl.size() - 1) % pl.size()];
            const Vec2& pt = pl[i];
            const Vec2& np = pl[(i + 1) % pl.size()];
            if (convex || LnInPoly(lp, np, pl)) {
                std::vector<Vec2> tri = { lp, pt, np };
                float r = optimize ? SliverRatio(tri) : 1.0f;
                if (r >= bestRatio) {
                    bestRatio = r;
                    bestTri = tri;
                    rest.clear();
                    for (size_t k = 0; k < pl.size(); ++k)
                        if (k != i) rest.push_back(pl[k]);
                    if (!optimize) break;
                }
            }
        }
        if (bestTri.empty()) return Shatter(pl, area);
        auto out = Shatter(bestTri, area);
        for (auto& t : Run(rest, area, convex, optimize)) out.push_back(std::move(t));
        return out;
    }
};

} // namespace

std::vector<std::vector<Vec2>> Triangulate(const std::vector<Vec2>& plist,
                                           float area, bool convex, bool optimize) {
    return Triangulator::Run(plist, area, convex, optimize);
}

// --- Paint --------------------------------------------------------------------

Paint& GPaint() {
    static Paint g;
    return g;
}

Color WithAlpha(const Color& c, float a) { return Color(c.r, c.g, c.b, a); }

Color InkAlpha(const Paint& p, float a) { return WithAlpha(p.ink, a); }

void Sink::Poly(std::vector<Vec2> pts, bool fill, Color fil, bool stroke, Color str, float wid,
                bool paper) {
    if (pts.size() < 2) return;
    Shape s;
    s.pts = std::move(pts);
    s.fill = fill; s.fil = fil;
    s.stroke = stroke; s.str = str; s.wid = wid;
    s.paper = paper;
    shapes.push_back(std::move(s));
}

// --- detail: brush primitives --------------------------------------------------

namespace detail {

float BlobFunDefault(float x) {
    return x <= 1 ? std::pow(std::sin(x * kPiF), 0.5f)
                  : -std::pow(std::sin((x + 1) * kPiF), 0.5f);
}

float BlobFunLeaf(float x) {
    return x <= 1 ? std::pow(std::sin(x * kPiF) * x, 0.5f)
                  : -std::pow(std::sin((x - 2) * kPiF * (x - 2)), 0.5f);
}

float BlobFunBamboo(float x) {
    return x <= 1 ? 2.75f * x * std::pow(1 - x, 1.0f / 1.8f)
                  : 2.75f * (x - 2) * std::pow(x - 1, 1.0f / 1.8f);
}

float BlobFunLog(float x) { return std::log(50.0f * x + 1.0f) / 3.95f; }

std::vector<Vec2> BlobPts(float x, float y, float len, float wid, float ang, float noi,
                          Fun1 fun) {
    const float reso = 20.0f;
    Fun1 f = fun ? fun : &BlobFunDefault;
    std::vector<Vec2> lalist;
    for (int i = 0; i <= (int)reso; ++i) {
        float p = ((float)i / reso) * 2.0f;
        float xo = len * 0.5f - std::fabs(p - 1.0f) * len;
        float yo = (f(p) * wid) * 0.5f;
        float a = std::atan2(yo, xo);
        float l = std::sqrt(xo * xo + yo * yo);
        lalist.push_back(Vec2(l, a));
    }
    std::vector<float> nslist;
    float n0 = R() * 10.0f;
    for (int i = 0; i <= (int)reso; ++i) nslist.push_back(Noise((float)i * 0.05f, n0));
    LoopNoise(nslist);
    std::vector<Vec2> plist;
    for (size_t i = 0; i < lalist.size(); ++i) {
        float ns = nslist[i] * noi + (1.0f - noi);
        float nx = x + std::cos(lalist[i].y + ang) * lalist[i].x * ns;
        float ny = y + std::sin(lalist[i].y + ang) * lalist[i].x * ns;
        plist.push_back(Vec2(nx, ny));
    }
    return plist;
}

void Blob(Sink& s, float x, float y, float len, float wid, float ang, const Color& col,
          float noi, Fun1 fun) {
    s.Poly(BlobPts(x, y, len, wid, ang, noi, fun), true, col, true, col, 0.0f);
}

void BlobPtsInto(Sink& s, const std::vector<Vec2>& pts, const Color& col) {
    s.Poly(pts, true, col, true, col, 0.0f);
}

void Stroke(Sink& s, const std::vector<Vec2>& ptlist, float wid, const Color& col,
            float noi, float out, const std::function<float(float)>& funIn) {
    std::function<float(float)> widthFun = funIn;
    if (!widthFun) widthFun = [](float x) { return std::sin(x * kPiF); };
    if (ptlist.size() < 2) return;
    std::vector<Vec2> vtx0, vtx1;
    float n0 = R() * 10.0f;
    for (size_t i = 1; i + 1 < ptlist.size(); ++i) {
        float w = wid * widthFun((float)i / (float)ptlist.size());
        w = w * (1 - noi) + w * noi * Noise((float)i * 0.5f, n0);
        float a1 = std::atan2(ptlist[i].y - ptlist[i - 1].y, ptlist[i].x - ptlist[i - 1].x);
        float a2 = std::atan2(ptlist[i].y - ptlist[i + 1].y, ptlist[i].x - ptlist[i + 1].x);
        float a = (a1 + a2) * 0.5f;
        if (a < a2) a += kPiF;
        vtx0.push_back(Vec2(ptlist[i].x + w * std::cos(a), ptlist[i].y + w * std::sin(a)));
        vtx1.push_back(Vec2(ptlist[i].x - w * std::cos(a), ptlist[i].y - w * std::sin(a)));
    }
    std::vector<Vec2> vtx;
    vtx.push_back(ptlist.front());
    for (auto& v : vtx0) vtx.push_back(v);
    for (int i = (int)vtx1.size() - 1; i >= 0; --i) vtx.push_back(vtx1[i]);
    vtx.push_back(ptlist.back());
    vtx.push_back(ptlist.front());
    s.Poly(vtx, true, col, true, col, out);
}

void Texture(Sink& s, const std::vector<std::vector<Vec2>>& ptlist, float xof, float yof,
             float tex, float wid, float len, float sha, const Paint& p, const Color& inkCol,
             float aBase, float aRange, TexDis dis, float noiConst) {
    if (ptlist.size() < 2 || ptlist[0].empty()) return;
    float reso0 = (float)ptlist.size();
    float reso1 = (float)ptlist[0].size();

    auto disFn = [&]() {
        switch (dis) {
            case kTexEdges:
                return (R() > 0.5f) ? 0.1f + 0.4f * R() : 0.9f - 0.4f * R();
            case kTexNearEdges:
                return (R() > 0.5f) ? 0.15f + 0.15f * R() : 0.85f - 0.15f * R();
            case kTexSquare:
                return Wtrand([](float a) { return a * a; });
            default:
                return (R() > 0.5f) ? (1.0f / 3.0f) * R() : (2.0f / 3.0f) + (1.0f / 3.0f) * R();
        }
    };
    auto noiFn = [&](float x) { return noiConst > 0 ? noiConst : 30.0f / x; };

    std::vector<std::vector<Vec2>> texlist;
    for (int i = 0; i < (int)tex; ++i) {
        int mid = (int)(disFn() * reso1);
        int hlen = (int)(R() * (reso1 * len));
        int start = std::min(std::max(mid - hlen, 0), (int)reso1);
        int end = std::min(std::max(mid + hlen, 0), (int)reso1);
        float layer = ((float)i / tex) * (reso0 - 1.0f);
        texlist.push_back({});
        for (int j = start; j < end; ++j) {
            float pp = layer - std::floor(layer);
            size_t lf = (size_t)std::min((float)reso0 - 1.0f, std::floor(layer));
            size_t lc = (size_t)std::min((float)reso0 - 1.0f, std::ceil(layer));
            if (j >= (int)ptlist[lf].size() || j >= (int)ptlist[lc].size()) continue;
            float x = ptlist[lf][j].x * pp + ptlist[lc][j].x * (1 - pp);
            float y = ptlist[lf][j].y * pp + ptlist[lc][j].y * (1 - pp);
            float n = noiFn(layer + 1.0f);
            texlist.back().push_back(Vec2(x + n * (Noise(x, (float)j * 0.5f) - 0.5f),
                                          y + n * (Noise(y, (float)j * 0.5f) - 0.5f)));
        }
    }

    auto emit = [&](const std::vector<Vec2>& pts, const Color& col, float w) {
        if (pts.size() < 2) return;
        std::vector<Vec2> shifted;
        shifted.reserve(pts.size());
        for (const auto& v : pts) shifted.push_back(Vec2(v.x + xof, v.y + yof));
        Stroke(s, shifted, w, col, 0.5f);
    };

    if (sha > 0) {
        for (size_t j = 0; j < texlist.size(); j += 2)
            emit(texlist[j], InkAlpha(p, 0.10f), sha);
    }
    size_t step = (sha > 0) ? 2 : 1;
    for (size_t j = (sha > 0 ? 1 : 0); j < texlist.size(); j += step)
        emit(texlist[j], WithAlpha(inkCol, aBase + R() * aRange), wid);
}

void FlipAll(std::vector<std::vector<Vec2>>& ptlist, float axis) {
    for (auto& line : ptlist)
        for (auto& pt : line) pt.x = axis - (pt.x - axis);
}

} // namespace detail

} // namespace lp::ss
