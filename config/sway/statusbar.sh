#!/usr/bin/env bash
# swaybar status line: volume | network | date/time
# Plain-text status_command for swaybar — prints one line, then sleeps.
# Uses wpctl (wireplumber) and nmcli (network-manager); no extra deps.
#
# The sleep is interruptible: send SIGUSR1 to refresh immediately
# (the volume keybinds do this so the bar updates the instant you turn the
# knob, while still idling at the slow tick otherwise):
#   pkill -USR1 -f statusbar.sh
set -u

sleep_pid=
trap 'kill "$sleep_pid" 2>/dev/null' USR1

volume() {
    local out
    out="$(wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null)" || { printf 'vol --'; return; }
    case "$out" in
        *MUTED*) printf 'vol muted' ;;
        # out looks like "Volume: 0.52"; field 2 is the 0..1 level
        *) printf 'vol %d%%' "$(awk '{printf "%d", $2 * 100 + 0.5}' <<<"$out")" ;;
    esac
}

network() {
    local name
    name="$(nmcli -t -f NAME connection show --active 2>/dev/null | head -n1)"
    if [ -n "$name" ]; then printf 'net %s' "$name"; else printf 'net offline'; fi
}

while true; do
    printf '%s   |   %s   |   %s\n' "$(volume)" "$(network)" "$(date '+%a %d %b  %H:%M')"
    sleep 5 & sleep_pid=$!
    wait "$sleep_pid"
done
