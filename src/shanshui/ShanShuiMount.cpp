// Live Shan Shui - library port, part 3: the Mount family (mountain,
// flatMount + its decorations, distMount, rock, foot) and water. Ports of the
// original JS with paint-order preserved exactly (the rim trees that get half
// covered by the paper mask depend on it).
#include "ShanShuiInternal.h"
#include <cmath>
#include <algorithm>


namespace lp::ss {

using detail::kPiF;
using detail::Blob;
using detail::Stroke;
using detail::Texture;
using detail::TexDis;
using detail::kTexEdges;
using detail::kTexNearEdges;
using detail::kTexSquare;

namespace {

Paint& P() { return GPaint(); }

// The white paper mask: hides ink beneath it and reads as paper.
inline void PaperPoly(Sink& s, std::vector<Vec2> pts) {
    s.Poly(std::move(pts), true, P().paper, false, Color(), 0.0f, true);
}

// foot(): pale ground strokes spilling from the mountain's base.
void Foot(Sink& s, const std::vector<std::vector<Vec2>>& ptlist, float xof, float yoff) {
    std::vector<std::vector<Vec2>> ftlist;
    const int span = 10;
    int ni = 0;
    for (int i = 0; i < (int)ptlist.size() - 2; ++i) {
        if (i != ni) continue;
        ni = std::min(ni + (RandChoice({1, 2})), (int)ptlist.size() - 1);
        ftlist.push_back({});
        ftlist.push_back({});
        for (int j = 0; j < std::min((int)ptlist[i].size() / 8, 10); ++j) {
            ftlist[ftlist.size() - 2].push_back(
                Vec2(ptlist[i][j].x + Noise((float)j * 0.1f, (float)i) * 10.0f, ptlist[i][j].y));
            ftlist[ftlist.size() - 1].push_back(
                Vec2(ptlist[i][ptlist[i].size() - 1 - j].x -
                         Noise((float)j * 0.1f, (float)i) * 10.0f,
                     ptlist[i][ptlist[i].size() - 1 - j].y));
        }
        std::reverse(ftlist[ftlist.size() - 2].begin(), ftlist[ftlist.size() - 2].end());
        std::reverse(ftlist[ftlist.size() - 1].begin(), ftlist[ftlist.size() - 1].end());
        for (int j = 0; j < span; ++j) {
            float p = (float)j / (float)span;
            float x1 = ptlist[i][0].x * (1 - p) + ptlist[ni][0].x * p;
            float y1 = ptlist[i][0].y * (1 - p) + ptlist[ni][0].y * p;
            float x2 = ptlist[i].back().x * (1 - p) + ptlist[ni].back().x * p;
            float y2 = ptlist[i].back().y * (1 - p) + ptlist[ni].back().y * p;
            float vib = -1.7f * (p - 1) * std::pow(p, 1.0f / 5.0f);
            y1 += vib * 5.0f + Noise(xof * 0.05f, (float)i) * 5.0f;
            y2 += vib * 5.0f + Noise(xof * 0.05f, (float)i) * 5.0f;
            ftlist[ftlist.size() - 2].push_back(Vec2(x1, y1));
            ftlist[ftlist.size() - 1].push_back(Vec2(x2, y2));
        }
    }
    for (const auto& f : ftlist) PaperPoly(s, f);
    for (const auto& f : ftlist) {
        std::vector<Vec2> shifted;
        shifted.reserve(f.size());
        for (const auto& v : f) shifted.push_back(Vec2(v.x + xof, v.y + yoff));
        Stroke(s, shifted, 1.0f, InkAlpha(P(), 0.1f + R() * 0.1f));
    }
}

using Bound = MountBound;

Bound BoundOf(const std::vector<Vec2>& plist) {
    Bound b;
    b.xmin = b.ymin = 1e30f;
    b.xmax = b.ymax = -1e30f;
    for (const auto& p : plist) {
        b.xmin = std::min(b.xmin, p.x);
        b.xmax = std::max(b.xmax, p.x);
        b.ymin = std::min(b.ymin, p.y);
        b.ymax = std::max(b.ymax, p.y);
    }
    return b;
}

} // namespace

namespace Mount {

void mountain(Sink& s, float xoff, float yoff, float seed, const MountOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 100.0f + R() * 400.0f;
    float wid = (o.wid >= 0) ? o.wid : 400.0f + R() * 200.0f;
    float tex = (o.tex >= 0) ? o.tex : 200.0f;
    float veg = o.veg ? P().veg : 0.0f;

    std::vector<std::vector<Vec2>> ptlist;
    const int reso0 = 10, reso1 = 50;
    float hoff = 0;
    for (int j = 0; j < reso0; ++j) {
        hoff += (R() * yoff) / 100.0f;
        ptlist.push_back({});
        for (int i = 0; i < reso1; ++i) {
            float x = ((float)i / (float)reso1 - 0.5f) * kPiF;
            float y = std::cos(x);
            y *= Noise(x + 10.0f, (float)j * 0.15f, seed);
            float p = 1.0f - (float)j / (float)reso0;
            ptlist.back().push_back(Vec2((x / kPiF) * wid * p, -y * hei * p + hoff));
        }
    }

    // Vegetation helper: gather candidate points by a growth rule, then accept
    // each by a proof rule (the original's closure trio).
    auto vegetate = [&](const std::function<void(float, float)>& treeFn,
                        const std::function<bool(int, int)>& growth,
                        const std::function<bool(std::vector<Vec2>&, size_t)>& proof) {
        std::vector<Vec2> veglist;
        for (size_t i = 0; i < ptlist.size(); ++i)
            for (size_t j = 0; j < ptlist[i].size(); ++j)
                if (growth((int)i, (int)j)) veglist.push_back(ptlist[i][j]);
        for (size_t i = 0; i < veglist.size(); ++i)
            if (proof(veglist, i)) treeFn(veglist[i].x, veglist[i].y);
    };
    auto always = [](std::vector<Vec2>&, size_t) { return true; };

    // RIM (painted before the paper mask, so the ridge covers their feet).
    vegetate(
        [&](float x, float y) {
            Tree::tree02(s, x + xoff, y + yoff - 5.0f,
                         {0, 0, InkAlpha(P(), Noise(x * 0.01f, y * 0.01f) * 0.15f + 0.5f), true,
                          0.5f, 2, nullptr});
        },
        [&](int i, int j) {
            float ns = Noise((float)j * 0.1f, seed);
            return i == 0 && ns * ns * ns < 0.1f &&
                   std::fabs(ptlist[i][j].y) / hei > 0.2f;
        },
        always);

    // WHITE BG
    {
        std::vector<Vec2> bg = ptlist[0];
        bg.push_back(Vec2(0, (float)reso0 * 4.0f));
        for (auto& v : bg) { v.x += xoff; v.y += yoff; }
        PaperPoly(s, std::move(bg));
    }
    // OUTLINE
    {
        std::vector<Vec2> shifted;
        for (const auto& v : ptlist[0]) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, 3.0f, InkAlpha(P(), 0.3f), 1.0f);
    }

