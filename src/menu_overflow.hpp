// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
namespace acrylic {
inline float MenuTickerOffset(ULONGLONG elapsed,int overflow,int speed) noexcept {
    if(overflow<=0 || speed<=0)return 0;
    constexpr ULONGLONG pause=900;
    const auto travel=static_cast<ULONGLONG>(std::ceil(1000.0*overflow/speed));
    const auto phase=elapsed%(2*pause+travel);
    if(phase<=pause)return 0;
    return static_cast<float>(std::min<double>(overflow,(phase-pause)*speed/1000.0));
}
// Fade only the premultiplied text layer; the acrylic/highlight underneath is
// never sampled, copied or dimmed. Both RGB and alpha get the same multiplier.
inline void FadeMenuText(DWORD* pixels,int width,int height,int fade,bool left,bool right) noexcept {
    fade=std::min(fade,width/2);
    if(!pixels || fade<=0)return;
    const auto dim=[](DWORD value,unsigned coverage){
        DWORD result=0;for(unsigned shift=0;shift<32;shift+=8)result|=(((value>>shift)&255)*coverage/255)<<shift;
        return result;
    };
    for(int y=0;y<height;++y)for(int x=0;x<fade;++x){
        const unsigned coverage=static_cast<unsigned>(255*x/fade);
        if(left)pixels[y*width+x]=dim(pixels[y*width+x],coverage);
        if(right)pixels[y*width+width-1-x]=dim(pixels[y*width+width-1-x],coverage);
    }
}
}
