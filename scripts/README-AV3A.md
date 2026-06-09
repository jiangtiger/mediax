# AV3A（Audio Vivid / AVS3-P3）扩展构建说明

VsTV 在应用侧通过 `VstvExtractorsFactory` 识别 MPEG-TS 中 stream type `0xD5` 的 AV3A 轨，并输出 `audio/av3a` 样本；**解码**依赖本仓库发布的 `media3-ffmpeg-decoder` 内含 `libarcdav3a` 及伴生原生库。

## 1. 与默认构建的差异

| 项 | 默认 `build.sh` | `ENABLE_AV3A=1 ./build.sh` |
|----|-----------------|----------------------------|
| FFmpeg 源码 | 官方 `release/6.0` 子模块 | 补丁版（含 `libarcdav3a`，CI 由 `fetch-av3a-ffmpeg.sh` 拉取） |
| ARM 链接 | 静态 `.a` → 单一 `libffmpegJNI.so` | 共享 `.so` + `libav3a_binaural_render.so`、`libAVS3AudioDec.so` |
| x86/x86_64 | 静态，无 AV3A | 同上（模拟器无 AV3A 出声，ARM 真机可解码） |
| 额外解码器 | 无 | `libarcdav3a` |
| JNI CMake | 上游静态 | `overlays/av3a/jni/CMakeLists.txt` |

## 2. CI 发版（推荐）

推送含 `av3a` 的标签（如 `1.8.2-av3a2`）时 workflow 自动：

1. `scripts/fetch-av3a-ffmpeg.sh` — 从 [nilaoda/Sourcecodeforplayer](https://github.com/nilaoda/Sourcecodeforplayer) 稀疏检出 `ffmpeg-6.1` + `dependency/android`
2. `ENABLE_AV3A=1` + `apply-av3a-overlays.sh`
3. 构建并发布 `org.jellyfin.media3:media3-ffmpeg-decoder:<标签名>`

无需再手动设置仓库变量 `MEDIAX_ENABLE_AV3A`（非 av3a 标签仍可用该变量做试验构建）。

## 3. 本地 / 自定义 FFmpeg 源

```bash
export ENABLE_AV3A=1
# 可选：export AV3A_FFMPEG_SOURCE=... AV3A_FFMPEG_REF=...
bash scripts/fetch-av3a-ffmpeg.sh   # 或自行将补丁 FFmpeg 放到 ffmpeg/
bash scripts/apply-av3a-overlays.sh
bash ./build.sh
```

官方 `FFmpeg/FFmpeg` **不含** `libarcdav3a`，不可直接用于 AV3A 构建。

## 4. VsTV 接入

在 `libs.versions.toml` 将 `jellyfinMedia3Ffmpeg` 指向已发布的 av3a 标签（如 `1.8.2-av3a2`）。

## 5. 许可说明

`libarcdav3a` / AVS3 音频解码实现可能受专有或受限许可约束；分发 APK 前请自行评估合规性。
