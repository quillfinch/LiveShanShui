// Live Shan Shui - library port, part 4: the Arch family (huts, tiered
// buildings, pagodas, boats, transmission towers) and the Man stick figures,
// including their little hats and walking sticks. Ports of the original JS.
#include "ShanShuiInternal.h"
#include <cmath>
#include <algorithm>

namespace lp::ss {

using detail::kPiF;
using detail::Stroke;
using detail::FlipAll;

namespace {

struct Vec2i { int x, y; Vec2i(int X, int Y) : x(X), y(Y) {} };

Paint& P() { return GPaint(); }

void PaperPoly(Sink& s, std::vector<Vec2> pts) {
    s.Poly(std::move(pts), true, P().paper, false, Color(), 0.0f, true);
}

// The original's architecture decoration callback receives the front face of a
// box and returns extra stroke lines for it.
struct DecRect { Vec2 pul, pur, pdl, pdr; };
using DecFn = std::function<std::vector<std::vector<Vec2>>(const DecRect&)>;
DecFn NoDec = [](const DecRect&) { return std::vector<std::vector<Vec2>>(); };

std::vector<std::vector<Vec2>> Deco(int style, Vec2 pul, Vec2 pur, Vec2 pdl, Vec2 pdr,
                                    Vec2i hsp, Vec2i vsp) {
    std::vector<std::vector<Vec2>> plist;
    std::vector<Vec2> dl = Div({pul, pdl}, (float)vsp.y);
    std::vector<Vec2> dr = Div({pur, pdr}, (float)vsp.y);
    std::vector<Vec2> du = Div({pul, pur}, (float)hsp.y);
    std::vector<Vec2> dd = Div({pdl, pdr}, (float)hsp.y);

    auto at = [](const std::vector<Vec2>& v, size_t i) {
        return v[std::min(i, v.size() - 1)];
    };

    if (style == 1) {          // -| |-
        Vec2 mlu = at(du, (size_t)hsp.x);
        Vec2 mru = at(du, du.size() - 1 - (size_t)hsp.x);
        Vec2 mld = at(dd, (size_t)hsp.x);
        Vec2 mrd = at(dd, du.size() - 1 - (size_t)hsp.x);
        for (size_t i = (size_t)vsp.x; i + (size_t)vsp.x < dl.size(); i += (size_t)vsp.x) {
            Vec2 mml = at(Div({mlu, mld}, (float)vsp.y), i);
            Vec2 mmr = at(Div({mru, mrd}, (float)vsp.y), i);
            plist.push_back(Div({mml, dl[i]}, 5));
            plist.push_back(Div({mmr, dr[i]}, 5));
        }
        plist.push_back(Div({mlu, mld}, 5));
        plist.push_back(Div({mru, mrd}, 5));
    } else if (style == 2) {   // ||||
        for (size_t i = (size_t)hsp.x; i + (size_t)hsp.x < du.size(); i += (size_t)hsp.x) {
            plist.push_back(Div({du[i], dd[i]}, 5));
        }
    } else if (style == 3) {   // |##|
        Vec2 mlu = at(du, (size_t)hsp.x);
        Vec2 mru = at(du, du.size() - 1 - (size_t)hsp.x);
        Vec2 mld = at(dd, (size_t)hsp.x);
        Vec2 mrd = at(dd, du.size() - 1 - (size_t)hsp.x);
        for (size_t i = (size_t)vsp.x; i + (size_t)vsp.x < dl.size(); i += (size_t)vsp.x) {
            Vec2 mml = at(Div({mlu, mld}, (float)vsp.y), i);
            Vec2 mmr = at(Div({mru, mrd}, (float)vsp.y), i);
            Vec2 mmu = at(Div({mlu, mru}, (float)vsp.y), i);
            Vec2 mmd = at(Div({mld, mrd}, (float)vsp.y), i);
            plist.push_back(Div({mml, mmr}, 5));
            plist.push_back(Div({mmu, mmd}, 5));
        }
        plist.push_back(Div({mlu, mld}, 5));
        plist.push_back(Div({mru, mrd}, 5));
    }
    return plist;
}

// hut(): the thatched-roof cottage body.
void Hut(Sink& s, float xoff, float yoff, float hei, float wid, float tex) {
    const int reso0 = 10, reso1 = 10;
    std::vector<std::vector<Vec2>> ptlist;
    for (int i = 0; i < reso0; ++i) {
        ptlist.push_back({});
        float heir = hei + hei * 0.2f * R();
        for (int j = 0; j < reso1; ++j) {
            float nx = wid * ((float)i / (float)(reso0 - 1) - 0.5f) *
                       std::pow((float)j / (float)(reso1 - 1), 0.7f);
            float ny = heir * ((float)j / (float)(reso1 - 1));
            ptlist.back().push_back(Vec2(nx, ny));
        }
    }
    {
        std::vector<Vec2> front = ptlist[0];
        front.pop_back();
        std::vector<Vec2> back = ptlist.back();
        back.pop_back();
        std::reverse(back.begin(), back.end());
        for (auto& v : back) front.push_back(v);
        for (auto& v : front) { v.x += xoff; v.y += yoff; }
        PaperPoly(s, front);
    }
    auto outline = [&](const std::vector<Vec2>& pl) {
        std::vector<Vec2> shifted;
        for (const auto& v : pl) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        s.Poly(shifted, false, Color(), true, InkAlpha(P(), 0.3f), 2.0f);
    };
    outline(ptlist[0]);
    outline(ptlist.back());
    Texture(s, ptlist, xoff, yoff, tex, 1.0f, 0.25f, 0.0f, P(),
            Color(120.0f / 255.0f, 120.0f / 255.0f, 120.0f / 255.0f, 1.0f), 0.3f, 0.3f,
            detail::kTexSquare, 5.0f);
}

// box(): one storey of a building, optionally translucent (stroke-only).
void Box(Sink& s, float xoff, float yoff, float hei, float wid, float rot, float per,
         bool tra, bool bot, float wei, const DecFn& dec) {
    float mid = -wid * 0.5f + wid * rot;
    float bmid = -wid * 0.5f + wid * (1 - rot);
    std::vector<std::vector<Vec2>> ptlist;
    ptlist.push_back(Div({Vec2(-wid * 0.5f, -hei), Vec2(-wid * 0.5f, 0)}, 5));
    ptlist.push_back(Div({Vec2(wid * 0.5f, -hei), Vec2(wid * 0.5f, 0)}, 5));
    if (bot) {
        ptlist.push_back(Div({Vec2(-wid * 0.5f, 0), Vec2(mid, per)}, 5));
        ptlist.push_back(Div({Vec2(wid * 0.5f, 0), Vec2(mid, per)}, 5));
    }
    ptlist.push_back(Div({Vec2(mid, -hei), Vec2(mid, per)}, 5));
    if (tra) {
        if (bot) {
            ptlist.push_back(Div({Vec2(-wid * 0.5f, 0), Vec2(bmid, -per)}, 5));
            ptlist.push_back(Div({Vec2(wid * 0.5f, 0), Vec2(bmid, -per)}, 5));
        }
        ptlist.push_back(Div({Vec2(bmid, -hei), Vec2(bmid, -per)}, 5));
    }

    float surf = (rot < 0.5f) ? 2.0f : -1.0f;
    DecRect rc{Vec2(surf * wid * 0.5f, -hei), Vec2(mid, -hei + per),
               Vec2(surf * wid * 0.5f, 0), Vec2(mid, per)};
    for (auto& line : dec(rc)) ptlist.push_back(line);

    if (!tra) {
        PaperPoly(s, {Vec2(xoff - wid * 0.5f, yoff - hei), Vec2(xoff + wid * 0.5f, yoff - hei),
                      Vec2(xoff + wid * 0.5f, yoff), Vec2(xoff + mid, yoff + per),
                      Vec2(xoff - wid * 0.5f, yoff)});
    }
    for (const auto& pl : ptlist) {
        std::vector<Vec2> shifted;
        for (const auto& v : pl) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, wei, InkAlpha(P(), 0.4f), 1.0f, 0.0f,
               [](float) { return 1.0f; });
    }
}

// rail(): a rickety bridge railing (or a fence when fro=false).
void Rail(Sink& s, float xoff, float yoff, float seed, float hei, float wid, float rot,
          float per, int seg, float wei, bool tra, bool fro) {
    float mid = -wid * 0.5f + wid * rot;
    float bmid = -wid * 0.5f + wid * (1 - rot);
    std::vector<std::vector<Vec2>> ptlist;
    if (fro) {
        ptlist.push_back(Div({Vec2(-wid * 0.5f, 0), Vec2(mid, per)}, (float)seg));
        ptlist.push_back(Div({Vec2(mid, per), Vec2(wid * 0.5f, 0)}, (float)seg));
    }
    if (tra) {
        ptlist.push_back(Div({Vec2(-wid * 0.5f, 0), Vec2(bmid, -per)}, (float)seg));
        ptlist.push_back(Div({Vec2(bmid, -per), Vec2(wid * 0.5f, 0)}, (float)seg));
    }
    if (fro) {
        ptlist.push_back(Div({Vec2(-wid * 0.5f, -hei), Vec2(mid, -hei + per)}, (float)seg));
        ptlist.push_back(Div({Vec2(mid, -hei + per), Vec2(wid * 0.5f, -hei)}, (float)seg));
    }
    if (tra) {
        ptlist.push_back(Div({Vec2(-wid * 0.5f, -hei), Vec2(bmid, -hei - per)}, (float)seg));
        ptlist.push_back(Div({Vec2(bmid, -hei - per), Vec2(wid * 0.5f, -hei)}, (float)seg));
    }
    if (tra) {
        size_t open = (size_t)(R() * (float)ptlist.size()) % ptlist.size();
        // (The original indexes ptlist[(open+len)%len] here, which is `open`
        // again - the same rail loses two posts.)
        if (!ptlist[open].empty()) ptlist[open].pop_back();
        if (!ptlist[open].empty()) ptlist[open].pop_back();
    }

    size_t half = ptlist.size() / 2;
    for (size_t i = 0; i < half; ++i) {
        for (size_t j = 0; j < ptlist[i].size(); ++j) {
            size_t other = (half + i) % ptlist.size();
            ptlist[i][j].y += (Noise((float)i, (float)j * 0.5f, seed) - 0.5f) * hei;
            size_t oj = j % ptlist[other].size();
            ptlist[other][oj].y +=
                (Noise((float)i + 0.5f, (float)j * 0.5f, seed) - 0.5f) * hei;
            std::vector<Vec2> ln = Div({ptlist[i][j], ptlist[other][oj]}, 2);
            ln[0].x += (R() - 0.5f) * hei * 0.5f;
            s.Poly(ln, false, Color(), true, InkAlpha(P(), 0.5f), 2.0f);
        }
    }
    for (const auto& pl : ptlist) {
        std::vector<Vec2> shifted;
        for (const auto& v : pl) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, wei, InkAlpha(P(), 0.5f), 0.5f, 0.0f,
               [](float) { return 1.0f; });
    }
}

