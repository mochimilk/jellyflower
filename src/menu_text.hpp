// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "menu_surface.hpp"
#include "menu_style.hpp"
#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <cmath>

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
    float scale_ = 1;
    bool variable_ = false;
    bool Target() {
        if(target_) return true;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96*scale_,96*scale_);
        if(!draw_ || FAILED(draw_->CreateDCRenderTarget(&properties,&target_))) return false;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if(FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0,0),&brush_))) {target_.Reset();return false;}
        return true;
    }
    ComPtr<IDWriteTextLayout> Layout(const std::wstring& text, float width, float height, UINT flags) const {
        ComPtr<IDWriteTextLayout> layout;
        if(!format_ || FAILED(write_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),
            format_.Get(),width,height,&layout))) return {};
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(flags&DT_RIGHT?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(flags&DT_VCENTER?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        return layout;
    }
public:
    bool Initialize(UINT dpi, bool allowVariable = true) {
        scale_=dpi/96.0f;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(write_.GetAddressOf()))) ||
            FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,draw_.GetAddressOf()))) return false;
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);
        ComPtr<IDWriteFontCollection> collection;
        UINT32 index=0;BOOL exists=FALSE;
        // Typographic families expose Segoe UI Variable as a genuine variable
        // family, unlike GDI's separate Small/Text/Display aliases.
        ComPtr<IDWriteFactory6> modern;ComPtr<IDWriteFontCollection2> families;
        if(allowVariable && SUCCEEDED(write_.As(&modern)) &&
            SUCCEEDED(modern->GetSystemFontCollection(FALSE,DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC,&families)) &&
            SUCCEEDED(families->FindFamilyName(L"Segoe UI Variable",&index,&exists)) && exists) {
            ComPtr<IDWriteTextFormat3> variable;
            const DWRITE_FONT_AXIS_VALUE weight{DWRITE_FONT_AXIS_TAG_WEIGHT,static_cast<float>(BodyTextStyle::Weight)};
            if(SUCCEEDED(modern->CreateTextFormat(L"Segoe UI Variable",families.Get(),&weight,1,BodyTextStyle::Size,locale,&variable)) &&
                SUCCEEDED(variable->SetAutomaticFontAxes(DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE))) {
                format_=variable;collection=families;variable_=true;
            }
        }
        // Windows 10 or systems without the variable face keep the same Body
        // role with the installed Segoe UI family. No font download is needed.
        if(!format_) {
            if(FAILED(write_->GetSystemFontCollection(&collection))) return false;
            exists=FALSE;
            if(FAILED(collection->FindFamilyName(L"Segoe UI",&index,&exists)) || !exists ||
                FAILED(write_->CreateTextFormat(L"Segoe UI",collection.Get(),static_cast<DWRITE_FONT_WEIGHT>(BodyTextStyle::Weight),
                    DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,BodyTextStyle::Size,locale,&format_))) return false;
        }
        ComPtr<IDWriteFontFamily> family;ComPtr<IDWriteFont> font;ComPtr<IDWriteFontFace> face;
        if(FAILED(collection->GetFontFamily(index,&family)) || FAILED(family->GetFirstMatchingFont(
            static_cast<DWRITE_FONT_WEIGHT>(BodyTextStyle::Weight),DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,&font)) ||
            FAILED(font->CreateFontFace(&face))) return false;
        DWRITE_FONT_METRICS metrics{};face->GetMetrics(&metrics);
        const float em=BodyTextStyle::Size/metrics.designUnitsPerEm;
        const float baseline=em*metrics.ascent+(BodyTextStyle::LineHeight-em*(metrics.ascent+metrics.descent))/2;
        if(FAILED(format_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,BodyTextStyle::LineHeight,baseline))) return false;
        return Target();
    }
    IDWriteTextFormat* Format() const {return format_.Get();}
    bool VariableFont() const {return variable_;}
    int Width(const std::wstring& text) const {
        const auto layout=Layout(text,100000,100000,0);DWRITE_TEXT_METRICS metrics{};
        return layout && SUCCEEDED(layout->GetMetrics(&metrics))?static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace*scale_)):0;
    }
    int InkCenter(const std::wstring& text, const RECT& bounds) const {
        const auto height=static_cast<float>(bounds.bottom-bounds.top)/scale_;
        const auto layout=Layout(text,static_cast<float>(bounds.right-bounds.left)/scale_,height,DT_VCENTER);
        DWRITE_OVERHANG_METRICS ink{};
        if(layout && SUCCEEDED(layout->GetOverhangMetrics(&ink)))
            return bounds.top+static_cast<int>(std::lround((height-ink.top+ink.bottom)*scale_/2));
        return (bounds.top+bounds.bottom)/2;
    }
    bool Draw(HDC destination, const RECT& bounds, const std::wstring& text, COLORREF color, UINT flags) {
        const LONG width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        if(width<=0 || height<=0 || text.empty()) return true;
        const auto layout=Layout(text,width/scale_,height/scale_,flags);
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
