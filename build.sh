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
#
# В Docker-режиме сборка идёт в именованном томе (thinclient-output), а не в
# bind-mount: во-первых, это сильно быстрее, во-вторых, глубокие деревья
# каталогов из configure-тестов (gnulib getcwd) роняют VirtioFS Docker Desktop
# на macOS. Готовые образы копируются обратно в buildroot/output/images/.
set -e
cd "$(dirname "$0")"

BR_VERSION=2025.08
IMAGE=thinclient-build
VOLUME=thinclient-output

if [ ! -d buildroot ]; then
    echo ">>> Клонирую Buildroot $BR_VERSION..."
    git clone --depth 1 --branch "$BR_VERSION" \
        https://gitlab.com/buildroot.org/buildroot.git buildroot
fi

if [ "$(uname -s)" = "Linux" ] && { [ -n "$NATIVE" ] || ! command -v docker >/dev/null 2>&1; }; then
    MODE=native
    MAKE="make BR2_EXTERNAL=$PWD"
else
    MODE=docker
    MAKE="make -C /src/buildroot O=/build BR2_EXTERNAL=/src"
fi

if [ "$MODE" = native ]; then
    if [ "$(id -u)" = 0 ]; then
        echo "Не собирай Buildroot под root — запусти от обычного пользователя" >&2
        exit 1
    fi
    # post-image.sh собирает образы хостовыми утилитами — проверим, что они есть
    for tool in gcc make mkfs.vfat mcopy parted syslinux; do
        command -v "$tool" >/dev/null 2>&1 || {
            echo "Не хватает '$tool'. Список пакетов для своего дистрибутива — в README (раздел 'Сборка (Linux)')" >&2
            exit 1
        }
    done
fi

docker_run() {
    # -t только при живом терминале (иначе падает под CI/фоновым запуском)
    TTY_FLAG=""
    [ -t 0 ] && TTY_FLAG="-it"
    # FORCE_UNSAFE_CONFIGURE: в контейнере сборка идёт под root,
    # иначе configure некоторых host-пакетов (tar) отказывается работать.
    # DOCKER_RUN_EXTRA — дополнительные флаги docker run снаружи,
    # например DOCKER_RUN_EXTRA="--cpuset-cpus 0-7" чтобы умерить параллелизм.
    # --name: защита от двух сборок в один том — вторая честно откажется
    docker run --rm --name thinclient-build-run $TTY_FLAG $DOCKER_RUN_EXTRA \
        -v "$PWD:/src" -v "$VOLUME:/build" \
        -e FORCE_UNSAFE_CONFIGURE=1 \
        -w /src/buildroot "$IMAGE" sh -c "$1"
}

run() {
    if [ "$MODE" = native ]; then
        ( cd buildroot && sh -c "$1" )
    else
        docker build -t "$IMAGE" .
        docker_run "$1"
    fi
}

copy_images() {
    # из docker-тома — в привычный buildroot/output/images/ на хосте
    [ "$MODE" = docker ] || return 0
    docker_run "mkdir -p /src/buildroot/output/images && \
        cp /build/images/*.img /build/images/*.iso /build/images/bzImage \
           /build/images/rootfs.cpio.gz /src/buildroot/output/images/ 2>/dev/null; true"
}

case "$1" in
    menuconfig)
        run "$MAKE thinclient_defconfig && $MAKE menuconfig"
        ;;
    clean)
        run "$MAKE clean"
        ;;
    *)
        # tc-launcher-dirclean: у «локальных» пакетов Buildroot не замечает
        # изменения исходников — пересобираем лаунчер всегда (это секунды)
        run "$MAKE thinclient_defconfig && $MAKE tc-launcher-dirclean && $MAKE"
        copy_images
        # На arm64-хосте (Apple Silicon) post-image пропускает usb.img: утилита
        # syslinux бывает только под x86. Дособираем секундным amd64-шагом (Rosetta).
        NEED_IMG=0
        [ ! -f buildroot/output/images/usb.img ] && NEED_IMG=1
        [ buildroot/output/images/usb.img -ot buildroot/output/images/rootfs.cpio.gz ] 2>/dev/null && NEED_IMG=1
        if [ "$MODE" = docker ] && [ "$NEED_IMG" = 1 ]; then
            echo ">>> Дособираю usb.img в amd64-контейнере..."
            docker build --platform linux/amd64 -t "$IMAGE-amd64" .
            TTY_FLAG=""; [ -t 0 ] && TTY_FLAG="-it"
            docker run --rm $TTY_FLAG --platform linux/amd64 -v "$PWD:/src" \
                -e BINARIES_DIR=/src/buildroot/output/images \
                "$IMAGE-amd64" /src/board/thinclient/post-image.sh
        fi
        echo ""
        echo ">>> Образы: buildroot/output/images/usb.img (флешка) и thinclient.iso (VirtualBox/Etcher)"
        echo ">>> Залить img: ./flash-usb.sh <устройство>  (macOS: /dev/diskN, Linux: /dev/sdX)"
        ;;
esac
