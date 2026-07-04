#!/bin/sh
# rtp2httpd quick install script for Armbian / Debian-based systems
# Automatically download and install the latest release binary from GitHub

set -eu

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# GitHub repository info (overridable via --repo-owner / --repo-url)
REPO_OWNER="stackia"
REPO_NAME="rtp2httpd"
SOURCE_REPO_URL=""

# Installation paths
INSTALL_DIR="/usr/local/bin"
CONFIG_PATH="/etc/rtp2httpd.conf"
SERVICE_PATH="/etc/systemd/system/rtp2httpd.service"
TMP_DIR="/tmp/rtp2httpd_install_armbian"

# Runtime options
LANG_CODE="en"
SELECTED_VERSION=""
ENABLE_SERVICE=true
FROM_SOURCE=""

# Message helper
msg() {
    if [ "$LANG_CODE" = "en" ]; then
        if [ "$#" -ge 2 ]; then
            echo "$2"
        else
            echo "$1"
        fi
    else
        echo "$1"
    fi
}

print_info() {
    printf "${GREEN}[INFO]${NC} %s\n" "$1" >&2
}

print_warn() {
    printf "${YELLOW}[WARN]${NC} %s\n" "$1" >&2
}

print_error() {
    printf "${RED}[ERROR]${NC} %s\n" "$1" >&2
}

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "$(msg '此脚本必须以 root 身份运行' 'This script must be run as root')"
        exit 1
    fi
}

check_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        print_error "$(msg "未找到命令: $1" "Command not found: $1")"
        return 1
    fi
    return 0
}

ensure_basic_tools() {
    if command -v curl >/dev/null 2>&1 && command -v wget >/dev/null 2>&1; then
        return 0
    fi

    if command -v apt-get >/dev/null 2>&1; then
        print_info "$(msg '正在为系统安装 curl 和 wget...' 'Installing curl and wget for this system...')"
        export DEBIAN_FRONTEND=noninteractive
        apt-get update >/dev/null 2>&1
        apt-get install -y curl wget ca-certificates >/dev/null 2>&1
        return 0
    fi

    print_error "$(msg '未找到 curl 或 wget，且 apt-get 不可用' 'Neither curl nor wget was found and apt-get is unavailable')"
    exit 1
}

ensure_build_tools() {
    if command -v git >/dev/null 2>&1 && command -v cmake >/dev/null 2>&1 && command -v cc >/dev/null 2>&1 &&
        command -v node >/dev/null 2>&1 && command -v corepack >/dev/null 2>&1; then
        return 0
    fi

    if ! command -v apt-get >/dev/null 2>&1; then
        print_error "$(msg '未找到 apt-get，无法自动安装编译依赖，请手动安装 git cmake build-essential nodejs 后重试' 'apt-get not found, cannot auto-install build dependencies; please install git, cmake, build-essential, and nodejs manually and retry')"
        exit 1
    fi

    print_info "$(msg '正在安装编译依赖...' 'Installing build dependencies...')"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >/dev/null 2>&1

    if ! command -v git >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1 || ! command -v cc >/dev/null 2>&1; then
        apt-get install -y git cmake build-essential pkg-config >/dev/null 2>&1
    fi

    if ! command -v node >/dev/null 2>&1; then
        print_info "$(msg '正在安装 Node.js...' 'Installing Node.js...')"
        curl -fsSL https://deb.nodesource.com/setup_24.x | bash - >/dev/null 2>&1
        apt-get install -y nodejs >/dev/null 2>&1
    fi

    if ! command -v corepack >/dev/null 2>&1; then
        npm install -g corepack >/dev/null 2>&1 || true
    fi
    corepack enable >/dev/null 2>&1 || true
}

