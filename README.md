# Glide

Three-finger app switching for Windows, built in native C++.

Glide lets you slide between open application windows with a Precision Touchpad. Live previews follow your fingers, the incoming window prepares its layout during the drag, and releasing completes the switch or returns to the current app.

The project is experimental and currently supports one display on Windows 11. It uses Win32, HID and Desktop Window Manager APIs, with no third-party animation framework, UI toolkit or .NET runtime.

## Features

- Live compositor previews that continue updating during a swipe.
- Incoming windows prepared before commit, with optional fitting to the screen.
- Direct finger tracking, velocity-aware commits and a short release animation.
- Elastic boundaries and optional wrapping between the last and first window.
- Interruptible transitions: catch an animation and continue dragging.
- Optional minimized-window inclusion, with placement restored on cancellation.
- A tray interface, keyboard shortcuts and saved preferences.
- Focus handoff designed to avoid taskbar attention flashes.

Glide switches individual application windows on the current desktop. Its motion is inspired by macOS; it does not implement macOS Spaces or replace the Windows compositor.

## Requirements

To run, use **Windows 11 x64**, a **Windows Precision Touchpad**, and **one connected display**. Windows' three-finger swipe action must be set to **Nothing**.

To build, install Visual Studio or Visual Studio Build Tools with the **Desktop development with C++** workload, including an x64 MSVC compiler and a Windows SDK. The PowerShell build locates MSVC through `vswhere`; it does not download dependencies.

## Build and run

The repository contains source code. Build the executable locally:

```powershell
git clone https://github.com/YASH-ai-bit/glide-windows.git
cd glide-windows
.\build.ps1
.\Glide.exe
```

The build writes the application and test executable to `artifacts/native/`. It also copies the application to the project root if that copy is not running. To start directly in the tray:

```powershell
.\Glide.exe --tray
```

### Configure the gesture

1. Open **Settings > Bluetooth & devices > Touchpad > Three-finger gestures**.
2. Set **Swipes** to **Nothing** so the Windows gesture does not compete with Glide.
3. Open Glide and choose **Enable swipes** if it is not already active.
4. Choose **Hide to tray**, return to an application and swipe horizontally with three fingers.

Glide enables automatically when it detects a Precision Touchpad and the Windows swipe action is already disabled. It reads that Windows setting without changing it.

## Controls

| Action | Control |
| --- | --- |
| Reveal the next or previous window | Three-finger horizontal swipe |
| Complete a switch | Release after dragging far enough, or flick |
| Return to the current window | Reverse direction or release a short, slow drag |
| Catch an ongoing transition | Put three fingers down and continue dragging |
| Cancel an active transition | `Esc` |
| Enable or pause swipes | `Ctrl + Alt + G` |
| Pause immediately | `Ctrl + Alt + Esc` |
| Open the controls | Double-click the tray icon |
| Exit Glide | Close the control window or choose **Quit** in the tray menu |

Windows retain a stable order while Glide runs. Newly opened windows join the end.

## Settings

| Setting | Default | Behavior |
| --- | --- | --- |
| Include minimized windows | Off | Adds minimized windows to the swipe order. |
| Wrap from the last window to the first | Off | Makes the order circular. Otherwise, the ends use elastic resistance. |
| Fit incoming windows during the swipe | On | Maximizes eligible incoming windows while the drag is underway. |
| Swipe sensitivity | 1.85 | Adjusts how far previews move relative to your fingers. |

Fitting changes the real incoming window's layout, not just its preview. Committing leaves it maximized, with its previous normal bounds available through Restore. Canceling returns it to its original placement. Non-resizable windows retain their dimensions.

When minimized windows are included, only the window being revealed is restored. Canceling returns it to its original minimized state, including its maximize-on-restore preference.

Preferences are stored in `%LOCALAPPDATA%\Glide\settings.ini`. Logs are stored in `%LOCALAPPDATA%\Glide\native.log`.

## How the motion works

Glide registers neighboring live views on three-finger touch-down. The first horizontal movement starts preparing the incoming window, independently of the commit decision. Source geometry is refreshed every frame so the preview stays aligned with the application's layout.

Position follows the fingers directly. Recent velocity predicts intent, and separate commit and cancellation thresholds prevent small fluctuations from changing the decision. A subtle scale transition finishes early in the drag.

Release uses a finite cubic curve with a planned duration of **40-135 ms**. Focus transfer overlaps that animation. Actual completion time also depends on display scheduling and the target application's response.

See [MATH.md](MATH.md) for the formulas and [RESEARCH.md](RESEARCH.md) for implementation references.

## Verification

```powershell
# Deterministic motion and input checks
.\artifacts\native\Glide.Tests.exe

# Touchpad detection and window discovery
.\artifacts\native\Glide.Tests.exe --diagnose

# Interactive compositor, layout and focus checks
.\artifacts\native\Glide.Tests.exe --integration
```

The integration suite creates temporary test applications and visibly switches between them. It checks live rendering, layout preparation before release, cancellation, minimized-window state, focus, interruption and taskbar flash events. One deliberate fixture flash calibrates the observer before measured transitions. Run this suite in an interactive desktop session; its fixtures close afterward.

The recorded native 0.5 run passed **3,962 deterministic assertions** and the integration checks, including no resize after release and no observed taskbar attention flashes during measured fixture transitions. These tests do not measure physical finger-to-display latency or guarantee the same behavior in every application. See [VERIFICATION.md](VERIFICATION.md) for methods and results.

## Development

```powershell
# Rebuild the application and diagnostics
.\build.ps1

# Rebuild only the test executable
.\build.ps1 -TestsOnly

# Build into artifacts/native without copying the app to the project root
.\build-native.ps1

# Gracefully stop this checkout's running app, including a tray instance
.\artifacts\native\Glide.Tests.exe --stop-running
```

Run the stop command from the project directory. It verifies the running executable's path before requesting shutdown.

If the root executable is running, the build leaves it open and keeps the new binary in `artifacts/native/`. Close Glide, copy that binary to the root and reopen it to use the new build.

| File | Responsibility |
| --- | --- |
| `native/glide.cpp` | Lifecycle, native controls, tray and input routing |
| `native/touchpad.hpp` | Raw HID input and hybrid contact reports |
| `native/motion.hpp` | Recognition, velocity, commit intent and release curves |
| `native/platform.hpp` | Window discovery, DWM views, focus transfer and frame pacing |
| `native/transition.hpp` | Preparation, presentation, fitting and rollback |
| `native/settings.hpp` | Saved preferences |
| `native/tests.cpp` | Deterministic tests, compositor fixtures and diagnostics |

## Current limitations

- Only one connected display is supported.
- Precision Touchpad detection and report formats may vary between devices.
- Hidden-to-tray, cloaked, disabled and tool windows are excluded.
- Suspended or unresponsive applications may need time to resume rendering.
- Protected content and exclusive full-screen applications may not appear correctly.
- Non-resizable windows and windows with fitting disabled leave unused space around their previews.
- Normal Windows taskbar indicators and application-generated notifications remain active.

The application makes no network requests and does not record window images. An optional test helper can capture Glide's own controls for visual verification. Glide does not install itself at startup. If you stop using it, restore your preferred Windows three-finger action in Touchpad settings.
