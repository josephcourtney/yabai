# Live BSP resize hardening: integration and qualification

**Status (2026-11-06):** At `7c2e68cfd19e`, 83 Python tests and 30 native tests PASS, signed ARM64 build and preflight PASS, running-daemon provenance PASS (PID 16622). Operator reports visual preview works on two landscape monitors and a 90-degree-rotated portrait monitor after the Quartz coordinate correction. The older AX live-resize mode still has reported jitter; interruption, min-size commit and broader regression acceptance remain pending. The subsequent preview lifecycle patch requires a new native build and scoped GUI verification.  
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

These commands **passed on the operator's Mac** for `7c2e68cfd19e` (83 Python and 30 native tests, signed ARM64 build, preflight). The next source revision must be rebuilt and requalified separately. The assistant cannot execute the macOS GUI tests; do not infer their success from unit tests.

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

## C5 — Experimental preview mode (operator acceptance pending)

1. On a disposable BSP Space, record `mouse_bsp_resize_preview`, `mouse_bsp_live_resize`, `mouse_live_resize` and the Space layout. Verify preview defaults to off in a fresh daemon.
2. Enable preview. Test top, bottom, left, right and nested splits on landscape and portrait displays. Confirm that only the overlay changes during dragging; release updates the real windows exactly once. Verify the intended stationary edges, gaps and padding.
3. Test click-without-drag, drag then return to the original pointer position, and rapid repeated drags. Verify no unintended commit or stale overlay.
4. Try resizing beyond an application's minimum width/height. Record whether the committed AX geometry differs from the overlay. Do not mark a mismatch as accepted.
5. Interrupt an active preview with Mission Control, Space change, display focus change, display reconfiguration, and source-window closure. The overlay must disappear and real window geometry must remain unchanged. No later drag event may silently resume AX resizing.
6. With preview off, verify the existing live and classic paths independently; known live-mode jitter may remain, but no new geometry or mouse-up regression is acceptable.
7. Repeat the cross-display modifier-move scenario, Zen tab-detach registration, and floating/stack/sublayer regressions. Record outcomes separately from preview acceptance.

No installed service, SIP, Dock, or personal session-restore changes are part of this qualification.

## Current candidate evidence (operator logs, 2026-11-06)

| Check | Evidence at `7c2e68cfd19e` |
| --- | --- |
| Source | Clean `feature/live-bsp-resize-hardening-20261106` |
| Python suite | 83/83 PASS |
| Native suite | 30/30 PASS |
| ARM64 optimized build | PASS; Apple Development signing verified |
| Build stamp | SHA256 `570d90d958b560ca`, tracked_dirty=false |
| Preflight | PASS, no running yabai daemon |
| Running foreground candidate | PASS, PID 16622, executable and stamp verified |
| Preview, landscape monitors | Operator reports working on both |
| Preview, rotated portrait monitor | Operator reports inversion fixed and working |
| Interruption, minimum-size commit, Zen and cross-display regressions | Not yet demonstrated on this candidate |
| Existing continuous AX resize | Jitter remains a known unresolved limitation |

The WindowServer invalid-window counts (450/30 s at preflight, 330/30 s while running) are observations, not proof of a regression or its cause.

## Historical evidence record (prior candidates)

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


## Experimental visual BSP resize preview (2026-11-06)

The separate mouse_bsp_resize_preview on|off option (default off) previews the
proposed BSP layout using a noninteractive translucent WindowServer overlay.
It clones the BSP tree; the authoritative tree and real application geometry
are not modified until mouse-up. The overlay is released on mouse-up, gesture
invalidation, source-window destruction, or Space view teardown. Stale
topology/ratio/Space state aborts commit rather than overwriting external
changes. Zoomed layouts are deliberately rejected until preview semantics are
defined. No application contents reflow during dragging.

The previous AX-driven mouse_bsp_live_resize mode is retained unchanged.
Preview on takes precedence over live mode. Preview off restores the previous
live/classic choice; mouse_bsp_live_resize off still selects classic.

Commands (run after macOS build and foreground daemon verification):

    yabai -m config mouse_modifier alt
    yabai -m config mouse_action2 resize
    yabai -m config mouse_bsp_live_resize on
    yabai -m config mouse_bsp_resize_preview on

Check: pointer drag draws stable prospective tile bounds with preserved gaps and
outer edges, while actual windows do not change. Mouse-up applies once. Compare
with mouse_bsp_resize_preview off (live AX mode) and then with
mouse_bsp_live_resize off (classic). Check app minimum-size rejection at
commit, mouse-up without motion, display/Space switching, source window close,
multi-display behavior, and that overlay never intercepts mouse input. Overlay
rendering uses private WindowServer APIs and requires runtime qualification.

