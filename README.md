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
├── sway/config              # window manager: keybinds, outputs, workspaces, bar, theme
├── sway/statusbar.sh        # swaybar status line: volume | network | clock
├── foot/foot.ini            # terminal: fish shell + Fira Code font + transparency
├── wofi/style.css           # launcher styling (Tokyo Night)
├── wofi/config              # launcher behavior
└── environment.d/10-shell.conf  # session-wide SHELL=fish
```

`config/` mirrors `~/.config/`, so adding a new dotfile is just: drop it under
`config/`, add its path to the `FILES` array in `install.sh`.

## Keeping the repo in sync

This repo uses a **copy** model — `install.sh` copies `config/` → `~/.config/`.
The two are not linked, so after you tweak a live config you must copy it back
into the repo and commit, or the change is lost on the next fresh install:

```bash
# after editing e.g. ~/.config/sway/config and reloading Sway:
cp ~/.config/sway/config config/sway/config
git add -A && git commit -m "tweak: ..."
```

Check for drift anytime with:

```bash
for f in sway/config foot/foot.ini environment.d/10-shell.conf; do
    diff -u config/$f ~/.config/$f
done
```

## Configuration notes

- **Monitors are machine-specific.** `config/sway/config` hard-codes two HP 25es
  1080p panels: `HDMI-A-1` (landscape) and `DP-3` (portrait, `transform 90`).
  On different hardware, run `swaymsg -t get_outputs` from inside Sway and edit
  the `output` lines (names, `transform`, `position`).
- **Modifier is Super** (the Windows key). Key binds: `Super+Return` = foot,
  `Super+Space` = wofi, `Super+Shift+S` = suspend, `Super+Shift+E` = exit.
  Full list in `config/sway/config`.
- **Top status bar** is swaybar (built into Sway), configured in the `bar {}`
  block of `config/sway/config`. Its status line comes from
  `config/sway/statusbar.sh` (volume via `wpctl`, network via `nmcli`, clock).
  The bar is translucent (`#RRGGBBAA` colors) to match the foot transparency.
- **No lock screen / notification daemon** by design. Displays blank via DPMS
  after 10 min idle (swayidle), relying on LUKS for security.
- **Why `environment.d/10-shell.conf`?** The login shell is fish, but GDM
  exports `SHELL=/bin/bash` into the graphical session; this overrides it so
  apps that follow `$SHELL` (foot, editors) get fish.

## Theme — Tokyo Night

A single palette (`bg #1a1b26`, `fg #c0caf5`, blue `#7aa2f7`, purple `#bb9af7`,
…) is applied across every layer:

- **foot** — full 16-color ANSI set in `config/foot/foot.ini` `[colors]`.
- **Sway** — window border colors, `gaps inner 8`, `default_border pixel 2`,
  and a solid `#1a1b26` background (`output * bg`), all in `config/sway/config`.
- **swaybar** — recolored `colors {}` block.
- **wofi** — `config/wofi/style.css`.
- **GTK apps** — dark Adwaita + Papirus-Dark icons, applied by `install.sh` via
  `gsettings`. (No apt-packaged exact Tokyo Night GTK theme; this is the
  reproducible dark approximation.)

To retheme, swap the hex values in those files for another palette's. The flashy
rices with rounded corners / blur / shadows are **SwayFX** (a Sway fork) or
**Hyprland**, not vanilla Sway — this config is vanilla (flat colors + gaps).

See `sway_setup.md` for the original design notes and rationale.
