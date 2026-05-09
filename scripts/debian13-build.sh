#!/usr/bin/env bash
# 在本仓库根目录：初始化子模块、编译 FFmpeg JNI、打包 Jellyfin Media3 FFmpeg 扩展 AAR。
# 前置：ANDROID_HOME、JDK 17、已安装 ndk;26.1.10909125 与 cmake;3.31.1（与 CI 一致）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

NDK_VER="${NDK_VER:-26.1.10909125}"
CMAKE_VER="${CMAKE_VER:-3.31.1}"

: "${ANDROID_HOME:?请设置 ANDROID_HOME 指向 Android SDK 根目录}"

export ANDROID_NDK_PATH="${ANDROID_HOME}/ndk/${NDK_VER}"
if [[ ! -d "$ANDROID_NDK_PATH" ]]; then
  echo "未找到 NDK: $ANDROID_NDK_PATH" >&2
  echo "请安装: \"\$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager\" --install \"ndk;${NDK_VER}\" \"cmake;${CMAKE_VER}\"" >&2
  exit 1
fi

echo "==> git submodule update --init --recursive"
git -C "$ROOT" submodule update --init --recursive

if [[ ! -d "${ROOT}/media/libraries/decoder_ffmpeg" ]]; then
  echo "子模块 media 未就绪: ${ROOT}/media" >&2
  exit 1
fi
if [[ ! -f "${ROOT}/ffmpeg/configure" ]]; then
  echo "子模块 ffmpeg 未就绪: ${ROOT}/ffmpeg" >&2
  exit 1
fi

# 空格分隔的 FFmpeg decoder 名，传给 androidx build_ffmpeg.sh 的 --enable-decoder=
# 默认加入常见视频软解；仅需 Jellyfin 原版音频集合时可置空: EXTRA_FFMPEG_DECODERS=
export EXTRA_FFMPEG_DECODERS="${EXTRA_FFMPEG_DECODERS:-h264 hevc vp9}"

echo "==> 编译 FFmpeg（EXTRA_FFMPEG_DECODERS=${EXTRA_FFMPEG_DECODERS}）"
"$ROOT/build.sh"

echo "==> Gradle :media3-ffmpeg-decoder:publishToMavenLocal"
chmod +x "$ROOT/gradlew" 2>/dev/null || true
"$ROOT/gradlew" :media3-ffmpeg-decoder:publishToMavenLocal

echo "==> 完成。产物示例路径（版本号见 gradle 属性 jellyfin.version）:"
echo "    ~/.m2/repository/org/jellyfin/media3/media3-ffmpeg-decoder/"
echo "可选：仅打 decoder AAR（不落 Maven 本地）:"
echo "    ./gradlew :androidx-media-lib-decoder-ffmpeg:bundleReleaseAar"
