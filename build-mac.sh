#!/bin/bash
# Builds Burial Voice.vst3 on macOS (universal: Apple Silicon + Intel) and installs it
# into ~/Library/Audio/Plug-Ins/VST3.
# Needs: Xcode Command Line Tools (xcode-select --install), CMake (brew install cmake), git.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -d JUCE8 ]; then
    git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE8
fi

cmake -B build-mac -DCMAKE_BUILD_TYPE=Release -DBV_JUCE_DIR="$PWD/JUCE8" \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-mac --config Release -j "$(sysctl -n hw.ncpu)"

DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/Burial Voice.vst3"
cp -R "build-mac/BurialVoice_artefacts/Release/VST3/Burial Voice.vst3" "$DEST/"
codesign --force --deep -s - "$DEST/Burial Voice.vst3"
xattr -dr com.apple.quarantine "$DEST/Burial Voice.vst3" 2>/dev/null || true

echo "Installed: $DEST/Burial Voice.vst3 — rescan plug-ins in your DAW."
