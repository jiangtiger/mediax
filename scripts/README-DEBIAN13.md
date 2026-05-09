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

若以 **root** 执行（且系统未装 `sudo`），脚本会直接调用 `apt-get`，无需安装 `sudo`。

构建需要 **JVM 17 或以上**（Gradle 要求）；**JDK 21 可用**，无负面影响。  
Debian 13（trixie）仓库里可能没有 `openjdk-17-jdk`，脚本会依次尝试 `openjdk-21-jdk`、`default-jdk`；若已装好 JDK 17+（例如已有 `javac`），会自动跳过装 JDK。  
`JAVA_HOME` 示例：`/usr/lib/jvm/java-21-openjdk-amd64` 或 `java-17-openjdk-amd64`。

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

- **未设置** `EXTRA_FFMPEG_DECODERS` 时：`build.sh` 与 CI 行为一致：在 Jellyfin 默认 **音频** 解码器之外，附加 **`h264 hevc vp9 av1 opus`**（与 `debian13-build.sh` 默认导出一致）。
- **已设置且为非空**（`export EXTRA_FFMPEG_DECODERS="..."`）：**只追加**你列出的解码器（不再自动加 `h264…opus`，需自己写全）。
- **已设置但为空串**（`EXTRA_FFMPEG_DECODERS= ./build.sh`）：**仅** Jellyfin 默认音频集，与上游最小集一致。

**本仓库 JNI FFmpeg 不含 `demuxer`/`protocol`**（AndroidX `build_ffmpeg.sh` 禁用了 `avformat` 等），**不能**依靠本 AAR 解决 RTSP/RTP 乱序、非标 SDP；需在拉流层处理。

## 4.1 GitHub Actions 发布后如何引用（GitHub Packages）

仅在推送 **semver 标签**（与 `1.8.1` 同形：`主.次.补丁`，无 `v` 前缀）时触发 Actions 完整构建，并执行 `publishDefaultPublicationToGitHubPackagesRepository`。**向 `main`/`master` 推送分支不会触发该工作流。**  
发布用的 Maven 版本取自 **标签名**（`-Pjellyfin.version=<tag>`），应与 `gradle.properties` 中维护的版本说明一致。示例：
`git tag -a 1.8.1 -m "1.8.1" && git push origin 1.8.1`

下游 `build.gradle` 示例：

```kotlin
repositories {
  maven {
    url = uri("https://maven.pkg.github.com/vesaaa/mediax")
    credentials {
      username = project.findProperty("gpr.user") as String? ?: System.getenv("GITHUB_ACTOR")
      password = project.findProperty("gpr.key") as String? ?: System.getenv("GITHUB_TOKEN")
    }
  }
}
// implementation("org.jellyfin.media3:media3-ffmpeg-decoder:1.8.1")
```

阅读 GitHub 包需 **有权限的 token**（如 `read:packages` 的 PAT）；同一账号对公开仓库可读时按 GitHub 当前策略为准。

每次 CI 还会上传 **`maven-org-jellyfin-media3`** 构件，可从 Actions 页下载完整目录备用。

## 5. 一键构建

在仓库根目录：

```bash
export ANDROID_HOME
# 可选：覆盖默认解码器（例：去掉 av1 减轻 CPU）
# export EXTRA_FFMPEG_DECODERS="h264 hevc vp9 opus"
chmod +x scripts/debian13-build.sh
./scripts/debian13-build.sh
```

步骤概要：

1. `git submodule update --init --recursive`
2. `./build.sh`（内部会创建 `media/.../jni/ffmpeg` → 本仓库 `ffmpeg` 的符号链接并编译多 ABI）
3. `./gradlew :media3-ffmpeg-decoder:publishToMavenLocal`

产物位于 Maven 本地仓库，例如：

`~/.m2/repository/org/jellyfin/media3/media3-ffmpeg-decoder/<version>/`

版本号由根目录 `gradle.properties` 的 `jellyfin.version` 决定（见 `buildSrc`）。

仅需要 **decoder 子模块** 的 release AAR 时：

```bash
./gradlew :androidx-media-lib-decoder-ffmpeg:bundleReleaseAar
```

## 6. 与 CI 的差异

- CI 使用 `ubuntu-24.04` + `ANDROID_SDK_ROOT` 预置路径；本机使用你设置的 `ANDROID_HOME`。
- 其余流程应与 workflow 中「Build ffmpeg」「publishToMavenLocal」一致；本地可通过 `NDK_VER` / `CMAKE_VER` 环境变量与脚本注释保持同步。

## 7. 常见问题

- **子模块目录为空**：务必执行 `git submodule update --init --recursive`。
- **`ffmpeg` 子模块克隆极慢或中途失败（exit 1）**：`.gitmodules` 指向 `https://git.ffmpeg.org/ffmpeg.git`，在内网或跨境链路上容易超时。可先放宽低速超时后重试，例如：  
  `export GIT_HTTP_LOW_SPEED_LIMIT=1000 GIT_HTTP_LOW_SPEED_TIME=600`  
  再执行 `git submodule update --init --recursive`；仍失败则分步执行 `git submodule update --init media`，成功后再 `git submodule update --init ffmpeg`，必要时在网络较好的环境打包 `mediax` 目录后再拷贝到 Debian。
- **NDK 路径不存在**：确认 `ls "$ANDROID_HOME/ndk/26.1.10909125"`。
- **FFmpeg 编译失败**：确认 `ffmpeg` 子模块已检出且 `ffmpeg/configure` 存在；必要时清理 `media/libraries/decoder_ffmpeg/src/main/jni/ffmpeg/android-libs` 后重试。
- **在 Windows 上跑 Gradle 报 “projectDirectory … media/… does not exist”**：本地未检出 `media` 子模块（或路径无效）。请先 `git submodule update --init --recursive`，或在 Linux/Debian 上按本文构建。
