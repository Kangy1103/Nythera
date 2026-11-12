#!/usr/bin/env bash
set -euo pipefail

# Remove only this template's output; leave client theme selections and other CSS alone.
config_dir="${XDG_CONFIG_HOME:-$HOME/.config}"
for theme in \
    "$config_dir/vesktop/themes/nythera.theme.css" \
    "$HOME/.var/app/dev.vencord.Vesktop/config/vesktop/themes/nythera.theme.css" \
    "$config_dir/Vencord/themes/nythera.theme.css" \
    "$HOME/.var/app/com.discordapp.Discord/config/Vencord/themes/nythera.theme.css"; do
    rm -f -- "$theme"
done
