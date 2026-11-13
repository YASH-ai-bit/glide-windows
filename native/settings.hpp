#pragma once
#include "platform.hpp"

namespace glide {
struct Settings {
    bool includeMinimized=false;
    bool wrapAround=false;
    bool fitIncoming=true;
    int sensitivity=185;

    static std::wstring path() {
        wchar_t local[MAX_PATH]{};
        if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH)) return L"";
        std::wstring folder=std::wstring(local)+L"\\Glide";
        CreateDirectoryW(folder.c_str(),nullptr);
        return folder+L"\\settings.ini";
    }
    void load() {
        std::wstring file=path(); if(file.empty()) return;
        includeMinimized=GetPrivateProfileIntW(L"Swipes",L"IncludeMinimized",0,file.c_str())!=0;
        wrapAround=GetPrivateProfileIntW(L"Swipes",L"WrapAround",0,file.c_str())!=0;
        fitIncoming=GetPrivateProfileIntW(L"Swipes",L"FitIncoming",1,file.c_str())!=0;
        sensitivity=std::clamp(int(GetPrivateProfileIntW(L"Swipes",L"Sensitivity",185,file.c_str())),100,300);
    }
    void save() const {
        std::wstring file=path(); if(file.empty()) return;
        WritePrivateProfileStringW(L"Swipes",L"IncludeMinimized",includeMinimized ? L"1" : L"0",file.c_str());
        WritePrivateProfileStringW(L"Swipes",L"WrapAround",wrapAround ? L"1" : L"0",file.c_str());
        WritePrivateProfileStringW(L"Swipes",L"FitIncoming",fitIncoming ? L"1" : L"0",file.c_str());
        WritePrivateProfileStringW(L"Swipes",L"Sensitivity",std::to_wstring(sensitivity).c_str(),file.c_str());
    }
};
}
