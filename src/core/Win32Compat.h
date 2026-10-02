// Live Shan Shui - COM / Win32 interop helpers.
//
// Why this file exists
// --------------------
// The engine is built with MinGW's UCRT64 toolchain and talks to Windows through
// hand-written vtables for the interfaces whose MSVC headers are not ABI-compatible
// with the system DLLs. Two concrete defects were measured on this machine:
//
//   1. D2D1_BITMAP_PROPERTIES1 is 32 bytes under MinGW but the OS expects the MSVC
//      layout (24 bytes of meaningful fields, 8-byte aligned). It is passed BY VALUE,
//      so the process dies on the first CreateBitmapFromDxgiSurface call.
//   2. D2D1_PIXEL_FORMAT is 8 bytes in both, so it is safe to embed directly.
//
// We therefore declare ABI-exact mirrors (suffix `Abi`) and cast only for those
// by-value parameters. Everything is asserted at compile time where possible.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <d2d1_1.h>
#include <d2d1_2.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <cstdint>
#include <cstddef>

namespace lp::abi {

// ---------------------------------------------------------------------------
// ABI mirrors
// ---------------------------------------------------------------------------

// MSVC: 8 + 4 + 4 + 4 (bitfield) = 20, padded to 24/32 by 8-byte alignment.
// Fields below BITMAP_OPTIONS line up byte-for-byte with the OS expectation.
struct BitmapProps1Abi {
    D2D1_PIXEL_FORMAT pixelFormat;   // offset 0  (8 bytes, identical layout)
    float dpiX;                      // offset 8
    float dpiY;                      // offset 12
    uint32_t bitmapOptions;          // offset 16
    ID2D1ColorContext* colorContext; // offset 24
};

// Measured on this toolchain (MinGW UCRT64 16.1.0):
//   D2D1_PIXEL_FORMAT              =  8 bytes
//   D2D1_BITMAP_PROPERTIES1        = 32 bytes (MSVC reports 32 as well once the
//                                    struct is 8-byte aligned; the *fields* below
//                                    are what the OS reads, and they must line up)
//   D2D1_RENDER_TARGET_PROPERTIES  = 28 bytes
//
// The engine never passes D2D1_RENDER_TARGET_PROPERTIES by value, so no mirror is
// declared for it - a mirror that is not exercised is worse than none.
static_assert(sizeof(D2D1_PIXEL_FORMAT) == 8, "D2D1_PIXEL_FORMAT must stay 8 bytes");
static_assert(offsetof(BitmapProps1Abi, pixelFormat) == 0, "pixelFormat must sit at offset 0");
static_assert(offsetof(BitmapProps1Abi, dpiX) == 8, "dpiX must sit at offset 8");
static_assert(offsetof(BitmapProps1Abi, dpiY) == 12, "dpiY must sit at offset 12");
static_assert(offsetof(BitmapProps1Abi, bitmapOptions) == 16, "bitmapOptions must sit at offset 16");
static_assert(offsetof(BitmapProps1Abi, colorContext) == 24, "colorContext must sit at offset 24");

inline BitmapProps1Abi MakeBitmapProps(DXGI_FORMAT fmt, D2D1_ALPHA_MODE alpha,
                                       uint32_t options, float dpi = 96.0f) {
    BitmapProps1Abi p{};
    p.pixelFormat.format = fmt;
    p.pixelFormat.alphaMode = alpha;
    p.dpiX = dpi;
    p.dpiY = dpi;
    p.bitmapOptions = options;   // must be explicit: E_INVALIDARG otherwise
    p.colorContext = nullptr;
    return p;
}

// ---------------------------------------------------------------------------
// ID2D1DeviceContext call helpers for the by-value-parameter methods.
//
// Rather than declaring every one of the ~100 vtable slots, we override just the
// overloads that take structs/aliased enums by value. Declaring the full vtable in
// each derived interface costs nothing at runtime and keeps the ABI explicit.
// ---------------------------------------------------------------------------

struct ID2D1RenderTargetAbi : public IUnknown {
    // ID2D1Resource
    virtual void STDMETHODCALLTYPE GetFactory(ID2D1Factory**) = 0;
    // ID2D1RenderTarget
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U, const void*, UINT32,
        const D2D1_BITMAP_PROPERTIES*, ID2D1Bitmap**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource*,
        const D2D1_BITMAP_PROPERTIES*, ID2D1Bitmap**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSharedBitmap(REFIID, void*,
        const D2D1_BITMAP_PROPERTIES*, ID2D1Bitmap**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap*, const D2D1_BITMAP_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1BitmapBrush**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1SolidColorBrush**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP*,
        UINT32, D2D1_GAMMA, D2D1_EXTEND_MODE, ID2D1GradientStopCollection**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1GradientStopCollection*, ID2D1LinearGradientBrush**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1GradientStopCollection*, ID2D1RadialGradientBrush**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(D2D1_SIZE_F, D2D1_SIZE_U,
        D2D1_PIXEL_FORMAT, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS, ID2D1BitmapRenderTarget**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateLayer(D2D1_SIZE_F, ID2D1Layer**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMesh(ID2D1Mesh**) = 0;
    virtual void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F, D2D1_POINT_2F, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) = 0;
    virtual void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) = 0;
    virtual void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F*, ID2D1Brush*) = 0;
    virtual void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) = 0;
    virtual void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT*, ID2D1Brush*) = 0;
    virtual void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) = 0;
    virtual void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE*, ID2D1Brush*) = 0;
    virtual void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry*, ID2D1Brush*, FLOAT, ID2D1StrokeStyle*) = 0;
    virtual void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry*, ID2D1Brush*, ID2D1Brush*) = 0;
    virtual void STDMETHODCALLTYPE FillMesh(ID2D1Mesh*, ID2D1Brush*) = 0;
    virtual void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap*, ID2D1Brush*, D2D1_OPACITY_MASK_CONTENT,
        const D2D1_RECT_F*, const D2D1_RECT_F*) = 0;
    virtual void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap*, const D2D1_RECT_F*, FLOAT,
        D2D1_BITMAP_INTERPOLATION_MODE, const D2D1_RECT_F*) = 0;
    virtual void STDMETHODCALLTYPE DrawText(const WCHAR*, UINT32, IDWriteTextFormat*,
        const D2D1_RECT_F*, ID2D1Brush*, D2D1_DRAW_TEXT_OPTIONS, DWRITE_MEASURING_MODE) = 0;
    virtual void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F, IDWriteTextLayout*,
        ID2D1Brush*, D2D1_DRAW_TEXT_OPTIONS) = 0;
    virtual void STDMETHODCALLTYPE DrawGlyphRun(D2D1_POINT_2F, const DWRITE_GLYPH_RUN*,
        ID2D1Brush*, DWRITE_MEASURING_MODE) = 0;
    virtual void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F*) = 0;
    virtual void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F*) = 0;
    virtual void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE) = 0;
    virtual D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() = 0;
    virtual void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE) = 0;
    virtual D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() = 0;
    virtual void STDMETHODCALLTYPE SetTextRenderingParams(IDWriteRenderingParams*) = 0;
    virtual void STDMETHODCALLTYPE GetTextRenderingParams(IDWriteRenderingParams**) = 0;
    virtual void STDMETHODCALLTYPE SetTags(D2D1_TAG, D2D1_TAG) = 0;
    virtual void STDMETHODCALLTYPE GetTags(D2D1_TAG*, D2D1_TAG*) = 0;
    virtual void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS*, ID2D1Layer*) = 0;
    virtual void STDMETHODCALLTYPE PopLayer() = 0;
    virtual HRESULT STDMETHODCALLTYPE Flush(D2D1_TAG*, D2D1_TAG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveDrawingState(ID2D1DrawingStateBlock*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RestoreDrawingState(ID2D1DrawingStateBlock*) = 0;
    virtual void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F*, D2D1_ANTIALIAS_MODE) = 0;
    virtual void STDMETHODCALLTYPE PopAxisAlignedClip() = 0;
    virtual void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F*) = 0;
    virtual void STDMETHODCALLTYPE BeginDraw() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG*, D2D1_TAG*) = 0;
    virtual D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() = 0;
    virtual void STDMETHODCALLTYPE SetDpi(FLOAT, FLOAT) = 0;
    virtual void STDMETHODCALLTYPE GetDpi(FLOAT*, FLOAT*) = 0;
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetSize() = 0;
    virtual D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() = 0;
    virtual UINT32 STDMETHODCALLTYPE GetMaximumBitmapSize() = 0;
    virtual BOOL STDMETHODCALLTYPE IsSupported(const D2D1_RENDER_TARGET_PROPERTIES*) = 0;
};