    Foot(s, ptlist, xoff, yoff);
    Texture(s, ptlist, xoff, yoff, tex, 1.5f, 0.2f, (RandChoice({0, 0, 0, 0, 1}) == 1) ? 5.0f : 0.0f,
            P(), P().ink);

    // TOP
    vegetate(
        [&](float x, float y) {
            Tree::tree02(s, x + xoff, y + yoff,
                         {0, 0, InkAlpha(P(), Noise(x * 0.01f, y * 0.01f) * 0.15f + 0.5f), true,
                          0.5f, 5, nullptr});
        },
        [&](int i, int j) {
            float ns = Noise((float)i * 0.1f, (float)j * 0.1f, seed + 2.0f);
            return ns * ns * ns < 0.1f * veg && std::fabs(ptlist[i][j].y) / hei > 0.5f;
        },
        always);

    if (veg > 0) {
        // MIDDLE
        vegetate(
            [&](float x, float y) {
                float ht = ((hei + y) / hei) * 70.0f;
                ht = ht * 0.3f + R() * ht * 0.7f;
                Tree::tree01(s, x + xoff, y + yoff,
                             {ht, R() * 3.0f + 1.0f,
                              InkAlpha(P(), Noise(x * 0.01f, y * 0.01f) * 0.15f + 0.3f), true,
                              0.5f, 5, nullptr});
            },
            [&](int i, int j) {
                float ns = Noise((float)i * 0.2f, (float)j * 0.05f, seed);
                return (j % 2) == 1 && ns * ns * ns * ns < 0.012f * veg &&
                       std::fabs(ptlist[i][j].y) / hei < 0.3f;
            },
            [](std::vector<Vec2>& veglist, size_t i) {
                int counter = 0;
                for (size_t j = 0; j < veglist.size(); ++j) {
                    if (i != j) {
                        float dx = veglist[i].x - veglist[j].x;
                        float dy = veglist[i].y - veglist[j].y;
                        if (dx * dx + dy * dy < 30.0f * 30.0f) ++counter;
                    }
                    if (counter > 2) return true;
                }
                return false;
            });

        // BOTTOM
        vegetate(
            [&](float x, float y) {
                float ht = ((hei + y) / hei) * 120.0f;
                ht = ht * 0.5f + R() * ht * 0.5f;
                float bc = R() * 0.1f;
                float bp = 1.0f;
                Tree::tree03(s, x + xoff, y + yoff,
                             {ht, 0,
                              InkAlpha(P(), Noise(x * 0.01f, y * 0.01f) * 0.15f + 0.3f), true,
                              0.5f, 5,
                              [bc, bp](float x) { return std::pow(x * bc, bp); }});
            },
            [&](int i, int j) {
                float ns = Noise((float)i * 0.2f, (float)j * 0.05f, seed);
                return (j == 0 || j == (int)ptlist[i].size() - 1) &&
                       ns * ns * ns * ns < 0.012f * veg;
            },
            always);
    }

