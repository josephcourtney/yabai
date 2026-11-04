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

Substitute the verified Space index and persistent ID below. These commands
only inspect the daemon and write local qualification state; **the operator
performs all window creation, dragging, closing and Space switching manually.**

```sh
just qualify-zen baseline <INDEX> <ID>
# Open one ordinary new Zen window; allow about two seconds.
just qualify-zen ordinary <INDEX> <ID>
# Detach one tab into a new window; allow about two seconds.
just qualify-zen detach1 <INDEX> <ID>
# Detach another tab into a new window; allow about two seconds.
just qualify-zen detach2 <INDEX> <ID>
# Switch to a different Space and return to the test Space.
just qualify-zen returned <INDEX> <ID>
# Close just the two detached windows, keeping original and ordinary.
just qualify-zen closed <INDEX> <ID>
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

- **Observed 2026-11-04 on integration revision `3fb3bfeb`:** `--refresh`
  found two missing Zen windows; explicitly enabled reconciliation then
  registered and visibly tiled a detached Zen window automatically.
- **Not yet established for this branch:** newly compiled default-on
  initialization, end-to-end staged tests, busy-process latency, sleep/wake,
  and cross-Space repeated detachment/closure.

No GitHub Actions or system-wide service modifications are part of this work.
