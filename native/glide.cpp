#include "touchpad.hpp"
#include "transition.hpp"
#include "settings.hpp"

namespace glide {
constexpr UINT FrameMessage=WM_APP+10,TrayMessage=WM_APP+11;
class Application {
    HWND window=nullptr,list=nullptr,status=nullptr,toggle=nullptr,telemetry=nullptr,slider=nullptr,minimizedOption=nullptr,wrapOption=nullptr,fitOption=nullptr;
    HFONT font=nullptr,heading=nullptr;
    HBRUSH background=CreateSolidBrush(RGB(17,21,28)),listBrush=CreateSolidBrush(RGB(27,34,45));
    NOTIFYICONDATAW tray{};
    std::unique_ptr<Transition> transition;
    std::unique_ptr<FrameClock> clock;
    std::unique_ptr<RawTouchpad> touchpad;
    Catalog catalog; Gesture gesture;
    Settings settings;
    bool enabled=false,closing=false;
    int dpi=96;
    std::wstring signature;
    int px(int value) const { return MulDiv(value,dpi,96); }
    HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
        HWND child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return child;
    }
    void show() { ShowWindow(window,SW_SHOWNORMAL); SetForegroundWindow(window); refresh(); }
    void setEnabled(bool value) {
        if(value && !nativeDisabled()) { SetWindowTextW(status,L"Set Windows Three-finger gestures > Swipes to Nothing first."); show(); return; }
        if(value && (!touchpad->connected() || GetSystemMetrics(SM_CMONITORS)!=1)) { SetWindowTextW(status,L"A Precision Touchpad and one connected display are required."); return; }
        enabled=value; gesture.reset();
        if(!enabled) transition->finish(false);
        SetWindowTextW(toggle,enabled ? L"Pause swipes" : L"Enable swipes");
        SetWindowTextW(status,enabled ? L"ACTIVE   Three-finger swipes switch between your open apps." : L"PAUSED   Your normal app controls are active.");
        wcscpy_s(tray.szTip,enabled ? L"Glide Native - app swipes active" : L"Glide Native - paused"); Shell_NotifyIconW(NIM_MODIFY,&tray);
    }
    bool primeCurrent() {
        auto windows=catalog.read(settings.includeMinimized); HWND current=GetForegroundWindow();
        auto it=std::find_if(windows.begin(),windows.end(),[&](const AppWindow& a) { return a.hwnd==current; });
        if(it==windows.end()) { transition->discard(); return false; }
        return transition->prime(windows,int(it-windows.begin()),settings.wrapAround,settings.fitIncoming);
    }
    void input(const Frame& f) {
        if(!enabled) { gesture.reset(); return; }
        auto e=gesture.feed(f);
        if(e.touchBegin && transition->active()) { transition->grab(f.time); gesture.capture(); }
        else if(e.touchBegin) primeCurrent();
        if(e.begin && !transition->active()) {
            bool ready=transition->isPrimed() && transition->origin()==GetForegroundWindow();
            if((ready || primeCurrent()) && transition->begin()) clock->enable(true);
        }
        if(e.move && transition->active()) transition->move(e.progress,e.velocity);
        if((e.end || e.touchEnd) && transition->active()) transition->release(e.velocity,e.cancel,f.time);
        else if(e.touchEnd) transition->discard();
    }
    void refresh() {
        if(!touchpad) return;
        wchar_t text[180]{};
        swprintf_s(text,L"Precision Touchpad: %s   |   %d fingers   |   %llu reports\nLast release-to-app: %.0f ms",touchpad->connected() ? L"connected" : L"not found",touchpad->fingers,touchpad->reports,transition->lastReleaseMs);
        SetWindowTextW(telemetry,text);
        if(!IsWindowVisible(window)) return;
        auto windows=catalog.read(); std::wstring state=settings.includeMinimized ? L"include" : L"skip";
        for(const auto& a:windows) state+=std::to_wstring(UINT_PTR(a.hwnd))+a.title+(IsIconic(a.hwnd) ? L"1" : L"0");
        if(state!=signature) {
            SendMessageW(list,LB_RESETCONTENT,0,0);
            for(const auto& a:windows) { std::wstring label=a.title+(IsIconic(a.hwnd) ? (settings.includeMinimized ? L"  (minimized, included)" : L"  (minimized, skipped)") : L""); SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())); }
            signature=std::move(state);
        }
    }
    void trayMenu() {
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,201,L"Open Glide"); AppendMenuW(menu,MF_STRING,202,enabled ? L"Pause" : L"Enable"); AppendMenuW(menu,MF_STRING,203,L"Quit");
        POINT p; GetCursorPos(&p); SetForegroundWindow(window);
        UINT result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr); DestroyMenu(menu);
        if(result==201) show(); if(result==202) setEnabled(!enabled); if(result==203) SendMessageW(window,WM_CLOSE,0,0);
    }
    static LRESULT CALLBACK proc(HWND w,UINT message,WPARAM p,LPARAM l) {
        Application* self=reinterpret_cast<Application*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(message==WM_NCCREATE) { self=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window=w; SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if(!self) return DefWindowProcW(w,message,p,l);
        try { return self->message(message,p,l); }
        catch(const std::exception& error) { std::string s=error.what(); log(std::wstring(s.begin(),s.end())); if(self->transition) self->setEnabled(false); return DefWindowProcW(w,message,p,l); }
    }
    LRESULT message(UINT m,WPARAM p,LPARAM l) {
        switch(m) {
        case WM_INPUT:
            if(touchpad) touchpad->read(reinterpret_cast<HRAWINPUT>(l),[this](const Frame& f) { input(f); });
            return DefWindowProcW(window,m,p,l);
        case WM_INPUT_DEVICE_CHANGE:
            if(touchpad) { touchpad->changed(reinterpret_cast<HANDLE>(l),p==GIDC_ARRIVAL); if(!touchpad->connected()) setEnabled(false); } return 0;
        case FrameMessage:
            if(clock) clock->pending=false;
            if(transition && transition->active()) transition->tick(now()); return 0;
        case WM_TIMER:
            if(gesture.stale(now()) && transition->active() && !transition->isSettling()) transition->release(0,true,now());
            if(enabled && !nativeDisabled()) setEnabled(false);
            refresh(); return 0;
        case WM_HOTKEY: if(p==2) setEnabled(false); else setEnabled(!enabled); return 0;
        case WM_COMMAND:
            if(LOWORD(p)==101) setEnabled(!enabled);
            if(LOWORD(p)==102) ShellExecuteW(window,L"open",L"ms-settings:devices-touchpad",nullptr,nullptr,SW_SHOWNORMAL);
            if(LOWORD(p)==103) ShowWindow(window,SW_HIDE);
            if(LOWORD(p)==104 || LOWORD(p)==105 || LOWORD(p)==106) {
                if(transition->active()) transition->finish(false);
                settings.includeMinimized=SendMessageW(minimizedOption,BM_GETCHECK,0,0)==BST_CHECKED;
                settings.wrapAround=SendMessageW(wrapOption,BM_GETCHECK,0,0)==BST_CHECKED;
                settings.fitIncoming=SendMessageW(fitOption,BM_GETCHECK,0,0)==BST_CHECKED;
                settings.save(); gesture.reset(); refresh();
            }
            return 0;
        case WM_HSCROLL:
            settings.sensitivity=int(SendMessageW(slider,TBM_GETPOS,0,0)); gesture.sensitivity=settings.sensitivity/100.0;
            if(LOWORD(p)==TB_ENDTRACK) settings.save(); return 0;
        case TrayMessage: if(l==WM_LBUTTONDBLCLK) show(); else if(l==WM_RBUTTONUP) trayMenu(); return 0;
        case WM_DISPLAYCHANGE: if(transition) transition->finish(false); if(GetSystemMetrics(SM_CMONITORS)!=1) setEnabled(false); return 0;
        case WM_CTLCOLORSTATIC:
            if(reinterpret_cast<HWND>(l)==minimizedOption || reinterpret_cast<HWND>(l)==wrapOption || reinterpret_cast<HWND>(l)==fitOption) {
                SetTextColor(reinterpret_cast<HDC>(p),GetSysColor(COLOR_BTNTEXT)); SetBkColor(reinterpret_cast<HDC>(p),GetSysColor(COLOR_BTNFACE)); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            SetTextColor(reinterpret_cast<HDC>(p),RGB(218,226,238)); SetBkColor(reinterpret_cast<HDC>(p),RGB(17,21,28)); return reinterpret_cast<LRESULT>(background);
        case WM_CTLCOLORLISTBOX: SetTextColor(reinterpret_cast<HDC>(p),RGB(218,226,238)); SetBkColor(reinterpret_cast<HDC>(p),RGB(27,34,45)); return reinterpret_cast<LRESULT>(listBrush);
        case WM_ERASEBKGND: { RECT r; GetClientRect(window,&r); FillRect(reinterpret_cast<HDC>(p),&r,background); return 1; }
        case WM_CLOSE:
            closing=true; settings.save(); setEnabled(false); Shell_NotifyIconW(NIM_DELETE,&tray); DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window,m,p,l);
    }
public:
    explicit Application(bool showControls=true) {
        settings.load(); gesture.sensitivity=settings.sensitivity/100.0;
        dpi=int(GetDpiForSystem());
        WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=proc; wc.lpszClassName=L"GlideNativeControls"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        RegisterClassW(&wc);
        DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        RECT r{0,0,px(580),px(660)}; AdjustWindowRectExForDpi(&r,style,FALSE,0,dpi);
        HWND w=CreateWindowExW(0,wc.lpszClassName,L"Glide Native - live app swipes",style,CW_USEDEFAULT,CW_USEDEFAULT,width(r),height(r),nullptr,nullptr,wc.hInstance,this);
        if(!w) throw std::runtime_error("Cannot create controls");
        font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        heading=CreateFontW(-px(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HWND title=control(L"STATIC",L"glide / native",0,24,18,530,40); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
        control(L"STATIC",L"Live windows, ready to fit while you swipe.",0,24,65,530,25);
        control(L"STATIC",L"Move three fingers sideways. Release to switch, or reverse to cancel.\nYou can catch an animation and continue dragging it.",0,24,99,530,45);
        status=control(L"STATIC",L"Starting...",0,24,154,530,34);
        list=control(L"LISTBOX",L"",LBS_NOINTEGRALHEIGHT|WS_VSCROLL,24,195,530,146);
        minimizedOption=control(L"BUTTON",L"Include minimized windows",BS_AUTOCHECKBOX|WS_TABSTOP,24,354,530,25,104);
        wrapOption=control(L"BUTTON",L"Wrap from the last window to the first",BS_AUTOCHECKBOX|WS_TABSTOP,24,384,530,25,105);
        fitOption=control(L"BUTTON",L"Fit incoming windows during the swipe",BS_AUTOCHECKBOX|WS_TABSTOP,24,414,530,25,106);
        SendMessageW(minimizedOption,BM_SETCHECK,settings.includeMinimized ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(wrapOption,BM_SETCHECK,settings.wrapAround ? BST_CHECKED : BST_UNCHECKED,0);
        SendMessageW(fitOption,BM_SETCHECK,settings.fitIncoming ? BST_CHECKED : BST_UNCHECKED,0);
        control(L"STATIC",L"Swipe sensitivity",0,24,452,150,28);
        slider=control(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS,175,445,379,35);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(100,300)); SendMessageW(slider,TBM_SETPOS,TRUE,settings.sensitivity);
        toggle=control(L"BUTTON",L"Enable swipes",BS_PUSHBUTTON|WS_TABSTOP,24,495,160,38,101);
        control(L"BUTTON",L"Touchpad settings",BS_PUSHBUTTON|WS_TABSTOP,196,495,188,38,102);
        control(L"BUTTON",L"Hide to tray",BS_PUSHBUTTON|WS_TABSTOP,396,495,158,38,103);
        telemetry=control(L"STATIC",L"",0,24,550,530,45);
        control(L"STATIC",L"Ctrl + Alt + G: toggle   |   Ctrl + Alt + Esc: pause\nEsc cancels a transition. Closing this window quits Glide.",0,24,610,530,42);
        transition=std::make_unique<Transition>(); clock=std::make_unique<FrameClock>(window,FrameMessage); touchpad=std::make_unique<RawTouchpad>(window);
        transition->ended=[this] { clock->enable(false); gesture.reset(); refresh(); };
        tray.cbSize=sizeof(tray); tray.hWnd=window; tray.uID=1; tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; tray.uCallbackMessage=TrayMessage; tray.hIcon=wc.hIcon; wcscpy_s(tray.szTip,L"Glide Native"); Shell_NotifyIconW(NIM_ADD,&tray);
        RegisterHotKey(window,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'G'); RegisterHotKey(window,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_ESCAPE);
        SetTimer(window,1,500,nullptr); setEnabled(nativeDisabled() && touchpad->connected()>0); refresh();
        ShowWindow(window,showControls ? SW_SHOWNORMAL : SW_HIDE); UpdateWindow(window);
        log(L"Native 0.7 started; enabled="+std::to_wstring(enabled)+L"; includeMinimized="+std::to_wstring(settings.includeMinimized)+L"; wrap="+std::to_wstring(settings.wrapAround)+L"; fit="+std::to_wstring(settings.fitIncoming)+L"; sensitivity="+std::to_wstring(settings.sensitivity));
    }
    ~Application() {
        if(transition) { transition->ended=nullptr; transition->finish(false); }
        clock.reset(); touchpad.reset(); transition.reset();
        Shell_NotifyIconW(NIM_DELETE,&tray); UnregisterHotKey(window,1); UnregisterHotKey(window,2);
        DeleteObject(font); DeleteObject(heading); DeleteObject(background); DeleteObject(listBrush);
    }
    int run() { MSG m; while(GetMessageW(&m,nullptr,0,0)>0) { if(!IsDialogMessageW(window,&m)) { TranslateMessage(&m); DispatchMessageW(&m); } } return int(m.wParam); }
};
}

#ifndef GLIDE_TEST_BUILD
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR arguments,int) {
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\Glide.NativeAppSwipes");
    if(GetLastError()==ERROR_ALREADY_EXISTS) { MessageBoxW(nullptr,L"Glide Native is already running. Open it from its tray icon.",L"Glide",MB_OK); CloseHandle(mutex); return 0; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES}; InitCommonControlsEx(&cc);
    int result=0; try { glide::Application app(wcscmp(arguments,L"--tray")!=0); result=app.run(); }
    catch(const std::exception& e) { std::string s=e.what(); std::wstring text(s.begin(),s.end()); glide::log(text); MessageBoxW(nullptr,text.c_str(),L"Glide",MB_ICONERROR); result=1; }
    CloseHandle(mutex); return result;
}
#endif