    // BOTT ARCH
    vegetate(
        [&](float x, float y) {
            int tt = RandChoice({0, 0, 1, 1, 1, 2});
            if (tt == 1) {
                Arch::arch02(s, x + xoff, y + yoff, seed,
                             {10, NormRand(40, 70), R(), 5, RandChoice({1, 2, 2, 3}),
                              RandChoice({1, 2, 3}), false});
            } else if (tt == 2) {
                Arch::arch04(s, x + xoff, y + yoff, seed,
                             {15, 30, 0.7f, 5, RandChoice({1, 1, 1, 2, 2}), 1, false});
            }
        },
        [&](int i, int j) {
            float ns = Noise((float)i * 0.2f, (float)j * 0.05f, seed + 10.0f);
            return i != 0 && (j == 1 || j == (int)ptlist[i].size() - 2) &&
                   ns * ns * ns * ns < 0.008f;
        },
        always);

    // TOP ARCH
    vegetate(
        [&](float x, float y) {
            Arch::arch03(s, x + xoff, y + yoff, seed,
                         {10, 40.0f + R() * 20.0f, 0.7f, 5, RandChoice({5, 7}), 1, false});
        },
        [&](int i, int j) {
            return i == 1 && std::fabs((float)j - (float)ptlist[i].size() * 0.5f) < 1 &&
                   R() < 0.02f;
        },
        always);

    // TRANSM
    vegetate(
        [&](float x, float y) { Arch::transmissionTower01(s, x + xoff, y + yoff, seed); },
        [&](int i, int j) {
            float ns = Noise((float)i * 0.2f, (float)j * 0.05f, seed + 20.0f * kPiF);
            return i % 2 == 0 && (j == 1 || j == (int)ptlist[i].size() - 2) &&
                   ns * ns * ns * ns < 0.002f;
        },
        always);

