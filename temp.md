# mediax 开发交接：armeabi-v7a AV3A 32 位 ARM 有画无声修复

> **用途**：供 mediax Cloud Agent 读取并在 `vesaaa/mediax` 仓库重新实现、提交、发版。  
> **注意**：本文档仅作交接说明，**不要**把 mediax 源码补丁放进 vstv 仓。

---

## 一、背景与问题

**设备**：X96_X10Pro，`armeabi-v7a`，Android 11 API 30，安装 vstv `arm-av3a` 包。

**现象**：播放 `cctv16-av3a` 时 AV3A 音轨识别正常，但 `FfmpegAudioDecoder` 报：

```text
FfmpegDecoderException: Failed to load decoder native libraries
```

随后走 video-only 回退 → **有画无声**。

**linker 实测**（AAR 内 `armeabi-v7a` 库）：

| 库 | 问题 |
|---|---|
| `libAVS3AudioDec.so` | 缺少可用 GNU_HASH |
| `libav3a_binaural_render.so` | `unsupported DT_RELASZ` |

**结论**：第三方 **armeabi-v7a 预编译 AV3A 库** 与 Android **32 位 linker** 不兼容（不是 API 32，是 **32 位 ARM ABI**）。`libavcodec` 等普通 FFmpeg 库正常。

---

## 二、修复策略

| ABI | 改前 | 改后 |
|-----|------|------|
| **arm64-v8a** | 共享 FFmpeg + AV3A 预编译库 + `libarcdav3a` | **不变** |
| **armeabi-v7a** | 同上（会 dlopen 失败） | **静态 FFmpeg**，跳过 `libarcdav3a` / `av1`，**不打包** AV3A 伴生 `.so` |
| **x86 / x86_64** | 静态 FFmpeg | **不变** |

**产品语义**：

- 32 位 ARM：普通频道有声；AV3A 频道在 **vstv 侧** Toast 提示「32 位 ARM 暂不支持菁彩声」（vstv PR #9 已做探测，等本包发布）。
- 64 位 ARM：AV3A 正常软解。

---

## 三、需改动的文件（共 4 个）

```
overlays/av3a/jni/build_ffmpeg.sh          # 修改
overlays/av3a/jni/CMakeLists.txt           # 修改
.github/workflows/extension-build.yaml     # 修改
scripts/validate-av3a-arm32-elf.sh         # 新建
```

**基准**：`main` 上已有 `db6d8eb`（跳过 ARM av1 解码器）。在其基础上实现下列改动。

---

## 四、逐文件说明

### 4.1 `overlays/av3a/jni/build_ffmpeg.sh`

**文件头注释**改为：

```bash
# AV3A 构建：arm64-v8a 使用共享库 + libarcdav3a；armeabi-v7a / x86 为静态 FFmpeg（无 AV3A 预编译库）。
```

**在 `X86_SKIP_DECODERS` 后新增**：

```bash
# x86 / armeabi-v7a 无 AV3A 软解；补丁 FFmpeg 6.1 在 --disable-asm 下编 av1 会缺 libavutil 符号（ff_av1_framerate）
X86_SKIP_DECODERS=(libarcdav3a av1 vp9)
ARMV7_SKIP_DECODERS=(libarcdav3a av1)
```

**删除**原先对 armeabi-v7a 的 `build_arm_av3a` 调用，**改为**新增函数 `build_armv7_static`（逻辑同 `build_x86_static`，用 `ARMV7_SKIP_DECODERS` 过滤）：

```bash
# armeabi-v7a：第三方预编译 AVS3 库与 Android 32 位 linker 不兼容（DT_RELASZ / 缺 GNU_HASH），
# 故 32 位 ARM 走静态 FFmpeg（无 libarcdav3a），菁彩声仅 arm64-v8a 专包路径提供。
build_armv7_static() {
  local -a extra_configure=("$@")
  local options="${X86_STATIC_OPTIONS}"
  for decoder in "${ENABLED_DECODERS[@]}"; do
    local skip=0
    for x in "${ARMV7_SKIP_DECODERS[@]}"; do
      [[ "${decoder}" == "${x}" ]] && skip=1 && break
    done
    [[ "${skip}" -eq 1 ]] && continue
    options="${options} --enable-decoder=${decoder}"
  done
  cd "${FFMPEG_SRC}"
  bash ./configure "${extra_configure[@]}" ${options}
  make -j"$JOBS"
  make install-libs
  make clean
}

build_armv7_static \
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
```

