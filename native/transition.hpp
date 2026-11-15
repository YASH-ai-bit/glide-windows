#pragma once
#include "platform.hpp"
namespace glide {
class Transition {
    struct Prepared {
        AppWindow app;
        WINDOWPLACEMENT saved{sizeof(saved)};
        RECT initial{},target{};
        bool minimized=false,maximized=false,fit=false,requested=false,changed=false;
        double requestedAt=0,readyAt=0;
        void prepare() {
            if(requested || (!minimized && !fit) || !app.valid()) return;
            requested=changed=true; requestedAt=now(); readyAt=0;
            WINDOWPLACEMENT p=saved; p.flags|=WPF_ASYNCWINDOWPLACEMENT;
            p.showCmd=(fit || maximized) ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE;
            BOOL accepted=SetWindowPlacement(app.hwnd,&p);
            if(!accepted) { requested=false; return; }
            // Schedule painting without synchronously waiting for another process.
            RedrawWindow(app.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);
        }
        void rollback() {
            if(!requested || !app.valid()) return;
            WINDOWPLACEMENT p=saved; p.flags|=WPF_ASYNCWINDOWPLACEMENT;
            if(minimized && !maximized && fit) {
                // Return to normal first; minimizing a currently maximized window
                // would otherwise silently change its maximize-on-restore flag.
                p.showCmd=SW_SHOWNOACTIVATE; SetWindowPlacement(app.hwnd,&p);
            }
            p.showCmd=minimized ? SW_SHOWMINNOACTIVE : maximized ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE;
            SetWindowPlacement(app.hwnd,&p); requested=false;
        }
        bool layoutReady() const {
            return !IsIconic(app.hwnd) && (!fit || IsZoomed(app.hwnd));
        }
    };
    HWND overlay=nullptr;
    std::array<AppWindow,3> apps{};
    std::array<std::unique_ptr<LiveView>,3> views;
    std::vector<Prepared> prepared;
    RECT monitor{},workArea{};
    double started=0,released=0,base=0,position=0,paintedPosition=0;
    bool running=false,primed=false,settling=false,activationSent=false,animate=true;
    int selected=0,presented=0;
    bool endpointSubmitted=false;
    ReleaseMotion motion;
    CommitIntent intent;
    HWND activated=nullptr;
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM p,LPARAM l) {
        if(m==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if(m==WM_ERASEBKGND) return 1;
        if(m==WM_PAINT) {
            PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r);
            HBRUSH brush=CreateSolidBrush(RGB(17,21,28)); FillRect(dc,&r,brush);
            DeleteObject(brush); EndPaint(w,&ps); return 0;
        }
        return DefWindowProcW(w,m,p,l);
    }
    Prepared* state(HWND w) {
        auto it=std::find_if(prepared.begin(),prepared.end(),[&](const Prepared& p) { return p.app.hwnd==w; });
        return it==prepared.end() ? nullptr : &*it;
    }
    RECT frame(int slot) {
        Prepared* p=state(apps[slot].hwnd);
        RECT result=slot==1 && p ? p->initial : p && p->fit ? p->target : views[slot]->rectangle();
        if(!animate) return result;
        if(slot==1) return result;
        double reveal=clamp(1-std::abs(position+slot-1),0,1);
        // The fitted page is present from its first visible pixel. Only a 1.2% lift
        // eases out during the first 28% of the drag, before the commit threshold.
        return scaleRect(result,.988+.012*incomingFit(reveal));
    }
    bool paint() {
        for(int i=0;i<3;i++) if(views[i]) {
            if(!views[i]->geometry()) return false;
            Prepared* p=state(apps[i].hwnd);
            if(p && p->requested && p->layoutReady()) {
                if(p->fit) p->target=views[i]->rectangle();
                if(!p->readyAt) p->readyAt=now();
            }
            if(!views[i]->paint(int(std::lround(position*width(monitor)))+(i-1)*width(monitor),monitor,frame(i))) return false;
        }
        paintedPosition=position;
        return true;
    }
    double lower() const { return apps[2].hwnd ? -1. : 0.; }
    double upper() const { return apps[0].hwnd ? 1. : 0.; }
    bool ownsForeground(HWND foreground) const {
        if(foreground==apps[1].hwnd || (activated && foreground==activated)) return true;
        return std::any_of(prepared.begin(),prepared.end(),[&](const Prepared& p) { return p.changed && foreground==p.app.hwnd; });
    }
    void prepareVisible() {
        int incoming=settling ? selected+1 : position<-.00025 ? 2 : position>.00025 ? 0 : 1;
        // Also service preparation here if direction changed between input frames.
        if(incoming!=1) if(auto p=state(apps[incoming].hwnd)) p->prepare();
        HWND foreground=GetForegroundWindow();
        if(!activationSent && foreground!=apps[1].hwnd && ownsForeground(foreground))
            activate(apps[1].hwnd,foreground);
    }
    void beginActivation(HWND target) {
        if(activationSent && GetForegroundWindow()==target) return;
        HWND foreground=GetForegroundWindow();
        if(!ownsForeground(foreground)) { finish(false); return; }
        if(IsIconic(target)) return;
        activationSent=activate(target,foreground);
        if(activationSent) activated=target;
    }
public:
    double lastReleaseMs=0,lastPrepareMs=0;
    bool lastPreparedBeforeRelease=false;
    std::function<void()> ended;
    Transition() {
        WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"GlideNativeTransition"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&wc);
        overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,
            L"Glide native live transition",WS_POPUP,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);
        if(!overlay) throw std::runtime_error("Cannot create transition window");
    }
    ~Transition() { finish(false); DestroyWindow(overlay); }
    bool active() const { return running; }
    bool isPrimed() const { return primed; }
    bool isSettling() const { return settling; }
    HWND origin() const { return apps[1].hwnd; }
    double progress() const { return position; }
    double renderedProgress() const { return paintedPosition; }
    double plannedDuration() const { return motion.duration(); }
    RECT viewport() const { return monitor; }
    RECT previewRectangle(int slot) { return views[slot] ? frame(slot) : RECT{}; }
    bool targetPrepared() {
        auto p=state(apps[selected+1].hwnd); return p && p->layoutReady();
    }
    void discard() {
        if(running) return;
        primed=false; for(auto& v:views) v.reset(); prepared.clear();
    }
    bool prime(const std::vector<AppWindow>& windows,int index,bool wrap=false,bool fitIncoming=true) {
        if(running || windows.empty() || index<0 || index>=int(windows.size())) return false;
        discard();
        int n=int(windows.size()); apps={}; apps[1]=windows[index];
        if(index>0) apps[0]=windows[index-1]; else if(wrap && n>1) apps[0]=windows.back();
        if(index+1<n) apps[2]=windows[index+1]; else if(wrap && n>1) apps[2]=windows.front();
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(apps[1].hwnd,MONITOR_DEFAULTTOPRIMARY),&mi);
        RECT source{}; DwmGetWindowAttribute(apps[1].hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&source,sizeof(source));
        bool fullscreen=source.left<=mi.rcMonitor.left && source.top<=mi.rcMonitor.top &&
            source.right>=mi.rcMonitor.right && source.bottom>=mi.rcMonitor.bottom;
        monitor=fullscreen ? mi.rcMonitor : mi.rcWork; workArea=mi.rcWork;
        SetWindowPos(overlay,HWND_TOPMOST,monitor.left,monitor.top,width(monitor),height(monitor),SWP_NOACTIVATE);
        BOOL animations=TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0); animate=animations!=FALSE;
        for(int i=0;i<3;i++) if(apps[i].hwnd) {
            views[i]=std::make_unique<LiveView>();
            if(!views[i]->open(overlay,apps[i].hwnd)) { discard(); return false; }
            if(!state(apps[i].hwnd)) {
                Prepared p; p.app=apps[i];
                if(!GetWindowPlacement(p.app.hwnd,&p.saved)) { discard(); return false; }
                p.minimized=IsIconic(p.app.hwnd)!=FALSE;
                p.maximized=IsZoomed(p.app.hwnd)!=FALSE || (p.minimized && (p.saved.flags&WPF_RESTORETOMAXIMIZED));
                LONG_PTR style=GetWindowLongPtrW(p.app.hwnd,GWL_STYLE);
                p.fit=i!=1 && fitIncoming && !p.maximized && (style&WS_MAXIMIZEBOX) && (style&WS_THICKFRAME);
                p.initial=views[i]->rectangle(); p.target=p.fit ? workArea : p.initial;
                prepared.push_back(p);
            }
        }
        base=position=paintedPosition=0; primed=true; return true;
    }
    bool begin() {
        if(running || !primed || GetForegroundWindow()!=apps[1].hwnd) return false;
        for(const auto& a:apps) if(a.hwnd && !a.valid()) { discard(); return false; }
        started=now(); released=0; selected=presented=0; intent.reset();
        settling=activationSent=endpointSubmitted=false; activated=nullptr; running=true; primed=false;
        if(!paint()) { finish(false); return false; }
        ShowWindow(overlay,SW_SHOWNOACTIVATE); UpdateWindow(overlay); return true;
    }
    bool start(const std::vector<AppWindow>& windows,int index,bool wrap=false,bool fitIncoming=true) {
        return prime(windows,index,wrap,fitIncoming) && begin();
    }
    void grab(double time) {
        if(!running) return;
        position=paintedPosition; base=rawPosition(position,lower(),upper());
        started=time; settling=activationSent=endpointSubmitted=false; selected=0; intent.reset();
        HWND foreground=GetForegroundWindow();
        if(foreground!=apps[1].hwnd && ownsForeground(foreground)) activate(apps[1].hwnd,foreground);
    }
    void move(double progress,double speed) {
        if(!running) return;
        position=dragPosition(base+progress,lower(),upper()); started=now();
        intent.update(position,speed);
        int incoming=position<-.00025 ? 2 : position>.00025 ? 0 : 1;
        // Dispatch the asynchronous layout request on the first directional input;
        // do not wait for a compositor callback or a commit decision to begin it.
        if(incoming!=1) if(auto p=state(apps[incoming].hwnd)) p->prepare();
    }
    void release(double speed,bool cancel,double time) {
        if(!running) return;
        selected=(cancel || modifiers()) ? 0 : intent.update(position,speed);
        if(!apps[selected+1].hwnd) selected=0;
        if((position<lower() && speed<0) || (position>upper() && speed>0)) speed=0;
        // Start at the last submitted visual position, not an input sample that
        // has never appeared on screen. Keep both pages on the same trajectory.
        position=paintedPosition;
        released=time; settling=true; activationSent=endpointSubmitted=false;
        motion.begin(position,-selected,speed,time,animate);
        auto chosen=state(apps[selected+1].hwnd);
        lastPreparedBeforeRelease=chosen && chosen->layoutReady();
        lastPrepareMs=chosen && chosen->readyAt ? (chosen->readyAt-chosen->requestedAt)*1000 : 0;
        // Layout stays prepared, but focus changes only near the end of the slide.
        // Do not roll back a still-visible outgoing preview at release.
        if(chosen && selected) chosen->prepare();
    }
    void tick(double time) {
        if(!running) return;
        if(GetAsyncKeyState(VK_ESCAPE)&0x8000 || time-started>12) { finish(false); return; }
        for(const auto& a:apps) if(a.hwnd && !a.valid()) { finish(false); return; }
        if(!ownsForeground(GetForegroundWindow())) { finish(false); return; }
        prepareVisible();
        if(settling) {
            position=motion.value(time);
            if(motion.done(time) || time-released>=std::max(0.,motion.duration()-.035))
                beginActivation(apps[selected+1].hwnd);
        }
        if(!running) return;
        if(!paint()) { finish(false); return; }
        presented++;
        if(settling && motion.done(time)) {
            // DwmFlush paces the next tick. Give the submitted endpoint a full
            // compositor opportunity before hiding the live outgoing surface.
            if(!endpointSubmitted) { endpointSubmitted=true; return; }
            HWND target=apps[selected+1].hwnd;
            if(GetForegroundWindow()==target && targetPrepared()) {
                lastReleaseMs=(time-released)*1000; clearAttention(target); finish(true);
            } else if(time-released>motion.duration()+.180) {
                log(L"Window activation or layout was not confirmed"); finish(false);
            }
        }
    }
    void finish(bool committed) {
        if(!running) { discard(); return; }
        running=false;
        HWND chosen=committed ? apps[selected+1].hwnd : apps[1].hwnd;
        HWND foreground=GetForegroundWindow();
        if(!committed && foreground!=chosen && ownsForeground(foreground)) activate(chosen,foreground);
        for(auto& p:prepared) if(p.app.hwnd!=chosen) p.rollback();
        ShowWindow(overlay,SW_HIDE); discard(); settling=false;
        if(ended) ended();
    }
};
}
