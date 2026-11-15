# Swipe implementation research

References reviewed on 2026-11-13. Glide's Windows implementation is original C++; upstream source was studied for behavior and design, not copied or linked into the executable.

## Apple

[Apple's open-source index](https://opensource.apple.com/) and [release index](https://opensource.apple.com/releases/) were reviewed. No public Mission Control or macOS compositor source implementing the swipe transition was located in that review. This project does not contain Apple's implementation.

Apple's [Multi-Touch gesture guide](https://support.apple.com/en-gb/102482), [open windows guide](https://support.apple.com/en-hk/guide/mac-help/mchlb7beb9af/mac) and [Desktop & Dock settings](https://support.apple.com/en-gb/guide/mac-help/mchlp1119/mac) document horizontal swipes between desktops/full-screen apps, minimized windows in the Dock, and reopening them through the Dock. Glide leaves minimized windows alone by default and offers an option to include them in horizontal swipes.

Glide switches among individual Windows application windows. Its design draws on the Spaces gesture but does not implement Spaces, Mission Control, App Expose or the Dock. Bounded edge resistance, continuous interruption and finite release timing are original design choices. Their constants have not been measured against a reference Mac and are not claimed to be Apple's exact values.

## GNOME Shell

Source: [swipeTracker.js](https://github.com/GNOME/gnome-shell/blob/main/js/ui/swipeTracker.js).

The tracker separates continuous gesture progress from release handling, uses recent motion history to estimate velocity, projects a destination and bounds its release duration. It also carries progress into an interrupted gesture. These principles informed Glide's direct finger tracking, velocity-based destination selection and regrab behavior. Glide uses its own 60 ms weighted regression history and a finite release curve with an exact endpoint.

## KDE KWin

Source: [slide.cpp](https://github.com/KDE/kwin/blob/master/src/plugins/slide/slide.cpp).

KWin distinguishes active gestures from subsequent animation, updates gesture position directly and advances animation with the render clock. Its slide effect transforms and clips window views. Glide similarly separates input sampling from display-paced presentation, using Windows DWM live surfaces and a native frame worker. The Linux effect operates on virtual desktops; Glide applies the transition to individual existing application windows.

## Smooth Swipe modification for KWin

Sources: [project](https://github.com/cesp99/smooth-swipe), [smoothswipe.cpp](https://github.com/cesp99/smooth-swipe/blob/main/src/smoothswipe.cpp), [springmotion.cpp](https://github.com/cesp99/smooth-swipe/blob/main/src/springmotion.cpp).

The modification filters finger tracking with a stiff spring, preserves velocity into release and keeps the rendered position when a transition is interrupted. Its spring implementation stops according to position and velocity tolerances. Glide carries over the interruption and momentum concepts while using direct finger position and a finite cubic release. It catches the last submitted position and permits a small bounded continuation when momentum opposes the destination. No KWin or Smooth Swipe source is bundled with Glide.

## Windows integration

- [DWM thumbnail overview](https://learn.microsoft.com/en-us/windows/win32/dwm/thumbnail-ovw): live window representations supplied by Windows.
- [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush): pacing updates against compositor presentation.
- [DWMWA_TRANSITIONS_FORCEDISABLED](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute): reviewed on 2026-11-15 for disabling show/hide transitions on Glide's own overlay, avoiding a second animation on top of the strip. Other applications' DWM attributes are left untouched.
- [SetWindowPlacement](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowplacement) and [WINDOWPLACEMENT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-windowplacement): preserving normal bounds and minimized/maximized state.
- [SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow) and [AttachThreadInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput): constrained focus handoff between processes.
- [Precision Touchpad collection](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection): parsing touch contacts and hybrid reports.
- [DwmQueryThumbnailSourceSize](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmquerythumbnailsourcesize): refreshing the live source dimensions as an application lays out.
- [FLASHWINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-flashwinfo): stopping attention flashing on the selected foreground window.
- [RegisterShellHookWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registershellhookwindow): observing `HSHELL_FLASH` in the test harness. This observer is calibrated before assertions and is not used by the production app.

See [MATH.md](MATH.md) for the implemented formulas and [VERIFICATION.md](VERIFICATION.md) for the measured fixture results and their limitations.
