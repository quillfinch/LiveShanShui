#include "GfxDevice.h"
#include "../core/Win32Compat.h"
#include "../core/Log.h"
#include "../core/Math.h"
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <wincodec.h>
#include <dcomp.h>
#include <algorithm>

using namespace lp::abi;

namespace lp::gfx {

namespace {

// Rounds up to a multiple of 4 - keeps D2D bitmap strides aligned and avoids
// partial-tile GPU writes on the upscale pass.
inline uint32_t RoundUp4(uint32_t v) { return (v + 3u) & ~3u; }

} // namespace

struct Device::Impl {
    HWND hwnd = nullptr;
    uint32_t width = 0, height = 0;
    uint32_t sceneW = 0, sceneH = 0;
    float sceneScale = 1.0f;
    float dim = 0.0f;
    bool transparent = false;
    IDCompositionDevice* comp = nullptr;
    IDCompositionTarget* compTarget = nullptr;
    IDCompositionVisual* compVisual = nullptr;
    bool software = false;
    wchar_t adapter[128] = L"";

    ID3D11Device* d3d = nullptr;
    ID3D11DeviceContext* d3dCtx = nullptr;
    IDXGISwapChain1* swap = nullptr;
    IDXGISwapChain2* swap2 = nullptr;
    HANDLE frameWait = nullptr;

    ID2D1Factory1* factory = nullptr;
    ID2D1Device* d2d = nullptr;
    ID2D1DeviceContext* dc = nullptr;
    ID2D1Bitmap1* backBuffer = nullptr;   // D2D view of the swap chain image
    ID2D1Bitmap1* scene = nullptr;        // offscreen scene surface (may be null)
    ID2D1SolidColorBrush* white = nullptr;

    IDWriteFactory* dwrite = nullptr;
    IDWriteTextFormat* uiFont = nullptr;
    IDWriteTextFormat* uiFontSmall = nullptr;
    IDWriteTextFormat* uiFontBold = nullptr;

    int refreshHz = 60;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER frameStart{};
    float lastMs = 0, avgMs = 0;
    uint32_t frameCount = 0;

    bool frameOpen = false, sceneOpen = false;

    void ReleaseSurfaces() {
        if (scene) { scene->Release(); scene = nullptr; }
        if (backBuffer) { backBuffer->Release(); backBuffer = nullptr; }
    }

    bool CreateTargets() {
        ReleaseSurfaces();
        if (!swap || !dc) return false;

        IDXGISurface* surf = nullptr;
        if (FAILED(swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf)) || !surf) {
            LP_LOGE(L"gfx: GetBuffer failed");
            return false;
        }
        HRESULT hr = CreateBitmapFromDxgiSurface(
            dc, surf, DXGI_FORMAT_B8G8R8A8_UNORM,
            transparent ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE,
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, &backBuffer);
        surf->Release();
        if (FAILED(hr)) {
            LP_LOGE(L"gfx: back buffer bitmap failed hr=0x%08X", (unsigned)hr);
            return false;
        }

        sceneW = RoundUp4((uint32_t)std::max(1.0f, width * sceneScale));
        sceneH = RoundUp4((uint32_t)std::max(1.0f, height * sceneScale));
        if (sceneW == width && sceneH == height) {
            // Full resolution: draw straight into the back buffer, no copy needed.
            scene = nullptr;
            return true;
        }
        // Offscreen scene surface. Deliberately created at 96 DPI: the upscale in
        // EndFrame() draws it with an EXPLICIT destination rectangle, so its DPI never
        // influences the on-screen size. (Previously the DPI was scaled with the
        // render scale and the blit relied on DrawImage's intrinsic sizing, which on
        // some systems left the wallpaper smaller than the window.)
        D2D1_SIZE_U size = D2D1::SizeU(sceneW, sceneH);
        BitmapProps1Abi props = MakeBitmapProps(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED,
            D2D1_BITMAP_OPTIONS_TARGET, 96.0f);
        hr = reinterpret_cast<ID2D1DeviceContextAbi*>(dc)->CreateBitmap(size, nullptr, 0, &props, &scene);
        if (FAILED(hr)) {
            LP_LOGW(L"gfx: offscreen scene bitmap failed hr=0x%08X, using full-res path", (unsigned)hr);
            scene = nullptr;
            sceneW = width; sceneH = height;
            return true;
        }
        return true;
    }