    // BOTT ROCK
    vegetate(
        [&](float x, float y) {
            Mount::rock(s, x + xoff, y + yoff, seed,
                        {20.0f + R() * 20.0f, 20.0f + R() * 20.0f, 40.0f, true, 0.5f, 2.0f});
        },
        [&](int i, int j) {
            return (j == 0 || j == (int)ptlist[i].size() - 1) && R() < 0.1f;
        },
        always);
}

void flatMount(Sink& s, float xoff, float yoff, float seed, const MountOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 40.0f + R() * 400.0f;
    float wid = (o.wid >= 0) ? o.wid : 400.0f + R() * 200.0f;
    float tex = (o.tex >= 0) ? o.tex : 80.0f;
    float cho = o.cho;

    std::vector<std::vector<Vec2>> ptlist;
    const int reso0 = 5, reso1 = 50;
    float hoff = 0;
    std::vector<std::vector<Vec2>> flat(reso0);
    for (int j = 0; j < reso0; ++j) {
        hoff += (R() * yoff) / 100.0f;
        ptlist.push_back({});
        for (int i = 0; i < reso1; ++i) {
            float x = ((float)i / (float)reso1 - 0.5f) * kPiF;
            float y = std::cos(x * 2) + 1;
            y *= Noise(x + 10.0f, (float)j * 0.1f, seed);
            float p = 1.0f - ((float)j / (float)reso0) * 0.6f;
            float nx = (x / kPiF) * wid * p;
            float ny = -y * hei * p + hoff;
            float h = 100.0f;
            if (ny < -h * cho + hoff) {
                ny = -h * cho + hoff;
                if (flat[j].size() % 2 == 0) flat[j].push_back(Vec2(nx, ny));
            } else {
                if (flat[j].size() % 2 == 1) flat[j].push_back(ptlist[j].back());
            }
            ptlist[j].push_back(Vec2(nx, ny));
        }
    }

    // WHITE BG + OUTLINE
    {
        std::vector<Vec2> bg = ptlist[0];
        bg.push_back(Vec2(0, (float)reso0 * 4.0f));
        for (auto& v : bg) { v.x += xoff; v.y += yoff; }
        PaperPoly(s, std::move(bg));
    }
    {
        std::vector<Vec2> shifted;
        for (const auto& v : ptlist[0]) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, 3.0f, InkAlpha(P(), 0.3f), 1.0f);
    }

    Texture(s, ptlist, xoff, yoff, tex, 2.0f, 0.2f, 0.0f, P(), P().ink, 0.0f, 0.3f,
            kTexEdges);

    std::vector<Vec2> grlist1, grlist2;
    for (int i = 0; i < reso0; i += 2) {
        if (flat[i].size() >= 2) {
            grlist1.push_back(flat[i].front());
            grlist2.push_back(flat[i].back());
        }
    }
    if (grlist1.empty()) return;

    Vec2 wb(grlist1[0].x, grlist2[0].x);
    for (int i = 0; i < 3; ++i) {
        float p = 0.8f - (float)i * 0.2f;
        grlist1.insert(grlist1.begin(), Vec2(wb.x * p, grlist1.front().y - 5.0f));
        grlist2.insert(grlist2.begin(), Vec2(wb.y * p, grlist2.front().y - 5.0f));
    }
    wb = Vec2(grlist1.back().x, grlist2.back().x);
    for (int i = 0; i < 3; ++i) {
        float p = 0.6f - (float)(i * i) * 0.1f;
        grlist1.push_back(Vec2(wb.x * p, grlist1.back().y + 1.0f));
        grlist2.push_back(Vec2(wb.y * p, grlist2.back().y + 1.0f));
    }

