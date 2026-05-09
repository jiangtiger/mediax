#!/usr/bin/env bash
# Debian 13（及兼容版本）：安装本机构建 mediax 所需的常见依赖（不含 Android SDK，见 README）。
# root 下直接调用 apt-get；普通用户需已安装 sudo，或未精简掉 sudo 的 Debian。
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
  APT_GET=(apt-get)
elif command -v sudo >/dev/null 2>&1; then
  APT_GET=(sudo apt-get)
else
  echo "未找到 sudo，且当前不是 root。请以 root 执行本脚本，或安装 sudo：apt-get install -y sudo" >&2
  exit 1
fi

"${APT_GET[@]}" update
"${APT_GET[@]}" install -y \
  openjdk-17-jdk \
  git \
  curl \
  unzip \
  procps \
  build-essential \
  python3

cat <<'EOF'

Android SDK / NDK 需单独安装，例如：

  export ANDROID_HOME="$HOME/Android/Sdk"
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  # 从 https://developer.android.com/studio#command-line-tools-only 下载 commandlinetools-linux
  # 解压到 $ANDROID_HOME/cmdline-tools/latest/

  yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
  "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --install \
    "platform-tools" \
    "platforms;android-34" \
    "ndk;26.1.10909125" \
    "cmake;3.31.1"

然后在本仓库根目录执行：

  export ANDROID_HOME
  ./scripts/debian13-build.sh

EOF
