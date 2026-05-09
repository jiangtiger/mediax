#!/bin/bash

# Ensure NDK is available
export ANDROID_NDK_PATH=$ANDROID_HOME/ndk/26.1.10909125

[[ ! -d "$ANDROID_NDK_PATH" ]] && echo "No NDK found, quitting…" && exit 1

# Setup environment
export ANDROIDX_MEDIA_ROOT="${PWD}/media"
export FFMPEG_MOD_PATH="${ANDROIDX_MEDIA_ROOT}/libraries/decoder_ffmpeg/src/main"
export FFMPEG_PATH="${PWD}/ffmpeg"

DEFAULT_DECODERS=(flac alac pcm_mulaw pcm_alaw mp3 aac ac3 eac3 dca mlp truehd)
ENABLED_DECODERS=("${DEFAULT_DECODERS[@]}")
MODERN_EXTRA=(h264 hevc vp9 av1 opus)
# 未设置 EXTRA_FFMPEG_DECODERS：附加互联网/IPTV常见软解。已设置且为空串：仅保留上方默认音频（与上游 Jellyfin 一致）。
if [ -z "${EXTRA_FFMPEG_DECODERS+x}" ]; then
  ENABLED_DECODERS+=("${MODERN_EXTRA[@]}")
elif [ -n "${EXTRA_FFMPEG_DECODERS}" ]; then
  read -r -a EXTRA <<< "${EXTRA_FFMPEG_DECODERS}"
  ENABLED_DECODERS+=("${EXTRA[@]}")
fi

# Create softlink to ffmpeg
ln -sf "${FFMPEG_PATH}" "${FFMPEG_MOD_PATH}/jni/ffmpeg"

# Start build
cd "${FFMPEG_MOD_PATH}/jni"
./build_ffmpeg.sh "${FFMPEG_MOD_PATH}" "${ANDROID_NDK_PATH}" "linux-x86_64" 21 "${ENABLED_DECODERS[@]}"
