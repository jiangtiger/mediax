# Debian 13 本机构建说明（mediax）

在 Debian 13（或兼容的测试版/unstable）上从源码构建 **Jellyfin AndroidX Media3 FFmpeg 解码扩展**，并与 GitHub Actions（`.github/workflows/extension-build.yaml`）对齐：NDK **26.1.10909125**、CMake **3.31.1**、`minSdk` **21**。

## 1. 克隆与子模块

```bash
git clone <你的 mediax 仓库 URL>
cd mediax
git submodule update --init --recursive
```

若未检出子模块，`gradle` 会报错找不到 `media/libraries/...`。

## 2. 系统依赖

```bash
chmod +x scripts/debian13-install-deps.sh
./scripts/debian13-install-deps.sh
```

需要 **JDK 17**（`JAVA_HOME` 建议指向 `/usr/lib/jvm/java-17-openjdk-amd64`）。

## 3. Android SDK / NDK / CMake

自行安装 [command-line tools](https://developer.android.com/studio#command-line-tools-only)，设置：

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
```

安装组件（版本与仓库根 `build.gradle.kts`、`build.sh` 一致）：

```bash
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --install \
  "platform-tools" \
  "platforms;android-34" \
  "ndk;26.1.10909125" \
  "cmake;3.31.1"
yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
```

## 4. FFmpeg 解码器列表

根目录 `build.sh` 保留 Jellyfin 默认音频解码器；可通过环境变量 **追加** FFmpeg 解码器（空格分隔），对应 upstream `build_ffmpeg.sh` 的 `--enable-decoder=`。

- `scripts/debian13-build.sh` 默认设置：`EXTRA_FFMPEG_DECODERS=h264 hevc vp9`（便于 H.264 / HEVC 等软解场景）。
- 若要与上游完全一致（仅音频）：在同一 shell 命令里把变量设为空，例如  
  `EXTRA_FFMPEG_DECODERS= ./scripts/debian13-build.sh` 或 `EXTRA_FFMPEG_DECODERS= ./build.sh`。

## 5. 一键构建

在仓库根目录：

```bash
export ANDROID_HOME
# 可选：覆盖默认视频解码器
# export EXTRA_FFMPEG_DECODERS="h264 hevc"
chmod +x scripts/debian13-build.sh
./scripts/debian13-build.sh
```

步骤概要：

1. `git submodule update --init --recursive`
2. `./build.sh`（内部会创建 `media/.../jni/ffmpeg` → 本仓库 `ffmpeg` 的符号链接并编译多 ABI）
3. `./gradlew :media3-ffmpeg-decoder:publishToMavenLocal`

产物位于 Maven 本地仓库，例如：

`~/.m2/repository/org/jellyfin/media3/media3-ffmpeg-decoder/<version>/`

版本号由 Gradle 属性 `jellyfin.version` 决定（未设置时多为 `latest-SNAPSHOT`，见 `buildSrc`）。

仅需要 **decoder 子模块** 的 release AAR 时：

```bash
./gradlew :androidx-media-lib-decoder-ffmpeg:bundleReleaseAar
```

## 6. 与 CI 的差异

- CI 使用 `ubuntu-24.04` + `ANDROID_SDK_ROOT` 预置路径；本机使用你设置的 `ANDROID_HOME`。
- 其余流程应与 workflow 中「Build ffmpeg」「publishToMavenLocal」一致；本地可通过 `NDK_VER` / `CMAKE_VER` 环境变量与脚本注释保持同步。

## 7. 常见问题

- **子模块目录为空**：务必执行 `git submodule update --init --recursive`。
- **NDK 路径不存在**：确认 `ls "$ANDROID_HOME/ndk/26.1.10909125"`。
- **FFmpeg 编译失败**：确认 `ffmpeg` 子模块已检出且 `ffmpeg/configure` 存在；必要时清理 `media/libraries/decoder_ffmpeg/src/main/jni/ffmpeg/android-libs` 后重试。
