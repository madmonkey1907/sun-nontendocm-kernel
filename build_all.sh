#!/bin/sh -e

MAKE(){
  make ARCH=arm "CROSS_COMPILE=$CROSS_COMPILE" CFLAGS_MODULE=-fno-pic ${1+"$@"} 1>/dev/null
}

CROSS_COMPILE=arm-linux-gnueabihf-
KDIR="$(pwd)"
EXTRAVERSION=".$(git log --oneline | wc -l)-madmonkey"
sed -i "s#EXTRAVERSION =.*#EXTRAVERSION = $EXTRAVERSION#" "$KDIR/Makefile"
eval "$(head -n4 "$KDIR/Makefile" | sed 's#\s*=\s*#=#')"
KVERS="$VERSION.$PATCHLEVEL.$SUBLEVEL$EXTRAVERSION"
instdir="$KDIR/modules-hmod"

MAKE mrproper
MAKE sun_nontendocm_defconfig
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$(QT_SELECT=4 qmake -query QT_INSTALL_LIBS)/pkgconfig"
pkg-config --exists QtCore
make ARCH=arm "CROSS_COMPILE=$CROSS_COMPILE" xconfig || \
make ARCH=arm "CROSS_COMPILE=$CROSS_COMPILE" menuconfig
MAKE savedefconfig
mv -f "defconfig" "arch/arm/configs/sun_nontendocm_defconfig"
git diff "arch/arm/configs/sun_nontendocm_defconfig"
MAKE dep
MAKE zImage
MAKE modules

cd "modules/mali"
MAKE "LICHEE_KDIR=$KDIR" "LICHEE_MOD_DIR=$(pwd)"
cd "$KDIR"

cd "clovercon"
MAKE "KDIR=$KDIR" module
cd "$KDIR"

rm -rf "$instdir"
mkdir "$instdir"

MAKE "INSTALL_MOD_PATH=$instdir" modules_install
#MAKE "INSTALL_MOD_PATH=$instdir" firmware_install
find "$instdir" -type l -delete

mkdir "$instdir/lib/modules/$KVERS/extra"
cp -f "modules/mali/mali.ko" "$instdir/lib/modules/$KVERS/extra/"
cp -f "clovercon/clovercon.ko" "$instdir/lib/modules/$KVERS/extra/"
cp -Rf clovercon/mod/* modules-hmod

echo "return 0" > "$instdir/uninstall"
echo "no-uninstall" >> "$instdir/uninstall"

find "$instdir" -type f -name "*.ko" -print0 | xargs -0 -n1 "${CROSS_COMPILE}strip" --strip-unneeded
cd "$instdir"
tar -czvf ../modules-$KVERS.hmod *
