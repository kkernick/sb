pkgname=sb-git
pkgdesc="Sandbox Applications"
pkgver=r30.fb858a2
pkgrel=1

source=("git+https://github.com/kkernick/sb.git")
sha256sums=("SKIP")
depends=(python findutils glibc which xdg-dbus-proxy bubblewrap strace)
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
