# Live BSP resize hardening: integration and qualification

**Status:** Operator compiled `c43f4261b03c` (83 Python, 18 native, ARM64 signed, preflight PASS); live size reset and cross-display tug-of-war are now reported fixed. Gate C still fails for jittery live resizing and apparent gap/bottom-padding violations. The next deduplication patch requires fresh macOS qualification.  
**Branch:** `feature/live-bsp-resize-hardening-20261106`  
**Base:** `fix/zen-detach-reconciliation-20261104`  
**Reference:** Christian-SC26/yabai `a26707adb304bae0b415d91e70e0285af2163c68` (2026-11-05), including v7.1.33, v7.1.34 and v7.2.1 fixes.

## Scope and key behavioral decisions

- `mouse_bsp_live_resize on` (default) moves managed BSP split boundaries and neighboring real windows while dragging, at a bounded ~33 ms step rate. It uses direct AX frame application rather than animation proxies for interactive steps.
- `mouse_bsp_live_resize off` uses classic resizing: resize the grabbed real window while dragging; adjust the matching BSP split on mouse release. `mouse_live_resize` is a compatibility alias.
- A dragged window's **initial** pointer position is independent of `down_location`, which advances as resize steps are applied. Applied steps count as movement even when the pointer returns near its origin.
- After a successful BSP live split adjustment, the handler does not apply a fallback free-window resize along an unsupported axis.
- Consecutive `MOUSE_MOVED` or `MOUSE_DRAGGED` events may be coalesced; mouse-down/up and nonmotion events remain barriers. Skipped retained `CGEventRef` values are released.
- The source and destination BSP checks both defer relocation **only for the actively modifier-moved window**, until mouse-up. When a window is rehomed after release, the vacated visible source view is flushed.
- Stack-layout insertion feedback covers the whole stack instead of suggesting a BSP split.
- Neither the forthcoming pure planner nor preview-only organization gestures from root [DESIGN.md](../DESIGN.md) are implemented here. Moving a real window during a modifier-drag still occurs.

### Source locations

| Component | Changes |
| --- | --- |
| `src/mouse_handler.[ch]` | `origin_location`, `resize_applied`, `bsp_live_resize`, gesture-moved and active-move predicates |
| `src/event_loop.c` | Release classification, on/off resize execution, bounded cadence, event coalescing, stack feedback |
| `src/window_manager.c` | Direct resize flush, defer source removal and destination insert while moving, flush vacated Space |
| `src/view.[ch]` | Direct BSP frame flush with changed-frame filter |
| `src/message.c` | On/off config and alias |
| `tests/src/mouse.c`, `tests/src/tests.m` | Native regression fixtures and registration |
| `doc/yabai.asciidoc`, `doc/yabai.1` | Config documentation |

## Gate A — Compile and local regression tests

Run on an isolated clean checkout of **this branch** on macOS, not from a source tree with old generated binaries:

```sh
git status --short
git rev-parse HEAD
just test-local
just build
just qualify-preflight
```

These commands have **not** been executed as part of the source-only integration. The current execution environment does not have a macOS SDK or access to the running desktop. Do not interpret newly committed tests as passing tests until the above commands complete.

Verify the native test runner includes tests for: live default, stationary click, threshold crossing, return-to-origin after resize, incremental versus original pointer coordinates, frame delta classification, active-move identity, and motion coalescing boundaries.

If the build fails, fix the actual compiler error on this branch before proceeding. After a successful signed build, record the executable hash, architecture, commit, macOS build and test totals in the qualification record below.

## Gate B — Confirm foreground daemon provenance

Follow existing [Zen qualification safety rules](zen-reconciliation.md). Use a separate, explicit foreground test daemon with an inert config; do **not** start or stop the user's installed service as a side effect.

```sh
just qualify-running
just smoke
# Only if a version-matching healthy SA already exists:
just smoke-sa
./bin/yabai -m config mouse_bsp_live_resize
./bin/yabai -m config mouse_live_resize
```

Both config queries must report the same `on`/`off` state. In a **disposable** layout-only session, test the on/off setter and alias by querying readback after each change. Restore `on` after qualification. Never change SIP, Dock, SA installation or user startup hooks for the test.

## Gate C — Guarded operator-driven functional tests

Create a disposable BSP Space with at least three known test windows (e.g. Kitty or a fixture app). Verify the currently active Space index, stable ID, type and window IDs **immediately before each stage**. Use only the signed foreground qualification daemon. All mouse gestures are performed deliberately by the operator.

Read-only snapshots:

```sh
./bin/yabai -m query --spaces |
  jq '.[] | {id,index,type,"has-focus": ."has-focus",windows}'
./bin/yabai -m query --windows |
  jq '.[] | {id,app,title,"is-floating": ."is-floating",frame,space,display}'
./bin/yabai -m space --dump-layout
```

