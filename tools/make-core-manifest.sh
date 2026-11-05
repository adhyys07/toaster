#!/bin/sh
# Writes the manifest.txt a Toaster core server needs.
#
# Toaster loads cores as native code, so it checks every download against a
# manifest of SHA-256 checksums and refuses to install one that does not
# match. The server needs a manifest.txt beside the cores; without it, nothing
# installs.
#
# The -t is not optional. sha256sum defaults to binary mode on Windows and
# writes "<hash> *name"; the asterisk then reads as part of the filename, so
# the manifest parses but matches nothing and the install fails with
# "manifest.txt does not list ...".
#
# Regenerate this whenever a core changes, or the old checksums will reject
# the new files.
#
# Usage: tools/make-core-manifest.sh [core-directory]

set -eu

directory="${1:-.}"
cd "$directory"

if ! ls ./*.dll >/dev/null 2>&1 && ! ls ./*.so >/dev/null 2>&1; then
    echo "no .dll or .so files in $directory" >&2
    exit 1
fi

# -t forces text mode, which writes "<hash>  <name>" with no mode marker.
sha256sum -t ./*.dll ./*.so 2>/dev/null |
    sed 's|\./||' > manifest.txt

count=$(wc -l < manifest.txt | tr -d ' ')
if [ "$count" -eq 0 ]; then
    echo "nothing was hashed, so no manifest was written" >&2
    rm -f manifest.txt
    exit 1
fi

cat manifest.txt
echo
echo "wrote manifest.txt with $count entries"
echo "upload it beside the cores, so it resolves at <core-server>/manifest.txt"

# Warn about anything Toaster expects but the manifest does not carry.
for core in swanstation_libretro.dll ppsspp_libretro.dll pcsx2_libretro.dll; do
    if ! grep -q " $core\$" manifest.txt; then
        echo "warning: $core is not in the manifest; Toaster will refuse it" >&2
    fi
done
