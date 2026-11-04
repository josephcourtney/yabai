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

## Context log: 12:46:25 daemon-free 59-error burst

A separate `log show --info` capture of 12:46:23–28, limited to
WindowServer, Dock and SystemUIServer, supplies the following sequence:

| Local timestamp | Observed event |
| --- | --- |
| 12:46:25.407–.408 | WindowServer reports processes 1139, 773 and 606 failed to act on dequeued pings before timeout |
| 12:46:25.408–.416 | Window ordering and frontmost-app transition to Zen (PID 817; SkyLight connection 5e803) |
| 12:46:25.436 | Seven WindowServer transaction timeouts (27238–27244) |
| 12:46:25.587–.692 | Several key/main-appearance and order-window changes across multiple connections |
| 12:46:25.682 | Three further transaction timeouts (27245–27247) |
| 12:46:25.718–.890 | Exactly 59 `_CGXPackagesSetWindowConstraints: Invalid window` messages |
| 12:46:27.115–.116 | Dock BackgroundTaskManagement user-data-change notifications, **after** the burst |

The burst closely follows a change in frontmost application to Zen and
multiple WindowServer ordering/appearance updates. Since yabai's process
list was empty throughout the enclosing measurement, **the daemon is not
required for this sequence**. The log is consistent with a macOS
activation/ordering problem or one originating in a client participating
in this transition; it does **not** identify which client issued the
invalid constraint request. The transaction timeouts and ping timeouts
may be associated with transient responsiveness problems but cannot
establish an actual user-visible input freeze.

**Important qualification:** The phase was designated "idle," but its
log proves that Zen became frontmost within that interval. We do not
know whether this resulted from operator interaction, another program
activating Zen, or a window-system behavior. Thus `no-daemon-02`
cannot be treated as a strictly no-interaction idle control. The
six-error `:58` events also require independent attribution.

**Next priority: test focus activation rather than mutate yabai:**

1. With no yabai daemon, bracket manually switching **Kitty → Zen**
   using a fresh unique phase label, record the exact action time and
   do not create/close/reposition any windows. Repeat at least three
   times with fresh labels and inspect both burst sizes and transaction
   timeout messages.
2. Repeat **Zen → Kitty** and a comparable **Kitty → another app**
   activation (where applicable). Keep the same screen/Space.
3. Repeat the same comparisons with yabai running but reconciliation
   OFF, then ON, only if the daemon-free activation controls are stable.
4. If 59-error bursts are tightly tied to Zen activation without
   yabai, prioritize Zen/macOS WindowServer behavior over BSP or the
   scripting addition. Capture the immediately preceding foreground
   transitions and ordering requests for each occurrence.

These are tests to discriminate causes, not a claim that Zen is defective.
The current phase collector logs the constraint errors, not all
transaction/activation messages; use a separate bounded `log show --info`
query for any stage where those messages matter.

## Follow-up focus-to-Zen experiment (2026-11-04 14:27 EST)

The 30-second `focus-zen-01` phase ran from 14:27:09.252 to
14:27:39.302, with `yabai_pids_before=[]` and
`yabai_pids_after=[]`. It recorded two equal bursts, each containing
**109** invalid-window errors:

| Burst | Start | End | Count | Duration |
| --- | --- | --- | ---: | ---: |
| 1 | 14:27:12.335 | 14:27:12.545 | 109 | 210 ms |
| 2 | 14:27:28.643 | 14:27:28.880 | 109 | 237 ms |

The contextual `log show --info` output confirms **two opposite application
activations** while no yabai daemon was running:

| Event | Zen activation | Kitty activation |
| --- | --- | --- |
| Frontmost app change | 14:27:12.023–.026 to Zen PID 817 | 14:27:28.329–.331 to Kitty PID 99463 |
| SkyLight connection | 5e803 (Zen) | 69123 (Kitty) |
| Failed ping notices | Four, 14:27:12.021–.022 | Four, 14:27:28.328–.329 |
| Transaction timeouts | Ten, 14:27:12.045 and .285 | Ten, 14:27:28.466 and .538 |
| Invalid-window burst | 109 at 14:27:12.335–.545 | 109 at 14:27:28.643–.880 |

