// Live Shan Shui - desktop pet implementation. See Pet.h.
//
// The pet is a small topmost popup window with its own D2D device (same pattern
// as the control panel), rendered from the engine's frame loop. Six species are
// drawn procedurally from one shared pose state (VisualState), so the live pet
// and the --selftest renderer execute exactly the same drawing code. Stats
// (hunger/happiness/xp/age) are stored per species in pet.ini.
#include "Pet.h"
#include "../gfx/GfxDevice.h"
#include "../gfx/Scene.h"
#include "../core/Config.h"
#include "../core/Math.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace lp {

namespace {

constexpr wchar_t kPetClass[] = L"LiveShanShui.Pet";
constexpr float kW = (float)kPetWindowW, kH = (float)kPetWindowH;
constexpr float kDecayHungerPerSec = 100.0f / (6.0f * 3600.0f);     // 6 h to starve
constexpr float kDecayHappyPerSec = 100.0f / (9.0f * 3600.0f);      // 9 h to sad
constexpr float kSleepAfterSec = 600.0f;                            // 10 min idle
constexpr int   kMaxEmotes = 10;

// Emote kinds (floating reaction marks). Plain ints so Emote stays POD-simple.
enum { EmoteHeart = 0, EmoteSparkle, EmoteNote, EmoteZzz, EmoteCrumb, EmoteFeather, EmoteRing };

std::wstring PetStatsPath() {
    std::wstring p = Config::FilePath();
    size_t slash = p.find_last_of(L'\\');
    return (slash == std::wstring::npos) ? L"pet.ini" : p.substr(0, slash + 1) + L"pet.ini";
}

Pet* FromHandle(HWND h) { return (Pet*)GetWindowLongPtrW(h, GWLP_USERDATA); }

int xPos(HWND h) {
    RECT r{};
    return (h && GetWindowRect(h, &r)) ? (int)r.left : -1;
}

LRESULT CALLBACK PetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Pet* self = FromHandle(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (Pet*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (self) {
        LRESULT r = 0;
        if (self->HandleMessage(msg, wp, lp, &r)) return r;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Taskbar geometry: the tray (clock) rect, and the app-icon area that the pet
// must never cover. Children differ across Windows versions; every lookup has a
// sensible fallback.
bool TaskbarZones(RECT& tray, RECT& icons, RECT& clock) {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!t || !GetWindowRect(t, &tray)) return false;
    HWND notify = FindWindowExW(t, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify && GetWindowRect(notify, &clock)) {
        // got the real clock area
    } else {
        clock = { tray.right - 170, tray.top, tray.right, tray.bottom };
    }
    HWND ms = FindWindowExW(t, nullptr, L"MSTaskSwWClass", nullptr);
    if (ms && GetWindowRect(ms, &icons)) {
        // got the real app-icon area
    } else {
        icons = { tray.left + 200, tray.top, clock.left - 60, tray.bottom };
    }
    return true;
}

// Nearest horizontal position that keeps the pet inside the taskbar span but
// never overlapping the app-icon area.
int ClampPetX(int x, int petW, const RECT& tray, const RECT& icons) {
    const int margin = 8;
    int loA = tray.left + 4, hiA = icons.left - margin - petW;
    int loB = icons.right + margin, hiB = tray.right - 4 - petW;
    auto clampTo = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (hiA < loA) hiA = loA;
    if (hiB < loB) hiB = loB;
    int a = clampTo(x, loA, hiA);
    int b = clampTo(x, loB, hiB);
    return (std::abs((long)(a - x)) <= std::abs((long)(b - x))) ? a : b;
}

void EnsureClass(HINSTANCE instance) {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = PetProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPetClass;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    done = true;
}

// ---------------------------------------------------------------------------
// pose + drawing. RenderPetFrame() is the single renderer: the live window and
// --selftest both go through it, so the species code has one caller per pixel.
// ---------------------------------------------------------------------------

struct Emote {
    int type = -1;
    float life = 0, max = 1;      // remaining / total seconds
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    Color tint;
};

struct BallState {
    bool active = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    float life = 0;
};

// Everything the renderer needs. The live Impl fills it each frame; the selftest
// fills a representative pose directly.
struct VisualState {
    float time = 0;
    float wag = 0;              // tail / fin phase
    float blink = 0;            // 0 open .. 1 closed
    float lookX = 0, lookY = 0; // pupil offset, -1..1
    float bodyY = kH * 0.60f;
    float squash = 1.0f;        // 0.85 stretched tall .. 1.2 squashed flat
    float rot = 0;              // whole-creature rotation (panda roll)
    bool airborne = false;      // mid-jump (chick shows its legs)
    bool sleeping = false, hungry = false, sad = false, happy = false;
    bool card = false;
    float munch = 0;            // remaining feed-animation seconds
    int level = 1;
    float ageSec = 0;
    Emote emotes[kMaxEmotes];
    BallState ball;
    const wchar_t* bubble = nullptr;
    float bubbleLife = 0;
};

// Eyes that track `look` and blink shut. Shared by every species.
void PetEyes(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
             float cx, float eyeY, float dx, float r, const Color& iris) {
    const float lx = v.lookX * r * 0.55f, ly = v.lookY * r * 0.5f;
    if (v.blink > 0.6f) {
        draw::Line(dc, brush, cx - dx - r * 1.2f, eyeY, cx - dx + r * 1.2f, eyeY, 2.0f, iris);
        draw::Line(dc, brush, cx + dx - r * 1.2f, eyeY, cx + dx + r * 1.2f, eyeY, 2.0f, iris);
        return;
    }
    for (int s = -1; s <= 1; s += 2) {
        float ex = cx + dx * s;
        draw::Circle(dc, brush, ex + lx, eyeY + ly, r, iris);
        draw::Circle(dc, brush, ex + lx - r * 0.35f, eyeY + ly - r * 0.35f, r * 0.34f,
                     Color(1, 1, 1, 0.92f));
    }
}

void PetBlush(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
              float cx, float y, float dx, float r, const Color& pink, float alpha) {
    draw::Circle(dc, brush, cx - dx, y, r, pink.WithAlpha(alpha));
    draw::Circle(dc, brush, cx + dx, y, r, pink.WithAlpha(alpha));
}

// A small heart (emote + reactions). s = half width.
void PetHeart(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
              float x, float y, float s, const Color& c) {
    draw::Circle(dc, brush, x - s * 0.5f, y - s * 0.3f, s * 0.55f, c);
    draw::Circle(dc, brush, x + s * 0.5f, y - s * 0.3f, s * 0.55f, c);
    D2D1_POINT_2F tri[3] = {
        D2D1::Point2F(x - s, y - s * 0.15f),
        D2D1::Point2F(x + s, y - s * 0.15f),
        D2D1::Point2F(x, y + s * 0.9f),
    };
    draw::FillPolygon(dc, brush, tri, 3, c);
}

// Four-point sparkle.
void PetSparkle(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                float x, float y, float s, const Color& c) {
    draw::Line(dc, brush, x - s, y, x + s, y, 1.6f, c);
    draw::Line(dc, brush, x, y - s, x, y + s, 1.6f, c);
    draw::Line(dc, brush, x - s * 0.45f, y - s * 0.45f, x + s * 0.45f, y + s * 0.45f, 1.2f, c.WithAlpha(0.7f));
    draw::Line(dc, brush, x - s * 0.45f, y + s * 0.45f, x + s * 0.45f, y - s * 0.45f, 1.2f, c.WithAlpha(0.7f));
}

// --- species ----------------------------------------------------------------

void DrawCat(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    const float cx = kW * 0.5f, y = v.bodyY;
    const float rx = 40.0f * v.squash, ry = 32.0f * (2.0f - v.squash);
    Color fur = Color::Hex(0xf2ddb8);
    Color shade = Color::Hex(0xd9bd92);
    Color pink = Color::Hex(0xf2a3b1);
    Color dark = Color::Hex(0x3a3327);

    // Tail: a wagging curve behind the body.
    float wag = std::sin(v.wag) * 10.0f;
    draw::Curve(dc, brush, cx + rx * 0.8f, y + 6.0f,
                cx + rx * 1.5f + wag, y - 14.0f,
                cx + rx * 1.15f + wag * 0.5f, y - 26.0f, 5.0f, shade);

    // Ears (two triangles with pink inners), with the occasional twitch.
    float twitch = (std::sin(v.time * 0.9f) > 0.97f) ? 3.0f : 0.0f;
    D2D1_POINT_2F earL[3] = {
        D2D1::Point2F(cx - rx * 0.72f, y - ry * 0.55f),
        D2D1::Point2F(cx - rx * 0.50f, y - ry - 14.0f - twitch),
        D2D1::Point2F(cx - rx * 0.18f, y - ry * 0.82f),
    };
    D2D1_POINT_2F earR[3] = {
        D2D1::Point2F(cx + rx * 0.72f, y - ry * 0.55f),
        D2D1::Point2F(cx + rx * 0.50f, y - ry - 14.0f + twitch),
        D2D1::Point2F(cx + rx * 0.18f, y - ry * 0.82f),
    };
    draw::FillPolygon(dc, brush, earL, 3, fur);
    draw::FillPolygon(dc, brush, earR, 3, fur);
    D2D1_POINT_2F earLi[3] = {
        D2D1::Point2F(cx - rx * 0.60f, y - ry * 0.62f),
        D2D1::Point2F(cx - rx * 0.48f, y - ry - 7.0f - twitch),
        D2D1::Point2F(cx - rx * 0.30f, y - ry * 0.80f),
    };
    D2D1_POINT_2F earRi[3] = {
        D2D1::Point2F(cx + rx * 0.60f, y - ry * 0.62f),
        D2D1::Point2F(cx + rx * 0.48f, y - ry - 7.0f + twitch),
        D2D1::Point2F(cx + rx * 0.30f, y - ry * 0.80f),
    };
    draw::FillPolygon(dc, brush, earLi, 3, pink.WithAlpha(0.75f));
    draw::FillPolygon(dc, brush, earRi, 3, pink.WithAlpha(0.75f));

    // Body + belly patch.
    brush->SetColor(D2D1::ColorF(fur.r, fur.g, fur.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);
    brush->SetColor(D2D1::ColorF(1.0f, 0.97f, 0.90f, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + ry * 0.35f), rx * 0.62f, ry * 0.55f), brush);

    // Face.
    const float eyeY = y - ry * 0.25f;
    const float eyeDx = 13.0f;
    PetEyes(v, dc, brush, cx, eyeY + (v.hungry ? 1.5f : 0.0f), eyeDx, 3.6f, dark);
    draw::Circle(dc, brush, cx, eyeY + 7.0f, 1.8f, pink);
    float mouthY = eyeY + 12.0f;
    draw::Line(dc, brush, cx - 4.0f, mouthY, cx, mouthY + 3.0f, 1.4f, Color::Hex(0x7a6a4d));
    draw::Line(dc, brush, cx + 4.0f, mouthY, cx, mouthY + 3.0f, 1.4f, Color::Hex(0x7a6a4d));
    // Whiskers.
    for (int s = -1; s <= 1; s += 2) {
        draw::Line(dc, brush, cx + s * (rx * 0.55f), eyeY + 5.0f,
                   cx + s * (rx * 0.95f), eyeY + 2.0f, 1.0f, shade.WithAlpha(0.8f));
        draw::Line(dc, brush, cx + s * (rx * 0.55f), eyeY + 7.0f,
                   cx + s * (rx * 0.98f), eyeY + 8.0f, 1.0f, shade.WithAlpha(0.8f));
    }
    PetBlush(dc, brush, cx, eyeY + 6.0f, eyeDx + 8.0f, 4.0f, pink, v.sleeping ? 0.35f : 0.55f);
}

void DrawShiba(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    const float cx = kW * 0.5f, y = v.bodyY;
    const float rx = 40.0f * v.squash, ry = 31.0f * (2.0f - v.squash);
    Color tan = Color::Hex(0xe8b36a);
    Color cream = Color::Hex(0xf7ecd9);
    Color dark = Color::Hex(0x4a3526);
    Color pink = Color::Hex(0xe9967f);

    // Curled tail: a tight hook that swings with the wag.
    float wag = std::sin(v.wag * 1.4f) * 6.0f;
    draw::Curve(dc, brush, cx + rx * 0.78f, y + 2.0f,
                cx + rx * 1.55f + wag, y - 16.0f,
                cx + rx * 1.05f + wag * 0.6f, y - 24.0f, 8.0f, tan);
    draw::Circle(dc, brush, cx + rx * 1.05f + wag * 0.6f, y - 24.0f, 4.0f, cream);

    // Pointy upright ears.
    float twitch = (std::sin(v.time * 1.1f + 2.0f) > 0.97f) ? -3.0f : 0.0f;
    D2D1_POINT_2F earL[3] = {
        D2D1::Point2F(cx - rx * 0.75f, y - ry * 0.50f),
        D2D1::Point2F(cx - rx * 0.58f, y - ry - 16.0f - twitch),
        D2D1::Point2F(cx - rx * 0.28f, y - ry * 0.86f),
    };
    D2D1_POINT_2F earR[3] = {
        D2D1::Point2F(cx + rx * 0.75f, y - ry * 0.50f),
        D2D1::Point2F(cx + rx * 0.58f, y - ry - 16.0f + twitch),
        D2D1::Point2F(cx + rx * 0.28f, y - ry * 0.86f),
    };
    draw::FillPolygon(dc, brush, earL, 3, tan);
    draw::FillPolygon(dc, brush, earR, 3, tan);

    brush->SetColor(D2D1::ColorF(tan.r, tan.g, tan.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);
    // Cream chest + muzzle.
    brush->SetColor(D2D1::ColorF(cream.r, cream.g, cream.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + ry * 0.42f), rx * 0.55f, ry * 0.52f), brush);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + 2.0f), rx * 0.34f, ry * 0.40f), brush);