Do not target Space indices from earlier logs; macOS may have renumbered them. Only read the BSP dump from an explicitly verified BSP Space. Exclude non-test windows from the acceptance fixture.

### C1 — Live mode

1. Set `./bin/yabai -m config mouse_bsp_live_resize on`.
2. Drag a vertical boundary: verify both neighboring windows move continuously in controlled increments, and end at the expected shared boundary.
3. Drag a horizontal boundary and then both axes near a corner. Verify the unsupported axis cannot leave a client outside its tile after the other split has moved.
4. Pause with the mouse held; release without moving further. The final ratio must remain stable, and no second resize adjustment is allowed.
5. Move the pointer away and back close to its original location before release. A real applied resize must not be mistaken for a stationary click.
6. Repeat quickly across nested BSP nodes and near permitted minimum sizes. No flicker, repeated animation proxy generation, overlapping frames or orphaned BSP leaves.

### C2 — Classic mode

1. Set `./bin/yabai -m config mouse_bsp_live_resize off`.
2. Drag one managed window and pause: the grabbed window alone changes during the gesture; the BSP split is reconciled on release, and neighboring windows settle to the correct layout.
3. Repeat both axes and the no-matching-fence case. Test a click without a drag: no layout mutation.
4. Read the alias with `./bin/yabai -m config mouse_live_resize`, then set via alias and verify canonical readback. Restore the original setting.

### C3 — Cross-display movement and event handling

1. On two or three displays, select a managed test window on source display A and a valid target on display B. Start a modifier **move** drag, not a resize.
2. Cross boundaries repeatedly while button is held. Check that the window is **not prematurely removed from its source BSP tree or reinserted into the destination BSP** by reconcile notifications before release.
3. On release, confirm a single logical destination placement, correct source gap closing, valid destination layout and no bounce-back. Run both target-window and empty-target drops.
4. Verify unmodified/native macOS drags still operate as before. This branch does *not* claim to fix every native cross-display transfer problem; record any remaining movement-loop symptoms separately.
5. Test consecutive fast drag events, rapid release after a long drag, mouse click/double-click and right-click without dragging, and daemon stop/restart. Ensure no lost mouse-up, stuck click, transient unresponsive keyboard, input freeze or stale insertion-feedback window.

### C4 — Regression coexistence

Repeat Zen's [six-stage tab-detachment acceptance](zen-reconciliation.md) if the environment remains disposable and safe. Validate focus-follows-mouse, floating and stack windows, layout persistence read/dump, and sublayer restoration. Native macOS tab grouping is intentionally **not** part of this branch; qualify against the future tab-integrated branch again.

## Evidence record (complete only after actual execution)

The original candidate `63b0a76ba22b` passed 83 Python tests, 11 native C tests, optimized ARM64 build, signing and runtime identity/smoke checks (PID 61919; 3 displays, 6 Spaces, 13 windows). The operator then observed live resize overlap at application minimum height and persistent cross-display move oscillation. These are failures, not acceptance. The corrective source changes learn minimum dimensions from AX readback, clamp split ratios against nested subtree minima, roll back rejected geometry, and defer BSP capture of the actively moved window until mouse-up. The first correction compiled and passed 83 Python and 15 native tests, signed and ran on macOS at `e328853f7870`; Gate C exposed severe live-resize flashing. The new patch removes immediate AX-readback rollback, learns minimum dimensions only from repeated delayed stable observations, and prevents competing neighbor-window AX notification flushes during a live resize. **This new correction has not yet been compiled or exercised on macOS.**

| Evidence | Result |
| --- | --- |
| Candidate commit and clean status | Original tested SHA: `63b0a76ba22b`; corrective SHA requires fresh build |
| macOS build and architecture | Original candidate: optimized ARM64, passed |
| `just test-local` result and test totals | Original candidate: 83 Python + 11 native C passed; corrective changes untested |
| `just build` and codesign | Original candidate: passed; corrective changes untested |
| `just qualify-preflight` | Original candidate: blocked while old daemon ran; later correct foreground daemon verified |
| Foreground daemon executable/build stamp | Original candidate: verified PID 61919, matching `~/code/yabai/bin/yabai` |
| Mode on/off query and alias | Both returned `on` during Gate B; operator confirms `on` for failed live resize |
| Live nested BSP and no-fence axis behavior | **FAIL**: min-height overlap observed in live mode; fix unqualified |
| Classic release and stationary click | Not yet fully qualified |
| Multi-display drag/retiling/source-gap result | **FAIL**: modifier move oscillation persists; fix unqualified |
| Mouse lifecycle / input responsiveness | Not yet fully qualified |
| Zen and sublayer regression | Not requalified on corrective changes |

**Promotion rule:** Keep the PR draft and do not merge into the active integration baseline until Gates A, B and the required parts of C pass against the final candidate SHA. If a phase fails, fix narrowly and rerun the affected acceptance stages. No GitHub Actions or automatic macOS desktop/system mutations.
