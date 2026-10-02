// Live Shan Shui - shared internals for the library's translation units.
// Not part of the public API; the generators in ShanShuiMount.cpp /
// ShanShuiArch.cpp / ShanShuiWorld.cpp build on these the way the original's
// JS closures shared stroke/blob/texture.
#pragma once
#include "ShanShuiLib.h"

namespace lp::ss {
namespace detail {

constexpr float kPiF = 3.14159265358979f;
constexpr float kTauF = 6.28318530717959f;

// Blob profiles from the original.
float BlobFunDefault(float x);      // pow(sin(pi x), .5)
float BlobFunLeaf(float x);         // the twig-leaf profile
float BlobFunBamboo(float x);       // tree07's tapered profile
float BlobFunLog(float x);          // tree03's log profile

using Fun1 = float (*)(float);

std::vector<Vec2> BlobPts(float x, float y, float len, float wid, float ang, float noi,
                          Fun1 fun);
void Blob(Sink& s, float x, float y, float len, float wid, float ang, const Color& col,
          float noi = 0.5f, Fun1 fun = nullptr);
void BlobPtsInto(Sink& s, const std::vector<Vec2>& pts, const Color& col);

// Tapered brush stroke; wid = brush half-width, noi = noise fraction, out =
// outline width around the resulting polygon, fun = width profile.
void Stroke(Sink& s, const std::vector<Vec2>& ptlist, float wid, const Color& col,
            float noi = 0.5f, float out = 1.0f,
            const std::function<float(float)>& fun = std::function<float(float)>());

enum TexDis { kTexThirds = 0, kTexEdges = 1, kTexNearEdges = 2, kTexSquare = 3 };

void Texture(Sink& s, const std::vector<std::vector<Vec2>>& ptlist, float xof, float yof,
             float tex, float wid, float len, float sha, const Paint& p, const Color& inkCol,
             float aBase = 0.0f, float aRange = 0.3f, TexDis dis = kTexThirds,
             float noiConst = 0.0f);

// Flip every point of a point-list-list around x = `axis` (the original's Arch.flip).
void FlipAll(std::vector<std::vector<Vec2>>& ptlist, float axis);

} // namespace detail

// Ground patch for the flat-topped mountains (Mount.flatDec in the original).
struct MountBound { float xmin = 0, xmax = 0, ymin = 0, ymax = 0; };

namespace Mount {
void flatDec(Sink& s, float xoff, float yoff, const MountBound& grbd);
} // namespace Mount

} // namespace lp::ss