// roof(): the flared Chinese eave.
void Roof(Sink& s, float xoff, float yoff, float hei, float wid, float rot, float per,
          float cor, float wei) {
    auto opf = [&](std::vector<Vec2> pl) {
        if (rot < 0.5f) {
            for (auto& p : pl) p.x = -p.x;
            std::reverse(pl.begin(), pl.end());
        }
        return pl;
    };
    float rrot = rot < 0.5f ? 1 - rot : rot;
    float mid = -wid * 0.5f + wid * rrot;
    float quat = (mid + wid * 0.5f) * 0.5f - mid;

    std::vector<std::vector<Vec2>> ptlist;
    ptlist.push_back(Div(opf({Vec2(-wid * 0.5f + quat, -hei - per * 0.5f),
                              Vec2(-wid * 0.5f + quat * 0.5f, -hei * 0.5f - per * 0.25f),
                              Vec2(-wid * 0.5f - cor, 0)}), 5));
    ptlist.push_back(Div(opf({Vec2(mid + quat, -hei),
                              Vec2((mid + quat + wid * 0.5f) * 0.5f, -hei * 0.5f),
                              Vec2(wid * 0.5f + cor, 0)}), 5));
    ptlist.push_back(Div(opf({Vec2(mid + quat, -hei),
                              Vec2(mid + quat * 0.5f, -hei * 0.5f + per * 0.5f),
                              Vec2(mid + cor, per)}), 5));
    ptlist.push_back(Div(opf({Vec2(-wid * 0.5f - cor, 0), Vec2(mid + cor, per)}), 5));
    ptlist.push_back(Div(opf({Vec2(wid * 0.5f + cor, 0), Vec2(mid + cor, per)}), 5));
    ptlist.push_back(Div(opf({Vec2(-wid * 0.5f + quat, -hei - per * 0.5f),
                              Vec2(mid + quat, -hei)}), 5));

    {
        std::vector<Vec2> polist = opf({Vec2(-wid * 0.5f, 0),
                                        Vec2(-wid * 0.5f + quat, -hei - per * 0.5f),
                                        Vec2(mid + quat, -hei),
                                        Vec2(wid * 0.5f, 0),
                                        Vec2(mid, per)});
        for (auto& v : polist) { v.x += xoff; v.y += yoff; }
        PaperPoly(s, polist);
    }
    for (const auto& pl : ptlist) {
        std::vector<Vec2> shifted;
        for (const auto& v : pl) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, wei, InkAlpha(P(), 0.4f), 1.0f, 0.0f,
               [](float) { return 1.0f; });
    }
}

