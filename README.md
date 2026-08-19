# thinclient-os

Свой минимальный Linux для тонких клиентов на старых Intel Atom.
Грузится с флешки (legacy BIOS), целиком живёт в RAM, показывает консольное
меню выбора сервера (стрелки + Enter) и открывает полноэкранную RDP-сессию.

Стек: Buildroot 2025.08 → ядро 6.12 LTS → busybox init → dialog-меню →
Xorg (поднимается только на время сессии) → FreeRDP 2.11 (RDP 7–10:
Windows Server 2008 R2 – 2022, NLA + TLS 1.2).

Из коробки: hostname `tc-<mac>` (виден в DHCP и RDP-сессии), DHCP на всех
интерфейсах, нативное разрешение любого монитора через KMS (+ VESA
fallback), проброс USB-принтеров (CUPS + канал printer), сетевые утилиты
(iproute2, ping, ethtool, tcpdump), bash-консоль из меню.

## Как это работает

1. BIOS грузит syslinux с FAT32-флешки, тот запускает ядро + initramfs.
2. Вся система (~120–150 МБ) распаковывается в RAM.
3. На tty1 вместо логина крутится `tc-menu` — ASCII-логотип и центрированное
   меню (тёмная тема из `/etc/dialogrc`, кириллица работает — шрифт грузит
   `S35console`). Список серверов — `servers.conf` прямо на флешке.
4. Выбрал сервер → `xinit` поднимает X с единственным клиентом —
   полноэкранным `xfreerdp`. Вышел из Windows → X умирает → снова меню.
5. Пункт «Добавить сервер» перемонтирует флешку rw, дописывает строку в
   `servers.conf` и возвращает ro. Настройки переживают перезагрузку.
6. Пункт «Консоль Linux» даёт root-шелл. Если на флешке лежит файл
   `shell.pass` — сперва спросит пароль (см. ниже).

## Сборка (macOS)

Нужен Docker (Buildroot собирается только под Linux).

```sh
./build.sh          # первый раз 1–3 часа: собирается тулчейн, ядро, Xorg, FreeRDP
```

Результат: `buildroot/output/images/usb.img`.

## Сборка (Linux)

Тот же `./build.sh`: если Docker установлен — соберёт в контейнере, если
нет — нативно. Принудительно нативная сборка: `NATIVE=1 ./build.sh`.

Для нативной сборки нужны пакеты (сборочные + утилиты для post-image.sh,
который собирает usb.img хостовыми mkfs.vfat/mcopy/parted/syslinux):

```sh
# Debian / Ubuntu
sudo apt install build-essential bc bzip2 cpio file git libncurses-dev \
    make patch perl python3 rsync unzip wget \
    dosfstools mtools parted syslinux syslinux-common isolinux xorriso

# Fedora (isolinux входит в пакет syslinux)
sudo dnf install gcc gcc-c++ make bc bzip2 cpio file git ncurses-devel \
    patch perl python3 rsync unzip wget dosfstools mtools parted syslinux xorriso

# Arch (isolinux входит в пакет syslinux)
sudo pacman -S --needed base-devel bc cpio file git ncurses rsync unzip wget \
    dosfstools mtools parted syslinux xorriso
```

Руками, без build.sh, это ровно две команды в каталоге buildroot:

```sh
make BR2_EXTERNAL=/путь/к/thinclient-os thinclient_defconfig
make
```

Нюансы нативной сборки:

- **Не под root** — Buildroot этого не любит, build.sh сам откажется.
- **Место**: ~15–20 ГБ в `buildroot/output` (тулчейн + исходники + сборка).
- **-j указывать не нужно**: Buildroot сам параллелит внутри пакетов
  (`BR2_JLEVEL=0` = все ядра).
- **Повторные сборки с нуля** сильно ускоряет ccache: включи `BR2_CCACHE=y`
  в menuconfig.
- Заливка флешки тем же `./flash-usb.sh /dev/sdX` (устройство смотри в
  `lsblk`), либо вручную:
  `sudo dd if=buildroot/output/images/usb.img of=/dev/sdX bs=4M conv=fsync status=progress`.

Первая сборка — момент, где надо проверить конфиг: имена опций Buildroot
иногда меняются между релизами, и если какой-то `BR2_PACKAGE_*` из
defconfig не применился, make молча его пропустит. Поэтому после
`./build.sh menuconfig` убедись, что выбраны: Xorg server, драйверы
fbdev/intel/libinput, xinit, freerdp (клиент X11), dialog.

## Заливка на флешку

```sh
diskutil list                # найти флешку, например /dev/disk4
./flash-usb.sh /dev/disk4    # ВСЁ на флешке будет стёрто
```

Подойдёт любая флешка от 512 МБ.