    // Shiba brow dots.
    const float eyeY = y - ry * 0.28f;
    const float eyeDx = 13.0f;
    draw::Circle(dc, brush, cx - eyeDx, eyeY - 8.0f, 2.2f, cream);
    draw::Circle(dc, brush, cx + eyeDx, eyeY - 8.0f, 2.2f, cream);
    PetEyes(v, dc, brush, cx, eyeY + (v.hungry ? 1.5f : 0.0f), eyeDx, 3.4f, dark);
    // Triangle nose + mouth.
    D2D1_POINT_2F nose[3] = {
        D2D1::Point2F(cx - 3.0f, eyeY + 6.0f),
        D2D1::Point2F(cx + 3.0f, eyeY + 6.0f),
        D2D1::Point2F(cx, eyeY + 10.0f),
    };
    draw::FillPolygon(dc, brush, nose, 3, dark);
    float mouthY = eyeY + 13.0f;
    draw::Line(dc, brush, cx - 4.0f, mouthY, cx, mouthY + 2.5f, 1.4f, dark);
    draw::Line(dc, brush, cx + 4.0f, mouthY, cx, mouthY + 2.5f, 1.4f, dark);
    // Tongue out when delighted (or mid-snack).
    if ((v.happy && v.blink < 0.6f) || v.munch > 0.0f) {
        float wig = std::sin(v.time * 6.0f) * 1.2f;
        draw::Circle(dc, brush, cx + wig, mouthY + 6.5f, 3.0f, pink);
    }
    PetBlush(dc, brush, cx, eyeY + 6.0f, eyeDx + 8.0f, 3.6f, pink, 0.45f);
}

