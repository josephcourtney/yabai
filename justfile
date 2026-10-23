# yabai development helpers. Run from the repository/worktree root.
set shell := ["bash", "-euo", "pipefail", "-c"]

sign_id := env_var_or_default("YABAI_SIGN_ID", "Apple Development")
config_file := env_var_or_default("YABAI_CONFIG", env_var("HOME") + "/.config/yabai/yabairc")

# List available recipes.
default:
    @just --list

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

# Run development daemon in the foreground (stop old daemon first).
run: qualify-preflight
    ./bin/yabai --verbose --config {{quote(config_file)}}

# Read-only checks only after confirming the test daemon's executable.
smoke: qualify-running
    ./bin/yabai --check-sa
    @printf 'Displays: '; ./bin/yabai -m query --displays | jq length
    @printf 'Spaces:   '; ./bin/yabai -m query --spaces | jq length
    @printf 'Windows:  '; ./bin/yabai -m query --windows | jq length

# Refresh missing accessibility windows manually.
refresh:
    ./bin/yabai -m window --refresh

# Enable or disable Zen window reconciliation.
auto state="on":
    ./bin/yabai -m config window_reconciliation {{quote(state)}}
    ./bin/yabai -m config window_reconciliation

# Stop the existing launchd service explicitly.
stop-installed:
    /opt/homebrew/bin/yabai --stop-service

# Restore the existing launchd service explicitly.
start-installed:
    /opt/homebrew/bin/yabai --start-service
