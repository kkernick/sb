pkgname=sb-git
pkgdesc="Sandbox Applications"
pkgver=r4.a3236c3
pkgrel=1

source=("git+https://github.com/kkernick/sb.git")
sha256sums=("SKIP")
depends=(python findutils glibc mesa-utils which xdg-dbus-proxy bubblewrap)
arch=("any")
provides=("sb")

pkgver() {
	cd $srcdir/sb
	printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

package() {
	cd $srcdir/sb
	for binary in sb; do
		install -Dm755 "$binary" "$pkgdir/usr/bin/$binary"
	done
}
