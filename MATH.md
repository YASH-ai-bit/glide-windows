# Swipe and commit math — native 0.8

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

## Release travel and braking

Let `a` be the last submitted visual position, `b` the selected endpoint, `D = abs(b-a)`, and `v` the release velocity. Starting from the submitted position avoids jumping to an input sample that has not appeared yet. The release integrates a velocity profile with three phases: a short ramp, steady travel and a short brake. It does not ease across the whole remaining distance.

```text
scale = sqrt(min(1, D / 0.16))
ramp = 0.032 * scale
brake = 0.028 * scale
direction = sign(b - a)
initialSpeed = max(v * direction, -0.025 / ramp)
peakSpeed = max(4.8, initialSpeed)
rampsDistance = (initialSpeed + peakSpeed) * ramp / 2 + peakSpeed * brake / 2
```

For sufficient remaining distance, the cruise duration is `(D - rampsDistance) / peakSpeed`. Otherwise the cruise is omitted and the peak is reduced to `(2*D - initialSpeed*ramp)/(ramp+brake)`. If that peak would be below the already forward-moving initial speed, braking begins immediately, with duration `2*D/initialSpeed`. This preserves a fast swipe's speed even very near the endpoint.

The ramp interpolates velocity using `S(u) = 3*u² - 2*u³`. Position comes from its integral `I(u) = u³ - u⁴/2`:

```text
rampTravel(u) = initialSpeed * ramp * u + (peakSpeed - initialSpeed) * ramp * I(u)
cruiseTravel(t) = peakSpeed * t
brakeTravel(u) = peakSpeed * brake * (u - I(u))
position = a + direction * accumulatedTravel
```

Velocity and acceleration meet continuously at phase boundaries. Final velocity and acceleration are zero, with an exact endpoint and no spring tolerance to wait for. Ordinary initial velocity is preserved. Opposing velocity is bounded so its outward excursion stays below 1.25% of a page. Cruise speed is 4.8 viewport widths per second unless the finger was already moving faster; short travel uses a lower peak or immediate braking.

For `D=0.6`, a stationary release takes 155 ms and a forward release at 2.5 widths/second takes about 147 ms. A stationary release with only `D=0.03` remaining takes about 26 ms. These are curve durations, before display scheduling and focus confirmation. An already reached endpoint, or disabled animation, skips the motion curve.

Focus transfer starts in the final 35 ms of the curve. The cover stays visible for a further compositor-paced tick after submitting the exact endpoint, and is removed only after foreground/layout confirmation. Prepared neighbors are rolled back at completion, so a window still sliding away on cancellation does not resize or minimize underneath its live preview. Unresponsive applications can still delay their own layout or drawing; the utility cannot make another process render instantly.

## Taskbar attention

The old handoff tried `SetForegroundWindow` before establishing the input-thread relationship. That rejected attempt could request taskbar attention. The new path checks responsiveness, attaches input queues, requests activation, then detaches. It clears flashing only on the selected window after that window is foreground. No global flashing or notification setting is changed.

The integration test calibrates a shell flash observer, then checks fixture events during actual cross-process switches. It also verifies layout starts with animation callbacks withheld, is ready before release, and does not resize again at handoff. See [the recorded results](VERIFICATION.md).