    const float d = 5.0f;
    std::vector<Vec2> g1 = Div(grlist1, d);
    std::vector<Vec2> g2 = Div(grlist2, d);
    std::reverse(g1.begin(), g1.end());
    std::vector<Vec2> grlist = g1;
    for (auto& v : g2) grlist.push_back(v);
    grlist.push_back(g1.empty() ? Vec2(0, 0) : g1[0]);
    for (size_t i = 0; i < grlist.size(); ++i) {
        float v = (1.0f - std::fabs(std::fmod((float)i, d) - d * 0.5f) / (d * 0.5f)) * 0.12f;
        grlist[i].x *= 1 - v + Noise(grlist[i].y * 0.5f) * v;
    }
    PaperPoly(s, grlist);
    {
        std::vector<Vec2> shifted;
        for (const auto& v : grlist) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, 3.0f, InkAlpha(P(), 0.2f));
    }

    Bound grbd = BoundOf(grlist);
    flatDec(s, xoff, yoff, grbd);
}

void flatDec(Sink& s, float xoff, float yoff, const Bound& grbd) {
    int tt = RandChoice({0, 0, 1, 2, 3, 4});

    int rockCount = (int)(R() * 5.0f);
    for (int j = 0; j < rockCount; ++j) {
        rock(s, xoff + NormRand(grbd.xmin, grbd.xmax),
             yoff + (grbd.ymin + grbd.ymax) * 0.5f + NormRand(-10, 10) + 10.0f,
             R() * 100.0f, {10.0f + R() * 20.0f, 10.0f + R() * 20.0f, 40.0f, true, 0.5f, 2.0f});
    }
    int t8groups = RandChoice({0, 0, 1, 2});
    for (int j = 0; j < t8groups; ++j) {
        float xr = xoff + NormRand(grbd.xmin, grbd.xmax);
        float yr = yoff + (grbd.ymin + grbd.ymax) * 0.5f + NormRand(-5, 5) + 20.0f;
        int sub = (int)(2.0f + R() * 3.0f);
        for (int k = 0; k < sub; ++k) {
            Tree::tree08(s,
                         xr + std::min(std::max(NormRand(-30, 30), grbd.xmin), grbd.xmax), yr,
                         {60.0f + R() * 40.0f, 0, Color(), false, 0.5f, 5, nullptr});
        }
    }

    if (tt == 0) {
        int n = (int)(R() * 3.0f);
        for (int j = 0; j < n; ++j) {
            rock(s, xoff + NormRand(grbd.xmin, grbd.xmax),
                 yoff + (grbd.ymin + grbd.ymax) * 0.5f + NormRand(-5, 5) + 20.0f,
                 R() * 100.0f, {50.0f + R() * 20.0f, 40.0f + R() * 20.0f, 40.0f, true, 0.5f, 5.0f});
        }
    } else if (tt == 1) {
        float pmin = R() * 0.5f;
        float pmax = R() * 0.5f + 0.5f;
        float xmin = grbd.xmin * (1 - pmin) + grbd.xmax * pmin;
        float xmax = grbd.xmin * (1 - pmax) + grbd.xmax * pmax;
        for (float i = xmin; i < xmax; i += 30.0f) {
            Tree::tree05(s, xoff + i + 20.0f * NormRand(-1, 1),
                         yoff + (grbd.ymin + grbd.ymax) * 0.5f + 20.0f,
                         {100.0f + R() * 200.0f, 0, Color(), false, 0.5f, 5, nullptr});
        }
        int n = (int)(R() * 4.0f);
        for (int j = 0; j < n; ++j) {
            rock(s, xoff + NormRand(grbd.xmin, grbd.xmax),
                 yoff + (grbd.ymin + grbd.ymax) * 0.5f + NormRand(-5, 5) + 20.0f,
                 R() * 100.0f, {50.0f + R() * 20.0f, 40.0f + R() * 20.0f, 40.0f, true, 0.5f, 5.0f});
        }
    } else if (tt == 2) {
        int n = RandChoice({1, 1, 1, 1, 2, 2, 3});
        for (int i = 0; i < n; ++i) {
            float xr = NormRand(grbd.xmin, grbd.xmax);
            float yr = (grbd.ymin + grbd.ymax) * 0.5f;
            Tree::tree04(s, xoff + xr, yoff + yr + 20.0f, {});
            int rn = (int)(R() * 2.0f);
            for (int j = 0; j < rn; ++j) {
                rock(s, xoff + std::max(grbd.xmin, std::min(grbd.xmax, xr + NormRand(-50, 50))),
                     yoff + yr + NormRand(-5, 5) + 20.0f,
                     (float)j * (float)i * R() * 100.0f,
                     {50.0f + R() * 20.0f, 40.0f + R() * 20.0f, 40.0f, true, 0.5f, 5.0f});
            }
        }
    } else if (tt == 3) {
        int n = RandChoice({1, 1, 1, 1, 2, 2, 3});
        for (int i = 0; i < n; ++i) {
            Tree::tree06(s, xoff + NormRand(grbd.xmin, grbd.xmax),
                         yoff + (grbd.ymin + grbd.ymax) * 0.5f,
                         {60.0f + R() * 60.0f, 0, Color(), false, 0.5f, 5, nullptr});
        }
    } else if (tt == 4) {
        float pmin = R() * 0.5f;
        float pmax = R() * 0.5f + 0.5f;
        float xmin = grbd.xmin * (1 - pmin) + grbd.xmax * pmin;
        float xmax = grbd.xmin * (1 - pmax) + grbd.xmax * pmax;
        for (float i = xmin; i < xmax; i += 20.0f) {
            Tree::tree07(s, xoff + i + 20.0f * NormRand(-1, 1),
                         yoff + (grbd.ymin + grbd.ymax) * 0.5f + NormRand(-1, 1),
                         {NormRand(40, 80), 0, Color(), false, 0.5f, 5, nullptr});
        }
    }

    int bushCount = (int)(50.0f * R());
    for (int i = 0; i < bushCount; ++i) {
        Tree::tree02(s, xoff + NormRand(grbd.xmin, grbd.xmax), yoff + NormRand(grbd.ymin, grbd.ymax),
                     {});
    }

    int ts = RandChoice({0, 0, 0, 0, 1});
    if (ts == 1 && tt != 4) {
        Arch::arch01(s, xoff + NormRand(grbd.xmin, grbd.xmax),
                     yoff + (grbd.ymin + grbd.ymax) * 0.5f + 20.0f, R(),
                     {NormRand(80, 100), NormRand(160, 200), 0.7f, R(), -1, 1, false});
    }
}