void DrawAxolotl(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    // Gentle swim sway around the anchor.
    const float cx = kW * 0.5f + std::sin(v.time * 1.3f) * 2.5f;
    const float y = v.bodyY + std::sin(v.time * 1.9f + 1.0f) * 1.5f;
    const float rx = 42.0f * v.squash, ry = 29.0f * (2.0f - v.squash);
    Color body = Color::Hex(0xf9bfd8);
    Color deep = Color::Hex(0xe58fb8);
    Color dark = Color::Hex(0x6b3a52);

    // Tail fin peeking out behind the body.
    float sway = std::sin(v.time * 2.2f) * 4.0f;
    brush->SetColor(D2D1::ColorF(deep.r, deep.g, deep.b, 0.85f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - rx * 0.95f + sway * 0.3f, y + ry * 0.25f),
                                  12.0f, 20.0f), brush);

    // Gills: three pulsing fronds fanning out sideways from each cheek.
    for (int s = -1; s <= 1; s += 2) {
        for (int i = 0; i < 3; ++i) {
            float pulse = 1.0f + 0.18f * std::sin(v.time * 2.6f + (float)i * 1.1f);
            float bx = cx + s * rx * 0.78f, by = y - ry * 0.30f + 7.0f * (float)i;
            float ex = bx + s * 14.0f * pulse;
            float ey = by + ((float)i - 1.0f) * 3.5f * pulse;
            draw::Line(dc, brush, bx, by, ex, ey, 3.2f, deep);
            draw::Circle(dc, brush, ex, ey, 2.4f, deep);
        }
    }

    brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);
    brush->SetColor(D2D1::ColorF(1.0f, 0.93f, 0.96f, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y + ry * 0.38f), rx * 0.6f, ry * 0.5f), brush);

    const float eyeY = y - ry * 0.30f;
    const float eyeDx = 14.0f;
    PetEyes(v, dc, brush, cx, eyeY + (v.hungry ? 1.5f : 0.0f), eyeDx, 3.0f, dark);
    // The famous perma-smile.
    draw::Curve(dc, brush, cx - 6.0f, eyeY + 9.0f, cx, eyeY + 14.0f, cx + 6.0f, eyeY + 9.0f,
                1.8f, dark);
    PetBlush(dc, brush, cx, eyeY + 7.0f, eyeDx + 8.0f, 3.6f, deep, 0.5f);
}

void DrawChick(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    const float cx = kW * 0.5f, y = v.bodyY;
    const float rx = 35.0f * v.squash, ry = 34.0f * (2.0f - v.squash);
    Color fluff = Color::Hex(0xffdf6b);
    Color deep = Color::Hex(0xe8b84a);
    Color beak = Color::Hex(0xf59b3c);
    Color dark = Color::Hex(0x5a4318);

    // Little legs dangle mid-jump.
    if (v.airborne) {
        draw::Line(dc, brush, cx - 8.0f, y + ry * 0.85f, cx - 9.0f, y + ry * 0.85f + 9.0f, 2.4f, beak);
        draw::Line(dc, brush, cx + 8.0f, y + ry * 0.85f, cx + 9.0f, y + ry * 0.85f + 9.0f, 2.4f, beak);
    }

    brush->SetColor(D2D1::ColorF(fluff.r, fluff.g, fluff.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);

    // Head sprouts.
    float sway = std::sin(v.time * 3.0f) * 2.0f;
    draw::Line(dc, brush, cx - 2.0f, y - ry - 1.0f, cx - 4.0f + sway, y - ry - 8.0f, 1.8f, deep);
    draw::Line(dc, brush, cx + 2.0f, y - ry - 1.0f, cx + 5.0f + sway, y - ry - 7.0f, 1.8f, deep);

    // Wings that flap on a slow beat (faster mid-jump).
    float flap = std::sin(v.time * (v.airborne ? 18.0f : 3.2f)) * (v.airborne ? 8.0f : 3.0f);
    for (int s = -1; s <= 1; s += 2) {
        D2D1_POINT_2F wing[3] = {
            D2D1::Point2F(cx + s * rx * 0.72f, y - ry * 0.15f),
            D2D1::Point2F(cx + s * (rx * 1.18f), y - ry * 0.15f + flap),
            D2D1::Point2F(cx + s * rx * 0.66f, y + ry * 0.42f),
        };
        draw::FillPolygon(dc, brush, wing, 3, deep);
    }

    const float eyeY = y - ry * 0.22f;
    const float eyeDx = 11.0f;
    PetEyes(v, dc, brush, cx, eyeY + (v.hungry ? 1.5f : 0.0f), eyeDx, 4.2f, dark);
    // Diamond beak; opens while munching.
    if (v.munch > 0.0f) {
        D2D1_POINT_2F top[3] = {
            D2D1::Point2F(cx - 5.0f, eyeY + 5.0f), D2D1::Point2F(cx + 5.0f, eyeY + 5.0f),
            D2D1::Point2F(cx, eyeY + 8.0f) };
        D2D1_POINT_2F bot[3] = {
            D2D1::Point2F(cx - 5.0f, eyeY + 11.0f), D2D1::Point2F(cx + 5.0f, eyeY + 11.0f),
            D2D1::Point2F(cx, eyeY + 8.0f) };
        draw::FillPolygon(dc, brush, top, 3, beak);
        draw::FillPolygon(dc, brush, bot, 3, beak);
    } else {
        D2D1_POINT_2F bk[3] = {
            D2D1::Point2F(cx - 5.0f, eyeY + 5.0f), D2D1::Point2F(cx + 5.0f, eyeY + 5.0f),
            D2D1::Point2F(cx, eyeY + 11.0f) };
        draw::FillPolygon(dc, brush, bk, 3, beak);
    }
    PetBlush(dc, brush, cx, eyeY + 7.0f, eyeDx + 8.0f, 3.6f, Color::Hex(0xf2a3b1), 0.5f);
}

void DrawGhost(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    // Ghosts hover: bigger, slower bob above the anchor.
    const float cx = kW * 0.5f;
    const float y = v.bodyY - 8.0f + std::sin(v.time * 1.6f) * 4.0f;
    const float rx = 36.0f * v.squash, ry = 32.0f * (2.0f - v.squash);
    Color body = Color(1, 1, 1, 0.88f);
    Color dark = Color::Hex(0x46506a);

    // Wavy skirt: scallop bumps along the bottom edge, rippling in sequence.
    for (int i = 0; i < 4; ++i) {
        float bx = cx - rx * 0.75f + (rx * 0.5f) * (float)i;
        float by = y + ry * 0.72f + std::sin(v.time * 3.0f + (float)i * 1.4f) * 2.2f;
        draw::Circle(dc, brush, bx, by, 8.5f, body);
    }

    brush->SetColor(D2D1::ColorF(body.r, body.g, body.b, body.a));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);

    const float eyeY = y - ry * 0.16f;
    const float eyeDx = 12.0f;
    if (v.blink > 0.6f || v.sleeping) {
        draw::Line(dc, brush, cx - eyeDx - 5.0f, eyeY, cx - eyeDx + 5.0f, eyeY, 2.2f, dark);
        draw::Line(dc, brush, cx + eyeDx - 5.0f, eyeY, cx + eyeDx + 5.0f, eyeY, 2.2f, dark);
    } else {
        // Tall oval eyes that also drift with the look direction.
        for (int s = -1; s <= 1; s += 2) {
            float ex = cx + eyeDx * s + v.lookX * 2.0f;
            float ey = eyeY + v.lookY * 1.5f;
            brush->SetColor(D2D1::ColorF(dark.r, dark.g, dark.b, 1.0f));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ex, ey), 3.2f, 5.2f), brush);
        }
    }
    // Little "oo" mouth when surprised (recent pet), gentle smile otherwise.
    if (v.munch <= 0.0f && v.happy && v.blink < 0.6f) {
        draw::Circle(dc, brush, cx, eyeY + 9.0f, 2.6f, dark);
    } else {
        draw::Curve(dc, brush, cx - 5.0f, eyeY + 8.0f, cx, eyeY + 12.0f, cx + 5.0f, eyeY + 8.0f,
                    1.6f, dark);
    }
    PetBlush(dc, brush, cx, eyeY + 6.0f, eyeDx + 8.0f, 3.4f, Color::Hex(0xf2a3b1), 0.4f);
}