# Clone and build rtp2httpd from source. Echoes the built binary's path on
# success (all other output goes to stderr so command substitution stays clean).
build_from_source() {
    ref="$1"
    repo_url="$2"
    src_dir="${TMP_DIR}/src"

    print_info "$(msg "正在从源码构建 (仓库: $repo_url, 引用: $ref)..." "Building from source (repo: $repo_url, ref: $ref)...")"

    rm -rf "$src_dir"
    if ! git clone --branch "$ref" --depth 1 "$repo_url" "$src_dir" >&2; then
        print_error "$(msg "克隆仓库失败: $repo_url ($ref)" "Failed to clone repository: $repo_url ($ref)")"
        exit 1
    fi

    (
        cd "$src_dir"

        print_info "$(msg '正在构建 Web UI...' 'Building web UI...')" >&2
        pnpm install --frozen-lockfile
        pnpm run web-ui:build

        print_info "$(msg '正在编译 rtp2httpd...' 'Compiling rtp2httpd...')" >&2
        cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_AGGRESSIVE_OPT=ON
        cmake --build build -j"$(getconf _NPROCESSORS_ONLN)"
    ) >&2

    echo "${src_dir}/build/rtp2httpd"
}

detect_download_tool() {
    if command -v curl >/dev/null 2>&1; then
        echo "curl"
    elif command -v wget >/dev/null 2>&1; then
        echo "wget"
    else
        echo ""
    fi
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64|armv8l)
            echo "aarch64"
            ;;
        armv7l|armv7)
            if [ -f /proc/cpuinfo ] && grep -qi 'vfp' /proc/cpuinfo; then
                echo "armv7-eabihf"
            else
                echo "armv7-eabi"
            fi
            ;;
        armv6l|armv6)
            if [ -f /proc/cpuinfo ] && grep -qi 'vfp' /proc/cpuinfo; then
                echo "arm-eabihf"
            else
                echo "arm-eabi"
            fi
            ;;
        mips64)
            echo "mips64"
            ;;
        mips)
            echo "mips"
            ;;
        mipsel)
            echo "mipsel"
            ;;
        *)
            echo "$(uname -m)"
            ;;
    esac
}

get_latest_version() {
    print_info "$(msg '获取最新版本信息...' 'Fetching latest version information...')"

    version=$(curl -fsSL "${GITHUB_API}/releases/latest" 2>/dev/null | sed -n 's/.*"tag_name": "\([^"]*\)".*/\1/p' | head -n 1)

    if [ -z "$version" ]; then
        print_error "$(msg '无法获取最新版本信息' 'Unable to fetch latest version information')"
        exit 1
    fi

    print_info "$(msg "最新版本: $version" "Latest version: $version")"
    echo "$version"
}

get_release_asset_name() {
    version="$1"
    arch="$2"

    release_json=$(curl -fsSL "${GITHUB_API}/releases/tags/${version}" 2>/dev/null || true)

    asset_name=$(printf '%s\n' "$release_json" | grep -o '"name": "[^"]*"' | sed 's/.*"name": "\([^"]*\)"/\1/' | grep -E "^rtp2httpd-.*-${arch}$" | head -n 1 || true)

    if [ -n "$asset_name" ]; then
        echo "$asset_name"
        return 0
    fi

    echo "rtp2httpd-${version#v}-${arch}"
}

build_download_url() {
    version="$1"
    asset_name="$2"
    echo "${GITHUB_RELEASE}/${version}/${asset_name}"
}

download_file() {
    url="$1"
    output="$2"

    print_info "$(msg '下载' 'Downloading'): $(basename "$output")"

    if [ "$DOWNLOAD_TOOL" = "curl" ]; then
        if ! curl -fsSL -o "$output" "$url"; then
            print_error "$(msg "下载失败: $url" "Download failed: $url")"
            return 1
        fi
    else
        if ! wget -q -O "$output" "$url"; then
            print_error "$(msg "下载失败: $url" "Download failed: $url")"
            return 1
        fi
    fi

    return 0
}

create_default_config() {
    if [ -f "$CONFIG_PATH" ]; then
        print_warn "$(msg "配置文件已存在，跳过: $CONFIG_PATH" "Config file already exists, skipping: $CONFIG_PATH")"
        return 0
    fi

    print_info "$(msg '正在创建默认配置文件...' 'Creating default config file...')"

    install -d "$(dirname "$CONFIG_PATH")"

    cat > "$CONFIG_PATH" <<'EOF'
# rtp2httpd default config
[global]
verbosity = 2
#maxclients = 20

[bind]
* 5140
EOF
}

