# PLAN — Finish fork integrations, then separate interaction from window management

**Status:** Approved sequence; documentation only. No implementation changes were made by creating this plan.  
**Updated:** 2026-11-06  
**Architectural specification:** [DESIGN.md](DESIGN.md)  
**Existing inventory:** [docs/fork-inventory-2026-11-04.md](docs/fork-inventory-2026-11-04.md)

## 0. Scope and decision

**Required order:**

1. Finish integration and qualification of **native macOS tab grouping** and the fixes accumulated in other forks.
2. Finish integration and qualification of **live BSP resizing** and its later correctness/performance improvements.
3. Complete the combined acceptance gates against the resulting baseline.
4. **Only then** implement the architecture in [DESIGN.md](DESIGN.md): gesture capture -> provisional layout/functional overlays -> semantic intent -> bounded commit/reconciliation.

Do not bundle the redesign into either feature-porting branch. Keep the entire effort **functionality-first**: no built-in focus ring, decorative overlays, custom window/Space transitions, wallpaper effects or alternate animation engine.

The proposed interaction overlay is functional feedback and is deferred to the redesign phase, not added as part of native-tab or live-resize integration.

## 1. Starting point and source provenance

### Our fork

Repository: [josephcourtney/yabai](https://github.com/josephcourtney/yabai).

As inspected before this plan:

- `master` is still upstream-equivalent at `dd845723416f`. Do **not** assume any integration feature is on `master`.
- The documentation and qualified Zen code are on `fix/zen-detach-reconciliation-20261104`, built on `integration/pr2-5-qualification-20261102`.
- PR #1 (focus/BSP/fullscreen correctness) and PR #6 (sublayer transition repair) were merged to `compat/macos-27.0.1`.
- PRs #2–#5 (mouse tap, hybrid manage, sessions, live resize) and the combined PR #7 remain **open drafts**, though their implementation is already included in the Zen branch. Their presence in source **does not** mean every runtime test passed.
- Zen-only AX reconciliation, default 750 ms polling, passed six staged macOS live acceptance steps on signed commit `490c05342f36`. Extended stress/sleep/multi-display remains unqualified.
- Earlier PR #7 native/build gate passed on `3fb3bfeb96ba` (37 Python tests and two native C tests). Its later app+title matching has not been completely runtime-accepted for restore. Do not apply `--load-session` to normal workspaces.

### Reference forks (pin before porting)

- [Christian-SC26/yabai](https://github.com/Christian-SC26/yabai) is the preferred **native tab** and **live BSP** source. On 2026-11-06 its `master` included v7.3.2 and additional later source changes through `a26707adb304`. Record the exact upstream SHA in every port and re-check it at the start of implementation; do not treat a moving branch name as a reproducible input.
- Native tabs originate with [Christian `04764aa03`](https://github.com/Christian-SC26/yabai/commit/04764aa03f5078cd9b651b15f3931fb0f559ff26), based in part on jpatrolla's work. Subsequent essential fixes include `039fae3c8` (real tabs/initialization and disabling), `3cbb2a5be` (floating groups), `f20dd3bc3`, `9db612ce7` and `ac2525e32` (opacity), and [`3876b2f59b8f`](https://github.com/Christian-SC26/yabai/commit/3876b2f59b8fdad085adf3735411862e209d58d5) (ignore hidden tab's stale Space when following an app).
- [`a26707adb304`](https://github.com/Christian-SC26/yabai/commit/a26707adb304bae0b415d91e70e0285af2163c68) prevents stack-array overflow past 32 entries. Treat this as a **required safety dependency** before expanding tab and stack interactions.
- Native-tab design reference: [jpatrolla/yabai](https://github.com/jpatrolla/yabai), but prefer the current Christian-SC26 implementation with its later corrections rather than an early experimental branch.
- Live BSP base was imported earlier as our PR #5 from Christian-SC26. Newer changes: Christian v7.1.33 (direct nonanimated flush, event coalescing, live/classic configuration and throttling), [`6f68eebec`](https://github.com/Christian-SC26/yabai/commit/6f68eebec) in v7.2.1 (mouse-up/drag-origin and wrong-axis correction), and v7.1.34 (cross-display drag/drop retiling and source-gap cleanup). Compare source and select focused patches; do not blindly cherry-pick across diverged trees.

**Before porting**, capture source SHA, base SHA, dependency chain, each touched function, and candidate acceptance tests. Update this plan's status records with actual commits, not merely “merged”.

## 2. Shared qualification and safety policy

These gates apply to every integration phase.

### Offline gate A — source/build

From a clean worktree at the exact candidate commit, with no installed candidate daemon being mistaken for the test build:

~~~sh
just test-local
just build
just qualify-preflight
~~~

Record test counts and failures, compiler warnings, signed binary digest, git SHA, architecture and macOS build. Source review and a compile do **not** replace live qualification.

### Read-only gate B — daemon identity and IPC

Run the explicitly signed test binary in the foreground with the inert qualification configuration, then use:

~~~sh
just qualify-running
just smoke
just smoke-sa  # only if a healthy, matching SA is already available and required
~~~

`just qualify-running` verifies the executable path and build stamp; `./bin/yabai -m` alone does not identify which daemon processed the command. Never automatically change SIP, install/reload the Dock scripting addition, restart Dock, replace the normal installed service, or execute personal startup hooks just to make a test pass. If SA is absent, mark SA-specific behavior blocked.

### Interactive gate C — isolated disposable desktop

Use explicitly identified disposable windows/Spaces, stable current Space IDs and fresh window IDs, and a foreground candidate. Qualify interactive mouse, keyboard, focus, native tabs, and layout operations on a controlled test Space before broader multi-display tests. Do not use session-wide restore or global `manage off/on` against everyday workspaces.

Take read-only before/after window, Space and layout snapshots. In human-driven stages, the operator performs pointer interactions; scripts should fail closed when guards are not met. Record functional symptoms, not WindowServer log count as a proxy.

### Regression policy

- Zero unexpected changes to real work Spaces.
- No unhandled AX/window lifecycle errors, infinite waits, stale mouse capture, crash, or corrupted BSP tree.
- No layout orphan, duplicate native-tab tile, lost focused keyboard window, or cross-display bounce caused by newly introduced code.
- Preserve native unmodified drag, click/double-click, window rules, hybrid manage, session serialization, Zen reconciliation, and existing tool/CLI behavior.
- Tests first for every bug already identified from fork changes. Stop on the first failing gate and fix or revert the narrow patch.
- No GitHub Actions. The fork's local signing, native tests and macOS qualifications remain authoritative.

## 3. Phase N — Native macOS tab grouping

**Goal:** integrate and accept a current, opt-in `window_native_tabs` implementation before changing the layout model or interactive architecture.

### N0. Source comparison and test fixtures

- [ ] Pin current Christian-SC26 source SHA; enumerate native-tab implementation and all subsequent dependent fixes through the pinned revision.
- [ ] Diff our `application.c`/`application.h`, `window.c`/`window.h`, `window_manager.c`/`window_manager.h`, `event_loop.c`/`event_loop.h`, `mission_control.c`, `view.c`/`view.h`, `message.c`, initialization, documentation and tests.
- [ ] Identify interaction with our Zen-specific reconciliation and with PR #7 layout persistence and hybrid manage. Do not overwrite our source-specific fixes.
- [ ] Establish synthetic test windows and native-tab fixtures covering selected/hidden tab membership and membership changes.
- [ ] Add/port the recent 32-window stack-capacity safety fix and regression tests; verify no out-of-bounds writes or dangling nodes before exercising tab and stack composition.

### N1. Port and isolate behavior

- [ ] Add opt-in `config window_native_tabs on|off`; default **off**. Preserve existing behavior when disabled and release hidden-tab state cleanly when disabled at runtime.
- [ ] Track selected and hidden native macOS windows through the relevant WindowServer notifications (815, 1325, 1326, etc.) and AX tab-change events, with defensive handling for apps that do not expose tab information.
- [ ] Map a native tab group to one managed layout slot; swap selected concrete window IDs within that slot without rebalancing the BSP tree.
- [ ] Handle opening, closing, hiding, showing, reordering, merging, and detaching tabs without duplicate layout entries or transient incorrect tiling.
- [ ] Preserve group-level float, zoom, insertion-point, stack, Space placement and focus semantics.
- [ ] Port v7.3.1 opacity lifecycle fixes: newly selected tab receives active opacity, ordered-out tabs do not flash at inactive opacity, and switching opacity off restores all members on later selection.
- [ ] Port v7.3.2 hidden-tab application-follow correction: never treat a hidden tab's stale Space as evidence of a visible app window.
- [ ] Ensure disabled behavior adds no unnecessary per-event tab scanning or long AX delays. Bound/restrict probes of unresponsive apps.
- [ ] Review `SLSRequestNotificationsForWindows` capacity (>1023) before extending subscriptions. Our existing growable buffer only warns at large counts; never silently replace it with a truncation that loses events.

### N2. Acceptance matrix

- [ ] **Unit tests:** one group/one slot, selected tab replace, independent non-tab windows, tab detach creates new slot, merge removes old slot, idempotent repeated/out-of-order events, no double free.
- [ ] **Capacity:** stacks at 31/32/33 entries and tab grouping across BSP and stack layouts; no corruption or duplicate membership.
- [ ] **Apps:** Terminal, Finder, Ghostty where available; also one ordinary non-native-tabbed app and Zen as negative controls. Do not equate browser tabs with native macOS tab groups.
- [ ] **Actions:** new tab, rapid consecutive new tabs, switch via keyboard/click, close active/inactive tab, drag tab out, drag into another group, Merge All Windows, close final tab, re-enable/disable config.
- [ ] **Placement:** `--swap`, `--warp`, `--stack`, resize, zoom-fullscreen, native/windowed fullscreen, move between Space/display, float group, restore from minimize/hidden state.
- [ ] **Focus:** app switch via Dock/keyboard while a hidden tab reports an obsolete Space; correct active app/menu-bar/keyboard focus; FFM/MFF with and without selected tab changes.
- [ ] **Persistence:** dumps and matching behavior with tabbed windows; reject ambiguous restore safely, do not use unstable IDs as persistent identity.
- [ ] **Zen regression:** repeat the six-stage detach qualification and validate AX-window registration/membership unchanged.
- [ ] Gate A, B and scoped C pass with recorded exact candidate binary provenance.

**Exit N:** Native-tab grouping can be enabled and disabled without disrupting existing windows, and all above required behavior has source tests plus targeted macOS acceptance. Any macOS-version-specific unsupported behavior is explicitly documented and isolated, not silently ignored.

## 4. Phase R — Live BSP resizing and drag correctness

> **Implementation checkpoint (2026-11-06).** Following the explicit request to finish live BSP resizing first, the source port is staged on `feature/live-bsp-resize-hardening-20261106`, based on `fix/zen-detach-reconciliation-20261104`. The port adds live/classic mode configuration, original-gesture-position tracking, wrong-axis fallback suppression, direct no-proxy BSP frame updates, 33 ms live cadence, consecutive-motion coalescing with mouse-button barriers, and guards for both source/destination cross-display retiling before mouse-up. Native gesture and coalescing tests have been committed. **This is source integration only**: a macOS compile, `just test-local`, `just build`, verified foreground daemon, multi-display interactive acceptance and final merge remain outstanding. See [the live-BSP qualification record](docs/live-bsp-resize-qualification.md). Phase N native tabs has not started; complete both N and R independently before Phase I. This phase order is intentionally different from the originally planned N-before-R order.

**Goal:** retain the live BSP functionality already present in draft PR #7, then incorporate later correctness and responsiveness improvements without simultaneously redesigning modifier-drag.

### R0. Audit the existing port

- [ ] Pin Christian-SC26 reference commits for v7.1.33, v7.1.34 and v7.2.1; diff current PR #5/combined implementation and map each behavior to a specific test.
- [ ] Baseline current mouse-tap lifecycle and GUI responsiveness with `just qualify-running`; record reproducible test cases including pointer release after stopping.
- [ ] Confirm the PR #2 dedicated runloop lifecycle remains correct while changing mouse handler state; retain the no-input-freeze and no-stuck-click requirements.

### R1. Port correctness and responsiveness fixes

- [ ] Add the original mouse-down location and explicit “resize was applied” tracking (Christian `6f68eebec`); do not mistake a previously moved/resized gesture for a click because the pointer stopped before release or returned to the origin.
- [ ] Always reconcile the BSP split on mouse release when required, including classic/off mode and edges with no matching BSP split.
- [ ] After changing a real BSP split, prevent a simultaneous fallback resize on an axis without a corresponding split from leaving a tile oversized.
- [ ] Expose documented `mouse_bsp_live_resize on|off` or equivalent (subject to collision with existing config); keep backward-compatible defaults until separately approved. “Off” means classic snap-on-release, not preview-only interaction.
- [ ] Use direct/no-animation frame flush for live interactive resize rather than redundant proxy captures/animation threads when the operation has `animate=false`.
- [ ] Evaluate bounded event coalescing and appropriate update cadence (Christian v7.1.33). Maintain correct release/end events, avoid stale/dropped gesture termination, and compare CPU and latency to the original.
- [ ] Integrate related cross-display dragged-window correctness (Christian v7.1.34): avoid premature retiling before actual mouse release, flush the vacated source layout after a verified drop, and prevent leftover gaps.
- [ ] Keep this as a minimal patch to the current live path; the later overlay-only interaction will replace the **organization gesture** semantics separately.

### R2. Acceptance matrix

- [ ] BSP split adjacent to left/right/top/bottom edges, corners, nested children, root nodes, min dimensions, non-resizable client windows and no valid split axis.
- [ ] Live on/off: hold still then release; drag and pause before release; resize then return near start; drag in mixed axes; release without drag; rapid repeated drags; no double adjustment.
- [ ] Neighbor windows update correctly during live mode, never remain outside assigned final tiles, and remain stationary in classic mode until mouse-up except the directly resized window where applicable.
- [ ] Correct operation with native macOS tab groups, floating windows, stack layout, zoom/insert semantics and screen padding.
- [ ] Cross-display ordinary/native drag and modifier drag with two or three monitors; no layout ping-pong, premature retiling or empty-gap residue. The new preview-only mechanism is not implemented in this phase, so any unresolved original behavior should be documented rather than declared fixed.
- [ ] Repeated FFM toggle, daemon stop/restart, sleep/wake, rapid input; event tap not wedged and normal clicks/keyboard unaffected.
- [ ] Compare representative drag latency (p50/p95), daemon CPU, event queue depth, dropped updates and GUI responsiveness against pre-port baseline. Performance decisions require measurements, not just source claims.
- [ ] Gate A, B and scoped C pass on a clean signed binary.

**Exit R:** Both live and classic BSP resize are deterministic and recover on release, including the edge-case fixes; native-tab behavior remains correct. The underlying interactive drag architectural deficiency may remain, but its baseline is measured and documented.

## 5. Phase I — Combined integration acceptance and stable baseline

Do **not** begin the new architecture before this phase is complete.

- [ ] Rebase or merge N and R only after their individual gates pass; resolve shared `mouse_handler`/`event_loop`/`view` conflicts semantically, not by accepting arbitrary hunks.
- [ ] Repeat unit, signed build, startup, and manual acceptance on **the final combined commit**; earlier tests on each isolated branch are not enough.
- [ ] Complete relevant outstanding draft PR #7 acceptance: global `manage` toggles in an isolated login/desktop, automatic sublayers, mouse-tap lifecycle, layout identity-matched restore on a disposable BSP, and explicit handling of nontransactional `--load-session` failures in a fully isolated environment.
- [ ] Re-run PR #6 BSP -> float -> BSP sublayer transition test and Zen's six-stage reconciliation test; include rapid native tabs and BSP resizing in the same run where safe.
- [ ] Verify topology and cross-display movement after Space/display reconfiguration, ordinary native drag, window rules and popup/PiP focus behavior, where applicable.
- [ ] Confirm no silent subscription loss at high tracked-window count; if the 1023-entry SPI limit is reproducible, devise and test a safe policy rather than leaving only the warning.
- [ ] Preserve a named known-good baseline commit, exact signed binary digest, fixture corpus and reproducible test transcript.
- [ ] Update [docs/fork-inventory-2026-11-04.md](docs/fork-inventory-2026-11-04.md) with realized inclusion/QA status, replacing stale “missing” entries. Keep detailed test provenance in specific qualification docs.
- [ ] Decide promotion target explicitly; do not equate feature-branch source presence with promotion to `master`.

**Exit I:** A reproducible, qualified baseline that includes native tab grouping, live/classic BSP resizing fixes, Zen reconciliation and previous correctness changes, with explicitly documented residual OS-dependent limits. **Only now start DESIGN.md implementation.**

## 6. Phase D — Build the new intent-driven interaction system

The stages below describe a planned migration; they should not be implemented during N/R/I.

### D0. Capture the failure and freeze expected semantics

- [ ] Build a bounded, repeatable two-/three-display reproduction of the reported modifier-drag bounce-back; record actual window frame, observed Space membership, pointer position, event-tap dispatch and layout modifications with timestamps and IDs.
- [ ] Determine what macOS receives during a currently claimed drag: the existing tap consumes modifier down/up but forwards drag samples, while yabai applies live window moves. Do not claim one specific causal loop before instrumenting it.
- [ ] Snapshot the current behavior of swap, warp, stack, no-target drops, resize and native drag, with tests independent of implementation internals.
- [ ] Define a minimal acceptance baseline: **no application window changes while previewing, cancel does nothing, accept yields exactly one intended logical operation**.

### D1. Pure layout planner and data contracts

- [ ] Define session-stable `LayoutItemId` and native `TabGroupId`, immutable `LayoutSnapshot`, `LayoutIntent`, `LayoutProposal` and typed error/result values.
- [ ] Separate logical group IDs from transient WindowServer IDs; include display/Space topology epochs, layout revisions and capability constraints.
- [ ] Extract pure BSP/stack operations into a planner without Cocoa, AX or WindowServer calls, while retaining tested layout semantics.
- [ ] Implement insert/swap/stack/move-to-Space/float proposals and exact affected-region previews. Use the existing tree until reference-equivalent tests support migration.
- [ ] Add property/fixture tests for unique membership, valid ratios/frames, capacity, purity, deterministic output, tab-group identity and stale snapshot rejection.

**Exit D1:** An intent produces a predictable proposal without changing any live window. Existing CLI behavior is not affected.

### D2. Modifier-drag capture and functional overlay prototype

- [ ] Add a feature flag (name to be finalized; default **off**) for preview-only modifier-drag organization.
- [ ] Establish gesture ownership: preserve click vs drag semantics, consume the complete claimed input stream, support escape/cancel and recover from event-tap failure. Do not steal unmodified native drags.
- [ ] Resolve cursor/display/target independently of source window geometry; support overlay-only drop zones and a prospective arrangement for affected windows.
- [ ] Implement a nonactivating, non-hit-testable functional overlay, excluded from yabai tiling/reconciliation; source app windows do not move on drag updates.
- [ ] Begin in-process behind an interface if necessary; retain the option to extract a user-owned overlay/input process without moving authoritative state out of yabai.
- [ ] Test display scale/topology, focus, accessibility, preview invalidation, rapid motion, cancellation and daemon exit. Measure preview responsiveness and CPU.

**Exit D2:** Real window frame/Space/layout membership remains unchanged throughout a drag and after cancel. No actual commit is permitted until D3 is accepted.

### D3. Transaction coordinator and same-Space commit

- [ ] Introduce prepare/apply/settle boundaries, revision/topology validation, conflict serialization, operation journal and typed outcomes (`committed`, `rejected`, `failed`, `partial`).
- [ ] Adapt current yabai executor primitives; do not directly mutate `struct view` from input handlers.
- [ ] First support same-Space insert/swap/stack, ensuring affected neighbors are placed deterministically and expected AX events cannot re-enter as new gestures.
- [ ] Use a fake OS executor to test asynchronous event delays, out-of-order notifications, duplicated notifications, missing windows, stale intent, timeout, crash and compensation.
- [ ] On the real desktop, verify committed geometry, focus, stacking and uniqueness, and ensure failure does not claim success.

**Exit D3:** Preview-only interaction commits same-Space rearrangements predictably; disabled mode retains the qualified old behavior.

### D4. Cross-display/Space commit

- [ ] Support distinct display and Space identities, including destinations currently visible on another display and guarded unsupported cases.
- [ ] Validate current source/destination membership before mutating, await asynchronous move confirmation when required, then reconcile membership and layout with bounded retries/deadlines.
- [ ] Test the original bounce-back reproduction: repeatedly cross display boundaries while holding the gesture, cancel once, then release on a selected destination. Exactly one logical move on accept; no real source-window motion before release.
- [ ] Test topology change mid-drag (cancel), target disappearing, SA absent, partial transfer, focus changes, slow AX observers, and source-window closure.
- [ ] Require a final observed-state report; avoid claims of atomic rollback for macOS multi-Space moves.

**Exit D4:** Multi-display organization satisfies the core scenario with no native transfer conflict or loop, or a narrowly identified unsupported platform state with explicit error.

### D5. Preview-only resizing and optional frontend separation

- [ ] Route provisional split resizing through the same planner; final ratios match those obtained by the qualified live/classic engine.
- [ ] Offer explicit interaction-mode choice without silently changing the meaning of established `mouse_bsp_live_resize` settings.
- [ ] Evaluate extraction of the overlay/input frontend into a separate process **only if** the interface and teardown tests justify the added IPC/lifecycle cost.
- [ ] Keep transaction execution solely in the daemon; external frontend failures cannot leave the manager stuck.
- [ ] Remove or retire the legacy direct organization-drag path only after parity, migration testing and a deliberate decision.

**Exit D5:** Both movement and resizing can be expressed as intents, previewed safely, committed correctly and canceled without changing real windows.

## 7. Tracking, evidence and stopping criteria

Every subphase should record:

| Field | Required evidence |
| --- | --- |
| Source | Upstream repository/ref/commit and relevant source diff |
| Target | Fork branch, exact commit, merge base and known-good ancestor |
| Change | Feature behavior and conflicts resolved |
| Tests | Exact test command, pass/fail counts, macOS target build and signing stamp |
| Runtime | Verified daemon executable/PID, SA capability if relevant, disposable Space/window identities, observed before/after state |
| Regression | Focus, Zen reconciliation, tabs, BSP/stack, cross-display and click/drag results |
| Limitations | Explicit untested/unsupported cases and user-impacting risk |
| Decision | Accepted, blocked, reverted or deferred; owner and next required acceptance evidence |

**Stop and repair or revert** if any stage causes unreliable input, an unbounded event-loop wait, unexpected real-workspace mutation, duplicated managed items, unsafe stack overflow, silent tab loss, stale Space focus, incorrect final geometry or a partial failure reported as success. Avoid broad WindowServer log hunts without a reproducible behavioral symptom.

For runtime qualification, use the existing read-only preflights and a deliberately started test daemon. Never perform potentially destructive global layout or session operations on the ordinary desktop.

No GitHub Actions, no automatic changes to SIP/Dock/installed services, and no unrequested cosmetic/animation integration.

## 8. Progress ledger

| Milestone | Current status as of 2026-11-06 |
| --- | --- |
| Document future interaction architecture | **Documented** in [DESIGN.md](DESIGN.md); implementation not started |
| Document ordered integration plan | **Documented** here |
| Native macOS tabs and follow-up fixes | **Not yet integrated** into active candidate |
| Stack overflow protection in latest Christian source | **Not yet integrated**; required safety dependency |
| Initial live BSP resize (PR #5) | **Source present / basic live test passed**; outstanding acceptance |
| Later BSP mouse-up, event coalescing and cross-display fixes | **Source port staged on `feature/live-bsp-resize-hardening-20261106`**; macOS tests/merge pending |
| Combined PR #7 and Zen baseline | **Source staged**, partial qualification; draft PR #7 remains open |
| Pure planner / preview-only modifier-drag | **Not started** |
| Bounded commit coordinator and cross-Space preview flow | **Not started** |
| Separate overlay process / preview-only resizing | **Deferred until earlier gates pass** |

Do not mark a milestone complete merely because a source patch applies or a branch builds. Record the specific functional acceptance required above.


### Experimental R-preview qualification (2026-11-06)

A separate opt-in visual preview implementation is staged in PR #8. It is a
narrow trial of D5-style presentation during BSP resize, not completion of the
D-phase interaction redesign. It clones the BSP and draws proposed tile bounds
without AX mutations until mouse-up. The mouse_bsp_live_resize option remains.
Qualification must establish overlay lifecycle, stationary outer-edge geometry,
minimum-size commit behavior, topology invalidation and live/classic regressions
before promotion. No runtime acceptance has been recorded.
