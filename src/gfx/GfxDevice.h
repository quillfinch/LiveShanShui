// Live Shan Shui - D3D11 + Direct2D render device.
//
// Design notes
// ------------
// * Flip-model (DXGI_SWAP_EFFECT_FLIP_DISCARD) with a waitable frame-latency object.
//   Waiting on that handle costs no CPU, unlike the DWM timer-driven blt path.
// * The scene is optionally rendered into an offscreen D2D bitmap at a fraction of
//   the output resolution and then upscaled with linear filtering. Glow/blur-heavy
//   scenes are visually indistinguishable at 0.8x but cost ~35% less fill rate.
// * All scene drawing targets `SceneTarget()`; the device composites to the swap
//   chain back buffer. Scenes never touch DXGI directly.
#pragma once
#include <cstdint>
#include <string>

struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace lp::gfx {

struct DeviceDesc {
    void* hwnd = nullptr;       // target window (WorkerW child or panel)
    uint32_t width = 0;         // output size in pixels
    uint32_t height = 0;
    float sceneScale = 1.0f;    // internal render scale (quality)
    bool allowSoftware = true;  // fall back to WARP if no hardware device
    bool transparent = false;   // per-pixel alpha via DirectComposition
};

class Device {
public:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool Create(const DeviceDesc& desc);
    void Destroy();

    // Recreates the swap chain (monitor change / DPI change / resize).
    bool Resize(uint32_t width, uint32_t height, float sceneScale);
    bool SetSceneScale(float sceneScale);

    // Extra darkening applied over the composited frame (config `dim`, 0..0.6).
    // Drawn onto the back buffer in EndFrame so it costs one rect, never a scene.
    void SetDim(float dim);
    float Dim() const;

    // --- frame lifecycle -----------------------------------------------------
    // Blocks on the swap chain's waitable object when `pace` is true.
    void BeginFrame(bool pace = true);
    bool EndFrame();                       // composites + Present

    // Called by scenes: bind the scene bitmap (or the back buffer when scale == 1).
    void BeginScene();
    // Ends scene drawing and upscales into the back buffer.
    void EndScene();

    uint32_t Width() const;
    uint32_t Height() const;
    uint32_t SceneWidth() const;
    uint32_t SceneHeight() const;
    int      RefreshHz() const;
    float    SceneScale() const;

    // --- drawing accessors (C++ COM pointers, no wrappers) -------------------
    ID2D1DeviceContext* Dc() const;
    IDWriteFactory*     DWrite() const;

    // Cached white brush; scenes tint by setting colour. Saves a COM call per draw.
    ID2D1SolidColorBrush* White() const;

    // Saves/restores the current swap chain image to a PNG (diagnostics + tests).
    bool CapturePng(const std::wstring& path) const;

    // Reads one pixel of the presented back buffer (0xAARRGGBB). Returns false on
    // failure. Used to prove full-screen coverage by sampling the far corners.
    bool ReadbackPixel(uint32_t x, uint32_t y, uint32_t* argb) const;

    // Rolling average of the CPU cost of a frame, in milliseconds.
    float LastFrameMs() const;
    float AverageFrameMs() const;

    const wchar_t* AdapterName() const;
    bool  IsSoftware() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace lp::gfx
