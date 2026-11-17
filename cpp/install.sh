#!/usr/bin/env bash
# Installs Firmament (C++) for this user on Linux: ~/.local/share/Firmament, ~/.local/bin/firmament and a menu entry.
set -e
cd "$(dirname "$0")"
DEST="$HOME/.local/share/Firmament"
mkdir -p "$DEST" "$HOME/.local/bin" "$HOME/.local/share/applications"
cp -r build/Firmament build/assets build/libstable-diffusion.so* "$DEST/" 2>/dev/null || cp -r build/Firmament build/assets "$DEST/"
printf '#!/bin/sh\nLD_LIBRARY_PATH="%s" exec "%s/Firmament" "$@"\n' "$DEST" "$DEST" > "$HOME/.local/bin/firmament"
chmod +x "$HOME/.local/bin/firmament"
printf '[Desktop Entry]\nName=Atelier - Firmament\nComment=360 skyboxes from a prompt\nExec=%s/.local/bin/firmament %%f\nIcon=%s/assets/logo.png\nType=Application\nCategories=Graphics;\n' "$HOME" "$DEST" > "$HOME/.local/share/applications/firmament.desktop"
echo "Installed: $DEST (run: firmament)"
