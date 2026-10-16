// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/Windows.Foundation.h>

// Public IGraphicsEffectD2D1Interop ABI; some Windows SDKs omit its declaration.
struct __declspec(uuid("2FC57384-A068-44D7-A331-30982FCF7177")) JellyEffectInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID*) noexcept=0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR,UINT*,UINT*) noexcept=0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT*) noexcept=0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT,winrt::impl::abi_t<winrt::Windows::Foundation::IPropertyValue>**) noexcept=0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT,IUnknown**) noexcept=0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT*) noexcept=0;
};