create_systemd_service() {
    if [ -f "$SERVICE_PATH" ]; then
        print_warn "$(msg "systemd 服务已存在，跳过: $SERVICE_PATH" "systemd service already exists, skipping: $SERVICE_PATH")"
        return 0
    fi

    print_info "$(msg '正在创建 systemd 服务...' 'Creating systemd service...')"

    install -d "$(dirname "$SERVICE_PATH")"

    cat > "$SERVICE_PATH" <<EOF
[Unit]
Description=rtp2httpd IPTV multicast to HTTP gateway
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/rtp2httpd --config ${CONFIG_PATH}
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
}

enable_service() {
    if [ "$ENABLE_SERVICE" != true ]; then
        return 0
    fi

    if command -v systemctl >/dev/null 2>&1; then
        print_info "$(msg '正在重新加载 systemd 配置...' 'Reloading systemd configuration...')"
        systemctl daemon-reload 2>/dev/null || true
        systemctl enable rtp2httpd.service 2>/dev/null || true
    else
        print_warn "$(msg '未检测到 systemctl，跳过服务启用' 'systemctl not detected, skipping service enablement')"
    fi
}

cleanup() {
    if [ -d "$TMP_DIR" ]; then
        print_info "$(msg '清理临时文件...' 'Cleaning up temporary files...')"
        rm -rf "$TMP_DIR"
    fi
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --lang)
                if [ -n "${2:-}" ]; then
                    LANG_CODE="$2"
                    shift
                else
                    print_error "$(msg '--lang 需要指定语言代码 (zh 或 en)' '--lang requires a language code (zh or en)')"
                    exit 1
                fi
                shift
                ;;
            --version)
                if [ -n "${2:-}" ]; then
                    SELECTED_VERSION="$2"
                    shift
                else
                    print_error "$(msg '--version 需要指定版本号' '--version requires a version number')"
                    exit 1
                fi
                shift
                ;;
            --no-service)
                ENABLE_SERVICE=false
                shift
                ;;
            --from-source)
                if [ -n "${2:-}" ]; then
                    FROM_SOURCE="$2"
                    shift
                else
                    print_error "$(msg '--from-source 需要指定分支、标签或 commit' '--from-source requires a branch, tag, or commit')"
                    exit 1
                fi
                shift
                ;;
            --repo-url)
                if [ -n "${2:-}" ]; then
                    SOURCE_REPO_URL="$2"
                    shift
                else
                    print_error "$(msg '--repo-url 需要指定仓库地址' '--repo-url requires a repository URL')"
                    exit 1
                fi
                shift
                ;;
            --repo-owner)
                if [ -n "${2:-}" ]; then
                    REPO_OWNER="$2"
                    shift
                else
                    print_error "$(msg '--repo-owner 需要指定 GitHub 用户名或组织名' '--repo-owner requires a GitHub username or organization')"
                    exit 1
                fi
                shift
                ;;
            --help|-h)
                echo "$(msg '用法' 'Usage'): $0 [$(msg '选项' 'options')]"
                echo ""
                echo "$(msg '选项' 'Options'):"
                echo "  --lang <zh|en>  $(msg '设置界面语言（默认: en）' 'Set display language (default: en)')"
                echo "  --version <vX.Y.Z>  $(msg '安装指定版本' 'Install a specific version')"
                echo "  --no-service    $(msg '跳过创建和启用 systemd 服务' 'Skip creating and enabling systemd service')"
                echo "  --from-source <ref>  $(msg '从源码构建安装指定分支/标签/commit，而非下载 Release 二进制文件' 'Build and install from source at this branch/tag/commit, instead of downloading a release binary')"
                echo "  --repo-owner <owner>  $(msg "GitHub 仓库所有者，用于下载 Release 或（配合 --from-source）克隆源码（默认: ${REPO_OWNER}）" "GitHub repository owner, used for downloading releases or (with --from-source) cloning source (default: ${REPO_OWNER})")"
                echo "  --repo-url <url>  $(msg '配合 --from-source 使用，完整指定要克隆的仓库地址（优先于 --repo-owner）' 'Used with --from-source, the full repository URL to clone (overrides --repo-owner)')"
                echo "  --help, -h      $(msg '显示此帮助信息' 'Show help')"
                echo ""
                exit 0
                ;;
            *)
                print_error "$(msg "未知参数: $1" "Unknown argument: $1")"
                exit 1
                ;;
        esac
    done
}

