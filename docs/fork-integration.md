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

## Required manual tests on macOS

- Run `just build` and `just smoke` with a signed binary and the daemon running.
- Verify `yabai -m rule --add app="^Finder$" ffm=off`, `yabai -m rule --list`, and mouse entry/exit behavior with global `focus_follows_mouse autofocus`. Reapply rules to existing windows with `yabai -m rule --apply`.
- Verify `mouse_follows_focus` remains normal for eligible windows and does **not** warp the pointer to/from PiP and transient dialogs.
- Close first, middle, and last tiled windows. Confirm the remaining BSP windows fill the freed region.
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