void distMount(Sink& s, float xoff, float yoff, float seed, float hei, float len, float seg) {
    const float span = 10.0f;
    std::vector<std::vector<Vec2>> ptlist;
    for (int i = 0; i < (int)(len / span / seg); ++i) {
        ptlist.push_back({});
        for (int j = 0; j <= (int)seg; ++j) {
            float k = (float)(i * (int)seg + j);
            ptlist.back().push_back(
                Vec2(xoff + k * span,
                     yoff - hei * Noise(k * 0.05f, seed) *
                                std::pow(std::sin(kPiF * k / (len / span)), 0.5f)));
        }
        for (int j = 0; j <= (int)seg / 2; ++j) {
            float k = (float)(i * (int)seg + j * 2);
            ptlist.back().insert(ptlist.back().begin(),
                                 Vec2(xoff + k * span,
                                      yoff + 24.0f * Noise(k * 0.05f, 2.0f, seed) *
                                                 std::sin(kPiF * k / (len / span))));
        }
    }
    Paint& paint_ = P();
    for (const auto& pl : ptlist) {
        // The original painted these ridges in neutral gray and let SVG multiply
        // blending warm them against the paper; source-over needs the warmth mixed
        // into the color itself.
        auto getCol = [&](float x, float y) {
            float c = Noise(x * 0.02f, y * 0.02f, yoff);              // 0..1
            float t = 0.05f + (1.0f - c) * 0.16f;                     // ink fraction
            return Color(paint_.paper.r + (paint_.ink.r - paint_.paper.r) * t,
                         paint_.paper.g + (paint_.ink.g - paint_.paper.g) * t,
                         paint_.paper.b + (paint_.ink.b - paint_.paper.b) * t, 1.0f);
        };
        Vec2 lastp = pl.back();
        s.Poly(pl, true, getCol(lastp.x, lastp.y), false, Color(), 1.0f);
        auto tris = Triangulate(pl, 100.0f, true, false);
        for (const auto& t : tris) {
            Vec2 m = MidPt(t.data(), (int)t.size());
            Color co = getCol(m.x, m.y);
            s.Poly(t, true, co, true, co, 1.0f);
        }
    }
}

