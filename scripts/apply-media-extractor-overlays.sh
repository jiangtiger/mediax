#!/bin/bash
# 将 mediax 解复用层 overlay（杜比视界 TS + Enhanced FLV）应用到 media 子模块。
# 需在 CI / 本地发布 media3-extractor 前执行。
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MEDIA="${ROOT}/media"
DV_FILES="${ROOT}/overlays/dolbyvision/files"
FLV_SRC="${ROOT}/overlays/enhanced-flv/flv"
FLV_DST="${MEDIA}/libraries/extractor/src/main/java/androidx/media3/extractor/flv"
NAL_DST="${MEDIA}/libraries/container/src/main/java/androidx/media3/container/NalUnitUtil.java"

if [[ ! -d "${MEDIA}/libraries/extractor" ]]; then
  echo "未找到 media 子模块，请先: git submodule update --init --recursive"
  exit 1
fi

# VSTV app 锁定 Media3 1.8.0；vstv extractor 须在 1.8.0 子模块上 overlay，避免 1.10 API（如 Metadata.getMatchingEntries）运行时崩溃。
# 仅当显式设置 MEDIAX_MEDIA_REF 时才检出（CI 对 +vstv 标签传 1.8.0；本地发布 vstv 请 MEDIAX_MEDIA_REF=1.8.0 bash ...）
if [[ -n "${MEDIAX_MEDIA_REF:-}" ]]; then
  echo "==> 检出 media 子模块 @ ${MEDIAX_MEDIA_REF}"
  git -C "${MEDIA}" fetch --tags origin
  git -C "${MEDIA}" checkout --force "${MEDIAX_MEDIA_REF}"
fi

echo "==> 应用杜比视界 overlay (androidx/media#3280${MEDIAX_MEDIA_REF:+, 基线 Media3 ${MEDIAX_MEDIA_REF}})"
if grep -q "H265_NAL_UNIT_TYPE_DV_RPU" "${NAL_DST}" 2>/dev/null; then
  echo "    杜比视界 overlay 已存在，跳过"
else
  cp -f "${DV_FILES}/libraries/container/src/main/java/androidx/media3/container/NalUnitUtil.java" \
    "${MEDIA}/libraries/container/src/main/java/androidx/media3/container/NalUnitUtil.java"
  cp -f "${DV_FILES}/libraries/extractor/src/main/java/androidx/media3/extractor/ts/"*.java \
    "${MEDIA}/libraries/extractor/src/main/java/androidx/media3/extractor/ts/"
  echo "    已覆盖 5 个 TS/DV 源文件"
fi

echo "==> 覆盖 Enhanced FLV (HEVC codec type 12)"
mkdir -p "${FLV_DST}"
cp -f "${FLV_SRC}/"*.java "${FLV_DST}/"
echo "    已复制 $(ls -1 "${FLV_SRC}"/*.java | wc -l | tr -d ' ') 个文件到 extractor/flv"

echo "media extractor overlays 完成"