    void CreateFonts() {
        if (!dwrite) return;
        auto make = [&](const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                        IDWriteTextFormat** out) {
            if (FAILED(dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out))) {
                *out = nullptr;
            } else {
                // Left/top alignment keeps panel layout arithmetic trivial.
                (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        };
        make(L"Segoe UI", 15.0f, DWRITE_FONT_WEIGHT_NORMAL, &uiFont);
        make(L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, &uiFontSmall);
        make(L"Segoe UI Semibold", 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &uiFontBold);
    }
};

Device::Device() : m_impl(new Impl()) {}

Device::~Device() {
    Destroy();
    delete m_impl;
    m_impl = nullptr;
}

void Device::Destroy() {
    Impl* d = m_impl;
    if (!d) return;
    if (d->frameWait) { CloseHandle(d->frameWait); d->frameWait = nullptr; }
    if (d->uiFont) { d->uiFont->Release(); d->uiFont = nullptr; }
    if (d->uiFontSmall) { d->uiFontSmall->Release(); d->uiFontSmall = nullptr; }
    if (d->uiFontBold) { d->uiFontBold->Release(); d->uiFontBold = nullptr; }
    if (d->dwrite) { d->dwrite->Release(); d->dwrite = nullptr; }
    if (d->white) { d->white->Release(); d->white = nullptr; }
    d->ReleaseSurfaces();
    if (d->dc) { d->dc->Release(); d->dc = nullptr; }
    if (d->d2d) { d->d2d->Release(); d->d2d = nullptr; }
    if (d->factory) { d->factory->Release(); d->factory = nullptr; }
    // A flip-model swap chain must be released before the device that owns it.
    if (d->swap2) { d->swap2->Release(); d->swap2 = nullptr; }
    if (d->swap) { d->swap->Release(); d->swap = nullptr; }
    if (d->compVisual) { d->compVisual->Release(); d->compVisual = nullptr; }
    if (d->compTarget) { d->compTarget->Release(); d->compTarget = nullptr; }
    if (d->comp) { d->comp->Release(); d->comp = nullptr; }
    if (d->d3dCtx) { d->d3dCtx->Release(); d->d3dCtx = nullptr; }
    if (d->d3d) { d->d3d->Release(); d->d3d = nullptr; }
}

bool Device::Create(const DeviceDesc& desc) {
    Impl* d = m_impl;
    Destroy();

    d->hwnd = (HWND)desc.hwnd;
    d->transparent = desc.transparent;
    d->width = desc.width;
    d->height = desc.height;
    d->sceneScale = desc.sceneScale <= 0 ? 1.0f : desc.sceneScale;
    QueryPerformanceFrequency(&d->qpcFreq);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                 nullptr, (void**)&d->factory)) || !d->factory) {
        LP_LOGE(L"gfx: D2D1CreateFactory failed");
        return false;
    }

    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                       D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   want, _countof(want), D3D11_SDK_VERSION,
                                   &d->d3d, &got, &d->d3dCtx);

    if (FAILED(hr)) {
        if (!desc.allowSoftware) return false;
        // WARP keeps the engine alive on machines with no usable GPU driver, at the
        // cost of CPU rendering. BGRA support is still required for Direct2D interop.
        LP_LOGW(L"gfx: hardware device unavailable (0x%08X), falling back to WARP", (unsigned)hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               want, _countof(want), D3D11_SDK_VERSION,
                               &d->d3d, &got, &d->d3dCtx);
        d->software = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        LP_LOGE(L"gfx: no D3D11 device at all (hr=0x%08X)", (unsigned)hr);
        RecordFailure(L"D3D11 device creation failed", hr);
        return false;
    }
    LP_LOGI(L"gfx: feature level 0x%X software=%d", (unsigned)got, d->software ? 1 : 0);

    IDXGIDevice* dxdev = nullptr;
    d->d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
    if (dxdev) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dxdev->GetAdapter(&ad)) && ad) {
            DXGI_ADAPTER_DESC adesc{};
            ad->GetDesc(&adesc);
            wcsncpy_s(d->adapter, adesc.Description, _TRUNCATE);
            ad->Release();
        }
        hr = d->factory->CreateDevice(dxdev, &d->d2d);
        dxdev->Release();
        if (FAILED(hr)) {
            LP_LOGE(L"gfx: D2D CreateDevice failed hr=0x%08X", (unsigned)hr);
            return false;
        }
    }
    // The device context must exist before any bitmap is derived from the swap chain.
    if (FAILED(d->d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d->dc))) {
        LP_LOGE(L"gfx: CreateDeviceContext failed");
        return false;
    }
    d->dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d->dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d->dc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&d->dwrite);
    d->CreateFonts();

    if (FAILED(d->dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &d->white)))
        d->white = nullptr;

    if (!Resize(desc.width, desc.height, desc.sceneScale)) return false;

    d->refreshHz = MonitorRefreshHz(d->hwnd);
    LP_LOGI(L"gfx: %ux%u scene=%ux%u refresh=%dHz adapter=%ls",
            d->width, d->height, d->sceneW, d->sceneH, d->refreshHz, d->adapter);
    return true;
}

