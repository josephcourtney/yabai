# External yabai fork integration inventory — 2026-11-04

## Comparison scope and interpretation

This is a **source audit**, not proof of runtime behavior on every macOS build.

- Local fork: [josephcourtney/yabai](https://github.com/josephcourtney/yabai).
- Default `master`: upstream-equivalent at `dd845723416f`. **Neither the integration features nor the Zen qualification branch are on master.**
- Active integration/qualification candidate: `fix/zen-detach-reconciliation-20261104`, 115 commits ahead of `master` as checked on 2026-11-04. This branch contains `integration/pr2-5-qualification-20261102` (37 commits behind the candidate, no intervening divergence).
- Compatibility base: `compat/macos-27.0.1`; PR #1 and PR #6 are merged there. PRs #2–#5 and combined draft PR #7 remain open. Features present in the candidate are not necessarily merged or fully accepted.
- Sources inspected: [performave/yabai-plus](https://github.com/performave/yabai-plus) `master` at `d10e2bf5bd5f`; [Christian-SC26/yabai](https://github.com/Christian-SC26/yabai) `master` at `9aadc2b7073d` (v7.3.1, Nov 4); [wxwern/yabai](https://github.com/wxwern/yabai) `master` at `c72829ca67ee`; [jpatrolla/yabai](https://github.com/jpatrolla/yabai) default `showcase/animations-core` at `aacac231b`. Upstream [asmvik/yabai](https://github.com/asmvik/yabai) is the common baseline.
- GitHub cross-fork comparisons identify **36**, **115**, **5**, and **54** respective source-fork-only commits after the common upstream base. These counts do **not** equal the number of missing features: our fork ports some changes independently with different commit IDs.

**Labels:** IN = implemented on the active integration candidate; QA = tested in a specified live scenario; STAGED = implemented but incomplete runtime acceptance; PARTIAL = only part of current source functionality or an analogous implementation; MISSING = no corresponding implementation found in the inspected source; CHECK = implementation or behavioral equivalence not established. No label implies released to `master`.

## Incorporated or independently implemented

| Source/change | Status in active candidate | Evidence and caveat |
| --- | --- | --- |
| performave `b7e7682`: suppress mouse warp to/from ineligible windows | IN / partial QA | Integrated via PR #1; tested ordinary eligible focus, not all PiP/transient/wake cases |
| performave `732373e`: Mission Control cross-display Space move geometry refresh | IN / not cross-display QA | Integrated via PR #1; no complete multi-display manual qualification |
| performave `a6fa2bf`: `--check-sa` live version/capability handshake | IN / QA | `src/yabai.c`, `src/sa.m`; healthy SA v2.1.31 observed in macOS 27 tests |
| performave `1cd51ce`: do not tile zero-area windows | IN / source verified | `src/window_manager.c::window_manager_should_manage_window` rejects width/height <= 0 |
| performave `54b2e66`, `31876a9`: `manage`, `window_sublayer_auto` config | STAGED | PR #3 composed into draft PR #7 and candidate; readback passed, global manage transitions need isolated QA |
| performave `fda359b` (plus.7): mark explicitly tiled windows `WINDOW_RULE_MANAGED` | IN / equivalence of intent | Candidate `src/message.c` sets/clears flag during `window --toggle float`; rebuild/cross-Space behavior still needs targeted regression |
| Christian `5463194`: BSP root flush, FFM rule exclusions, external-bar padding | IN / partial QA | Merged PR #1; ordinary BSP close and per-rule FFM checked |
| Christian `a9caf3d`: fullscreen/zoom recovery | IN / partial QA | Merged PR #1; native fullscreen returns to a usable tree but original leaf order is not guaranteed |
| Christian `#2`: dedicated mouse event-tap thread and retained runloop teardown | STAGED / basic QA | Candidate `src/mouse_handler.c` has retained CFRunLoop, stop, wake, join; clicks/drags and daemon restart passed; sleep/wake and rapid teardown require more tests |
| Christian `#5`: live BSP mouse resizing | STAGED / basic QA | Source present in candidate, neighboring tiles observed updating during drag; not newer v7.1.33/v7.2.1 fixes |
| Christian `#4`: BSP layouts and workspace session save/restore | STAGED / partial QA | Strict app+title fail-closed matching now implemented; dump/save tested, `--load-layout` not fully qualified, `--load-session` nontransactional and not safe in ordinary workspaces |
| jpatrolla `0dca1b2`: NULL AX ref window destroy guard | IN | Ported via PR #1 |
| jpatrolla `1640f6f`: use correct window owner's PSN for SLS focus opcode | IN | Ported via PR #1; later jpatrolla abandoned that focus method on Tahoe in favor of AX-only, so do not assume it is best for every OS |
| wxwern `113d92c` / related: Golden Gate OS and SA support | IN / macOS 27.0 QA | `workspace_is_macos_goldengate`, OSAX v2.1.31 and corresponding payload code present; no inference about macOS 27.2 |
| Local PR #6: BSP/stack -> float sublayer restoration | IN / QA | SA-qualified 2-Kitty below->normal->below test passed on macOS 27 |
| Local Zen AX reconciliation | IN / QA | `window_reconciliation` Zen-only default ON, 750 ms; six-stage manual Zen-detach qualification passed on signed `490c05342f36` |

**Branch/promotion caveat:** PR #7 is still a draft. Its strict session matcher was later revised and passed unit/build gates (37 pytest plus 2 native C tests on `3fb3bfeb`); earlier interactive sessions tested older builds. The Zen branch's later source contains the revision, but six-stage Zen acceptance does not automatically qualify all PR #2–#5 features. Existing PR #7 descriptions/comments and `docs/pr2-5-qualification.md` preserve the exact provenance.

## New or still-unincorporated candidates: correctness first

| Priority | Source/fix | Current status | Proposed acceptance / disposition |
| --- | --- | --- | --- |
| P0 | Christian v7.1.38: bound `native-fullscreen` wait when target Space never becomes active | MISSING / source-confirmed | Candidate `window_manager_toggle_window_native_fullscreen` still has `while (sid != space_manager_active_space()) { usleep(100000); }` without deadline. Port bounded wait and return an explicit failure. Test failure path without hanging event loop |
| P0 | Christian v7.1.34: menu-open accounting per application; reset after termination | MISSING / source-confirmed | Candidate lacks `open_menus`/reset implementation. Reproduce stuck FFM after menu-close notification loss; unit-test app lifetime and reset |
| P0 | Christian v7.1.34: cross-display window drag should not re-tile before mouse-up; flush source Space gap | CHECK | Candidate has mouse-state drag tracking but no equivalence test. Diff `MOUSE_DRAGGED`, window rehome and `window_node_flush` paths, test two-display drags |
| P0 | Christian v7.2.1: mouse-up BSP adjustment and suppression of wrong-axis resize after live split | MISSING / source-confirmed | Candidate lacks `origin_location`/`resize_applied` and `mouse_bsp_live_resize` config. Port after preserving staged PR #5; test stop-before-release, click-no-drag, both axes, nested layouts |
| P0 | jpatrolla `2d56a293`: limit `SLSRequestNotificationsForWindows` list to 1023 | PARTIAL / concern unverified locally | Candidate has a safe growable buffer and **warns**, but still invokes SPI with count >= 1024. jpatrolla reports total subscription failure at that threshold. Verify API behavior on disposable test with appropriate fixture before adopting cap; decide overflow policy, don't silently drop subscriptions |
| P1 | Christian v7.1.34: discover app windows restored on inactive Spaces at launch | PARTIAL | Candidate has `window_manager_add_existing_application_windows` in refresh/activation paths but not proven equivalent to new launch-time scan. Add an inactive-Space restore fixture and compare call sites |
| P1 | Christian v7.1.34: OSAX build prerequisites include payload/headers | MISSING / source-confirmed | Candidate `makefile` generates OSAX outputs from `loader.m`/`payload.m` without tracking `arm64_payload.m`/`common.h`. Fix dependency graph; reproduce stale-payload build |
| P1 | Christian v7.1.40/7.2.0/7.3.0: Mission Control cancel/gesture focus, menu-bar ownership, key-window restoration, swipe-to-select | PARTIAL | Candidate polls for MC exit, but newer behavioral fixes are not present as a unit. Port independently after reproducible macOS 27 acceptance, especially keyboard focus after gesture exit |
| P1 | Christian v7.1.33: coalesced mouse drag events, direct nonanimated BSP flush, configurable live/classic resize | PARTIAL | Basic earlier live resize staged; newer config and optimized direct path absent. Measure input latency/frames; avoid blindly altering global event queue |
| P1 conditional | wxwern `da81789` / Christian v7.1.28, 7.1.35–37: macOS 27 SIP-ON space gestures, non-natural scroll polarity, multistep jump reliability | MISSING / confirmed | Candidate lacks `event_serialize.c` and `space_gesture_natural_scrolling`. Test under **SIP enabled without SA** in isolated session. More urgent only if SIP-on mode is a supported objective |
| P1 conditional | Christian v7.1.38–39: macOS 27.2 reduced SA capabilities, version bump discipline, WindowServer-based Space moves | MISSING / not required for 27.0 | Qualify by exact macOS/Dock build before porting. Avoid pretending 27.0 patterns work on 27.2 |
| P1 conditional | performave `915b53d`/`fc8d9e7`: partial-write-safe PAC ABI SA patch, prefer SA for cross-display window moves | CHECK | Compare against existing SA loader/Space moves and test on affected macOS/architecture; no evidence from this audit of a present local failure |
| P2 | performave `817edb0`: avoid startup auto-tiling before asynchronous `manage off` config takes effect | PARTIAL | Candidate checks `wm->manage` when adding windows; startup sequencing may still temporarily tile before config. Isolate startup with `manage off` and assert no intermediate resize |
| P2 | performave `0910c60`: sticky/PiP Arc focus does not hijack cross-display/empty Space switches | CHECK | Eligibility warp rules integrated, but this is a distinct space-focus behavior. Test PiP across displays before porting |
| P2 | jpatrolla: WindowServer key-focus resolution, refocus after empty Space/app close, geometric focus in float/stack layouts, inter-display/wrapping focus | MISSING as integrated features | Valuable separately, but uses private SPIs and has only single-machine Tahoe 26.5 test evidence. Isolate and test each; do not merge refactored focus subsystem as one patch |
| P2 | jpatrolla: Finder desktop-role window re-tracking after topology changes, selected native tab AX adoption | MISSING | Candidate has neither new tab-set lifecycle nor related code. Relevant if adopting native tabs or handling display reconnect role windows |
| P2 | Christian v7.3.0/v7.3.1: `window_native_tabs` single-tile grouping and opacity fixes | MISSING | Opt-in native macOS tab grouping (Terminal/Finder/Ghostty), **different from Zen browser tabs**. Test open/close/switch/detach, floating groups, opacity; do not merge before baseline correctness gates |
| P3 | wxwern `2128e14`: feedback radius 17 for macOS 27 | MISSING | Candidate defines `INSERT_FEEDBACK_RADIUS 9`; cosmetic, low risk |
| P3 | performave: hash-pinned sudoers installer and development-binary path preservation | MISSING | Optional privileged/development workflow; do not change sudoers automatically |
| P3 | jpatrolla: focus-ring overlay, CoreAnimation frame/Space animations, wallpaper fades, mixed-refresh pacing, SA transform engine | MISSING / DEFER | Large invasive private-API subsystem. Source README identifies prototype tested on one Tahoe 26.5 M2 Pro system, not macOS 27; defer until fundamental features and safety gates are stable |

## Recommended next tranche and stopping rules

1. **Correctness-only branch first**: bounded native-fullscreen wait; application-scoped menu accounting; OSAX rebuild dependencies. Each has an explicit failure mode and narrow testable scope.
2. **Regression branch second**: cross-display drag/drop, Mission Control keyboard-focus recovery, and mouse-up resize fix, each verified on the target macOS and two-display topology. Use isolated test Spaces and signed build provenance.
3. **Investigate notification subscription limit** separately with non-destructive verification; current warning is not a fix.
4. **Finish accepting PR #7** with specific missing gates: hybrid/global manage startup transitions, session load-layout exact identity, isolated load-session failure handling, advanced mouse-tap lifecycle, all-edge BSP resize, and post-merge correctness regressions.
5. **Conditional 27.2/SIP-on work** only on an explicitly supported OS/SIP mode; do not transplant version-specific Dock offsets without runtime build checks.
6. **Defer appearance/animation/native tabs** pending user need and narrower prototypes. Native-tab grouping merits its own feature branch and must not regress Zen reconciliation.

**Promotion rule:** upstream-equivalent `master` is untouched; integration candidate is not production-promoted. Require a clean local test, signed binary provenance, targeted interactive acceptance, and preserved no-GitHub-Actions policy. Distinguish source presence from runtime proof.

## Sources and audit limitations

- [Current correctness integration document](fork-integration.md), [combined PR #2–#5 qualification](pr2-5-qualification.md), [Zen acceptance](zen-reconciliation.md), [PR #7](https://github.com/josephcourtney/yabai/pull/7).
- [performave changelog](https://github.com/performave/yabai-plus/blob/master/CHANGELOG.md), especially plus.5–plus.7; [explicit-manage fix](https://github.com/performave/yabai-plus/commit/fda359b3602cfa70b04dc064072ee21e067541b8).
- [Christian changelog](https://github.com/Christian-SC26/yabai/blob/master/CHANGELOG.md), versions 7.1.27–7.3.1; [resize mouse-up fix](https://github.com/Christian-SC26/yabai/commit/6f68eebec); [gesture multi-step fix](https://github.com/Christian-SC26/yabai/commit/17ad96abc).
- [wxwern SIP-on gesture serialization](https://github.com/wxwern/yabai/commit/da81789ce), [macOS 27 SA support](https://github.com/wxwern/yabai/commit/113d92c79), [rounded feedback](https://github.com/wxwern/yabai/commit/2128e1457).
- [jpatrolla default showcase README](https://github.com/jpatrolla/yabai/blob/showcase/animations-core/README.md), [tab adoption and subscription-cap claim](https://github.com/jpatrolla/yabai/commit/2d56a293f).

Only the named forks, their default branches, and the stated local integration branches were inspected. Source-code search and commit comparisons cannot prove behavioral equivalence. No tests were executed on the user's Mac as part of this refresh.
