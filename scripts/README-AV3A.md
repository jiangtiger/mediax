# AV3A（Audio Vivid / AVS3-P3）扩展构建说明

VsTV 在应用侧通过 `VstvExtractorsFactory` 识别 MPEG-TS 中 stream type `0xD5` 的 AV3A 轨，并输出 `audio/av3a` 样本；**解码**依赖本仓库发布的 `media3-ffmpeg-decoder` 内含 `libarcdav3a` 解码器。

## 1. 与默认构建的差异

| 项 | 默认 `build.sh` | `ENABLE_AV3A=1 ./build.sh` |
|----|-----------------|----------------------------|
| FFmpeg 链接 | 静态 `.a` | 共享 `.so` |
| 额外解码器 | 无 | `libarcdav3a` |
| 伴生库 | 无 | `libav3a_binaural_render.so`、`libAVS3AudioDec.so` |
| JNI CMake | 上游静态 | `overlays/av3a/jni/CMakeLists.txt` |

## 2. FFmpeg 源码要求

官方 `git.ffmpeg.org` 的 `release/6.0` **不含** `libarcdav3a`。需使用已集成 AV3A 的 FFmpeg 分支替换 `ffmpeg` 子模块，例如社区维护的补丁版（参见 [androidx/media#938](https://github.com/androidx/media/issues/938)、[nilaoda/Blog#81](https://github.com/nilaoda/Blog/discussions/81)）。

将补丁版 FFmpeg 检出到 `ffmpeg/` 后：

```bash
export ENABLE_AV3A=1
bash scripts/apply-av3a-overlays.sh
bash ./build.sh
```

## 3. 发布

1. 递增 `gradle.properties` 中 `jellyfin.version`（如 `1.8.2-av3a1`）。
2. 推送标签 `v1.8.2-av3a1` 触发 CI。
3. VsTV 在 `libs.versions.toml` 将 `jellyfinMedia3Ffmpeg` 指向该版本。

## 4. 许可说明

`libarcdav3a` / AVS3 音频解码实现可能受专有或受限许可约束；分发 APK 前请自行评估合规性。
