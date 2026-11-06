# Zen tab-detachment reconciliation

## Observed failure and repair

On 2026-11-04, two detached Zen windows were absent from `query --windows`
until `window --refresh` queued two previously untracked windows. With the
existing 750 ms `window_reconciliation on` setting, a subsequent detached tab
was registered automatically and visibly tiled into the BSP. Its query entry
had `has-ax-reference=true` and `is-floating=false`.

The likely cause is missing macOS AX window-creation/focus notifications from
Firefox-derived tab detachment (compare Mozilla Bug 2079565). This is **not**
a verified diagnosis of Zen's specific binary or Firefox patch level.

This branch makes **Zen-only reconciliation enabled by default** and starts
the timer when the daemon message loop starts, even when the startup config
does not mention the setting. The implementation:

- Inspects only non-hidden registered applications named exactly `Zen`.
- Runs scanning on yabai's event loop; timer events are coalesced.
- Queues newly discovered AX windows via `WINDOW_CREATED`, preserving
  observation, rules, BSP insertion and `window_created` signals.
- Checks for existing window IDs in both discovery and creation handling.
- Keeps manual `window --refresh` available for all non-hidden apps.
- Allows `config window_reconciliation off` and intervals of 250–10000 ms;
  the default remains 750 ms.

The fallback does not require changing Dock, SIP, launchd or a window rule.
It may be disabled if AX scanning causes measurable latency. Retain the opt-out
and regression tests until the upstream browser fix is verified in Zen.

## Offline validation

Run from a clean checkout of this branch:

```sh
just test-local
just build
just qualify-preflight
```

`test-local` includes cross-platform fixture tests and source-wiring checks.
They do not substitute for a signed macOS build or live desktop qualification.
Do not change Dock, SIP or a system-wide yabai service as a side effect.

## Guarded, read-only live acceptance

Use a separate macOS test session and the explicitly started foreground
qualification daemon. Keep Scroot closed during qualification in light of the
previous independent WindowServer/input incident. `just qualify-running`
verifies the actual daemon executable and its build stamp; a successful
`./bin/yabai -m` query alone does not prove daemon identity.

Create or choose an explicitly **disposable BSP Space**, activate it and
open an initial Zen window there. Label only that Space:

```sh
./bin/yabai -m space --label yabai-zen-qualification
./bin/yabai -m query --spaces | jq '.[] | select(.label == "yabai-zen-qualification") | {index,id,type,"has-focus": ."has-focus",windows}'
just qualify-running
```

**The label is not sufficient.** Verify that `type` is `bsp` and
`has-focus` is `true`. A floating Space is *not* suitable: `--dump-layout`
is BSP-only. On 2026-11-04 the first live attempt targeted Space index 5,
persistent ID 211, which was `type=float` with two existing windows.
The baseline consequently failed; later phases failed only because no
baseline state file existed. This was a harness precondition failure, not
evidence that Zen reconciliation failed. Do **not** switch that or any
other real work Space to BSP just to make the test pass. Create/select a
separate disposable BSP Space using deliberate user-controlled operations.

The first test Zen window should have several tabs so detaching one does
not close its source window. Do not start the next phase unless the preceding
phase prints `PASS` and records the state file.

Set the *verified* Space index and stable ID as shell variables, then run
these commands in sequence. The qualification script only inspects the
daemon and writes local state; **the operator performs all window creation,
dragging, closing and Space switching manually.**

```sh
space_index=REPLACE_WITH_INDEX
space_id=REPLACE_WITH_ID

just qualify-zen baseline "$space_index" "$space_id"
# Open one ordinary new Zen window; allow about two seconds.
just qualify-zen ordinary "$space_index" "$space_id"
# Detach one tab (without closing its source window); allow about two seconds.
just qualify-zen detach1 "$space_index" "$space_id"
# Detach another tab (without closing its source window); allow about two seconds.
just qualify-zen detach2 "$space_index" "$space_id"
# Switch to a different Space and return to the test Space.
just qualify-zen returned "$space_index" "$space_id"
# Close just the two detached windows, keeping original and ordinary.
just qualify-zen closed "$space_index" "$space_id"
```

Each phase fails closed unless the same active, labeled persistent Space ID is
a BSP; all expected Zen IDs are unique, present, AX-backed, non-floating and
members of the intended Space; and the number of Zen entries in its **actual
BSP tree** matches the number of registered Zen windows. The latter check
uses the read-only `space --dump-layout` command. It proves inclusion as a
set, not per-window identity or exact leaf ordering, because the dump records
application names rather than native window IDs.

