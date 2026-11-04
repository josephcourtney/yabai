# WindowServer constraint-error investigation (macOS 27)

**Status:** 2026-11-04: existing Zen BSP acceptance PASSED on signed
revision 490c05342f36; intermittent WindowServer errors remain
**unattributed**. This is a read-only investigation, not a proposed change
to the renderer, Dock scripting addition, or Zen reconciliation.

## Established observations

1. The archived 2026-11-04 log contains 307 instances of
   _CGXPackagesSetWindowConstraints: Invalid window, all emitted by
   WindowServer[464:ee4]. Seven bursts are separated by more than 1 second.
   Burst sizes: 59, 59, 59, 59, 6, 6, 59. Durations in milliseconds:
   110, 135, 176, 142, 9, 16, 226.
2. In independent 30-second counts, an idle reconciliation OFF interval
   yielded 6 errors and an idle reconciliation ON interval yielded 0.
   After stopping the yabai daemon for 35 seconds, another 30-second
   measurement yielded 6 errors. This argues against attributing the
   observed *bursts* to each 750 ms reconciliation timer tick.
3. During live Zen acceptance the 30-second counters reported 195
   (ordinary), 177 (detach1), 307 (detach2), 77 (Space return) and
   195 (window closure). These rolling windows **overlap** and may include
   activity other than the named stage. They are not event-specific totals.
4. The log text lacks the invoking client PID, the invalid window ID and
   a stack trace. Therefore neither exact attribution nor an actual
   window-management fault can be inferred from the message alone.
5. Similar errors are reported in independent macOS applications (Stats,
   BetterDisplay, menu-bar popovers, Firefox, Codex/ChatGPT) on versions
   including macOS 26 and 27. They are not a yabai-specific fingerprint.

Relevant public reports (independent, not proof of local causality):

- https://github.com/exelban/stats/issues/3212
- https://github.com/waydabber/BetterDisplay/issues/5687
- https://github.com/openai/codex/issues/32501
- https://github.com/jizhi0v0/macos27-beta-issues/blob/main/issues/apple-windowserver-invalid-window.md

## Follow-up controlled data (2026-11-04, 12:45–12:51 EST)

After rebuilding the signed candidate at commit `cae12ccf96aa`, local
qualification reported **75 Python tests and two native C tests passing**,
with a verified clean signed ARM64 build. The following independent
phase-bounded 30-second measurements were collected:

| Phase | Running yabai | Reconciliation | Errors | Bursts |
| --- | --- | --- | ---: | --- |
| no-daemon-01 | no | n/a | 6 | 6 |
| no-daemon-02 | no | n/a | 59 | 59 |
| idle-on-01 | yes, PID 7859 | on | 118 | 59 + 59 |
| idle-off-01 | yes, PID 7859 | off | 6 | 6 |
| idle-on-02 | yes, PID 7859 | on | 0 | none |
| bsp-create-01 | yes, PID 7859 | not logged | 6 | 6 |

**Key update:** the exact 59-error burst occurred at 12:46:25.718–25.890
with **no yabai process before or after the capture**. Running yabai and
the 750 ms reconciliation timer are therefore not necessary for a 59-error
burst. This does not exclude prior state altered by yabai or the Dock
scripting addition, or another macOS component.

The 6-error pattern occurred near 12:45:58.770–.784, 12:47:58.768–.784,
and 12:49:58.767–.783, an approximately **120-second separation** in
these three measurements despite different daemon conditions. Treat a
separate periodic activity (e.g. another process or OS component) as a
candidate and collect more data before assigning a source.

**Invalid operation association:** `bsp-create-01` was captured from
12:49:47 to 12:50:27, but the logged `space --layout bsp` command ran
at 12:50:48. Its 6 errors at 12:49:58 cannot be evidence of that layout
command. Other unlogged manual operations may still have taken place.

**Invalid overlapping controls:** `bsp-create-03` started at 12:51:19
and `bsp-create-04` at 12:51:27 without either being finished. Any
results from the overlapping phases are not independent. Preserve their
markers and explicitly abandon them if they cannot be interpreted.
The diagnostic harness now rejects new phase starts while *any* phase
is active and supports evidence-preserving archival.

The observed background 59-error and 6-error burst signatures reduce the
value of a single BSP ON/OFF comparison. Prefer synchronized observations
with precisely recorded operations and repeated controls before assigning
a window-management cause.

## Code paths to distinguish

- **BSP geometry:** src/space_manager.c, view flushes and
  src/window_manager.c::window_manager_set_window_frame, which invokes
  Accessibility position and size setters.
- **Automatic sublayer:** src/space_manager.c::
  space_manager_tile_window_on_space_with_insertion_point calls
  window_manager_adjust_layer(window, LAYER_BELOW);
  src/window_manager.c::window_manager_adjust_layer delegates to the
  scripting addition, whose src/osax/payload.m::do_window_layer calls
  SLSSetWindowSubLevel.
- **Zen reconciliation:** the 750 ms timer queues missing AX references
  through the normal WINDOW_CREATED handler. It does not independently
  set constraints, move windows, or assign layers; any downstream
  creation can still take the two paths above.

These are source-derived *hypotheses*, not discovered causes. Turning
window_sublayer_auto OFF changes the requested BELOW sublayer to NORMAL
but **does not necessarily suppress scripting addition layer messages**.
It is not equivalent to disabling all layer IPC.

## New bounded, read-only measurement tool

The script is scripts/diagnose_windowserver.py. It **never** runs any
yabai IPC, changes a layout, creates a window, restarts a daemon, or alters
Dock/SIP/services. It records a local start/end timestamp and yabai PIDs,
retrieves exactly the bounded WindowServer log interval using macOS
log show, and writes a JSON summary plus the raw log.

