#!/usr/bin/env bash
#
# build-mac.sh: build YOI.vst3 on macOS, run its tests, and link it into ~/Library/Audio/Plug-Ins/VST3.
#
# Steinberg's validator runs on the plug-in as part of the build. The result is ad-hoc signed,
# which is enough to run it on this Mac.
#
# USAGE (from the workspace root):
#   VST3/scripts/build-mac.sh            # Release
#   VST3/scripts/build-mac.sh Debug      # Debug, with the web inspector enabled
#
# Set VST3_SDK_DIR and CHOC_DIR to existing checkouts to skip the first download.
#

set -euo pipefail

CONFIGURATION="${1:-Release}"
VST3_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$VST3_DIR/build"

UI_DEBUG=OFF
if [[ "$CONFIGURATION" == "Debug" ]]; then
    UI_DEBUG=ON
fi

cmake -S "$VST3_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIGURATION" -DYOI_UI_DEBUG="$UI_DEBUG" \
    ${VST3_SDK_DIR:+-DVST3_SDK_DIR="$VST3_SDK_DIR"} ${CHOC_DIR:+-DCHOC_DIR="$CHOC_DIR"}
cmake --build "$BUILD_DIR" --target YOI yoi_vst3_tests -j "$(sysctl -n hw.ncpu)"

echo
echo "Running tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo
echo "Built:       $BUILD_DIR/VST3/$CONFIGURATION/YOI.vst3"
echo "Linked into: ~/Library/Audio/Plug-Ins/VST3/YOI.vst3"
