# mediax

自建 **Jellyfin AndroidX Media3 FFmpeg 解码扩展**（`org.jellyfin.media3:media3-ffmpeg-decoder`）的构建与发布仓库，基于 [androidx/media](https://github.com/androidx/media) 与 FFmpeg 子模块。

## 解码能力：相对上游 Jellyfin 默认（原版 `build.sh` 音频集）多支持什么

本仓库在未设置 `EXTRA_FFMPEG_DECODERS` 时，与 Jellyfin upstream 同源思路：**先启用一组默认 FFmpeg 解码器，再按需附加**。  
其中 **「Jellyfin 默认」音频集**与本仓库 [`build.sh`](build.sh) 中 `DEFAULT_DECODERS` 一致：

| 类型 | FFmpeg `--enable-decoder=` 名称 |
|------|--------------------------------|
| 音频（与上游 Jellyfin 默认一致） | `flac` `alac` `pcm_mulaw` `pcm_alaw` `mp3` `aac` `ac3` `eac3` `dca` `mlp` `truehd` |

在上述基础上，mediax **默认追加**（`MODERN_EXTRA`）用于互联网/OTT 常见的软解兜底：

| 类型 | FFmpeg 解码器 |
|------|---------------|
| 视频（相对上游默认多出） | `h264`、`hevc`、`vp9`、`av1` |
| 音频（相对上游默认多出） | `opus` |

简要说明：**`av1` 软解 CPU 占用高**；**RTSP/RTP 乱序等非标流**不属于本 JNI FFmpeg 解码层能力，需在 App 拉流/解复用侧处理（见 Debian 文档）。  
覆盖方式与完整说明见下方的 Debian 文档第 4 节。

## 文档

- **[Debian / 本机构建与 FFmpeg 解码说明](scripts/README-DEBIAN13.md)**：依赖、子模块、`EXTRA_FFMPEG_DECODERS`、GitHub Packages 引用、`409` 与 POM 行为等。

## 引用（GitHub Packages）

坐标：`org.jellyfin.media3:media3-ffmpeg-decoder`，版本以 **发布标签** 为准（如 `1.8.1-beta3`，对应 tag `v1.8.1-beta3`）。  
同一版本在 Packages 上**不可重复覆盖**；如需重发请递增标签。

## 发布触发

推送符合 workflow 的版本标签触发构建与发布（见 `.github/workflows/extension-build.yaml`）。