bool Device::Resize(uint32_t width, uint32_t height, float sceneScale) {
    Impl* d = m_impl;
    if (!d->d3d || !d->dc || !d->hwnd) return false;
    if (width == 0 || height == 0) return false;

    d->sceneScale = sceneScale <= 0 ? 1.0f : sceneScale;
    if (d->swap && width == d->width && height == d->height) return d->CreateTargets();

    d->ReleaseSurfaces();
    if (d->frameWait) { CloseHandle(d->frameWait); d->frameWait = nullptr; }
    if (d->swap2) { d->swap2->Release(); d->swap2 = nullptr; }
    if (d->swap) { d->swap->Release(); d->swap = nullptr; }

    d->width = width;
    d->height = height;

    IDXGIDevice* dxdev = nullptr;
    d->d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
    IDXGIFactory2* fac = nullptr;
    if (dxdev) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dxdev->GetAdapter(&ad)) && ad) {
            ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
            ad->Release();
        }
    }

    // Per-pixel transparency: a composition swap chain (premultiplied alpha)
    // hosted by DirectComposition on a WS_EX_NOREDIRECTIONBITMAP window.
    auto setupComposition = [&]() -> bool {
        if (!d->comp) {
            IDXGIDevice* dxdev = nullptr;
            if (FAILED(d->d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev))) return false;
            HRESULT hr = DCompositionCreateDevice(dxdev, IID_PPV_ARGS(&d->comp));
            dxdev->Release();
            if (FAILED(hr)) return false;
            if (FAILED(d->comp->CreateTargetForHwnd(d->hwnd, TRUE, &d->compTarget))) return false;
            if (FAILED(d->comp->CreateVisual(&d->compVisual))) return false;
        }
        d->compVisual->SetContent(d->swap);
        d->compTarget->SetRoot(d->compVisual);
        return SUCCEEDED(d->comp->Commit());
    };

    bool ok = false;
    if (fac) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = width;
        sd.Height = height;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = d->transparent ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        HRESULT hr = d->transparent
            ? fac->CreateSwapChainForComposition(d->d3d, &sd, nullptr, &d->swap)
            : fac->CreateSwapChainForHwnd(d->d3d, d->hwnd, &sd, nullptr, nullptr, &d->swap);
        if (SUCCEEDED(hr)) {
            if (SUCCEEDED(d->swap->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&d->swap2))) {
                d->frameWait = d->swap2->GetFrameLatencyWaitableObject();
                // One frame in flight: lowest latency and lowest power.
                d->swap2->SetMaximumFrameLatency(1);
            }
            ok = d->CreateTargets() && (!d->transparent || setupComposition());
        } else {
            LP_LOGE(L"gfx: CreateSwapChainForHwnd failed hr=0x%08X", (unsigned)hr);
        }
        fac->Release();
    }
    if (dxdev) dxdev->Release();

    if (!ok) {
        // Surface the failure rather than silently rendering nothing.
        RecordFailure(L"Could not create the wallpaper swap chain", 0);
        return false;
    }
    d->refreshHz = MonitorRefreshHz(d->hwnd);
    d->frameCount = 0;
    d->avgMs = 0;
    LP_LOGI(L"gfx: resized %ux%u scene=%ux%u scale=%.2f", width, height, d->sceneW, d->sceneH, d->sceneScale);
    return true;
}