**保留** `build_arm_av3a arm64-v8a ...` 及两个 `build_x86_static` 调用不变。

---

### 4.2 `overlays/av3a/jni/CMakeLists.txt`

**注释**改为：

```cmake
# AV3A 版 CMake：arm64-v8a 链接共享 FFmpeg + AVS3 伴生库；armeabi-v7a / x86 保持上游静态链接。
```

**条件分支**从：

```cmake
if(ANDROID_ABI MATCHES "^(armeabi-v7a|arm64-v8a)$")
```

改为：

```cmake
# 仅 arm64-v8a 链接 AV3A 共享库；armeabi-v7a 走下方静态 FFmpeg 分支（无菁彩声原生解码）。
if(ANDROID_ABI STREQUAL "arm64-v8a")
```

`else()` 静态链接分支无需改；`armeabi-v7a` 自动走静态 `.a` 路径。

---

### 4.3 `scripts/validate-av3a-arm32-elf.sh`（新建，可执行）

```bash
#!/usr/bin/env bash
# 校验 arm-av3a 工件中 armeabi-v7a 的 AV3A 伴生库是否可被 Android 32 位 linker 加载。
# 预编译库若含 DT_RELASZ 或缺少 GNU_HASH，会在真机 dlopen 失败（有画无声）。
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <path-to-aar-or-apk> [path-to-aar-or-apk...]" >&2
  exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

check_arm32_av3a_libs() {
  local archive="$1"
  local name
  name="$(basename "$archive")"
  if [[ ! -f "$archive" ]]; then
    echo "ERROR: missing archive: $archive" >&2
    return 1
  fi

  local found=0
  while IFS= read -r entry; do
    [[ -z "$entry" ]] && continue
    found=1
    local so_path="${TMP}/${name}/${entry}"
    mkdir -p "$(dirname "$so_path")"
    unzip -p "$archive" "$entry" > "$so_path"

    if readelf -d "$so_path" 2>/dev/null | grep -q '(RELA)'; then
      echo "ERROR: ${name}:${entry} has RELA relocations (unsupported on armeabi-v7a Android linker)" >&2
      return 1
    fi
    if ! readelf -d "$so_path" 2>/dev/null | grep -qE 'GNU_HASH|\(HASH\)'; then
      echo "ERROR: ${name}:${entry} missing DT_GNU_HASH/DT_HASH" >&2
      return 1
    fi
    echo "OK: ${name}:${entry}"
  done < <(unzip -Z1 "$archive" | grep -E '^lib/armeabi-v7a/(libAVS3AudioDec|libav3a_binaural_render)\.so$' || true)

  if [[ "$found" -eq 0 ]]; then
    echo "OK: ${name} has no armeabi-v7a AV3A companion libs (expected for av3a7+ builds)"
  fi
  return 0
}

for archive in "$@"; do
  check_arm32_av3a_libs "$archive"
done
```

**语义**：若 AAR 里**没有**这两个 armeabi-v7a 库 → 通过（av3a7+ 预期行为）；若有且 ELF 不合格 → CI 失败。

---

### 4.4 `.github/workflows/extension-build.yaml`

在 `Build extension (Maven local layout for CI artifact)` 步骤之后、`Publish to GitHub Packages` 之前插入：

```yaml
      - name: Validate armeabi-v7a AV3A companion ELF (av3a tags)
        if: github.event_name == 'push' && startsWith(github.ref, 'refs/tags/') && contains(github.ref, 'av3a')
        run: |
          chmod +x scripts/validate-av3a-arm32-elf.sh
          mapfile -t AARS < <(find ~/.m2/repository/org/jellyfin/media3/media3-ffmpeg-decoder -name '*.aar' 2>/dev/null)
          if [[ "${#AARS[@]}" -eq 0 ]]; then
            echo "::error::未找到 media3-ffmpeg-decoder AAR"
            exit 1
          fi
          bash scripts/validate-av3a-arm32-elf.sh "${AARS[@]}"
```

