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

PACKAGES=(sway foot wofi swayidle pavucontrol fonts-firacode fish)

# Files to install, relative to config/ (source) and ~/.config (destination).
FILES=(
    sway/config
    foot/foot.ini
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

# --- 3. Login shell -------------------------------------------------------
fish_path="$(command -v fish || echo /usr/bin/fish)"
current_shell="$(getent passwd "$USER" | cut -d: -f7)"
if [ "$current_shell" != "$fish_path" ]; then
    info "Setting login shell to fish ($fish_path)"
    sudo chsh -s "$fish_path" "$USER"
else
    info "Login shell already fish"
fi

cat <<EOF

Done.

Next steps:
  1. Log out of your current session.
  2. At the GDM login screen, click your username, then the gear icon
     (bottom-right) and select "Sway".
  3. Log in.

Note: config/sway/config contains monitor output names, rotation and
positions specific to this machine (two HP 25es panels on HDMI-A-1 and
DP-3). On different hardware, run 'swaymsg -t get_outputs' and edit the
output lines. See README.md.
EOF
