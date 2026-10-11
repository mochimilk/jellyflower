// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/cursor_icon.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return ERROR_INVALID_PARAMETER;
    const std::wstring assets = std::wstring(argv[1]) + L"\\cursor\\assets\\";
    const wchar_t* folders[]{L"light", L"dark", L"blueberry", L"greentea", L"mint", L"strawberry", L"thaitea", L"vanilla", L"yogurt"};
    // Independent observed swatches from the pack's solid 128px source frames.
    const uint32_t bodies[]{0xffffff,0x242424,0xb0b1ff,0xe1eb8e,0xd6f687,0xffe2ff,0xfbacad,0xece68d,0xcaeafe};
    const uint32_t borders[]{0x000000,0xffffff,0x1a0056,0x1b1d1a,0x323c3a,0x6a336f,0x400008,0x1e1c00,0x01203b};
    int failures = 0;
    std::array<std::vector<DWORD>, 7> reference;
    const int sizes[]{16,20,24,30,32,40,64};
    for (int source = 0; source < 12; ++source) for (size_t entry = 0; entry < std::size(sizes); ++entry) {
        const int size = sizes[entry];
        const std::wstring file = source < 9 ? assets + folders[source] + L"\\arrow.cur"
            : source == 9 ? L"C:\\Windows\\Cursors\\aero_arrow.cur" : source == 10 ? L"" : assets + L"missing\\arrow.cur";
        const HICON icon = acrylic::CreateCursorMenuIcon(file.c_str(), size);
        ICONINFO info{}; BITMAP bitmap{};
        if (!icon || !GetIconInfo(icon, &info) || !GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap)) { ++failures; continue; }
        BITMAPINFO layout{};
        layout.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        std::vector<DWORD> pixels(static_cast<size_t>(size) * size);
        const HDC dc = CreateCompatibleDC(nullptr);
        if (bitmap.bmWidth != size || bitmap.bmHeight != size || GetDIBits(dc, info.hbmColor, 0, size, pixels.data(), &layout, DIB_RGB_COLORS) != size) ++failures;
        DeleteDC(dc);
        for (DWORD threshold : {32u,128u}) {
            int left = size, top = size, right = 0, bottom = 0;
            for (int y=0;y<size;++y) for (int x=0;x<size;++x) if ((pixels[static_cast<size_t>(y)*size+x] >> 24) >= threshold) {
                left = std::min(left,x); top = std::min(top,y); right = std::max(right,x+1); bottom = std::max(bottom,y+1);
            }
            if (right <= left || bottom <= top || std::abs(left+right-size)>1 || std::abs(top+bottom-size)>1) {
                std::printf("FAIL: source=%d size=%d alpha=%lu bounds=%d,%d,%d,%d\n",source,size,threshold,left,top,right,bottom); ++failures;
            }
        }
        if (source == 0) reference[entry] = pixels;
        for (size_t index = 0; index < pixels.size(); ++index) {
            const DWORD value = pixels[index], alpha = value >> 24;
            if (((value >> 16) & 255) > alpha || ((value >> 8) & 255) > alpha || (value & 255) > alpha) ++failures;
            if ((reference[entry][index] >> 24) != alpha) ++failures;
        }
        if (source < 9 && size == 64) {
            const auto body = 0xff000000u | bodies[source], border = 0xff000000u | borders[source];
            if (std::count(pixels.begin(),pixels.end(),body) < 30 || std::count(pixels.begin(),pixels.end(),border) < 10) {
                std::printf("FAIL: source=%d body/border colors do not match the real cursor\n",source); ++failures;
            }
        }
        if (argc > 2) {
            const auto name = std::wstring(argv[2]) + L"\\cursor-svg-" + std::to_wstring(source) + L"-" + std::to_wstring(size) + L".tga";
            std::ofstream output(name, std::ios::binary);
            const unsigned char header[]{0,0,2,0,0,0,0,0,0,0,0,0,static_cast<unsigned char>(size),0,static_cast<unsigned char>(size),0,32,0x28};
            output.write(reinterpret_cast<const char*>(header),sizeof(header));
            output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()*4));
        }
        DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); DestroyIcon(icon);
    }
    if (acrylic::CreateCursorMenuIcon(L"",7) || acrylic::CreateCursorMenuIcon(L"",129)) ++failures;
    std::printf("%s: supplied SVG geometry/centering, identical alpha for all colors/stock/fallback, premultiplied output, and nine actual body/border color pairs at seven sizes.\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
