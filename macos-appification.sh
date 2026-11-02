#!/usr/bin/env bash
#
# macos-appification.sh
#
# Package pt2-clone as a native macOS application bundle.
#
# Requirements:
#   - macOS (Apple Silicon)
#   - pt2-clone already compiled using CMake
#   - SDL2 available through Homebrew
#
# Usage:
#   ./macos-appification.sh
#
# The resulting ProTracker.app depends on Homebrew's SDL2 library.
# It is not a standalone distributable application.
#

set -euo pipefail

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

APP_NAME="ProTracker"
APP_IDENTIFIER="org.pt2clone.protracker"
APP_VERSION="1.0"

APP_PATH="${ROOT_DIR}/${APP_NAME}.app"
BINARY_SOURCE="${ROOT_DIR}/release/other/pt2-clone"
CONFIG_SOURCE="${ROOT_DIR}/release/other/protracker.ini"

BINARY_DEST="${APP_PATH}/Contents/MacOS/pt2-clone"
RESOURCES_DIR="${APP_PATH}/Contents/Resources"
PLIST_PATH="${APP_PATH}/Contents/Info.plist"

ICON_SOURCE="${ROOT_DIR}/release/macos/pt2-clone-macos.app/Contents/Resources/pt2-clone-macos.icns"
ICON_DEST="${RESOURCES_DIR}/pt2-clone-macos.icns"

# ------------------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------------------

log() {
    printf '\n==> %s\n' "$*"
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

# ------------------------------------------------------------------------------
# Preflight checks
# ------------------------------------------------------------------------------

log "Checking build environment..."

[[ "$(uname -s)" == "Darwin" ]] ||
    die "This script requires macOS."

[[ "$(uname -m)" == "arm64" ]] ||
    die "This script currently supports Apple Silicon only."

[[ -f "$BINARY_SOURCE" ]] ||
    die "Missing executable: $BINARY_SOURCE"

[[ -f "$CONFIG_SOURCE" ]] ||
    die "Missing configuration: $CONFIG_SOURCE"

[[ -x "$BINARY_SOURCE" ]] ||
    die "Source executable is not executable."

[[ -f "$ICON_SOURCE" ]] ||
    die "Missing official application icon: $ICON_SOURCE"

file "$BINARY_SOURCE"

# ------------------------------------------------------------------------------
# Create application bundle
# ------------------------------------------------------------------------------

log "Creating ${APP_NAME}.app..."

rm -rf -- "$APP_PATH"

mkdir -p \
    "${APP_PATH}/Contents/MacOS" \
    "$RESOURCES_DIR"

cp "$BINARY_SOURCE" "$BINARY_DEST"
chmod 755 "$BINARY_DEST"

cp "$CONFIG_SOURCE" "$RESOURCES_DIR/"
cp "$ICON_SOURCE" "$ICON_DEST"

# ------------------------------------------------------------------------------
# Generate Info.plist
# ------------------------------------------------------------------------------

log "Generating Info.plist..."

cat > "$PLIST_PATH" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>pt2-clone</string>

    <key>CFBundleIdentifier</key>
    <string>${APP_IDENTIFIER}</string>

    <key>CFBundleName</key>
    <string>${APP_NAME}</string>

    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>

    <key>CFBundlePackageType</key>
    <string>APPL</string>

    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>

    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>

    <key>NSHighResolutionCapable</key>
    <true/>

    <key>CFBundleIconFile</key>
    <string>pt2-clone-macos.icns</string>
</dict>
</plist>
EOF

plutil -lint "$PLIST_PATH"

# ------------------------------------------------------------------------------
# Verify executable dependencies
# ------------------------------------------------------------------------------

log "Checking dynamic library dependencies..."

otool -L "$BINARY_DEST"

# ------------------------------------------------------------------------------
# Code signing
# ------------------------------------------------------------------------------

log "Applying ad hoc code signature..."

codesign \
    --force \
    --sign - \
    "$BINARY_DEST"

codesign \
    --force \
    --sign - \
    "$APP_PATH"

# ------------------------------------------------------------------------------
# Verify signature
# ------------------------------------------------------------------------------

log "Verifying application signature..."

codesign \
    --verify \
    --deep \
    --strict \
    --verbose=2 \
    "$APP_PATH"

# ------------------------------------------------------------------------------
# Final information
# ------------------------------------------------------------------------------

log "Application successfully created!"

printf '\n'
printf 'Application: %s\n' "$APP_PATH"
printf 'Architecture: %s\n' "$(uname -m)"
printf 'Bundle ID:    %s\n' "$APP_IDENTIFIER"
printf 'Version:      %s\n' "$APP_VERSION"
printf '\n'

printf 'To launch:\n'
printf '  open "%s"\n' "$APP_PATH"

printf '\nTo install:\n'
printf '  ditto "%s" "/Applications/%s.app"\n' \
    "$APP_PATH" "$APP_NAME"

printf '\n'
