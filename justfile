# yabai development helpers. Run from the repository/worktree root.
set shell := ["bash", "-euo", "pipefail", "-c"]

sign_id := env_var_or_default("YABAI_SIGN_ID", "Apple Development")
config_file := env_var_or_default("YABAI_CONFIG", env_var("HOME") + "/.config/yabai/yabairc")
qualification_config := justfile_directory() + "/scripts/qualification.yabairc"

# List available recipes.
default:
    @just --list

# Run the Python qualification suite with isolated pytest via uv.
test-python:
    uv run --no-project --isolated --with-requirements requirements-test.txt python -m pytest -q tests

# Validate a patch without modifying files.
check-patch file:
    git apply --check --index {{quote(file)}}

# Apply and stage a patch, then inspect its summary.
apply file: (check-patch file)
    git apply --index {{quote(file)}}
    git diff --cached --check
    git diff --cached --stat

# Optimized build, sign, and verify (does not install system-wide).
build:
    make install
    codesign --force --sign {{quote(sign_id)}} ./bin/yabai
    codesign --verify --verbose=2 ./bin/yabai
    @./bin/yabai --version
    @lipo -archs ./bin/yabai
    python3 ./scripts/qualification_build_stamp.py write

# Clean/regenerate embedded scripting addition, then optimize and sign.
rebuild-sa:
    make clean
    make
    just build

# Read-only safety check before starting a foreground qualification daemon.
qualify-preflight:
    python3 ./scripts/qualify_runtime.py preflight

# Confirm the running daemon is this worktree's binary, not Homebrew yabai.
qualify-running:
    python3 ./scripts/qualify_runtime.py running

# Default qualification daemon uses a deliberately inert startup config.
# The user's yabairc can register Dock hooks or launch external processes.
run: qualify-preflight
    ./bin/yabai --verbose --config {{quote(qualification_config)}}

# Explicit opt-in: execute normal user yabairc, including arbitrary hooks.
# Not suitable for isolated regression qualification.
run-user-config: qualify-preflight
    @printf '%s\n' 'WARNING: executing user yabairc; Dock and topology hooks may run' >&2
    ./bin/yabai --verbose --config {{quote(config_file)}}

# Read-only daemon checks that do not require an injected Dock payload.
smoke: qualify-running
    @printf 'Displays: '; ./bin/yabai -m query --displays | jq length
    @printf 'Spaces:   '; ./bin/yabai -m query --spaces | jq length
    @printf 'Windows:  '; ./bin/yabai -m query --windows | jq length

# Additionally require a version-matching, healthy Dock scripting addition.
smoke-sa: smoke
    ./bin/yabai --check-sa

# Refresh missing accessibility windows manually.
refresh:
    ./bin/yabai -m window --refresh

# Enable or disable Zen window reconciliation.
auto state="on":
    ./bin/yabai -m config window_reconciliation {{quote(state)}}
    ./bin/yabai -m config window_reconciliation

# Read-only audit of potential stale sublayers on floating Spaces.
qualify-layers: qualify-running
    python3 ./scripts/qualify_layout_layers.py

# Mutates ONLY an active BSP/stack Space labeled yabai-layer-qualification,
# with EXACTLY the two specified managed Kitty test windows. It can reorder them.
# The persistent Space ID prevents silent retargeting after macOS reindexes Spaces.
qualify-layer-cycle space space_id kitty1 kitty2: qualify-running
    python3 ./scripts/qualify_layout_layers.py --cycle-index {{quote(space)}} --expected-space-id {{quote(space_id)}} --test-window-ids {{quote(kitty1)}} {{quote(kitty2)}} --allow-layout-mutation

# Stop the existing launchd service explicitly.
stop-installed:
    /opt/homebrew/bin/yabai --stop-service

# Restore the existing launchd service explicitly.
start-installed:
    /opt/homebrew/bin/yabai --start-service