void DrawPanda(const VisualState& v, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush) {
    const float cx = kW * 0.5f, y = v.bodyY;
    const float rx = 40.0f * v.squash, ry = 33.0f * (2.0f - v.squash);
    Color white = Color::Hex(0xf5f2ea);
    Color black = Color::Hex(0x2a2a2e);

    // Ears.
    draw::Circle(dc, brush, cx - rx * 0.62f, y - ry * 0.82f, 9.5f, black);
    draw::Circle(dc, brush, cx + rx * 0.62f, y - ry * 0.82f, 9.5f, black);

    brush->SetColor(D2D1::ColorF(white.r, white.g, white.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y), rx, ry), brush);

    // Chunky arms; the right one holds bamboo while snacking.
    brush->SetColor(D2D1::ColorF(black.r, black.g, black.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - rx * 0.92f, y + ry * 0.18f), 8.0f, 13.0f), brush);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + rx * 0.92f, y + ry * 0.18f), 8.0f, 13.0f), brush);

    const float eyeY = y - ry * 0.22f;
    const float eyeDx = 13.0f;
    // Eye patches with bright eyes inside.
    brush->SetColor(D2D1::ColorF(black.r, black.g, black.b, 1.0f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - eyeDx, eyeY), 7.2f, 8.6f), brush);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + eyeDx, eyeY), 7.2f, 8.6f), brush);
    if (v.blink > 0.6f) {
        Color whiteLine = Color::Hex(0xf5f2ea);
        draw::Line(dc, brush, cx - eyeDx - 4.0f, eyeY, cx - eyeDx + 4.0f, eyeY, 2.0f, whiteLine);
        draw::Line(dc, brush, cx + eyeDx - 4.0f, eyeY, cx + eyeDx + 4.0f, eyeY, 2.0f, whiteLine);
    } else {
        float lx = v.lookX * 2.2f, ly = v.lookY * 2.0f;
        draw::Circle(dc, brush, cx - eyeDx + lx, eyeY + ly, 3.0f, white);
        draw::Circle(dc, brush, cx + eyeDx + lx, eyeY + ly, 3.0f, white);
        draw::Circle(dc, brush, cx - eyeDx + lx, eyeY + ly, 1.5f, black);
        draw::Circle(dc, brush, cx + eyeDx + lx, eyeY + ly, 1.5f, black);
    }
    draw::Circle(dc, brush, cx, eyeY + 8.0f, 2.2f, black);
    float mouthY = eyeY + 12.5f;
    draw::Line(dc, brush, cx - 3.5f, mouthY, cx, mouthY + 2.5f, 1.4f, black);
    draw::Line(dc, brush, cx + 3.5f, mouthY, cx, mouthY + 2.5f, 1.4f, black);

    // Bamboo snack in the left paw while munching.
    if (v.munch > 0.0f) {
        Color cane = Color::Hex(0x7ec850);
        float bx = cx - rx * 0.92f, by = y + ry * 0.10f;
        draw::Line(dc, brush, bx, by + 8.0f, bx - 4.0f, by - 16.0f, 3.0f, cane);
        D2D1_POINT_2F leaf[3] = {
            D2D1::Point2F(bx - 4.0f, by - 12.0f),
            D2D1::Point2F(bx - 14.0f, by - 18.0f),
            D2D1::Point2F(bx - 5.0f, by - 20.0f),
        };
        draw::FillPolygon(dc, brush, leaf, 3, cane);
    }
}

using DrawFn = void (*)(const VisualState&, ID2D1DeviceContext*, ID2D1SolidColorBrush*);

struct PetKindInfo {
    const wchar_t* key;
    const wchar_t* name;
    uint32_t accent;
    DrawFn draw;
    const wchar_t* chatter[3];
};

// Positional wiring: index must match the PetKind enum order (see Pet.h).
const PetKindInfo kPetKinds[kPetKindCount] = {
    { L"cat",     L"Mochi", 0xf2ddb8, DrawCat,     { L"nya~",       L"purr purr",   L"mrrp?" } },
    { L"shiba",   L"Taro",  0xe8b36a, DrawShiba,   { L"woof!",      L"bork bork",   L"awoo?" } },
    { L"axolotl", L"Lumi",  0xf9bfd8, DrawAxolotl, { L"blub blub",  L"pbt!",        L"~glub~" } },
    { L"chick",   L"Pip",   0xffdf6b, DrawChick,   { L"piyo!",      L"peep peep",   L"cheep?" } },
    { L"ghost",   L"Boo",   0xdfe8ff, DrawGhost,   { L"booo...",    L"ooo~",        L"hi hi" } },
    { L"panda",   L"Momo",  0xcfd4dc, DrawPanda,   { L"munch munch",L"yaaawn",      L"rawr" } },
};
static_assert(kPetKindCount == (int)(sizeof(kPetKinds) / sizeof(kPetKinds[0])),
              "kPetKinds must match PetKind");

} // namespace

// --- exported kind helpers ---------------------------------------------------

PetKind PetKindDefault() { return PetKind::Cat; }

const wchar_t* PetKindKey(PetKind kind) {
    int i = (int)kind;
    return (i >= 0 && i < kPetKindCount) ? kPetKinds[i].key : L"cat";
}

const wchar_t* PetKindName(PetKind kind) {
    int i = (int)kind;
    return (i >= 0 && i < kPetKindCount) ? kPetKinds[i].name : L"Mochi";
}

uint32_t PetKindAccent(PetKind kind) {
    int i = (int)kind;
    return (i >= 0 && i < kPetKindCount) ? kPetKinds[i].accent : 0xf2ddb8;
}

bool PetKindFromKey(const wchar_t* key, PetKind* out) {
    if (!key || !*key) return false;
    for (int i = 0; i < kPetKindCount; ++i) {
        if (_wcsicmp(key, kPetKinds[i].key) == 0 || _wcsicmp(key, kPetKinds[i].name) == 0) {
            if (out) *out = (PetKind)i;
            return true;
        }
    }
    return false;
}

// --- the shared renderer ------------------------------------------------------

// Level curve: cheap early, slower later. Needs roughly 20, 40, 65, 95... xp.
int LevelForXp(int xp) {
    int level = 1, need = 20, acc = 0;
    while (level < 30 && xp >= acc + need) {
        acc += need;
        ++level;
        need += 15 + level * 5;
    }
    return level;
}

