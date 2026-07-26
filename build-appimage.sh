#!/usr/bin/env bash
# Construit Pushly-x86_64.AppImage (nécessite appimagetool dans le PATH ou
# APPIMAGETOOL pointant vers le binaire).
set -euo pipefail
cd "$(dirname "$0")"

APPIMAGETOOL="${APPIMAGETOOL:-appimagetool}"

make
rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/share/pushly \
         AppDir/usr/share/applications \
         AppDir/usr/share/icons/hicolor/256x256/apps

cp pushly AppDir/usr/bin/
cp start.mp3 stop.mp3 PushlyLogo.png AppDir/usr/share/pushly/
cp pushly.desktop AppDir/usr/share/applications/
cp PushlyLogo.png AppDir/usr/share/icons/hicolor/256x256/apps/pushly.png
cp pushly.desktop AppDir/
cp PushlyLogo.png AppDir/pushly.png

cat > AppDir/AppRun <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/pushly" "$@"
EOF
chmod +x AppDir/AppRun

ARCH=x86_64 "$APPIMAGETOOL" AppDir Pushly-x86_64.AppImage
echo "OK: Pushly-x86_64.AppImage"
