#!/usr/bin/env bash
# Builds a universal macOS Release, signs it with your Developer ID, notarises it,
# staples the tickets and writes the zips plus SHA256SUMS.txt into dist/.
#
# One-time setup (on your Mac):
#   1. Install a "Developer ID Application" certificate in your login keychain.
#        security find-identity -v -p codesigning     # shows the exact name
#   2. Store notarisation credentials once (uses an app-specific password):
#        xcrun notarytool store-credentials "acoustic-notary" \
#            --apple-id you@example.com --team-id YOURTEAMID
#
# Usage:
#   DEVELOPER_ID="Developer ID Application: Your Name (TEAMID)" \
#   scripts/build_mac.sh 0.9.0-beta
#
# Optional: NOTARY_PROFILE (default acoustic-notary), BUILD_DIR (default build-mac),
#           SKIP_NOTARIZE=1 to sign only (for a quick local test).
set -euo pipefail

VERSION="${1:?Usage: scripts/build_mac.sh <version>, e.g. 0.9.0-beta}"
: "${DEVELOPER_ID:?Set DEVELOPER_ID to your 'Developer ID Application: ...' identity}"
NOTARY_PROFILE="${NOTARY_PROFILE:-acoustic-notary}"
BUILD_DIR="${BUILD_DIR:-build-mac}"
SKIP_NOTARIZE="${SKIP_NOTARIZE:-0}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ART="$BUILD_DIR/AcousticAnalyzer_artefacts/Release"
DIST="$ROOT/dist"
ENTITLEMENTS="$ROOT/scripts/entitlements-standalone.plist"

echo "==> Building universal Release"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0"
cmake --build "$BUILD_DIR" --config Release --parallel

VST3="$ART/VST3/AcousticAnalyzer.vst3"
AU="$ART/AU/AcousticAnalyzer.component"
APP="$ART/Standalone/AcousticAnalyzer.app"
for b in "$VST3" "$AU" "$APP"; do
    [ -e "$b" ] || { echo "Missing build output: $b" >&2; exit 1; }
done

echo "==> Checking architectures"
for b in "$VST3" "$AU" "$APP"; do
    bin=$(find "$b/Contents/MacOS" -type f -maxdepth 1 | head -n 1)
    echo "$(basename "$b"): $(lipo -archs "$bin")"
done

echo "==> Signing (hardened runtime + secure timestamp)"
codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" "$VST3"
codesign --force --options runtime --timestamp --sign "$DEVELOPER_ID" "$AU"
codesign --force --options runtime --timestamp --entitlements "$ENTITLEMENTS" \
         --sign "$DEVELOPER_ID" "$APP"

for b in "$VST3" "$AU" "$APP"; do
    codesign --verify --deep --strict --verbose=2 "$b"
done

mkdir -p "$DIST"

if [ "$SKIP_NOTARIZE" = "1" ]; then
    echo "==> SKIP_NOTARIZE=1: skipping notarisation. These zips are signed but NOT notarised."
else
    echo "==> Notarising (this can take a few minutes)"
    SUBMIT="$DIST/notarize-submit.zip"
    rm -f "$SUBMIT"
    # One submission for all three bundles. ditto keeps the signature intact.
    STAGE_N="$(mktemp -d)"
    cp -R "$VST3" "$AU" "$APP" "$STAGE_N/"
    ditto -c -k --keepParent "$STAGE_N" "$SUBMIT"
    rm -rf "$STAGE_N"

    xcrun notarytool submit "$SUBMIT" --keychain-profile "$NOTARY_PROFILE" --wait
    rm -f "$SUBMIT"

    echo "==> Stapling tickets"
    for b in "$VST3" "$AU" "$APP"; do
        xcrun stapler staple "$b"
        xcrun stapler validate "$b"
    done
fi

echo "==> Packaging"
STAGE="$(mktemp -d)/AcousticAnalyzer-$VERSION-macOS"
mkdir -p "$STAGE"
cp -R "$VST3" "$AU" "$APP" "$STAGE/"
cp README.md "$STAGE/"
[ -f LICENSE ] && cp LICENSE "$STAGE/"
ZIP="$DIST/AcousticAnalyzer-$VERSION-macOS.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$STAGE" "$ZIP"
rm -rf "$(dirname "$STAGE")"
echo "Created $ZIP"

"$ROOT/scripts/make_checksums.sh" "$DIST"

echo
echo "Done. Before you upload, test the ZIP the way a user would:"
echo "  unzip it somewhere else, copy the plugins to the system folders and load them in your DAW."
echo "  Run 'auval -v aufx AN01 AL1Z' to confirm the AU validates."
