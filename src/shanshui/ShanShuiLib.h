// Live Shan Shui - procedural Chinese landscape painting engine.
//
// A native C++ port of "shan-shui-inf" by LingDong (https://github.com/LingDong-/shan-shui-inf,
// MIT license). The original is vanilla JavaScript that emits SVG <polyline>
// strings; this port keeps the algorithms and swaps the SVG emitter for Shape
// records (polygon + fill + stroke) that a Direct2D renderer turns into path
// geometry. Randomness is re-seeded per world, so a given seed always paints
// the same landscape.
//
// Not part of the original: the sun, bird flocks, mist bands and the collector's
// seal, which are added here in the same spirit.
#pragma once
#include "../core/Math.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lp::ss {

// ---------------------------------------------------------------------------
// Deterministic randomness: a Blum-Blum-Shub generator (s = s*s mod m), exact
// in 128-bit arithmetic. One global stream is consumed strictly in generation
// order (chunks are always extended left-to-right), which keeps whole worlds
// reproducible from a single seed.
// ---------------------------------------------------------------------------
class Prng {
public:
    void Seed(uint32_t x);
    float Next();                        // 0..1
    static Prng& Global();
    uint64_t State() const { return s_; }
    void Restore(uint64_t s) { s_ = s; } // for tests

private:
    uint64_t s_ = 12345;
    static constexpr uint64_t kP = 999979ull;
    static constexpr uint64_t kQ = 999983ull;
    static constexpr uint64_t kM = kP * kQ;
};

// Global convenience: mirrors the original's Math.random().
float R();

// p5.js-style Perlin noise (ported from the original, which borrowed it from
// p5). Table is seeded from the global world seed.
class Perlin {
public:
    void Seed(uint32_t seed);
    float Noise(float x, float y = 0.0f, float z = 0.0f);
    static Perlin& Global();

private:
    bool built_ = false;
    float table_[4096];
};

float Noise(float x, float y = 0.0f, float z = 0.0f);

// ---------------------------------------------------------------------------
// Small utilities ported from the original's Util/PolyTools.
// ---------------------------------------------------------------------------
Vec2 MidPt(const Vec2* pts, int count);
float PDist(const Vec2& a, const Vec2& b);
float Mapval(float v, float a0, float a1, float b0, float b1);

// Renormalizes a noise sample list so it loops smoothly from 0 to 1.
void LoopNoise(std::vector<float>& ns);

int RandChoice(int count);                       // uniform index into n choices
template <typename T>
T RandChoice(std::initializer_list<T> list) {
    const T* arr = list.begin();
    return arr[((size_t)((float)list.size() * R())) % list.size()];
}
float NormRand(float m, float M);                // uniform in [m, M]
float Wtrand(const std::function<float(float)>& f);  // rejection sampling
float RandGaussian();                            // ~[-1, 1], bell shaped

// Midpoint-smoothed quadratic bezier through control points.
std::vector<Vec2> Bezmh(const std::vector<Vec2>& p, float w = 1.0f);

// Resamples a polyline to roughly `reso` subdivisions per segment.
std::vector<Vec2> Div(const std::vector<Vec2>& plist, float reso);

// Ear-clipping triangulation (the original's PolyTools.triangulate).
std::vector<std::vector<Vec2>> Triangulate(const std::vector<Vec2>& plist,
                                           float area = 100.0f,
                                           bool convex = false,
                                           bool optimize = true);

// ---------------------------------------------------------------------------
// Shapes: the port's answer to SVG <polyline> strings.
// ---------------------------------------------------------------------------
struct Shape {
    std::vector<Vec2> pts;
    bool fill = false;  Color fil;    // closed filled polygon when set
    bool stroke = false; Color str;   // polyline outline when set
    float wid = 0;                    // stroke width
    bool paper = false;               // true when `fil` is the paper mask
                                     // (must not be tinted by ink scaling)
};

// Collects the output of the generators. Mirrors the original's string
// concatenation; order is paint order.
class Sink {
public:
    void Poly(std::vector<Vec2> pts, bool fill, Color fil, bool stroke, Color str, float wid,
              bool paper = false);
    void Add(Shape s) { shapes.push_back(std::move(s)); }
    std::vector<Shape> shapes;
};

// The painting's shared style, threaded through the generators so the panel's
// controls and the custom color can act on everything at once.
struct Paint {
    Color paper = Color::Hex(0xf5efe2);   // warm paper
    Color ink = Color::Hex(0x646464);     // neutral ink gray (rgb 100,100,100)
    float veg = 1.0f;                     // vegetation density multiplier
    float scale = 1.0f;                   // world scale multiplier for features
};

Color InkAlpha(const Paint& p, float a);     // ink color with alpha a
Color WithAlpha(const Color& c, float a);

// The active painting style. The original hardcoded its ink strings globally;
// this port threads one Paint through World::Reset and lets every generator
// read it, so the panel's custom color acts on the whole painting.
Paint& GPaint();

