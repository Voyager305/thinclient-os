# Окружение сборки Buildroot (сам Buildroot собирается только под Linux)
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    bc build-essential bzip2 ca-certificates cpio file git gzip \
    libncurses-dev make patch perl python3 rsync sed tar unzip wget \
    dosfstools mtools parted syslinux syslinux-common \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
