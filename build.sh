#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
DEST="$PWD/NativeBridge.app"
STAGE="$(mktemp -d -t nativebridge-build)"
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/NativeBridge.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -O -parse-as-library -target arm64-apple-macos14 Source/NativeBridge.swift -o "$APP/Contents/MacOS/NativeBridge"
cp Source/engine.py Source/ios_settings.py Source/build_extras.py "$APP/Contents/Resources/"
cp -R Source/iOS "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>NativeBridge</string>
<key>CFBundleIdentifier</key><string>local.nativebridge.builder</string>
<key>CFBundleExecutable</key><string>NativeBridge</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.5.0</string>
<key>CFBundleVersion</key><string>8</string>
<key>LSMinimumSystemVersion</key><string>14.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
xattr -cr "$APP"
codesign --force --sign - "$APP"
ditto "$APP" "$DEST"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$PWD/NativeBridge.zip"
echo "Built: $DEST"