The default state path is `/tmp/yabai-zen-qualification.json`, and baseline
refuses to overwrite it. Supply a fresh fourth argument to `just qualify-zen`
for another run. The test refuses skipped stages, unexpected disappearance,
missing registration and lingering windows after closure. Visually verify
tiling and responsiveness as well.

### Optional negative control

In an **isolated** test session, disable reconciliation, detach a fresh Zen
tab and inspect `query --windows` *before* calling `window --refresh`.
Record whether the new window is missing, then perform a manual refresh and
check the result. Reenable reconciliation afterward:

```sh
./bin/yabai -m config window_reconciliation off
# Manually detach a tab; query --windows first.
./bin/yabai -m window --refresh
./bin/yabai -m config window_reconciliation on
```

This control changes runtime configuration and can register missed windows;
do not perform it between phases of the staged read-only acceptance test. If
a future Zen version emits the expected AX notifications, the control may no
longer reproduce the defect; that is not a failure of the fallback.

## Qualification status

### Completed: macOS runtime acceptance (2026-11-04)

- **Build and offline tests:** `490c05342f366b49ec697b10c7c0b31dcedc0f81`
  passed 60 Python tests and two native C tests. The ARM64 binary was built
  and Apple Development signed, with SHA-256 prefix `01aba8e4068af48b`.
  `just qualify-preflight` passed, and the runtime check verified that the
  foreground process actually ran the signed binary.
- **Dedicated BSP test Space:** index 5, persistent Space ID 245, label
  `yabai-zen-qualification`. Kitty (8200) stayed present to operate the
  test. After the initial floating-Space setup error, the Space was configured
  to BSP, and the qualification state was preserved between stages.
- **All six sequential live stages passed**, without `window --refresh`:
  1. `baseline`: Zen window 8667.
  2. `ordinary`: an additional Zen window 8755.
  3. `detach1`: detached Zen tab became window 8768.
  4. `detach2`: second detached Zen tab became window 8777.
  5. `returned`: expected window inventory and BSP membership still held
     after the operator-reported Space switch and return.
  6. `closed`: detached windows 8768 and 8777 disappeared while original
     windows 8667 and 8755 remained.
- Every stage reported `PASS` under daemon PID 75826, with verified binary
  provenance, matching Space identity, AX references, nonfloating window
  state, and expected Zen count in the live BSP tree. The tree dump validates
  count and application names, not exact native IDs or tile ordering.
- **Result:** the previously reproduced missed Zen tab-detachment registration
  did not recur during these two consecutive detachments with the Zen-only
  reconciliation fallback enabled. This is a successful functional acceptance
  test, not proof that every future Zen build or macOS transition is covered.

### Separate unresolved WindowServer error investigation

A read-only, phase-bounded probe and controlled investigation procedure are
available in [windowserver-constraints-investigation.md](windowserver-constraints-investigation.md).
The experiment separately samples no-daemon, reconciliation OFF/ON, BSP/FLOAT,
and automatic-sublayer conditions. The diagnostic scripts do not manipulate
windows, restart services, or change configuration.


- The WindowServer emitted intermittent
  `_CGXPackagesSetWindowConstraints: Invalid window` bursts during desktop
  operations. The `just qualify-running` 30-second counts included 195 at
  `ordinary`, 177 at `detach1`, 307 at `detach2`, 77 at `returned`,
  and 195 at `closed`.
- Following a 35-second period with **no yabai daemon**, preflight recorded
  6 errors in the prior 30 seconds. An idle within-daemon comparison measured
  6 with reconciliation OFF versus 0 with reconciliation ON. These observations
  **do not** support a claim that the 750 ms reconciliation timer alone causes
  the bursts.
- The uploaded WindowServer excerpt shows repeated subsecond bursts of
  identical errors, but does not identify the initiating process or invalid
  window ID. The relationship to yabai, BSP operations, Dock/window-layer
  behavior, or other components is unresolved.
- Functional testing may be considered **passed**, but do not classify the
  separate desktop/window-server safety issue as resolved on this evidence.
  Avoid promoting this feature branch to a production merge until the
  operational risk and reproducibility have been evaluated.

### Earlier failed setup attempt (not a functional failure)

The first test targeted Space 211, which was floating; the BSP layout dump
failed before a baseline state could be written. The harness was corrected
to validate identity, focus, label and BSP layout before querying the tree,
with additional offline regression cases. A daemon restart later reset the
disposable Space layout and label; the operator explicitly restored them.

### Remaining coverage limits

Extended stress/latency measurements, sleep/wake, multi-display behavior,
repeated cross-Space detachments, and classification of the WindowServer
bursts remain unqualified. The fallback can be disabled with
`config window_reconciliation off`; the disabled case must not be assumed
to auto-discover detached Zen tabs.

No GitHub Actions or system-wide service changes are part of this work.