// pagroof(): the pagoda's upswept tier roof.
void Pagroof(Sink& s, float xoff, float yoff, float hei, float wid, float rot, float per,
             float cor, int sid, float wei) {
    (void)rot;
    std::vector<std::vector<Vec2>> ptlist;
    std::vector<Vec2> polist = {Vec2(0, -hei)};
    for (int i = 0; i < sid; ++i) {
        float fx = wid * ((float)i / (float)(sid - 1) - 0.5f);
        float fy = per * (1 - std::fabs((float)i / (float)(sid - 1) - 0.5f) * 2);
        float fxx = (wid + cor) * ((float)i / (float)(sid - 1) - 0.5f);
        if (i > 0) ptlist.push_back({ptlist.back()[2], Vec2(fxx, fy)});
        ptlist.push_back({Vec2(0, -hei), Vec2(fx * 0.5f, (-hei + fy) * 0.5f), Vec2(fxx, fy)});
        polist.push_back(Vec2(fxx, fy));
    }
    for (auto& v : polist) { v.x += xoff; v.y += yoff; }
    PaperPoly(s, polist);
    for (const auto& pl : ptlist) {
        std::vector<Vec2> shifted;
        for (const auto& v : Div(pl, 5)) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        Stroke(s, shifted, wei, InkAlpha(P(), 0.4f), 1.0f, 0.0f,
               [](float) { return 1.0f; });
    }
}

} // namespace