The four ping notices name **the same PIDs** (28948, 1139, 773,
606) on both transitions. Both bursts were preceded by deferred
window ordering, key/main-appearance changes, and ten transaction
timeouts. Some of the order/appearance changes involve other SkyLight
connections besides the newly frontmost application. **The available
log does not identify which client invoked the invalid constraint
operation, whether those four processes are a cause or consequence of
the transition, or whether any focus change was unusually slow to the
user.**

This revises the prior hypothesis: the second 109-error burst was
**Zen → Kitty**, not unexplained background activity. The common
observable condition is app activation/ordering, **not specifically
Zen becoming frontmost**. Together with daemon-free captures, this
makes yabai's reconciliation polling and BSP tiling much weaker
candidates for the invalid-constraint burst. Prior scripting-addition
state cannot be ruled out.

An exact repeated burst size (109 here; 59 in an earlier capture)
is not evidence of 109 or 59 distinct windows.

### Discriminating follow-up (no yabai daemon)

Rather than change yabai source or perform BSP mutations, use
repeated, bounded, explicitly annotated activation trials:

1. Kitty → Zen, then Zen → Kitty; record the **time and direction** of
   each intentional transition and examine the associated bursts.
2. Kitty → another ordinary app, then back. This discriminates whether
   the problem is Zen-specific or broader application switching.
3. Where feasible, switch between two non-Zen apps. Repeat without
   switching as a control.
4. If every app-switch direction reproduces the same ping/timeout/error
   signature, correlate the four repeatedly unresponsive PIDs with
   running process names using `ps -p 28948,1139,773,606 -o pid,ppid,comm,args`
   **while those PIDs still exist**. PID reuse makes historical
   identities unreliable. Do not kill the processes based on these
   messages.

The collector brackets errors but does not itself record which app
received focus. Continue using a separate bounded `log show --info`
capture for causal ordering. The focus-transition evidence is strong
temporal correlation, **not** attribution of an underlying defect.


## Non-Zen focus-control summary (2026-11-04 14:46–14:47 EST)

The reported `focus-other-01` experiment lasted 30.037 seconds
(14:46:49.036–14:47:19.073) and recorded **491** matching errors
in six grouped bursts, with `yabai_pids_before=[]` and
`yabai_pids_after=[]`.

| Burst start | End | Error count | Duration |
| --- | --- | ---: | ---: |
| 14:46:58.037 | 14:46:58.960 | 120 | 923 ms |
| 14:47:00.725 | 14:47:00.927 | 19 | 202 ms |
| 14:47:02.237 | 14:47:02.511 | 109 | 274 ms |
| 14:47:04.729 | 14:47:04.957 | 99 | 228 ms |
| 14:47:06.020 | 14:47:07.860 | 141 | 1840 ms |
| 14:47:17.264 | 14:47:17.342 | 3 | 78 ms |

This makes a **running** yabai daemon unnecessary to reproduce
substantial WindowServer invalid-window logging. The phase label
indicates an intended control involving an app other than Zen, but
the summary alone does **not** confirm actual foreground application
identities or interaction count. No attribution to another app or
specific OS routine should be made without the contextual log.

The 1840 ms grouped burst means the first and final errors were
1.84 seconds apart; grouping allows internal gaps up to 1 second,
so it does not establish continuous blocked operation for 1.84
seconds or an actual input freeze.

Next, retrieve a window covering **all six bursts** with the same
WindowServer/Dock/SystemUIServer predicate:

```sh
log show --style compact --info \
  --start '2026-11-04 14:46:56' \
  --end '2026-11-04 14:47:19' \
  --predicate '(process == "WindowServer" OR process == "Dock" OR process == "SystemUIServer")' \
  > /tmp/yabai-windowserver-context-1447.log
```

Review foreground changes, ping failures, transaction timeouts, and
other app/window lifecycle events immediately preceding each burst.
If degraded pointer/keyboard responsiveness occurs, suspend interactive
trials. Do not kill WindowServer or change Dock/SIP settings as an
unvalidated workaround.

