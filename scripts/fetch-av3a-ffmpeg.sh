#!/bin/bash
# 将 ffmpeg 子模块目录替换为含 libarcdav3a 的补丁版（默认 nilaoda/Sourcecodeforplayer）。
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG_DIR="${ROOT}/ffmpeg"
SOURCE="${AV3A_FFMPEG_SOURCE:-https://github.com/nilaoda/Sourcecodeforplayer.git}"
REF="${AV3A_FFMPEG_REF:-master}"

TMP="$(mktemp -d)"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

echo "Fetching patched FFmpeg from ${SOURCE} (ref=${REF}) ..."
git clone --depth 1 --filter=blob:none --sparse "${SOURCE}" "${TMP}/src"
(
  cd "${TMP}/src"
  git sparse-checkout init --cone
  git sparse-checkout set ffmpeg/ffmpeg-6.1 ffmpeg/dependency
  if ! git checkout "${REF}" 2>/dev/null; then
    echo "ref ${REF} checkout failed, using default branch"
  fi
)

if [[ ! -d "${TMP}/src/ffmpeg/ffmpeg-6.1" ]]; then
  echo "missing ffmpeg/ffmpeg-6.1 in ${SOURCE}"
  exit 1
fi

rm -rf "${FFMPEG_DIR:?}"/*
mkdir -p "${FFMPEG_DIR}"
cp -a "${TMP}/src/ffmpeg/ffmpeg-6.1/." "${FFMPEG_DIR}/"
cp -a "${TMP}/src/ffmpeg/dependency" "${FFMPEG_DIR}/dependency"

test -f "${FFMPEG_DIR}/configure"
test -f "${FFMPEG_DIR}/libavcodec/libarcdav3a.c"
test -d "${FFMPEG_DIR}/dependency/android/armv8-a"
echo "Patched FFmpeg ready at ${FFMPEG_DIR}"
