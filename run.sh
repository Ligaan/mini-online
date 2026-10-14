#!/usr/bin/env bash
set -euo pipefail

HOST=pi
DIR=mini-online
SESSION=server

# 1. Removes any old session and the old server with it
ssh "$HOST" "tmux kill-session -t $SESSION 2>/dev/null || true"

# 2. Starts the server inside a detached tmux session
ssh "$HOST" "tmux new-session -d -s $SESSION ./$DIR/build/server/server"

# 3. Opens a window on the Pi's screen, attached to that session
ssh "$HOST" "WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/\$(id -u) nohup lxterminal -e tmux attach -t $SESSION >/dev/null 2>&1 &"

# 4. attach from the laptop
ssh -t "$HOST" "tmux attach -t $SESSION"