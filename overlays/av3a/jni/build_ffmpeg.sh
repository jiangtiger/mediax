#!/bin/bash
#
# AV3A 构建：ARM 使用共享库 + libarcdav3a；x86 回退为上游静态构建（无 AV3A 预编译库）。
#
set -eu

FFMPEG_MODULE_PATH="$1"
echo "FFMPEG_MODULE_PATH is ${FFMPEG_MODULE_PATH}"
NDK_PATH="$2"
echo "NDK path is ${NDK_PATH}"
HOST_PLATFORM="$3"
echo "Host platform is ${HOST_PLATFORM}"
ANDROID_ABI="$4"
echo "ANDROID_ABI is ${ANDROID_ABI}"
ENABLED_DECODERS=("${@:5}")
echo "Enabled decoders are ${ENABLED_DECODERS[@]}"
JOBS="$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)"
echo "Using $JOBS jobs for make"

FFMPEG_SRC="${FFMPEG_MODULE_PATH}/jni/ffmpeg"
DEP_ROOT="${FFMPEG_SRC}/dependency/android"

copy_av3a_prebuilt() {
  local out_abi="$1"
  local dep_cpu="$2"
  local out_dir="${FFMPEG_SRC}/android-libs/${out_abi}"
  local dep_dir="${DEP_ROOT}/${dep_cpu}"
  for lib in libav3a_binaural_render.so libAVS3AudioDec.so; do
    if [[ ! -f "${dep_dir}/${lib}" ]]; then
      echo "missing ${dep_dir}/${lib}"
      exit 1
    fi
    cp -f "${dep_dir}/${lib}" "${out_dir}/"
  done
}

ARM_AV3A_OPTIONS="
 --target-os=android
 --disable-static
 --enable-shared
 --disable-doc
 --disable-programs
 --disable-everything
 --disable-avdevice
 --enable-avformat
 --enable-swscale
 --disable-postproc
 --disable-avfilter
 --disable-symver
 --enable-swresample
 --extra-ldexeflags=-pie
 --disable-v4l2-m2m
 --disable-vulkan
 --enable-libarcdav3a
 --enable-decoders
 --enable-decoder=libarcdav3a
 --disable-decoder=av1
 "

X86_STATIC_OPTIONS="
 --target-os=android
 --enable-static
 --disable-shared
 --disable-doc
 --disable-programs
 --disable-everything
 --disable-avdevice
 --disable-avformat
 --disable-swscale
 --disable-postproc
 --disable-avfilter
 --disable-symver
 --enable-swresample
 --extra-ldexeflags=-pie
 --disable-v4l2-m2m
 --disable-vulkan
 "

TOOLCHAIN_PREFIX="${NDK_PATH}/toolchains/llvm/prebuilt/${HOST_PLATFORM}/bin"
if [[ ! -d "${TOOLCHAIN_PREFIX}" ]]; then
  echo "Please set correct NDK_PATH, $NDK_PATH is incorrect"
  exit 1
fi

ARMV7_CLANG="${TOOLCHAIN_PREFIX}/armv7a-linux-androideabi${ANDROID_ABI}-clang"
if [[ ! -e "$ARMV7_CLANG" ]]; then
  echo "ARMv7 Clang compiler with path $ARMV7_CLANG does not exist"
  exit 1
fi
ANDROID_ABI_64BIT="$ANDROID_ABI"
if [[ "$ANDROID_ABI_64BIT" -lt 21 ]]; then
  ANDROID_ABI_64BIT=21
fi

build_arm_av3a() {
  local out_abi="$1"
  local dep_cpu="$2"
  shift 2
  local -a extra_configure=("$@")
  local options="${ARM_AV3A_OPTIONS}"
  for decoder in "${ENABLED_DECODERS[@]}"; do
    options="${options} --enable-decoder=${decoder}"
  done
  cd "${FFMPEG_SRC}"
  ./configure "${extra_configure[@]}" ${options}
  make -j"$JOBS"
  make install-libs
  copy_av3a_prebuilt "${out_abi}" "${dep_cpu}"
  make clean
}

build_x86_static() {
  local -a extra_configure=("$@")
  local options="${X86_STATIC_OPTIONS}"
  for decoder in "${ENABLED_DECODERS[@]}"; do
    [[ "${decoder}" == "libarcdav3a" ]] && continue
    options="${options} --enable-decoder=${decoder}"
  done
  cd "${FFMPEG_SRC}"
  ./configure "${extra_configure[@]}" ${options}
  make -j"$JOBS"
  make install-libs
  make clean
}

build_arm_av3a armeabi-v7a armv7-a \
  --libdir=android-libs/armeabi-v7a \
  --arch=arm \
  --cpu=armv7-a \
  --cross-prefix="${TOOLCHAIN_PREFIX}/armv7a-linux-androideabi${ANDROID_ABI}-" \
  --nm="${TOOLCHAIN_PREFIX}/llvm-nm" \
  --ar="${TOOLCHAIN_PREFIX}/llvm-ar" \
  --ranlib="${TOOLCHAIN_PREFIX}/llvm-ranlib" \
  --strip="${TOOLCHAIN_PREFIX}/llvm-strip" \
  --extra-cflags="-march=armv7-a -mfloat-abi=softfp" \
  --extra-ldflags="-Wl,--fix-cortex-a8"

build_arm_av3a arm64-v8a armv8-a \
  --libdir=android-libs/arm64-v8a \
  --arch=aarch64 \
  --cpu=armv8-a \
  --cross-prefix="${TOOLCHAIN_PREFIX}/aarch64-linux-android${ANDROID_ABI_64BIT}-" \
  --nm="${TOOLCHAIN_PREFIX}/llvm-nm" \
  --ar="${TOOLCHAIN_PREFIX}/llvm-ar" \
  --ranlib="${TOOLCHAIN_PREFIX}/llvm-ranlib" \
  --strip="${TOOLCHAIN_PREFIX}/llvm-strip"

build_x86_static \
  --libdir=android-libs/x86 \
  --arch=x86 \
  --cpu=i686 \
  --cross-prefix="${TOOLCHAIN_PREFIX}/i686-linux-android${ANDROID_ABI}-" \
  --nm="${TOOLCHAIN_PREFIX}/llvm-nm" \
  --ar="${TOOLCHAIN_PREFIX}/llvm-ar" \
  --ranlib="${TOOLCHAIN_PREFIX}/llvm-ranlib" \
  --strip="${TOOLCHAIN_PREFIX}/llvm-strip" \
  --disable-asm

build_x86_static \
  --libdir=android-libs/x86_64 \
  --arch=x86_64 \
  --cpu=x86-64 \
  --cross-prefix="${TOOLCHAIN_PREFIX}/x86_64-linux-android${ANDROID_ABI_64BIT}-" \
  --nm="${TOOLCHAIN_PREFIX}/llvm-nm" \
  --ar="${TOOLCHAIN_PREFIX}/llvm-ar" \
  --ranlib="${TOOLCHAIN_PREFIX}/llvm-ranlib" \
  --strip="${TOOLCHAIN_PREFIX}/llvm-strip" \
  --disable-asm
