#!/usr/bin/env bash
#
# Sway setup installer.
# Installs packages, copies config files into ~/.config, and sets fish as the
# login shell. Idempotent and safe to re-run: existing config files are backed
# up before being overwritten.
#
# Usage:  ./install.sh
#
set -euo pipefail

# Resolve the repo directory regardless of where the script is run from.
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_SRC="$REPO_DIR/config"
CONFIG_DST="${XDG_CONFIG_HOME:-$HOME/.config}"

PACKAGES=(
    # Compositor + core apps
    sway foot wofi swayidle pavucontrol fonts-firacode fish
    # Login manager (greetd) + its text greeter (tuigreet)
    greetd tuigreet
    # Networking + audio (wpctl/pavucontrol talk to PipeWire)
    network-manager wireplumber pipewire-pulse
    # Wayland portals — screenshots/screen-share (wlr) + file dialogs (gtk)
    xdg-desktop-portal-wlr xdg-desktop-portal-gtk
    # GTK dark theme plumbing for the gsettings step below
    papirus-icon-theme gnome-themes-extra gsettings-desktop-schemas dconf-cli
)

# Files to install, relative to config/ (source) and ~/.config (destination).
FILES=(
    sway/config
    sway/statusbar.sh
    foot/foot.ini
    wofi/style.css
    wofi/config
    environment.d/10-shell.conf
)

info() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }

# --- 1. Packages ----------------------------------------------------------
info "Installing packages: ${PACKAGES[*]}"
sudo apt-get update
sudo apt-get install -y "${PACKAGES[@]}"

# --- 2. Config files ------------------------------------------------------
backup_dir="$HOME/.config-backup-$(date +%Y%m%d-%H%M%S)"
for rel in "${FILES[@]}"; do
    src="$CONFIG_SRC/$rel"
    dst="$CONFIG_DST/$rel"
    mkdir -p "$(dirname "$dst")"
    if [ -e "$dst" ] && ! cmp -s "$src" "$dst"; then
        mkdir -p "$(dirname "$backup_dir/$rel")"
        cp -a "$dst" "$backup_dir/$rel"
        info "backed up existing $dst -> $backup_dir/$rel"
    fi
    cp "$src" "$dst"
    info "installed $dst"
done

# --- 2b. Login manager (greetd) -------------------------------------------
# greetd config lives in /etc (not ~/.config), so it's handled separately.
# We drop in the validated config.toml (tuigreet greeter launching Sway),
# then make greetd the display manager.
info "Installing greetd config -> /etc/greetd/config.toml"
sudo install -d /etc/greetd
if [ -e /etc/greetd/config.toml ] && \
   ! cmp -s "$REPO_DIR/system/greetd/config.toml" /etc/greetd/config.toml; then
    sudo cp -a /etc/greetd/config.toml "/etc/greetd/config.toml.bak-$(date +%Y%m%d-%H%M%S)"
    info "backed up existing /etc/greetd/config.toml"
fi
sudo cp "$REPO_DIR/system/greetd/config.toml" /etc/greetd/config.toml

# Disable any other display manager first so the display-manager.service alias
# is free, then enable greetd. On a bare TTY install this loop is a no-op.
for dm in gdm gdm3 lightdm sddm; do
    if systemctl is-enabled "$dm" >/dev/null 2>&1; then
        info "Disabling existing display manager: $dm"
        sudo systemctl disable "$dm"
    fi
done
info "Enabling greetd as the display manager"
sudo systemctl enable greetd

# --- 3. Login shell -------------------------------------------------------
fish_path="$(command -v fish || echo /usr/bin/fish)"
current_shell="$(getent passwd "$(id -un)" | cut -d: -f7)"
if [ "$current_shell" != "$fish_path" ]; then
    info "Setting login shell to fish ($fish_path)"
    sudo chsh -s "$fish_path" "$(id -un)"
else
    info "Login shell already fish"
fi

# --- 4. GTK theme (dark + Papirus icons) ----------------------------------
# No apt-packaged exact Tokyo Night GTK theme; use dark Adwaita + Papirus-Dark
# so GTK apps (pavucontrol, file dialogs) match the dark palette.
if command -v gsettings >/dev/null; then
    info "Applying dark GTK theme + Papirus-Dark icons"
    gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark' || true
    gsettings set org.gnome.desktop.interface gtk-theme   'Adwaita-dark'  || true
    gsettings set org.gnome.desktop.interface icon-theme  'Papirus-Dark'  || true
fi

cat <<EOF

Done.

Next steps:
  1. Reboot (greetd replaces whatever display manager was running).
  2. At the tuigreet greeter, your username is pre-filled; type your
     password. Sway is the default session (toggle sessions with F3).
  3. Log in -> Sway.

If greetd ever fails to start, you land at a text TTY: log in and run
'sway' directly, or reinstall a graphical login with 'sudo apt install gdm3'.

Note: config/sway/config contains monitor output names, rotation and
positions specific to this machine (two HP 25es panels on HDMI-A-1 and
DP-3). On different hardware, run 'swaymsg -t get_outputs' and edit the
output lines. See README.md.
EOF
