# Окружение сборки Buildroot (сам Buildroot собирается только под Linux)
FROM debian:bookworm

# Пакет syslinux (утилита-установщик) существует только под x86 — на arm64
# ставим всё остальное, а usb.img дособирается amd64-контейнером (см. build.sh)
RUN apt-get update && apt-get install -y --no-install-recommends \
    bc build-essential bzip2 ca-certificates cpio file git gzip \
    libncurses-dev make patch perl python3 rsync sed tar unzip wget \
    dosfstools mtools parted syslinux-common isolinux xorriso \
    && if [ "$(dpkg --print-architecture)" = "amd64" ]; then \
        apt-get install -y --no-install-recommends syslinux \
            grub-common grub-efi-amd64-bin grub-efi-ia32-bin; \
    fi \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