namespace Arch {

void arch01(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 70.0f;
    float wid = (o.wid >= 0) ? o.wid : 180.0f;
    float per = o.per;
    (void)seed;

    float p = 0.4f + R() * 0.2f;
    float h0 = hei * p;
    float h1 = hei * (1 - p);

    Hut(s, xoff, yoff - hei, h0, wid, 300.0f);
    Box(s, xoff, yoff, h1, wid * 2.0f / 3.0f, 0.7f, per, true, false, 3.0f, NoDec);
    Rail(s, xoff, yoff, seed, 10.0f, wid, 0.7f, per * 2, (int)(3.0f + R() * 3.0f), 1.0f,
         true, false);

    int mcnt = RandChoice({0, 1, 1, 2});
    if (mcnt == 1) {
        Man::man(s, xoff + NormRand(-wid / 3, wid / 3), yoff,
                 {0.42f, RandChoice({true, false}), 1, 0, {}});
    } else if (mcnt == 2) {
        Man::man(s, xoff + NormRand(-wid / 4, -wid / 5), yoff, {0.42f, false, 1, 0, {}});
        Man::man(s, xoff + NormRand(wid / 5, wid / 4), yoff, {0.42f, true, 1, 0, {}});
    }
    Rail(s, xoff, yoff, seed, 10.0f, wid, 0.7f, per * 2, (int)(3.0f + R() * 3.0f), 1.0f,
         false, true);
}

void arch02(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 10.0f;
    float wid = (o.wid >= 0) ? o.wid : 50.0f;
    float rot = o.rot;
    float per = o.per;
    int sto = (o.sto >= 0) ? o.sto : 3;
    int sty = o.sty;
    bool rai = o.rai;

    static const Vec2i kHsp[4] = {{0, 0}, {1, 5}, {1, 5}, {1, 4}};
    static const Vec2i kVsp[4] = {{0, 0}, {1, 2}, {1, 2}, {1, 3}};

    float hoff = 0;
    for (int i = 0; i < sto; ++i) {
        Box(s, xoff, yoff - hoff, hei, wid * std::pow(0.85f, (float)i), rot, per, false, true,
            1.5f,
            [sty, &kHsp, &kVsp](const DecRect& rc) {
                return Deco(sty, rc.pul, rc.pur, rc.pdl, rc.pdr, kHsp[sty], kVsp[sty]);
            });
        if (rai) {
            Rail(s, xoff, yoff - hoff, (float)i * 0.2f, hei * 0.5f,
                 wid * std::pow(0.85f, (float)i) * 1.1f, rot, per, 4, 0.5f, false, true);
        }
        // The original occasionally hangs a "Pizza Hut" sign here; the port
        // keeps the RNG roll but leaves the facade blank.
        if (sto == 1 && R() < 1.0f / 3.0f) {
            // sign slot (deliberately blank)
        }
        Roof(s, xoff, yoff - hoff - hei, hei, wid * std::pow(0.9f, (float)i), rot, per, 5.0f,
             1.5f);
        hoff += hei * 1.5f;
    }
}

void arch03(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 10.0f;
    float wid = (o.wid >= 0) ? o.wid : 50.0f;
    float rot = o.rot;
    float per = o.per;
    int sto = (o.sto >= 0) ? o.sto : 7;

    float hoff = 0;
    for (int i = 0; i < sto; ++i) {
        Box(s, xoff, yoff - hoff, hei, wid * std::pow(0.85f, (float)i), rot, per * 0.5f, false,
            true, 1.5f,
            [](const DecRect& rc) {
                return Deco(1, rc.pul, rc.pur, rc.pdl, rc.pdr, Vec2i(1, 4), Vec2i(1, 2));
            });
        Rail(s, xoff, yoff - hoff, (float)i * 0.2f, hei * 0.5f,
             wid * std::pow(0.85f, (float)i) * 1.1f, rot, per * 0.5f, 5, 0.5f, false, true);
        Pagroof(s, xoff, yoff - hoff - hei, hei * 1.5f, wid * std::pow(0.9f, (float)i), rot,
                per, 10.0f, 4, 1.5f);
        hoff += hei * 1.5f;
    }
}

void arch04(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o) {
    float hei = (o.hei >= 0) ? o.hei : 15.0f;
    float wid = (o.wid >= 0) ? o.wid : 30.0f;
    float rot = o.rot;
    float per = o.per;
    int sto = (o.sto >= 0) ? o.sto : 2;

    float hoff = 0;
    for (int i = 0; i < sto; ++i) {
        Box(s, xoff, yoff - hoff, hei, wid * std::pow(0.85f, (float)i), rot, per * 0.5f, true,
            true, 1.5f, NoDec);
        Rail(s, xoff, yoff - hoff, (float)i * 0.2f, hei / 3.0f,
             wid * std::pow(0.85f, (float)i) * 1.2f, rot, per * 0.5f, 3, 0.5f, true, true);
        Pagroof(s, xoff, yoff - hoff - hei, hei, wid * std::pow(0.9f, (float)i), rot, per,
                10.0f, 4, 1.5f);
        hoff += hei * 1.2f;
    }
}

void boat01(Sink& s, float xoff, float yoff, float seed, float sca, bool fli) {
    (void)seed;
    float dir = fli ? -1.0f : 1.0f;
    ManOpts mo;
    mo.sca = 0.5f * sca;
    mo.fli = !fli;
    mo.hat = 2;
    mo.item = 1;
    float lenB[9] = {0, 30, 20, 30, 10, 30, 30, 30, 30};
    for (int i = 0; i < 9; ++i) mo.lenBase[i] = lenB[i];
    Man::man(s, xoff + 20.0f * sca * dir, yoff, mo);

    std::vector<Vec2> plist1, plist2;
    for (float i = 0; i < 120.0f * sca; i += 5.0f * sca) {
        plist1.push_back(Vec2(i * dir, std::pow(std::sin(i / (120.0f * sca) * kPiF), 0.5f) * 7.0f * sca));
        plist2.push_back(Vec2(i * dir, std::pow(std::sin(i / (120.0f * sca) * kPiF), 0.5f) * 10.0f * sca));
    }
    std::vector<Vec2> plist = plist1;
    for (int i = (int)plist2.size() - 1; i >= 0; --i) plist.push_back(plist2[i]);
    {
        std::vector<Vec2> shifted;
        for (const auto& v : plist) shifted.push_back(Vec2(v.x + xoff, v.y + yoff));
        PaperPoly(s, shifted);
        Stroke(s, shifted, 1.0f, InkAlpha(P(), 0.4f), 0.5f, 1.0f,
               [](float x) { return std::sin(x * kPiF * 2.0f); });
    }
}

void transmissionTower01(Sink& s, float xoff, float yoff, float seed, float hei, float wid) {
    (void)seed;
    auto toGlobal = [&](const std::vector<Vec2>& pl) {
        std::vector<Vec2> out;
        for (const auto& v : pl) out.push_back(Vec2(v.x + xoff, v.y + yoff));
        return out;
    };
    auto quickstroke = [&](std::vector<Vec2> pl) {
        Stroke(s, Div(pl, 5), 1.0f, InkAlpha(P(), 0.4f), 0.5f, 1.0f,
               [](float) { return 0.5f; });
    };

    Vec2 p00(-wid * 0.05f, -hei), p01(wid * 0.05f, -hei);
    Vec2 p10(-wid * 0.1f, -hei * 0.9f), p11(wid * 0.1f, -hei * 0.9f);
    Vec2 p20(-wid * 0.2f, -hei * 0.5f), p21(wid * 0.2f, -hei * 0.5f);
    Vec2 p30(-wid * 0.5f, 0), p31(wid * 0.5f, 0);

    const float bch[3][2] = {{0.7f, -0.85f}, {1.0f, -0.675f}, {0.7f, -0.5f}};
    for (int i = 0; i < 3; ++i) {
        Vec2 l(-bch[i][0] * wid, bch[i][1] * hei);
        Vec2 r(bch[i][0] * wid, bch[i][1] * hei);
        quickstroke({l, r});
        quickstroke({l, Vec2(0, (bch[i][1] - 0.05f) * hei)});
        quickstroke({r, Vec2(0, (bch[i][1] - 0.05f) * hei)});
        quickstroke({l, Vec2(l.x, (bch[i][1] + 0.1f) * hei)});
        quickstroke({r, Vec2(r.x, (bch[i][1] + 0.1f) * hei)});
    }

    std::vector<Vec2> l10 = Div({p00, p10, p20, p30}, 5);
    std::vector<Vec2> l11 = Div({p01, p11, p21, p31}, 5);
    for (size_t i = 0; i + 1 < l10.size(); ++i) {
        quickstroke({l10[i], l11[i + 1]});
        quickstroke({l11[i], l10[i + 1]});
    }
    quickstroke({p00, p01});
    quickstroke({p10, p11});
    quickstroke({p20, p21});
    quickstroke({p00, p10, p20, p30});
    quickstroke({p01, p11, p21, p31});
}

} // namespace Arch