main() {
    # Computed here (not at top-level) so --repo-owner/--repo-url overrides,
    # parsed by parse_args before main() runs, take effect.
    GITHUB_API="https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}"
    GITHUB_RELEASE="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download"
    if [ -z "$SOURCE_REPO_URL" ]; then
        SOURCE_REPO_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}.git"
    fi

    print_info "=========================================="
    print_info "$(msg 'rtp2httpd Armbian Quick Installer' 'rtp2httpd Armbian Quick Installer')"
    print_info "=========================================="
    echo ""

    check_root

    if ! check_command install; then
        exit 1
    fi

    ensure_basic_tools

    if [ -n "$FROM_SOURCE" ]; then
        ensure_build_tools

        mkdir -p "$TMP_DIR"
        VERSION="$FROM_SOURCE"
        BINARY_PATH=$(build_from_source "$FROM_SOURCE" "$SOURCE_REPO_URL")
    else
        DOWNLOAD_TOOL=$(detect_download_tool)
        if [ -z "$DOWNLOAD_TOOL" ]; then
            print_error "$(msg '未找到 curl 或 wget' 'Neither curl nor wget was found')"
            exit 1
        fi

        print_info "$(msg "检测到下载工具: $DOWNLOAD_TOOL" "Detected download tool: $DOWNLOAD_TOOL")"

        ARCH=$(detect_arch)
        print_info "$(msg "检测到架构: $ARCH" "Detected architecture: $ARCH")"

        if [ -z "$SELECTED_VERSION" ]; then
            VERSION=$(get_latest_version)
        else
            VERSION="$SELECTED_VERSION"
            print_info "$(msg "使用指定版本: $VERSION" "Using specified version: $VERSION")"
        fi

        mkdir -p "$TMP_DIR"

        ASSET_NAME=$(get_release_asset_name "$VERSION" "$ARCH")
        DOWNLOAD_URL=$(build_download_url "$VERSION" "$ASSET_NAME")

        print_info "$(msg "准备下载软件包: $ASSET_NAME" "Preparing to download package: $ASSET_NAME")"

        BINARY_PATH="$TMP_DIR/$(basename "$ASSET_NAME")"
        if ! download_file "$DOWNLOAD_URL" "$BINARY_PATH"; then
            cleanup
            exit 1
        fi
    fi

    install -d "$INSTALL_DIR"
    install -m 0755 "$BINARY_PATH" "$INSTALL_DIR/rtp2httpd"

    create_default_config
    create_systemd_service
    enable_service

    cleanup

    print_info ""
    print_info "=========================================="
    print_info "$(msg '安装完成！' 'Installation complete!')"
    print_info "=========================================="
    print_info ""
    print_info "$(msg "已安装版本: $VERSION" "Installed version: $VERSION")"
    print_info "$(msg "二进制文件: $INSTALL_DIR/rtp2httpd" "Binary: $INSTALL_DIR/rtp2httpd")"
    print_info "$(msg "配置文件: $CONFIG_PATH" "Config file: $CONFIG_PATH")"
    print_info ""
    print_info "$(msg '后续步骤：' 'Next steps:')"
    print_info "$(msg "1. 运行: systemctl start rtp2httpd" "1. Run: systemctl start rtp2httpd")"
    print_info "$(msg "2. 运行: systemctl status rtp2httpd" "2. Run: systemctl status rtp2httpd")"
    print_info "$(msg "3. 如需修改配置，编辑 $CONFIG_PATH" "3. To change settings, edit $CONFIG_PATH")"
    print_info ""
}

trap cleanup EXIT INT TERM

parse_args "$@"
main
