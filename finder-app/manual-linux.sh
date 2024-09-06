#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi
echo ""

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    echo "Cleaning previous compilation"
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    echo ""
    echo "Prepare kernel config"
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    echo ""
    echo "Complile kernel"
    make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
    echo ""
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi
echo ""

# TODO: Create necessary base directories
echo "Create necessary base directories"
mkdir -p rootfs && cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

cd ${OUTDIR}/rootfs
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
echo ""

# TODO: Add library dependencies to rootfs
echo "Copy Program interpreter from cross compiler directory to rootfs/lib"
cp "$(find "$(dirname $(dirname $(which ${CROSS_COMPILE}gcc)))" -name "ld-linux-aarch64.so.1" 2>/dev/null)" "${OUTDIR}/rootfs/lib/"

echo "Copy Libraries from cross compiler directory to rootfs/lib64"
cp "$(find "$(dirname $(dirname $(which ${CROSS_COMPILE}gcc)))" -name "libm.so.6" 2>/dev/null)" "${OUTDIR}/rootfs/lib64/"
cp "$(find "$(dirname $(dirname $(which ${CROSS_COMPILE}gcc)))" -name "libresolv.so.2" 2>/dev/null)" "${OUTDIR}/rootfs/lib64/"
cp "$(find "$(dirname $(dirname $(which ${CROSS_COMPILE}gcc)))" -name "libc.so.6" 2>/dev/null)" "${OUTDIR}/rootfs/lib64/"
echo ""

# TODO: Make device nodes
echo "Make device nodes"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1
echo ""

# TODO: Clean and build the writer utility
echo "Build writer utility"
cd ${FINDER_APP_DIR}
make "CROSS_COMPILE=${CROSS_COMPILE}" clean
make "CROSS_COMPILE=${CROSS_COMPILE}"
echo ""

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copy the finder related scripts and executables to the rootfs/home directory"
cp writer "${OUTDIR}/rootfs/home/"
cp finder.sh "${OUTDIR}/rootfs/home/"
cp finder-test.sh "${OUTDIR}/rootfs/home/"
cp autorun-qemu.sh "${OUTDIR}/rootfs/home/"
cp autorun-qemu.sh "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp -r conf/* "${OUTDIR}/rootfs/home/conf/"
echo ""

# TODO: Chown the root directory
cd "${OUTDIR}/rootfs"
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
echo "Create initramfs.cpio.gz"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ..
gzip -f initramfs.cpio