bool RenderPetFrame(gfx::Device& device, PetKind kind, const VisualState& v) {
    ID2D1DeviceContext* dc = device.Dc();
    ID2D1SolidColorBrush* brush = device.White();
    if (!dc || !brush) return false;
    const float w = kW, h = kH;
    const int ki = (int)kind;
    if (ki < 0 || ki >= kPetKindCount) return false;

    // Card background (optional); without it, a soft shadow grounds the creature
    // against whatever wallpaper is behind the transparent window. Ghosts hover,
    // so their shadow is fainter and smaller.
    if (v.card) {
        Color card0 = Color::Hex(0x171d2b), card1 = Color::Hex(0x10141f);
        D2D1_GRADIENT_STOP bg[2];
        bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(card0.r, card0.g, card0.b, 1);
        bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(card1.r, card1.g, card1.b, 1);
        draw::VerticalGradient(dc, w, h, bg, 2);
    } else {
        float sa = 0.22f, sr = 34.0f;
        if (kind == PetKind::Ghost) { sa = 0.10f; sr = 26.0f; }
        draw::Circle(dc, brush, w * 0.5f, h * 0.94f, sr, Color(0, 0, 0, sa));
    }

    // The panda's roll rotates the whole creature around its own anchor.
    if (v.rot != 0.0f)
        dc->SetTransform(D2D1::Matrix3x2F::Rotation(v.rot * 180.0f / kPi,
                                                    D2D1::Point2F(w * 0.5f, v.bodyY)));
    kPetKinds[ki].draw(v, dc, brush);
    if (v.rot != 0.0f)
        dc->SetTransform(D2D1::Matrix3x2F::Identity());

    // The play ball, drawn in front so it can bounce across the creature.
    if (v.ball.active && v.ball.life > 0.0f) {
        draw::Circle(dc, brush, v.ball.x, v.ball.y, 8.0f, Color(0.96f, 0.96f, 0.98f, 0.95f));
        draw::Curve(dc, brush, v.ball.x - 7.5f, v.ball.y - 3.0f, v.ball.x, v.ball.y + 3.0f,
                    v.ball.x + 7.5f, v.ball.y - 3.0f, 1.8f, Color::Hex(0xe0525c));
        draw::Circle(dc, brush, v.ball.x - 2.5f, v.ball.y - 2.5f, 1.6f, Color(1, 1, 1, 0.9f));
    }

    // Emotes (hearts, sparkles, notes, zzz, crumbs...).
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    for (const Emote& e : v.emotes) {
        if (e.type < 0 || e.life <= 0.0f) continue;
        float a = Clamp01(e.life / e.max);
        switch (e.type) {
            case EmoteHeart:
                PetHeart(dc, brush, e.x, e.y, 6.0f, Color::Hex(0xff6f91).WithAlpha(a));
                break;
            case EmoteSparkle:
                PetSparkle(dc, brush, e.x, e.y, 5.0f + 2.0f * (1.0f - a),
                           Color::Hex(0xffd873).WithAlpha(a));
                break;
            case EmoteNote: {
                Color nc = Color::Hex(0x9fd4ff).WithAlpha(a);
                draw::Circle(dc, brush, e.x, e.y, 2.6f, nc);
                draw::Line(dc, brush, e.x + 2.4f, e.y - 1.0f, e.x + 2.4f, e.y - 9.0f, 1.4f, nc);
                draw::Line(dc, brush, e.x + 2.4f, e.y - 9.0f, e.x + 6.5f, e.y - 7.0f, 1.4f, nc);
                break;
            }
            case EmoteCrumb:
                draw::Circle(dc, brush, e.x, e.y, 2.0f, e.tint.WithAlpha(a));
                break;
            case EmoteFeather: {
                brush->SetColor(D2D1::ColorF(1, 1, 0.85f, a * 0.9f));
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(e.x, e.y), 4.5f, 1.4f), brush);
                break;
            }
            case EmoteRing:
                draw::Circle(dc, brush, e.x, e.y, 3.0f + (1.0f - a) * 5.0f,
                             Color::Hex(0xbfe8ff).WithAlpha(a * 0.8f));
                break;
            default: break;   // EmoteZzz is drawn below with the text format
        }
    }
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

    // Text: one format per frame keeps the code simple; the pet draws at most a
    // few short strings per frame.
    IDWriteFactory* dw = device.DWrite();
    IDWriteTextFormat* fmt = nullptr;
    if (dw) {
        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                             11.0f, L"en-us", &fmt);
    }
    auto text = [&](const wchar_t* s, float x, float y, float tw, float th, const Color& c) {
        if (!fmt || !s) return;
        brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
        dc->DrawText(s, (UINT32)wcslen(s), fmt, D2D1::RectF(x, y, x + tw, y + th),
                     brush, D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
    };

    // Zzz emotes need the text format, so they come after it exists.
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    for (const Emote& e : v.emotes) {
        if (e.type != EmoteZzz || e.life <= 0.0f) continue;
        float a = Clamp01(e.life / e.max);
        text(L"z", e.x, e.y, 10, 12, Color(1, 1, 1, a * 0.8f));
    }
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

    // Speech bubble.
    if (v.bubble && v.bubbleLife > 0.0f) {
        float a = Clamp01(v.bubbleLife / 0.4f);   // quick fade at the end
        float bx = 30.0f, by = 8.0f, bw = kW - 60.0f, bh = 24.0f;
        draw::RoundedRect(dc, brush, bx, by, bw, bh, 9.0f, Color(0.97f, 0.97f, 1.0f, 0.92f * a));
        D2D1_POINT_2F tail[3] = {
            D2D1::Point2F(bx + bw * 0.5f - 5.0f, by + bh - 1.0f),
            D2D1::Point2F(bx + bw * 0.5f + 7.0f, by + bh - 1.0f),
            D2D1::Point2F(bx + bw * 0.5f, by + bh + 7.0f),
        };
        draw::FillPolygon(dc, brush, tail, 3, Color(0.97f, 0.97f, 1.0f, 0.92f * a));
        text(v.bubble, bx + 6.0f, by + 5.0f, bw - 12.0f, bh - 8.0f,
             Color::Hex(0x2a3348).WithAlpha(a));
    }

    // Feed button (cookie), top-right.
    bool hungryNow = !v.sleeping && v.munch <= 0.0f;
    draw::Circle(dc, brush, w - 18.0f, 18.0f, 11.0f,
                 hungryNow ? Color::Hex(0xc98f5a) : Color::Hex(0x4a4132));
    if (hungryNow) {
        draw::Circle(dc, brush, w - 21.0f, 15.0f, 1.5f, Color::Hex(0x5a3a20));
        draw::Circle(dc, brush, w - 15.0f, 19.0f, 1.5f, Color::Hex(0x5a3a20));
        draw::Circle(dc, brush, w - 18.0f, 22.0f, 1.5f, Color::Hex(0x5a3a20));
    }

    // Play button (ball), top-left. Dimmed while a ball is already bouncing.
    bool ballReady = !v.ball.active;
    draw::Circle(dc, brush, 18.0f, 18.0f, 11.0f,
                 ballReady ? Color(0.93f, 0.93f, 0.96f, 1.0f) : Color::Hex(0x3a4152));
    draw::Curve(dc, brush, 11.5f, 15.0f, 18.0f, 21.0f, 24.5f, 15.0f, 1.6f,
                ballReady ? Color::Hex(0xe0525c) : Color::Hex(0x2a303e));
    draw::Circle(dc, brush, 15.5f, 14.5f, 1.6f, Color(1, 1, 1, ballReady ? 0.9f : 0.3f));

    // Status line (card mode): mood + level.
    if (v.card) {
        const wchar_t* mood = v.sleeping ? L"zzz..." : v.hungry ? L"hungry"
                            : v.sad ? L"lonely" : v.munch > 0.0f ? L"munch munch"
                            : v.ball.active ? L"playing" : L"happy";
        wchar_t line[64];
        swprintf(line, 64, L"%ls - Lv %d", mood, v.level);
        text(line, 12.0f, h - 20.0f, w - 24.0f, 14, Color(0x8d, 0x97, 0xad, 0.9f));
    }

    if (fmt) fmt->Release();
    return true;
}

// --- Impl ---------------------------------------------------------------------

struct Pet::Impl {
    HWND hwnd = nullptr;
    gfx::Device device;

    PetKind kind = PetKind::Cat;
    float time = 0;
    float hunger = 35.0f;        // 0 fed .. 100 starving
    float happiness = 75.0f;     // 0 sad .. 100 delighted
    int xp = 0;
    int level = 1;
    float ageSec = 0;
    float lastInteract = 0;      // seconds since the last click
    float saveTimer = 0;

    Rng rng{ 0x51e7u };
    Emote emotes[kMaxEmotes];
    BallState ball;
    const wchar_t* bubble = nullptr;
    float bubbleLife = 0;
    float chatterTimer = 18.0f;
    float idleActTimer = 14.0f;
    float zzzTimer = 0;
    float ringTimer = 0;

    float blinkTimer = 2.5f;
    float blinkAnim = 0;         // counts down a 0.26 s close/open cycle
    float lookX = 0, lookY = 0;  // eased pupil position
    float munch = 0;
    float wag = 0;
    float jumpT = -1.0f;         // -1 idle, otherwise 0..1
    float landT = 0;             // landing squash timer
    float rollT = -1.0f;         // panda roll 0..1

    bool suppressed = false;
    bool card = false;           // draw the dark card behind the creature
    int savedX = -1;             // persisted horizontal position
    bool dragging = false;
    int grabDX = 0;              // cursor offset inside the window
    int hoverX = -1, hoverY = -1;
    bool hoverTracked = false;

    void Say(const wchar_t* text, float life = 3.0f) {
        bubble = text;
        bubbleLife = life;
    }

    Emote* FreeEmote() {
        for (Emote& e : emotes)
            if (e.life <= 0.0f) return &e;
        return nullptr;
    }

    void SpawnEmote(int type, float x, float y, float vx, float vy, float life, Color tint) {
        Emote* e = FreeEmote();
        if (!e) return;
        e->type = type;
        e->max = e->life = life;
        e->x = x; e->y = y; e->vx = vx; e->vy = vy;
        e->tint = tint;
    }

    void BurstSparkles() {
        for (int i = 0; i < 8; ++i) {
            float ang = (kTau * (float)i) / 8.0f;
            SpawnEmote(EmoteSparkle, kW * 0.5f, kH * 0.45f,
                       std::cos(ang) * 26.0f, std::sin(ang) * 26.0f - 20.0f,
                       0.9f, Color::Hex(0xffd873));
        }
    }

    bool StartJump() {
        if (jumpT >= 0.0f || rollT >= 0.0f) return false;
        jumpT = 0.0f;
        return true;
    }

