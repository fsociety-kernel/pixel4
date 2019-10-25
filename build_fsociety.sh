#!/bin/bash

echo
echo "Clean Build Directory"
echo

make clean && make mrproper

echo
echo "Issue Build Commands"
echo

mkdir -p out
export ARCH=arm64
export SUBARCH=arm64
export CLANG_PATH=~/linux-x86/clang-r370808/bin
export PATH=${CLANG_PATH}:${PATH}
export CLANG_TRIPLE=aarch64-linux-gnu-
export CROSS_COMPILE=~/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export CROSS_COMPILE_ARM32=~/arm-linux-androideabi-4.9/bin/arm-linux-androideabi-
export LD_LIBRARY_PATH=~/linux-x86/clang-r370808/lib:$LD_LIBRARY_PATH

echo
echo "Set DEFCONFIG"
echo
make CC=clang AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip O=out fsociety_defconfig

echo
echo "Build The Good Stuff"
echo

make CC=clang AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip O=out -j16
