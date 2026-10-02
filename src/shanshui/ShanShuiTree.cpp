// Live Shan Shui - library port, part 2: the eight Tree generators and their
// helpers (branch, twig, barkify). Ports of the original JS, one function per
// generator; RNG and noise calls stay in the original order so a given world
// seed grows the same forest.
#include "ShanShuiInternal.h"
#include <cmath>
#include <algorithm>

namespace lp::ss {

using detail::kPiF;
using detail::Blob;
using detail::BlobPts;
using detail::BlobFunBamboo;
using detail::BlobFunLeaf;
using detail::BlobFunLog;
using detail::Stroke;
using detail::Texture;
using detail::FlipAll;

namespace {

Paint& P() { return GPaint(); }

// leaf-color jitter: the original splits its "rgba(...)" string and jitters
// alpha; the port jitters the Color directly.
inline Color LeafCol(const Color& c, float jitter) {
    return Color(c.r, c.g, c.b, std::min(1.0f, c.a + R() * jitter));
}

inline Color LeafColExact(const Color& c) { return c; }

struct BranchResult {
    std::vector<Vec2> a, b;
};

// branch(): a tapered limb built from a few leaned segments, resampled.
BranchResult MakeBranch(float hei, float wid, float ang, float det, float ben) {
    BranchResult br;
    float nx = 0, ny = 0;
    std::vector<Vec2> tlist;
    tlist.push_back(Vec2(nx, ny));
    float a0 = 0;
    const int g = 3;
    for (int i = 0; i < g; ++i) {
        a0 += (ben * 0.5f + (R() * ben) * 0.5f) * (RandChoice({-1, 1}));
        nx += std::cos(a0) * hei / (float)g;
        ny -= std::sin(a0) * hei / (float)g;
        tlist.push_back(Vec2(nx, ny));
    }
    float ta = std::atan2(tlist.back().y, tlist.back().x);
    for (auto& t : tlist) {
        float a = std::atan2(t.y, t.x);
        float d = std::sqrt(t.x * t.x + t.y * t.y);
        t.x = d * std::cos(a - ta + ang);
        t.y = d * std::sin(a - ta + ang);
    }

    float span = det;
    float tl = (float)(tlist.size() - 1) * span;
    float lx = 0, ly = 0;
    for (float i = 0; i < tl; i += 1.0f) {
        Vec2 lastp = tlist[(size_t)std::floor(i / span)];
        Vec2 nextp = tlist[(size_t)std::ceil(i / span)];
        float p = std::fmod(i, span) / span;
        float px = lastp.x * (1 - p) + nextp.x * p;
        float py = lastp.y * (1 - p) + nextp.y * p;
        float an = std::atan2(py - ly, px - lx);
        float woff = (Noise(i * 0.3f) - 0.5f) * wid * hei / 80.0f;
        float b = (p == 0) ? R() * wid : 0;
        float nw = wid * (((tl - i) / tl) * 0.5f + 0.5f);
        br.a.push_back(Vec2(px + std::cos(an + kPiF * 0.5f) * (nw + woff + b),
                            py + std::sin(an + kPiF * 0.5f) * (nw + woff + b)));
        br.b.push_back(Vec2(px + std::cos(an - kPiF * 0.5f) * (nw - woff + b),
                            py + std::sin(an - kPiF * 0.5f) * (nw - woff + b)));
        lx = px; ly = py;
    }
    return br;
}

// twig(): the small leafy shoots at branch tips.
void Twig(Sink& s, float tx, float ty, int dep, float ang, float sca, float wid, int dir,
          bool leaf, float leafSize) {
    const int tl = 10;
    float hs = R() * 0.5f + 0.5f;
    std::vector<Vec2> twlist;
    float a0 = ((R() * kPiF) / 6.0f) * (float)dir + ang;
    for (int i = 0; i < tl; ++i) {
        float fun2 = -1.0f / std::pow((float)i / (float)tl + 1.0f, 5.0f) + 1.0f;
        float mx = (float)dir * fun2 * 50.0f * sca * hs;
        float my = -(float)i * 5.0f * sca;
        float a = std::atan2(my, mx);
        float d = std::pow(mx * mx + my * my, 0.5f);
        float nx = std::cos(a + a0) * d;
        float ny = std::sin(a + a0) * d;
        twlist.push_back(Vec2(nx + tx, ny + ty));
        if ((i == tl / 3 || i == (tl * 2) / 3) && dep > 0) {
            Twig(s, nx + tx, ny + ty, dep - 1, ang, sca * 0.8f, wid,
                 dir * (RandChoice({-1, 1})), leaf, leafSize);
        }
        if (i == tl - 1 && leaf) {
            for (int j = 0; j < 5; ++j) {
                float dj = ((float)j - 2.5f) * 5.0f;
                Blob(s,
                     nx + tx + std::cos(ang) * dj * wid,
                     ny + ty + (std::sin(ang) * dj - leafSize / (float)(dep + 1)) * wid,
                     (15.0f + 12.0f * R()) * wid, (6.0f + 3.0f * R()) * wid,
                     ang * 0.5f + kPiF * 0.5f + kPiF * 0.2f * (R() - 0.5f),
                     InkAlpha(P(), 0.5f + (float)dep * 0.2f), 0.5f, &BlobFunLeaf);
            }
        }
    }
    Stroke(s, twlist, 1.0f, InkAlpha(P(), 0.5f), 0.5f, 1.0f,
           [](float x) { return std::cos(x * kPiF * 0.5f); });
}

// barkify(): bark marks and ridges along a trunk.
void Barkify(Sink& s, float x, float y, const BranchResult& tr) {
    auto bark = [&](float bx, float by, float wid, float ang) {
        float len = 10.0f + 10.0f * R();
        float noi = 0.5f;
        const float reso = 20.0f;
        std::vector<Vec2> lalist;
        for (int i = 0; i <= (int)reso; ++i) {
            float p = ((float)i / reso) * 2.0f;
            float xo = len * 0.5f - std::fabs(p - 1.0f) * len;
            float yo = (detail::BlobFunDefault(p) * wid) * 0.5f;
            float a = std::atan2(yo, xo);
            float l = std::sqrt(xo * xo + yo * yo);
            lalist.push_back(Vec2(l, a));
        }
        std::vector<float> nslist;
        float n0 = R() * 10.0f;
        for (int i = 0; i <= (int)reso; ++i) nslist.push_back(Noise((float)i * 0.05f, n0));
        LoopNoise(nslist);
        std::vector<Vec2> brklist;
        for (size_t i = 0; i < lalist.size(); ++i) {
            float ns = nslist[i] * noi + (1.0f - noi);
            brklist.push_back(Vec2(bx + std::cos(lalist[i].y + ang) * lalist[i].x * ns,
                                   by + std::sin(lalist[i].y + ang) * lalist[i].x * ns));
        }
        float fr = R();
        Stroke(s, brklist, 0.8f, InkAlpha(P(), 0.4f), 0.0f, 0.0f,
               [fr](float x) { return std::sin((x + fr) * kPiF * 3.0f); });
    };

    for (size_t i = 2; i + 1 < tr.a.size(); ++i) {
        float a0 = std::atan2(tr.a[i].y - tr.a[i - 1].y, tr.a[i].x - tr.a[i - 1].x);
        float a1 = std::atan2(tr.b[i].y - tr.b[i - 1].y, tr.b[i].x - tr.b[i - 1].x);
        float p = R();
        float nx = tr.a[i].x * (1 - p) + tr.b[i].x * p;
        float ny = tr.a[i].y * (1 - p) + tr.b[i].y * p;
        if (R() < 0.2f) {
            Blob(s, nx + x, ny + y, 15.0f, 6.0f - std::fabs(p - 0.5f) * 10.0f,
                 (a0 + a1) * 0.5f, InkAlpha(P(), 0.6f), 1.0f);
        } else {
            bark(nx + x, ny + y, 5.0f - std::fabs(p - 0.5f) * 10.0f, (a0 + a1) * 0.5f);
        }
        if (R() < 0.05f) {
            float jl = R() * 2.0f + 2.0f;
            bool useA = RandChoice({0, 1}) == 0;
            float xa = useA ? tr.a[i].x : tr.b[i].x;
            float ya = useA ? tr.a[i].y : tr.b[i].y;
            float aa = useA ? a0 : a1;
            for (int j = 0; j < (int)jl; ++j) {
                Blob(s, xa + x + std::cos(aa) * ((float)j - jl * 0.5f) * 4.0f,
                     ya + y + std::sin(aa) * ((float)j - jl * 0.5f) * 4.0f,
                     4.0f + 6.0f * R(), 4.0f, a0 + kPiF * 0.5f, InkAlpha(P(), 0.6f));
            }
        }
    }

    std::vector<Vec2> trf = tr.a;
    for (int i = (int)tr.b.size() - 1; i >= 0; --i) trf.push_back(tr.b[i]);
    std::vector<std::vector<Vec2>> rglist(1);
    for (const auto& pt : trf) {
        if (R() < 0.5f) rglist.push_back({});
        else rglist.back().push_back(pt);
    }
    for (size_t i = 0; i < rglist.size(); ++i) {
        rglist[i] = Div(rglist[i], 4);
        for (auto& pt : rglist[i]) {
            pt.x += (Noise((float)i, (float)(&pt - rglist[i].data()) * 0.1f, 1) - 0.5f) *
                    (15.0f + 5.0f * RandGaussian());
            pt.y += (Noise((float)i, (float)(&pt - rglist[i].data()) * 0.1f, 2) - 0.5f) *
                    (15.0f + 5.0f * RandGaussian());
        }
        std::vector<Vec2> shifted;
        for (const auto& v : rglist[i]) shifted.push_back(Vec2(v.x + x, v.y + y));
        Stroke(s, shifted, 1.5f, InkAlpha(P(), 0.7f), 0.5f, 0.0f);
    }
}

// The shared "white trunk fill + dark center stroke" finisher of tree04/05/06/08.
void TrunkFinish(Sink& s, std::vector<Vec2> trmlist, float x, float y, const Color& col) {
    std::vector<Vec2> shifted;
    shifted.reserve(trmlist.size());
    for (const auto& v : trmlist) shifted.push_back(Vec2(v.x + x, v.y + y));
    s.Poly(shifted, true, P().paper, true, col, 0.0f, true);
    if (trmlist.size() > 2) {
        trmlist.erase(trmlist.begin());
        trmlist.pop_back();
        shifted.clear();
        for (const auto& v : trmlist) shifted.push_back(Vec2(v.x + x, v.y + y));
        Stroke(s, shifted, 2.5f, InkAlpha(P(), 0.4f + R() * 0.1f), 0.9f, 0.0f,
               [](float) { return std::sin(1.0f); });
    }
}

} // namespace

namespace Tree {

void tree01(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    const int reso = 10;
    std::vector<Vec2> nslist;
    for (int i = 0; i < reso; ++i)
        nslist.push_back(Vec2(Noise((float)i * 0.5f), Noise((float)i * 0.5f, 0.5f)));

    std::vector<Vec2> line1, line2;
    for (int i = 0; i < reso; ++i) {
        float nx = x;
        float ny = y - ((float)i * o.hei) / (float)reso;
        if (i >= reso / 4) {
            int jcount = (int)(((float)(reso - i)) / 5.0f);
            for (int j = 0; j < jcount; ++j) {
                Blob(s,
                     nx + (R() - 0.5f) * o.wid * 1.2f * (float)(reso - i),
                     ny + (R() - 0.5f) * o.wid,
                     R() * 20.0f * (float)(reso - i) * 0.2f + 10.0f,
                     R() * 6.0f + 3.0f,
                     ((R() - 0.5f) * kPiF) / 6.0f,
                     LeafCol(col, 0.2f));
            }
        }
        line1.push_back(Vec2(nx + (nslist[i].x - 0.5f) * o.wid - o.wid * 0.5f, ny));
        line2.push_back(Vec2(nx + (nslist[i].y - 0.5f) * o.wid + o.wid * 0.5f, ny));
    }
    s.Poly(line1, false, Color(), true, col, 1.5f);
    s.Poly(line2, false, Color(), true, col, 1.5f);
}

void tree02(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    for (int i = 0; i < o.clu; ++i) {
        Blob(s,
             x + RandGaussian() * (float)o.clu * 4.0f,
             y + RandGaussian() * (float)o.clu * 4.0f,
             R() * o.hei * 0.75f + o.hei * 0.5f,
             R() * o.wid * 0.75f + o.wid * 0.5f,
             kPiF * 0.5f,
             col, 0.5f, &BlobFunLeaf);
    }
}

void tree03(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    const int reso = 10;
    std::vector<Vec2> nslist;
    for (int i = 0; i < reso; ++i)
        nslist.push_back(Vec2(Noise((float)i * 0.5f), Noise((float)i * 0.5f, 0.5f)));

    Sink blobs;
    std::vector<Vec2> line1, line2;
    for (int i = 0; i < reso; ++i) {
        float ben = o.benX ? o.benX((float)i / (float)reso) * 100.0f : 0.0f;
        float nx = x + ben;
        float ny = y - ((float)i * o.hei) / (float)reso;
        if (i >= reso / 5) {
            int jcount = (reso - i) * 2;
            for (int j = 0; j < jcount; ++j) {
                float shapeF = std::log(50.0f * (float)(reso - i) / (float)reso + 1.0f) / 3.95f;
                float ox = R() * o.wid * 2.0f * shapeF;
                Blob(blobs,
                     nx + ox * (float)(RandChoice({-1, 1})),
                     ny + (R() - 0.5f) * o.wid * 2.0f,
                     ox * 2.0f,
                     R() * 6.0f + 3.0f,
                     ((R() - 0.5f) * kPiF) / 6.0f,
                     LeafCol(col, 0.2f));
            }
        }
        line1.push_back(Vec2(nx + (((nslist[i].x - 0.5f) * o.wid - o.wid * 0.5f) * (float)(reso - i)) / (float)reso, ny));
        line2.push_back(Vec2(nx + (((nslist[i].y - 0.5f) * o.wid + o.wid * 0.5f) * (float)(reso - i)) / (float)reso, ny));
    }
    std::vector<Vec2> lc = line1;
    for (int i = (int)line2.size() - 1; i >= 0; --i) lc.push_back(line2[i]);
    // line1/line2 are already in world space (nx/ny include x/y); the original
    // emits them as-is. Adding the anchor here as well would paint every trunk
    // mask at twice its tree's position.
    s.Poly(lc, true, P().paper, true, col, 1.5f, true);
    for (auto& sh : blobs.shapes) s.Add(std::move(sh));
}

void tree04(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    float hei = o.hei > 0 ? o.hei : 300;
    float wid = o.wid > 0 ? o.wid : 6;

    Sink txcanv, twcanv;
    BranchResult tr = MakeBranch(hei, wid, -kPiF * 0.5f, 10.0f, kPiF * 0.2f);
    Barkify(txcanv, x, y, tr);
    std::vector<Vec2> trlist = tr.a;
    for (int i = (int)tr.b.size() - 1; i >= 0; --i) trlist.push_back(tr.b[i]);

    std::vector<Vec2> trmlist;
    for (size_t i = 0; i < trlist.size(); ++i) {
        bool cut = (i >= (size_t)((float)trlist.size() * 0.3f) &&
                    i <= (size_t)((float)trlist.size() * 0.7f) && R() < 0.1f) ||
                   i == trlist.size() / 2 - 1;
        if (cut) {
            float ba = kPiF * 0.2f - kPiF * 1.4f * (i > trlist.size() / 2 ? 1.0f : 0.0f);
            BranchResult br = MakeBranch(hei * (R() + 1.0f) * 0.3f, wid * 0.5f, ba, 10.0f,
                                         kPiF * 0.2f);
            br.a.erase(br.a.begin());
            br.b.erase(br.b.begin());
            for (auto& v : br.a) v += trlist[i];
            for (auto& v : br.b) v += trlist[i];
            Barkify(txcanv, x, y, br);
            for (size_t j = 0; j < br.a.size(); ++j) {
                if (R() < 0.2f || j == br.a.size() - 1) {
                    Twig(twcanv, br.a[j].x + trlist[i].x + x, br.a[j].y + trlist[i].y + y, 1,
                         ba > -kPiF * 0.5f ? ba : ba + kPiF, (0.5f * hei) / 300.0f,
                         hei / 300.0f, ba > -kPiF * 0.5f ? 1 : -1, true, 12.0f);
                }
            }
            std::vector<Vec2> brl = br.a;
            for (int k = (int)br.b.size() - 1; k >= 0; --k) brl.push_back(br.b[k]);
            for (auto& v : brl) trmlist.push_back(v);
        } else {
            trmlist.push_back(trlist[i]);
        }
    }
    TrunkFinish(s, trmlist, x, y, col);
    for (auto& sh : txcanv.shapes) s.Add(std::move(sh));
    for (auto& sh : twcanv.shapes) s.Add(std::move(sh));
}

void tree05(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    float hei = o.hei > 0 ? o.hei : 300;
    float wid = o.wid > 0 ? o.wid : 5;

    Sink txcanv, twcanv;
    BranchResult tr = MakeBranch(hei, wid, -kPiF * 0.5f, 10.0f, 0.0f);
    Barkify(txcanv, x, y, tr);
    std::vector<Vec2> trlist = tr.a;
    for (int i = (int)tr.b.size() - 1; i >= 0; --i) trlist.push_back(tr.b[i]);

    std::vector<Vec2> trmlist;
    for (size_t i = 0; i < trlist.size(); ++i) {
        float p = std::fabs((float)i - (float)trlist.size() * 0.5f) / ((float)trlist.size() * 0.5f);
        bool cut = (i >= (size_t)((float)trlist.size() * 0.2f) &&
                    i <= (size_t)((float)trlist.size() * 0.8f) && i % 3 == 0 && R() > p) ||
                   i == trlist.size() / 2 - 1;
        if (cut) {
            float bar = R() * 0.2f;
            float ba = -bar * kPiF - (1 - bar * 2) * kPiF * (i > trlist.size() / 2 ? 1.0f : 0.0f);
            BranchResult br = MakeBranch(hei * (0.3f * p - R() * 0.05f), wid * 0.5f, ba, 10.0f, 0.5f);
            br.a.erase(br.a.begin());
            br.b.erase(br.b.begin());
            for (auto& v : br.a) v += trlist[i];
            for (auto& v : br.b) v += trlist[i];
            for (size_t j = 0; j < br.a.size(); ++j) {
                if (j % 20 == 0 || j == br.a.size() - 1) {
                    Twig(twcanv, br.a[j].x + trlist[i].x + x, br.a[j].y + trlist[i].y + y, 0,
                         ba > -kPiF * 0.5f ? ba : ba + kPiF, (0.2f * hei) / 300.0f, hei / 300.0f,
                         ba > -kPiF * 0.5f ? 1 : -1, true, 5.0f);
                }
            }
            std::vector<Vec2> brl = br.a;
            for (int k = (int)br.b.size() - 1; k >= 0; --k) brl.push_back(br.b[k]);
            for (auto& v : brl) trmlist.push_back(v);
        } else {
            trmlist.push_back(trlist[i]);
        }
    }
    TrunkFinish(s, trmlist, x, y, col);
    for (auto& sh : txcanv.shapes) s.Add(std::move(sh));
    for (auto& sh : twcanv.shapes) s.Add(std::move(sh));
}

void tree06(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    float hei = o.hei > 0 ? o.hei : 100;
    float wid = o.wid > 0 ? o.wid : 6;

    Sink txcanv, twcanv;

    // Recursive limb splitter (the original's fracTree closure).
    std::function<std::vector<Vec2>(float, float, int, float, float, float)> fracTree =
        [&](float xoff, float yoff, int dep, float fhei, float fwid, float fang) {
            BranchResult tr = MakeBranch(fhei, fwid, fang, fhei / 20.0f, kPiF * 0.2f);
            Barkify(txcanv, xoff, yoff, tr);
            std::vector<Vec2> trlist = tr.a;
            for (int i = (int)tr.b.size() - 1; i >= 0; --i) trlist.push_back(tr.b[i]);

            std::vector<Vec2> trmlist;
            for (size_t i = 0; i < trlist.size(); ++i) {
                float p = std::fabs((float)i - (float)trlist.size() * 0.5f) /
                          ((float)trlist.size() * 0.5f);
                (void)p;
                bool cut = ((R() < 0.025f &&
                             i >= (size_t)((float)trlist.size() * 0.2f) &&
                             i <= (size_t)((float)trlist.size() * 0.8f)) ||
                            i == trlist.size() / 2 - 1 || i == trlist.size() / 2 + 1) && dep > 0;
                if (cut) {
                    float bar = 0.02f + R() * 0.08f;
                    float ba = bar * kPiF - bar * 2 * kPiF * (i > trlist.size() / 2 ? 1.0f : 0.0f);
                    std::vector<Vec2> brl = fracTree(
                        trlist[i].x + xoff, trlist[i].y + yoff, dep - 1,
                        fhei * (0.7f + R() * 0.2f), fwid * 0.6f, fang + ba);
                    for (size_t j = 0; j < brl.size(); ++j) {
                        if (R() < 0.03f) {
                            Twig(twcanv, brl[j].x + trlist[i].x + xoff,
                                 brl[j].y + trlist[i].y + yoff, 2,
                                 ba * (R() * 0.5f + 0.75f), 0.3f, 1.0f, ba > 0 ? 1 : -1,
                                 false, 0.0f);
                        }
                    }
                    for (auto& v : brl) trmlist.push_back(Vec2(v.x + trlist[i].x, v.y + trlist[i].y));
                } else {
                    trmlist.push_back(trlist[i]);
                }
            }
            return trmlist;
        };

    std::vector<Vec2> trmlist = fracTree(x, y, 3, hei, wid, -kPiF * 0.5f);
    TrunkFinish(s, trmlist, x, y, col);
    for (auto& sh : txcanv.shapes) s.Add(std::move(sh));
    for (auto& sh : twcanv.shapes) s.Add(std::move(sh));
}

void tree07(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : Color(1, 1, 1, 1);
    float hei = o.hei > 0 ? o.hei : 60;
    float wid = o.wid > 0 ? o.wid : 4;

    const int reso = 10;
    std::vector<Vec2> nslist;
    for (int i = 0; i < reso; ++i)
        nslist.push_back(Vec2(Noise((float)i * 0.5f), Noise((float)i * 0.5f, 0.5f)));

    std::vector<Vec2> line1, line2;
    std::vector<std::vector<Vec2>> T;
    for (int i = 0; i < reso; ++i) {
        float ben = o.benX ? o.benX((float)i / (float)reso) * 100.0f
                           : std::sqrt((float)i / (float)reso) * 0.2f * 100.0f;
        float nx = x + ben;
        float ny = y - ((float)i * hei) / (float)reso;
        if (i >= reso / 4) {
            float bpl = 0;  // (unused marker; blob length below)
            (void)bpl;
            auto pts = BlobPts(nx + (R() - 0.5f) * wid * 1.2f * (float)(reso - i) * 0.5f,
                               ny + (R() - 0.5f) * wid * 0.5f,
                               R() * 50.0f + 20.0f, R() * 12.0f + 12.0f,
                               (-R() * kPiF) / 6.0f, 0.5f, &BlobFunBamboo);
            auto tris = Triangulate(pts, 50.0f, true, false);
            for (auto& t : tris) T.push_back(std::move(t));
        }
        line1.push_back(Vec2(nx + (nslist[i].x - 0.5f) * wid - wid * 0.5f, ny));
        line2.push_back(Vec2(nx + (nslist[i].y - 0.5f) * wid + wid * 0.5f, ny));
    }
    {
        std::vector<Vec2> lc = line1;
        for (int i = (int)line2.size() - 1; i >= 0; --i) lc.push_back(line2[i]);
        auto tris = Triangulate(lc, 50.0f, true, true);
        for (auto it = tris.rbegin(); it != tris.rend(); ++it)
            T.insert(T.begin(), std::move(*it));
    }
    const Paint& gp = P();
    for (const auto& tri : T) {
        Vec2 m = MidPt(tri.data(), (int)tri.size());
        float c = Noise(m.x * 0.02f, m.y * 0.02f);                // 0..1
        float t = 0.12f + c * 0.55f;                              // ink fraction
        Color co(gp.paper.r + (gp.ink.r - gp.paper.r) * t,
                 gp.paper.g + (gp.ink.g - gp.paper.g) * t,
                 gp.paper.b + (gp.ink.b - gp.paper.b) * t, 0.85f);
        s.Poly(tri, true, co, true, co, 0.0f);
    }
}

void tree08(Sink& s, float x, float y, const TreeOpts& o) {
    Color col = o.colSet ? o.col : InkAlpha(P(), 0.5f);
    float hei = o.hei > 0 ? o.hei : 80;
    float wid = o.wid > 0 ? o.wid : 1;

    float ang = NormRand(-1, 1) * kPiF * 0.2f;

    BranchResult tr = MakeBranch(hei, wid, -kPiF * 0.5f + ang, hei / 20.0f, kPiF * 0.2f);
    std::vector<Vec2> trlist = tr.a;
    for (int i = (int)tr.b.size() - 1; i >= 0; --i) trlist.push_back(tr.b[i]);

    Sink twcanv;

    // Recursive fine-branch twigs.
    std::function<void(float, float, int, float, float, float)> fracTree =
        [&](float xoff, float yoff, int dep, float fang, float len, float ben) {
            auto fun = (dep == 0) ? std::function<float(float)>(
                                        [](float x) { return std::cos(0.5f * kPiF * x); })
                                  : std::function<float(float)>([](float) { return 1.0f; });
            Vec2 spt(xoff, yoff);
            std::vector<Vec2> trmlist = { Vec2(xoff, yoff), Vec2(xoff + len, yoff) };
            trmlist = Div(trmlist, 10);
            float bfunSign = (RandChoice({0, 1}) == 0) ? 1.0f : -1.0f;
            for (size_t i = 0; i < trmlist.size(); ++i)
                trmlist[i].y += std::sin((float)i / (float)trmlist.size() * kPiF) * 2.0f * bfunSign;
            for (auto& pt : trmlist) {
                float d = PDist(pt, spt);
                float a = std::atan2(pt.y - spt.y, pt.x - spt.x);
                pt.x = spt.x + d * std::cos(a + fang);
                pt.y = spt.y + d * std::sin(a + fang);
            }
            Stroke(twcanv, trmlist, 0.8f, InkAlpha(P(), 0.5f), 0.5f, 1.0f, fun);
            if (dep != 0) {
                float nben = ben + (float)(RandChoice({-1, 1})) * kPiF * 0.001f * (float)(dep * dep);
                Vec2 ept(spt.x + std::cos(fang) * len, spt.y + std::sin(fang) * len);
                if (R() < 0.5f) {
                    fracTree(ept.x, ept.y, dep - 1,
                             fang + nben + kPiF * (RandChoice({NormRand(-1.0f, 0.5f), NormRand(0.5f, 1.0f)})) * 0.2f,
                             len * NormRand(0.8f, 0.9f), nben);
                    fracTree(ept.x, ept.y, dep - 1,
                             fang + nben + kPiF * (RandChoice({NormRand(-1.0f, -0.5f), NormRand(0.5f, 1.0f)})) * 0.2f,
                             len * NormRand(0.8f, 0.9f), nben);
                } else {
                    fracTree(ept.x, ept.y, dep - 1, fang + nben,
                             len * NormRand(0.8f, 0.9f), nben);
                }
            }
        };

    for (size_t i = 0; i < trlist.size(); ++i) {
        if (R() < 0.2f) {
            fracTree(x + trlist[i].x, y + trlist[i].y, (int)(4.0f * R()),
                     -kPiF * 0.5f - ang * R(), 20.0f, 0.0f);
        } else if (i == trlist.size() / 2) {
            fracTree(x + trlist[i].x, y + trlist[i].y, 3, -kPiF * 0.5f + ang, 25.0f, 0.0f);
        }
    }

    std::vector<Vec2> shifted;
    shifted.reserve(trlist.size());
    for (const auto& v : trlist) shifted.push_back(Vec2(v.x + x, v.y + y));
    s.Poly(shifted, true, P().paper, true, col, 0.0f, true);
    Stroke(s, shifted, 2.5f, InkAlpha(P(), 0.6f + R() * 0.1f), 0.9f, 0.0f,
           [](float) { return std::sin(1.0f); });
    for (auto& sh : twcanv.shapes) s.Add(std::move(sh));
}

} // namespace Tree

} // namespace lp::ss