The parent BSP extent is now partitioned using complementary integer extents,
so the stationary outside edge cannot wander by one pixel as ratios vary.
This geometry invariant applies to all BSP modes.

Status: At `7c2e68c`, signed macOS build, 83 Python tests, 30 native tests,
preflight and running-daemon checks passed. Basic multi-monitor visual preview
was reported working by the operator. The subsequent lifecycle patch and
remaining Gate C cases are not yet qualified. PR remains draft.

Preview mouse-up bypasses ordinary window-drop classification entirely; it commits or cancels the shadow BSP once, then clears gesture state. This prevents optimistic AX frame changes at commit from being misread as a window move.

If Mission Control interrupts a preview, abort the entire resize gesture. Do not fall back to live AX resizing before the mouse button is released. If a parent area is smaller than its configured gap, clamp the effective gap to the parent extent and never generate negative tile dimensions.


## Portrait-display preview inversion (2026-11-06)

Operator qualification at HEAD 6ffa995: 83 Python tests PASS, 28 native
tests PASS, arm64 build/signature PASS, preflight PASS. Visual preview works
on two landscape displays, but the preview rectangles appear upside down on
a 90-degree-rotated portrait display. This is Gate C visual FAIL.

Cause identified in bsp_preview_draw_leaves: global BSP tile coordinates are
top-left/y-down; the Quartz overlay drawing context is bottom-left/y-up. The
old code subtracted the overlay origin but did not invert the local Y axis.
A left/right split can hide the defect; a top/bottom split exposes it.

Corrective patch converts every tile to local Quartz coordinates:

    local_x = tile.x - root.x
    local_y = root.h - (tile.y - root.y) - tile.h

Do not rotate based on display orientation: the correction is a coordinate
convention conversion, not a physical monitor rotation. The same conversion
is applied to all displays. Two native tests cover portrait top/bottom tiles,
landscape left/right tiles, nested quadrants and nonzero/negative origins.

Operator subsequently reports the preview works on all three displays after
`7c2e68c`. The precise mouse-up geometry, interruption and regression cases
in C5 remain separately pending.


## Space/display interruption hardening (post-7c2e68c)

Code review identified that an overlay could remain visible after a Space
or display change until the next pointer event. The new abort helper cancels
the overlay and ends the resize gesture on Space/display activation, display
add/remove/move/resize, and Space destruction. The Mission Control abort paths
reuse the same helper. Preview mode also no longer claims live AX notification
ownership if the separate live-resize setting is enabled. Two native tests
cover abort-state idempotence and mode isolation.

This patch is not yet compiled or operator-qualified. Repeat C5 interruption
tests on the new exact SHA before merging.


## AX minimum-size / off-screen preview regression (post-81aa597)

The operator observed that ChatGPT's preview tile could be made narrower than
macOS's ordinary window-resize minimum. On mouse-up the client retained a larger
size while accepting yabai's proposed position, extending the window off-screen.

The experimental correction performs a **size-only, shrink-only preflight** on
mouse-up, with bounded, repeated AX readback (up to 200ms for the whole view).
It learns the observed minimum, clamps BSP ratios recursively (including nested
subtrees), and only then writes final positions. If observations do not settle,
or constraints cannot fit, it restores the previous BSP split instead of
committing an unverified off-screen placement. This is a release-time safeguard,
not a guarantee that the first preview outline knows a new application's limit.

**Operator acceptance (not yet executed):**
1. With ChatGPT and another window on a disposable Space, preview a width below
   ChatGPT's native minimum. The outline may initially be optimistic, but the
   committed window must remain within its assigned on-screen tile; the neighbor
   must be resized consistently, without overlap.
2. Repeat immediately: the learned minimum should now constrain the preview.
3. Check left- and right-side ChatGPT placement, top/bottom splits, nested BSP
   trees, gap/padding, and the portrait monitor.
4. Check applications with responsive AX resizing and applications that refuse
   AX resizing. An unsettled or infeasible preflight must restore the previous
   layout, not commit an off-screen window.
5. Measure the mouse-up latency and any transient overlap. The preflight may
   wait up to 200ms and may resize window contents before moving them; it does
   not alter drag-time responsiveness.
6. Requalify both independent live/classic resize modes, cross-display moves,
   Zen tab-detach and interruption handling.

The new source and native tests require a fresh macOS build and runtime
qualification; no PASS is claimed for this patch.
