pkgname=sb-git
pkgdesc="Sandbox Applications"
pkgver=r127.a0f1dd6
pkgrel=1

source=("git+https://github.com/kkernick/sb.git")
sha256sums=("SKIP")
depends=(python findutils glibc which xdg-dbus-proxy bubblewrap)
makedepends=(zip)
optdepends=(
'strace: for --strace, useful for debugging'
'zypak: for --zypak, required for Chromium/Electron'
'hardened_malloc: for --hardened-malloc'
)
arch=("any")
provides=("sb")

pkgver() {
  cd $srcdir/sb
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

build() {
  cd $srcdir/sb
  make
}

package() {
  cd $srcdir/sb
  for binary in sb sb-startup sb-refresh; do
    install -Dm755 "$binary" "$pkgdir/usr/bin/$binary"
  done
  for service in sb.service; do
    install -Dm644 "$service" "$pkgdir/usr/lib/systemd/user/$service"
  done
}
