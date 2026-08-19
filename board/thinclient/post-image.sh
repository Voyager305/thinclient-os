#!/bin/sh
# Запускается Buildroot'ом после сборки образов (внутри Linux-контейнера),
# либо отдельно: BINARIES_DIR=... ./post-image.sh
# Собирает два артефакта в $BINARIES_DIR:
#   usb.img        — боевой образ флешки (MBR + FAT32 + syslinux), список
#                    серверов записываемый; нужен бинарник `syslinux` (x86-only
#                    пакет — на arm64-хосте шаг пропускается, см. build.sh)
#   thinclient.iso — hybrid ISO для VirtualBox (как CD) и balenaEtcher;
#                    файловая система только для чтения
set -e

BOARD_DIR="$(cd "$(dirname "$0")" && pwd)"
PART_MB=256

# UEFI: bootx64/bootia32 генерятся grub-mkstandalone (конфиг вшивается
# внутрь образа загрузчика). Buildroot тут не помощник: на i686 он не
# собирает x86_64-efi. Нет grub-утилит в системе — тихо живём BIOS-only.
EFI_DIR="$BINARIES_DIR/efi-gen"
gen_efi() {
    [ -f "$EFI_DIR/bootx64.efi" ] || [ -f "$EFI_DIR/bootia32.efi" ] && return 0
    command -v grub-mkstandalone >/dev/null 2>&1 || return 1
    mkdir -p "$EFI_DIR"
    [ -d /usr/lib/grub/x86_64-efi ] && grub-mkstandalone -O x86_64-efi \
        -o "$EFI_DIR/bootx64.efi" \
        "boot/grub/grub.cfg=$BOARD_DIR/grub-efi.cfg" 2>/dev/null
    [ -d /usr/lib/grub/i386-efi ] && grub-mkstandalone -O i386-efi \
        -o "$EFI_DIR/bootia32.efi" \
        "boot/grub/grub.cfg=$BOARD_DIR/grub-efi.cfg" 2>/dev/null
    [ -f "$EFI_DIR/bootx64.efi" ] || [ -f "$EFI_DIR/bootia32.efi" ]
}

# докладывает EFI/BOOT/boot*.efi в FAT-образ $1 (через mtools)
add_efi_to_fat() {
    gen_efi || return 0
    mmd -i "$1" ::/EFI ::/EFI/BOOT 2>/dev/null || true
    [ -f "$EFI_DIR/bootx64.efi" ]  && mcopy -o -i "$1" "$EFI_DIR/bootx64.efi"  ::/EFI/BOOT/
    [ -f "$EFI_DIR/bootia32.efi" ] && mcopy -o -i "$1" "$EFI_DIR/bootia32.efi" ::/EFI/BOOT/
    true
}

# отдельный маленький FAT для El Torito EFI-загрузки ISO
build_efiboot_img() {
    gen_efi || return 1
    EFIBOOT="$BINARIES_DIR/efiboot.img"
    dd if=/dev/zero of="$EFIBOOT" bs=1M count=8 status=none
    mkfs.vfat -F 12 -n EFIBOOT "$EFIBOOT" >/dev/null
    add_efi_to_fat "$EFIBOOT"
}

build_img() {
    IMG="$BINARIES_DIR/usb.img"
    PART="$BINARIES_DIR/part.img"

    # 1. FAT32-раздел с ядром, initramfs, конфигом syslinux и списком серверов
    dd if=/dev/zero of="$PART" bs=1M count=$PART_MB status=none
    mkfs.vfat -F 32 -n THINCLIENT "$PART" >/dev/null
    mcopy -i "$PART" "$BINARIES_DIR/bzImage"          ::/bzImage
    mcopy -i "$PART" "$BINARIES_DIR/rootfs.cpio.gz"   ::/initrd.gz
    mcopy -i "$PART" "$BOARD_DIR/syslinux.cfg"        ::/syslinux.cfg
    mcopy -i "$PART" "$BOARD_DIR/servers.conf.sample" ::/servers.conf
    add_efi_to_fat "$PART"
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
}

build_iso() {
    ISOLINUX_BIN=""
    for p in /usr/lib/ISOLINUX/isolinux.bin /usr/share/syslinux/isolinux.bin /usr/lib/syslinux/bios/isolinux.bin; do
        [ -f "$p" ] && ISOLINUX_BIN="$p" && break
    done
    ISOHDPFX=""
    for p in /usr/lib/ISOLINUX/isohdpfx.bin /usr/share/syslinux/isohdpfx.bin /usr/lib/syslinux/bios/isohdpfx.bin; do
        [ -f "$p" ] && ISOHDPFX="$p" && break
    done
    LDLINUX=""
    for p in /usr/lib/syslinux/modules/bios/ldlinux.c32 /usr/share/syslinux/ldlinux.c32 /usr/lib/syslinux/bios/ldlinux.c32; do
        [ -f "$p" ] && LDLINUX="$p" && break
    done

    if ! command -v xorriso >/dev/null 2>&1 || [ -z "$ISOLINUX_BIN" ] || [ -z "$ISOHDPFX" ]; then
        echo ">>> ISO пропущен: нужны xorriso и isolinux (см. README)" >&2
        return 0
    fi

    ISO="$BINARIES_DIR/thinclient.iso"
    ISO_DIR="$BINARIES_DIR/iso-root"
    rm -rf "$ISO_DIR"
    mkdir -p "$ISO_DIR/isolinux"
    cp "$BINARIES_DIR/bzImage"          "$ISO_DIR/bzImage"
    cp "$BINARIES_DIR/rootfs.cpio.gz"   "$ISO_DIR/initrd.gz"
    cp "$BOARD_DIR/servers.conf.sample" "$ISO_DIR/servers.conf"
    cp "$BOARD_DIR/syslinux.cfg"        "$ISO_DIR/isolinux/isolinux.cfg"
    cp "$ISOLINUX_BIN"                  "$ISO_DIR/isolinux/"
    [ -n "$LDLINUX" ] && cp "$LDLINUX"  "$ISO_DIR/isolinux/"

    # UEFI-запись добавляется, только если grub-efi собрался
    EFI_OPTS=""
    if build_efiboot_img; then
        cp "$BINARIES_DIR/efiboot.img" "$ISO_DIR/efiboot.img"
        EFI_OPTS="-eltorito-alt-boot -e efiboot.img -no-emul-boot -isohybrid-gpt-basdat"
    fi

    xorriso -as mkisofs -quiet -o "$ISO" \
        -isohybrid-mbr "$ISOHDPFX" \
        -b isolinux/isolinux.bin -c isolinux/boot.cat \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        $EFI_OPTS \
        -V THINCLIENT "$ISO_DIR"
    rm -rf "$ISO_DIR"
    echo ">>> Готово: $ISO ($(du -h "$ISO" | cut -f1))"
}

if command -v syslinux >/dev/null 2>&1; then
    build_img
else
    # пакет syslinux есть только под x86; на arm64-хосте (Apple Silicon)
    # usb.img дособерёт build.sh отдельным amd64-контейнером
    echo ">>> usb.img пропущен: нет утилиты syslinux (соберётся amd64-шагом)" >&2
fi
build_iso
