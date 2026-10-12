// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shared.hpp"
#include <aclapi.h>
#include <vector>
namespace acrylic {
// Grant the verified Search package read/execute access to this app-owned DLL
// only. Do not broaden access to its directory, user files or other packages.
inline HRESULT SearchHelperAccess(HANDLE target,const wchar_t* file){
    Handle token;HANDLE raw=nullptr;if(!OpenProcessToken(target,TOKEN_QUERY,&raw))return HRESULT_FROM_WIN32(GetLastError());token.reset(raw);
    DWORD size=0;GetTokenInformation(token.get(),TokenAppContainerSid,nullptr,0,&size);if(!size)return HRESULT_FROM_WIN32(GetLastError());
    std::vector<BYTE> buffer(size);if(!GetTokenInformation(token.get(),TokenAppContainerSid,buffer.data(),size,&size))return HRESULT_FROM_WIN32(GetLastError());
    const auto sid=reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(buffer.data())->TokenAppContainer;if(!sid)return E_INVALIDARG;
    PACL existing=nullptr;PSECURITY_DESCRIPTOR descriptor=nullptr;
    DWORD error=GetNamedSecurityInfoW(file,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&existing,nullptr,&descriptor);
    if(error)return HRESULT_FROM_WIN32(error);
    EXPLICIT_ACCESSW access{};access.grfAccessPermissions=FILE_GENERIC_READ|FILE_GENERIC_EXECUTE;access.grfAccessMode=GRANT_ACCESS;
    access.grfInheritance=NO_INHERITANCE;access.Trustee.TrusteeForm=TRUSTEE_IS_SID;access.Trustee.TrusteeType=TRUSTEE_IS_USER;access.Trustee.ptstrName=reinterpret_cast<LPWSTR>(sid);
    ACCESS_MASK rights=0;error=GetEffectiveRightsFromAclW(existing,&access.Trustee,&rights);
    if(!error && (rights&access.grfAccessPermissions)==access.grfAccessPermissions){LocalFree(descriptor);return S_OK;}
    PACL updated=nullptr;error=SetEntriesInAclW(1,&access,existing,&updated);
    if(!error)error=SetNamedSecurityInfoW(const_cast<LPWSTR>(file),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,updated,nullptr);
    if(updated)LocalFree(updated);LocalFree(descriptor);return HRESULT_FROM_WIN32(error);
}
inline HANDLE DuplicateTo(HANDLE value,HANDLE target,DWORD rights){
    HANDLE remote=nullptr;if(!DuplicateHandle(GetCurrentProcess(),value,target,&remote,rights,FALSE,0))return nullptr;return remote;
}
inline void CloseRemote(HANDLE target,HANDLE value)noexcept{
    if(!target || !value)return;HANDLE local=nullptr;
    if(DuplicateHandle(target,value,GetCurrentProcess(),&local,0,FALSE,DUPLICATE_CLOSE_SOURCE))CloseHandle(local);
}
}