## Correlated non-Zen activation log (2026-11-04 14:46:56–14:47:19)

The complete context log for the `focus-other-01` phase contains
491 invalid-window errors. It confirms repeated changes in which
application was frontmost **without a yabai daemon**, including the
following SkyLight `DoDeferredOrdering` events:

| Frontmost at | Application | Invalid-window burst following |
| --- | --- | --- |
| 14:46:57.736 | Messages (PID 843) | 120 at 14:46:58.037–.960 |
| 14:47:00.415 | Finder (PID 877) | 19 at 14:47:00.725–.927 |
| 14:47:01.934 | Kitty | 109 at 14:47:02.237–.511 |
| 14:47:04.416 | Slack | 99 at 14:47:04.729–.957 |
| 14:47:06.003 | Kitty | 119 errors before the next activation |
| 14:47:06.912 | Zen | 11 errors before the next activation |
| 14:47:07.556 | Kitty | 11 errors through 14:47:07.860 |

The final three subgroups are combined into the single 141-error
`14:47:06.020–14:47:07.860` burst by the parser's *one-second
maximum inter-event gap*. **It is not one uninterrupted two-second
failure.** These counts are grouped by observed activation time,
not assigned causally to individual clients.

At 14:47:17.010–.011, four ping failures occurred for PIDs
**28948, 1139, 773 and 606**, followed by a separate three-error
invalid-window burst at 14:47:17.264–.342. There is **no new
frontmost-app transition** in the immediately preceding seconds.
The same four PIDs were reported at 14:46:57.731–.732. This is
evidence of a broader problem than foreground-application changes
alone (e.g. periodic WindowServer housekeeping or a persistent
client connection), and the failed pings deserve separate study.

A programmatic count of the context log yields:
- **491** `_CGXPackagesSetWindowConstraints: Invalid window` entries.
- **2,162** `transaction … timed out` log entries, but only **768
  distinct transaction numbers** (some logged repeatedly). Do not
  equate the number of log lines with distinct new transaction
  failures.
- **Eight** failed-ping log entries (four PIDs reported twice).
- **24** `Unsupported image function XRGB` entries, concentrated
  around earlier activation/rendering changes. This is a distinct
  compositor diagnostic and has no confirmed causal association.

The shared signature across Messages, Finder, Kitty, Slack and Zen
strongly weakens a Zen-specific diagnosis. These observations do not
prove a macOS-only bug: an unrelated third-party app, a persistent
scripting-addition effect, or another shared window-system client
could still be involved. There is no basis to modify the yabai BSP
or reconciliation paths to suppress these messages.

### Highest-value follow-up

First identify the repeatedly failed ping recipients while they are
still alive:

```sh
ps -p 28948,1139,773,606 -o pid,ppid,comm,args
```

Record missing PIDs and PID-reuse limitations. Do not kill any process
solely because it appears in a timeout message. Where safe, perform
a noninteractive control with no application switching to determine
whether ping failures and small invalid-window bursts recur.

If a real input freeze returns, capture an appropriately authorized
WindowServer diagnostic report or `spindump` after ensuring it will
not disrupt the machine. A separate incident report should correlate
time of symptoms, current process identities and logs. Avoid
automating invasive mitigation based on error counts.

## Repeated ping-recipient process identities (2026-11-04 14:47)

Read-only `ps -p 28948,1139,773,606 -o pid,lstart,ppid,comm,args`
identified the four processes repeatedly mentioned by WindowServer.
All had start times preceding both 14:27 and 14:47 captures:

| PID | Started, local time | Executable | Identification |
| --- | --- | --- | --- |
| 606 | Nov 2 19:40:14 | `/Library/Application Support/JAMF/Jamf.app/Contents/MacOS/JamfDaemon…` | Jamf management daemon (administrative framework) |
| 773 | Nov 2 19:40:22 | `/usr/sbin/universalaccessd` | macOS accessibility system service |
| 1139 | Nov 2 19:41:02 | `~/.local/bin/svim` | SketchyVim, accessibility-based Vim editing for macOS text fields (locally signed build) |
| 28948 | Nov 4 13:27:54 | `/usr/libexec/studentd` | **Apple Classroom** student-control agent (not Screen Time) |