## Проверка без железа

Сборка делает два артефакта в `buildroot/output/images/`:

- `usb.img` — боевой образ для флешки (FAT32, список серверов пишется);
- `thinclient.iso` — hybrid ISO для тестов: VirtualBox подключает его как
  CD-привод напрямую, balenaEtcher пишет на флешку как есть. Файловая
  система ISO только для чтения — добавленные через меню серверы не
  сохраняются; постоянный список для ISO зашивается пересборкой
  (`servers.conf.sample`).

VirtualBox: VM «Other Linux (32-bit)», RAM 1024 МБ, EFI выключен,
`thinclient.iso` — в CD-привод. Сеть работает из коробки (драйверы e1000 и
pcnet32 включены). Для проверки RDP-сессии выбери графический контроллер
VBoxVGA и добавь `vga=791` в строку APPEND (framebuffer для X).

QEMU:

```sh
qemu-system-i386 -m 1024 -cdrom buildroot/output/images/thinclient.iso
# или боевой образ:
qemu-system-i386 -m 1024 -drive format=raw,file=buildroot/output/images/usb.img
```

## Настройка на клиенте

- В BIOS выставить загрузку с USB (или boot-меню — обычно F11/F12/Esc).
- Список серверов правится либо через пункт меню, либо руками: воткни
  флешку в любой комп и отредактируй `servers.conf` (формат `имя;ip`).
- Пункт «Консоль Linux» по умолчанию открыт для всех. Чтобы закрыть его
  паролем, положи на флешку файл `shell.pass` с sha256-хэшем пароля:

  ```sh
  printf '%s' 'мой-пароль' | shasum -a 256 | cut -c1-64 > shell.pass
  ```

## Структура

```
configs/thinclient_defconfig       конфиг Buildroot (пакеты, ядро, initramfs)
board/thinclient/
  linux.fragment                   фрагмент конфига ядра (графика, сеть, usb, vfat)
  syslinux.cfg                     конфиг загрузчика
  servers.conf.sample              стартовый список серверов
  post-image.sh                    сборка usb.img (MBR + FAT32 + syslinux)
  rootfs-overlay/
    etc/inittab                    tc-menu на tty1, отладочный getty на tty2
    etc/dialogrc                   тема оформления меню
    etc/init.d/S35console          консольный шрифт с кириллицей
    etc/init.d/S40network          lo + DHCP на всех интерфейсах
    etc/init.d/S50flash            монтирование флешки в /mnt/flash
    usr/bin/tc-menu                меню (dialog): серверы, добавление, консоль
    usr/bin/tc-session             запуск xfreerdp внутри X
build.sh                           сборка в Docker
flash-usb.sh                       заливка образа на флешку (macOS)
```

## Тонкости железа

- **Разрядность.** Собирается под i686 — работает на любом Atom, включая
  32-битные N270/N280. Если весь парк 64-битный (D525/N2600+), в
  defconfig можно переключить на `BR2_x86_64=y` — прироста почти не даст,
  так что i686 — нормальный универсальный выбор.
- **Cedar Trail (N2600/N2800/D2500/D2700).** Графика GMA 3600 (PowerVR)
  без нормального драйвера; ядерный `gma500` даёт KMS-framebuffer без
  ускорения — для консоли и RDP этого достаточно. Если экран чёрный,
  добавь `vga=791` в APPEND в `syslinux.cfg`.
- **Старые BIOS и USB.** Если клиент не видит флешку — проверь в BIOS
  режим USB-HDD, отключи «USB legacy floppy». Разметка в образе обычная
  MBR с выравниванием 1 МиБ, подавляющее большинство BIOS её ест.
- **RDP к Windows Server 2008 R2 – 2022.** FreeRDP 2.11 умеет NLA и
  TLS 1.2 — покрывает весь этот ряд. Нюанс только с самым древним
  2008 (не R2) без апдейтов: ему может хотеться TLS 1.0, который
  OpenSSL 3 по умолчанию запрещает — решается на сервере включением
  TLS 1.2 (KB4019276). В `tc-session` стоит `/cert:ignore`
  (сертификаты не проверяются) — для доверенной локалки нормально,
  для строгого режима убери флаг и положи CA-серт.

## Что подкрутить под себя

- `tc-session` — параметры xfreerdp: `/bpp:16` (скорость на слабом железе),
  `+clipboard`, звук (`/sound`), проброс принтеров и т.п.
- `tc-menu` — можно добавить пункт удаления/редактирования серверов.
- `etc/dialogrc` — цвета меню (8 базовых цветов ncurses + жирность).
- `inittab` — убрать отладочную консоль на tty2 в боевой сборке
  (пункт «Консоль Linux» в меню при этом остаётся).
