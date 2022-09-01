pkgname=wslbridge2
pkgver=0.9
pkgrel=1
pkgdesc="Bridge WSL with Windows terminal emulators"
arch=('i686' 'x86_64')
license=('GPL3')
makedepends=('gcc' 'make')
url='https://github.com/Biswa96/wslbridge2'
source=(https://github.com/Biswa96/wslbridge2/archive/refs/tags/v${pkgver}.tar.gz
        wslbridge2-backend)
sha256sums=('3614de12350bc9c6c4f3bcceb0d676d5d993c646a924070514224e8f8b39fe67'
            'SKIP')

build() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  make RELEASE=1
}

package() {
  cd "${srcdir}/${pkgname}-${pkgver}"
  mkdir -p ${pkgdir}/usr/bin
  install -m755 bin/wslbridge2.exe ${pkgdir}/usr/bin/wslbridge2.exe
  install -m755 ${srcdir}/wslbridge2-backend ${pkgdir}/usr/bin/wslbridge2-backend
}
