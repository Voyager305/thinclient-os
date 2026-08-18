#!/bin/sh
# Сборка образа тонкого клиента.
#
#   ./build.sh              — сборка (на macOS всегда в Docker; на Linux — в Docker,
#                             если он есть, иначе нативно)
#   NATIVE=1 ./build.sh     — принудительно нативная сборка на Linux (без Docker)
#   ./build.sh menuconfig   — покрутить конфиг Buildroot
#   ./build.sh clean        — очистить сборку
#
# Первая сборка 1–3 часа (тулчейн, ядро, Xorg, FreeRDP), дальше инкрементально.
set -e
cd "$(dirname "$0")"

BR_VERSION=2025.08
IMAGE=thinclient-build

if [ ! -d buildroot ]; then
    echo ">>> Клонирую Buildroot $BR_VERSION..."
    git clone --depth 1 --branch "$BR_VERSION" \
        https://gitlab.com/buildroot.org/buildroot.git buildroot
fi

if [ "$(uname -s)" = "Linux" ] && { [ -n "$NATIVE" ] || ! command -v docker >/dev/null 2>&1; }; then
    MODE=native
    EXT="$PWD"
else
    MODE=docker
    EXT=/src
fi

if [ "$MODE" = native ]; then
    if [ "$(id -u)" = 0 ]; then
        echo "Не собирай Buildroot под root — запусти от обычного пользователя" >&2
        exit 1
    fi
    # post-image.sh собирает usb.img хостовыми утилитами — проверим, что они есть
    for tool in gcc make mkfs.vfat mcopy parted syslinux; do
        command -v "$tool" >/dev/null 2>&1 || {
            echo "Не хватает '$tool'. Список пакетов для своего дистрибутива — в README (раздел 'Сборка (Linux)')" >&2
            exit 1
        }
    done
fi

run() {
    if [ "$MODE" = native ]; then
        ( cd buildroot && sh -c "$1" )
    else
        docker build -t "$IMAGE" .
        docker run --rm -it -v "$PWD:/src" -w /src/buildroot "$IMAGE" sh -c "$1"
    fi
}

case "$1" in
    menuconfig)
        run "make BR2_EXTERNAL=$EXT thinclient_defconfig && make menuconfig"
        ;;
    clean)
        run "make clean"
        ;;
    *)
        run "make BR2_EXTERNAL=$EXT thinclient_defconfig && make"
        echo ""
        echo ">>> Образ флешки: buildroot/output/images/usb.img"
        echo ">>> Залить: ./flash-usb.sh <устройство>  (macOS: /dev/diskN, Linux: /dev/sdX)"
        ;;
esac