    void AddXp(int amount) {
        xp += amount;
        int nl = LevelForXp(xp);
        if (nl > level) {
            level = nl;
            BurstSparkles();
            Say(L"level up!");
            StartJump();
        }
    }

    void ThrowBall() {
        if (ball.active) return;
        ball.active = true;
        ball.x = 24.0f;
        ball.y = 26.0f;
        ball.vx = rng.Range(70.0f, 120.0f) * (rng.Unit() < 0.5f ? -1.0f : 1.0f);
        ball.vy = -30.0f;
        ball.life = 3.2f;
        SpawnEmote(EmoteNote, kW * 0.28f, kH * 0.30f, 6.0f, -26.0f, 1.2f, Color());
    }

    // --- persistence ---------------------------------------------------------

    void Load() {
        const std::wstring prefix = std::wstring(PetKindKey(kind)) + L".";
        FILE* f = nullptr;
        if (_wfopen_s(&f, PetStatsPath().c_str(), L"r, ccs=UTF-8") != 0 || !f) return;
        wchar_t line[160];
        while (fgetws(line, _countof(line), f)) {
            wchar_t* eq = wcsstr(line, L"=");
            if (!eq) continue;
            *eq = 0;
            std::wstring key(line);
            float val = (float)_wtof(eq + 1);
            if (key.compare(0, prefix.size(), prefix) == 0) {
                std::wstring sub = key.substr(prefix.size());
                if (sub == L"hunger") hunger = val;
                else if (sub == L"happiness") happiness = val;
                else if (sub == L"xp") xp = (int)val;
                else if (sub == L"age") ageSec = val;
            } else if (kind == PetKind::Cat && key == L"hunger") {
                hunger = val;                 // pre-1.6 save without kind prefixes
            } else if (kind == PetKind::Cat && key == L"happiness") {
                happiness = val;
            }
        }
        fclose(f);
        hunger = Clamp(hunger, 0.0f, 95.0f);
        happiness = Clamp(happiness, 10.0f, 100.0f);
        xp = std::max(0, xp);
        ageSec = std::max(0.0f, ageSec);
        level = LevelForXp(xp);
    }

    void Save() {
        // Read-modify-write: every species keeps its own block, so saving one pet
        // must not erase the others.
        std::wstring prefix = std::wstring(PetKindKey(kind)) + L".";
        std::vector<std::wstring> out;
        FILE* in = nullptr;
        if (_wfopen_s(&in, PetStatsPath().c_str(), L"r, ccs=UTF-8") == 0 && in) {
            wchar_t line[160];
            while (fgetws(line, _countof(line), in)) {
                std::wstring s(line);
                while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
                if (s.empty()) continue;
                size_t eq = s.find(L'=');
                if (eq == std::wstring::npos) continue;
                std::wstring key = s.substr(0, eq);
                if (key == L"x") continue;    // rewritten below from the live window
                bool statLine = false;        // every species block is rewritten below
                for (int i = 0; i < kPetKindCount; ++i) {
                    std::wstring p = std::wstring(kPetKinds[i].key) + L".";
                    if (key.compare(0, p.size(), p) == 0) { statLine = true; break; }
                }
                if (statLine) continue;
                out.push_back(s);
            }
            fclose(in);
        }
        int x = savedX >= 0 ? savedX : xPos(hwnd);
        if (x >= 0) {
            wchar_t xl[32];
            swprintf(xl, 32, L"x=%d", x);
            out.push_back(xl);
        }
        // prefix already ends with '.'; keys read back as "<kind>.hunger" etc.
        wchar_t buf[64];
        swprintf(buf, 64, L"%lshunger=%.2f", prefix.c_str(), hunger);
        out.push_back(buf);
        swprintf(buf, 64, L"%lshappiness=%.2f", prefix.c_str(), happiness);
        out.push_back(buf);
        swprintf(buf, 64, L"%lsxp=%d", prefix.c_str(), xp);
        out.push_back(buf);
        swprintf(buf, 64, L"%lsage=%.0f", prefix.c_str(), ageSec);
        out.push_back(buf);

        FILE* f = nullptr;
        if (_wfopen_s(&f, PetStatsPath().c_str(), L"w, ccs=UTF-8") != 0 || !f) return;
        for (const std::wstring& s : out) fwprintf(f, L"%ls\n", s.c_str());
        fclose(f);
    }
};

Pet::Pet() : m_impl(new Impl()) {}

Pet::~Pet() {
    Destroy();
    delete m_impl;
}

HWND Pet::Window() const { return m_impl ? m_impl->hwnd : nullptr; }

PetKind Pet::GetKind() const { return m_impl ? m_impl->kind : PetKindDefault(); }

void Pet::SetKind(PetKind kind) {
    Impl* d = m_impl;
    if (!d || d->kind == kind) return;
    d->Save();                       // persist the pet we are switching away from
    d->kind = kind;
    d->hunger = 35.0f;
    d->happiness = 75.0f;
    d->xp = 0;
    d->ageSec = 0;
    d->level = 1;
    d->ball.active = false;
    d->bubble = nullptr;
    d->bubbleLife = 0;
    d->jumpT = d->rollT = -1.0f;
    for (Emote& e : d->emotes) e.life = 0;
    d->rng = Rng(0x51e7u + 977u * (unsigned)kind);
    d->Load();                       // this pet's own block, if it has one
    d->Say(kPetKinds[(int)kind].chatter[0]);
}