Each process had `PPID=1`, indicating launchd parentage, but
launch context and management policy remain unexamined.
No PID-reuse issue is apparent between the shown process start times
and the captured events; that does not prove any process caused an
error. Jamf confirms its daemon supports administrative monitoring;
Apple's `studentd(8)` manpage identifies it as Classroom's agent.
Neither daemon should be modified or stopped for this test. In
particular, this Mac appears to be managed by Jamf; respect
organizational policies.

**Causal limitation:** A WindowServer message saying a process
`failed to act on a ping it dequeued before timing out` does not
establish that the process initiated the failing
`_CGXPackagesSetWindowConstraints` request. The four recipients
could instead reflect a common broadcast/timeout path. The same
four PIDs preceding both Zen-related and non-Zen application
switches is evidence about shared participation in WindowServer's
state, not individual responsibility.

**Read-only follow-up** to establish context and potential
responsiveness anomalies, without terminating any process:

```sh
ps -p 28948,1139,773,606 -o pid,ppid,uid,stat,%cpu,%mem,etime,command
file "$HOME/.local/bin/svim"
ls -l "$HOME/.local/bin/svim"
codesign -dv --verbose=2 "$HOME/.local/bin/svim" 2>&1
```

The user's desktop-reload configuration explicitly runs
`brew services restart svim` under the label "Reloading SketchyVim".
Upstream SketchyVim (https://github.com/FelixKratz/SketchyVim)
implements Vim editing in macOS accessible text fields; it requests
Accessibility permission and supports a `~/.config/svim/svimrc`
configuration. This establishes the identity and expected functional
domain of the running `svim`, but does not establish which exact
APIs this locally built binary invokes during application activation.
Before an A/B test, confirm which launchd job owns PID 1139.
Do not change Jamf, Apple services, or launchd definitions.

## Live process snapshot and `svim` signature (2026-11-04)

A later `ps`, `file`, `ls -l` and `codesign -dv` read-only check
confirmed that the four repeated ping recipients were still running:

| PID | State | CPU | Memory | Notes |
| --- | --- | ---: | ---: | --- |
| 606 | `Ss` | 2.3% | 0.1% | JamfDaemon, root (UID 0) |
| 773 | `S` | 0.0% | 0.0% | universalaccessd, UID 503 |
| 1139 | `S` | 0.0% | 0.1% | `~/.local/bin/svim`, UID 503 |
| 28948 | `S` | 0.0% | 0.0% | studentd, UID 503 |

The `svim` executable is a 2,222,192-byte native ARM64 Mach-O
binary dated September 16, 2026, signed September 16 at 14:42:47.
Its signing identifier is `com.josephcourtney.svim`, signing
authority `svim-local`, with no Team ID. No special code-signing
flags were reported. These details establish local provenance, but **not** equivalence
to the upstream SketchyVim build, its launch mechanism, entitlements,
granted Accessibility permissions, or the exact handling of window
and focus events.

All four processes were sleeping at the sampled instant; there was
no conspicuous CPU/memory load. A momentary snapshot cannot rule out
earlier deadlocks or response latency at the exact WindowServer ping
times. Do not terminate managed/Apple processes based on ping messages.

To investigate `svim` without running it or stopping it, first
inspect its currently loaded framework dependencies, open handles,
and launch context using read-only commands such as:

```sh
otool -L "$HOME/.local/bin/svim"
lsof -nP -p 1139
launchctl print "gui/$(id -u)" | grep -i -C 3 -F 'svim'
```

The inspection confirms a GUI/Accessibility-capable process:
`otool -L` lists Cocoa, Carbon, AppKit, ApplicationServices,
CoreGraphics and other frameworks; `lsof` reports the `svimrc`
file, `~/Library/Logs/svim.log`, `svim.err`, and a lock file.
The executable's identifier and linked frameworks are insufficient
to attribute WindowServer errors or demonstrate use of a particular
event tap. No production yabai change is justified.

A previous October 22 WindowServer log recorded SketchyVim
checking in as an application, acquiring a SkyLight connection, and
nearby TCC access checks. This is consistent with its accessibility
role, **not** proof that it originated the later constraint failures.

**Inspect before any A/B stop:**

```sh
launchctl print "gui/$(id -u)/com.josephcourtney.svim"
brew services info svim
sed -n '1,120p' "$HOME/.config/svim/svimrc"
tail -n 80 "$HOME/Library/Logs/svim.err"
tail -n 80 "$HOME/Library/Logs/svim.log"
```

The launchd listing also contained `com.josephcourtney.svim-upstream-check`,
`homebrew.mxcl.svim`, and `sh.brew.svim`. Their appearance in
service-registration and enablement output does **not** establish that
multiple processes are active; confirm the owning job and any
KeepAlive/restart policy before choosing a reversible service stop.
Once verified, an explicitly controlled SketchyVim ON/OFF comparison
while leaving yabai stopped is a useful next test. Do not stop
`universalaccessd`, `studentd` or managed Jamf services.

## SketchyVim launchd and log inspection (2026-11-04 15:49 EST)

`launchctl print gui/503/com.josephcourtney.svim` confirms the live
instance is a user LaunchAgent, managed by
`~/Library/LaunchAgents/com.josephcourtney.svim.plist`, running
`~/.local/bin/svim` as **PID 1139**. Launchd reports:
`state=running`, `runs=1`, and `last exit code=(never exited)`.
The job exposes a PID-local `com.apple.axserver` endpoint. This
is compatible with SketchyVim's Accessibility integration, but it
does not prove the origin of any invalid-window request.

`brew services info svim` returned only a Homebrew *formula trust*
error for `felixkratz/formulae/svim`; it **did not** report whether
the Homebrew service itself is active. Do not grant formula/tap trust
for the purpose of this diagnostic.

The last 80 lines from the SketchyVim logs contained three distinct
repeated messages:

- `~/.config/svim/svim.sh: line 19: sketchybar: command not found`;
- `svim already running? Error: Could not acquire lock-file.`;
- `Accessibility not granted. Exit.`.

Because the supplied log excerpts have **no timestamps**, it is
not established whether these messages occurred recently or during
older startup attempts. The current PID 1139 has remained running
since Nov 2; lock failures and denied Accessibility checks may
belong to *other* attempts rather than the current process.
The shell callback may fail because `sketchybar` is absent or
because it is not on the callback's PATH. None of these messages
directly identifies the WindowServer constraint error source.

**Read-only next steps:** identify the meaning of all registered
SketchyVim jobs and inspect the execution context before attempting
a reversible A/B test:

```sh
for label in com.josephcourtney.svim com.josephcourtney.svim-upstream-check homebrew.mxcl.svim sh.brew.svim; do
  echo "== $label =="
  launchctl print "gui/$(id -u)/$label" 2>&1 |
    grep -E 'state =|path =|program =|pid =|runs =|last exit|KeepAlive|runatload|job state'
done
plutil -p "$HOME/Library/LaunchAgents/com.josephcourtney.svim.plist"
stat -f '%Sm %z %N' -t '%Y-%m-%d %H:%M:%S' \
  "$HOME/Library/Logs/svim.err" "$HOME/Library/Logs/svim.log"
command -v sketchybar || true
sed -n '1,45p' "$HOME/.config/svim/svim.sh"
```

The four-job check only inspects service registration/status; missing
labels should be reported as such, not taken as evidence of an active
duplicate. A proper A/B test must identify the one owning process,
confirm restart behavior, and avoid inadvertently leaving SketchyVim
disabled or launching several copies. Do not modify managed or Apple
agents. Source binary/permissions investigation is separate from
the already functionally qualified yabai Zen reconciliation fix.

## SketchyVim service ownership follow-up (2026-11-04 15:52 EST)

The primary `com.josephcourtney.svim` service is loaded from
`~/Library/LaunchAgents/com.josephcourtney.svim.plist`, `state=running`,
PID **1139**, `runs=1`, last exit **never exited**. The plist has
`RunAtLoad=true` but no explicit `KeepAlive`, and configures a
`PATH` containing `/opt/homebrew/bin`.

`com.josephcourtney.svim-upstream-check` is separately loaded,
**not running**, `runs=7`, and last exited **0**; its executable
is `~/.local/bin/svim-upstream-watch`. Searches for the
`homebrew.mxcl.svim` and `sh.brew.svim` service labels returned
no matching status lines (the original command suppressed errors),
so these services cannot be identified as active duplicate editors.
No evidence of more than one running SketchyVim instance was found.

The `svim.sh` callback uses a bare `sketchybar` invocation on
line 19, but `command -v sketchybar` resolves
`/opt/homebrew/bin/sketchybar` and the primary LaunchAgent includes
that directory in its PATH. Historical `command not found` messages
therefore could reflect a different callback environment or earlier
configuration; inspect timestamp and subprocess environment before
changing the script.

The attempted log timestamp command printed
`stat: bad file descriptor`. Prefer the explicit macOS executable:

```sh
/usr/bin/stat -f '%Sm %z %N' -t '%Y-%m-%d %H:%M:%S' \
  "$HOME/Library/Logs/svim.err" "$HOME/Library/Logs/svim.log"
```

### Proposed controlled SketchyVim A/B/A trial (not yet performed)

**Preconditions:** yabai stopped, no active WS diagnostic phase,
only the `com.josephcourtney.svim` job owning PID 1139, and no
ongoing editor activity requiring SketchyVim. On each arm, repeat
the same explicit Kitty → Messages → Kitty focus sequence
and record visible responsiveness. A single trial has high variance
and is not sufficient for causal attribution.

1. **A1, SketchyVim ON:** verify the primary service is running;
   capture a fresh, uniquely named 30-second phase, switch focus
   twice on the same Space, and record event times.
2. **B, SketchyVim OFF:** temporarily boot out **only the user
   LaunchAgent** `gui/$(id -u)/com.josephcourtney.svim`
   (`launchctl bootout`), verify `pgrep -fl svim` and
   `launchctl print` state, repeat the same phase with a new label.
   Leave Jamf, Apple services, the updater job and OS preferences
   untouched.
3. **A2, SketchyVim ON:** restore the original job with
   `launchctl bootstrap gui/$(id -u)
   "$HOME/Library/LaunchAgents/com.josephcourtney.svim.plist"`;
   verify a single new SketchyVim PID, and repeat the phase.
   Restore the job even if a measurement fails.

With each phase, compare the detailed log's actual
`frontmostProcess` transitions and window constraint failures
per transition, not just aggregate error counts or burst count.
Small bursts without a focus transition are separate controls.
If the service cannot be restored, stop the experiment and
restore its original LaunchAgent configuration before more work.

This is a **proposed** A/B/A test, not a claim that SketchyVim causes
WindowServer errors, and it should not be automated to disable
SketchyVim permanently.

## SketchyVim reversible A/B/A results (2026-11-04 15:59–16:01 EST)

The operator ran the proposed test with yabai PIDs empty at both
endpoints of all captures, temporarily booted out *only*
`gui/503/com.josephcourtney.svim`, then restored that LaunchAgent via
`launchctl bootstrap`. The restored instance is running as PID
**10457**. A normal, single service appears to have been restored.

| Phase | SketchyVim | WindowServer invalid-window errors | Bursts |
| --- | --- | ---: | --- |
| `svim-on-before-01` | ON | 22 | 11 at 15:59:58.857–.878; 11 at 16:00:00.377–.381 |
| `svim-off-01` | OFF | 11 | 11 at 16:00:58.859–.878 |
| `svim-on-after-01` | ON, restored | 0 | None |

The ON-before measurement was 15:59:50.059–16:00:20.096,
OFF was 16:00:33.103–16:01:03.136, and ON-after was
16:01:05.283–16:01:35.318. Capture boundaries matter:
the ON-after phase did not include 16:01:58, the time at which a
later minute-aligned event might occur.

**Interpretation:** SketchyVim is **not required** for a recurring
11-error burst. The 15:59:58.857 and 16:00:58.859 bursts were
60.002 seconds apart, and ON and OFF conditions each saw one.
This supports a separate, approximately minute-aligned event
(the earlier 6-error examples were near `:58`, too). The zero
ON-after count does not establish remediation or permanent cessation.
The A/B/A results do **not** implicate SketchyVim; further testing
must account for wall-clock phase and actual focus transitions.
The phase summaries do not contain frontmost-app transitions, so
the intended Kitty → Messages → Kitty transitions are not verified
from this output alone.

The earlier `svim.err` and `svim.log` contents are **historical**:
their file modification timestamps are Sep 16 at 13:57:04
and Sep 16 at 13:36:03 respectively. Errors about
`Accessibility not granted`, lock-file acquisition and missing
`sketchybar` therefore cannot be treated as current November
errors without timestamped corroboration.

**Disposition:** leave the restored SketchyVim service running,
avoid stopping managed or macOS services, and do not modify yabai
geometry, sublayer or reconciliation code on this evidence.
A low-risk next step is to inspect WindowServer context around
15:59:58, 16:00:00 and 16:00:58 (or record future repeated
minute-aligned events without focus switching). Compare
`DoDeferredOrdering`, frontmost-app identity, ping/transaction
timeouts, and the distinct periodic `:58` signature.

## Periodic 11-error signature vs activation-associated bursts (2026-11-04 15:59–16:01)

A separate `log show --style compact --info` window covering
15:59:56–16:01:01 (WindowServer only) was inspected programmatically.
It has **33** `_CGXPackagesSetWindowConstraints: Invalid window`
entries in exactly three groups of **11**:

| Group | First error | Last error | SketchyVim state |
| --- | --- | --- | --- |
| A1 | 15:59:58.857 | 15:59:58.878 | running PID 1139 |
| A1 | 16:00:00.377 | 16:00:00.381 | running PID 1139 |
| B | 16:00:58.859 | 16:00:58.878 | stopped |

The first and last groups started **60.002 s** apart across
SketchyVim state change. The detailed WindowServer context includes
**zero** `DoDeferredOrdering` events, **zero** failed-ping notices
and **zero** `transaction N timed out` messages within this
entire window. There is one keyboard focus-policy reevaluation at
16:00:32.969 explicitly caused by SketchyVim's termination; it is
not accompanied by invalid-window errors.

The log confirms `Process death: ... (svim)`, PID 1139, at
16:00:32.967 and its SkyLight connection
`0xb4b03` closing with `ownedWindowCount=0`. This validates
that the OFF arm genuinely removed the old SketchyVim client from
WindowServer. The third 11-error group occurred 25.9 seconds later,
with the client gone.

An `HID activity: 1 -> 0` event precedes the first group by
~1.135 s; no analogous event immediately precedes the other
minute-aligned group. A sequence of display-3 brightness updates
at 16:00:49–50 precedes the OFF group by ~8–9 seconds, so neither
is currently an established initiating event.

**Working distinction:** At least two observed operational
signatures must be kept separate in analysis:

1. **Small periodic:** 6 or 11 invalid-window log lines near
   wall-clock `:58`, no observed focus transition or massive
   transaction timeouts, persisting without both yabai and
   SketchyVim. Periodicity is supported by multiple timestamps,
   not yet proved over a long continuous no-input baseline.
2. **Activation-associated:** 59/99/109/120/141-error grouped
   bursts following distinct foreground changes, typically with
   failed pings and high volumes of transaction timeouts. These
   may reflect a separate cause or a greater amplification of
   the same underlying invalid reference.

Do not infer the error corresponds to a new distinct window per
line, or that the grouped duration is total WindowServer blockage.
The periodic result proves neither a harmless log-only bug nor a
direct relationship to the earlier whole-desktop input freeze.
Where possible quantify separate counters for no-input baseline
and focus changes.

**Safe next test:** with yabai absent and SketchyVim restored,
run an uninterrupted 130-second `just ws-idle` capture without
switching applications or moving windows; start at any time but
record whether the interval actually includes two or more
wall-clock `:58` boundaries. If needed repeat with a differently
named label. Compare periodic burst count/phase and absence of
`DoDeferredOrdering`. This experiment avoids changing another
service or daemon. If meaningful unresponsiveness appears, stop
interactive tests and collect a separately timed incident record.

## Uninterrupted 130-second baseline (2026-11-04 16:15–16:17 EST)

The user ran `just ws-idle periodic-no-switch-01 130` with no
yabai PIDs detected at the beginning or end. The diagnostic
recorded **240** `_CGXPackagesSetWindowConstraints: Invalid window`
messages across four bursts:

| Start | End | Count | Duration |
| --- | --- | ---: | ---: |
| 16:15:58.852 | 16:15:58.876 | 11 | 24 ms |
| 16:16:58.855 | 16:16:58.876 | 11 | 21 ms |
| 16:17:45.299 | 16:17:45.437 | 109 | 138 ms |
| 16:17:48.295 | 16:17:48.449 | 109 | 154 ms |

Phase interval 16:15:40.982501–16:17:51.026440,
elapsed 130.044 seconds. The two 11-message bursts begin
**60.003 s** apart and align with the previous 15:59:58.857
and 16:00:58.859 events. This strengthens a once-per-minute
wall-clock-aligned mechanism that does not require yabai
or SketchyVim. The exact source remains unknown.

The two **109-message** bursts occur ~2.996 seconds apart in
the same nominally no-switch capture. **Subsequent contextual logs
confirm foreground activations**: Zen at 16:17:45.009–.011 and
Kitty at 16:17:48.002–.005, followed by the corresponding invalid
constraint errors at 16:17:45.299 and 16:17:48.295 (~288–290 ms
later). Both transitions have WindowServer deferred ordering and
frontmost-process updates; the second also has mouse events.
The label `periodic-no-switch-01` therefore does **not** describe
a genuinely uninterrupted no-switch control. The two 109-message
bursts reinforce the activation-associated signature; they are
not evidence of large bursts without foreground changes.

**Next evidence collection (read-only):** obtain all
WindowServer, Dock and SystemUIServer messages spanning
16:17:43–16:17:50 to check whether both 109-error groups
follow a frontmost-app change, `DoDeferredOrdering`,
client ping failures, transaction timeouts, display transitions,
or another event. A wider 16:15:56–16:16:01 and
16:16:56–16:17:01 slice may identify the source of the
minute-aligned 11-message groups. Do not attribute a process
without a corresponding client/request ID or intervention.

## Corrected activation context and periodic-source experiment

The contextual 16:17:43–16:17:50 log confirms that the 109-error
bursts follow **Zen and Kitty activation**, not an otherwise
unchanged desktop. The 11-error groups at 16:15:58.852 and
16:16:58.855 are still consistent with a separate wall-clock
minute-aligned mechanism. Their source is unknown; the current
evidence does not establish a causal link to the historic input
freeze.

**Next passive capture:** With yabai stopped and SketchyVim in
its restored normal state, use a fresh phase name for an uninterrupted
4–5-minute `just ws-idle` capture. Avoid manual app switching,
window manipulation, or display-brightness changes while it runs.
The operator should note any accidental interaction, because the
phase label alone does not validate inactivity. This collects
several :58 boundaries without toggling any other service.

Afterward, retrieve a bounded, information-level WindowServer
context covering the same interval (substitute the exact start/end
times from the phase JSON):

```sh
log show --style compact --info \
  --start '2026-11-04 HH:MM:SS' \
  --end '2026-11-04 HH:MM:SS' \
  --predicate '(process == "WindowServer" OR process == "Dock" OR process == "SystemUIServer")' \
  > /tmp/yabai-ws-periodic-context.log
```

For each :58 burst, compare the preceding 3 seconds and following
1 second for deferred ordering/frontmost-process changes, failed
pings, transaction timeouts, display changes, timer-like log
messages, and consistent connection/window identifiers. Report
negative findings only for message classes actually present in
the collected logging level; a missing log line is not proof the
underlying event did not occur. Compare with at least one matched
non-burst minute. Keep periodic and activation-associated
signatures in separate summaries. Do not attribute an individual
process without a client/request identifier or a discriminating
intervention. No Apple/Jamf service changes are authorized.

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
