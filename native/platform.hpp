#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "motion.hpp"

namespace glide {
inline double now() { static double frequency=[] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }(); LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart/frequency; }
inline int width(const RECT& r) { return r.right-r.left; }
inline int height(const RECT& r) { return r.bottom-r.top; }
inline bool modifiers() { for(int key:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN}) if(GetAsyncKeyState(key)&0x8000) return true; return false; }
inline bool nativeDisabled() {
    DWORD value=1,size=sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad",L"ThreeFingerSlideEnabled",RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS && value==0;
}
inline void log(const std::wstring& text) {
    wchar_t path[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA",path,MAX_PATH);
    std::wstring folder=std::wstring(path)+L"\\Glide"; CreateDirectoryW(folder.c_str(),nullptr);
    FILE* f=nullptr; _wfopen_s(&f,(folder+L"\\native.log").c_str(),L"a, ccs=UTF-8");
    if(f) { SYSTEMTIME t; GetLocalTime(&t); fwprintf(f,L"%02u:%02u:%02u %ls\n",t.wHour,t.wMinute,t.wSecond,text.c_str()); fclose(f); }
}
struct AppWindow {
    HWND hwnd=nullptr; DWORD pid=0; std::wstring title;
    bool valid() const {
        DWORD p=0; GetWindowThreadProcessId(hwnd,&p); DWORD cloaked=0;
        return IsWindow(hwnd) && p==pid && IsWindowEnabled(hwnd) && IsWindowVisible(hwnd)
            && SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && !cloaked;
    }
};
class Catalog {
    std::vector<HWND> order;
public:
    std::vector<AppWindow> read(bool includeMinimized=true) {
        std::vector<AppWindow> result;
        EnumWindows([](HWND w,LPARAM l)->BOOL {
            auto& out=*reinterpret_cast<std::vector<AppWindow>*>(l);
            if(!IsWindowVisible(w) || !IsWindowEnabled(w)) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(w,&pid); if(pid==GetCurrentProcessId()) return TRUE;
            LONG_PTR ex=GetWindowLongPtrW(w,GWL_EXSTYLE);
            if((ex&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)) || (GetWindow(w,GW_OWNER) && !(ex&WS_EX_APPWINDOW))) return TRUE;
            DWORD cloaked=0; if(FAILED(DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) || cloaked) return TRUE;
            wchar_t title[512]{}; GetWindowTextW(w,title,512); if(!title[0] || wcscmp(title,L"Program Manager")==0) return TRUE;
            RECT r{}; GetWindowRect(w,&r); if(!IsIconic(w) && (width(r)<100 || height(r)<80)) return TRUE;
            out.push_back({w,pid,title}); return TRUE;
        },reinterpret_cast<LPARAM>(&result));
        order.erase(std::remove_if(order.begin(),order.end(),[&](HWND w) { return std::none_of(result.begin(),result.end(),[&](const AppWindow& a) { return a.hwnd==w; }); }),order.end());
        for(const auto& a:result) if(std::find(order.begin(),order.end(),a.hwnd)==order.end()) order.push_back(a.hwnd);
        std::sort(result.begin(),result.end(),[&](const AppWindow& a,const AppWindow& b) { return std::find(order.begin(),order.end(),a.hwnd)<std::find(order.begin(),order.end(),b.hwnd); });
        // Keep hidden-to-taskbar windows in the remembered order even when skipped.
        if(!includeMinimized) result.erase(std::remove_if(result.begin(),result.end(),[](const AppWindow& a) { return IsIconic(a.hwnd)!=FALSE; }),result.end());
        return result;
    }
};
struct FocusDiagnostics { unsigned attempts=0,denied=0,preflightTimeouts=0; };
inline FocusDiagnostics focusDiagnostics;
inline void clearAttention(HWND target) {
    if(GetForegroundWindow()!=target) return;
    FLASHWINFO info{sizeof(info),target,FLASHW_STOP,0,0}; FlashWindowEx(&info);
}
inline bool activate(HWND target,HWND expected) {
    if(!IsWindow(target) || IsIconic(target)) return false;
    if(GetForegroundWindow()==target) return true;
    if(GetForegroundWindow()!=expected || modifiers()) return false;
    // A speculative, unattached SetForegroundWindow can flash the taskbar before
    // failing. Establish the input relationship first and make only the valid attempt.
    DWORD_PTR unused=0;
    if(!SendMessageTimeoutW(expected,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,4,&unused) || !SendMessageTimeoutW(target,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,4,&unused)) { focusDiagnostics.preflightTimeouts++; return false; }
    DWORD here=GetCurrentThreadId(),from=GetWindowThreadProcessId(expected,nullptr),to=GetWindowThreadProcessId(target,nullptr);
    bool a=from!=here && AttachThreadInput(here,from,TRUE),b=false;
    if(from!=here && !a) return false;
    if(to!=here && to!=from) b=AttachThreadInput(here,to,TRUE)!=FALSE;
    bool result=false;
    if((to==here || to==from || b) && GetForegroundWindow()==expected) {
        focusDiagnostics.attempts++; result=SetForegroundWindow(target)!=FALSE || GetForegroundWindow()==target;
        if(!result) focusDiagnostics.denied++;
    }
    if(b) AttachThreadInput(here,to,FALSE); if(a) AttachThreadInput(here,from,FALSE);
    if(result) clearAttention(target);
    return result;
}
inline bool cropToRect(const RECT& dest,const RECT& source,const RECT& clip,RECT& clipped,RECT& part) {
    clipped={std::max(clip.left,dest.left),std::max(clip.top,dest.top),std::min(clip.right,dest.right),std::min(clip.bottom,dest.bottom)};
    if(width(clipped)<=0 || height(clipped)<=0 || width(dest)<=0 || height(dest)<=0) return false;
    double sx=double(width(source))/width(dest),sy=double(height(source))/height(dest);
    part={source.left+LONG(std::lround((clipped.left-dest.left)*sx)),source.top+LONG(std::lround((clipped.top-dest.top)*sy)),source.left+LONG(std::lround((clipped.right-dest.left)*sx)),source.top+LONG(std::lround((clipped.bottom-dest.top)*sy))};
    return width(part)>0 && height(part)>0;
}
inline bool cropRect(const RECT& dest,const RECT& source,int w,int h,RECT& clipped,RECT& part) {
    return cropToRect(dest,source,{0,0,w,h},clipped,part);
}
class LiveView {
    HTHUMBNAIL thumb=nullptr;
    HWND window=nullptr;
    RECT bounds{},crop{};
public:
    ~LiveView() { if(thumb) DwmUnregisterThumbnail(thumb); }
    bool open(HWND destination,HWND source) {
        window=source;
        return SUCCEEDED(DwmRegisterThumbnail(destination,source,&thumb)) && geometry();
    }
    bool geometry() {
        if(!thumb || !IsWindow(window)) return false;
        SIZE s{}; if(FAILED(DwmQueryThumbnailSourceSize(thumb,&s)) || s.cx<=0 || s.cy<=0) return false;
        RECT outer{}; GetWindowRect(window,&outer); bounds=outer;
        if(IsIconic(window)) {
            WINDOWPLACEMENT p{sizeof(p)}; GetWindowPlacement(window,&p);
            MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&mi);
            bounds=(p.flags&WPF_RESTORETOMAXIMIZED) ? mi.rcWork : p.rcNormalPosition;
            if(!(p.flags&WPF_RESTORETOMAXIMIZED)) OffsetRect(&bounds,mi.rcWork.left-mi.rcMonitor.left,mi.rcWork.top-mi.rcMonitor.top);
            crop={0,0,s.cx,s.cy};
        } else {
            DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds));
            LONG left=std::max(0L,bounds.left-outer.left),top=std::max(0L,bounds.top-outer.top);
            crop={left,top,std::min(s.cx,left+width(bounds)),std::min(s.cy,top+height(bounds))};
        }
        return true;
    }
    RECT rectangle() const { return bounds; }
    bool paint(int offset,const RECT& monitor,const RECT& screenBounds) {
        if(!thumb || !IsWindow(window)) return false;
        RECT dest=screenBounds; OffsetRect(&dest,offset-monitor.left,-monitor.top);
        DWM_THUMBNAIL_PROPERTIES props{}; props.dwFlags=DWM_TNP_VISIBLE;
        // Each window belongs to a separate page, even if its native bounds
        // extend outside the work area. Never paint over an adjacent page.
        RECT page{std::max(0,offset),0,std::min(width(monitor),offset+width(monitor)),height(monitor)};
        props.fVisible=cropToRect(dest,crop,page,props.rcDestination,props.rcSource);
        if(props.fVisible) { props.dwFlags=0x1f; props.opacity=255; props.fSourceClientAreaOnly=FALSE; }
        return SUCCEEDED(DwmUpdateThumbnailProperties(thumb,&props));
    }
};
class FrameClock {
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr),active=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HWND receiver; UINT message; std::thread worker;
public:
    std::atomic<bool> pending{false};
    FrameClock(HWND w,UINT msg):receiver(w),message(msg),worker([this] {
        HANDLE events[]={stop,active};
        while(WaitForMultipleObjects(2,events,FALSE,INFINITE)==WAIT_OBJECT_0+1) {
            double t=now(); HRESULT hr=DwmFlush();
            if(FAILED(hr) || now()-t<.002) WaitForSingleObject(stop,4);
            if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0) break;
            if(WaitForSingleObject(active,0)==WAIT_OBJECT_0 && !pending.exchange(true)) PostMessageW(receiver,message,0,0);
        }
    }) {}
    ~FrameClock() { SetEvent(stop); worker.join(); CloseHandle(active); CloseHandle(stop); }
    void enable(bool on) { if(on) SetEvent(active); else ResetEvent(active); }
};
}
