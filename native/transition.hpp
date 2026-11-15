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
    struct Page { int number=0; AppWindow app; std::unique_ptr<LiveView> view; };
    std::vector<AppWindow> order;
    std::vector<Page> pages;
    int originIndex=0,anchor=0,gesturePage=0;
    bool wrapAround=false,fitWindows=true;
    std::vector<Prepared> prepared;
    RECT monitor{},workArea{};
    double started=0,released=0,base=0,position=0,paintedPosition=0,gestureTravel=0;
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
    AppWindow appAt(int page) const {
        if(order.empty()) return {};
        int index=originIndex+page,n=int(order.size());
        if(wrapAround && n>1) index=(index%n+n)%n;
        return index>=0 && index<n ? order[index] : AppWindow{};
    }
    bool ensurePage(int number) {
        if(std::any_of(pages.begin(),pages.end(),[&](const Page& p) { return p.number==number; })) return true;
        AppWindow app=appAt(number); if(!app.hwnd) return true;
        if(!app.valid()) return false;
        Page page; page.number=number; page.app=app; page.view=std::make_unique<LiveView>();
        if(!page.view->open(overlay,app.hwnd)) return false;
        if(!state(app.hwnd)) {
            Prepared p; p.app=app;
            if(!GetWindowPlacement(app.hwnd,&p.saved)) return false;
            p.minimized=IsIconic(app.hwnd)!=FALSE;
            p.maximized=IsZoomed(app.hwnd)!=FALSE || (p.minimized && (p.saved.flags&WPF_RESTORETOMAXIMIZED));
            LONG_PTR style=GetWindowLongPtrW(app.hwnd,GWL_STYLE);
            p.fit=app.hwnd!=origin() && fitWindows && !p.maximized && (style&WS_MAXIMIZEBOX) && (style&WS_THICKFRAME);
            p.initial=page.view->rectangle(); p.target=p.fit ? workArea : p.initial;
            prepared.push_back(p);
        }
        pages.push_back(std::move(page)); return true;
    }
    RECT frame(const Page& page) {
        Prepared* p=state(page.app.hwnd);
        return p ? (p->fit ? p->target : p->initial) : page.view->rectangle();
    }
    void trimPages() {
        int left=int(std::floor(-position))-1,right=int(std::ceil(-position))+1;
        pages.erase(std::remove_if(pages.begin(),pages.end(),[&](const Page& p) {
            // Raw input can advance before the frame callback. Never unregister
            // a surface that is still visible in the last submitted frame.
            return (p.number<left || p.number>right) && std::abs(paintedPosition+p.number)>=1.;
        }),pages.end());
    }
    bool syncPages() {
        int left=int(std::floor(-position))-1,right=int(std::ceil(-position))+1;
        // Keep visible surfaces alive while new neighbors enter the rolling strip.
        for(int i=left;i<=right;i++) if(!ensurePage(i)) return false;
        trimPages();
        return true;
    }
    bool paint() {
        if(!syncPages()) return false;
        for(auto& page:pages) {
            if(!page.app.valid() || !page.view->geometry()) return false;
            Prepared* p=state(page.app.hwnd);
            if(p && p->requested && p->layoutReady()) {
                if(p->fit) p->target=page.view->rectangle();
                if(!p->readyAt) p->readyAt=now();
            }
            if(!page.view->paint(int(std::lround((position+page.number)*width(monitor))),monitor,frame(page))) return false;
        }
        paintedPosition=position;
        trimPages();
        return true;
    }
    double lower() const { return wrapAround && order.size()>1 ? -1000000. : -double(int(order.size())-1-originIndex); }
    double upper() const { return wrapAround && order.size()>1 ? 1000000. : double(originIndex); }
    int boundPage(int page) const { return wrapAround && order.size()>1 ? page : std::max(-originIndex,std::min(int(order.size())-1-originIndex,page)); }
    bool ownsForeground(HWND foreground) const {
        if(foreground==origin() || (activated && foreground==activated)) return true;
        return std::any_of(prepared.begin(),prepared.end(),[&](const Prepared& p) { return p.changed && foreground==p.app.hwnd; });
    }
    void prepareVisible() {
        for(auto& page:pages) if(std::abs(position+page.number)<.99975)
            if(auto p=state(page.app.hwnd)) p->prepare();
        HWND foreground=GetForegroundWindow();
        if(!activationSent && foreground!=origin() && foreground!=activated && ownsForeground(foreground))
            activate(activated ? activated : origin(),foreground);
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
        // The cover is a compositor surface, not a new app window to animate in.
        BOOL disable=TRUE;
        DwmSetWindowAttribute(overlay,DWMWA_TRANSITIONS_FORCEDISABLED,&disable,sizeof(disable));
    }
    ~Transition() { finish(false); DestroyWindow(overlay); }
    bool active() const { return running; }
    bool isPrimed() const { return primed; }
    bool isSettling() const { return settling; }
    HWND origin() const { return appAt(0).hwnd; }
    HWND destinationWindow() const { return appAt(selected).hwnd; }
    int destinationPage() const { return selected; }
    size_t surfaceCount() const { return pages.size(); }
    double progress() const { return position; }
    double renderedProgress() const { return paintedPosition; }
    double plannedDuration() const { return motion.duration(); }
    RECT viewport() const { return monitor; }
    RECT previewRectangle(int slot) {
        auto it=std::find_if(pages.begin(),pages.end(),[&](const Page& p) { return p.number==slot-1; });
        return it!=pages.end() ? frame(*it) : RECT{};
    }
    bool targetPrepared() {
        auto p=state(appAt(selected).hwnd); return p && p->layoutReady();
    }
    void discard() {
        if(running) return;
        primed=false; pages.clear(); prepared.clear(); order.clear();
    }
    bool prime(const std::vector<AppWindow>& windows,int index,bool wrap=false,bool fitIncoming=true) {
        if(running || windows.empty() || index<0 || index>=int(windows.size())) return false;
        discard();
        order=windows; originIndex=index; wrapAround=wrap; fitWindows=fitIncoming;
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(origin(),MONITOR_DEFAULTTOPRIMARY),&mi);
        RECT source{}; DwmGetWindowAttribute(origin(),DWMWA_EXTENDED_FRAME_BOUNDS,&source,sizeof(source));
        bool fullscreen=source.left<=mi.rcMonitor.left && source.top<=mi.rcMonitor.top &&
            source.right>=mi.rcMonitor.right && source.bottom>=mi.rcMonitor.bottom;
        monitor=fullscreen ? mi.rcMonitor : mi.rcWork; workArea=mi.rcWork;
        SetWindowPos(overlay,HWND_TOPMOST,monitor.left,monitor.top,width(monitor),height(monitor),SWP_NOACTIVATE);
        BOOL animations=TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0); animate=animations!=FALSE;
        for(int i=-1;i<=1;i++) if(!ensurePage(i)) { discard(); return false; }
        base=position=paintedPosition=0; primed=true; return true;
    }
    bool begin() {
        if(running || !primed || GetForegroundWindow()!=origin()) return false;
        for(const auto& p:pages) if(!p.app.valid()) { discard(); return false; }
        started=now(); released=0; selected=presented=anchor=gesturePage=0; gestureTravel=0; intent.reset();
        settling=activationSent=endpointSubmitted=false; activated=nullptr; running=true; primed=false;
        if(!paint()) { finish(false); return false; }
        ShowWindow(overlay,SW_SHOWNOACTIVATE); UpdateWindow(overlay); return true;
    }
    bool start(const std::vector<AppWindow>& windows,int index,bool wrap=false,bool fitIncoming=true) {
        return prime(windows,index,wrap,fitIncoming) && begin();
    }
    void grab(double time) {
        if(!running) return;
        // Catch the pixels in place but advance the gesture's logical origin to
        // the pending destination. Another flick can therefore reach the next app.
        anchor=settling ? selected : boundPage(int(std::lround(-paintedPosition)));
        position=paintedPosition; base=rawPosition(position,lower(),upper());
        started=time; settling=activationSent=endpointSubmitted=false; selected=anchor; gesturePage=0; gestureTravel=0; intent.reset();
    }
    void move(double progress,double speed) {
        if(!running) return;
        position=dragPosition(base+progress,lower(),upper()); started=now();
        gestureTravel=progress;
        int whole=int(std::trunc(-progress));
        if(whole!=gesturePage) { gesturePage=whole; intent.reset(); }
        intent.update(progress+gesturePage,speed);
        // Dispatch the asynchronous layout request on the first directional input;
        // do not wait for a compositor callback or a commit decision to begin it.
        if(!syncPages()) { finish(false); return; }
        prepareVisible();
    }
    void release(double speed,bool cancel,double time) {
        if(!running) return;
        selected=(cancel || modifiers()) ? 0 : boundPage(anchor+gesturePage+intent.update(gestureTravel+gesturePage,speed));
        if((position<lower() && speed<0) || (position>upper() && speed>0)) speed=0;
        // Start at the last submitted visual position, not an input sample that
        // has never appeared on screen. Keep both pages on the same trajectory.
        position=paintedPosition;
        released=time; settling=true; activationSent=endpointSubmitted=false;
        motion.begin(position,-selected,speed,time,animate);
        if(!ensurePage(selected)) { finish(false); return; }
        auto chosen=state(appAt(selected).hwnd);
        lastPreparedBeforeRelease=chosen && chosen->layoutReady();
        lastPrepareMs=chosen && chosen->readyAt ? (chosen->readyAt-chosen->requestedAt)*1000 : 0;
        // Layout stays prepared, but focus changes only near the end of the slide.
        // Do not roll back a still-visible outgoing preview at release.
        if(chosen && selected) chosen->prepare();
    }
    void tick(double time) {
        if(!running) return;
        if(GetAsyncKeyState(VK_ESCAPE)&0x8000 || time-started>12) { finish(false); return; }
        if(!appAt(0).valid()) { finish(false); return; }
        if(!ownsForeground(GetForegroundWindow())) { finish(false); return; }
        if(settling) {
            position=motion.value(time);
            if(motion.done(time) || time-released>=std::max(0.,motion.duration()-.035))
                beginActivation(appAt(selected).hwnd);
        }
        if(!running) return;
        if(!syncPages()) { finish(false); return; }
        prepareVisible();
        if(!paint()) { finish(false); return; }
        presented++;
        if(settling && motion.done(time)) {
            // DwmFlush paces the next tick. Give the submitted endpoint a full
            // compositor opportunity before hiding the live outgoing surface.
            if(!endpointSubmitted) { endpointSubmitted=true; return; }
            HWND target=appAt(selected).hwnd;
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
        HWND chosen=committed ? appAt(selected).hwnd : origin();
        HWND foreground=GetForegroundWindow();
        if(!committed && foreground!=chosen && ownsForeground(foreground)) activate(chosen,foreground);
        for(auto& p:prepared) if(p.app.hwnd!=chosen) p.rollback();
        ShowWindow(overlay,SW_HIDE); discard(); settling=false;
        if(ended) ended();
    }
};
}