---

## 五、提交与发版

**建议 commit message**：

```text
fix(av3a): use static FFmpeg on armeabi-v7a, AV3A only on arm64

Third-party armv7-a prebuilts use ELF relocations incompatible with
Android 32-bit linker (DT_RELASZ / missing GNU_HASH), causing
FfmpegAudioDecoder 'Failed to load decoder native libraries' on
armeabi-v7a devices.

- armeabi-v7a: static FFmpeg without libarcdav3a
- arm64-v8a: unchanged shared AV3A path
- CI: validate armv7 AV3A companion ELF in published AAR
```

**分支**：`cursor/armv7-av3a-static-ffmpeg-81ba`（或任意 feature 分支）

**发布标签**：`1.8.2-av3a7`（tag 名含 `av3a` 才会走 AV3A 构建与上述校验）

```bash
git checkout -b cursor/armv7-av3a-static-ffmpeg-81ba
# …改完 4 个文件…
git add overlays/av3a/jni/build_ffmpeg.sh overlays/av3a/jni/CMakeLists.txt \
        scripts/validate-av3a-arm32-elf.sh .github/workflows/extension-build.yaml
git commit -m "fix(av3a): use static FFmpeg on armeabi-v7a, AV3A only on arm64"
git push -u origin cursor/armv7-av3a-static-ffmpeg-81ba
git tag 1.8.2-av3a7
git push origin 1.8.2-av3a7
```

等 GitHub Actions 将 `org.jellyfin.media3:media3-ffmpeg-decoder:1.8.2-av3a7` 发到 GitHub Packages。

---

## 六、验收标准

1. **CI**：打 `1.8.2-av3a7` 标签后 workflow 绿；校验步骤输出  
   `OK: … has no armeabi-v7a AV3A companion libs (expected for av3a7+ builds)`
2. **AAR 内容**：
   - `lib/armeabi-v7a/`：**无** `libAVS3AudioDec.so`、`libav3a_binaural_render.so`
   - `lib/arm64-v8a/`：**仍有** AV3A 共享库
3. **真机**（X96 32 位盒）：普通频道有声；AV3A 频道不再因 native load 失败而无声崩溃循环

---

## 七、与 vstv 的衔接

| 项目 | 状态 |
|------|------|
| vstv PR | https://github.com/vesaaa/vstv-src/pull/9 |
| vstv 依赖版本 | `jellyfinMedia3FfmpegAv3a = "1.8.2-av3a7"`（已写在 PR 里） |
| vstv 发版 | mediax `1.8.2-av3a7` 发包后合并 PR #9 → 打标签 **v2.5.10** |

**不要把 mediax 补丁放进 vstv 仓**；两仓分开提交。本文档 `temp.md` 仅为交接，发版完成后可删除。

---

## 八、给 mediax Agent 的一句话任务

> 在 `vesaaa/mediax` 的 `main` 上实现上述 4 文件改动（armeabi-v7a 改静态 FFmpeg、仅 arm64 保留 AV3A 共享库、加 ELF 校验脚本与 CI 步骤），提交并 push 分支，打标签 **`1.8.2-av3a7`** 触发发包。

---

## 九、参考：完整 diff 摘要

相对 `main` 共 4 文件、+88 / -5 行：

```
 .github/workflows/extension-build.yaml | 11 ++++++++
 overlays/av3a/jni/CMakeLists.txt       |  5 ++--
 overlays/av3a/jni/build_ffmpeg.sh      | 27 ++++++++++++++++--
 scripts/validate-av3a-arm32-elf.sh     | 50 ++++++++++++++++++++++++++
 4 files changed, 88 insertions(+), 5 deletions(-)
```

原参考提交（未 push 到 mediax 远程）：`82b1ba9`