void rock(Sink& s, float xoff, float yoff, float seed, const MountOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 80.0f;
    float wid = (o.wid >= 0) ? o.wid : 100.0f;
    float tex = (o.tex >= 0) ? o.tex : 40.0f;
    float sha = (o.sha >= 0) ? o.sha : 10.0f;

    const int reso0 = 10, reso1 = 50;
    std::vector<std::vector<Vec2>> ptlist;
    for (int i = 0; i < reso0; ++i) {
        ptlist.push_back({});
        std::vector<float> nslist;
        for (int j = 0; j < reso1; ++j) nslist.push_back(Noise((float)i, (float)j * 0.2f, seed));
        LoopNoise(nslist);
        for (int j = 0; j < reso1; ++j) {
            float a = ((float)j / (float)reso1) * kPiF * 2.0f - kPiF * 0.5f;
            float l = (wid * hei) /
                      std::sqrt(std::pow(hei * std::cos(a), 2.0f) +
                                std::pow(wid * std::sin(a), 2.0f));
            l *= 0.7f + 0.3f * nslist[j];
            float p = 1.0f - (float)i / (float)reso0;
            float nx = std::cos(a) * l * p;
            float ny = -std::sin(a) * l * p;
            if (kPiF < a || a < 0) ny *= 0.2f;
            ny += hei * ((float)i / (float)reso0) * 0.2f;
            ptlist.back().push_back(Vec2(nx, ny));
        }
    }

    {
        std::vector<Vec2> bg = ptlist[0];
        bg.push_back(Vec2(xoff, yoff));
        PaperPoly(s, std::move(bg));
    }
    {
        std::vector<Vec2> shifted;
        for (const auto& v : ptlist[0]) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, 3.0f, InkAlpha(P(), 0.3f), 1.0f);
    }
    // The original's rock texture is a lighter gray than the mountain ink.
    Color lightInk((P().paper.r + P().ink.r) * 0.5f, (P().paper.g + P().ink.g) * 0.5f,
                   (P().paper.b + P().ink.b) * 0.5f, 1.0f);
    Texture(s, ptlist, xoff, yoff, tex, 3.0f, 0.2f, sha, P(), lightInk, 0.3f, 0.3f,
            kTexNearEdges);
}

} // namespace Mount

void water(Sink& s, float xoff, float yoff, float seed, float hei, float len, float clu) {
    (void)seed;
    std::vector<std::vector<Vec2>> ptlist;
    float yk = 0;
    for (int i = 0; i < (int)clu; ++i) {
        ptlist.push_back({});
        float xk = (R() - 0.5f) * (len / 8.0f);
        yk += R() * 5.0f;
        float lk = len / 4.0f + R() * (len / 4.0f);
        const float reso = 5.0f;
        for (float j = -lk; j < lk; j += reso) {
            ptlist.back().push_back(
                Vec2(j + xk, std::sin(j * 0.2f) * hei * Noise(j * 0.1f) - 20.0f + yk));
        }
    }
    for (size_t j = 1; j < ptlist.size(); ++j) {
        std::vector<Vec2> shifted;
        for (const auto& v : ptlist[j]) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, 1.0f, InkAlpha(P(), 0.3f + R() * 0.3f));
    }
}

} // namespace lp::ss
