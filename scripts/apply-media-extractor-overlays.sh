#!/bin/bash
# 将 mediax 解复用层 overlay（杜比视界 TS + Enhanced FLV）应用到 media 子模块。
# 需在 CI / 本地发布 media3-extractor 前执行。
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MEDIA="${ROOT}/media"
PATCH="${ROOT}/overlays/dolbyvision/pr3280.patch"
FLV_SRC="${ROOT}/overlays/enhanced-flv/flv"
FLV_DST="${MEDIA}/libraries/extractor/src/main/java/androidx/media3/extractor/flv"
NAL_DST="${MEDIA}/libraries/container/src/main/java/androidx/media3/container/NalUnitUtil.java"

if [[ ! -d "${MEDIA}/libraries/extractor" ]]; then
  echo "未找到 media 子模块，请先: git submodule update --init --recursive"
  exit 1
fi

echo "==> 应用杜比视界 patch (androidx/media#3280)"
cd "${MEDIA}"
if git apply --check "${PATCH}" 2>/dev/null; then
  git apply "${PATCH}"
  echo "    pr3280.patch 已应用"
elif grep -q "H265_NAL_UNIT_TYPE_DV_RPU" "${NAL_DST}" 2>/dev/null; then
  echo "    杜比视界 patch 已存在，跳过"
else
  echo "    警告: patch 无法应用，请检查 media 子模块版本是否与 Media3 1.8.x 对齐"
  exit 1
fi

echo "==> 覆盖 Enhanced FLV (HEVC codec type 12)"
mkdir -p "${FLV_DST}"
cp -f "${FLV_SRC}/"*.java "${FLV_DST}/"
echo "    已复制 $(ls -1 "${FLV_SRC}"/*.java | wc -l | tr -d ' ') 个文件到 extractor/flv"

echo "media extractor overlays 完成"
