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
UpdateStatus state = UpdateStatus::Idle;
bool dateEnabled = true;
UINT cursorMask = 1u << CursorDisabled, cursorChoice = CursorDisabled, settings = 0;
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
LRESULT CALLBACK Owner(HWND window, UINT message, WPARAM parameter, LPARAM other) {
    if (message == TrayThemeQueryMessage) return parameter ? static_cast<UINT>(AutoThemeStatus::Manual) : light;
    if (message == TrayDateQueryMessage) return dateEnabled;
    if (message == TraySettingMessage && parameter == TrayDateCommand) { dateEnabled = !dateEnabled; return dateEnabled; }
    if (message == TrayUpdateQueryMessage) return static_cast<UINT>(state);
    if (message == TrayCursorQueryMessage) return parameter ? cursorChoice : cursorMask;
    if (message == TrayCursorIconMessage) {
        wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
        return reinterpret_cast<LRESULT>(CreateCursorMenuIcon((std::wstring(windows)+L"\\Cursors\\aero_arrow.cur").c_str(),static_cast<int>(parameter)));
    }
    if (message == TraySettingMessage && parameter == TrayUpdateCommand) { state = UpdateStatus::Checking; return static_cast<UINT>(state); }
    if (message == TraySettingMessage) { ++settings; return cursorChoice; }
    return DefWindowProcW(window,message,parameter,other);
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
        Check(empty>count/2 && solid>100 && edges>100,"background reveals acrylic while text/icons retain solid and antialiased coverage");
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
        for (int theme=0;theme<2;++theme) for (UINT dpi : {96u,144u,192u}) {
            light=theme!=0;state=UpdateStatus::Idle;
            Popup popup;popup.controller=controller;popup.dpi=dpi;popup.light=light;popup.systemLight=light;
            cursorMask=1u<<CursorDisabled;
            popup.rows=RootRows(AcrylicMode::System,true,light,cursorMask);
            Check(popup.Create(controller,{100,100},false),"native popup created");
            Check(popup.acrylic,"Windows accepts explicit acrylic on root menu");
            LOGFONTW font{};GetObjectW(popup.font,sizeof(font),&font);
            Check(font.lfQuality==ANTIALIASED_QUALITY,"GDI fallback uses grayscale smoothing for glass");
            const HDC fontDc=CreateCompatibleDC(nullptr);const auto previous=SelectObject(fontDc,popup.font);wchar_t face[LF_FACESIZE]{};GetTextFaceW(fontDc,LF_FACESIZE,face);TEXTMETRICW resolved{};GetTextMetricsW(fontDc,&resolved);SelectObject(fontDc,previous);DeleteDC(fontDc);
            NONCLIENTMETRICSW metrics{sizeof(metrics)};
            Check(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi)!=FALSE,"Windows menu font queried at display DPI");
            Check(wcscmp(font.lfFaceName,metrics.lfMenuFont.lfFaceName)==0 && font.lfHeight==metrics.lfMenuFont.lfHeight
                && font.lfWeight==metrics.lfMenuFont.lfWeight,"menu inherits Windows' family, size and weight without overrides");
            NativeTextChecks(popup);
            if(theme==0) std::wcout<<L"Menu font at "<<dpi<<L" DPI: "<<face<<L", height="<<font.lfHeight<<L", weight="<<font.lfWeight<<L'\n';
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
            popup.Activate(4,false);
            Check(state==UpdateStatus::Checking && popup.window==same && !popup.done,"check starts while keeping menu alive");
            for (const auto result : {UpdateStatus::Checking,UpdateStatus::Current,UpdateStatus::Available,UpdateStatus::NoRelease,UpdateStatus::Error,UpdateStatus::Downloading,UpdateStatus::Installing}) {
                state=result;popup.RefreshUpdate();
                Check(popup.rows[4].label == std::wstring(L"v") + AppVersion,"installed version remains primary label for every update state");
                const int label=popup.menuText.Width(popup.rows[4].label),secondary=popup.menuText.Width(popup.rows[4].secondary);
                RECT bounds{};GetClientRect(popup.buttons[4],&bounds);
                Check(label+secondary+popup.Px(34+12+12)<bounds.right,"native text measurements fit without overlap");
                wchar_t accessible[256]{};GetWindowTextW(popup.buttons[4],accessible,256);
                Check(std::wstring(accessible).find(UpdateStatusLabel(result))!=std::wstring::npos,"accessible update state reflects result");
                const auto name=std::wstring(light?L"light-":L"dark-")+std::to_wstring(dpi)+L"-"+std::to_wstring(static_cast<UINT>(result))+L".bmp";
                Capture(popup,std::filesystem::path(argv[1])/name);
                if (result == UpdateStatus::Checking) {
                    Check(popup.spinnerBitmap && popup.spinnerBitmap->GetLastStatus()==Gdiplus::Ok,"spinner bitmap loaded");
                    Gdiplus::Color corner;popup.spinnerBitmap->GetPixel(0,0,&corner);
                    Check(corner.GetAlpha()==0,"spinner retains transparent corners");
                    popup.updateStarted -= 125;
                    Capture(popup,std::filesystem::path(argv[1])/(L"spin-"+name));
                }
            }
            Popup cursors;cursors.controller=controller;cursors.parent=&popup;cursors.kind=TrayCursorCommand;cursors.dpi=dpi;cursors.light=light;cursors.systemLight=light;
            cursorMask=1u<<CursorDisabled;cursors.rows=CursorRows(cursorMask,CursorDisabled);
            Check(cursors.Create(popup.window,{100,100},true),"cursor submenu created");
            Check(cursors.acrylic,"nested menu also requests acrylic");
            const HWND submenu=cursors.window;
            const UINT before=settings;cursors.Activate(1,false);
            Check(settings==before,"disabled cursor cannot invoke selection");
            for(size_t i=0;i<10;++i) Check(!IsWindowEnabled(cursors.buttons[i]),"custom cursor choice disabled");
            Check(IsWindowEnabled(cursors.buttons[10]) && cursors.buttons.size()==11,"disable enabled and redundant unlock row removed");
            cursors.Focus(0);Check(GetFocus()==cursors.buttons[10],"keyboard skips locked cursor choices");
            Capture(cursors,std::filesystem::path(argv[1])/(std::wstring(light?L"locked-light-":L"locked-dark-")+std::to_wstring(dpi)+L".bmp"));
            cursorMask=(1u<<(CursorDisabled+1))-1;cursors.RefreshCursors();
            popup.RefreshCursors();Check(popup.rows[3].label==L"cursor" && popup.rows[3].secondary.empty() && HasSubmenu(popup.rows[3].command),"import replaces unlock action with cursor submenu in place");
            for(size_t i=0;i<10;++i) Check(IsWindowEnabled(cursors.buttons[i]),"import enables custom choices");
            Check(cursors.window==submenu && !cursors.done && cursors.rows[10].checked,"import keeps submenu and Windows selection");
            Capture(cursors,std::filesystem::path(argv[1])/(std::wstring(light?L"unlocked-light-":L"unlocked-dark-")+std::to_wstring(dpi)+L".bmp"));
            popup.highContrast=true;popup.ConfigureBackdrop();
            Check(!popup.acrylic,"high contrast uses a solid fallback");
            popup.highContrast=false;popup.ConfigureBackdrop();Check(popup.acrylic,"acrylic recovers after fallback");
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
        std::cout<<"PASS: production menus at 96/144/192 DPI in both themes; regular fonts, root date/icon toggles, acrylic attributes/foreground alpha/fallback, installed version/update states/spinner frames, locked cursor actions and keyboard skipping, live unlocking without changing selection.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
