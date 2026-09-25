// LivePaper - control panel.
//
// A single Direct2D surface with custom hit-testing instead of a tree of child
// controls. For a panel this small that is both less code and less overhead: one
// swap chain, one WM_PAINT-free present per frame, and nothing to re-layout.
//
// The content grows with the scene count and the per-scene parameter sliders, so
// everything below the header scrolls: RebuildLayout() works in content
// coordinates, rendering applies a -scroll translation inside a clip rectangle,
// and hit-testing adds the scroll back in. The geometry therefore has exactly one
// source of truth again.
#pragma once
#include "../core/Config.h"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace lp {

class Panel {
public:
    // Raised when the user changes something that needs to be applied to the engine.
    struct Callbacks {
        std::function<void(SceneId)> onScene;
        std::function<void(const Config&)> onConfig;
        std::function<void()> onClose;
    };

    Panel();
    ~Panel();
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    bool Create(HINSTANCE instance, const Callbacks& cb);
    void Destroy();

    void Show();
    void Hide();
    bool IsVisible() const;

    // Pushes fresh state in (called when the engine's config changes elsewhere).
    void SetState(const Config& cfg, bool paused, const std::wstring& statusLine,
                  const std::wstring& adapterLine);

    // Labels for the active scene's parameter sliders (ParamName(i), 0..3).
    // Stored by copy: the scene that produced them may be destroyed at any time.
    void SetSceneParams(const wchar_t* const* names, int count);

    HWND Window() const;

    // Returns true when the message was consumed by the panel.
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

    // Renders one frame; only meaningful while visible.
    void Render();

private:
    enum class Element { None, SceneButton, Slider, Toggle, Close, FpsSegment, QualitySegment };

    struct Hit {
        Element kind = Element::None;
        int index = -1;      // scene index, slider index, or toggle index
        RECT rect{};
    };

    void RebuildLayout();
    Hit HitTest(int x, int y) const;
    void OnClick(const Hit& hit, int x);
    void OnDrag(int x);
    void EndDrag();

    struct Impl;
    Impl* m_impl;
};

} // namespace lp