// --- Man -----------------------------------------------------------------------

namespace {

struct ManPts {
    Vec2 p[9];
};

// Skeleton parent chain: 0 torso base; 1 spine, 3 leg; 2 head, 5/7 arms, 4 foot,
// 6/8 hands (the original's sct tree).
const int kManParent[9] = {-1, 0, 1, 0, 3, 1, 5, 1, 5};

// expand(): offsets a polyline sideways by a width profile (cloth silhouettes).
void Expand(const std::vector<Vec2>& ptlist, const std::function<float(float)>& wfun,
            std::vector<Vec2>& vtx0, std::vector<Vec2>& vtx1) {
    float n0 = R() * 10.0f;
    (void)n0;
    for (size_t i = 1; i + 1 < ptlist.size(); ++i) {
        float w = wfun((float)i / (float)ptlist.size());
        float a1 = std::atan2(ptlist[i].y - ptlist[i - 1].y, ptlist[i].x - ptlist[i - 1].x);
        float a2 = std::atan2(ptlist[i].y - ptlist[i + 1].y, ptlist[i].x - ptlist[i + 1].x);
        float a = (a1 + a2) * 0.5f;
        if (a < a2) a += kPiF;
        vtx0.push_back(Vec2(ptlist[i].x + w * std::cos(a), ptlist[i].y + w * std::sin(a)));
        vtx1.push_back(Vec2(ptlist[i].x - w * std::cos(a), ptlist[i].y - w * std::sin(a)));
    }
    size_t l = ptlist.size() - 1;
    float a0 = std::atan2(ptlist[1].y - ptlist[0].y, ptlist[1].x - ptlist[0].x) - kPiF * 0.5f;
    float a1 = std::atan2(ptlist[l].y - ptlist[l - 1].y, ptlist[l].x - ptlist[l - 1].x) - kPiF * 0.5f;
    float w0 = wfun(0), w1 = wfun(1);
    vtx0.insert(vtx0.begin(), Vec2(ptlist[0].x + w0 * std::cos(a0), ptlist[0].y + w0 * std::sin(a0)));
    vtx1.insert(vtx1.begin(), Vec2(ptlist[0].x - w0 * std::cos(a0), ptlist[0].y - w0 * std::sin(a0)));
    vtx0.push_back(Vec2(ptlist[l].x + w1 * std::cos(a1), ptlist[l].y + w1 * std::sin(a1)));
    vtx1.push_back(Vec2(ptlist[l].x - w1 * std::cos(a1), ptlist[l].y - w1 * std::sin(a1)));
}

// tranpoly(): maps a normalized garment outline onto the p0->p1 segment.
std::vector<Vec2> Tranpoly(Vec2 p0, Vec2 p1, std::vector<Vec2> ptlist) {
    for (auto& v : ptlist) v.x = -v.x;
    float ang = std::atan2(p1.y - p0.y, p1.x - p0.x) - kPiF * 0.5f;
    float scl = PDist(p0, p1);
    std::vector<Vec2> qlist;
    for (const auto& v : ptlist) {
        float d = PDist(v, Vec2(0, 0));
        float a = std::atan2(v.y, v.x);
        qlist.push_back(Vec2(p0.x + d * scl * std::cos(ang + a),
                             p0.y + d * scl * std::sin(ang + a)));
    }
    return qlist;
}

std::vector<Vec2> Flipper(std::vector<Vec2> plist) {
    for (auto& v : plist) v.x = -v.x;
    return plist;
}

void Hat01(Sink& s, Vec2 p0, Vec2 p1, bool fli) {
    float seed = R();
    auto f = [&](std::vector<Vec2> pl) { return fli ? Flipper(pl) : pl; };
    s.Poly(Tranpoly(p0, p1,
                    f({Vec2(-0.3f, 0.5f), Vec2(0.3f, 0.8f), Vec2(0.2f, 1), Vec2(0, 1.1f),
                       Vec2(-0.3f, 1.15f), Vec2(-0.55f, 1), Vec2(-0.65f, 0.5f)})),
          true, InkAlpha(P(), 0.8f), false, Color(), 0.0f);
    std::vector<Vec2> qlist1;
    for (int i = 0; i < 10; ++i) {
        qlist1.push_back(Vec2(-0.3f - Noise((float)i * 0.2f, seed) * (float)i * 0.1f,
                              0.5f - (float)i * 0.3f));
    }
    s.Poly(Tranpoly(p0, p1, f(qlist1)), false, Color(), true, InkAlpha(P(), 0.8f), 1.0f);
}

void Hat02(Sink& s, Vec2 p0, Vec2 p1, bool fli) {
    auto f = [&](std::vector<Vec2> pl) { return fli ? Flipper(pl) : pl; };
    s.Poly(Tranpoly(p0, p1,
                    f({Vec2(-0.3f, 0.5f), Vec2(-1.1f, 0.5f), Vec2(-1.2f, 0.6f),
                       Vec2(-1.1f, 0.7f), Vec2(-0.3f, 0.8f), Vec2(0.3f, 0.8f),
                       Vec2(1.0f, 0.7f), Vec2(1.3f, 0.6f), Vec2(1.2f, 0.5f),
                       Vec2(0.3f, 0.5f)})),
          true, InkAlpha(P(), 0.8f), false, Color(), 0.0f);
}

void Stick01(Sink& s, Vec2 p0, Vec2 p1, bool fli) {
    float seed = R();
    std::vector<Vec2> qlist1;
    const int l = 12;
    for (int i = 0; i < l; ++i) {
        qlist1.push_back(Vec2(-Noise((float)i * 0.1f, seed) * 0.1f *
                                  std::sin((float)i / (float)l * kPiF) * 5.0f,
                              (float)i * 0.3f));
    }
    s.Poly(Tranpoly(p0, p1, fli ? Flipper(qlist1) : qlist1), false, Color(), true,
           InkAlpha(P(), 0.5f), 1.0f);
}

} // namespace