bool Pet::Create(HINSTANCE instance) {
    EnsureClass(instance);

    // Load stats first: a saved x position wins over the default spawn point.
    m_impl->Load();

    // Position: directly above the clock by default, draggable anywhere along
    // the taskbar except over the app icons.
    RECT tray{}, icons{}, clock{};
    int x, y;
    if (TaskbarZones(tray, icons, clock)) {
        int clockW = clock.right - clock.left;
        x = (m_impl->savedX >= 0) ? m_impl->savedX
                                  : clock.left + std::max(0, (clockW - kPetWindowW) / 2);
        x = ClampPetX(x, kPetWindowW, tray, icons);
        y = tray.top - kPetWindowH + 8;
    } else {
        RECT wa{0, 0, 1280, 800};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right - kPetWindowW - 24;
        y = wa.bottom - kPetWindowH - 8;
    }

    m_impl->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE,
        kPetClass, L"LiveShanShui Pet",
        WS_POPUP | WS_CLIPCHILDREN, x, y, kPetWindowW, kPetWindowH,
        nullptr, nullptr, instance, this);
    if (!m_impl->hwnd) return false;

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_impl->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_impl->hwnd, 20, &dark, sizeof(dark));

    RECT rc{};
    GetClientRect(m_impl->hwnd, &rc);
    gfx::DeviceDesc desc;
    desc.hwnd = m_impl->hwnd;
    desc.width = (uint32_t)(rc.right - rc.left);
    desc.height = (uint32_t)(rc.bottom - rc.top);
    desc.sceneScale = 1.0f;
    desc.allowSoftware = true;
    desc.transparent = true;
    if (!m_impl->device.Create(desc)) {
        DestroyWindow(m_impl->hwnd);
        m_impl->hwnd = nullptr;
        return false;
    }

    m_enabled = true;
    ShowWindow(m_impl->hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_impl->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void Pet::Destroy() {
    if (!m_impl) return;
    if (m_impl->hwnd) {
        m_impl->Save();
        SetWindowLongPtrW(m_impl->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_impl->hwnd);
        m_impl->hwnd = nullptr;
    }
    m_impl->device.Destroy();
    m_enabled = false;
}

void Pet::SetEnabled(bool enabled) {
    if (enabled == m_enabled) return;
    if (enabled) {
        Create(GetModuleHandleW(nullptr));
    } else {
        if (m_impl->hwnd) ShowWindow(m_impl->hwnd, SW_HIDE);
        m_impl->Save();
        m_enabled = false;
    }
}

void Pet::SetSuppressed(bool suppressed) {
    if (!m_impl || !m_impl->hwnd || !m_enabled) return;
    if (suppressed == m_impl->suppressed) return;
    m_impl->suppressed = suppressed;
    ShowWindow(m_impl->hwnd, suppressed ? SW_HIDE : SW_SHOWNOACTIVATE);
}

void Pet::SetCard(bool card) {
    m_impl->card = card;
}

void Pet::Tick(float dt) {
    Impl* d = m_impl;
    if (!d->hwnd || !m_enabled || d->suppressed) return;
    dt = std::min(dt, 0.1f);   // clamp long stalls so physics stays stable
    d->time += dt;
    d->lastInteract += dt;
    d->ageSec += dt;
    d->hunger = Clamp(d->hunger + kDecayHungerPerSec * dt, 0.0f, 100.0f);
    d->happiness = Clamp(d->happiness - kDecayHappyPerSec * dt, 0.0f, 100.0f);
    d->wag += dt * (4.0f + d->happiness * 0.04f);
    d->munch = std::max(0.0f, d->munch - dt);

    // Blinking: a random countdown, then a quick close-open cycle.
    d->blinkTimer -= dt;
    if (d->blinkTimer <= 0.0f) {
        d->blinkAnim = 0.26f;
        d->blinkTimer = 2.2f + d->rng.Range(0.0f, 3.5f);
    }
    d->blinkAnim = std::max(0.0f, d->blinkAnim - dt);

    // Jump / roll / landing.
    if (d->jumpT >= 0.0f) {
        d->jumpT += dt / 0.55f;
        if (d->jumpT >= 1.0f) {
            d->jumpT = -1.0f;
            d->landT = 0.14f;
        }
    }
    d->landT = std::max(0.0f, d->landT - dt);
    if (d->rollT >= 0.0f) {
        d->rollT += dt / 0.8f;
        if (d->rollT >= 1.0f) d->rollT = -1.0f;
    }

    // Emotes drift.
    for (Emote& e : d->emotes) {
        if (e.life <= 0.0f) continue;
        e.life -= dt;
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        if (e.type == EmoteCrumb || e.type == EmoteFeather) e.vy += 60.0f * dt;
        if (e.type == EmoteFeather) e.x += std::sin(d->time * 5.0f) * 8.0f * dt;
    }

    // Ball physics.
    if (d->ball.active) {
        d->ball.life -= dt;
        d->ball.vy += 620.0f * dt;
        d->ball.x += d->ball.vx * dt;
        d->ball.y += d->ball.vy * dt;
        const float r = 8.0f;
        if (d->ball.x < r) { d->ball.x = r; d->ball.vx = std::abs(d->ball.vx) * 0.7f; }
        if (d->ball.x > kW - r) { d->ball.x = kW - r; d->ball.vx = -std::abs(d->ball.vx) * 0.7f; }
        if (d->ball.y > kH - 16.0f - r) {
            d->ball.y = kH - 16.0f - r;
            d->ball.vy = -std::abs(d->ball.vy) * 0.62f;
            d->ball.vx *= 0.985f;
        }
        if (d->ball.life <= 0.0f || (std::abs(d->ball.vy) < 12.0f && d->ball.y >= kH - 24.0f - r))
            d->ball.active = false;
    }

    // Speech bubble lifetime.
    if (d->bubbleLife > 0.0f) {
        d->bubbleLife -= dt;
        if (d->bubbleLife <= 0.0f) d->bubble = nullptr;
    }

    // Idle chatter.
    d->chatterTimer -= dt;
    if (d->chatterTimer <= 0.0f) {
        d->chatterTimer = 25.0f + d->rng.Range(0.0f, 30.0f);
        if (d->lastInteract < kSleepAfterSec && !d->bubble)
            d->Say(kPetKinds[(int)d->kind].chatter[d->rng.Int(0, 2)], 2.2f);
    }

    // Random idle actions: hop, wiggle, or a remark.
    d->idleActTimer -= dt;
    if (d->idleActTimer <= 0.0f) {
        d->idleActTimer = 12.0f + d->rng.Range(0.0f, 10.0f);
        if (d->lastInteract < kSleepAfterSec) {
            int pick = d->rng.Int(0, 2);
            if (pick == 0) d->StartJump();
            else if (pick == 1)
                d->SpawnEmote(EmoteNote, kW * 0.30f + d->rng.Range(-8.0f, 8.0f), kH * 0.30f,
                              6.0f, -24.0f, 1.2f, Color());
            else if (!d->bubble)
                d->Say(kPetKinds[(int)d->kind].chatter[d->rng.Int(0, 2)], 2.2f);
        }
    }

    // State emotes: snoring while asleep, bubbles for the axolotl.
    d->zzzTimer -= dt;
    if (d->lastInteract > kSleepAfterSec && d->zzzTimer <= 0.0f) {
        d->zzzTimer = 1.6f;
        d->SpawnEmote(EmoteZzz, kW * 0.62f + d->rng.Range(0.0f, 6.0f),
                      kH * 0.28f, 4.0f, -14.0f, 2.0f, Color());
    }
    d->ringTimer -= dt;
    if (d->kind == PetKind::Axolotl && d->ringTimer <= 0.0f) {
        d->ringTimer = 4.0f + d->rng.Range(0.0f, 3.0f);
        d->SpawnEmote(EmoteRing, kW * 0.5f + d->rng.Range(-16.0f, 16.0f), kH * 0.22f,
                      0.0f, -18.0f, 1.8f, Color());
    }

    // Hungry pets ask for food now and then.
    if (d->hunger > 65.0f && !d->bubble && d->rng.Unit() < dt * 0.12f)
        d->Say(L"feed me...", 2.4f);

    // Eyes ease toward the cursor, the ball, or forward again.
    float tx = 0, ty = d->hunger > 65.0f ? 0.35f : 0.0f;
    if (d->ball.active) {
        float fx = kW * 0.5f, fy = d->ball.y >= d->ball.x ? kH * 0.55f : kH * 0.35f;
        tx = Clamp((d->ball.x - fx) / 40.0f, -1.0f, 1.0f);
        ty = Clamp((d->ball.y - fy) / 40.0f, -1.0f, 1.0f);
    } else if (d->hoverX >= 0) {
        tx = Clamp(((float)d->hoverX - kW * 0.5f) / 40.0f, -1.0f, 1.0f);
        ty = Clamp(((float)d->hoverY - kH * 0.45f) / 40.0f, -1.0f, 1.0f);
    }
    d->lookX = Damp(d->lookX, tx, 0.09f, dt);
    d->lookY = Damp(d->lookY, ty, 0.09f, dt);

    d->saveTimer += dt;
    if (d->saveTimer >= 30.0f) {
        d->saveTimer = 0;
        d->Save();
    }
}

void Pet::Render() {
    Impl* d = m_impl;
    if (!d->hwnd || !m_enabled || d->suppressed) return;
    if (!d->device.Dc()) return;

    // Pose for this frame.
    VisualState v;
    v.time = d->time;
    v.wag = d->wag;
    if (d->blinkAnim > 0.0f)
        v.blink = std::sin(kPi * (1.0f - d->blinkAnim / 0.26f));
    v.lookX = d->lookX;
    v.lookY = d->lookY;
    v.bodyY = kH * 0.60f + std::sin(d->time * 2.1f) * 3.0f + (d->munch > 0 ? 3.0f : 0.0f);
    if (d->jumpT >= 0.0f) {
        v.bodyY -= std::sin(kPi * Clamp01(d->jumpT)) * 26.0f;
        v.squash = (d->jumpT < 0.5f) ? 0.88f : 0.92f;
        v.airborne = d->jumpT > 0.08f && d->jumpT < 0.92f;
    }
    if (d->landT > 0.0f) v.squash = 1.0f + (d->landT / 0.14f) * 0.18f;
    if (v.squash == 1.0f) v.squash = 1.0f + 0.03f * std::sin(d->time * 2.1f);
    v.rot = (d->rollT >= 0.0f) ? kTau * Clamp01(d->rollT) : 0.0f;
    v.sleeping = d->lastInteract > kSleepAfterSec;
    v.hungry = d->hunger > 65.0f && !v.sleeping;
    v.sad = d->happiness < 25.0f && !v.hungry;
    v.happy = d->happiness > 70.0f && !v.sleeping;
    v.card = d->card;
    v.munch = d->munch;
    v.level = d->level;
    v.ageSec = d->ageSec;
    for (int i = 0; i < kMaxEmotes; ++i) v.emotes[i] = d->emotes[i];
    v.ball = d->ball;
    v.bubble = d->bubble;
    v.bubbleLife = d->bubbleLife;

    d->device.BeginFrame(false);
    d->device.BeginScene();
    RenderPetFrame(d->device, d->kind, v);
    d->device.EndScene();
    d->device.EndFrame();
}

bool Pet::RenderTestFrame(gfx::Device& device, PetKind kind, float time) {
    VisualState v;
    v.time = time;
    v.bodyY = kH * 0.60f + std::sin(time * 2.1f) * 3.0f;
    v.squash = 1.0f + 0.03f * std::sin(time * 2.1f);
    v.wag = time * 5.0f;
    v.blink = (std::sin(time * 1.7f) > 0.9f) ? 0.8f : 0.0f;
    v.lookX = std::sin(time * 0.8f) * 0.5f;
    v.happy = true;
    v.munch = (std::fmod(time, 3.0f) < 1.0f) ? 0.5f : 0.0f;
    // A representative emote + ball so those paths are exercised too.
    v.emotes[0].type = EmoteHeart;
    v.emotes[0].max = v.emotes[0].life = 1.0f;
    v.emotes[0].x = kW * 0.7f; v.emotes[0].y = kH * 0.30f;
    v.emotes[1].type = EmoteZzz;
    v.emotes[1].max = v.emotes[1].life = 2.0f;
    v.emotes[1].x = kW * 0.62f; v.emotes[1].y = kH * 0.26f;
    return RenderPetFrame(device, kind, v);
}

int PetSelfTest(gfx::Device& device, const wchar_t* pngPrefix) {
    int fails = 0;
    // Table consistency: order matches the enum, keys unique, draw fns present.
    for (int i = 0; i < kPetKindCount; ++i) {
        if (!kPetKinds[i].key || !kPetKinds[i].name || !kPetKinds[i].draw) ++fails;
        for (int j = 0; j < kPetKindCount; ++j) {
            if (i != j && _wcsicmp(kPetKinds[i].key, kPetKinds[j].key) == 0) ++fails;
        }
        PetKind back = PetKindDefault();
        if (!PetKindFromKey(kPetKinds[i].key, &back) || back != (PetKind)i) ++fails;
    }
    if (PetKindFromKey(L"no-such-pet", nullptr)) ++fails;

    // Render every kind (three poses each) and keep one PNG per kind. Each frame
    // paints its own opaque background first (like every scene does), otherwise
    // strokes from earlier frames bleed through the shared test surface.
    for (int i = 0; i < kPetKindCount; ++i) {
        bool ok = true;
        for (int f = 0; f < 3 && ok; ++f) {
            device.BeginFrame(false);
            device.BeginScene();
            ID2D1DeviceContext* tdc = device.Dc();
            if (tdc && device.White()) {
                device.White()->SetColor(D2D1::ColorF(0.055f, 0.065f, 0.10f, 1.0f));
                tdc->FillRectangle(D2D1::RectF(0, 0, kW, kH), device.White());
            }
            ok = Pet::RenderTestFrame(device, (PetKind)i, 0.0f + 0.35f * (float)f);
            device.EndScene();
            device.EndFrame();
        }
        if (!ok) { ++fails; continue; }
        std::wstring png = pngPrefix;
        png += L"-";
        png += kPetKinds[i].key;
        png += L".png";
        if (!device.CapturePng(png)) ++fails;
    }
    return fails;
}

// --- window messages -----------------------------------------------------------

bool Pet::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    Impl* d = m_impl;
    if (result) *result = 0;
    switch (msg) {
        case WM_ERASEBKGND:
            return true;
        case WM_MOUSEMOVE: {
            d->hoverX = (short)LOWORD(lp);
            d->hoverY = (short)HIWORD(lp);
            if (!d->hoverTracked) {
                TRACKMOUSEEVENT te{ sizeof(te), TME_LEAVE, d->hwnd, 0 };
                TrackMouseEvent(&te);
                d->hoverTracked = true;
            }
            if (d->dragging) {
                RECT tray{}, icons{}, clock{};
                POINT pt{};
                GetCursorPos(&pt);
                if (TaskbarZones(tray, icons, clock)) {
                    int x = ClampPetX(pt.x - d->grabDX, kPetWindowW, tray, icons);
                    SetWindowPos(d->hwnd, nullptr, x, tray.top - kPetWindowH + 8, 0, 0,
                                 SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return false;
        }
        case WM_MOUSELEAVE:
            d->hoverX = d->hoverY = -1;
            d->hoverTracked = false;
            return false;
        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            // Feed button (cookie), top-right?
            float fdx = (float)x - (kW - 18.0f), fdy = (float)y - 18.0f;
            if (fdx * fdx + fdy * fdy <= 14.0f * 14.0f) {
                d->hunger = Clamp(d->hunger - 30.0f, 0.0f, 100.0f);
                d->happiness = Clamp(d->happiness + 3.0f, 0.0f, 100.0f);
                d->munch = 0.6f;
                d->lastInteract = 0;
                d->Say(L"yum!", 1.8f);
                Color crumbTint = (d->kind == PetKind::Panda) ? Color::Hex(0x7ec850)
                                                              : Color::Hex(0xc98f5a);
                for (int i = 0; i < 3; ++i)
                    d->SpawnEmote(EmoteCrumb, kW * 0.5f + d->rng.Range(-10.0f, 10.0f),
                                  kH * 0.32f, d->rng.Range(-18.0f, 18.0f), -20.0f,
                                  0.8f, crumbTint);
                d->AddXp(5);
                return true;
            }
            // Play button (ball), top-left?
            float bdx = (float)x - 18.0f, bdy = (float)y - 18.0f;
            if (bdx * bdx + bdy * bdy <= 14.0f * 14.0f) {
                if (!d->ball.active) {
                    d->ThrowBall();
                    d->happiness = Clamp(d->happiness + 10.0f, 0.0f, 100.0f);
                    d->hunger = Clamp(d->hunger + 4.0f, 0.0f, 100.0f);
                    d->lastInteract = 0;
                    d->Say(L"again! again!", 2.0f);
                    d->AddXp(8);
                }
                return true;
            }
            // Otherwise: pet the companion (hearts + xp) and start a taskbar drag.
            d->happiness = Clamp(d->happiness + 6.0f, 0.0f, 100.0f);
            d->lastInteract = 0;
            POINT pt{};
            GetCursorPos(&pt);
            RECT wr{};
            GetWindowRect(d->hwnd, &wr);
            d->dragging = true;
            d->grabDX = pt.x - wr.left;
            SetCapture(d->hwnd);
            for (int i = 0; i < 2; ++i) {
                d->SpawnEmote(EmoteHeart,
                              Clamp((float)x + d->rng.Range(-10.0f, 10.0f), 24.0f, kW - 24.0f),
                              std::min((float)y, kH * 0.45f) - i * 6.0f,
                              d->rng.Range(-6.0f, 6.0f), -30.0f, 1.25f, Color());
            }
            if (d->kind == PetKind::Chick) {
                for (int i = 0; i < 2; ++i)
                    d->SpawnEmote(EmoteFeather, (float)x, (float)y - 8.0f,
                                  d->rng.Range(-20.0f, 20.0f), -36.0f, 1.1f, Color());
            }
            if (!d->bubble && d->rng.Unit() < 0.35f)
                d->Say(kPetKinds[(int)d->kind].chatter[d->rng.Int(0, 2)], 1.8f);
            d->AddXp(2);
            return true;
        }
        case WM_LBUTTONDBLCLK:
            // A double-click makes the pet hop.
            d->lastInteract = 0;
            if (d->StartJump()) d->Say(L"wheee!", 1.4f);
            return true;
        case WM_LBUTTONUP:
            if (d->dragging) {
                d->dragging = false;
                ReleaseCapture();
                d->savedX = xPos(d->hwnd);
                d->Save();
            }
            return false;
        case WM_CAPTURECHANGED:
            d->dragging = false;
            return false;
        case WM_DESTROY:
            d->Save();
            d->hwnd = nullptr;
            return false;   // let DefWindowProc finish destruction
        default:
            break;
    }
    return false;
}

} // namespace lp