bool Device::SetSceneScale(float sceneScale) {
    Impl* d = m_impl;
    if (!d->dc) return false;
    if (std::abs(sceneScale - d->sceneScale) < 0.001f) return true;
    d->sceneScale = sceneScale;
    return d->CreateTargets();
}

void Device::SetDim(float dim) {
    m_impl->dim = Clamp(dim, 0.0f, 0.6f);
}

float Device::Dim() const { return m_impl->dim; }

void Device::BeginFrame(bool pace) {
    Impl* d = m_impl;
    if (!d->swap) return;
    if (pace && d->frameWait) {
        // Sleeps until the compositor is ready for a new frame. No spin, no timer.
        WaitForSingleObjectEx(d->frameWait, 250, FALSE);
    }
    QueryPerformanceCounter(&d->frameStart);
    d->frameOpen = true;
}

void Device::BeginScene() {
    Impl* d = m_impl;
    if (!d->dc) return;
    d->dc->BeginDraw();
    d->dc->SetTarget(d->scene ? (ID2D1Image*)d->scene : (ID2D1Image*)d->backBuffer);
    // Transparent devices start every frame from fully transparent pixels.
    if (d->transparent) d->dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    d->sceneOpen = true;
}

void Device::EndScene() {
    Impl* d = m_impl;
    if (!d || !d->sceneOpen) return;
    HRESULT hr = d->dc->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        LP_LOGW(L"gfx: device lost, recreating targets");
        d->CreateTargets();
    } else if (FAILED(hr)) {
        LP_LOGW(L"gfx: EndDraw hr=0x%08X", (unsigned)hr);
    }
    d->sceneOpen = false;
}

bool Device::EndFrame() {
    Impl* d = m_impl;
    if (!d->swap || !d->dc) return false;

    // Upscale the offscreen scene into the back buffer with an EXPLICIT destination
    // rectangle that covers the whole output. Relying on DrawImage's intrinsic sizing
    // (or on the scene bitmap's DPI) is what previously left the wallpaper smaller
    // than the window and the rest of the desktop showing through.
    if (d->scene && d->backBuffer) {
        d->dc->BeginDraw();
        d->dc->SetTarget(d->backBuffer);
        d->dc->SetTransform(D2D1::Matrix3x2F::Identity());
        d->dc->Clear(d->transparent ? D2D1::ColorF(0, 0, 0, 0) : D2D1::ColorF(0, 0, 0, 1));
        D2D1_RECT_F dst = D2D1::RectF(0, 0, (float)d->width, (float)d->height);
        D2D1_RECT_F src = D2D1::RectF(0, 0, (float)d->sceneW, (float)d->sceneH);
        // DrawBitmap (the 1.0 method) takes an explicit source + destination rect,
        // so the result fills the back buffer exactly, at any resolution.
        d->dc->DrawBitmap(d->scene, &dst, 1.0f,
                          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
        HRESULT hr = d->dc->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) d->CreateTargets();
    }

    // Config `dim`: one black overlay rect on the composited frame. Drawn as its own
    // pass so it applies identically on the upscale path and the full-res path, and
    // costs a single fill rather than touching every scene.
    if (d->dim > 0.001f && d->backBuffer && d->white) {
        d->dc->BeginDraw();
        d->dc->SetTarget(d->backBuffer);
        d->dc->SetTransform(D2D1::Matrix3x2F::Identity());
        d->white->SetColor(D2D1::ColorF(0, 0, 0, d->dim));
        d->dc->FillRectangle(D2D1::RectF(0, 0, (float)d->width, (float)d->height), d->white);
        HRESULT hr = d->dc->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) d->CreateTargets();
    }

    DXGI_PRESENT_PARAMETERS pp{};
    HRESULT phr = d->swap->Present1(1, 0, &pp);
    if (phr == DXGI_ERROR_DEVICE_REMOVED || phr == DXGI_ERROR_DEVICE_RESET) {
        LP_LOGE(L"gfx: device removed on Present");
        return false;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    d->lastMs = (float)((double)(now.QuadPart - d->frameStart.QuadPart) * 1000.0 / d->qpcFreq.QuadPart);
    // Exponential moving average keeps the adaptive logic from over-reacting.
    d->avgMs = (d->frameCount < 8) ? d->lastMs : (d->avgMs * 0.9f + d->lastMs * 0.1f);
    d->frameCount++;
    d->frameOpen = false;
    return true;
}

