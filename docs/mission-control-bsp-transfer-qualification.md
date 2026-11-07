# Mission Control BSP Space-transfer qualification

## Reproduction and suspected race

Reported on 2026-11-06 after PR #8 was merged into the fork's `main`:
dragging a managed window from one BSP Space to another through macOS Mission
Control can leave the **remaining windows on the source Space at their old
sizes** until a subsequent Space switch.

The existing `MISSION_CONTROL_EXIT` handler runs one pass of
`window_manager_correct_for_mission_control_changes`. WindowServer can still
report the previous Space membership at that instant. A later `SPACE_CHANGED`
handler repeats validation and makes the source BSP tree catch up.

The candidate retains the immediate pass and schedules three **bounded**
post-exit checks at 150, 450 and 1200 ms. Follow-ups inspect only visible user
Spaces, do not invalidate unchanged layouts, and are canceled by a new Mission
Control session. They do not run while Mission Control is active. No timer
remains active after the bounded checks.

## Operator qualification — pending

Use two disposable BSP Spaces and disposable windows. Do not use existing work
Spaces, change installed services, inject a scripting addition, or alter SIP.

1. Stop the existing **foreground qualification** daemon. On the candidate
   branch run `just test-local`, `just build`, `just qualify-preflight`,
   then `just run` in the foreground. Run `just qualify-running` in a second
   terminal. Expected: 83 Python tests and **38 native tests** (subject to
   actual local output).
2. On source Space A, arrange at least three managed test windows and record
   `./bin/yabai -m query --spaces` and
   `./bin/yabai -m query --windows --space`. Prepare destination BSP Space B.
3. Open Mission Control and drag one test window from A to B. Return to A
   **without switching to B first**. The remaining windows must fill the
   vacated tile within the bounded 1.2 s reconciliation window. Confirm the
   moved window is no longer in A's BSP window list.
4. After that observation, switch to B. The transferred window must be managed
   once, with no duplicate, and the other B windows must retile correctly.
5. Repeat A→B with A and B on the same display, and with the Spaces on
   different displays. Include the portrait display, nested BSP splits, and
   a source with only two windows.
6. Verify no unexpected retile during Mission Control, no stale callbacks after
   quickly reopening it, no repeated AX jitter on unaffected windows, and
   no changes to normal modifier-drag moves, preview resizing or Zen detach.
7. Optionally run the WindowServer diagnostics before/after; do not attribute
   invalid-window log counts to this patch without a controlled comparison.

## Evidence requirements

Record the exact candidate SHA, signed build provenance, Python/native test
counts, foreground daemon identity, Space/window IDs, before/after geometry,
and whether the source reflow occurred without a Space switch. A source-level
retry test is not a substitute for native macOS Mission Control acceptance.

**Status:** candidate code committed; not compiled or GUI-qualified on macOS.
