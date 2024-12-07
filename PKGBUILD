pkgname=sb-git
pkgdesc="Sandbox Applications"
pkgver=r164.6c51d9f
pkgrel=1

source=("git+https://github.com/kkernick/sb.git")
sha256sums=("SKIP")
depends=(python findutils glibc which bubblewrap)
makedepends=(zip)
optdepends=(
'strace: for --strace, useful for debugging'
'hardened_malloc: for --hardened-malloc'
'zram-generator: for --sof=zram'
'xdg-dbus-proxy: for --portals, --see, --talk, --own for Flatpak Emulation'
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

  install -Dm 644 sb.conf "$pkgdir/usr/lib/systemd/zram-generator.conf.d/sb.conf"
}
