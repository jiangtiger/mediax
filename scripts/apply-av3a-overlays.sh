#!/bin/bash
# ENABLE_AV3A=1 时：Java 映射 + AV3A 版 JNI 构建脚本（需补丁 FFmpeg 子模块）。
set -eu

if [[ "${ENABLE_AV3A:-0}" != "1" ]]; then
  echo "ENABLE_AV3A 未设置，跳过完整 AV3A overlays。"
  exit 0
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JNI_DIR="${ROOT}/media/libraries/decoder_ffmpeg/src/main/jni"

bash "${ROOT}/scripts/apply-av3a-java-overlay.sh"

cp "${ROOT}/overlays/av3a/jni/build_ffmpeg.sh" "${JNI_DIR}/build_ffmpeg.sh"
cp "${ROOT}/overlays/av3a/jni/CMakeLists.txt" "${JNI_DIR}/CMakeLists.txt"
chmod +x "${JNI_DIR}/build_ffmpeg.sh"
echo "已覆盖 AV3A 版 build_ffmpeg.sh / CMakeLists.txt"
