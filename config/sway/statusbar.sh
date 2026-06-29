#!/usr/bin/env bash
# swaybar status line: volume | network | date/time
# Plain-text status_command for swaybar — prints one line every 2.5s.
# Uses wpctl (wireplumber) and nmcli (network-manager); no extra deps.
# Deliberately simple: a plain timed loop, no signals/FIFO (those proved
# fragile under rapid volume scrolling).
set -u

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

while :; do
    printf '%s   |   %s   |   %s\n' "$(volume)" "$(network)" "$(date '+%a %d %b  %H:%M')"
    sleep 2.5
done
