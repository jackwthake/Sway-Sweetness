# Sway Setup — Context Doc

## System
- Debian 12 (Bookworm), current install ~1.5 years old
- Just ran sudo apt-get update + upgrade
- Currently using XFCE on X11
- Keeping XFCE alongside Sway, not a fresh install
- Shell: fish (set system-wide via chsh)
- Terminal: foot

## Hardware
- Two 1080p monitors
- One landscape, one vertical (portrait, transform 90)
- Output names TBD — run `swaymsg -t get_outputs` to confirm

## Packages to install
```
sudo apt install sway foot wofi swayidle
```

## What we want
- Sway + wofi (app launcher / command palette)
- fish as default shell system-wide
- No lock screen (LUKS full disk encryption at boot is sufficient)
- No notification daemon
- No status bar (at least to start)
- Display sleep via swayidle + DPMS, no swaylock
- Two monitor layout with workspace assignment per output
- Mouse pointer present and functional

## Minimal config goals
- Super as modifier
- foot bound to Super+Return
- wofi bound to Super+space
- systemctl suspend bound to a key
- Both monitors configured with correct rotation and position
- Workspaces pinned to outputs
- dbus environment line for Wayland compat

## Useful diagnostics
- `swaymsg -t get_outputs` — monitor info
- `swaymsg -t get_tree` — window tree
- `journalctl -xe` — crash/error logs

## Audio / Wifi
- pavucontrol for audio GUI
- nmtui for wifi

---

*Footnote: end goal includes a custom wlr-layer-shell Wayland client as the wallpaper — a live rasterizer scene with system vitals driving environmental effects (CPU → wind etc) and a small animated cat. Two surfaces, one per output, probably one process. Not in scope for this session.*