ID2D1DeviceContext* Device::Dc() const { return m_impl->dc; }
IDWriteFactory* Device::DWrite() const { return m_impl->dwrite; }
ID2D1SolidColorBrush* Device::White() const { return m_impl->white; }

uint32_t Device::Width() const { return m_impl->width; }
uint32_t Device::Height() const { return m_impl->height; }
uint32_t Device::SceneWidth() const { return m_impl->scene ? m_impl->sceneW : m_impl->width; }
uint32_t Device::SceneHeight() const { return m_impl->scene ? m_impl->sceneH : m_impl->height; }
int Device::RefreshHz() const { return m_impl->refreshHz; }
float Device::SceneScale() const { return m_impl->sceneScale; }
float Device::LastFrameMs() const { return m_impl->lastMs; }
float Device::AverageFrameMs() const { return m_impl->avgMs; }
const wchar_t* Device::AdapterName() const { return m_impl->adapter; }
bool Device::IsSoftware() const { return m_impl->software; }

bool Device::CapturePng(const std::wstring& path) const {
    Impl* d = m_impl;
    if (!d->swap || !d->d3d) return false;

    IDXGISurface* surf = nullptr;
    if (FAILED(d->swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf)) || !surf) return false;
    ID3D11Texture2D* tex = nullptr;
    surf->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    surf->Release();
    if (!tex) return false;

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;

    ID3D11Texture2D* stage = nullptr;
    if (FAILED(d->d3d->CreateTexture2D(&td, nullptr, &stage)) || !stage) { tex->Release(); return false; }
    d->d3dCtx->CopyResource(stage, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(d->d3dCtx->Map(stage, 0, D3D11_MAP_READ, 0, &map))) {
        stage->Release(); tex->Release(); return false;
    }

    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IWICImagingFactory), (void**)&wic)) && wic) {
        IWICStream* st = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        if (SUCCEEDED(wic->CreateStream(&st)) && st &&
            SUCCEEDED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
            if (SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache))) {
                IWICBitmapFrameEncode* fr = nullptr;
                IPropertyBag2* pb = nullptr;
                if (SUCCEEDED(enc->CreateNewFrame(&fr, &pb))) {
                    fr->Initialize(pb);
                    fr->SetSize(td.Width, td.Height);
                    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
                    fr->SetPixelFormat(&pf);
                    if (SUCCEEDED(fr->WritePixels(td.Height, map.RowPitch,
                                                  map.RowPitch * td.Height, (BYTE*)map.pData))) {
                        fr->Commit();
                        enc->Commit();
                        ok = true;
                    }
                    fr->Release();
                    if (pb) pb->Release();
                }
            }
            enc->Release();
        }
        st->Release();
        wic->Release();
    }
    d->d3dCtx->Unmap(stage, 0);
    stage->Release();
    tex->Release();
    return ok;
}

bool Device::ReadbackPixel(uint32_t x, uint32_t y, uint32_t* argb) const {
    Impl* d = m_impl;
    if (!d->swap || !d->d3d || !d->d3dCtx || !argb) return false;
    if (x >= d->width || y >= d->height) return false;

    IDXGISurface* surf = nullptr;
    if (FAILED(d->swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf)) || !surf) return false;
    ID3D11Texture2D* tex = nullptr;
    surf->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    surf->Release();
    if (!tex) return false;

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    td.ArraySize = 1;
    td.MipLevels = 1;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;

    ID3D11Texture2D* stage = nullptr;
    if (FAILED(d->d3d->CreateTexture2D(&td, nullptr, &stage)) || !stage) { tex->Release(); return false; }
    d->d3dCtx->CopyResource(stage, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    bool ok = false;
    if (SUCCEEDED(d->d3dCtx->Map(stage, 0, D3D11_MAP_READ, 0, &map)) && map.pData) {
        const BYTE* row = (const BYTE*)map.pData + (size_t)y * map.RowPitch;
        const BYTE* px = row + (size_t)x * 4;   // BGRA
        *argb = ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
        d->d3dCtx->Unmap(stage, 0);
        ok = true;
    }
    stage->Release();
    tex->Release();
    return ok;
}

} // namespace lp::gfx
