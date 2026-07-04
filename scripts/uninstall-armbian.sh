#!/bin/sh
# rtp2httpd uninstall script for Armbian / Debian-based systems
# Removes the binary, systemd service, and (optionally) the config file installed by install-armbian.sh

set -eu

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Installation paths (must match install-armbian.sh)
INSTALL_DIR="/usr/local/bin"
CONFIG_PATH="/etc/rtp2httpd.conf"
SERVICE_PATH="/etc/systemd/system/rtp2httpd.service"
BINARY_PATH="${INSTALL_DIR}/rtp2httpd"

# Runtime options
LANG_CODE="en"
PURGE_CONFIG=false

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

stop_and_disable_service() {
    if ! command -v systemctl >/dev/null 2>&1; then
        print_warn "$(msg '未检测到 systemctl，跳过服务停用' 'systemctl not detected, skipping service teardown')"
        return 0
    fi

    if systemctl is-active --quiet rtp2httpd.service 2>/dev/null; then
        print_info "$(msg '正在停止 rtp2httpd 服务...' 'Stopping rtp2httpd service...')"
        systemctl stop rtp2httpd.service 2>/dev/null || true
    fi

    if systemctl is-enabled --quiet rtp2httpd.service 2>/dev/null; then
        print_info "$(msg '正在禁用 rtp2httpd 服务...' 'Disabling rtp2httpd service...')"
        systemctl disable rtp2httpd.service 2>/dev/null || true
    fi
}

remove_service_file() {
    if [ -f "$SERVICE_PATH" ]; then
        print_info "$(msg "正在删除 systemd 服务文件: $SERVICE_PATH" "Removing systemd service file: $SERVICE_PATH")"
        rm -f "$SERVICE_PATH"
        if command -v systemctl >/dev/null 2>&1; then
            systemctl daemon-reload 2>/dev/null || true
            systemctl reset-failed rtp2httpd.service 2>/dev/null || true
        fi
    else
        print_warn "$(msg "未找到 systemd 服务文件，跳过: $SERVICE_PATH" "systemd service file not found, skipping: $SERVICE_PATH")"
    fi
}

remove_binary() {
    if [ -f "$BINARY_PATH" ]; then
        print_info "$(msg "正在删除二进制文件: $BINARY_PATH" "Removing binary: $BINARY_PATH")"
        rm -f "$BINARY_PATH"
    else
        print_warn "$(msg "未找到二进制文件，跳过: $BINARY_PATH" "Binary not found, skipping: $BINARY_PATH")"
    fi
}

remove_config() {
    if [ "$PURGE_CONFIG" != true ]; then
        if [ -f "$CONFIG_PATH" ]; then
            print_info "$(msg "保留配置文件: $CONFIG_PATH（使用 --purge 可一并删除）" "Keeping config file: $CONFIG_PATH (use --purge to remove it too)")"
        fi
        return 0
    fi

    if [ -f "$CONFIG_PATH" ]; then
        print_info "$(msg "正在删除配置文件: $CONFIG_PATH" "Removing config file: $CONFIG_PATH")"
        rm -f "$CONFIG_PATH"
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
            --purge)
                PURGE_CONFIG=true
                shift
                ;;
            --help|-h)
                echo "$(msg '用法' 'Usage'): $0 [$(msg '选项' 'options')]"
                echo ""
                echo "$(msg '选项' 'Options'):"
                echo "  --lang <zh|en>  $(msg '设置界面语言（默认: en）' 'Set display language (default: en)')"
                echo "  --purge         $(msg '同时删除配置文件 (/etc/rtp2httpd.conf)' 'Also remove the config file (/etc/rtp2httpd.conf)')"
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
    print_info "=========================================="
    print_info "$(msg 'rtp2httpd Armbian 卸载脚本' 'rtp2httpd Armbian Uninstaller')"
    print_info "=========================================="
    echo ""

    check_root

    stop_and_disable_service
    remove_service_file
    remove_binary
    remove_config

    print_info ""
    print_info "=========================================="
    print_info "$(msg '卸载完成！' 'Uninstall complete!')"
    print_info "=========================================="
    print_info ""
    if [ "$PURGE_CONFIG" != true ] && [ -f "$CONFIG_PATH" ]; then
        print_info "$(msg "配置文件仍保留在: $CONFIG_PATH" "Config file is still present at: $CONFIG_PATH")"
    fi
    print_info ""
}

parse_args "$@"
main
