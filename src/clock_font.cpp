// SPDX-License-Identifier: GPL-3.0-or-later
#include "clock_font.hpp"
#include <dwrite_3.h>
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <cmath>
namespace acrylic {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT hr) { if (FAILED(hr)) throw hr; }
class TextRenderer final : public IDWriteTextRenderer {
    std::atomic<ULONG> refs{1};
    ComPtr<IDWriteBitmapRenderTarget> target;
    ComPtr<IDWriteRenderingParams> parameters;
public:
    TextRenderer(IDWriteBitmapRenderTarget* bitmap, IDWriteRenderingParams* params) : target(bitmap), parameters(params) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDWriteTextRenderer) && iid != __uuidof(IDWritePixelSnapping)) return E_NOINTERFACE;
        *out = static_cast<IDWriteTextRenderer*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG value = --refs; if (!value) delete this; return value; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* value) override { *value = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* matrix) override { return target->GetCurrentTransform(matrix); }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* value) override { *value = target->GetPixelsPerDip(); return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE measuring,
        const DWRITE_GLYPH_RUN* glyphs, const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        return target->DrawGlyphRun(x, y, measuring, glyphs, parameters.Get(), RGB(255, 255, 255), nullptr);
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return E_NOTIMPL; }
};
}
struct ClockFontRenderer::Impl {
    ComPtr<IDWriteFactory5> factory;
    ComPtr<IDWriteFontCollection1> collection;
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_METRICS fontMetrics{};
    std::wstring matchedFamily;
    UINT matchedWeight = 0;
    double sizeRatio = 1;
    explicit Impl(const std::wstring& path) {
        if (path.empty()) throw HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory5), reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
        ComPtr<IDWriteFontFile> file;
        Check(factory->CreateFontFileReference(path.c_str(), nullptr, &file));
        ComPtr<IDWriteFontSetBuilder1> builder;
        Check(factory->CreateFontSetBuilder(&builder)); Check(builder->AddFontFile(file.Get()));
        ComPtr<IDWriteFontSet> fonts; Check(builder->CreateFontSet(&fonts));
        Check(factory->CreateFontCollectionFromFontSet(fonts.Get(), &collection));
        if (!collection->GetFontFamilyCount()) throw HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        ComPtr<IDWriteFontFamily> first;
        Check(static_cast<IDWriteFontCollection*>(collection.Get())->GetFontFamily(0, &first));
        ComPtr<IDWriteLocalizedStrings> names; Check(first->GetFamilyNames(&names));
        UINT32 index = 0, length = 0; BOOL found = FALSE;
        Check(names->FindLocaleName(L"en-us", &index, &found)); if (!found) index = 0;
        Check(names->GetStringLength(index, &length)); family.resize(length + 1);
        Check(names->GetString(index, family.data(), length + 1)); family.resize(length);
        ComPtr<IDWriteFont> font; Check(first->GetFont(0, &font)); weight = font->GetWeight();
        font->GetMetrics(&fontMetrics);
    }
    double MatchedSize(double size, const std::wstring& referenceFamily, UINT referenceWeight) {
        if (matchedFamily != referenceFamily || matchedWeight != referenceWeight) {
            ComPtr<IDWriteFontCollection> system;
            Check(static_cast<IDWriteFactory*>(factory.Get())->GetSystemFontCollection(&system));
            UINT32 index = 0; BOOL found = FALSE;
            Check(system->FindFamilyName(referenceFamily.c_str(), &index, &found));
            if (!found) Check(system->FindFamilyName(L"Segoe UI", &index, &found));
            if (!found) throw E_FAIL;
            ComPtr<IDWriteFontFamily> reference; Check(system->GetFontFamily(index, &reference));
            ComPtr<IDWriteFont> font;
            Check(reference->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(referenceWeight),
                DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font));
            DWRITE_FONT_METRICS metrics{}; font->GetMetrics(&metrics);
            const double body = fontMetrics.xHeight ? fontMetrics.xHeight : fontMetrics.capHeight;
            const double target = metrics.xHeight ? metrics.xHeight : metrics.capHeight;
            if (!body || !target || !metrics.designUnitsPerEm || !fontMetrics.designUnitsPerEm) throw E_FAIL;
            sizeRatio = target * fontMetrics.designUnitsPerEm / (body * metrics.designUnitsPerEm);
            // Ultralight strokes need a small optical increase at taskbar sizes.
            // Pangaia's 12-DIP nominal size becomes 13; Playground's much smaller
            // lowercase body instead determines its larger, measured correction.
            if (weight < DWRITE_FONT_WEIGHT_LIGHT) sizeRatio = std::max(sizeRatio, 13.0 / 12.0);
            sizeRatio = std::clamp(sizeRatio, 0.5, 3.0);
            matchedFamily = referenceFamily; matchedWeight = referenceWeight;
        }
        return size * sizeRatio;
    }
    ClockPixels Render(const std::wstring& text, double size, double scale, uint32_t argb,
        const std::wstring& referenceFamily, UINT referenceWeight) {
        if (size <= 0 || size > 128 || scale <= 0 || scale > 8 || text.size() > 128) throw E_INVALIDARG;
        const double matchedSize = MatchedSize(size, referenceFamily, referenceWeight);
        ComPtr<IDWriteTextFormat> format;
        Check(factory->CreateTextFormat(family.c_str(), collection.Get(), weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, static_cast<FLOAT>(matchedSize), L"en-us", &format));
        Check(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        ComPtr<IDWriteTextLayout> layout;
        Check(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), 4096, 512, &layout));
        DWRITE_TEXT_METRICS metrics{}; DWRITE_OVERHANG_METRICS overhang{};
        Check(layout->GetMetrics(&metrics)); Check(layout->GetOverhangMetrics(&overhang));
        DWRITE_LINE_METRICS line{}; UINT32 lines = 0;
        Check(layout->GetLineMetrics(&line, 1, &lines));
        const FLOAT x = 1 + std::max(0.0f, overhang.left), y = 1 + std::max(0.0f, overhang.top);
        ClockPixels result;
        result.baseline = y + line.baseline;
        result.fontSize = matchedSize;
        result.dipWidth = std::ceil(x + metrics.widthIncludingTrailingWhitespace + std::max(0.0f, overhang.right) + 1);
        result.dipHeight = std::ceil(y + metrics.height + std::max(0.0f, overhang.bottom) + 1);
        result.width = static_cast<UINT>(std::ceil(result.dipWidth * scale));
        result.height = static_cast<UINT>(std::ceil(result.dipHeight * scale));
        if (!result.width || !result.height || result.width > 8192 || result.height > 2048) throw E_INVALIDARG;
        // The XAML destination must span exactly the raster's physical pixels.
        // Otherwise fractional display scales introduce a second resize/filter.
        result.dipWidth = result.width / scale;
        result.dipHeight = result.height / scale;
        ComPtr<IDWriteGdiInterop> gdi; Check(factory->GetGdiInterop(&gdi));
        ComPtr<IDWriteBitmapRenderTarget> target; Check(gdi->CreateBitmapRenderTarget(nullptr, result.width, result.height, &target));
        Check(target->SetPixelsPerDip(static_cast<FLOAT>(scale)));
        ComPtr<IDWriteBitmapRenderTarget1> grayscale; Check(target.As(&grayscale));
        Check(grayscale->SetTextAntialiasMode(DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE));
        const HDC dc = target->GetMemoryDC(); RECT bounds{0, 0, static_cast<LONG>(result.width), static_cast<LONG>(result.height)};
        FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        GdiFlush();
        ComPtr<IDWriteRenderingParams> params; Check(factory->CreateRenderingParams(&params));
        ComPtr<TextRenderer> renderer; renderer.Attach(new TextRenderer(target.Get(), params.Get()));
        Check(layout->Draw(nullptr, renderer.Get(), x, y));
        GdiFlush();
        DIBSECTION dib{};
        if (GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(dib), &dib) != sizeof(dib) || !dib.dsBm.bmBits || dib.dsBm.bmBitsPixel != 32) throw E_FAIL;
        result.pixels.resize(static_cast<size_t>(result.width) * result.height);
        const UINT opacity = argb >> 24, r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
        for (UINT row = 0; row < result.height; ++row) {
            // GetMemoryDC guarantees a top-down DIB. GetObject's height sign
            // does not reliably retain that orientation on current Windows.
            const auto* source = reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(dib.dsBm.bmBits) + static_cast<size_t>(row) * dib.dsBm.bmWidthBytes);
            for (UINT col = 0; col < result.width; ++col) {
                const UINT value = source[col], coverage = std::max({value & 255, (value >> 8) & 255, (value >> 16) & 255});
                const UINT alpha = (coverage * opacity + 127) / 255;
                result.pixels[static_cast<size_t>(row) * result.width + col] = (alpha << 24) | ((r * alpha / 255) << 16) | ((g * alpha / 255) << 8) | (b * alpha / 255);
            }
        }
        return result;
    }
};
ClockFontRenderer::ClockFontRenderer(const std::wstring& path) : impl(std::make_unique<Impl>(path)) {}
ClockFontRenderer::~ClockFontRenderer() = default;
ClockPixels ClockFontRenderer::Render(const std::wstring& text, double size, double scale, uint32_t argb,
    const std::wstring& referenceFamily, UINT referenceWeight) { return impl->Render(text, size, scale, argb, referenceFamily, referenceWeight); }
std::wstring ClockFontRenderer::Family() const { return impl->family; }
}
