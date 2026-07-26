# Maintainer: BlaMacfly
pkgname=pushly
pkgver=1.0.1
pkgrel=3
pkgdesc="Key spammer léger avec intervalle humanisé (portage Linux GTK3/XTest)"
arch=('x86_64')
url="https://github.com/BlaMacfly/Pushly"
license=('MIT')
depends=('gtk3' 'libxtst' 'gstreamer' 'gst-plugins-good' 'gst-plugins-base')
makedepends=('git' 'pkgconf')
install=pushly.install
source=("git+https://github.com/BlaMacfly/Pushly.git#branch=linux")
sha256sums=('SKIP')

build() {
  cd "Pushly"
  make
}

package() {
  cd "Pushly"
  make DESTDIR="$pkgdir" PREFIX=/usr install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
