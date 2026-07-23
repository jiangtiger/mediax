#!/bin/bash
# 将 MPEG2 视频软解 overlay 应用到 media 子模块。
# 在 CI / 本地构建前执行，会：
#   - 给 build_ffmpeg.sh 启用 swscale
#   - 给 FfmpegLibrary.java 添加 mpeg2video 映射
#   - 给 build.sh 的 MODERN_EXTRA 添加 mpeg2video
#   - 复制视频解码 JNI / Java 文件到 media 子模块
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MEDIA="${ROOT}/media"
JNI_DIR="${MEDIA}/libraries/decoder_ffmpeg/src/main/jni"
JAVA_DIR="${MEDIA}/libraries/decoder_ffmpeg/src/main/java/androidx/media3/decoder/ffmpeg"
OVERLAY_ROOT="${ROOT}/overlays/video/files"

echo "==> 应用 MPEG2 视频软解 overlay"

# 1) 启用 swscale（将 --disable-swscale 替换为 --enable-swscale）
BUILD_FFMPEG="${JNI_DIR}/build_ffmpeg.sh"
echo "    修补 build_ffmpeg.sh: 启用 swscale"
python3 - "$BUILD_FFMPEG" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r') as f:
    content = f.read()
if '--disable-swscale' not in content:
    print('WARNING: --disable-swscale 未找到，可能已修改', file=sys.stderr)
else:
    content = content.replace('    --disable-swscale', '    --enable-swscale')
    with open(path, 'w') as f:
        f.write(content)
    print('    已将 --disable-swscale 替换为 --enable-swscale')
PY

# 2) 给 FfmpegLibrary.java 添加 mpeg2video 映射
FFMPEG_LIB="${JAVA_DIR}/FfmpegLibrary.java"
echo "    修补 FfmpegLibrary.java: 添加 MPEG2 video MIME 映射"
python3 - "$FFMPEG_LIB" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r') as f:
    content = f.read()

# 插入 MimeTypes.VIDEO_MPEG2 映射（在 case MimeTypes.VIDEO_H265 之后，default 之前）
old_h265 = '''      case MimeTypes.VIDEO_H265:
        return "hevc";
      default:'''
if old_h265 not in content:
    print('WARNING: H265 case block 未找到，跳过', file=sys.stderr)
    sys.exit(1)

new_mpeg2 = '''      case MimeTypes.VIDEO_H265:
        return "hevc";
      case MimeTypes.VIDEO_MPEG2:
        return "mpeg2video";
      case "video/mpeg2":
        return "mpeg2video";
      default:'''
content = content.replace(old_h265, new_mpeg2)
with open(path, 'w') as f:
    f.write(content)
print('    已添加 MPEG2 video MIME 映射')
PY

# 3) 给 build.sh 的 MODERN_EXTRA 添加 mpeg2video
BUILD_SH="${ROOT}/build.sh"
echo "    修补 build.sh: 添加 mpeg2video 到 MODERN_EXTRA"
python3 - "$BUILD_SH" <<'PY'
import sys
path = sys.argv[1]
with open(path, 'r') as f:
    content = f.read()

old_line = 'MODERN_EXTRA=(h264 hevc vp9 av1 opus)'
if old_line not in content:
    print('WARNING: MODERN_EXTRA 行未找到，跳过', file=sys.stderr)
    sys.exit(1)

content = content.replace(old_line, 'MODERN_EXTRA=(h264 hevc vp9 av1 opus mpeg2video)')
with open(path, 'w') as f:
    f.write(content)
print('    已添加 mpeg2video 到 MODERN_EXTRA')
PY

# 4) 复制 overlay 文件到 media 子模块
echo "    复制视频解码 overlay 文件..."

cp "${OVERLAY_ROOT}/libraries/decoder_ffmpeg/src/main/jni/ffmpeg_jni.cc" \
   "${JNI_DIR}/ffmpeg_jni.cc"
echo "      -> ffmpeg_jni.cc"

cp "${OVERLAY_ROOT}/libraries/decoder_ffmpeg/src/main/jni/CMakeLists.txt" \
   "${JNI_DIR}/CMakeLists.txt"
echo "      -> CMakeLists.txt"

cp "${OVERLAY_ROOT}/libraries/decoder_ffmpeg/src/main/java/androidx/media3/decoder/ffmpeg/ExperimentalFfmpegVideoDecoder.java" \
   "${JAVA_DIR}/ExperimentalFfmpegVideoDecoder.java"
echo "      -> ExperimentalFfmpegVideoDecoder.java"

cp "${OVERLAY_ROOT}/libraries/decoder_ffmpeg/src/main/java/androidx/media3/decoder/ffmpeg/ExperimentalFfmpegVideoRenderer.java" \
   "${JAVA_DIR}/ExperimentalFfmpegVideoRenderer.java"
echo "      -> ExperimentalFfmpegVideoRenderer.java"

echo "==> MPEG2 视频软解 overlay 应用完成"
