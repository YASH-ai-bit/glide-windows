# Native build verification

## Native 0.6 — outgoing transition

Checked on 2026-11-15 with the same Windows 11, single-display setup and MSVC configuration described below. The application and test executable compiled without warnings.

```text
PASS: 5043 native assertions
Minimized restore release: 222.672 ms
Release-to-focused-app median=191.362 ms; worst=206.75 ms
Incoming layout ready after request=28.1645 ms; prepared before release=1; post-release resizes=0
Focus requests=18; denied=0; flash observer calibrated=1; fixture flashes=0
PASS: live rendering, early fitting, cancellation, minimized policies, edges, regrab, focus and latency
```

New regression checks cover the release speed bound, clipping oversized previews to separate pages, unchanged outgoing bounds during both dragging and release, and starting release at the last submitted frame when newer input has not been rendered. The live fixture also verifies that the outgoing app retains focus early in the release, the endpoint remains active for another compositor-paced tick, and a temporarily restored neighbor stays live while sliding away on cancellation. Existing fitting, focus, minimized-state, interruption and taskbar-flash checks passed.

The longer release compared with 0.5 is intentional: the previous short curve could accelerate abruptly across the remaining screen. The new curve distributes that travel over more frames and waits one further presentation tick before uncovering the real app. Preparation still starts during the drag, with no new resize at release. Timings measure release until foreground confirmation and completion; they do not measure physical input latency or establish subjective smoothness.

Tested application: 264,192 bytes, SHA-256 `67326F88F2DB49EA8983AB91831DE1077B5B73D4F909D8CE593DF5B02689A58F`.

## Native 0.5 — historical reference

Reference build checked on 2026-11-13 on a Windows 11 laptop, build 26300, with a built-in Precision Touchpad and one display. Compiler configuration: MSVC x64, C++17, optimization `/O2`, static runtime `/MT`, warning level `/W4`.

## Results

`./build-native.ps1` compiled the application and tests without warnings. The resulting executable launched successfully with `--tray`. The following outputs record that test run; build artifacts and screenshots are not included in this repository. Run the commands after building to collect results on another machine.

`./artifacts/native/Glide.Tests.exe`:

```text
PASS: 3962 native assertions
```

`./artifacts/native/Glide.Tests.exe --integration`:

```text
Minimized restore release: 111.633 ms
Release-to-focused-app median=111.37 ms; worst=111.563 ms
Incoming layout ready after request=26.5502 ms; prepared before release=1; post-release resizes=0
Focus requests=20; denied=0; flash observer calibrated=1; fixture flashes=0
PASS: live rendering, early fitting, cancellation, minimized policies, edges, regrab, focus and latency
```

The median and worst values cover six repeated transitions between separate native test applications. The test measures elapsed time from release until the chosen application is confirmed as foreground and the transition is finished. It is a software measurement, not a physical finger-to-display latency measurement. These figures are observations, not guarantees for every application or system load.

The integration checks verified both minimized-window policies. With minimized windows excluded, swipes left them untouched. With inclusion enabled, the incoming neighbor was restored and its changed colors appeared live; the opposite minimized neighbor stayed minimized. Cancellation retained normal bounds and the maximize-on-restore flag and kept the original app focused. Committing restored and focused the chosen app.

Additional checks covered a one-window elastic boundary, no wrapping by default, optional wrapping into a minimized normal window, preserved taskbar work area, catching the last submitted frame, reverse cancellation, and cancellation when an unrelated fixture gained focus. The latter retained the external application's focus. Motion checks verified continuous recognition pickup, sub-threshold movement after catching a release, bounded elastic resistance, bounded reverse momentum and immediate release when animation is disabled.

Version 0.5 added a normal-window layout fixture. Touch-down priming remained hidden and did not resize it. After the first directional input, the test deliberately withheld all animation-message dispatch and confirmed the actual window was already maximizing. At 8% drag distance it was fitted while fingers were still logically down, before commit intent. At 31% travel the preview rectangle exactly matched the real window. Its last `WM_SIZE` timestamp was unchanged through release and after focus handoff. Canceling a prepared preview restored the original normal bounds; disabling fitting retained them throughout. A minimized normal window also retained its original maximize-on-restore preference after cancellation.

The shell observer was calibrated by deliberately flashing one fixture, stopping that flash, then clearing the collected events before measurements. During the measured integration transitions it observed zero fixture `HSHELL_FLASH` notifications, and the activation helper recorded zero rejected foreground requests. The observer is test-only. These checks establish the result for these fixtures; they do not suppress a real application's own new-message notifications or remove the normal active-taskbar indicator.

The native controls confirmed the Precision Touchpad was connected. Windows' built-in three-finger swipe action was set to Nothing before testing; Glide did not change that setting. Saved options survived restart. These automated results do not establish subjective smoothness or physical touch-to-display latency across different touchpads.

The running application's controls were visually inspected. Labels, the three options, status, buttons and shortcuts were legible and fit the window. The C++ capture helper briefly showed the controls without activation, then restored their hidden state. Captured images are local diagnostics and are not part of the published source.

## Tested executable

- Filename: `Glide.exe`.
- Size: 264,192 bytes.
- SHA-256: `C446BE948786AB11A83A4F1D34133E3C7DEFC68CFD89C43CDC17E4E5F0678893`.
- The launched copy matched the compiler output.
- Launched successfully; the process was responding with its controls hidden in the tray.

`dumpbin /dependents` listed only Windows system DLLs:

```text
USER32.dll
GDI32.dll
SHELL32.dll
dwmapi.dll
HID.DLL
ADVAPI32.dll
COMCTL32.dll
KERNEL32.dll
```

The native executable has no .NET or third-party animation/UI runtime dependency. The checksum identifies the reference artifact; it is not a guarantee of byte-for-byte reproducible builds. The repository contains the native source and build scripts, not the reference executable.
