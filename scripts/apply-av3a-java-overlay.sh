#!/bin/bash
# 仅补丁 FfmpegLibrary.java：audio/av3a → libarcdav3a（不替换 JNI 构建脚本）。
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG_LIB="${ROOT}/media/libraries/decoder_ffmpeg/src/main/java/androidx/media3/decoder/ffmpeg/FfmpegLibrary.java"

if [[ ! -f "${FFMPEG_LIB}" ]]; then
  echo "未找到 ${FFMPEG_LIB}，请先 checkout 子模块 media"
  exit 1
fi

if grep -q 'audio/av3a' "${FFMPEG_LIB}"; then
  echo "FfmpegLibrary 已含 audio/av3a 映射，跳过。"
  exit 0
fi

python3 - "${FFMPEG_LIB}" <<'PY'
import sys
from pathlib import Path

p = Path(sys.argv[1])
txt = p.read_text(encoding="utf-8")
marker = "      default:\n        return null;"
patch = "      case \"audio/av3a\":\n        return \"libarcdav3a\";\n      default:\n        return null;"
if marker not in txt:
    raise SystemExit(f"FfmpegLibrary.java 结构未知，请手动合并: {p}")
p.write_text(txt.replace(marker, patch, 1), encoding="utf-8")
print("patched", p)
PY
