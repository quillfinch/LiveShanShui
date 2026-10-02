// Live Shan Shui - control panel.
//
// A single Direct2D surface with custom hit-testing instead of a tree of child
// controls. For a panel this small that is both less code and less overhead: one
// swap chain, one present per frame, and nothing to re-layout on resize.
//
// Layout note: RebuildLayout() computes every rectangle once, in CONTENT
// coordinates, and Render() only reads them (translated by the current scroll).
// Keeping one source of geometry is what stops hit-testing and pixels drifting
// apart as the panel evolves.
#include "Panel.h"
#include "Pet.h"
#include "../gfx/GfxDevice.h"
#include "../gfx/Scene.h"
#include "../core/Log.h"
#include "../core/Math.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <algorithm>


#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

namespace lp {

namespace {

constexpr wchar_t kPanelClass[] = L"LiveShanShui.Panel";

constexpr float kWidth   = 470.0f;
constexpr float kPad     = 20.0f;
constexpr float kRowH    = 34.0f;
constexpr float kSegH    = 26.0f;
constexpr float kSceneGap  = 10.0f;
constexpr float kSliderRow = 30.0f;
constexpr int   kParamSliderCount = 4;
// The window never grows past this (or the work area); the content scrolls instead.
constexpr int   kMaxViewH = 880;

// Shared palette; the scene swatches below are derived from the scene accents.
const Color kBg0     = Color::Hex(0x0a0d15);
const Color kBg1     = Color::Hex(0x131826);
const Color kCard    = Color::Hex(0x1a2030);
const Color kAccent   = Color::Hex(0x4dd0e1);
const Color kAccent2  = Color::Hex(0x7c5cff);
const Color kText     = Color::Hex(0xe8edf7);
const Color kTextDim  = Color::Hex(0x8d97ad);
const Color kTrack    = Color::Hex(0x2a3348);

const wchar_t* kToggleLabels[] = {
    L"Pause on battery",
    L"Pause when locked",
    L"Pause behind fullscreen apps",
    L"Pause when desktop is covered",
    L"Span all monitors",
    L"Start with Windows",
    L"Show desktop pet",
    L"Pet card background",
};
constexpr int kToggleCount = (int)(sizeof(kToggleLabels) / sizeof(kToggleLabels[0]));

const wchar_t* kFpsLabels[] = { L"15", L"24", L"30", L"48", L"60", L"Max" };
const int kFpsValues[] = { 15, 24, 30, 48, 60, 0 };
constexpr int kFpsCount = 6;

const wchar_t* kQualityLabels[] = { L"Low", L"Balanced", L"High" };
const int kQualityValues[] = { 0, 1, 2 };
constexpr int kQualityCount = 3;

constexpr int kPresetCount = 10;

// Curated custom-color presets (0xRRGGBB).
const unsigned kColorPresets[kPresetCount] = {
    0x4DD0E1,  // cyan
    0x5AB8FF,  // sky
    0x7C5CFF,  // violet
    0xE879F9,  // orchid
    0xFF6F91,  // rose
    0xFF5C5C,  // coral red
    0xFFB347,  // amber
    0xF4E04D,  // citron
    0x7CE855,  // leaf
    0x2FD8A8,  // mint
};

D2D1_RECT_F ToRect(const RECT& r) {
    return D2D1::RectF((float)r.left, (float)r.top, (float)r.right, (float)r.bottom);
}

bool Contains(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

} // namespace

struct Panel::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    gfx::Device device;
    Callbacks cb;

    Config cfg;
    bool paused = false;
    std::wstring statusLine;
    std::wstring adapterLine;

    wchar_t paramNames[kParamSliderCount][28]{};
    int paramCount = kParamSliderCount;
    bool colorSupported = false;
    float colorHue = 0.52f, colorSat = 0.65f;   // derived from cfg.customColor

    std::vector<Hit> hits;
    RECT closeRect{};
    RECT sliderRects[kParamSliderCount]{};
    RECT fpsRects[kFpsCount]{};
    RECT qualityRects[kQualityCount]{};
    RECT colorToggleRect{}, colorHueRect{}, colorSatRect{};
    RECT presetRects[kPresetCount]{}, randomRect{};
    RECT shuffleRect{};
    RECT petRects[kPetKindCount]{};
    float heading1Y = 0, colorHeadingY = 0, heading2Y = 0, heading3Y = 0;
    float petHeadingY = 0, seedY = 0, toggleY0 = 0;
    Element dragging2 = Element::None;   // ColorSlider drag target
    int hoverIndex = -1;
    Element hoverKind = Element::None;

    Element dragging = Element::None;
    int dragIndex = -1;
    float anim = 0.0f;
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;

    // Scrolling: content coordinates run 0..contentH; the window shows
    // [scroll, scroll + viewH). Fixed chrome (header, footer) ignores the scroll.
    float scroll = 0, maxScroll = 0;
    int contentH = 0, viewH = 0;

