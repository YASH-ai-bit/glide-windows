#include "glide.cpp"
#include <iostream>
#include <sstream>

using namespace glide;
namespace {
int assertions=0;
void check(bool value,const char* description) { if(!value) throw std::runtime_error(description); assertions++; }
Frame frame(double t,double x,double y,int count) { Frame f; f.time=t; f.count=f.reported=count; for(int i=0;i<count;i++) f.contacts[i]={uint32_t(i),x+.02*i,y}; return f; }
void unitTests() {
    check(sizeof(HIDP_VALUE_CAPS)==72,"HID descriptor layout");
    check(sizeof(RAWINPUTHEADER)==24,"x64 raw input header");
    check(destination(-.05,-3)==1,"flick commits"); check(destination(-.05,0)==0,"short slow drag cancels");
    check(destination(-.4,4)==0,"reversing velocity cancels forward intent");
    check(destination(.6,0)==-1,"right swipe selects previous");
    for(double position:{-.95,-.7,-.3,-.05,.15,.7}) for(double target:{-1.,0.,1.}) for(double velocity:{-5.,0.,5.}) {
        ReleaseMotion motion; motion.begin(position,target,velocity,10);
        check(motion.duration()<=.550,"release duration remains finite even for two-screen synthetic travel");
        double prior=position;
        for(int i=0;i<=20;i++) {
            double value=motion.value(10+motion.duration()*i/20.);
            check(value>=std::min(position,target)-.015 && value<=std::max(position,target)+.015,"opposing momentum has a bounded excursion");
            if(velocity*(target-position)>=0) check((value-prior)*(target-position)>=-1e-8,"forward release stays monotonic");
            if(i) check(std::abs(value-prior)/(motion.duration()/20)<=std::max(6.5,std::abs(velocity))+.001,"release does not fast-forward beyond the speed budget");
            prior=value;
        }
        check(std::abs(motion.value(10+motion.duration()+.001)-target)<1e-12,"exact release endpoint");
    }
    CommitIntent intent;
    check(intent.update(-.08,-3)==1,"velocity projects commit during the drag");
    check(intent.update(-.24,0)==1,"intent survives small velocity changes");
    check(intent.update(-.22,2)==0,"deliberate reversal cancels projected intent");
    check(intent.update(-.08,0)==0,"short stationary drag does not commit");
    ReleaseMotion almostDone; almostDone.begin(-.97,-1,0,0);
    check(almostDone.duration()<.100,"nearly complete drag has no long release dwell");
    RECT clipped{},clippedSource{};
    check(cropToRect({-50,0,1050,500},{0,0,1100,500},{400,0,1000,500},clipped,clippedSource) && clipped.left==400 && clippedSource.left==450,"oversized source is clipped to its page, preventing overlap");
    ReleaseMotion reverse; reverse.begin(-.4,0,-.4,10);
    check(reverse.value(10.001)<-.4,"reverse release preserves initial momentum before turning");
    check(std::abs((reverse.value(10.00001)+.4)/.00001+.4)<.01,"initial velocity is continuous for a gentle reversal");
    reverse.begin(-.4,-1,-2,10,false); check(reverse.done(10) && reverse.value(10)==-1,"reduced animation reaches the endpoint immediately");
    for(double lower:{-1.,0.}) for(double upper:{0.,1.}) {
        double prior=-100;
        for(int i=-80;i<=80;i++) {
            double raw=i/20.,shown=dragPosition(raw,lower,upper);
            check(shown>lower-.12 && shown<upper+.12,"elastic ends stay bounded");
            check(shown>=prior,"edge resistance never reverses finger movement"); prior=shown;
            check(std::abs(rawPosition(shown,lower,upper)-raw)<1e-7,"catching an elastic edge does not jump");
        }
    }
    Velocity v; v.add(1,0); v.add(1.02,.04); v.add(1.04,.08); check(std::abs(v.value(1.04)-2)<.001,"velocity from input history"); check(v.value(1.2)==0,"stale velocity cleared");
    v.clear(); v.add(1,0); v.add(1.02,-.04); v.add(1.04,-.08); v.add(1.06,-.04); v.add(1.08,0);
    check(v.value(1.08)>0,"recent reversal outweighs old velocity");
    Gesture g; check(!g.feed(frame(0,.5,.5,2)).begin,"two fingers ignored"); g.feed(frame(.01,0,0,0));
    check(g.feed(frame(.02,.5,.5,3)).touchBegin,"three contacts arm");
    check(!g.feed(frame(.03,.497,.5,3)).begin,"jitter ignored");
    auto event=g.feed(frame(.04,.4,.5,3)); check(event.begin && event.move && event.progress<0,"horizontal swipe starts");
    event=g.feed(frame(.05,.4,.5,4)); check(event.end && event.cancel,"fourth finger cancels");
    g.reset(); g.feed(frame(1,.5,.5,3)); g.feed(frame(1.02,.5,.65,3)); check(!g.feed(frame(1.04,.2,.65,3)).begin,"vertical swipe remains rejected");
    g.reset(); g.feed(frame(2,.5,.5,3)); g.feed(frame(2.02,.4,.5,3)); event=g.feed(frame(2.2,0,0,0)); check(event.end && event.velocity==0,"holding before lift removes stale momentum");
    g.reset(); g.feed(frame(3,.5,.5,3)); event=g.feed(frame(3.01,.4895,.5,3));
    check(event.begin && std::abs(event.progress)<.002,"recognition threshold does not jump the window");
    g.reset(); g.feed(frame(4,.5,.5,3)); g.capture(); event=g.feed(frame(4.01,.499,.5,3));
    check(event.move && !event.begin && event.progress<0,"caught animation responds below the recognition threshold");
    FrameAssembler assembler; Frame out,part; part.contacts[0]={1,.1,.2}; part.count=1; uint32_t id=1;
    check(!assembler.add(1,3,part,&id,1,out),"hybrid waits for contacts");
    part.contacts[0].id=id=2; check(!assembler.add(1,0,part,&id,1,out),"hybrid second packet partial");
    part.contacts[0].id=id=3; check(assembler.add(1,0,part,&id,1,out) && out.count==3,"hybrid complete frame assembled");
    RECT dest{},source{}; check(cropRect({-200,0,800,500},{8,8,1008,508},1000,500,dest,source) && source.left==208,"crop tracks sliding source");
    check(!cropRect({1000,0,2000,500},{0,0,1000,500},1000,500,dest,source),"offscreen view hidden");
    std::cout<<"PASS: "<<assertions<<" native assertions\n";
}
constexpr UINT SetColor=WM_APP+100;
LRESULT CALLBACK hostProc(HWND w,UINT m,WPARAM p,LPARAM l) {
    if(m==WM_SIZE) SetPropW(w,L"Glide.LastSize",reinterpret_cast<HANDLE>(UINT_PTR(GetTickCount64())));
    if(m==SetColor) { SetWindowLongPtrW(w,GWLP_USERDATA,LONG_PTR(p)); InvalidateRect(w,nullptr,FALSE); return 0; }
    if(m==WM_PAINT) { PAINTSTRUCT ps; HDC dc=BeginPaint(w,&ps); RECT r; GetClientRect(w,&r); HBRUSH b=CreateSolidBrush(COLORREF(GetWindowLongPtrW(w,GWLP_USERDATA))); FillRect(dc,&r,b); DeleteObject(b); EndPaint(w,&ps); return 0; }
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_CLOSE) { DestroyWindow(w); return 0; }
    if(m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(w,m,p,l);
}
int host(int color,bool minimized,bool maximized) {
    WNDCLASSW wc{}; wc.lpfnWndProc=hostProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"GlideNativeFixture"; RegisterClassW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,L"Glide native integration fixture",WS_OVERLAPPEDWINDOW,40,40,820,620,nullptr,nullptr,wc.hInstance,nullptr);
    SetWindowLongPtrW(w,GWLP_USERDATA,color); ShowWindow(w,maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL); UpdateWindow(w);
    if(minimized) ShowWindow(w,SW_SHOWMINNOACTIVE);
    std::cout<<UINT_PTR(w)<<std::endl;
    MSG m{}; while(GetMessageW(&m,nullptr,0,0)>0) { TranslateMessage(&m); DispatchMessageW(&m); } return 0;
}
struct Fixture {
    PROCESS_INFORMATION process{}; HWND hwnd=nullptr;
    Fixture(int color,bool minimized,bool maximized) {
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE read=nullptr,write=nullptr;
        if(!CreatePipe(&read,&write,&sa,0)) throw std::runtime_error("fixture stdout pipe"); SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
        HANDLE nul=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=si.hStdError=write; si.hStdInput=nul;
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr,path,MAX_PATH);
        std::wstring command=L"\""+std::wstring(path)+L"\" --host "+std::to_wstring(color)+L" "+std::to_wstring(minimized)+L" "+std::to_wstring(maximized);
        BOOL started=CreateProcessW(nullptr,&command[0],nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&process);
        CloseHandle(write); CloseHandle(nul); if(!started) { CloseHandle(read); throw std::runtime_error("fixture process"); }
        std::string line; double deadline=now()+5;
        while(now()<deadline) { DWORD available=0; PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr); if(available) { char c=0; DWORD n=0; ReadFile(read,&c,1,&n,nullptr); if(c=='\n') break; line+=c; } else Sleep(2); }
        CloseHandle(read); if(line.empty()) throw std::runtime_error("fixture did not provide HWND"); hwnd=reinterpret_cast<HWND>(std::stoull(line));
    }
    ~Fixture() { if(hwnd) PostMessageW(hwnd,WM_CLOSE,0,0); if(process.hProcess) { if(WaitForSingleObject(process.hProcess,1500)!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,1); CloseHandle(process.hProcess); CloseHandle(process.hThread); } }
    AppWindow app() const { return {hwnd,process.dwProcessId,L"Native test window"}; }
};
class Harness {
    HWND receiver=nullptr;
    UINT shellMessage=RegisterWindowMessageW(L"SHELLHOOK");
    std::unique_ptr<FrameClock> clock;
    static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM p,LPARAM l) {
        auto self=reinterpret_cast<Harness*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(m==WM_NCCREATE) { self=static_cast<Harness*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if(m==FrameMessage && self) {
            self->clock->pending=false; self->transition.tick(now());
            if(self->transition.active() && self->transition.isSettling() && std::abs(self->transition.progress())==1) self->sawEndpoint=true;
            return 0;
        }
        if(self && m==self->shellMessage) { if(p==HSHELL_FLASH) self->flashes.push_back(reinterpret_cast<HWND>(l)); return 0; }
        return DefWindowProcW(w,m,p,l);
    }
public:
    Transition transition;
    bool sawEndpoint=false;
    std::vector<HWND> flashes;
    bool shellWatching=false;
    Harness() {
        WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"GlideNativeTestPump"; RegisterClassW(&wc);
        receiver=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);
        shellWatching=RegisterShellHookWindow(receiver)!=FALSE;
        clock=std::make_unique<FrameClock>(receiver,FrameMessage); transition.ended=[this] { clock->enable(false); };
    }
    ~Harness() { transition.ended=nullptr; transition.finish(false); clock.reset(); if(shellWatching) DeregisterShellHookWindow(receiver); DestroyWindow(receiver); }
    void pump(int milliseconds) {
        double until=now()+milliseconds/1000.; MSG m{};
        while(now()<until) { while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(1); }
    }
    void start(const std::vector<AppWindow>& windows,int index,bool wrap=false,bool fit=true) { sawEndpoint=false; check(transition.start(windows,index,wrap,fit),"transition starts"); clock->enable(true); }
    bool waitClosed(int limit=500) { double end=now()+limit/1000.; while(transition.active() && now()<end) pump(2); return !transition.active(); }
};
COLORREF pixel(int x,int y) { HDC dc=GetDC(nullptr); COLORREF color=GetPixel(dc,x,y); ReleaseDC(nullptr,dc); return color; }
bool blue(COLORREF c) { return GetBValue(c)>200 && GetRValue(c)<40; }
void integration() {
    Fixture first(RGB(255,0,0),false,true),second(RGB(0,0,255),true,true),third(RGB(0,180,0),true,false);
    Harness h; auto windows=std::vector<AppWindow>{first.app(),second.app(),third.app()};
    h.pump(150); activate(first.hwnd,GetForegroundWindow()); h.pump(120);
    check(GetForegroundWindow()==first.hwnd,"test source focused");
    FLASHWINFO flash{sizeof(flash),second.hwnd,FLASHW_TRAY|FLASHW_TIMER,0,25}; FlashWindowEx(&flash); h.pump(80);
    bool flashObserver=h.shellWatching && std::find(h.flashes.begin(),h.flashes.end(),second.hwnd)!=h.flashes.end();
    flash.dwFlags=FLASHW_STOP; FlashWindowEx(&flash); h.pump(50); h.flashes.clear(); focusDiagnostics={};
    WINDOWPLACEMENT beforeSecond{sizeof(beforeSecond)},beforeThird{sizeof(beforeThird)};
    GetWindowPlacement(second.hwnd,&beforeSecond); GetWindowPlacement(third.hwnd,&beforeThird);
    Catalog catalog; auto all=catalog.read();
    check(std::any_of(all.begin(),all.end(),[&](const AppWindow& a) { return a.hwnd==second.hwnd; }),"catalog includes minimized apps");
    auto visible=catalog.read(false);
    check(std::none_of(visible.begin(),visible.end(),[&](const AppWindow& a) { return a.hwnd==second.hwnd || a.hwnd==third.hwnd; }),"leave-minimized policy excludes minimized swipe destinations");
    h.start({first.app()},0); h.transition.move(-.4,-2); h.pump(60);
    check(IsIconic(second.hwnd) && IsIconic(third.hwnd),"default behavior does not wake minimized applications");
    check(std::abs(h.transition.progress())<.12,"one-window gesture resists at the edge");
    h.transition.release(-2,false,now()); check(h.waitClosed(),"one-window edge returns promptly");
    check(GetForegroundWindow()==first.hwnd,"one-window gesture keeps focus");
    h.start({third.app(),first.app(),second.app()},1); h.transition.move(-.25,-1); h.pump(140);
    check(h.transition.active(),"transition stays active while minimized neighbors restore");
    check(!IsIconic(second.hwnd),"minimized neighbor restored behind transition");
    check(IsIconic(third.hwnd)!=FALSE,"opposite neighbor is not restored speculatively");
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(first.hwnd,MONITOR_DEFAULTTOPRIMARY),&mi);
    RECT viewport=h.transition.viewport(); check(EqualRect(&viewport,&mi.rcWork)!=FALSE,"normal app swipes preserve the taskbar work area");
    check(IsZoomed(second.hwnd)!=FALSE,"maximized restore state preserved");
    RECT leaving=h.transition.previewRectangle(1),sourceBounds{};
    DwmGetWindowAttribute(first.hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&sourceBounds,sizeof(sourceBounds));
    check(EqualRect(&leaving,&sourceBounds)!=FALSE,"leaving page keeps its original size during dragging");
    int w=GetSystemMetrics(SM_CXSCREEN),y=GetSystemMetrics(SM_CYSCREEN)/2;
    check(blue(pixel(int(w*.9),y)),"restored incoming window renders live");
    SendMessageW(second.hwnd,SetColor,RGB(0,255,255),0); h.pump(60);
    COLORREF cyan=pixel(int(w*.9),y); check(GetBValue(cyan)>200 && GetGValue(cyan)>200,"restored app keeps rendering during swipe");
    h.transition.release(0,true,now()); h.pump(35);
    check(h.transition.active() && !IsIconic(second.hwnd),"cancel keeps the departing neighbor live until it leaves the screen");
    check(h.waitClosed(),"cancel completes"); h.pump(100);
    check(IsIconic(second.hwnd) && IsIconic(third.hwnd),"cancel re-minimizes temporary neighbors");
    check(GetForegroundWindow()==first.hwnd,"cancel keeps original app focused");
    WINDOWPLACEMENT after{sizeof(after)}; GetWindowPlacement(second.hwnd,&after);
    check(EqualRect(&beforeSecond.rcNormalPosition,&after.rcNormalPosition)!=FALSE,"cancel preserves window placement");
    check((after.flags&WPF_RESTORETOMAXIMIZED)==(beforeSecond.flags&WPF_RESTORETOMAXIMIZED),"cancel preserves maximize-on-restore flag");
    h.start(windows,0); h.transition.move(-.36,-2); h.pump(100);
    double lastDrag=h.transition.renderedProgress();
    h.transition.move(-.40,-2); // Input arrives after the last submitted drag frame.
    h.transition.release(-2,false,now());
    check(h.transition.progress()==lastDrag,"release starts at the submitted frame without jumping to unseen input");
    h.pump(35); leaving=h.transition.previewRectangle(1);
    check(h.transition.active() && EqualRect(&leaving,&sourceBounds),"outgoing window retains its size through release");
    check(h.waitClosed(),"minimized selection completes"); h.pump(40);
    check(h.sawEndpoint,"final page is submitted for a compositor cycle before cover removal");
    check(GetForegroundWindow()==second.hwnd && !IsIconic(second.hwnd),"selected minimized app is restored and focused");
    check(IsIconic(third.hwnd)!=FALSE,"unselected minimized app returns to taskbar");
    check(h.transition.lastReleaseMs<230,"complete restore/focus handoff under 230 ms");
    std::cout<<"Minimized restore release: "<<h.transition.lastReleaseMs<<" ms\n";
    std::vector<double> timings;
    for(int i=0;i<6;i++) {
        int index=GetForegroundWindow()==first.hwnd ? 0 : 1; int direction=index==0 ? 1 : -1;
        h.start(windows,index); h.transition.move(-direction*.4,-direction*2.5); h.pump(35);
        h.transition.release(-direction*2.5,false,now()); check(h.waitClosed(),"repeat swipe completes");
        check(GetForegroundWindow()==windows[index+direction].hwnd,"repeat swipe focuses chosen app"); timings.push_back(h.transition.lastReleaseMs);
    }
    std::sort(timings.begin(),timings.end());
    double median=(timings[(timings.size()-1)/2]+timings[timings.size()/2])/2;
    std::cout<<"Release-to-focused-app median="<<median<<" ms; worst="<<timings.back()<<" ms\n";
    check(timings.back()<230,"release latency remains bounded");
    int current=GetForegroundWindow()==first.hwnd ? 0 : 1;
    h.start(windows,current); h.transition.move(-.4,-2); h.pump(35); h.transition.release(-2,false,now()); h.pump(35);
    check(GetForegroundWindow()==windows[current].hwnd,"outgoing app retains focus in the early release frames");
    double displayed=h.transition.renderedProgress(); h.transition.grab(now()); double caught=h.transition.progress();
    check(std::abs(caught-displayed)<1e-12,"regrab catches the rendered position without extrapolating ahead");
    check(std::abs(caught)>.1,"regrab retains in-flight position");
    h.transition.move(.35,2); h.transition.release(0,true,now()); check(h.waitClosed(),"regrab and reverse complete");
    check(GetForegroundWindow()==windows[current].hwnd,"regrab cancellation returns to origin");
    check(IsIconic(third.hwnd)!=FALSE,"regrab cancellation restores minimized state");
    WINDOWPLACEMENT restoredThird{sizeof(restoredThird)}; GetWindowPlacement(third.hwnd,&restoredThird);
    check((restoredThird.flags&WPF_RESTORETOMAXIMIZED)==(beforeThird.flags&WPF_RESTORETOMAXIMIZED),"fitting a minimized normal window does not change its restore preference on cancel");
    activate(first.hwnd,GetForegroundWindow()); h.pump(40);
    h.start(windows,0); h.transition.move(.7,3); h.pump(50);
    check(h.transition.progress()>0 && h.transition.progress()<.12,"first window has an elastic left boundary");
    h.transition.release(3,false,now()); check(h.waitClosed(),"edge release completes");
    check(GetForegroundWindow()==first.hwnd,"default strip never wraps to the opposite end");
    h.start(windows,0,true); h.transition.move(.4,2); h.pump(100); h.transition.release(2,false,now());
    check(h.waitClosed() && GetForegroundWindow()==third.hwnd,"optional wrapping reaches the last window");
    WINDOWPLACEMENT wrapped{sizeof(wrapped)}; GetWindowPlacement(third.hwnd,&wrapped);
    check(EqualRect(&beforeThird.rcNormalPosition,&wrapped.rcNormalPosition)!=FALSE,"wrapped minimized restore preserves normal bounds");
    activate(first.hwnd,GetForegroundWindow()); h.pump(40);
    Fixture outsider(RGB(170,120,10),false,false); h.pump(60); activate(first.hwnd,GetForegroundWindow()); h.pump(40);
    h.start(windows,0); h.transition.move(-.2,-1); h.pump(40);
    activate(outsider.hwnd,GetForegroundWindow()); h.pump(60);
    check(!h.transition.active() && GetForegroundWindow()==outsider.hwnd,"external focus change cancels without stealing focus back");
    activate(first.hwnd,GetForegroundWindow()); h.pump(40);
    WINDOWPLACEMENT beforeFit{sizeof(beforeFit)}; GetWindowPlacement(outsider.hwnd,&beforeFit);
    std::vector<AppWindow> fitWindows{first.app(),outsider.app()};
    check(h.transition.prime(fitWindows,0),"touch-down primes live views"); h.pump(50);
    check(h.transition.isPrimed() && !h.transition.active() && !IsZoomed(outsider.hwnd),"priming alone does not resize or show a transition");
    h.transition.discard();
    h.start(fitWindows,0); h.transition.move(-.08,0);
    // Deliberately don't dispatch animation messages: input itself must initiate fit.
    double fitDeadline=now()+.080;
    while(!IsZoomed(outsider.hwnd) && now()<fitDeadline) Sleep(1);
    check(IsZoomed(outsider.hwnd)!=FALSE,"layout begins before the first animation callback is dispatched");
    h.pump(130);
    check(IsZoomed(outsider.hwnd)!=FALSE,"incoming normal window is fitted while fingers are still down before commit intent");
    RECT early=h.transition.previewRectangle(2);
    check(width(early)>width(mi.rcWork)*.98,"incoming preview is already fitted at the beginning of the drag");
    h.transition.move(-.31,0); h.pump(40); RECT fitted=h.transition.previewRectangle(2);
    RECT actual{}; DwmGetWindowAttribute(outsider.hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&actual,sizeof(actual));
    check(EqualRect(&fitted,&actual)!=FALSE,"incoming page uses actual fitted geometry without a scale animation");
    check(EqualRect(&early,&fitted)!=FALSE,"incoming geometry is identical at early and late reveal");
    UINT_PTR sizeBeforeRelease=UINT_PTR(GetPropW(outsider.hwnd,L"Glide.LastSize"));
    h.transition.release(0,false,now()); check(h.transition.lastPreparedBeforeRelease,"layout was ready before the user committed");
    double preparedMs=h.transition.lastPrepareMs;
    check(h.waitClosed() && GetForegroundWindow()==outsider.hwnd,"prepared window receives focus"); h.pump(60);
    check(UINT_PTR(GetPropW(outsider.hwnd,L"Glide.LastSize"))==sizeBeforeRelease,"no second resize occurs at or after focus handoff");
    std::cout<<"Incoming layout ready after request="<<preparedMs<<" ms; prepared before release=1; post-release resizes=0\n";
    WINDOWPLACEMENT normal=beforeFit; normal.showCmd=SW_SHOWNOACTIVATE; SetWindowPlacement(outsider.hwnd,&normal); h.pump(80);
    activate(first.hwnd,GetForegroundWindow()); h.pump(40);
    h.start(fitWindows,0); h.transition.move(-.2,0); h.pump(80); check(IsZoomed(outsider.hwnd)!=FALSE,"cancel fixture was fitted during drag");
    h.transition.release(0,true,now()); check(h.waitClosed(),"fitted preview cancels"); h.pump(80);
    WINDOWPLACEMENT canceled{sizeof(canceled)}; GetWindowPlacement(outsider.hwnd,&canceled);
    check(!IsZoomed(outsider.hwnd) && EqualRect(&beforeFit.rcNormalPosition,&canceled.rcNormalPosition),"cancellation restores normal size and position");
    h.start(fitWindows,0,false,false); h.transition.move(-.2,0); h.pump(60);
    check(!IsZoomed(outsider.hwnd),"fit option can preserve normal window dimensions");
    h.transition.release(0,true,now()); check(h.waitClosed(),"unfitted preview cancels");
    // Three rapid flicks must advance across four apps without closing the cover
    // or snapping the visible strip to each intermediate destination.
    std::vector<AppWindow> queue{first.app(),second.app(),third.app(),outsider.app()};
    ShowWindow(third.hwnd,SW_SHOWMINNOACTIVE);
    activate(first.hwnd,GetForegroundWindow()); h.pump(60);
    check(IsIconic(third.hwnd)!=FALSE,"chain fixture starts minimized");
    h.start(queue,0);
    for(int page=1;page<=3;page++) {
        if(page>1) {
            double chainPosition=h.transition.renderedProgress();
            h.transition.grab(now());
            check(h.transition.progress()==chainPosition,"chained swipe preserves the displayed strip position");
        }
        h.transition.move(-.16,-3); h.pump(18); h.transition.release(-3,false,now());
        check(h.transition.destinationPage()==page && h.transition.destinationWindow()==queue[page].hwnd,"rapid flick advances to a new app in queue order");
        if(page<3) { h.pump(20); check(h.transition.active(),"chain accepts another swipe before settling"); }
    }
    check(h.waitClosed(1000) && GetForegroundWindow()==outsider.hwnd,"three rapid flicks reach the fourth app");
    h.pump(40);
    check(IsIconic(third.hwnd)!=FALSE,"passing a minimized app restores it after the chain ends");
    // A single long gesture can also traverse multiple pages and render both
    // neighbors at their shared boundary, with only a bounded set of surfaces.
    activate(first.hwnd,GetForegroundWindow()); h.pump(40); h.start(queue,0);
    h.transition.move(-2.4,-2);
    RECT retained=h.transition.previewRectangle(1);
    check(width(retained)>0 && h.transition.renderedProgress()==0,"new input retains the old visible surface until the replacement frame");
    h.pump(100);
    check(std::abs(h.transition.progress()+2.4)<1e-9,"long drag is not trapped at the first adjacent page");
    check(h.transition.surfaceCount()<=4,"rolling strip bounds the live surface count");
    check(GetGValue(pixel(int(w*.2),y))>140,"third app remains visible on the outgoing side of the strip");
    check(GetRValue(pixel(int(w*.85),y))>140,"fourth app is visible on the incoming side of the strip");
    h.transition.release(-2,false,now()); check(h.transition.destinationPage()==3,"long drag selects the fourth page");
    check(h.waitClosed() && GetForegroundWindow()==outsider.hwnd,"long drag commits to the fourth app");
    activate(first.hwnd,GetForegroundWindow()); h.pump(40); h.start(queue,0);
    h.transition.move(-.16,-3); h.pump(20); h.transition.release(-3,false,now()); h.pump(20);
    h.transition.grab(now()); h.transition.move(.16,3); h.pump(20); h.transition.release(3,false,now());
    check(h.transition.destinationPage()==0,"reverse flick returns toward the previous queue destination");
    check(h.waitClosed() && GetForegroundWindow()==first.hwnd,"rapid reverse keeps the original app");
    h.start(queue,0,true); h.transition.move(-4.35,-2); h.pump(80); h.transition.release(-2,false,now());
    check(h.transition.destinationPage()==5 && h.transition.destinationWindow()==second.hwnd,"rolling strip wraps beyond a complete queue traversal");
    check(h.waitClosed() && GetForegroundWindow()==second.hwnd,"wrapped strip focuses the correct app");
    activate(first.hwnd,GetForegroundWindow()); h.pump(40); h.start(queue,0);
    h.transition.move(-2.3,-2); h.pump(60); h.transition.release(0,true,now());
    check(h.waitClosed(1000) && GetForegroundWindow()==first.hwnd && IsIconic(third.hwnd),"canceling multi-page travel restores origin and minimized state");
    std::cout<<"PASS: four-app chaining, long drags, fixed geometry, reverse flicks, bounded surfaces and full-queue wrapping\n";
    unsigned fixtureFlashes=0;
    for(HWND flashed:h.flashes) if(flashed==first.hwnd || flashed==second.hwnd || flashed==third.hwnd || flashed==outsider.hwnd) fixtureFlashes++;
    std::cout<<"Focus requests="<<focusDiagnostics.attempts<<"; denied="<<focusDiagnostics.denied<<"; flash observer calibrated="<<flashObserver<<"; fixture flashes="<<fixtureFlashes<<"\n";
    check(focusDiagnostics.denied==0,"handoff avoids rejected foreground requests");
    if(flashObserver) check(fixtureFlashes==0,"swiping does not request taskbar attention flashes");
    std::cout<<"PASS: live rendering, early fitting, cancellation, minimized policies, edges, regrab, focus and latency\n";
}
void stopRunning() {
    HWND window=FindWindowW(L"GlideNativeControls",nullptr); if(!window) { std::cout<<"Glide is not running\n"; return; }
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process) throw std::runtime_error("cannot inspect running Glide");
    wchar_t path[MAX_PATH]{},expected[MAX_PATH]{}; DWORD count=MAX_PATH;
    GetFullPathNameW(L"Glide.exe",MAX_PATH,expected,nullptr);
    bool ours=QueryFullProcessImageNameW(process,0,path,&count) && _wcsicmp(path,expected)==0;
    if(!ours) { CloseHandle(process); throw std::runtime_error("running window is not this project's Glide executable"); }
    PostMessageW(window,WM_CLOSE,0,0); DWORD result=WaitForSingleObject(process,5000); CloseHandle(process);
    check(result==WAIT_OBJECT_0,"Glide closed gracefully"); std::cout<<"Stopped this project's Glide cleanly\n";
}
void diagnose() {
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"GlideNativeDiagnosis"; RegisterClassW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    { RawTouchpad pad(w); Catalog catalog; auto windows=catalog.read(); int minimized=0; for(const auto& a:windows) minimized+=IsIconic(a.hwnd)!=FALSE;
      std::cout<<"Precision Touchpads="<<pad.connected()<<"; app windows="<<windows.size()<<"; minimized included="<<minimized<<"; native swipe disabled="<<nativeDisabled()<<"\n"; }
    DestroyWindow(w);
}
void controlsImage(const wchar_t* path) {
    HWND window=FindWindowW(L"GlideNativeControls",nullptr);
    check(window!=nullptr,"native controls are running for visual verification");
    RECT bounds{}; GetWindowRect(window,&bounds); int w=width(bounds),h=height(bounds);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr); void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap) { DeleteDC(dc); throw std::runtime_error("controls bitmap allocation failed"); }
    HGDIOBJ previous=SelectObject(dc,bitmap);
    bool hidden=IsWindowVisible(window)==FALSE;
    if(hidden) { ShowWindow(window,SW_SHOWNOACTIVATE); RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW); DwmFlush(); }
    BOOL captured=PrintWindow(window,dc,2);
    if(hidden) ShowWindow(window,SW_HIDE);
    if(captured) {
        BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
        DWORD bytes=DWORD(w*h*4); header.bfSize=header.bfOffBits+bytes;
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD written=0;
        if(file!=INVALID_HANDLE_VALUE) {
            captured=WriteFile(file,&header,sizeof(header),&written,nullptr) && WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr) && WriteFile(file,pixels,bytes,&written,nullptr);
            CloseHandle(file);
        } else captured=FALSE;
    }
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    check(captured!=FALSE,"controls image was captured"); std::cout<<"Saved native controls image\n";
}
}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        if(argc>4 && wcscmp(argv[1],L"--host")==0) return host(_wtoi(argv[2]),_wtoi(argv[3])!=0,_wtoi(argv[4])!=0);
        if(argc>2 && wcscmp(argv[1],L"--controls-image")==0) controlsImage(argv[2]);
        else if(argc>1 && wcscmp(argv[1],L"--integration")==0) integration();
        else if(argc>1 && wcscmp(argv[1],L"--diagnose")==0) diagnose();
        else if(argc>1 && wcscmp(argv[1],L"--stop-running")==0) stopRunning();
        else unitTests();
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
