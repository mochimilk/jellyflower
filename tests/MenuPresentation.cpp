// SPDX-License-Identifier: GPL-3.0-or-later
// Native presentation checks on Windows 11 build 22621+; does not change desktop settings.
#include "../src/popup.cpp"
#include "../src/cursor_icon.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace acrylic;
namespace {
bool light = false;
AutoThemeStatus automatic = AutoThemeStatus::Manual;
UpdateStatus state = UpdateStatus::Idle;
bool dateEnabled = true;
WallpaperStatus wallpaperState = WallpaperStatus::Off;
HRESULT wallpaperError=E_FAIL;
UINT cursorMask = 1u << CursorDisabled, cursorChoice = CursorDisabled, settings = 0;
UINT githubOpens=0;
std::vector<UINT> cursorPreviews;
COLORREF behind = RGB(220,30,30);
void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
BOOL WINAPI AlternateMenuMetrics(UINT action, UINT size, PVOID output, UINT, UINT) {
    if(action!=SPI_GETNONCLIENTMETRICS || size!=sizeof(NONCLIENTMETRICSW)) return FALSE;
    auto& metrics=*static_cast<NONCLIENTMETRICSW*>(output);
    wcscpy_s(metrics.lfMenuFont.lfFaceName,L"Tahoma");
    metrics.lfMenuFont.lfCharSet=DEFAULT_CHARSET;
    metrics.lfMenuFont.lfHeight=-18;metrics.lfMenuFont.lfWeight=FW_BOLD;
    return TRUE;
}
BOOL WINAPI MissingMenuMetrics(UINT, UINT, PVOID, UINT, UINT) { return FALSE; }
void FontSourceChecks() {
    const auto font=CreateSystemMenuFont(144,AlternateMenuMetrics);Check(font!=nullptr,"queried alternate menu font created");
    LOGFONTW selected{};GetObjectW(font,sizeof(selected),&selected);DeleteObject(font);
    Check(wcscmp(selected.lfFaceName,L"Tahoma")==0 && selected.lfHeight==-18 && selected.lfWeight==FW_BOLD,
        "menu inherits the queried family, size and weight without overrides");
    LOGFONTW stock{};Check(GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(stock),&stock)!=0,"stock GUI font available");
    const auto fallback=CreateSystemMenuFont(96,MissingMenuMetrics);Check(fallback!=nullptr,"failed metrics query falls back to Windows GUI font");
    GetObjectW(fallback,sizeof(selected),&selected);DeleteObject(fallback);
    Check(wcscmp(selected.lfFaceName,stock.lfFaceName)==0 && selected.lfHeight==stock.lfHeight && selected.lfWeight==stock.lfWeight,
        "fallback family, size and weight also come from Windows");
}
void NativeTextChecks(Popup& popup) {
    const auto format=popup.menuText.Format();Check(format!=nullptr,"native DirectWrite text format initialized");
    LOGFONTW native{};GetObjectW(popup.font,sizeof(native),&native);
    Check(format->GetFontWeight()==native.lfWeight && std::abs(format->GetFontSize()+native.lfHeight)<.01f,
        "DirectWrite preserves the inherited size and weight at the actual display DPI");
    const RECT bounds{0,0,popup.Px(240),popup.Px(40)};
    for(const auto color:{RGB(0,0,0),RGB(255,255,255),RGB(100,100,100)}) {
        MenuSurface surface(nullptr,bounds,false);Check(surface.pixels!=nullptr,"transparent text fixture bitmap");
        Check(popup.menuText.Draw(surface.Dc(),bounds,L"system acrylic date",color,DT_SINGLELINE|DT_VCENTER),"DirectWrite draws directly to alpha pixels");
        size_t empty=0,edges=0,solid=0;int top=bounds.bottom,bottom=0;
        for(LONG y=0;y<bounds.bottom;++y) for(LONG x=0;x<bounds.right;++x) {
            const DWORD pixel=surface.pixels[static_cast<size_t>(y)*bounds.right+x],alpha=pixel>>24;
            empty+=alpha==0;edges+=alpha>0 && alpha<255;solid+=alpha==255;
            Check((pixel&255)<=alpha && ((pixel>>8)&255)<=alpha && ((pixel>>16)&255)<=alpha,"DirectWrite pixels have valid premultiplied alpha");
            if(alpha>20) {top=std::min<int>(top,y);bottom=std::max<int>(bottom,y);}
        }
        Check(empty>static_cast<size_t>(bounds.right*bounds.bottom)/2 && edges>20 && solid>5,"native text retains crisp opaque stems, smoothed edges and transparent background");
        const int center=popup.menuText.InkCenter(L"system acrylic date",bounds);
        const double pixelCenter=(top+bottom+1)/2.0; // Pixel centers sit at n+0.5.
        if(std::abs(center-pixelCenter)>1) std::cout<<"Native ink center="<<center<<" pixel top="<<top<<" bottom="<<bottom<<'\n';
        Check(std::abs(center-pixelCenter)<=1,"icon alignment follows the native glyph ink rather than GDI metrics");
    }
}
double Contrast(COLORREF a,COLORREF b) {
    auto luminance=[](COLORREF color){
        auto channel=[](BYTE value){const double c=value/255.0;return c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);};
        return .2126*channel(GetRValue(color))+.7152*channel(GetGValue(color))+.0722*channel(GetBValue(color));
    };
    const auto first=luminance(a),second=luminance(b);return (std::max(first,second)+.05)/(std::min(first,second)+.05);
}
void ExitTextChecks(Popup& popup) {
    const auto& row=popup.rows[3];
    const COLORREF ink=popup.LabelInk(row,false);
    Check(GetRValue(ink)>GetGValue(ink)+25 && GetRValue(ink)>GetBValue(ink)+25,"exit uses a distinct red in each theme");
    LOGBRUSH base{},hover{};GetObjectW(popup.background,sizeof(base),&base);GetObjectW(popup.highlight,sizeof(hover),&hover);
    Check(Contrast(ink,base.lbColor)>=4.5 && Contrast(ink,hover.lbColor)>=4.5,"exit red has readable contrast on base and hover surfaces");
    RECT bounds{};GetClientRect(popup.window,&bounds);
    for(int interaction=0;interaction<3;++interaction) {
        popup.hovered=interaction==1?3:-1;popup.keyboard=interaction==2;
        if(interaction==2) SetFocus(popup.buttons[3]);
        MenuSurface surface(nullptr,bounds,false);popup.Paint(surface.Dc());GdiFlush();
        RECT exit{};GetWindowRect(popup.buttons[3],&exit);MapWindowPoints(nullptr,popup.window,reinterpret_cast<POINT*>(&exit),2);
        size_t red=0;
        for(LONG y=exit.top;y<exit.bottom;++y) for(LONG x=exit.left+popup.Px(34);x<exit.right-popup.Px(24);++x) {
            const DWORD pixel=surface.pixels[static_cast<size_t>(y)*bounds.right+x];
            const auto r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
            if((pixel>>24)>=240 && r>g+25 && r>b+25) ++red;
        }
        Check(red>5,"production exit label paints red when idle, hovered and keyboard focused");
    }
    popup.highContrast=true;popup.highlightedText=RGB(25,235,70);
    Check(popup.LabelInk(row,false)==popup.text && popup.LabelInk(row,true)==popup.highlightedText,"contrast themes retain Windows normal and highlight text colors");
    popup.highContrast=false;popup.UpdateTheme(light);popup.hovered=-1;popup.keyboard=false;
}
void GithubChecks(Popup& popup) {
    Check(popup.rows[1].label==L"github" && std::wstring(GithubUrl)==L"https://github.com/mochimilk/jellyflower","github row opens the project's fixed HTTPS destination");
    RECT bounds{};GetClientRect(popup.window,&bounds);
    RECT row{};GetWindowRect(popup.buttons[1],&row);MapWindowPoints(nullptr,popup.window,reinterpret_cast<POINT*>(&row),2);
    auto pixels=[&] {
        MenuSurface surface(nullptr,bounds,false);popup.Paint(surface.Dc());GdiFlush();std::vector<DWORD> region;
        for(LONG y=row.top+popup.Px(8);y<row.bottom-popup.Px(8);++y)
            for(LONG x=row.right-popup.Px(28);x<row.right-popup.Px(12);++x)
                region.push_back(surface.pixels[static_cast<size_t>(y)*bounds.right+x]);
        return region;
    };
    auto coverage=[&] {
        const auto with=pixels();HICON saved[]{popup.externalIcon.icons[0],popup.externalIcon.icons[1]};
        popup.externalIcon.icons[0]=popup.externalIcon.icons[1]=nullptr;
        const auto without=pixels();std::copy(std::begin(saved),std::end(saved),popup.externalIcon.icons);size_t count=0;
        for(size_t i=0;i<with.size();++i) count+=with[i]!=without[i];
        return count;
    };
    popup.keyboard=false;popup.hovered=-1;Check(coverage()==0,"external arrow is hidden at rest");
    popup.hovered=1;Check(coverage()>10,"external arrow paints at the right on hover");
    popup.hovered=-1;popup.keyboard=true;SetFocus(popup.buttons[1]);Check(coverage()>10,"external arrow also appears for keyboard focus");
    popup.keyboard=false;
}
std::vector<DWORD> IconPixels(HICON icon) {
    ICONINFO source{};Check(icon && GetIconInfo(icon,&source),"indicator icon is readable");
    BITMAP bitmap{};Check(GetObjectW(source.hbmColor,sizeof(bitmap),&bitmap)!=0,"indicator has color surface");
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),bitmap.bmWidth,-bitmap.bmHeight,1,32,BI_RGB};
    std::vector<DWORD> pixels(static_cast<size_t>(bitmap.bmWidth)*bitmap.bmHeight);
    const HDC dc=GetDC(nullptr);
    const int read=GetDIBits(dc,source.hbmColor,0,bitmap.bmHeight,pixels.data(),&info,DIB_RGB_COLORS);
    ReleaseDC(nullptr,dc);DeleteObject(source.hbmColor);DeleteObject(source.hbmMask);
    Check(read==bitmap.bmHeight,"indicator pixels read completely");return pixels;
}
void IndicatorChecks(Popup& popup) {
    auto verify=[&](const MenuIndicator& glyph,int resource,COLORREF normal,COLORREF hover) {
        const auto original=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(resource),IMAGE_ICON,popup.Px(MenuGlyphSize),popup.Px(MenuGlyphSize),0));
        const auto mask=IconPixels(original);DestroyIcon(original);
        for(int highlighted=0;highlighted<2;++highlighted) {
            const COLORREF ink=highlighted?hover:normal;const auto pixels=IconPixels(glyph.Get(highlighted!=0));
            Check(pixels.size()==mask.size(),"indicators share the menu glyph canvas at this DPI");size_t visible=0;
            for(size_t i=0;i<pixels.size();++i) {
                const DWORD alpha=pixels[i]>>24;
                Check(alpha==mask[i]>>24,"tinting preserves every supplied SVG coverage sample");visible+=alpha>0;
                Check(((pixels[i]>>16)&255)==(GetRValue(ink)*alpha+127)/255
                    && ((pixels[i]>>8)&255)==(GetGValue(ink)*alpha+127)/255
                    && (pixels[i]&255)==(GetBValue(ink)*alpha+127)/255,"indicator uses the expected premultiplied ink");
            }
            Check(visible>10,"indicator contains visible artwork");
        }
    };
    auto all=[&] {
        verify(popup.checkIcon,IDI_CHECK_LIGHT,popup.text,popup.highlightedText);
        verify(popup.chevronIcon,IDI_CHEVRON_RIGHT_LIGHT,popup.SecondaryInk(false),popup.SecondaryInk(true));
        if(popup.kind==TrayGeneralCommand) verify(popup.externalIcon,IDI_EXTERNAL_LIGHT,popup.SecondaryInk(false),popup.SecondaryInk(true));
    };
    Check(popup.SecondaryInk(false)==(light?RGB(100,100,100):RGB(170,170,170)),"arrows and status labels share secondary ink");all();
    popup.highContrast=true;popup.text=RGB(20,230,80);popup.highlightedText=RGB(240,30,180);
    Check(popup.UpdateIndicators(),"indicator contrast palette refresh");
    Check(popup.SecondaryInk(false)==popup.text && popup.SecondaryInk(true)==popup.highlightedText,"contrast themes use Windows normal and highlight colors");all();
    Check(popup.UpdateTheme(light),"restore fixture menu palette");
}
void AutoThemeChecks(Popup& popup) {
    const bool saved=light;light=false;automatic=AutoThemeStatus::Manual;popup.UpdateTheme(light);
    const HWND same=popup.window;popup.Activate(0,false);
    Check(automatic==AutoThemeStatus::Locating && !light,"selecting auto keeps the current theme while location is pending");
    Check(popup.rows[0].secondary==L"auto" && popup.window==same && !popup.done,"auto appears immediately without closing the menu");
    for(const auto status:{AutoThemeStatus::Locating,AutoThemeStatus::Automatic}) for(const bool theme:{false,true}) {
        light=theme;automatic=status;Check(popup.UpdateTheme(light),"automatic theme presentation refresh");
        const auto expected=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(light?IDI_SUNSET_LIGHT:IDI_SUNSET_DARK),IMAGE_ICON,popup.Px(MenuGlyphSize),popup.Px(MenuGlyphSize),0));
        Check(IconPixels(popup.themeIcon)==IconPixels(expected),"pending and active auto use the supplied sunset in both themes");DestroyIcon(expected);
        wchar_t name[128]{};GetWindowTextW(popup.buttons[0],name,128);
        Check(popup.rows[0].secondary==L"auto" && std::wstring(name)==L"system, auto","visible and accessible labels both say auto");
    }
    for(const auto& entry:std::initializer_list<std::pair<AutoThemeStatus,const wchar_t*>>{{AutoThemeStatus::Denied,L"location off"},{AutoThemeStatus::Unavailable,L"no location"},{AutoThemeStatus::Error,L"try again"}}) {
        automatic=entry.first;popup.UpdateTheme(light);Check(popup.rows[0].secondary==entry.second,"actionable location failures remain visible");
    }
    automatic=AutoThemeStatus::Manual;light=saved;popup.UpdateTheme(light);
}
void SubmenuToggleChecks(Popup& popup,int index) {
    auto click=[&]{SendMessageW(popup.window,WM_COMMAND,MAKEWPARAM(popup.rows[index].command,BN_CLICKED),reinterpret_cast<LPARAM>(popup.buttons[index]));};
    popup.Hover(index);popup.OpenChild(index,false);const auto branch=popup.child->window;
    click();Check(!IsWindowVisible(branch) && !popup.done,"clicking an open parent row collapses only its branch");
    popup.Hover(index);popup.Hover(index);
    SendMessageW(popup.window,WM_TIMER,HoverTimer,0);
    Check(!IsWindowVisible(branch),"same-row movement and queued hover timers do not reopen a clicked-closed branch");
    popup.Hover(-1);popup.Hover(index);SendMessageW(popup.window,WM_TIMER,HoverTimer,0);
    Check(IsWindowVisible(branch),"leaving and hovering back restores normal submenu entry");
    click();click();Check(IsWindowVisible(branch),"a second explicit click can reopen without leaving the row");
    click();popup.Focus(index);popup.Key(VK_RIGHT);
    Check(IsWindowVisible(branch) && popup.child->keyboard,"keyboard right can reopen a clicked-closed branch");
}
LRESULT CALLBACK Owner(HWND window, UINT message, WPARAM parameter, LPARAM other) {
    if (message == TrayWallpaperQueryMessage) return parameter?wallpaperError:static_cast<LRESULT>(wallpaperState);
    if (message == TraySettingMessage && parameter == TrayWallpaperCommand) {
        wallpaperState = wallpaperState == WallpaperStatus::On ? WallpaperStatus::Restoring : WallpaperStatus::Applying;
        return static_cast<UINT>(wallpaperState);
    }
    if(message==TraySettingMessage && parameter==TrayGithubCommand) {++githubOpens;return 1;}
    if (message == TrayThemeQueryMessage) return parameter ? static_cast<UINT>(automatic) : light;
    if (message == TraySettingMessage && parameter == TrayThemeCommand) {
        if (automatic==AutoThemeStatus::Manual && !light) automatic=AutoThemeStatus::Locating;
        else {light=automatic!=AutoThemeStatus::Manual || !light;automatic=AutoThemeStatus::Manual;}
        return light;
    }
    if (message == TrayDateQueryMessage) return dateEnabled;
    if (message == TraySettingMessage && parameter == TrayDateCommand) { dateEnabled = !dateEnabled; return dateEnabled; }
    if (message == TrayUpdateQueryMessage) return static_cast<UINT>(state);
    if (message == TrayCursorQueryMessage) return parameter ? cursorChoice : cursorMask;
    if (message == TrayCursorPreviewMessage) { cursorPreviews.push_back(static_cast<UINT>(parameter)); return parameter==CursorPreviewNone?0:reinterpret_cast<LRESULT>(CopyImage(LoadCursorW(nullptr,IDC_ARROW),IMAGE_CURSOR,0,0,0)); }
    if (message == TrayCursorIconMessage) {
        wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
        return reinterpret_cast<LRESULT>(CreateCursorMenuIcon((std::wstring(windows)+L"\\Cursors\\aero_arrow.cur").c_str(),static_cast<int>(parameter)));
    }
    if (message == TraySettingMessage && parameter == TrayUpdateCommand) { state = UpdateStatus::Checking; return static_cast<UINT>(state); }
    if (message == TraySettingMessage) {
        ++settings;
        if (parameter >= CursorCommandBase && parameter <= CursorCommandBase + CursorDisabled) cursorChoice = static_cast<UINT>(parameter) - CursorCommandBase;
        return cursorChoice;
    }
    return DefWindowProcW(window,message,parameter,other);
}
void PartialPaintChecks(Popup& popup) {
    RECT bounds{};GetClientRect(popup.window,&bounds);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),bounds.right,-bounds.bottom,1,32};
    HDC dc[2]{CreateCompatibleDC(nullptr),CreateCompatibleDC(nullptr)};void* bits[2]{};HBITMAP bitmap[2]{};HGDIOBJ old[2]{};
    for(int i=0;i<2;++i){bitmap[i]=CreateDIBSection(dc[i],&info,DIB_RGB_COLORS,&bits[i],nullptr,0);old[i]=SelectObject(dc[i],bitmap[i]);}
    popup.Hover(1);popup.Paint(dc[0]);ValidateRect(popup.window,nullptr);popup.Hover(2);
    RECT dirty{};Check(GetUpdateRect(popup.window,&dirty,FALSE) && dirty.bottom-dirty.top<=popup.rowHeight*2+1,"hover invalidates only the old and new row bounds");
    IntersectClipRect(dc[0],dirty.left,dirty.top,dirty.right,dirty.bottom);popup.Paint(dc[0]);SelectClipRgn(dc[0],nullptr);popup.Paint(dc[1]);GdiFlush();
    Check(memcmp(bits[0],bits[1],static_cast<size_t>(bounds.right)*bounds.bottom*4)==0,"partial glass redraw is pixel-identical to a complete menu redraw");
    LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);double elapsed[2]{};
    for(int clipped=0;clipped<2;++clipped){if(clipped)IntersectClipRect(dc[0],dirty.left,dirty.top,dirty.right,dirty.bottom);
        QueryPerformanceCounter(&start);for(int n=0;n<40;++n)popup.Paint(dc[0]);GdiFlush();QueryPerformanceCounter(&end);
        elapsed[clipped]=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart/40;SelectClipRgn(dc[0],nullptr);}
    std::cout<<"Menu paint dpi="<<popup.dpi<<" full="<<elapsed[0]<<"ms hover="<<elapsed[1]<<"ms\n";
    popup.Hover(-1);popup.ClearPreviewCursors();ValidateRect(popup.window,nullptr);
    for(int i=0;i<2;++i){SelectObject(dc[i],old[i]);DeleteObject(bitmap[i]);DeleteDC(dc[i]);}
}
void Capture(Popup& popup, const std::filesystem::path& path) {
    RECT bounds{}; GetClientRect(popup.window,&bounds);
    BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),bounds.right,-bounds.bottom,1,32};
    HDC dc=CreateCompatibleDC(nullptr); void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    const auto old=SelectObject(dc,bitmap); popup.Paint(dc);
    Check(popup.textFallbacks==0,"production menu uses DirectWrite without falling back to GDI");
    GdiFlush();
    if (popup.acrylic) {
        size_t empty=0,solid=0,edges=0;
        const size_t count=static_cast<size_t>(bounds.right)*bounds.bottom;
        auto* rgba=static_cast<DWORD*>(pixels);
        for(size_t i=0;i<count;++i) {
            const DWORD pixel=rgba[i],alpha=pixel>>24;
            empty += alpha==0; solid += alpha==255; edges += alpha>0 && alpha<255;
            Check((pixel&255)<=alpha && ((pixel>>8)&255)<=alpha && ((pixel>>16)&255)<=alpha,"glass content is valid premultiplied alpha");
            // A themed backdrop here is only a presentation preview; Windows
            // supplies the actual desktop acrylic behind these pixels.
            const DWORD base=popup.light?249u:32u;
            DWORD composed=0;
            for(int shift : {0,8,16}) composed |= std::min<DWORD>(255,((pixel>>shift)&255)+base*(255-alpha)/255)<<shift;
            rgba[i]=composed;
        }
        // In the general menu the focus pill makes one label opaque;
        // its remaining exit text supplies fewer translucent edge pixels.
        Check(empty>count/2 && solid>100 && edges>(popup.kind==TrayGeneralCommand?20u:100u),
            "background reveals acrylic while text/icons retain solid and antialiased coverage");
    }
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
    const DWORD bytes=bounds.right*bounds.bottom*4;header.bfSize=header.bfOffBits+bytes;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));
    file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(static_cast<char*>(pixels),bytes);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
