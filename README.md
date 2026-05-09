# mediax

自建 **Jellyfin AndroidX Media3 FFmpeg 解码扩展**（`org.jellyfin.media3:media3-ffmpeg-decoder`）的构建与发布仓库，基于 [androidx/media](https://github.com/androidx/media) 与 FFmpeg 子模块。

## 文档

- **[Debian / 本机构建与 FFmpeg 解码说明](scripts/README-DEBIAN13.md)**：依赖、子模块、`EXTRA_FFMPEG_DECODERS`、相对 Jellyfin 默认解码器的新增项、GitHub Packages 引用说明。

## 引用（GitHub Packages）

坐标：`org.jellyfin.media3:media3-ffmpeg-decoder`，版本以 **发布标签** 为准（如 `1.8.1-beta3`，对应 tag `v1.8.1-beta3`）。  
同一版本在 Packages 上**不可重复覆盖**；如需重发请递增标签。

## 发布触发

推送符合 workflow 的版本标签触发构建与发布（见 `.github/workflows/extension-build.yaml`）。
