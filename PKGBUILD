pkgname=sb-git
pkgdesc="Sandbox Applications"
pkgver=r88.c781e86
pkgrel=1

source=("git+https://github.com/kkernick/sb.git")
sha256sums=("SKIP")
depends=(python findutils glibc which xdg-dbus-proxy bubblewrap strace zypak)
arch=("any")
provides=("sb")

pkgver() {
  cd $srcdir/sb
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

package() {
  cd $srcdir/sb
  for binary in sb sb-startup sb-refresh sb-cache; do
    install -Dm755 "$binary" "$pkgdir/usr/bin/$binary"
  done
  for service in sb.service; do
    install -Dm644 "$service" "$pkgdir/usr/lib/systemd/user/$service"
  done
}