// ---------------------------------------------------------------------------
// Generators. Each appends shapes to `sink` in world coordinates.
// ---------------------------------------------------------------------------
struct TreeOpts {
    float hei = 50, wid = 3;
    Color col;
    bool colSet = false;
    float noi = 0.5f;
    int clu = 5;
    // Trunk bend profile (tree03/tree07), x in 0..1, multiplied by 100.
    std::function<float(float)> benX;
};

namespace Tree {
void tree01(Sink& s, float x, float y, const TreeOpts& o = {});
void tree02(Sink& s, float x, float y, const TreeOpts& o = {});
void tree03(Sink& s, float x, float y, const TreeOpts& o = {});
void tree04(Sink& s, float x, float y, const TreeOpts& o = {});
void tree05(Sink& s, float x, float y, const TreeOpts& o = {});
void tree06(Sink& s, float x, float y, const TreeOpts& o = {});
void tree07(Sink& s, float x, float y, const TreeOpts& o = {});
void tree08(Sink& s, float x, float y, const TreeOpts& o = {});
} // namespace Tree

struct MountOpts {
    float hei = -1;      // -1 = the original's random default
    float wid = -1;
    float tex = -1;      // -1 = per-generator default (mountain 200, flat 80, rock 40)
    bool veg = true;
    float cho = 0.5f;
    float sha = -1;
};

namespace Mount {
void mountain(Sink& s, float xoff, float yoff, float seed, const MountOpts& o = {});
void flatMount(Sink& s, float xoff, float yoff, float seed, const MountOpts& o = {});
void distMount(Sink& s, float xoff, float yoff, float seed,
               float hei = 300, float len = 2000, float seg = 5);
void rock(Sink& s, float xoff, float yoff, float seed, const MountOpts& o = {});
} // namespace Mount

struct ArchOpts {
    float hei = -1, wid = -1;
    float rot = 0.7f, per = 5;
    int sto = -1;        // -1 = original default
    int sty = 1;
    bool rai = false;
};

namespace Arch {
void arch01(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o = {});
void arch02(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o = {});
void arch03(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o = {});
void arch04(Sink& s, float xoff, float yoff, float seed, const ArchOpts& o = {});
void boat01(Sink& s, float xoff, float yoff, float seed, float sca = 1, bool fli = false);
void transmissionTower01(Sink& s, float xoff, float yoff, float seed,
                         float hei = 100, float wid = 20);
} // namespace Arch

struct ManOpts {
    float sca = 0.5f;
    bool fli = true;
    int hat = 1;              // 1 or 2
    int item = 0;             // 0 none, 1 stick
    float lenBase[9] = { 0, 30, 20, 30, 30, 30, 30, 30, 30 };
};

namespace Man {
void man(Sink& s, float xoff, float yoff, const ManOpts& o = {});
} // namespace Man

void water(Sink& s, float xoff, float yoff, float seed, float hei = 2, float len = 800,
           float clu = 10);

// Extras that were not in the original, drawn in the same style.
void sun(Sink& s, float x, float y, float r, const Paint& p);
void birdFlock(Sink& s, float x, float y, const Paint& p);
void sealStamp(Sink& s, float x, float y, float size);   // the red collector seal

// Procedural paper texture (the original's canvas-generated paper), one square
// RGBA tile, BGRA8 premultiplied with alpha 255. Independent RNG state.
void PaperTile(std::vector<uint32_t>& out, int size, uint32_t seed);

// ---------------------------------------------------------------------------
// The world: chunk planning and streaming (a port of the original's
// mountplanner/chunkloader/MEM machinery).
// ---------------------------------------------------------------------------
struct WorldItem {
    std::string tag;
    float x = 0, y = 0;              // y is paint order (smaller = farther)
    Sink sink;
};

class World {
public:
    static constexpr float kWindy = 800.0f;   // painting coordinate height
    static constexpr float kChunkW = 512.0f;

    // Resets everything and re-seeds the stream. Deterministic per seed.
    void Reset(uint32_t seed, const Paint& paint);

    // Extends the generated range to cover [xmin, xmax] (left to right only).
    void Ensure(float xmin, float xmax);

    // Frees everything left of `keepFrom` (the camera only moves right).
    void Trim(float keepFrom);

    // Items whose x falls in [xmin - margin, xmax + margin], in paint order.
    // Pointers stay valid until the next Ensure/Trim.
    void Collect(float xmin, float xmax, std::vector<const WorldItem*>& out) const;

    float GeneratedTo() const { return xmax_; }

private:
    std::vector<WorldItem> planChunk(float xmin, float xmax);

    Paint paint_;
    uint32_t seed_ = 0;
    std::vector<std::unique_ptr<WorldItem>> items_;  // stable addresses, sorted by y
    float xmin_ = 0, xmax_ = 0;
    std::vector<int> planMtx_;       // mountain-spacing counters per xstep column
    int planBase_ = 0;               // world-x / kXStep of column 0

public:
    static constexpr float kXStep = 5.0f;
};

} // namespace lp::ss