LRESULT CALLBACK Background(HWND window,UINT message,WPARAM parameter,LPARAM other) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
        const auto brush=CreateSolidBrush(behind);FillRect(dc,&bounds,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,parameter,other);
}
void Pump() {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}DwmFlush();}
void Compositor(HWND controller,const std::filesystem::path& destination) {
    WNDCLASSW wc{};wc.lpfnWndProc=Background;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"jellyflower.backdrop.fixture";
    Check(RegisterClassW(&wc)!=0,"background fixture class");
    // Keep the controlled backdrop visible above unrelated desktop windows without taking focus.
    const HWND background=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"jellyflower acrylic test",WS_POPUP,100,100,500,500,nullptr,nullptr,wc.hInstance,nullptr);
    Check(background!=nullptr,"background fixture created");
    struct Close {HWND window;~Close(){DestroyWindow(window);}} close{background};
    ShowWindow(background,SW_SHOWNOACTIVATE);UpdateWindow(background);
    for(int theme=0;theme<2;++theme) {
        light=theme!=0;state=UpdateStatus::Idle;cursorMask=1u<<CursorDisabled;
        Popup popup;popup.controller=controller;popup.dpi=96;popup.systemLight=light;popup.rows=RootRows(AcrylicMode::System,true,light,cursorMask);
        Check(popup.Create(background,{180,420},false),"composited popup");
        popup.closing=true; // The fixture never takes focus away from the user's app.
        ShowWindow(popup.window,SW_SHOWNOACTIVATE);
        RECT bounds{};GetWindowRect(popup.window,&bounds);
        COLORREF samples[2]{};
        for(int color=0;color<2;++color) {
            behind=color?RGB(30,30,220):RGB(220,30,30);
            RedrawWindow(background,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
            RedrawWindow(popup.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);Pump();
            Sleep(400);Pump();
            HDC screen=GetDC(nullptr);
            Check(GetPixel(screen,550,550)==behind,"controlled background is visible rather than covered by another app");
            samples[color]=GetPixel(screen,bounds.left+150,bounds.top+14);
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),bounds.right-bounds.left,-(bounds.bottom-bounds.top),1,32};
            HDC dc=CreateCompatibleDC(screen);void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto old=SelectObject(dc,bitmap);
            Check(BitBlt(dc,0,0,info.bmiHeader.biWidth,-info.bmiHeader.biHeight,screen,bounds.left,bounds.top,SRCCOPY)!=0,"capture only the test menu bounds");GdiFlush();
            BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);const DWORD bytes=info.bmiHeader.biWidth*(-info.bmiHeader.biHeight)*4;header.bfSize=header.bfOffBits+bytes;
            std::ofstream file(destination/(std::wstring(L"compositor-")+(light?L"light-":L"dark-")+(color?L"blue.bmp":L"red.bmp")),std::ios::binary);
            file.write(reinterpret_cast<char*>(&header),sizeof(header));file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));file.write(static_cast<char*>(pixels),bytes);
            SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        }
        std::cout<<"Compositor "<<(light?"light":"dark")<<": red="<<std::hex<<samples[0]<<" blue="<<samples[1]<<std::dec<<'\n';
        Check(samples[0]!=CLR_INVALID && samples[1]!=CLR_INVALID && std::abs(static_cast<int>(GetRValue(samples[0]))-static_cast<int>(GetRValue(samples[1])))>8
            && std::abs(static_cast<int>(GetBValue(samples[0]))-static_cast<int>(GetBValue(samples[1])))>8,"actual menu pixels reveal changed desktop background in both themes");
    }
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2 || argc==3,"output directory argument");SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::filesystem::create_directories(argv[1]);
        FontSourceChecks();
        WNDCLASSW owner{};owner.lpfnWndProc=Owner;owner.hInstance=GetModuleHandleW(nullptr);owner.lpszClassName=L"jellyflower.fixture";
        Check(RegisterClassW(&owner)!=0,"fixture class");
        const HWND controller=CreateWindowW(owner.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,owner.hInstance,nullptr);
        WNDCLASSW wc{};wc.lpfnWndProc=PopupProc;wc.hInstance=owner.hInstance;wc.lpszClassName=PopupClass;
        Check(RegisterClassW(&wc)!=0,"production popup class");
        for (int theme=0;theme<2;++theme) for (UINT dpi : {96u,120u,144u,168u,192u}) {
            light=theme!=0;state=UpdateStatus::Idle;
            Popup popup;popup.controller=controller;popup.dpi=dpi;popup.light=light;popup.systemLight=light;
            cursorMask=1u<<CursorDisabled;
            popup.rows=RootRows(AcrylicMode::System,true,light,cursorMask);
            Check(popup.Create(controller,{100,100},false),"native popup created");
            ShowWindow(popup.window,SW_SHOWNORMAL);SetForegroundWindow(popup.window);
            Check(popup.acrylic,"Windows accepts explicit acrylic on root menu");
            LOGFONTW font{};GetObjectW(popup.font,sizeof(font),&font);
            Check(font.lfQuality==ANTIALIASED_QUALITY,"Windows font uses grayscale smoothing for glass");
            NONCLIENTMETRICSW metrics{sizeof(metrics)};
            Check(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi)!=FALSE,"Windows menu font queried at display DPI");
            Check(wcscmp(font.lfFaceName,metrics.lfMenuFont.lfFaceName)==0 && font.lfHeight==metrics.lfMenuFont.lfHeight
                && font.lfWeight==metrics.lfMenuFont.lfWeight,"Windows supplies the complete menu family, size and weight");
            NativeTextChecks(popup);
            IndicatorChecks(popup);
            AutoThemeChecks(popup);
            automatic=AutoThemeStatus::Locating;popup.UpdateTheme(light);
            Capture(popup,std::filesystem::path(argv[1])/(std::wstring(light?L"auto-light-":L"auto-dark-")+std::to_wstring(dpi)+L".bmp"));
            automatic=AutoThemeStatus::Manual;popup.UpdateTheme(light);
            Check(popup.rows[0].label==L"system" && popup.rows[0].secondary==(light?L"light":L"dark"),"system first with current theme secondary");
            Check(popup.rows[3].label==L"colors" && popup.rows[3].secondary==L"unlock" && !HasSubmenu(popup.rows[3].command),"no pack shows one unlock action, no submenu");
            Check(popup.colorIcons.size()==9,"all cursor preview colors render without redistributing cursor files");
            const HWND same=popup.window;
            dateEnabled=true;
            Check(popup.rows[1].command==TrayToggleCommand && popup.rows[2].command==TrayDateCommand && !HasSubmenu(TrayDateCommand),"date directly below acrylic, no nested controls");
            Check(popup.dateIcons[0] && popup.dateIcons[1] && popup.dateIcons[0]!=popup.dateIcons[1],"both supplied calendar glyphs loaded");
            popup.Activate(2,false);
            Check(!dateEnabled && !popup.clockDate && popup.rows[2].secondary==L"off" && popup.window==same && !popup.done,"date off updates in place");
            Capture(popup,std::filesystem::path(argv[1])/(std::wstring(light?L"date-off-light-":L"date-off-dark-")+std::to_wstring(dpi)+L".bmp"));
            popup.Activate(2,false);
            Check(dateEnabled && popup.clockDate && popup.rows[2].secondary==L"on" && popup.window==same && !popup.done,"date on updates in place");
            Check(popup.rows.size()==5 && popup.rows[4].command==TrayGeneralCommand && popup.rows[4].label==L"general"
                && HasSubmenu(popup.rows[4].command) && popup.generalIcon,"general replaces root update/exit and has the supplied gear icon");
            Check(std::none_of(popup.rows.begin(),popup.rows.end(),[](const Row& row){return row.command==TrayUpdateCommand || row.command==TrayExitCommand;}),"update and exit only appear inside general");
            Capture(popup,std::filesystem::path(argv[1])/(std::wstring(light?L"root-light-":L"root-dark-")+std::to_wstring(dpi)+L".bmp"));
            popup.Activate(4,true);
            Check(popup.child && popup.child->kind==TrayGeneralCommand && popup.child->rows.size()==4,"general submenu opens with wallpaper, github, version and exit actions");
            auto& general=*popup.child;
            const HWND generalWindow=general.window;
            Check(general.rows[0].command==TrayWallpaperCommand && general.rows[0].label==L"jellyflower wallpaper" && general.wallpaperIcon,"wallpaper row has supplied themed artwork");
            const HWND wallpaperWindow=general.window;
            wallpaperState=WallpaperStatus::Off;general.RefreshWallpaper();general.Activate(0,false);
            Check(general.rows[0].secondary==L"on" && !IsWindowEnabled(general.buttons[0]) && !popup.done && general.window==wallpaperWindow,"wallpaper queues enable in place without closing the menu");
            wallpaperState=WallpaperStatus::On;general.RefreshWallpaper();
            Check(IsWindowEnabled(general.buttons[0]),"wallpaper row re-enabled on completion");
            general.Activate(0,false);Check(general.rows[0].secondary==L"off" && !popup.done,"wallpaper restore stays in the same menu");
            wallpaperState=WallpaperStatus::ErrorOn;general.RefreshWallpaper();
            Check(general.rows[0].secondary==L"error 80004005" && IsWindowEnabled(general.buttons[0]),"unknown wallpaper failure shows a code inline without requiring logs");
            RECT wallpaperBounds{};GetClientRect(general.buttons[0],&wallpaperBounds);
            for(const auto status:WallpaperErrorLabels){const auto needed=general.menuText.Width(general.rows[0].label)+general.menuText.Width(status)+general.Px(34)+2*general.Px(12);
                if(needed>wallpaperBounds.right)std::cout<<"Wallpaper fit dpi="<<dpi<<" required="<<needed<<" actual="<<wallpaperBounds.right<<'\n';
                Check(needed<=wallpaperBounds.right,"wallpaper label and all failure statuses fit at this DPI");}
            wallpaperError=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);general.RefreshWallpaper();Check(general.rows[0].secondary==L"image missing" && IsWindowEnabled(general.buttons[0]) && general.window==wallpaperWindow,"missing-image error is readable without logs and keeps retry in place");
            wallpaperError=E_ACCESSDENIED;general.RefreshWallpaper();Check(general.rows[0].secondary==L"not allowed","wallpaper permission error is readable inline");wallpaperError=E_FAIL;
            wallpaperState=WallpaperStatus::Off;general.RefreshWallpaper();general.Focus(0);
            SubmenuToggleChecks(popup,4);
            GithubChecks(general);
            IndicatorChecks(general);
            general.hovered=1;
            Capture(general,std::filesystem::path(argv[1])/(std::wstring(light?L"github-light-":L"github-dark-")+std::to_wstring(dpi)+L".bmp"));
            general.hovered=-1;
            Check(general.rows[2].command==TrayUpdateCommand && general.rows[3].command==TrayExitCommand && general.rows[3].divider && general.rows[1].command==TrayGithubCommand
                && GetFocus()==general.buttons[1],"general actions preserve the github focus set by its keyboard check");
            Check(general.packageIcon && general.packageOpenIcon && general.updateIcon && general.githubIcon && general.externalIcon,"general owns themed update artwork");
            popup.RefreshCursors();Check(IsWindowVisible(generalWindow),"no-pack cursor refresh does not close general");
            general.Activate(2,false);
            Check(state==UpdateStatus::Checking && popup.window==same && general.window==generalWindow && !popup.done,"check starts inside general while keeping both menus alive");
            for (const auto result : {UpdateStatus::Checking,UpdateStatus::Current,UpdateStatus::Available,UpdateStatus::NoRelease,UpdateStatus::Error,UpdateStatus::Downloading,UpdateStatus::Installing}) {
                state=result;popup.RefreshUpdate();
                Check(general.updateStatus==result && general.rows[2].label==std::wstring(L"v")+AppVersion,"root refresh reaches general and preserves installed version label");
                if(result==UpdateStatus::Available) Check(general.rows[2].secondary==L"download","available update presents download action");
                const int label=general.menuText.Width(general.rows[2].label),secondary=general.menuText.Width(general.rows[2].secondary);
                RECT bounds{};GetClientRect(general.buttons[2],&bounds);
                Check(label+secondary+general.Px(34+12+12)<bounds.right,"native text measurements fit without overlap");
                wchar_t accessible[256]{};GetWindowTextW(general.buttons[2],accessible,256);
                Check(std::wstring(accessible).find(UpdateStatusLabel(result))!=std::wstring::npos,"accessible general update state reflects result");
                const auto name=std::wstring(light?L"general-light-":L"general-dark-")+std::to_wstring(dpi)+L"-"+std::to_wstring(static_cast<UINT>(result))+L".bmp";
                Capture(general,std::filesystem::path(argv[1])/name);
                if(result==UpdateStatus::Checking) {
                    Check(general.spinnerBitmap && general.spinnerBitmap->GetLastStatus()==Gdiplus::Ok,"nested spinner bitmap loaded");
                    Gdiplus::Color corner;general.spinnerBitmap->GetPixel(0,0,&corner);
                    Check(corner.GetAlpha()==0,"nested spinner retains transparent corners");
                    general.updateStarted-=125;
                    Capture(general,std::filesystem::path(argv[1])/(L"spin-"+name));
                }
            }
            ExitTextChecks(general);
            general.Key(VK_LEFT);Check(!IsWindowVisible(generalWindow) && !popup.done,"left arrow closes general without dismissing root");
            state=UpdateStatus::Available;popup.RefreshUpdate();popup.Activate(4,true);
            Check(popup.child->window==generalWindow && popup.child->rows[2].secondary==L"download","reopening general preserves latest update state");
            popup.child->Key(VK_ESCAPE);Check(!IsWindowVisible(generalWindow) && !popup.done,"escape closes nested general only");
            popup.child->HideBranch();popup.child.reset();
            popup.child=std::make_unique<Popup>();popup.childIndex=3;
            auto& cursors=*popup.child;
            cursors.controller=controller;cursors.parent=&popup;cursors.kind=TrayCursorCommand;cursors.dpi=dpi;cursors.light=light;cursors.systemLight=light;
            cursorMask=1u<<CursorDisabled;cursors.rows=CursorRows(cursorMask,CursorDisabled);
            Check(cursors.Create(popup.window,{100,100},true),"cursor submenu created");
            Check(cursors.acrylic,"nested menu also requests acrylic");
            ShowWindow(cursors.window,SW_SHOWNORMAL);SetForegroundWindow(cursors.window);
            const HWND submenu=cursors.window;
            const UINT before=settings;cursors.Activate(1,false);
            Check(settings==before,"disabled cursor cannot invoke selection");
            cursorPreviews.clear(); cursors.Hover(1);Check(cursorPreviews.empty(),"disabled cursor cannot start a hover preview");
            for(size_t i=0;i<10;++i) Check(!IsWindowEnabled(cursors.buttons[i]),"custom cursor choice disabled");
            Check(IsWindowEnabled(cursors.buttons[10]) && cursors.buttons.size()==11,"disable enabled and redundant unlock row removed");
            cursors.Focus(0);Check(GetFocus()==cursors.buttons[10],"keyboard skips locked cursor choices");
            Capture(cursors,std::filesystem::path(argv[1])/(std::wstring(light?L"locked-light-":L"locked-dark-")+std::to_wstring(dpi)+L".bmp"));
            cursorMask=(1u<<(CursorDisabled+1))-1;cursors.RefreshCursors();
            popup.RefreshCursors();Check(popup.rows[3].label==L"cursor" && popup.rows[3].secondary.empty() && HasSubmenu(popup.rows[3].command),"import replaces unlock action with cursor submenu in place");
            for(size_t i=0;i<10;++i) Check(IsWindowEnabled(cursors.buttons[i]),"import enables custom choices");
            Check(cursors.window==submenu && !cursors.done && cursors.rows[10].checked,"import keeps submenu and Windows selection");
            PartialPaintChecks(cursors);cursorPreviews.clear();
            const UINT beforeHover = settings;
            cursors.Hover(2);cursors.Hover(2);
            Check(cursorPreviews.size()==1 && cursorPreviews.back()==2 && settings==beforeHover && cursorChoice==CursorDisabled && cursors.rows[10].checked,
                "hover sends one transient color preview and keeps selected checkmark");
            cursors.Hover(4);Check(cursorPreviews.size()==2 && cursorPreviews.back()==4,"moving directly to another row previews that color");
            {const auto loads=cursorPreviews.size();cursors.Hover(2);cursors.Hover(4);Check(cursorPreviews.size()==loads && cursors.previewCursor,"revisiting rows reuses native pointers without another controller/file load");}
            cursors.Hover(-1);Check(cursorPreviews.back()==CursorPreviewNone && cursorChoice==CursorDisabled,"leaving restores committed choice");
            cursors.Hover(5);cursors.Activate(5,false);cursors.Hover(-1);
            Check(cursorChoice==5 && popup.cursorMode==5 && cursors.rows[5].checked && !popup.done && IsWindowVisible(submenu),"click commits preview in place and leave retains new selection");
            cursors.Hover(6);cursors.Focus(5);Check(cursorPreviews.back()==CursorPreviewNone,"keyboard navigation ends mouse preview");
            cursors.Hover(7);cursors.HideBranch();Check(cursorPreviews.back()==CursorPreviewNone,"hiding a nested branch ends preview immediately");
            const auto hiddenPreviews=cursorPreviews.size();cursors.Hover(7);
            Check(cursorPreviews.size()==hiddenPreviews && cursors.previewMode==CursorPreviewNone,"queued row hover cannot restart a hidden submenu preview");
            ShowWindow(submenu,SW_SHOWNORMAL);cursors.closing=false;
            cursorChoice=CursorDisabled;cursors.RefreshCursors();
            Capture(cursors,std::filesystem::path(argv[1])/(std::wstring(light?L"unlocked-light-":L"unlocked-dark-")+std::to_wstring(dpi)+L".bmp"));
            Check(!popup.done,"menu still live after cursor fixture interaction");
            popup.highContrast=true;popup.ConfigureBackdrop();
            Check(!popup.acrylic,"high contrast uses a solid fallback");
            popup.highContrast=false;popup.ConfigureBackdrop();Check(popup.acrylic,"acrylic recovers after fallback");
            popup.OpenChild(3,true);Check(popup.child && popup.child->kind==TrayCursorCommand && !popup.done,"cursor replaces the previous general branch");
            SubmenuToggleChecks(popup,3);
            popup.OpenChild(4,true);Check(popup.child && popup.child->kind==TrayGeneralCommand && popup.child->rows[2].secondary==L"download" && !popup.done,"general replaces cursor and queries current update state");
            popup.child->Activate(3,true);
            Check(popup.done && popup.result==TrayExitCommand && !IsWindowVisible(popup.child->window),"nested exit returns the existing controller shutdown command and dismisses every menu");
        }
        {
            Popup preview;preview.controller=controller;preview.rows=RootRows(AcrylicMode::System,true,light,cursorMask);
            Check(preview.Create(controller,{100,100},false),"cursor dismissal fixture created");
            preview.OpenChild(3,false);preview.child->Hover(4);
            Check(cursorPreviews.back()==4,"cursor preview starts in nested menu");
            SendMessageW(preview.window,WM_CANCELMODE,0,0);
            Check(preview.done && cursorPreviews.back()==CursorPreviewNone,"outside dismissal restores preview through normal Finish path");
            const auto finishedPreviews=cursorPreviews.size();preview.child->Hover(4);
            Check(cursorPreviews.size()==finishedPreviews,"queued row hover cannot restart a dismissed menu preview");
        }
        {
            Popup link;link.controller=controller;link.kind=TrayGeneralCommand;link.rows=GeneralRows();
            Check(link.Create(controller,{100,100},false),"external link menu created");
            const auto before=githubOpens;link.Activate(1,true);
            Check(githubOpens==before+1 && link.done && !link.result,"github dispatches once and dismisses the menu after successful launch");
        }
        Check(MenuEntranceProgress(0)==0 && MenuEntranceProgress(1)==1,"entrance curve endpoints");
        Check(std::abs(MenuEntranceProgress(.125)-.5)<.000001,"bezier x inversion matches analytic control point curve");
        double last=0;for(int i=0;i<=1000;++i){const auto value=MenuEntranceProgress(i/1000.0);Check(value>=last && value<=1,"entrance curve monotonic without overshoot");last=value;}
        for(UINT dpi:{96u,120u,144u,168u,192u}) {
            MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(controller,MONITOR_DEFAULTTOPRIMARY),&monitor);
            Popup edge;edge.controller=controller;edge.dpi=dpi;edge.rows=RootRows(AcrylicMode::System,true,false,1u<<CursorDisabled);
            Check(edge.Create(controller,{monitor.rcWork.right,monitor.rcWork.bottom},false),"edge popup created");
            RECT rect{};GetWindowRect(edge.window,&rect);
            Check(rect.right==monitor.rcWork.right-edge.Px(12) && rect.bottom==monitor.rcWork.bottom-edge.Px(12),"equal 12 DIP bottom/right screen margins at all scales");
            edge.entering=true;edge.entranceStarted=1000;edge.AdvanceEntrance(1000);GetWindowRect(edge.window,&rect);
            Check(rect.top==edge.restingBounds.top+edge.Px(32),"entrance starts below resting position");
            edge.AdvanceEntrance(1125);GetWindowRect(edge.window,&rect);Check(rect.top>=edge.restingBounds.top && rect.top<edge.restingBounds.top+edge.Px(16),"documented fast entrance curve advances upward");
            edge.AdvanceEntrance(1250);GetWindowRect(edge.window,&rect);Check(!edge.entering && EqualRect(&rect,&edge.restingBounds),"animation settles exactly and stops its timer");
            edge.highContrast=true;edge.BeginEntrance();Check(!edge.entering,"contrast fallback skips motion");
            Popup nested;nested.parent=&edge;nested.controller=controller;nested.dpi=dpi;nested.rows=CursorRows(1u<<CursorDisabled,CursorDisabled);
            Check(nested.Create(edge.window,{rect.right,rect.top},true),"nested edge popup created");nested.BeginEntrance();Check(!nested.entering,"nested hover menu does not repeat root entrance");
        }
        if(argc==3 && wcscmp(argv[2],L"--compositor")==0) Compositor(controller,argv[1]);
        DestroyWindow(controller);
        std::cout<<"PASS: Windows menu typography at 96/120/144/168/192 DPI in both themes; native DirectWrite alpha/ink alignment, root date/icon toggles, general submenu/update propagation/spinner frames/red exit, branch switching, acrylic/fallback, cursor actions and keyboard selection.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