    void DrawString(ID2D1DeviceContext* dc, IDWriteTextFormat* fmt, const wchar_t* text,
                    float x, float y, float w, float h, const Color& color,
                    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    int NeededHeight() const;
    int ClampViewHeight(int needed) const;
};

namespace {

Panel* FromHandle(HWND h) { return (Panel*)GetWindowLongPtrW(h, GWLP_USERDATA); }

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Panel* self = FromHandle(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (Panel*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (self) {
        LRESULT result = 0;
        if (self->HandleMessage(msg, wp, lp, &result)) return result;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureClass(HINSTANCE instance) {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

Panel::Panel() : m_impl(new Impl()) {}

Panel::~Panel() {
    Destroy();
    delete m_impl;
}

bool Panel::Create(HINSTANCE instance, const Callbacks& cb) {
    Impl* d = m_impl;
    d->instance = instance;
    d->cb = cb;
    EnsureClass(instance);

    // Anchor to the primary monitor's work area (which excludes the taskbar) so the
    // panel can never be clipped by the screen edge or hidden behind the taskbar.
    RECT wa{0, 0, 1280, 800};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int waH = wa.bottom - wa.top;
    int height = d->ClampViewHeight(660);
    if (height < 340) height = 340;
    int x = wa.right - (int)kWidth - 16;
    int y = wa.top + std::max(0, (waH - height) / 2);
    if (x < wa.left) x = wa.left + 8;
    if (y < wa.top) y = wa.top + 8;

    d->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPanelClass, L"Live Shan Shui",
        WS_POPUP | WS_CLIPCHILDREN, x, y, (int)kWidth, height,
        nullptr, nullptr, instance, this);

    if (!d->hwnd) {
        LP_LOGE(L"panel: CreateWindowEx failed %lu", GetLastError());
        return false;
    }

    DwmSetWindowAttribute(d->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &d->corner, sizeof(d->corner));
    // DWMWA_USE_IMMERSIVE_DARK_MODE; the numeric value avoids a newer SDK dependency.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(d->hwnd, 20, &dark, sizeof(dark));

    RECT rc{};
    GetClientRect(d->hwnd, &rc);
    gfx::DeviceDesc desc;
    desc.hwnd = d->hwnd;
    desc.width = (uint32_t)(rc.right - rc.left);
    desc.height = (uint32_t)(rc.bottom - rc.top);
    desc.sceneScale = 1.0f;   // UI text must stay crisp
    desc.allowSoftware = true;
    if (!d->device.Create(desc)) {
        LP_LOGE(L"panel: device creation failed");
        DestroyWindow(d->hwnd);
        d->hwnd = nullptr;
        return false;
    }
    RebuildLayout();
    return true;
}

void Panel::Destroy() {
    Impl* d = m_impl;
    d->device.Destroy();
    if (d->hwnd) {
        SetWindowLongPtrW(d->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(d->hwnd);
        d->hwnd = nullptr;
    }
}

HWND Panel::Window() const { return m_impl->hwnd; }

void Panel::Show() {
    Impl* d = m_impl;
    if (!d->hwnd) return;
    RebuildLayout();
    ShowWindow(d->hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(d->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    d->anim = 0.0f;
}

void Panel::Hide() {
    Impl* d = m_impl;
    if (d->hwnd) ShowWindow(d->hwnd, SW_HIDE);
}

bool Panel::IsVisible() const {
    Impl* d = m_impl;
    return d->hwnd && IsWindowVisible(d->hwnd);
}

void Panel::SetState(const Config& cfg, bool paused, const std::wstring& statusLine,
                     const std::wstring& adapterLine) {
    Impl* d = m_impl;
    d->cfg = cfg;
    d->paused = paused;
    d->statusLine = statusLine;
    d->adapterLine = adapterLine;
    // Keep the color sliders pinned to whatever the current custom color is.
    float h = 0, sat = 0, v = 0;
    Color::Hex(cfg.customColor).ToHsv(h, sat, v);
    d->colorHue = h;
    d->colorSat = Clamp(sat, 0.0f, 1.0f);
}

void Panel::SetSceneParams(const wchar_t* const* names, int count, bool colorSupported) {
    Impl* d = m_impl;
    d->colorSupported = colorSupported;
    count = std::max(0, std::min(count, kParamSliderCount));
    for (int i = 0; i < kParamSliderCount; ++i) {
        const wchar_t* src = (i < count && names && names[i] && names[i][0]) ? names[i] : nullptr;
        if (src) wcsncpy_s(d->paramNames[i], src, _TRUNCATE);
        else swprintf_s(d->paramNames[i], L"Parameter %d", i + 1);
    }
    d->paramCount = kParamSliderCount;
}

void Panel::Impl::DrawString(ID2D1DeviceContext* dc, IDWriteTextFormat* fmt, const wchar_t* text,
                             float x, float y, float w, float h, const Color& color,
                             DWRITE_TEXT_ALIGNMENT align) {
    if (!dc || !fmt || !text || !device.White()) return;
    if (fmt->GetTextAlignment() != align) fmt->SetTextAlignment(align);
    device.White()->SetColor(D2D1::ColorF(color.r, color.g, color.b, color.a));
    dc->DrawText(text, (UINT32)wcslen(text), fmt, D2D1::RectF(x, y, x + w, y + h),
                 device.White(), D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

int Panel::Impl::ClampViewHeight(int needed) const {
    RECT wa{0, 0, 1280, 800};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int waH = wa.bottom - wa.top - 24;
    int cap = std::min(kMaxViewH, waH);
    return std::max(340, std::min(needed, cap));
}

int Panel::Impl::NeededHeight() const {
    // Mirrors the flow in RebuildLayout.
    float y = 74.0f;
    y += 26.0f + kParamSliderCount * kSliderRow + 8.0f;   // heading + 4 sliders
    if (colorSupported) {
        y += 26.0f + 30.0f + kSliderRow + kSliderRow + 34.0f;   // heading + toggle + hue/sat + presets
    }
    y += 34.0f;                                           // seed row
    y += 26.0f + kPetKindCount / 2 * 30.0f
       + (kPetKindCount / 2 - 1) * 8.0f + 8.0f;           // companion heading + pet grid
    y += 26.0f + kRowH + kRowH + 10.0f;                   // performance heading + 2 rows
    y += 26.0f + kToggleCount * 30.0f;                    // behaviour heading + toggles
    y += kPad + 40.0f;           // footer
    return (int)y;
}

void Panel::RebuildLayout() {
    Impl* d = m_impl;
    d->hits.clear();

    float y = 74.0f;

    // --- painting parameters -------------------------------------------------
    d->heading1Y = y;
    y += 26.0f;
    for (int i = 0; i < kParamSliderCount; ++i) {
        d->sliderRects[i].left = (LONG)(kPad + 108);
        d->sliderRects[i].top = (LONG)(y + 7);
        d->sliderRects[i].right = (LONG)(kWidth - kPad);
        d->sliderRects[i].bottom = (LONG)(y + 21);
        Hit h;
        h.kind = Element::Slider;
        h.index = i;
        h.rect = d->sliderRects[i];
        d->hits.push_back(h);
        y += kSliderRow;
    }
    y += 8.0f;

    // --- color (only for scenes that use the global custom color) -----------
    if (d->colorSupported) {
        d->colorHeadingY = y;
        y += 26.0f;
        d->colorToggleRect.left = (LONG)kPad;
        d->colorToggleRect.top = (LONG)(y + 3);
        d->colorToggleRect.right = (LONG)(kWidth - kPad);
        d->colorToggleRect.bottom = (LONG)(y + 27);
        { Hit h; h.kind = Element::ColorToggle; h.index = 0; h.rect = d->colorToggleRect; d->hits.push_back(h); }
        y += 30.0f;
        for (int i = 0; i < 2; ++i) {
            RECT& r = (i == 0) ? d->colorHueRect : d->colorSatRect;
            r.left = (LONG)(kPad + 108);
            r.top = (LONG)(y + 7);
            r.right = (LONG)(kWidth - kPad);
            r.bottom = (LONG)(y + 21);
            Hit h; h.kind = Element::ColorSlider; h.index = i; h.rect = r;
            d->hits.push_back(h);
            y += kSliderRow;
        }
        float px = kPad;
        for (int i = 0; i < kPresetCount; ++i) {
            d->presetRects[i].left = (LONG)px;
            d->presetRects[i].top = (LONG)y;
            d->presetRects[i].right = (LONG)(px + 26);
            d->presetRects[i].bottom = (LONG)(y + 26);
            Hit h; h.kind = Element::ColorPreset; h.index = i; h.rect = d->presetRects[i];
            d->hits.push_back(h);
            px += 32.0f;
        }
        d->randomRect.left = (LONG)(px + 4);
        d->randomRect.top = (LONG)y;
        d->randomRect.right = (LONG)(px + 56);
        d->randomRect.bottom = (LONG)(y + 26);
        { Hit h; h.kind = Element::ColorRandom; h.index = 0; h.rect = d->randomRect; d->hits.push_back(h); }
        y += 34.0f;
    }

    // --- seed ----------------------------------------------------------------
    d->seedY = y;
    d->shuffleRect.left = (LONG)(kWidth - kPad - 86);
    d->shuffleRect.top = (LONG)(y + 1);
    d->shuffleRect.right = (LONG)(kWidth - kPad);
    d->shuffleRect.bottom = (LONG)(y + 27);
    { Hit h; h.kind = Element::ShuffleButton; h.index = 0; h.rect = d->shuffleRect; d->hits.push_back(h); }
    y += 34.0f;

    // --- performance --------------------------------------------------------
    d->heading2Y = y;
    y += 26.0f;
    auto buildSegments = [&](RECT* out, int count) {
        float sx = kPad + 96;
        float avail = (kWidth - kPad) - sx;
        float segW = avail / (float)count;
        for (int i = 0; i < count; ++i) {
            out[i].left = (LONG)(sx + segW * i + 1);
            out[i].top = (LONG)(y + 2);
            out[i].right = (LONG)(sx + segW * (i + 1) - 1);
            out[i].bottom = (LONG)(y + kSegH - 2);
            Hit h;
            h.kind = Element::FpsSegment;
            h.index = i;
            h.rect = out[i];
            d->hits.push_back(h);
        }
    };
    buildSegments(d->fpsRects, kFpsCount);
    y += kRowH;
    {
        float sx = kPad + 96;
        float avail = (kWidth - kPad) - sx;
        float segW = avail / (float)kQualityCount;
        for (int i = 0; i < kQualityCount; ++i) {
            d->qualityRects[i].left = (LONG)(sx + segW * i + 1);
            d->qualityRects[i].top = (LONG)(y + 2);
            d->qualityRects[i].right = (LONG)(sx + segW * (i + 1) - 1);
            d->qualityRects[i].bottom = (LONG)(y + kSegH - 2);
            Hit h;
            h.kind = Element::QualitySegment;
            h.index = i;
            h.rect = d->qualityRects[i];
            d->hits.push_back(h);
        }
    }
    y += kRowH + 10.0f;

    // --- companion (desktop pet species) -------------------------------------
    d->petHeadingY = y;
    y += 26.0f;
    {
        const float gap = 10.0f, bh = 30.0f, vgap = 8.0f;
        const float bw = (kWidth - kPad * 2 - gap) / 2.0f;
        for (int i = 0; i < kPetKindCount; ++i) {
            int col = i % 2, row = i / 2;
            d->petRects[i].left = (LONG)(kPad + col * (bw + gap));
            d->petRects[i].top = (LONG)(y + row * (bh + vgap));
            d->petRects[i].right = (LONG)(d->petRects[i].left + bw);
            d->petRects[i].bottom = (LONG)(d->petRects[i].top + bh);
            Hit h; h.kind = Element::PetButton; h.index = i; h.rect = d->petRects[i];
            d->hits.push_back(h);
        }
        y += (kPetKindCount / 2) * bh + (kPetKindCount / 2 - 1) * vgap;
    }
    y += 8.0f;

    // --- behaviour toggles --------------------------------------------------
    d->heading3Y = y;
    y += 26.0f;
    d->toggleY0 = y;
    for (int i = 0; i < kToggleCount; ++i) {
        Hit h;
        h.kind = Element::Toggle;
        h.index = i;
        h.rect.left = (LONG)kPad;
        h.rect.top = (LONG)(y + i * 30.0f);
        h.rect.right = (LONG)(kWidth - kPad);
        h.rect.bottom = h.rect.top + 30;
        d->hits.push_back(h);
    }

    // --- close (fixed chrome, not part of the scrolled content) -------------
    d->closeRect.left = (LONG)(kWidth - kPad - 26);
    d->closeRect.top = 20;
    d->closeRect.right = d->closeRect.left + 26;
    d->closeRect.bottom = d->closeRect.top + 26;
    {
        Hit h;
        h.kind = Element::Close;
        h.index = 0;
        h.rect = d->closeRect;
        d->hits.push_back(h);
    }

    // --- fit the window, then derive the scroll range ------------------------
    d->contentH = d->NeededHeight();
    if (d->hwnd) {
        int needed = d->ClampViewHeight(d->contentH);
        RECT wr{};
        GetWindowRect(d->hwnd, &wr);
        int curW = wr.right - wr.left, curH = wr.bottom - wr.top;
        if (curH != needed || curW != (int)kWidth) {
            SetWindowPos(d->hwnd, nullptr, 0, 0, (int)kWidth, needed,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            d->device.Resize((uint32_t)kWidth, (uint32_t)needed, 1.0f);
        }
        RECT cr{};
        GetClientRect(d->hwnd, &cr);
        d->viewH = cr.bottom - cr.top;
    } else {
        d->viewH = d->contentH;
    }
    d->maxScroll = std::max(0.0f, (float)(d->contentH - d->viewH));
    d->scroll = Clamp(d->scroll, 0.0f, d->maxScroll);
}

Panel::Hit Panel::HitTest(int px, int py) const {
    Impl* d = m_impl;
    // The close button lives in the fixed header; everything else scrolls.
    if (Contains(d->closeRect, px, py)) {
        Hit h;
        h.kind = Element::Close;
        h.index = 0;
        h.rect = d->closeRect;
        return h;
    }
    int cy = py + (int)d->scroll;
    for (const auto& h : d->hits) {
        if (h.kind == Element::Close) continue;
        if (Contains(h.rect, px, cy)) return h;
    }
    return Hit{};
}

void Panel::OnClick(const Hit& hit, int px) {
    Impl* d = m_impl;
    switch (hit.kind) {
        case Element::FpsSegment: {
            d->cfg.targetFps = kFpsValues[std::min(hit.index, kFpsCount - 1)];
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::QualitySegment: {
            d->cfg.quality = (Quality)kQualityValues[std::min(hit.index, kQualityCount - 1)];
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::Toggle: {
            switch (hit.index) {
                case 0: d->cfg.pauseOnBattery = !d->cfg.pauseOnBattery; break;
                case 1: d->cfg.pauseWhenLocked = !d->cfg.pauseWhenLocked; break;
                case 2: d->cfg.pauseWhenFullscreen = !d->cfg.pauseWhenFullscreen; break;
                case 3: d->cfg.pauseWhenOccluded = !d->cfg.pauseWhenOccluded; break;
                case 4: d->cfg.spanAllMonitors = !d->cfg.spanAllMonitors; break;
                case 5: d->cfg.startWithWindows = !d->cfg.startWithWindows; break;
                case 6: d->cfg.petEnabled = !d->cfg.petEnabled; break;
                case 7: d->cfg.petCard = !d->cfg.petCard; break;
                default: break;
            }
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::Slider:
        case Element::ColorSlider:
            d->dragging = hit.kind;
            d->dragIndex = hit.index;
            OnDrag(px);
            break;
        case Element::ColorToggle:
            d->cfg.useCustomColor = !d->cfg.useCustomColor;
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        case Element::ColorPreset: {
            int i = std::min(hit.index, kPresetCount - 1);
            d->cfg.customColor = kColorPresets[i];
            d->cfg.useCustomColor = true;
            float h = 0, sat = 0, v = 0;
            Color::Hex(d->cfg.customColor).ToHsv(h, sat, v);
            d->colorHue = h;
            d->colorSat = Clamp(sat, 0.0f, 1.0f);
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::ColorRandom: {
            Rng rng(GetTickCount() | 1u);
            d->colorHue = rng.Unit();
            d->colorSat = rng.Range(0.55f, 0.9f);
            Color c = Color::Hsv(d->colorHue, d->colorSat, 0.85f);
            d->cfg.customColor = ((unsigned)(c.r * 255.0f) << 16)
                               | ((unsigned)(c.g * 255.0f) << 8)
                               | (unsigned)(c.b * 255.0f);
            d->cfg.useCustomColor = true;
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::ShuffleButton:
            if (d->cb.onShuffle) d->cb.onShuffle();
            break;
        case Element::PetButton: {
            int i = std::min(hit.index, kPetKindCount - 1);
            d->cfg.petKind = PetKindKey((PetKind)i);
            if (d->cb.onConfig) d->cb.onConfig(d->cfg);
            break;
        }
        case Element::Close:
            Hide();
            if (d->cb.onClose) d->cb.onClose();
            break;
        default:
            break;
    }
}

void Panel::OnDrag(int px) {
    Impl* d = m_impl;
    if (d->dragging == Element::Slider) {
        int idx = Clamp(d->dragIndex, 0, kParamSliderCount - 1);
        const RECT& r = d->sliderRects[idx];
        float span = (float)std::max(1L, r.right - r.left);
        float t = Clamp01((float)(px - r.left) / span);
        d->cfg.sceneParam[idx] = t;
        if (d->cb.onConfig) d->cb.onConfig(d->cfg);
        return;
    }
    if (d->dragging == Element::ColorSlider) {
        const RECT& r = (d->dragIndex == 0) ? d->colorHueRect : d->colorSatRect;
        float span = (float)std::max(1L, r.right - r.left);
        float t = Clamp01((float)(px - r.left) / span);
        if (d->dragIndex == 0) d->colorHue = t; else d->colorSat = t;
        Color c = Color::Hsv(d->colorHue, d->colorSat, 0.85f);
        d->cfg.customColor = ((unsigned)(c.r * 255.0f) << 16)
                           | ((unsigned)(c.g * 255.0f) << 8)
                           | (unsigned)(c.b * 255.0f);
        d->cfg.useCustomColor = true;
        if (d->cb.onConfig) d->cb.onConfig(d->cfg);
    }
}

void Panel::EndDrag() {
    Impl* d = m_impl;
    d->dragging = Element::None;
    d->dragging2 = Element::None;
    d->dragIndex = -1;
}

bool Panel::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    Impl* d = m_impl;
    if (result) *result = 0;

    switch (msg) {
        case WM_ERASEBKGND:
            return true;   // the swap chain owns every pixel
        case WM_CLOSE:
            Hide();
            if (d->cb.onClose) d->cb.onClose();
            return true;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                Hide();
                if (d->cb.onClose) d->cb.onClose();
                return true;
            }
            break;
        case WM_LBUTTONDOWN: {
            // Taking focus on click is what lets WM_MOUSEWHEEL reach the panel.
            SetFocus(d->hwnd);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            OnClick(HitTest(x, y), x);
            return true;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (d->dragging == Element::Slider) {
                OnDrag(x);
            } else {
                Hit h = HitTest(x, y);
                d->hoverKind = h.kind;
                d->hoverIndex = h.index;
            }
            return true;
        }
        case WM_MOUSEWHEEL: {
            int delta = (short)HIWORD(wp);
            float before = d->scroll;
            d->scroll = Clamp(d->scroll - (float)delta * 0.65f, 0.0f, d->maxScroll);
            if (d->scroll != before) Render();
            return true;
        }
        case WM_MOUSELEAVE:
            d->hoverKind = Element::None;
            d->hoverIndex = -1;
            return true;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            EndDrag();
            return true;
        case WM_RBUTTONUP:
            return true;
        default:
            break;
    }
    return false;
}

void Panel::Render() {
    Impl* d = m_impl;
    if (!d->hwnd || !d->device.Dc() || !IsWindowVisible(d->hwnd)) return;
    if (d->hits.empty()) RebuildLayout();
    if (d->anim < 1.0f) d->anim = std::min(1.0f, d->anim + 0.08f);

    d->device.BeginFrame(false);   // event driven, not paced
    d->device.BeginScene();
    ID2D1DeviceContext* dc = d->device.Dc();
    ID2D1SolidColorBrush* brush = d->device.White();
    if (!dc || !brush) { d->device.EndScene(); return; }

    RECT cr{};
    GetClientRect(d->hwnd, &cr);
    const float w = (float)(cr.right - cr.left);
    const float h = (float)(cr.bottom - cr.top);

    D2D1_GRADIENT_STOP bg[2];
    bg[0].position = 0.0f; bg[0].color = D2D1::ColorF(kBg1.r, kBg1.g, kBg1.b, 1.0f);
    bg[1].position = 1.0f; bg[1].color = D2D1::ColorF(kBg0.r, kBg0.g, kBg0.b, 1.0f);
    draw::VerticalGradient(dc, w, h, bg, 2);

    // --- text formats (created per frame like the rest of the panel's UI; cheap
    // relative to a 33 ms frame and keeps the object lifetime obvious) ---------
    IDWriteFactory* dw = d->device.DWrite();
    IDWriteTextFormat* fmtTitle = nullptr, *fmtBody = nullptr, *fmtSmall = nullptr, *fmtScene = nullptr;
    if (dw) {
        dw->CreateTextFormat(L"Segoe UI Semibold", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19.0f, L"en-us", &fmtTitle);
        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &fmtBody);
        dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-us", &fmtSmall);
        dw->CreateTextFormat(L"Segoe UI Semibold", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-us", &fmtScene);
        for (IDWriteTextFormat* f : { fmtTitle, fmtBody, fmtSmall, fmtScene }) {
            if (f) {
                f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }
    }

    // --- fixed header ---------------------------------------------------------
    d->DrawString(dc, fmtTitle, L"Live Shan Shui", kPad, 16.0f, 260, 30, kText);
    d->DrawString(dc, fmtSmall, d->paused ? L"PAUSED" : L"LIVE", kPad, 46.0f, 120, 16,
                  d->paused ? Color::Hex(0xf0b429) : kAccent);
    draw::RoundedRect(dc, brush, kPad, 64.0f, w - kPad * 2, 2.0f, 1.0f,
                      Color(kAccent.r, kAccent.g, kAccent.b, 0.30f));

    {
        float cx = (float)(d->closeRect.left + d->closeRect.right) * 0.5f;
        float cy = (float)(d->closeRect.top + d->closeRect.bottom) * 0.5f;
        bool hot = (d->hoverKind == Element::Close);
        draw::RoundedRect(dc, brush, (float)d->closeRect.left, (float)d->closeRect.top, 26, 26, 13,
                          Color(1, 1, 1, hot ? 0.14f : 0.06f));
        draw::Line(dc, brush, cx - 5, cy - 5, cx + 5, cy + 5, 1.6f, kTextDim);
        draw::Line(dc, brush, cx + 5, cy - 5, cx - 5, cy + 5, 1.6f, kTextDim);
    }

    // --- scrolled content -------------------------------------------------------
    // Everything between the header and the footer is drawn under a translation
    // and clipped, so it can be taller than the window without painting the chrome.
    const float footerTop = h - 48.0f;
    dc->PushAxisAlignedClip(D2D1::RectF(0, 68.0f, w, footerTop), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(0, -d->scroll));

    auto heading = [&](const wchar_t* text, float y) {
        d->DrawString(dc, fmtSmall, text, kPad, y, 300, 16, kTextDim);
    };
    auto segmentRow = [&](const wchar_t* label, const wchar_t* const* labels, const int* values,
                          int count, int current, const RECT* rects, Element kind) {
        float yy = (float)rects[0].top - 2.0f;
        d->DrawString(dc, fmtBody, label, kPad, yy, 92, 20, kText);
        for (int i = 0; i < count; ++i) {
            bool on = (values[i] == current);
            bool hot = (d->hoverKind == kind && d->hoverIndex == i);
            draw::RoundedRect(dc, brush, (float)rects[i].left, (float)rects[i].top,
                              (float)(rects[i].right - rects[i].left),
                              (float)(rects[i].bottom - rects[i].top), 5.0f,
                              on ? Color(kAccent2.r, kAccent2.g, kAccent2.b, 0.80f)
                                 : (hot ? Color(0x24, 0x2c, 0x42) : kCard));
            d->DrawString(dc, fmtSmall, labels[i], (float)rects[i].left,
                          (float)rects[i].top + 4, (float)(rects[i].right - rects[i].left), 18,
                          on ? kText : kTextDim, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    };

    // per-scene parameters
    heading(L"SCENE SETTINGS", d->heading1Y);
    for (int i = 0; i < kParamSliderCount; ++i) {
        const RECT& r = d->sliderRects[i];
        float t = Clamp01(d->cfg.sceneParam[i]);
        float sx0 = (float)r.left, sx1 = (float)r.right;
        float sy = (float)r.top + 7.0f;
        d->DrawString(dc, fmtBody, d->paramNames[i], kPad, (float)r.top - 4, 104, 20, kText);
        draw::RoundedRect(dc, brush, sx0, sy, sx1 - sx0, 4, 2.0f, kTrack);
        draw::RoundedRect(dc, brush, sx0, sy, (sx1 - sx0) * t, 4, 2.0f, kAccent);
        draw::Circle(dc, brush, sx0 + (sx1 - sx0) * t, sy + 2, 6.0f, Color(1, 1, 1, 0.95f));
    }

    // --- color section -------------------------------------------------------
    if (d->colorSupported) {
        heading(L"COLOR", d->colorHeadingY);
        {
            float ty = (float)d->colorToggleRect.top + 2.0f;
            bool on = d->cfg.useCustomColor;
            bool hot = (d->hoverKind == Element::ColorToggle);
            d->DrawString(dc, fmtBody, L"Custom color", kPad, ty, w - kPad * 2 - 56, 20,
                          hot ? kText : Color(kText.r, kText.g, kText.b, 0.92f));
            draw::RoundedRect(dc, brush, w - kPad - 42, ty, 38, 20, 10,
                              on ? Color(kAccent.r, kAccent.g, kAccent.b, 0.85f) : kTrack);
            draw::Circle(dc, brush, w - kPad - 42 + (on ? 28.0f : 10.0f), ty + 10, 7.5f,
                         Color(1, 1, 1, 0.95f));
        }
        {
            const RECT& r = d->colorHueRect;
            float sx0 = (float)r.left, sx1 = (float)r.right;
            float sy = (float)(r.top + r.bottom) * 0.5f;
            d->DrawString(dc, fmtBody, L"Hue", kPad, (float)r.top - 4, 104, 20, kText);
            D2D1_GRADIENT_STOP hs[7];
            for (int i = 0; i < 7; ++i) {
                Color hc = Color::Hsv((float)i / 6.0f, 0.85f, 0.90f);
                hs[i].position = (float)i / 6.0f;
                hs[i].color = D2D1::ColorF(hc.r, hc.g, hc.b, 1.0f);
            }
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(hs, 7, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                gp.startPoint = D2D1::Point2F(sx0, sy);
                gp.endPoint = D2D1::Point2F(sx1, sy);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                    dc->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(sx0, sy - 3.0f, sx1, sy + 3.0f), 3, 3), gb);
                    gb->Release();
                }
                coll->Release();
            }
            draw::Circle(dc, brush, sx0 + (sx1 - sx0) * Clamp01(d->colorHue), sy, 6.5f,
                         Color(1, 1, 1, 0.95f));
        }
        {
            const RECT& r = d->colorSatRect;
            float sx0 = (float)r.left, sx1 = (float)r.right;
            float sy = (float)(r.top + r.bottom) * 0.5f;
            d->DrawString(dc, fmtBody, L"Saturation", kPad, (float)r.top - 4, 104, 20, kText);
            Color lo = Color::Hsv(d->colorHue, 0.0f, 0.60f);
            Color hi = Color::Hsv(d->colorHue, 1.0f, 0.85f);
            D2D1_GRADIENT_STOP ss[2];
            ss[0].position = 0.0f; ss[0].color = D2D1::ColorF(lo.r, lo.g, lo.b, 1.0f);
            ss[1].position = 1.0f; ss[1].color = D2D1::ColorF(hi.r, hi.g, hi.b, 1.0f);
            ID2D1GradientStopCollection* coll = nullptr;
            if (SUCCEEDED(dc->CreateGradientStopCollection(ss, 2, &coll)) && coll) {
                ID2D1LinearGradientBrush* gb = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp{};
                gp.startPoint = D2D1::Point2F(sx0, sy);
                gp.endPoint = D2D1::Point2F(sx1, sy);
                if (SUCCEEDED(dc->CreateLinearGradientBrush(gp, coll, &gb)) && gb) {
                    dc->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(sx0, sy - 3.0f, sx1, sy + 3.0f), 3, 3), gb);
                    gb->Release();
                }
                coll->Release();
            }
            draw::Circle(dc, brush, sx0 + (sx1 - sx0) * Clamp01(d->colorSat), sy, 6.5f,
                         Color(1, 1, 1, 0.95f));
        }
        for (int i = 0; i < kPresetCount; ++i) {
            const RECT& r = d->presetRects[i];
            float cx = ((float)r.left + r.right) * 0.5f;
            float cy = ((float)r.top + r.bottom) * 0.5f;
            Color pc = Color::Hex(kColorPresets[i]);
            bool active = d->cfg.useCustomColor && d->cfg.customColor == kColorPresets[i];
            bool hot = (d->hoverKind == Element::ColorPreset && d->hoverIndex == i);
            draw::Circle(dc, brush, cx, cy, 12.0f, pc.WithAlpha(hot || active ? 1.0f : 0.8f));
            if (active) {
                brush->SetColor(D2D1::ColorF(1, 1, 1, 0.9f));
                dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 14.0f, 14.0f), brush, 1.6f, nullptr);
            }
        }
        {
            const RECT& r = d->randomRect;
            bool hot = (d->hoverKind == Element::ColorRandom);
            draw::RoundedRect(dc, brush, (float)r.left, (float)r.top,
                              (float)(r.right - r.left), 26.0f, 13.0f,
                              hot ? Color(0x24, 0x2c, 0x42) : kCard);
            float rcx = ((float)r.left + r.right) * 0.5f - 16.0f;
            float rcy = ((float)r.top + r.bottom) * 0.5f;
            draw::RoundedRect(dc, brush, rcx - 8, rcy - 8, 16, 16, 4, Color(1, 1, 1, 0.10f));
            draw::Circle(dc, brush, rcx - 3.5f, rcy - 3.5f, 1.4f, kText);
            draw::Circle(dc, brush, rcx + 3.5f, rcy + 3.5f, 1.4f, kText);
            d->DrawString(dc, fmtSmall, L"Random", rcx + 12.0f, rcy - 7.0f, 44, 14,
                          kTextDim, DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    // --- seed row ------------------------------------------------------------
    {
        float ty = d->seedY;
        wchar_t seedText[32];
        swprintf_s(seedText, L"%u", (unsigned)d->cfg.variation);
        d->DrawString(dc, fmtSmall, L"SEED", kPad, ty + 6.0f, 90, 16, kTextDim);
        d->DrawString(dc, fmtBody, seedText, kPad + 52, ty + 3.0f, 160, 20, kText);
        bool hot = (d->hoverKind == Element::ShuffleButton);
        draw::RoundedRect(dc, brush, (float)d->shuffleRect.left, (float)d->shuffleRect.top,
                          (float)(d->shuffleRect.right - d->shuffleRect.left), 26.0f, 13.0f,
                          hot ? Color(0x24, 0x2c, 0x42) : kCard);
        d->DrawString(dc, fmtScene, L"Shuffle",
                      (float)d->shuffleRect.left, (float)d->shuffleRect.top + 4.0f,
                      (float)(d->shuffleRect.right - d->shuffleRect.left), 20,
                      hot ? kText : kTextDim, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // performance
    heading(L"PERFORMANCE", d->heading2Y);
    segmentRow(L"Frame rate", kFpsLabels, kFpsValues, kFpsCount, d->cfg.targetFps,
               d->fpsRects, Element::FpsSegment);
    segmentRow(L"Quality", kQualityLabels, kQualityValues, kQualityCount, (int)d->cfg.quality,
               d->qualityRects, Element::QualitySegment);

    // companion: desktop pet species picker
    heading(L"COMPANION", d->petHeadingY);
    for (int i = 0; i < kPetKindCount; ++i) {
        const RECT& r = d->petRects[i];
        bool active = _wcsicmp(d->cfg.petKind.c_str(), PetKindKey((PetKind)i)) == 0;
        bool hot = (d->hoverKind == Element::PetButton && d->hoverIndex == i);
        Color card = active ? Color(0x1e, 0x26, 0x3a)
                            : (hot ? Color(0x24, 0x2c, 0x42) : kCard);
        draw::RoundedRect(dc, brush, (float)r.left, (float)r.top,
                          (float)(r.right - r.left), (float)(r.bottom - r.top), 8.0f, card);
        if (active) {
            brush->SetColor(D2D1::ColorF(kAccent.r, kAccent.g, kAccent.b, 0.9f));
            dc->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF((float)r.left, (float)r.top,
                                              (float)r.right, (float)r.bottom), 8, 8),
                brush, 1.6f, nullptr);
        }
        float dotX = (float)r.left + 18.0f;
        float cy = ((float)r.top + r.bottom) * 0.5f;
        draw::Circle(dc, brush, dotX, cy, 6.5f, Color::Hex(PetKindAccent((PetKind)i)));
        draw::Circle(dc, brush, dotX - 2.0f, cy - 2.0f, 1.8f, Color(1, 1, 1, 0.55f));
        d->DrawString(dc, fmtBody, PetKindName((PetKind)i), (float)r.left + 32.0f,
                      (float)r.top + 5.0f, (float)(r.right - r.left) - 40.0f, 20,
                      active ? kText : Color(kText.r, kText.g, kText.b, 0.92f));
    }

    // behaviour toggles
    heading(L"BEHAVIOUR", d->heading3Y);
    for (const auto& hit : d->hits) {
        if (hit.kind != Element::Toggle) continue;
        bool on = false;
        switch (hit.index) {
            case 0: on = d->cfg.pauseOnBattery; break;
            case 1: on = d->cfg.pauseWhenLocked; break;
            case 2: on = d->cfg.pauseWhenFullscreen; break;
            case 3: on = d->cfg.pauseWhenOccluded; break;
            case 4: on = d->cfg.spanAllMonitors; break;
            case 5: on = d->cfg.startWithWindows; break;
            case 6: on = d->cfg.petEnabled; break;
            case 7: on = d->cfg.petCard; break;
            default: break;
        }
        float ty = (float)hit.rect.top + 5.0f;
        bool hot = (d->hoverKind == Element::Toggle && d->hoverIndex == hit.index);
        d->DrawString(dc, fmtBody, kToggleLabels[hit.index], kPad, ty, w - kPad * 2 - 56, 20,
                      hot ? kText : Color(kText.r, kText.g, kText.b, 0.92f));
        draw::RoundedRect(dc, brush, w - kPad - 42, ty, 38, 20, 10,
                          on ? Color(kAccent.r, kAccent.g, kAccent.b, 0.85f) : kTrack);
        draw::Circle(dc, brush, w - kPad - 42 + (on ? 28.0f : 10.0f), ty + 10, 7.5f,
                     Color(1, 1, 1, 0.95f));
    }

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->PopAxisAlignedClip();

    // --- fixed footer ----------------------------------------------------------
    float fy = h - 32.0f;
    d->DrawString(dc, fmtSmall, d->statusLine.c_str(), kPad, fy, w - kPad * 2, 16, kTextDim);
    d->DrawString(dc, fmtSmall, d->adapterLine.c_str(), kPad, fy + 14, w - kPad * 2, 16,
                  Color(kTextDim.r, kTextDim.g, kTextDim.b, 0.7f));

    d->device.EndScene();
    d->device.EndFrame();

    if (fmtTitle) fmtTitle->Release();
    if (fmtBody) fmtBody->Release();
    if (fmtSmall) fmtSmall->Release();
    if (fmtScene) fmtScene->Release();
}

} // namespace lp
