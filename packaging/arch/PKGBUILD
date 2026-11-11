# Maintainer: Mem Reduct for Linux contributors
pkgname=memreduct
pkgver=1.3.0
pkgrel=1
pkgdesc="Linux memory monitor with cache cleaning, PSI, ZRAM and cgroup metrics"
arch=('x86_64' 'aarch64' 'armv7h' 'i686')
url="https://github.com/flessan/memreduct-linux"
license=('GPL-3.0-or-later')
depends=('glibc')
makedepends=('git')
checkdepends=('python')
optdepends=(
  'libnotify: send automatic desktop notifications'
  'zram-generator: configure compressed-RAM swap devices'
)
backup=('etc/memreduct.conf')
source=("memreduct::git+https://github.com/flessan/memreduct-linux.git#commit=dcdd2e549cfdb7b2146443efb384793f404cba8a")
sha256sums=('SKIP')

build() {
  cd "$srcdir/memreduct"
  make clean
  make CC="${CC:-cc}" \
    CFLAGS="$CFLAGS -std=c99 -Wall -Wextra -Wpedantic" \
    LDFLAGS="$LDFLAGS"
}

check() {
  cd "$srcdir/memreduct"
  make test
}

package() {
  cd "$srcdir/memreduct"
  make DESTDIR="$pkgdir" PREFIX=/usr install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