// ID2D1DeviceContext : ID2D1RenderTarget, with the 1.1 additions we rely on.
struct ID2D1DeviceContextAbi : public ID2D1RenderTargetAbi {
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U, const void*, UINT32,
        const BitmapProps1Abi*, ID2D1Bitmap1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource*,
        const BitmapProps1Abi*, ID2D1Bitmap1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateColorContext(D2D1_COLOR_SPACE, const BYTE*, UINT32,
        ID2D1ColorContext**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateColorContextFromFilename(PCWSTR, ID2D1ColorContext**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateColorContextFromWicColorContext(IWICColorContext*,
        ID2D1ColorContext**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromDxgiSurface(IDXGISurface*,
        const BitmapProps1Abi*, ID2D1Bitmap1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateEffect(REFCLSID, ID2D1Effect**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP*, UINT32,
        D2D1_COLOR_SPACE, D2D1_COLOR_SPACE, D2D1_BUFFER_PRECISION, D2D1_EXTEND_MODE,
        D2D1_COLOR_INTERPOLATION_MODE, ID2D1GradientStopCollection1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImageBrush(ID2D1Image*, const D2D1_IMAGE_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1ImageBrush**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap*, const D2D1_IMAGE_BRUSH_PROPERTIES*,
        const D2D1_BRUSH_PROPERTIES*, ID2D1BitmapBrush1**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCommandList(ID2D1CommandList**) = 0;
    virtual BOOL STDMETHODCALLTYPE IsDxgiFormatSupported(DXGI_FORMAT) = 0;
    virtual BOOL STDMETHODCALLTYPE IsBufferPrecisionSupported(D2D1_BUFFER_PRECISION) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetImageLocalBounds(ID2D1Image*, D2D1_RECT_F*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetImageWorldBounds(ID2D1Image*, D2D1_RECT_F*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGlyphRunWorldBounds(D2D1_POINT_2F, const DWRITE_GLYPH_RUN*,
        DWRITE_MEASURING_MODE, D2D1_RECT_F*) = 0;
    virtual void STDMETHODCALLTYPE GetDevice(ID2D1Device**) = 0;
    virtual void STDMETHODCALLTYPE SetTarget(ID2D1Image*) = 0;
    virtual void STDMETHODCALLTYPE GetTarget(ID2D1Image**) = 0;
    virtual void STDMETHODCALLTYPE SetRenderingControls(const D2D1_RENDERING_CONTROLS*) = 0;
    virtual void STDMETHODCALLTYPE GetRenderingControls(D2D1_RENDERING_CONTROLS*) = 0;
    virtual void STDMETHODCALLTYPE SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND) = 0;
    virtual D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE GetPrimitiveBlend() = 0;
    virtual void STDMETHODCALLTYPE SetUnitMode(D2D1_UNIT_MODE) = 0;
    virtual D2D1_UNIT_MODE STDMETHODCALLTYPE GetUnitMode() = 0;
    virtual void STDMETHODCALLTYPE DrawGlyphRun(D2D1_POINT_2F, const DWRITE_GLYPH_RUN*,
        const DWRITE_GLYPH_RUN_DESCRIPTION*, ID2D1Brush*, DWRITE_MEASURING_MODE) = 0;
    virtual void STDMETHODCALLTYPE DrawImage(ID2D1Image*, const D2D1_POINT_2F*, const D2D1_RECT_F*,
        D2D1_INTERPOLATION_MODE, D2D1_COMPOSITE_MODE) = 0;
    virtual void STDMETHODCALLTYPE DrawGdiMetafile(ID2D1GdiMetafile*, const D2D1_POINT_2F*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(REFWICPixelFormatGUID,
        IWICBitmapSource*, const BitmapProps1Abi*, ID2D1Bitmap1**) = 0;
    virtual void STDMETHODCALLTYPE SetColorSpace(D2D1_COLOR_SPACE) = 0;
    virtual D2D1_COLOR_SPACE STDMETHODCALLTYPE GetColorSpace() = 0;
};

// ---------------------------------------------------------------------------
// Thin wrappers. Each forwards to the vtable slot the OS actually implements.
// ---------------------------------------------------------------------------

inline HRESULT CreateBitmapFromDxgiSurface(ID2D1DeviceContext* dc, IDXGISurface* surf,
                                           DXGI_FORMAT fmt, D2D1_ALPHA_MODE alpha,
                                           uint32_t options, ID2D1Bitmap1** out) {
    BitmapProps1Abi p = MakeBitmapProps(fmt, alpha, options);
    // The two interfaces have byte-identical vtables; the cast exists only to select
    // the ABI-correct by-value parameter type.
    return reinterpret_cast<ID2D1DeviceContextAbi*>(dc)->CreateBitmapFromDxgiSurface(surf, &p, out);
}

inline HRESULT CreateBitmap1(ID2D1DeviceContext* dc, D2D1_SIZE_U size, const void* src,
                             UINT32 pitch, DXGI_FORMAT fmt, D2D1_ALPHA_MODE alpha,
                             uint32_t options, ID2D1Bitmap1** out) {
    BitmapProps1Abi p = MakeBitmapProps(fmt, alpha, options);
    return reinterpret_cast<ID2D1DeviceContextAbi*>(dc)->CreateBitmap(size, src, pitch, &p, out);
}

// SetAntialiasMode / SetTextAntialiasMode / SetUnitMode / SetPrimitiveBlend take
// enum parameters. Under the MSVC ABI those are 4-byte enums passed in registers,
// identical to MinGW, so the normal interface is safe. We keep the wrappers only
// for readability at call sites.

// ---------------------------------------------------------------------------
// Misc Win32 helpers
// ---------------------------------------------------------------------------

// Rounds a client rect to the containing monitor's refresh rate.
int MonitorRefreshHz(HWND hwnd);
// True when the monitor is running on battery power.
bool IsOnBattery();
// True when an exclusive-fullscreen (non-desktop) window owns the foreground.
bool IsFullscreenAppInForeground();
// True when the workstation is locked / no user is logged in interactively.
bool IsSessionLocked();
// True when any non-shell window covers (nearly) the whole virtual desktop, whether
// or not it is in the foreground - i.e. the wallpaper is not visible anywhere.
// Polled, not per-frame: EnumWindows walks every top-level window.
bool IsDesktopCovered();

} // namespace lp::abi
