#!/bin/bash
# Builds an installer package for macOS: VST3 + AU into /Library/Audio/Plug-Ins, app into /Applications.
set -euo pipefail
VERSION="$1"; SRC="$2"; OUT="$3"
NAME="OL-4 Optical Leveler"
mkdir -p "$OUT" pkgroot/vst3 pkgroot/au pkgroot/app comps
cp -R "$SRC/VST3/$NAME.vst3" pkgroot/vst3/
cp -R "$SRC/AU/$NAME.component" pkgroot/au/
cp -R "$SRC/Standalone/$NAME.app" pkgroot/app/
pkgbuild --root pkgroot/vst3 --identifier com.olaudio.ol4.vst3 --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/VST3" comps/vst3.pkg
pkgbuild --root pkgroot/au   --identifier com.olaudio.ol4.au   --version "$VERSION" --install-location "/Library/Audio/Plug-Ins/Components" comps/au.pkg
pkgbuild --root pkgroot/app  --identifier com.olaudio.ol4.app  --version "$VERSION" --install-location "/Applications" comps/app.pkg
cat > distribution.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $VERSION</title>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="app"/>
  </choices-outline>
  <choice id="vst3" title="VST3 plugin" description="For Ableton Live, FL Studio, Cubase, Studio One, Reaper and other VST3 hosts."><pkg-ref id="com.olaudio.ol4.vst3"/></choice>
  <choice id="au" title="Audio Unit (AU)" description="For Logic Pro and GarageBand."><pkg-ref id="com.olaudio.ol4.au"/></choice>
  <choice id="app" title="Standalone app" description="Use with a mic or interface, no DAW needed."><pkg-ref id="com.olaudio.ol4.app"/></choice>
  <pkg-ref id="com.olaudio.ol4.vst3">vst3.pkg</pkg-ref>
  <pkg-ref id="com.olaudio.ol4.au">au.pkg</pkg-ref>
  <pkg-ref id="com.olaudio.ol4.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution distribution.xml --package-path comps "$OUT/OL-4-macOS-Installer-$VERSION.pkg"
( cd "$SRC" && zip -qry "$OLDPWD/$OUT/OL-4-macOS-manual.zip" "VST3/$NAME.vst3" "AU/$NAME.component" "Standalone/$NAME.app" )
cp installer/INSTALL-macOS.txt "$OUT/" 2>/dev/null || true
ls -la "$OUT"
