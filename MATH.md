# Swipe and commit math — native 0.7

The design separates preparation, finger tracking, the commit decision and the final snap. Preparing a window never waits for commit intent.

## Direct tracking

`p` is horizontal displacement in viewport widths: negative reveals the next window, positive reveals the previous one. The three contact positions are averaged. Touchpad displacement is multiplied by the saved sensitivity. Once a horizontal gesture is recognized, its 1% touchpad dead zone is subtracted to avoid a jump at pickup.

Position follows input directly. Only velocity is filtered: samples from the most recent 60 ms use weighted linear regression, with weights `exp((sampleTime - now) / 0.025)`. Recent motion therefore matters more than old motion when reversing. A stale history produces zero velocity. The compositor clock presents the latest position rather than replaying a queue of old positions.

At an unavailable edge, outward displacement `d` becomes `0.12*d/(0.12+d)`. Resistance starts continuously and remains below 12% of the viewport. Catching a release uses the last submitted position and the inverse resistance mapping, so the next input starts at the same place.

## Prepare before deciding

Three-finger touch-down registers the neighboring DWM views while the transition stays hidden. The first directional movement queues asynchronous restoration/maximization of the incoming window. This happens in the input handler, before the first animation callback and regardless of commit intent. Merely touching down does not resize anything.

With fitting enabled, the incoming preview uses the intended work-area rectangle immediately. Its native application lays out behind the transition, and the live source dimensions are refreshed every frame. When Windows supplies its actual maximized bounds, those become the target rectangle, accommodating application-specific size constraints. Canceling restores the saved placement and minimized/maximized state; committing retains the prepared layout.

## Rolling strip

Each window has an integer page coordinate `k` relative to the original app. Every page uses the same strip displacement `p`:

```text
x(k) = round((p + k) * viewportWidth)
```

Windows translate at a fixed preview size, with no zoom, lift or scale easing. Fitted windows use the prepared layout; other windows retain their initial bounds. Each preview is clipped to its own moving viewport so oversized native bounds cannot overlap the neighboring page.

The strip retains the session's window order and lazily registers the visible pages plus one neighbor on either side. At most four surfaces remain after a rendered frame. Between input and presentation, old visible surfaces remain registered until the replacement frame is submitted. Traveling farther creates the next neighbors instead of reaching a three-window limit. Optional wrapping maps the page number modulo the number of windows. Only revealed windows and the chosen destination are prepared; skipped windows recover their original placement at completion.

Glide disables DWM show/hide transitions on its own overlay window. The slide therefore does not also fade or animate the cover as a newly opened application. This does not change system animation preferences or another application's DWM attributes.

## Commit intent

Velocity `v` is measured in viewport widths per second. Project 85 ms ahead, limiting the contribution to 35% of the viewport:

```text
q = p + clamp(0.085*v, -0.35, 0.35)
```

A next-window commit becomes eligible at `q < -0.30` with `p < -0.025`; previous-window selection is symmetric. A short fast flick can therefore commit without a long drag. Once selected, the next-window intent persists until `p > -0.14` or `q > -0.10`. These separate return thresholds prevent small velocity fluctuations from toggling the decision. A deliberate reversal cancels it. Release reevaluates the same state; it does not begin a new fitting phase.

For multi-page gestures, the intent rule operates on fractional travel. Let `d` be this gesture's displacement, `j = trunc(-d)` the whole pages traversed, and `a` its logical starting page. Apply the rule to `d+j`, then choose `a+j+intent`, bounded by the queue ends when wrapping is off. Crossing a whole-page boundary resets the fractional intent.

When another gesture catches an unfinished release, its visual base is the last submitted position while its logical starting page becomes the pending destination. A further same-direction flick advances one more app immediately; a reverse flick returns toward the previous destination. No intermediate focus handoff or endpoint is required. A short caught gesture can still complete the already pending switch. Explicit cancellation returns to the session's original app.

## Finite snap

Let `a` be the last submitted visual position, `b` the selected endpoint, `D = abs(b-a)`, and `v` the release velocity. Starting from the submitted position avoids jumping to an input sample that has not appeared yet. The initial duration in seconds is:

```text
T = clamp(0.060 + 0.160*sqrt(D) - 0.004*min(abs(v), 6), 0.055, 0.500)
```

For example, `D=0.6, |v|=2.5` gives about 174 ms; `D=0.03, v=0` gives about 88 ms. An already reached endpoint, or disabled animation, skips the motion curve. Presentation and focus confirmation still apply.

For `u = clamp(elapsed/T, 0, 1)` and `delta = b-a`:

```text
m = clamp(v*T/delta, -min(0.8, 0.10/abs(delta)), 2.8)
position(u) = a + delta * ((-2*u³ + 3*u²) + m*(u³ - 2*u² + u))
```

The duration is then increased as needed to limit peak speed. The normalized derivative is:

```text
g(u) = (3*m - 6)*u² + (6 - 4*m)*u + m
peakSpeed = abs(delta)/T * max(abs(g(u)))
speedLimit = max(6.5, abs(v))
```

The maximum is evaluated at both endpoints and the quadratic's interior vertex, when present. If the peak exceeds the limit, multiply `T` by `peakSpeed/speedLimit`, recompute `m`, and repeat (up to 16 iterations). This prevents a moderate flick from accelerating abruptly to cover most of the screen in a short tail, while allowing an already fast physical swipe to retain its momentum.

This Hermite curve has an exact endpoint and zero final velocity. Reasonable initial velocity carries through; extreme tangents are bounded. Opposing momentum may continue briefly before reversing, with its excursion below 1.5% of a viewport. Elastic boundaries absorb outward momentum. There is no spring epsilon to wait for.

Focus transfer starts in the final 35 ms of the curve. The cover stays visible for a further compositor-paced tick after submitting the exact endpoint, and is removed only after foreground/layout confirmation. Prepared neighbors are rolled back at completion, so a window still sliding away on cancellation does not resize or minimize underneath its live preview. Unresponsive applications can still delay their own layout or drawing; the utility cannot make another process render instantly.

## Taskbar attention

The old handoff tried `SetForegroundWindow` before establishing the input-thread relationship. That rejected attempt could request taskbar attention. The new path checks responsiveness, attaches input queues, requests activation, then detaches. It clears flashing only on the selected window after that window is foreground. No global flashing or notification setting is changed.

The integration test calibrates a shell flash observer, then checks fixture events during actual cross-process switches. It also verifies layout starts with animation callbacks withheld, is ready before release, and does not resize again at handoff. See [the recorded results](VERIFICATION.md).
