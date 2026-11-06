# Combined PR #2–#5 qualification (macOS 27)

**Status (2026-11-02): staged for testing, NOT compiled or runtime-qualified.**
Source patches from [PR #2](https://github.com/josephcourtney/yabai/pull/2),
[PR #3](https://github.com/josephcourtney/yabai/pull/3),
[PR #4](https://github.com/josephcourtney/yabai/pull/4), and
[PR #5](https://github.com/josephcourtney/yabai/pull/5) were applied
with verified patch contexts in that order to the merged compatibility base
`0206cbb`. Each feature has a separate staging commit. The four source PRs
remain open and unchanged. No GitHub Actions.

## Why this branch is not yet a merge candidate

- **PR #2** changes global mouse-event tap threading and teardown. Bad
  lifecycle behavior can make ordinary clicks or drags unreliable.
- **PR #3** changes **global** window management and automatic sublayer
  control. `manage off/on` can float/retile eligible windows on *every*
  Space, including real workspaces.
- **PR #4** introduces nontransactional session restoration.
  `--load-session` can move windows across multiple Spaces, and cannot
  guarantee rollback after an interruption.
- **PR #5** changes the behavior of interactive BSP border drags and
  shares the mouse-state struct changed by PR #2.

The patch contexts apply without conflict, but **this is not a C compile,
link, unit-test, or runtime result**.

## Gate A: local build and source tests (no desktop mutation)

Use an ordinary checkout of `integration/pr2-5-qualification-20261102`.
First ensure `git status --short` is empty, and the existing *foreground*
qualification daemon has been stopped with Ctrl+C. Do not stop or replace
installed services implicitly.

```sh
git fetch origin
git switch -c review/pr2-5 origin/integration/pr2-5-qualification-20261102
just test-local
just build
just qualify-preflight
git status --short
```

Build diagnostics must be inspected; a success with nonfatal SDK deprecation
warnings is distinct from a compile or link error. The strict build stamp
records the signed binary and exact Git revision and rejects tracked edits.
Do not start the candidate daemon if these checks fail.

## Gate B: non-mutating runtime smoke

In a **separate Mac login session** or similarly disposable desktop environment,
ensure Scroot is closed and no other yabai daemon is running. With a clean
signed candidate, start `just run` in one terminal. Its default
`scripts/qualification.yabairc` is deliberately inert and does not run
your normal startup hooks. In a second terminal:

```sh
just smoke-sa
./bin/yabai -m config manage
./bin/yabai -m config window_sublayer_auto
./bin/yabai -m query --spaces
./bin/yabai -m query --windows
```

Expected default config values: `manage=on` and
`window_sublayer_auto=on`. Do not load/reload a Dock scripting addition,
restart Dock, or alter SIP automatically. If the SA handshake is unavailable,
mark SA-dependent tests blocked rather than changing SIP.

## Gate C: interactive runtime acceptance (isolated test session only)

1. **Mouse event tap (#2).** Verify ordinary click, click/drag, application
   activation and key input with FFM on/off, rapid repeated mode changes,
   daemon restart, and sleep/wake. If clicks fail, stop the foreground
   daemon using the still-functional terminal. Inspect tap thread shutdown
   behavior; do not infer success from `just smoke` alone.
2. **Hybrid manage (#3).** On only disposable windows, set
   `./bin/yabai -m config manage off` then `on`, checking window
   assignment, floating and tiling state before and after. Repeat after
   startup with a literal `manage off` in an *isolated* temporary startup
   config; test explicit/rule-managed window preservation. Toggle
   `window_sublayer_auto off` then `on` with a healthy SA; confirm
   automatic BELOW and NORMAL transitions and preservation of explicit
   layers. **Do not run global manage toggles in your regular work session.**
3. **Layout persistence (#4).** First check `space --dump-layout`,
   `--dump-session`, and `--save-layout <absolute-temp-path>` with
   disposable Kitty windows and inspect the JSON on disk. Then in an
   **isolated user session**, verify `--load-layout`, `--save-session`,
   and `--load-session`: missing windows, display reordering, changed Space
   identities and interrupted restores. Snapshot windows and Spaces before
   and after, and explicitly record partial restores. **Never use
   `--load-session` against actual workspaces.**
4. **Live BSP border resize (#5).** In a disposable BSP Space with at least
   three Kitty tiles, drag each relevant border and corner along both axes.
   Check frame continuity during the drag and immediately on release,
   minimum sizes, nested split trees, mouse-up without double-adjustment,
   floating fallback and behavior across displays. Record any stuck drag
   state or visual jumps.
5. **Regression gates (#1/#6).** Reconfirm ordinary eligible focus, BSP
   reflow, fullscreen entry/exit and, in a separately labeled disposable
   Space with exactly two Kitty windows, the guarded
   `just qualify-layer-cycle <index> <stable-space-id> <window1> <window2>`
   after validating all preconditions. Never reuse historical IDs.

Capture exact commit, build stamp, command outputs, window IDs, Space IDs,
and before/after JSON queries. `docs/space-layer-qualification.md` explains
the isolated layered-window test.

## PR #4 strict window identity (unqualified source fix)

After the initial 2026-11-02 macOS runtime smoke tests (recorded as PR
comments), subsequent source review revealed that app-only restoration could
arbitrarily exchange multiple Kitty/Zen windows. The staged corrective
implementation now matches the **exact application + saved title** of every
restored window, refuses missing or duplicate matches, and rejects stacked
leaf layouts that cannot be reconstructed unambiguously. It does not silently
substitute an arbitrary window. Session restore builds and validates the
full assignment plan before moving windows and refuses partial/incomplete
window sets. These source changes invalidate the earlier signed build
provenance; none of the earlier runtime results qualify the new revision.

**Limitations:** Titles can change; two windows with the same app and title
cannot be distinguished reliably and will be rejected. A missing or ambiguous
window is an error, not a best-effort restore. Multi-Space restoration remains
**nontransactional** after validation if the operating system rejects a move;
layout/layer-transition behavior still requires isolated runtime checks.
Do not use `--load-session` on ordinary work Spaces.

Rebuild and run the native matcher regression via `just test-local`, then
`just build` and `just qualify-preflight` with the old foreground daemon
stopped. Confirm signed provenance and test `--load-layout` **only** on a
verified disposable BSP Space with unique Kitty titles. Snapshot before/after
window IDs, positions, tree, and layers. Verify a deliberate duplicate-title
case is rejected without mutating frames. Test `--load-session` only in an
isolated macOS login session or VM after additional review.

## Acceptance policy

Only after Gate A succeeds can Gate B begin. The mutating Gate C requires a
disposable session and explicit human control of the mouse. Keep PR #2–#5
as drafts until the relevant feature gates pass; record failures with precise
reproduction steps and fix them on the staged branch before integrating into
`compat/macos-27.0.1`. No GitHub Actions, and no automatic SIP, Dock, or
system-wide service changes.
