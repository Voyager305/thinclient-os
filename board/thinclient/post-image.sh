#!/bin/sh
# Запускается Buildroot'ом после сборки образов (внутри Linux-контейнера).
# Собирает готовый образ флешки: MBR + загрузочный FAT32-раздел с syslinux.
# Результат: output/images/usb.img — заливается на флешку через dd.
set -e

BOARD_DIR="$(dirname "$0")"
PART_MB=256
IMG="$BINARIES_DIR/usb.img"
PART="$BINARIES_DIR/part.img"

# 1. FAT32-раздел с ядром, initramfs, конфигом syslinux и списком серверов
dd if=/dev/zero of="$PART" bs=1M count=$PART_MB status=none
mkfs.vfat -F 32 -n THINCLIENT "$PART" >/dev/null
mcopy -i "$PART" "$BINARIES_DIR/bzImage"        ::/bzImage
mcopy -i "$PART" "$BINARIES_DIR/rootfs.cpio.gz" ::/initrd.gz
mcopy -i "$PART" "$BOARD_DIR/syslinux.cfg"      ::/syslinux.cfg
mcopy -i "$PART" "$BOARD_DIR/servers.conf.sample" ::/servers.conf
syslinux --install "$PART"

# 2. Диск целиком: MBR-разметка, раздел с отступом 1 МиБ, boot-флаг
dd if=/dev/zero of="$IMG" bs=1M count=$((PART_MB + 1)) status=none
parted -s "$IMG" mklabel msdos mkpart primary fat32 1MiB 100% set 1 boot on

# 3. Загрузочный код syslinux в MBR
MBR_BIN=""
for p in /usr/lib/syslinux/mbr/mbr.bin /usr/lib/syslinux/bios/mbr.bin \
         /usr/lib/SYSLINUX/mbr.bin /usr/share/syslinux/mbr.bin; do
    [ -f "$p" ] && MBR_BIN="$p" && break
done
[ -n "$MBR_BIN" ] || { echo "mbr.bin не найден — установи syslinux-common"; exit 1; }
dd if="$MBR_BIN" of="$IMG" bs=440 count=1 conv=notrunc status=none

# 4. Кладём раздел на место
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none
rm -f "$PART"

echo ">>> Готово: $IMG ($(du -h "$IMG" | cut -f1))"
