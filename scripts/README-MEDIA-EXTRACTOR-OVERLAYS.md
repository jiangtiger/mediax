# Media3 Extractor Overlays（mediax）

在官方 `androidx/media` 子模块上叠加 VSTV 需要的解复用层补丁，并通过 **`media3-extractor-vstv`** 模块发布为 `org.jellyfin.media3:media3-extractor`。

## 包含内容

| Overlay | 来源 | 作用 |
|---------|------|------|
| **杜比视界 TS** | [androidx/media#3280](https://github.com/androidx/media/pull/3280)（overlay 源来自 1.10 port，**须在 media 子模块 `1.8.0` 上应用**） | HLS/MPEG-TS 内 DV 流正确输出 `video/dolby-vision`，避免 Profile 5 绿/洋红偏色 |
| **Enhanced FLV HEVC** | 社区实现（codec type 12 + `HevcConfig`） | FLV 容器内 H.265 解复用 |

## 本地应用 overlay

```bash
git submodule update --init --recursive
MEDIAX_MEDIA_REF=1.8.0 bash scripts/apply-media-extractor-overlays.sh
bash ./gradlew :media3-extractor-vstv:publishToMavenLocal -Pjellyfin.version=1.8.0+vstv2
# 或打 tag 后由 CI 发布到 GitHub Packages
git tag 1.8.0+vstv2 && git push origin 1.8.0+vstv2
```

> overlay 须在 **Media3 1.8.0** 子模块上应用（`MEDIAX_MEDIA_REF=1.8.0`）。`NalUnitUtil` 等文件基于 1.10 port，已去掉 **H.266/VVC** 等 1.8.0 不存在的 API。  
> **勿在 1.10 子模块上发布 `1.8.0+vstv*`**：与 VSTV 内 ExoPlayer 1.8.0 混用会 `NoSuchMethodError`（`Metadata.getMatchingEntries`）。

## VSTV 引用

`mytv-android` 已配置：

- `org.jellyfin.media3:media3-extractor:1.8.0+vstv2`（基于 Media3 **1.8.0** 子模块 + overlay；**勿用 `+vstv1`**）
- 排除官方 `androidx.media3:media3-extractor`

**首次集成前**需先发布上述 artifact，否则 Gradle 解析会失败。

## 与 AV3A 的关系

- **media3-extractor-vstv**：解复用（TS/FLV）
- **media3-ffmpeg-decoder**：解码软解兜底

两者独立，可分别发版。