On macOS, from this branch:

    just ws-idle no-daemon-idle-01 30
    just ws-start named-operation-01
    # Perform the named desktop action manually.
    just ws-finish named-operation-01

Reports are written to /tmp/yabai-windowserver-diagnostics/ as
<phase>.json and <phase>.log. A phase with a pending start has an
<phase>.active.json marker. Names must be unique; existing results are
never overwritten. **Only one active phase at a time is permitted**;
interprocess file locking rejects accidental overlap. PIDs that change
between start and finish are recorded as a confound. Phases are bounded
to 15 minutes. A log query error preserves the marker instead of silently
claiming zero events. To inspect and archive interrupted phases without
losing evidence:

    just ws-status
    just ws-abandon bsp-create-03 "overlapping phase, no reliable end timestamp"
    just ws-abandon bsp-create-04 "overlapping phase, no reliable end timestamp"

Abandoned phases remain as <phase>.abandoned.json (including the original
start timestamp, PID list, archival timestamp and reason); the active
markers are then retired. Do **not** finish an old phase long after its
operation or recycle a completed/abandoned label.

The offline parser also accepts the archived original log:

    just ws-analyze /path/to/yabai-windowserver-spike.log

Fields: error_count, burst_count, burst_size_histogram, counts_by_second,
and precise burst start/end and duration. **Use per-phase bounded counts;
do not compare overlapping --last 30s windows as if independent.**
The start/finish commands and Kitty redraws can themselves cause desktop
activity: use matched control phases and look for bursts **within** the
bracket, not a single sharp boundary spike. macOS log-show timestamps and
buffering impose further timing uncertainty. A zero count means no matching
entries in the retrieved interval, not proof that no window problem occurred.

## Controlled measurement sequence

Use a disposable macOS session and the already-qualified candidate build.
Keep Scroot closed. Never start two yabai daemons. If input becomes
unreliable, stop the foreground daemon via its terminal rather than
changing Dock or attempting invasive cleanup.

**First, idle controls** (do not open, close or move windows during each
30-second sample):

1. With *no* yabai process running, collect two or three distinct
   phases: no-daemon-idle-01, no-daemon-idle-02, etc. Verify with pgrep.
2. Start the candidate foreground daemon explicitly. Verify its actual
   binary provenance with just qualify-running.
3. Set window_reconciliation OFF; collect idle-off-01 and idle-off-02.
4. Set window_reconciliation ON; collect idle-on-01 and idle-on-02.
5. Repeat OFF and ON in reverse order if counts differ substantially.
   Restore ON afterward for normal Zen functionality. These steps
   explicitly mutate only the reconciliation config, not window topology.

**Second, discrete operations.** Use a clearly labeled disposable Space
with recorded *persistent ID*, verified active layout, and only disposable
windows. Bracket each manual operation with ws-start/ws-finish, taking
care not to perform unrelated actions in the interval.

- Focus-only: focus between existing windows without changing layout.
- Ordinary window create/close on FLOAT, then on BSP. Compare repeated
  comparable operations, not the first isolated event alone.
- BSP resize/reflow: create or close one ordinary Kitty test window.
- BSP -> FLOAT and FLOAT -> BSP, if you explicitly authorize each
  transition and first verify the target is still the disposable Space.
  Note that layout transitions may change window placement.
- Repeat same controlled BSP operation with
  window_sublayer_auto ON versus OFF, holding Zen reconciliation OFF
  for both sets. The OFF setting may still issue NORMAL layer commands,
  so treat this as evidence about requested sublayers, not presence of IPC.
- Zen detach while FLOAT versus BSP (both with reconciliation ON) only
  after the ordinary-window controls are stable. Do not perform any
  manual window --refresh during the Zen automated-discovery test.

Always record the actual action, observed visual behavior, initial
window IDs, Space ID/index, daemon PID, SA status, reconciliation and
window_sublayer_auto settings in a separate short experiment note. The
script deliberately does not inspect full window titles or change system
state. Do not use any historic Space index or window ID without a fresh
identity check.

**Retrieve comparisons:**

    jq -s '[.[] | select(has("error_count")) |
      {phase, elapsed_seconds, error_count, burst_size_histogram,
       pid_changed}]' /tmp/yabai-windowserver-diagnostics/*.json

### Interpretation and stopping rule

- Significant bursts only on BSP transitions, not equivalent floating
  transitions: prioritize layout flush/geometry and sublayer paths.
- Significant difference between sublayer-auto settings under otherwise
  matched BSP operations: investigate layer setting, but do not claim
  IPC is isolated.
- Similar bursts with no daemon, on FLOAT, or from menu-bar interaction:
  broaden the investigation to macOS components and other window managers.
- Repeated identical 59-error groups are an important signature, not
  evidence of 59 distinct windows or a particular invalid window ID.
- Log bursts without input, CPU, or visual symptoms may reflect an OS
  logging defect; do not conflate them with a confirmed desktop freeze.

Stop experimental mutation immediately if ordinary clicks or keyboard
input fail, frames become unstable, or WindowServer remains persistently
busy. Record the event; do not force-kill WindowServer.

## Acceptance disposition

The six-stage Zen reconciliation test has passed. The separate WindowServer
issue has not been attributed. A safe merge decision should use the
discrete-operation results and observable desktop behavior, not merely
the raw count of a known cross-application macOS error string.

No GitHub Actions, service installation, Dock restart, SIP changes, or
automatic BSP changes are introduced by the diagnostic tool.
