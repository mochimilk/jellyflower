// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <wincodec.h>
#include <shcore.h>
#include <wrl/client.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace acrylic::adaptive {
struct Bitmap {
    UINT width=0,height=0;
    std::vector<uint8_t> pixels;
    uint64_t hash=0;
    bool monochrome=false;
};
inline Bitmap Inspect(winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap const& bitmap){
    Bitmap result;
    const auto width=bitmap.PixelWidth(),height=bitmap.PixelHeight();
    if(width<1 || height<1 || width>128 || height>128)return result;
    const size_t size=static_cast<size_t>(width)*height*4;
    const auto buffer=bitmap.PixelBuffer();
    if(buffer.Length()<size || !buffer.data())return result;
    result.width=width;result.height=height;
    result.pixels.assign(buffer.data(),buffer.data()+size);
    uint64_t hash=14695981039346656037ull;
    unsigned visible=0,transparent=0;
    bool colored=false;
    for(size_t offset=0;offset<size;offset+=4){
        const auto* pixel=result.pixels.data()+offset;
        hash=(hash^pixel[3])*1099511628211ull;
        if(pixel[3]<8){++transparent;continue;}
        ++visible;
        for(unsigned channel=0;channel<3;++channel)hash=(hash^pixel[channel])*1099511628211ull;
        const auto range=std::minmax({pixel[0],pixel[1],pixel[2]});
        // Ignore fully transparent RGB garbage; reject even a small visible
        // colored detail. Never flatten a colored logo or warning into a mask.
        if(range.second-range.first>2)colored=true;
    }
    result.hash=hash;
    // Opaque bitmap backgrounds are not isolated theme-able glyph artwork.
    result.monochrome=!colored && visible>=4 && transparent>=std::max(1u,static_cast<UINT>(width*height)/20);
    return result;
}
inline winrt::Windows::Storage::Streams::IRandomAccessStream MaskStream(Bitmap const& bitmap){
    using Microsoft::WRL::ComPtr;
    if(!bitmap.monochrome || !bitmap.width || !bitmap.height)winrt::throw_hresult(E_INVALIDARG);
    ComPtr<IWICImagingFactory> factory;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
    ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(bitmap.width,bitmap.height));
    auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));
    if(format!=GUID_WICPixelFormat32bppBGRA)winrt::throw_hresult(E_NOTIMPL);
    auto pixels=bitmap.pixels;
    for(size_t offset=0;offset<pixels.size();offset+=4)pixels[offset]=pixels[offset+1]=pixels[offset+2]=255;
    winrt::check_hresult(frame->WritePixels(bitmap.height,bitmap.width*4,static_cast<UINT>(pixels.size()),pixels.data()));
    winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
    LARGE_INTEGER beginning{};winrt::check_hresult(stream->Seek(beginning,STREAM_SEEK_SET,nullptr));
    winrt::Windows::Storage::Streams::IRandomAccessStream result{nullptr};
    winrt::check_hresult(CreateRandomAccessStreamOverStream(stream.Get(),BSOS_DEFAULT,
        winrt::guid_of<winrt::Windows::Storage::Streams::IRandomAccessStream>(),reinterpret_cast<void**>(winrt::put_abi(result))));
    return result;
}
}
