#!/bin/bash
# Copies Burial Voice (VST3 + AU) into your user plug-in folders and clears the
# download quarantine flag so the DAW is allowed to load it.
set -e
cd "$(dirname "$0")"

VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3" "$AU"

rm -rf "$VST3/Burial Voice.vst3" "$AU/Burial Voice.component"
cp -R "Burial Voice.vst3" "$VST3/"
cp -R "Burial Voice.component" "$AU/"
xattr -dr com.apple.quarantine "$VST3/Burial Voice.vst3" "$AU/Burial Voice.component" 2>/dev/null || true

# make Logic / GarageBand re-scan Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Burial Voice установлен:"
echo "  VST3: $VST3/Burial Voice.vst3"
echo "  AU:   $AU/Burial Voice.component"
echo "Перезапусти DAW и пересканируй плагины."
