# Qualification: managed Space layer restoration

## Scope

A BSP or stack Space can place managed windows in the BELOW sublayer
(`sub-level=-20`). When the Space is changed to `float`, yabai must restore
their automatically assigned sublayer to NORMAL (`sub-level=0`) **before**
clearing its managed tree. Changing the Space back to BSP/stack retile-lowers
those windows again.

The transition uses `space_manager_set_layout_for_space` for global and
per-Space layout configuration. `window_manager_adjust_layer` respects
explicit window/rule layer selections. This fix does **not** retrospectively
repair already stranded windows that were below before a floating Space was
loaded, nor does it guarantee visual frontmost order in every application.

## Verified local result (macOS 27, November 2, 2026)

- Verified code revision: `82ce60b2cf8ec26495777b732eea42d21b49f09c`.
- Signed Apple Silicon executable: `yabai-v7.1.25`, SHA-256 prefix
  `4ac2f908e64fa9f2`, post-sign build provenance and preflight passed.
- Scroot absent; only the qualified foreground yabai daemon was running.
- Dock scripting addition already loaded and healthy: payload v2.1.31.
  No installation, Dock restart, or SIP modifications were needed.
- Local `just test-python`: **36 pytest cases passed**. A completed native
  `make -C tests` run for the final combined integration revision has not
  been supplied; do not claim that it passed.
- A dedicated, active macOS Space (index 5, stable ID 366) was labeled
  `yabai-layer-qualification` and contained exactly two managed Kitty
  windows, IDs 8273 and 8299.
- Guarded `just qualify-layer-cycle 5 366 8273 8299` verified both windows
  were present at BELOW/-20 before the transition, NORMAL/0 while floating,
  and BELOW/-20 when restored to BSP. It reported
  `float_layer_pass=true`, `retile_pass=true`.

These IDs identify **historical evidence only**. Do not reuse them blindly
in another run. The test was performed on the qualifying source revision;
later workflow removal and local-test recipe changes did not modify
production C logic.

## Safe local qualification

1. Stop any installed or other yabai daemon. Do **not** automatically inject
   a scripting addition or alter SIP.
2. From a clean macOS worktree, run `just test-local`, `just build`, and
   `just qualify-preflight` (in that order). Build stamping requires a
   matching checked-out commit and no modified tracked files.
3. Start `just run` in the foreground. By default it loads the **inert**
   `scripts/qualification.yabairc`; `just run-user-config` deliberately
   opts into personal hooks and is not suitable for isolated qualification.
4. In a second terminal, run `just smoke-sa` to confirm process identity,
   IPC queries, and a working, version-matched scripting addition.
5. Create a **disposable** macOS Desktop and exactly two new Kitty windows.
   Confirm the active Space's stable `id`, index, window membership, and
   starting layers using `./bin/yabai -m query --spaces` and
   `./bin/yabai -m query --windows --space`. Avoid existing work Spaces.
6. Label only the disposable Space using
   `./bin/yabai -m space <index> --label yabai-layer-qualification`, then
   change it to BSP using `--layout bsp`. Confirm both test windows are
   `is-floating=false`, `layer=normal`, `sub-layer=below`, and have
   negative `sub-level`.
7. Use `just qualify-layer-cycle <index> <stable-space-id> <kitty-id-1>
   <kitty-id-2>`. The script refuses to mutate anything unless the Space
   remains active, labeled, BSP/stack managed, has exactly those two Kitty
   windows, and both are initially lowered. It attempts to restore the
   original layout even if the float probe fails.

Do not repeat a failed mutation until its output and the Space's current
state have been inspected. The script may reorder the two **test** windows.
An empty `just qualify-layers` finding is not evidence that transitions
work; its audit is read-only and flags only *potential* stale layers.

## Related limitations and remaining checks

The WindowServer
`_CGXPackagesSetWindowConstraints: Invalid window` log counts fluctuated
widely with and without the daemon present, so they cannot be attributed to
this change. The remaining broader fork-correctness checks are recorded in
`docs/fork-integration.md`. The original fix is scoped to
layout-transition restoration with a healthy Dock scripting addition.

**No GitHub Actions.** Validation is local; no workflow file belongs in
this fork's integration branches.
