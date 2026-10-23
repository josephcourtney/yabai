# Fork integration: correctness batch

This branch is based on `compat/macos-27.0.1`; keep `master` untouched until runtime validation.

## Incorporated sources

- Performave `b7e768216b`: inhibit cursor warps through ineligible focus.
- Performave `732373ee5d`: protect in-flight cross-space window moves and refresh a moved space's display geometry.
- jpatrolla `0dca1b28`: avoid `CFRelease(NULL)` during window destruction.
- jpatrolla `1640f6f5`: get the process serial number from the actual window owner's SLS connection.
- Christian-SC26 `5463194969`: flush BSP root after un-tiling; support per-rule focus-follows-mouse exclusions; preserve external-bar padding.
- Christian-SC26 `a9caf3d78c`: native fullscreen recovery, state transitions, and zoom-layout recovery.

The branch retains the existing `--check-sa`, manual/automatic window reconciliation, macOS 27 compatibility, safe notification-buffer growth, and explicit tiling patches.

## Runtime qualification: safety and branch identity

During the 2026-10-22 desktop qualification session, mouse clicks and application activation became unreliable. The accompanying WindowServer `_CGXPackagesSetWindowConstraints: Invalid window` error rate dropped from 91 to 6 occurrences per 30 seconds after **Scroot** exited (with the yabai daemon already stopped). This implicates Scroot in the error flood, but does **not** establish that Scroot caused the input failure. Keep Scroot closed during further yabai testing. No yabai PR is runtime-qualified on the strength of this observation.

There is a second independent pitfall: invoking `./bin/yabai -m` sends commands to the *running daemon*, which may still be the installed `/opt/homebrew/bin/yabai` or a different branch's process. A successful client response does not prove which implementation was exercised.

From the qualification worktree on macOS:

```bash
cd ~/code/yabai-qualification
git fetch origin
git switch --detach origin/integration/fork-correctness-20261022
make clean
just build
just stop-installed
just qualify-preflight
```

`just qualify-preflight` is read-only. It fails if a Scroot process is detected or any yabai daemon is still running. It also prints HEAD and the baseline 30-second WindowServer error count; it does not change Dock, SIP, launchd, or configuration.

Run `just run` in Terminal 1; leave it in the foreground. In Terminal 2:

```bash
cd ~/code/yabai-qualification
just smoke
```

`just smoke` now runs `just qualify-running` first. That check uses macOS `proc_pidpath` to verify there is exactly one yabai daemon and that its *actual executable path* matches the test worktree binary. It also rejects a daemon that predates a rebuild. Optional `--ref origin/integration/fork-correctness-20261022` checks the checked-out Git revision. If a check fails, stop the daemon and resolve the mismatch rather than continuing with feature tests.

`just smoke` exercises daemon IPC and display/space/window queries **without requiring** the Dock scripting addition. Run `just smoke-sa` only after intentionally loading a compatible Dock payload; it requires a live, version-matching SA handshake. The 2026-10-23 isolated run at commit `55fd60b1` confirmed candidate PID 74354, Scroot absent, and working ordinary clicks, but the original `smoke` halted at `--check-sa` because the SA socket did not respond. That is not evidence of an input regression. Keep the no-SA test and any later injection test separate; `sudo ./bin/yabai --load-sa` can install/replace `/Library/ScriptingAdditions/yabai.osax` and restart Dock, and requires appropriate SIP configuration. Do not change SIP settings merely to make a smoke test pass.

**Important:** Each of PRs #2–#5 is a separate branch, not a combined build. Stop the foreground daemon before switching branches, clean/rebuild, and restart. Until the qualification helper is included in those branches, keep a copy of it outside the worktree and invoke it directly when testing them. Do not merge before all relevant macOS 27 runtime tests pass.

If input becomes unreliable again, use the still-functioning terminal to record process paths and stop the test daemon. Do not assume killing yabai unloads its Dock scripting addition. A Dock restart is a distinct diagnostic intervention.

## Observed macOS 27 runtime qualification (2026-10-23; Scroot closed, no Dock SA)

- Test daemon executable positively identified as `~/code/yabai-qualification/bin/yabai`; no Scroot process. Read-only query smoke passed. Ordinary mouse clicking remained functional.
- `focus_follows_mouse autofocus` worked; `ffm=off` Finder exclusion blocked hover-based focus transfer while ordinary clicking and eligible-window autofocus remained functional.
- Four Kitty BSP tiles filled the available layout; deleting three successive windows correctly reflowed surviving tiles to the remaining area. One immediate query briefly reported a closed window during reflow; it disappeared without manual refresh after a short delay.
- Three-window `zoom-parent` and `zoom-fullscreen` geometry and distinct flags matched expectations; unzoom restored nonoverlapping tiles. `windowed-fullscreen` toggled and restored the starting tiled geometry.
- Native fullscreen moved Kitty window 1213 into a new fullscreen Space (space ID 57, index 5), then returned it to a regular tiled Space. All three windows were present and nonoverlapping afterward. **The returning window 1213 and another Kitty window 1564 exchanged their upper/lower positions.** Thus native fullscreen entry/exit and valid retile passed, but *exact tile-order preservation was not observed*. The underlying fullscreen path removes the window from the BSP tree and reinserts it on return; this behavior is also present on `compat/macos-27.0.1`. Do not claim this as a demonstrated new regression without comparative testing.
- These are user-reported local qualification results, not GitHub Actions assertions. No Dock scripting-addition or second-display behaviors have been qualified in this session.

## Required manual tests on macOS

- Run `just build` and `just smoke` with a signed binary and confirmed test daemon.
- Verify `yabai -m rule --add app="^Finder$" ffm=off`, `yabai -m rule --list`, and actual mouse entry/exit behavior with global `focus_follows_mouse autofocus`. Reapply rules with `yabai -m rule --apply`.
- Verify `mouse_follows_focus` remains normal for eligible windows and does **not** warp the pointer to/from PiP and transient dialogs.
- Close first, middle, and last tiled windows. Confirm remaining BSP windows fill the freed region.
- Toggle window zoom-parent, zoom-fullscreen, and native fullscreen; check every surviving window retile and no event-loop hang.
- With two displays, drag a space between displays in Mission Control; verify windows remain on that space with updated frames.
- Check windowed fullscreen against nonzero `external_bar` padding.
- Use `window --refresh` and automatic reconciliation with a browser that sometimes misses AX creation events.
- Test `--check-sa` before and after Dock restart; confirm only the appropriate SA is injected.
- Check a high window count (>1023) carefully in a disposable session.

## Important open concern

jpatrolla reports an undocumented limit of 1023 entries for
`SLSRequestNotificationsForWindows`. The existing growable buffer only
prevents memory corruption; it does not address a possible WindowServer
subscription-count limit. The precise limit and failure mode have **not**
been verified; avoid claiming this case is fixed. The API must be
measured before changing overflow behavior.

## Remaining independent integration phases

1. Isolated mouse event-tap threading and verified run-loop shutdown from Christian-SC26.
2. Hybrid `manage=off` and `window_sublayer_auto` from performave, protecting manually tiled windows.
3. Workspace save/restore and live BSP border-resize from Christian-SC26.
4. Comparative macOS 27 SIP-on space switching from Christian-SC26 and wxwern with Dock-pattern validation.
5. jpatrolla animation/focus-ring architecture, kept behind opt-in flags.

**Validation status:** GitHub Actions builds the Apple Silicon executable and runs
existing tests using a macOS 15 runner. These tests cannot validate the real
WindowServer, Dock injection, or macOS 27 behavior. Local macOS runtime testing
is mandatory before merging.
