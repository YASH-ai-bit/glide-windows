# Swipe and commit math — native 0.5

The design separates preparation, finger tracking, the commit decision and the final snap. Preparing a window never waits for commit intent.

## Direct tracking

`p` is horizontal displacement in viewport widths: negative reveals the next window, positive reveals the previous one. The three contact positions are averaged. Touchpad displacement is multiplied by the saved sensitivity. Once a horizontal gesture is recognized, its 1% touchpad dead zone is subtracted to avoid a jump at pickup.

Position follows input directly. Only velocity is filtered: samples from the most recent 60 ms use weighted linear regression, with weights `exp((sampleTime - now) / 0.025)`. Recent motion therefore matters more than old motion when reversing. A stale history produces zero velocity. The compositor clock presents the latest position rather than replaying a queue of old positions.

At an unavailable edge, outward displacement `d` becomes `0.12*d/(0.12+d)`. Resistance starts continuously and remains below 12% of the viewport. Catching a release uses the last submitted position and the inverse resistance mapping, so the next input starts at the same place.

## Prepare before deciding

Three-finger touch-down registers the neighboring DWM views while the transition stays hidden. The first directional movement queues asynchronous restoration/maximization of the incoming window. This happens in the input handler, before the first animation callback and regardless of commit intent. Merely touching down does not resize anything.

With fitting enabled, the incoming preview uses the intended work-area rectangle immediately. Its native application lays out behind the transition, and the live source dimensions are refreshed every frame. When Windows supplies its actual maximized bounds, those become the target rectangle, accommodating application-specific size constraints. Canceling restores the saved placement and minimized/maximized state; committing retains the prepared layout.

For reveal fraction `r`, the subtle incoming scale is:

```text
u = clamp(r / 0.28, 0, 1)
S(u) = 3*u² - 2*u³
scale = 0.988 + 0.012*S(u)
```

The scale is exactly 1 by 28% travel, before the slow-drag commit threshold. This is a 1.2% visual lift, not a late resize. The outgoing scale decreases by at most 0.6% and returns to exactly 1 on cancellation. These effects are disabled when Windows client-area animation is disabled.

## Commit intent

Velocity `v` is measured in viewport widths per second. Project 85 ms ahead, limiting the contribution to 35% of the viewport:

```text
q = p + clamp(0.085*v, -0.35, 0.35)
```

A next-window commit becomes eligible at `q < -0.30` with `p < -0.025`; previous-window selection is symmetric. A short fast flick can therefore commit without a long drag. Once selected, the next-window intent persists until `p > -0.14` or `q > -0.10`. These separate return thresholds prevent small velocity fluctuations from toggling the decision. A deliberate reversal cancels it. Release reevaluates the same state; it does not begin a new fitting phase.

## Finite snap

Let `a` be release position, `b` the selected endpoint, `D = abs(b-a)`, and `v` the release velocity. Duration in seconds is:

```text
T = clamp(0.040 + 0.100*sqrt(D) - 0.006*min(abs(v), 6), 0.040, 0.135)
```

For example, `D=0.6, |v|=2.5` gives about 102 ms; `D=0.03, v=0` gives about 57 ms. An already reached endpoint, or disabled animation, completes immediately.

For `u = clamp(elapsed/T, 0, 1)` and `delta = b-a`:

```text
m = clamp(v*T/delta, -min(0.8, 0.10/abs(delta)), 2.8)
position(u) = a + delta * ((-2*u³ + 3*u²) + m*(u³ - 2*u² + u))
```

This Hermite curve has an exact endpoint and zero final velocity. Reasonable initial velocity carries through; extreme tangents are bounded. Opposing momentum may continue briefly before reversing, with its excursion below 1.5% of a viewport. Elastic boundaries absorb outward momentum. There is no spring epsilon to wait for.

Focus transfer begins as soon as release chooses the destination. The cover remains until the curve reaches its endpoint and foreground/layout confirmation succeeds, so focus-dependent painting overlaps the snap. Unresponsive applications can still delay their own layout or drawing; the utility cannot make another process render instantly.

## Taskbar attention

The old handoff tried `SetForegroundWindow` before establishing the input-thread relationship. That rejected attempt could request taskbar attention. The new path checks responsiveness, attaches input queues, requests activation, then detaches. It clears flashing only on the selected window after that window is foreground. No global flashing or notification setting is changed.

The integration test calibrates a shell flash observer, then checks fixture events during actual cross-process switches. It also verifies layout starts with animation callbacks withheld, is ready before release, and does not resize again at handoff. See [the recorded results](VERIFICATION.md).