void Man::man(Sink& s, float xoff, float yoff, const ManOpts& o) {
    float sca = o.sca;
    bool fli = o.fli;

    float ang[9];
    ang[0] = 0;
    ang[1] = -kPiF * 0.5f;
    ang[2] = 0;
    ang[3] = (kPiF * 0.25f) * R();
    ang[4] = (kPiF * 0.75f) * R();
    ang[5] = kPiF * 0.75f;
    ang[6] = -kPiF * 0.25f;
    ang[7] = -kPiF * 0.75f - (kPiF * 0.25f) * R();
    ang[8] = -kPiF * 0.25f;

    float len[9];
    for (int i = 0; i < 9; ++i) len[i] = o.lenBase[i] * sca;

    auto grot = [&](int ind) {
        float rot = 0;
        int i = ind;
        while (i >= 0) {
            rot += ang[i];
            i = kManParent[i];
        }
        return rot;
    };
    auto gpos = [&](int ind) {
        Vec2 pos(0, 0);
        int i = ind;
        while (i >= 0) {
            float a = grot(i);
            pos.x += len[i] * std::cos(a);
            pos.y += len[i] * std::sin(a);
            i = kManParent[i];
        }
        return pos;
    };

    ManPts pts;
    for (int i = 0; i < 9; ++i) pts.p[i] = gpos(i);
    yoff -= pts.p[4].y;

    auto toGlobal = [&](const Vec2& v) {
        return Vec2((fli ? -1.0f : 1.0f) * v.x + xoff, v.y + yoff);
    };

    auto cloth = [&](std::vector<Vec2> plist, const std::function<float(float)>& fun) {
        std::vector<Vec2> tlist = Bezmh(plist, 2.0f);
        std::vector<Vec2> t1, t2;
        Expand(tlist, fun, t1, t2);
        std::vector<Vec2> outline = t1;
        for (int i = (int)t2.size() - 1; i >= 0; --i) outline.push_back(t2[i]);
        std::vector<Vec2> shifted;
        for (const auto& v : outline) shifted.push_back(toGlobal(v));
        PaperPoly(s, shifted);
        {
            std::vector<Vec2> a;
            for (const auto& v : t1) a.push_back(toGlobal(v));
            Stroke(s, a, 1.0f, InkAlpha(P(), 0.5f));
        }
        {
            std::vector<Vec2> b;
            for (const auto& v : t2) b.push_back(toGlobal(v));
            Stroke(s, b, 1.0f, InkAlpha(P(), 0.6f));
        }
    };

    auto fsleeve = [&](float x) {
        return sca * 8.0f * (std::sin(0.5f * x * kPiF) * std::pow(std::sin(x * kPiF), 0.1f) +
                             (1 - x) * 0.4f);
    };
    auto fbody = [&](float x) {
        return sca * 11.0f * (std::sin(0.5f * x * kPiF) * std::pow(std::sin(x * kPiF), 0.1f) +
                              (1 - x) * 0.5f);
    };
    auto fhead = [&](float x) {
        return sca * 7.0f * std::pow(std::max(0.0f, 0.25f - (x - 0.5f) * (x - 0.5f)), 0.3f);
    };

    if (o.item == 1) Stick01(s, toGlobal(pts.p[8]), toGlobal(pts.p[6]), fli);

    cloth({pts.p[1], pts.p[7], pts.p[8]}, fsleeve);
    cloth({pts.p[1], pts.p[0], pts.p[3], pts.p[4]}, fbody);
    cloth({pts.p[1], pts.p[5], pts.p[6]}, fsleeve);
    cloth({pts.p[1], pts.p[2]}, fhead);

    {
        std::vector<Vec2> hlist = Bezmh({pts.p[1], pts.p[2]}, 2.0f);
        std::vector<Vec2> h1, h2;
        Expand(hlist, fhead, h1, h2);
        h1.erase(h1.begin(), h1.begin() + (int)((float)h1.size() * 0.1f));
        h2.erase(h2.begin(), h2.begin() + (int)((float)h2.size() * 0.95f));
        std::vector<Vec2> outline = h1;
        for (int i = (int)h2.size() - 1; i >= 0; --i) outline.push_back(h2[i]);
        std::vector<Vec2> shifted;
        for (const auto& v : outline) shifted.push_back(toGlobal(v));
        s.Poly(shifted, true, InkAlpha(P(), 0.6f), false, Color(), 0.0f);
    }

    if (o.hat == 2) Hat02(s, toGlobal(pts.p[1]), toGlobal(pts.p[2]), fli);
    else Hat01(s, toGlobal(pts.p[1]), toGlobal(pts.p[2]), fli);
}

} // namespace lp::ss
