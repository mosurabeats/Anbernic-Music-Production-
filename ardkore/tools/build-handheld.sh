#!/bin/bash
# Cross-build ARDKORE for aarch64 handhelds (muOS, Knulli, ROCKNIX via
# PortMaster) and package a zip.
#
# Links against Debian Bullseye's glibc 2.31 and SDL2 2.0.14 so the binary
# runs on the older userlands these firmwares ship. The device provides
# libSDL2 at runtime.
#
# Needs: aarch64-linux-gnu-gcc, curl, xz, dpkg-deb, zip (or python3).
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD=build
SYSROOT=$BUILD/sysroot-bullseye-arm64
MIRROR=${MIRROR:-http://deb.debian.org/debian}
CC=${CC:-aarch64-linux-gnu-gcc}
PKGS="libc6 libc6-dev linux-libc-dev libsdl2-2.0-0 libsdl2-dev"

if [ ! -f "$SYSROOT/.done" ]; then
    echo "== fetching Bullseye arm64 sysroot"
    mkdir -p "$BUILD/debs" "$SYSROOT"
    curl -fsSL "$MIRROR/dists/bullseye/main/binary-arm64/Packages.xz" | xz -d > "$BUILD/Packages"
    for p in $PKGS; do
        f=$(awk -v p="$p" '$1 == "Package:" { cur = $2 } $1 == "Filename:" && cur == p { print $2; exit }' "$BUILD/Packages")
        [ -n "$f" ] || { echo "package $p not found" >&2; exit 1; }
        curl -fsSL -o "$BUILD/debs/$(basename "$f")" "$MIRROR/$f"
        dpkg-deb -x "$BUILD/debs/$(basename "$f")" "$SYSROOT"
    done
    # Debian's dev symlinks are absolute (libm.so -> /lib/.../libm.so.6).
    # Point them inside the sysroot, or the linker silently falls back to
    # static archives that drag in GLIBC_PRIVATE symbols.
    find "$SYSROOT" -type l | while read -r link; do
        target=$(readlink "$link")
        case "$target" in /*) ln -sf "$(realpath -m "$SYSROOT$target")" "$link" ;; esac
    done
    touch "$SYSROOT/.done"
fi

echo "== compiling"
LIBDIR=$SYSROOT/usr/lib/aarch64-linux-gnu
# Use only the sysroot's headers: a distro cross compiler otherwise searches
# its own (newer) glibc headers first.
INCLUDES="-nostdinc -isystem $("$CC" -print-file-name=include) \
    -isystem $SYSROOT/usr/include/aarch64-linux-gnu -isystem $SYSROOT/usr/include"
"$CC" --sysroot="$SYSROOT" $INCLUDES -O2 -mcpu=cortex-a53 -std=gnu99 -Wall -Wextra -Wno-format-truncation \
    -include tools/glibc-compat.h -Isrc -I"$SYSROOT/usr/include/SDL2" -D_REENTRANT \
    -o "$BUILD/ardkore" \
    src/engine.c src/sample.c src/wav.c src/machine.c src/demo.c src/ps1.c src/params.c \
    src/project.c src/ui.c src/font.c src/main.c \
    -L"$LIBDIR" -Wl,-rpath-link,"$LIBDIR" -Wl,--allow-shlib-undefined -lSDL2 -lm
aarch64-linux-gnu-strip "$BUILD/ardkore" 2>/dev/null || true

# Refuse to ship a binary that needs more than the aarch64 glibc baseline.
NEWEST=$(aarch64-linux-gnu-objdump -T "$BUILD/ardkore" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "== newest glibc symbol: $NEWEST"
if [ "$NEWEST" != "GLIBC_2.17" ]; then
    echo "binary needs $NEWEST; pin it in tools/glibc-compat.h" >&2
    exit 1
fi
if aarch64-linux-gnu-objdump -T "$BUILD/ardkore" | grep -q GLIBC_PRIVATE; then
    echo "binary imports GLIBC_PRIVATE symbols (static libc/libm got linked in)" >&2
    exit 1
fi

echo "== packaging"
PKG=$BUILD/pkg
rm -rf "$PKG" "$BUILD/ARDKORE-handheld.zip"
mkdir -p "$PKG/ardkore/samples"
cp port/ARDKORE.sh "$PKG/"
cp "$BUILD/ardkore" "$PKG/ardkore/"
cp samples/README.txt "$PKG/ardkore/samples/"
cp port/controls.txt.example port/INSTALL.txt "$PKG/ardkore/"
cp port/INSTALL.txt "$PKG/"
if command -v zip >/dev/null; then
    (cd "$PKG" && zip -qr ../ARDKORE-handheld.zip .)
else
    python3 - "$PKG" "$BUILD/ARDKORE-handheld.zip" <<'EOF'
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(root):
        for f in files:
            path = os.path.join(d, f)
            info = zipfile.ZipInfo(os.path.relpath(path, root))
            info.external_attr = (0o755 if f in ("ardkore", "ARDKORE.sh") else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, open(path, "rb").read())
EOF
fi
echo "== built $BUILD/ARDKORE-handheld.zip"
