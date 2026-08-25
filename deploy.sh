#!/bin/sh
# d3hack-custom: copy the built subsdk9/main.npdm to BOTH exefs locations.
# Ryujinx actually loads mods/contents/<tid>/d3hack/exefs -- the sdcard/atmosphere
# path is only kept in sync so a real Switch / Atmosphere copy stays current.
# Deploying to just one of them silently runs the previous build.
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
TID=01001b300b9be000
for D in "$APPDATA/Ryujinx/mods/contents/$TID/d3hack/exefs" \
         "$APPDATA/Ryujinx/sdcard/atmosphere/contents/$TID/exefs"; do
    [ -d "$D" ] || { echo "skip (missing): $D"; continue; }
    cp "$SRC/build/work.nso"  "$D/subsdk9"
    cp "$SRC/build/work.npdm" "$D/main.npdm"
    echo "deployed -> $D"
done
