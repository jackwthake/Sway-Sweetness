#!/usr/bin/env bash
# Screenshot helper for Sway (grim + slurp + wl-clipboard).
# Saves a PNG to ~/Pictures/Screenshots AND copies it to the clipboard.
#   screenshot.sh full     capture the whole focused output
#   screenshot.sh region   drag-select an area (Esc cancels cleanly)
set -euo pipefail

mode="${1:-full}"
dir="${XDG_PICTURES_DIR:-$HOME/Pictures}/Screenshots"
mkdir -p "$dir"
file="$dir/$(date +'%Y-%m-%d_%H-%M-%S').png"

case "$mode" in
    region)
        geom="$(slurp)" || exit 0   # Esc / cancel -> no-op, no file written
        grim -g "$geom" "$file"
        ;;
    full|*)
        grim "$file"
        ;;
esac

# Copy the saved PNG to the clipboard so it can be pasted directly.
wl-copy --type image/png < "$file"
