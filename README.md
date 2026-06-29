# sway-setup

My personal Sway (Wayland) setup for Debian 12, kept alongside an existing
GNOME/XFCE install. One script installs the packages, drops the config files
into place, and sets fish as the login shell.

## Quick start

```bash
git clone <this-repo> sway-setup
cd sway-setup
./install.sh
```

Then log out and pick **Sway** from the GDM login screen (click your username,
then the gear icon at bottom-right).

## What it does

`install.sh`:

1. **Installs packages** — `sway foot wofi swayidle pavucontrol fonts-firacode fish`.
2. **Copies config files** from `config/` into `~/.config/` (backing up anything
   it would overwrite to `~/.config-backup-<timestamp>/`).
3. **Sets fish as the login shell** via `chsh` (if it isn't already).

It's idempotent — safe to re-run.

## Layout

```
config/
├── sway/config              # window manager: keybinds, outputs, workspaces
├── foot/foot.ini            # terminal: fish shell + Fira Code font
└── environment.d/10-shell.conf  # session-wide SHELL=fish
```

`config/` mirrors `~/.config/`, so adding a new dotfile is just: drop it under
`config/`, add its path to the `FILES` array in `install.sh`.

## Configuration notes

- **Monitors are machine-specific.** `config/sway/config` hard-codes two HP 25es
  1080p panels: `HDMI-A-1` (landscape) and `DP-3` (portrait, `transform 90`).
  On different hardware, run `swaymsg -t get_outputs` from inside Sway and edit
  the `output` lines (names, `transform`, `position`).
- **Modifier is Super** (the Windows key). Key binds: `Super+Return` = foot,
  `Super+Space` = wofi, `Super+Shift+S` = suspend, `Super+Shift+E` = exit.
  Full list in `config/sway/config`.
- **No lock screen / status bar / notification daemon** by design. Displays
  blank via DPMS after 10 min idle (swayidle), relying on LUKS for security.
- **Why `environment.d/10-shell.conf`?** The login shell is fish, but GDM
  exports `SHELL=/bin/bash` into the graphical session; this overrides it so
  apps that follow `$SHELL` (foot, editors) get fish.

See `sway_setup.md` for the original design notes and rationale.
