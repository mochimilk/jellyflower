// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "menu_surface.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <vector>

namespace acrylic {
// DirectWrite lays out and rasterizes text directly into premultiplied alpha.
// Grayscale is required for glass: RGB ClearType assumes an opaque backdrop.
class MenuText {
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1Factory> draw_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> format_;
    struct CachedLayout {std::wstring text;float width,height;UINT flags;ComPtr<IDWriteTextLayout> value;};
    mutable std::vector<CachedLayout> layouts_;
    bool Target() {
        if(target_) return true;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(!draw_ || FAILED(draw_->CreateDCRenderTarget(&properties,&target_))) return false;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if(FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0,0),&brush_))) {target_.Reset();return false;}
        return true;
    }
    ComPtr<IDWriteTextLayout> Layout(const std::wstring& text, float width, float height, UINT flags) const {
        flags&=DT_RIGHT|DT_VCENTER;
        for(const auto& cached:layouts_)if(cached.width==width && cached.height==height && cached.flags==flags && cached.text==text)return cached.value;
        ComPtr<IDWriteTextLayout> layout;
        if(!format_ || FAILED(write_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),
            format_.Get(),width,height,&layout))) return {};
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(flags&DT_RIGHT?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(flags&DT_VCENTER?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(layouts_.size()>=64)layouts_.clear();
        layouts_.push_back({text,width,height,flags,layout});
        return layout;
    }
public:
    bool Initialize(HFONT font) {
        layouts_.clear();
        LOGFONTW native{};
        if(!GetObjectW(font,sizeof(native),&native)) return false;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_.GetAddressOf()))) ||
            FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,draw_.GetAddressOf()))) return false;
        ComPtr<IDWriteGdiInterop> interop;ComPtr<IDWriteFont> mapped;
        if(FAILED(write_->GetGdiInterop(&interop)) || FAILED(interop->CreateFontFromLOGFONT(&native,&mapped))) return false;
        // A negative LOGFONT height specifies the em. A positive one includes
        // internal leading, which must be removed before creating the format.
        float size=static_cast<float>(-native.lfHeight);
        if(native.lfHeight>=0) {
            HDC dc=CreateCompatibleDC(nullptr);if(!dc) return false;
            const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};
            const bool measured=GetTextMetricsW(dc,&metrics)!=FALSE;
            SelectObject(dc,old);DeleteDC(dc);
            if(!measured) return false;
            size=static_cast<float>(metrics.tmHeight-metrics.tmInternalLeading);
        }
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);
        if(size<=0 || FAILED(write_->CreateTextFormat(native.lfFaceName,nullptr,mapped->GetWeight(),
            mapped->GetStyle(),mapped->GetStretch(),size,locale,&format_))) return false;
        return Target();
    }
    IDWriteTextFormat* Format() const {return format_.Get();}
    int Width(const std::wstring& text) const {
        const auto layout=Layout(text,100000,100000,0);DWRITE_TEXT_METRICS metrics{};
        return layout && SUCCEEDED(layout->GetMetrics(&metrics))?static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace)):0;
    }
    int InkCenter(const std::wstring& text, const RECT& bounds) const {
        const auto height=static_cast<float>(bounds.bottom-bounds.top);
        const auto layout=Layout(text,static_cast<float>(bounds.right-bounds.left),height,DT_VCENTER);
        DWRITE_OVERHANG_METRICS ink{};
        if(layout && SUCCEEDED(layout->GetOverhangMetrics(&ink)))
            return bounds.top+static_cast<int>(std::lround((height-ink.top+ink.bottom)/2));
        return (bounds.top+bounds.bottom)/2;
    }
    bool Draw(HDC destination, const RECT& bounds, const std::wstring& text, COLORREF color, UINT flags) {
        const LONG width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        if(width<=0 || height<=0 || text.empty()) return true;
        const auto layout=Layout(text,static_cast<float>(width),static_cast<float>(height),flags);
        if(!layout || !Target()) return false;
        const RECT local{0,0,width,height};MenuSurface layer(destination,local,false);
        if(!layer.pixels || FAILED(target_->BindDC(layer.Dc(),&local))) return false;
        brush_->SetColor(D2D1::ColorF(GetRValue(color)/255.0f,GetGValue(color)/255.0f,GetBValue(color)/255.0f));
        target_->BeginDraw();target_->Clear(D2D1::ColorF(0,0));
        target_->DrawTextLayout(D2D1::Point2F(),layout.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        const auto result=target_->EndDraw();
        if(FAILED(result)) {
            if(result==D2DERR_RECREATE_TARGET) {brush_.Reset();target_.Reset();}
            return false;
        }
        const BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool copied=AlphaBlend(destination,bounds.left,bounds.top,width,height,layer.Dc(),0,0,width,height,blend)!=FALSE;
        GdiFlush();return copied;
    }
};
}
