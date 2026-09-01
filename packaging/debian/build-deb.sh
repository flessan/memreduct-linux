#!/bin/sh
# Build a .deb package for Debian / Ubuntu / Mint / Pop!_OS and derivatives.
# Requires: make, a C compiler, dpkg-deb (all present on any Debian system).
#
# Usage: ./packaging/debian/build-deb.sh
# Output: memreduct_<version>_<arch>.deb in the repository root.

set -eu

cd "$(dirname "$0")/../.."

VERSION="$(sed -n 's/^#define APP_VERSION[[:space:]]*"\(.*\)"/\1/p' src/memreduct.c)"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
PKGDIR="$(mktemp -d)"

trap 'rm -rf "$PKGDIR"' EXIT

make clean
make
make DESTDIR="$PKGDIR" PREFIX=/usr install

# Debian expects units in /lib/systemd/system
mkdir -p "$PKGDIR/lib/systemd/system"
mv "$PKGDIR/usr/lib/systemd/system/memreduct.service" "$PKGDIR/lib/systemd/system/"
rmdir -p "$PKGDIR/usr/lib/systemd/system" 2>/dev/null || true

mkdir -p "$PKGDIR/DEBIAN"

cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: memreduct
Version: $VERSION
Section: admin
Priority: optional
Architecture: $ARCH
Depends: libc6
Recommends: libnotify-bin
Maintainer: Mem Reduct for Linux contributors <noreply@example.com>
Homepage: https://github.com/flessan/memreduct-linux
Description: real-time memory monitoring and cleaning
 Lightweight real-time memory management application to monitor and
 clean system memory. Linux port of Mem Reduct: cleans the page cache,
 dentry and inode slab caches, compacts physical memory, and can flush
 swap back to RAM - on demand, at a usage threshold, or on a timer.
EOF

echo "/etc/memreduct.conf" > "$PKGDIR/DEBIAN/conffiles"

cat > "$PKGDIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
	systemctl daemon-reload || true
fi
echo "Enable automatic memory cleaning with: systemctl enable --now memreduct"
EOF

cat > "$PKGDIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ -d /run/systemd/system ]; then
	systemctl stop memreduct 2>/dev/null || true
	systemctl disable memreduct 2>/dev/null || true
fi
EOF

chmod 755 "$PKGDIR/DEBIAN/postinst" "$PKGDIR/DEBIAN/prerm"

# gzip the man page as Debian policy requires
gzip -9nf "$PKGDIR/usr/share/man/man1/memreduct.1"

dpkg-deb --root-owner-group --build "$PKGDIR" "memreduct_${VERSION}_${ARCH}.deb"

echo "Built: memreduct_${VERSION}_${ARCH}.deb"
