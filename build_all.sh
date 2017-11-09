#!/bin/sh -e

MAKE(){
  make ARCH=arm "CROSS_COMPILE=$CROSS_COMPILE" ${1+"$@"} 1>/dev/null
}

CROSS_COMPILE=arm-linux-gnueabihf-
KDIR="$(pwd)"
KVERS="3.4.112"
instdir="$KDIR/modules-hmod"

MAKE mrproper
MAKE sun_nontendocm_defconfig
#make ARCH=arm "CROSS_COMPILE=$CROSS_COMPILE" xconfig
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
find "$instdir" -type l -delete

mv "$instdir/lib/modules/$KVERS+" "$instdir/lib/modules/$KVERS" || true
mkdir "$instdir/lib/modules/$KVERS/extra"
cp -f "modules/mali/mali.ko" "$instdir/lib/modules/$KVERS/extra/"
cp -f "clovercon/clovercon.ko" "$instdir/lib/modules/$KVERS/extra/"

find "$instdir" -type f -name "*.ko" -print0 | xargs -0 -n1 "${CROSS_COMPILE}strip" --strip-unneeded
makepack "$instdir"
mv "$instdir.hmod.tgz" "madmonkey-modules-$KVERS.hmod"
