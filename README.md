# Thin Client OS

Минимальный Linux для тонких клиентов: загружается с флешки, целиком живёт
в RAM и умеет ровно одно — показать список серверов и открыть полноэкранную
RDP-сессию к выбранному. Работает практически на любом x86 — от 32-битных
нетбуков 2008 года до современных ПК с UEFI без CSM (см. «Поддерживаемое
железо»).

Стек: Buildroot 2025.08 · ядро 6.12 LTS · busybox init · собственный
ncurses-лаунчер · Xorg (поднимается только на время сессии) · FreeRDP 2.11.

## Возможности

- **Загрузка с флешки**: гибридные образы — legacy BIOS (syslinux) и UEFI
  x64/ia32 (grub-efi); вся система распаковывается в RAM, флешку после
  загрузки можно вынуть.
- **Любое x86-железо**: от 32-битных Atom (N270/N280, Cedar Trail) до
  современных десктопов — нативная графика на Intel/NVIDIA/старых AMD,
  VESA-fallback на остальном, широкая поддержка сетевых карт.
- **Интерфейс**: тёмный ncurses-лаунчер — список серверов по центру,
  сервисные кнопки (Console / Reboot / Off) столбиком внизу, hostname и IP
  в углу. Управление двумя стрелками и Enter; добавление сервера из меню.
- **RDP**: FreeRDP 2.11 — Windows Server 2008 R2 … 2022 (RDP 7–10, NLA,
  TLS 1.2), нативное разрешение монитора, динамическая смена разрешения,
  буфер обмена, проброс принтеров (CUPS + канал `printer`).
- **Сеть**: DHCP на всех интерфейсах, hostname `tc-<mac без разделителей>`
  (виден в DHCP leases и в RDP-сессии), самовосстановление — интерфейс,
  появившийся после загрузки, подхватывается без перезагрузки.
- **Загрузочная заставка**: тёмный экран с прогресс-баром (psplash),
  служебный вывод спрятан на tty2.
- **Консоль**: полноценный bash (промпт `root@tc-…:путь#`), опционально за
  паролем; на борту iproute2, ping, ethtool, tcpdump, lsusb, htop, mc,
  nano, vim, vi.
- **SSH**: sshd (dropbear) слушает 22-й порт с загрузки — `ssh root@tc-…`,
  пароль по умолчанию `changeme` (меняется в defconfig,
  `BR2_TARGET_GENERIC_ROOT_PASSWD`). Тот же пароль — у логина на tty2.
  Нюанс ISO/CD: система живёт в RAM, host-ключ sshd генерится на каждую
  загрузку — ssh-клиент будет предупреждать о смене ключа.
- **Персистентность**: список серверов хранится на FAT-разделе флешки и
  переживает перезагрузки (в варианте usb.img).

## Быстрый старт

Готовые образы лежат в [Releases](../../releases):

| Образ | Для чего | Как записать | Список серверов |
|---|---|---|---|
| `thinclient.iso` | флешка (основной вариант) и виртуалки | balenaEtcher / CD в VM | на флешке — редактируемый (раздел `TCDATA` создаётся при первой загрузке); с CD в VM — read-only |
| `usb.img` | альтернатива для флешки | `dd` / `./flash-usb.sh` | редактируется из меню |

1. Записать образ на флешку (≥512 МБ): ISO — балена-этчером как есть,
   img — `sudo dd if=usb.img of=/dev/sdX bs=4M conv=fsync status=progress`.
2. В BIOS клиента выставить загрузку с USB (boot-меню: обычно F11/F12/Esc).
3. Готово: заставка → меню → Enter на сервере → RDP.

## Использование

- **Меню**: стрелки вверх/вниз ходят по кругу «серверы → + Add server →
  Console → Reboot → Off», Enter выполняет.
- **Добавить сервер**: пункт `+ Add server`, два поля (имя; ip или
  ip:порт). Сохраняется на флешку (см. ниже); с CD в виртуалке — до
  перезагрузки.
- **Конфиг-раздел `TCDATA` (флешка с ISO)**: сам ISO9660 — формат
  компакт-диска, только для чтения, поэтому при **первой загрузке**
  система сама создаёт в свободном месте флешки FAT-раздел `TCDATA` и
  переносит туда стартовый `servers.conf`. Дальше и меню пишет туда, и
  руками правится: воткни флешку в комп (Linux, macOS, Windows 10+ —
  старые Windows видят только первый раздел) и отредактируй
  `servers.conf` на разделе `TCDATA` (формат `имя;ip`, `#` — комментарии).
  Повторная прошивка Этчером стирает раздел — он пересоздастся со
  стартовым списком. На usb.img конфиг лежит на его собственном
  FAT-разделе — правится так же.
- **Стартовый список** (что окажется в образе и на свежем `TCDATA`):
  `board/thinclient/servers.conf.sample`, фиксируется при сборке.
- **Консоль Linux**: пункт Console — root-шелл (bash). Чтобы закрыть его
  паролем, положи на флешку файл `shell.pass` с sha256-хэшем:
  `printf '%s' 'пароль' | shasum -a 256 | cut -c1-64 > shell.pass`
- **Отладка**: tty2 (Ctrl+Alt+F2) — логин-консоль и весь вывод загрузки.
- **Правка файлов на флешке из консоли**: `tc-edit` (servers.conf) или
  `tc-edit <файл>` — сам перемонтирует rw и вернёт read-only после
  сохранения; голый nano упрётся в read-only.
- **Параметры RDP** (цветность, звук, принтеры): `tc-session` в overlay,
  см. «Структура».

## Сборка из исходников

Сборка идёт в Docker (на macOS — обязательно, Buildroot собирается только
под Linux; на Linux Docker опционален). Первый прогон 1–3 часа, дальше
инкрементально.

```sh
git clone https://github.com/Voyager305/thinclient-os && cd thinclient-os
./build.sh                 # клонирует Buildroot и собирает оба образа
./build.sh menuconfig      # покрутить конфиг Buildroot
NATIVE=1 ./build.sh        # Linux: нативная сборка без Docker
```

Результат: `buildroot/output/images/{thinclient.iso,usb.img}`.
Заливка: `./flash-usb.sh <устройство>` (macOS `/dev/diskN`, Linux `/dev/sdX`).

Пакеты для нативной сборки на Linux (сборочные + утилиты для post-image):

```sh
# Debian / Ubuntu
sudo apt install build-essential bc bzip2 cpio file git libncurses-dev \
    make patch perl python3 rsync unzip wget \
    dosfstools mtools parted syslinux syslinux-common isolinux xorriso

# Fedora (isolinux входит в syslinux)
sudo dnf install gcc gcc-c++ make bc bzip2 cpio file git ncurses-devel \
    patch perl python3 rsync unzip wget dosfstools mtools parted syslinux xorriso

# Arch (isolinux входит в syslinux)
sudo pacman -S --needed base-devel bc cpio file git ncurses rsync unzip wget \
    dosfstools mtools parted syslinux xorriso
```

Нюансы: не собирать под root; ~15–20 ГБ места; `-j` не указывать (Buildroot
параллелит сам); повторные чистые сборки ускоряет `BR2_CCACHE=y`. На Apple
Silicon сборка идёт в docker-томе (bind-mount VirtioFS роняет Docker Desktop
на глубоких каталогах configure-тестов), а `usb.img` дособирается коротким
amd64-шагом — всё это `build.sh` делает сам.

## Проверка без железа

- **UTM (Apple Silicon)**: Create VM → **Emulate** → Other → i386, RAM
  1024 МБ, `thinclient.iso` как CD. Сетевую карту выбрать **e1000** или
  **rtl8139** (режим Shared Network). VirtualBox на M-маках x86-гостей не
  умеет — только UTM/QEMU.
- **QEMU**: `qemu-system-i386 -m 1024 -cdrom thinclient.iso`
- **VirtualBox (x86-хост)**: VM «Other Linux (32-bit)», EFI выключен, ISO в
  CD-привод.

## Структура репозитория

```
configs/thinclient_defconfig       конфиг Buildroot (пакеты, ядро, initramfs)
package/tc-launcher/               ncurses-лаунчер (C, пакет BR2_EXTERNAL)
patches/psplash/                   тёмная тема заставки, без логотипа
board/thinclient/
  linux.fragment                   конфиг ядра: графика (i915/gma500/virt),
                                   сетевухи, USB, vfat/iso9660, usblp
  syslinux.cfg                     загрузчик (BIOS), консоль на tty2
  servers.conf.sample              стартовый список серверов
  post-build.sh                    чистка target (автозапуск X и т.п.)
  post-image.sh                    сборка usb.img (MBR+FAT32) и hybrid ISO
  rootfs-overlay/
    etc/inittab                    tc-menu на tty1, getty на tty2
    etc/init.d/S00splash           заставка psplash
    etc/init.d/S35console          консольные шрифты
    etc/init.d/S40network          сеть: hostname tc-<mac>, DHCP (tc-netup)
    etc/init.d/S50flash            носитель конфигов: поиск/создание TCDATA
    etc/init.d/S98splashdone       гасит заставку перед меню
    etc/profile.d/tc-prompt.sh     промпт user@host:cwd
    root/.bashrc, .vimrc           окружение root-консоли
    usr/bin/tc-menu                обёртка: исполняет выбор лаунчера
    usr/bin/tc-netup               идемпотентный подъём сети
    usr/bin/tc-session             запуск xfreerdp внутри X
build.sh                           сборка (Docker/нативно)
flash-usb.sh                       заливка usb.img на флешку (macOS/Linux)
```

## Поддерживаемое железо

Примерный список того, что работает нативно. Система собрана под i686 и
запускается на любом x86-процессоре — и 32-, и 64-битном (переключение
сборки на `BR2_x86_64=y` выигрыша почти не даёт).

**Загрузка**
| Способ | Чем | Примечание |
|---|---|---|
| Legacy BIOS / CSM | syslinux, isolinux | любые машины с 2000-х |
| UEFI x64 | grub-efi (bootx64) | современные ПК, CSM не нужен |
| UEFI ia32 | grub-efi (bootia32) | планшетно-неттопные Atom (Bay Trail и т.п.) |

**Видео (нативное разрешение через KMS)**
| Семейство | Драйвер | Примеры |
|---|---|---|
| Intel встройка | i915 | GMA 900/950/3100/4500, HD Graphics — атомные неттопы и десктопные чипсеты |
| Intel Poulsbo/Cedar Trail | gma500 | GMA 500/600/3600/3650 (N2600/N2800/D2500/D2700), без ускорения |
| NVIDIA | nouveau | GeForce 6xxx … GTX 7xx полноценно; новее — вывод без ускорения |
| AMD (старые) | radeon | Radeon от R300 до HD 8xxx (до GCN 1-го поколения) |
| Виртуалки | bochs/cirrus/vmwgfx/virtio | QEMU/UTM, VirtualBox, VMware |
| Всё остальное | VESA 1024x768 | fallback на BIOS; под UEFI — GOP в нативном разрешении |

**Сеть (проводная)**: Intel (e100, e1000/e1000e, igb, igc), Realtek
(RTL8139, RTL8169/8168 + прошивки), Broadcom (tg3), Atheros/Qualcomm
(atl1c, atl1e, alx), Marvell (sky2, skge), VIA Rhine, nForce (forcedeth),
AMD PCnet, virtio-net. **USB-ethernet**: Realtek r8152/8153, ASIX
(AX88172…AX88179), CDC Ethernet — типовые «свистки».

**Прочее**: USB 1.1/2.0/3.x (OHCI/UHCI/EHCI/XHCI), встроенные SD-ридеры
(SDHCI), USB-принтеры (usblp + CUPS), клавиатуры PS/2 и USB. RAM — от
512 МБ. Wi-Fi нет сознательно — тонкий клиент живёт на проводе.

Если железки нет в списке — скорее всего она всё равно заведётся через
VESA-fallback; если не завелась сеть — воткни USB-ethernet свисток из
списка выше.

## Тонкости

- **RDP к Windows Server 2008 (не R2) без апдейтов**: ему может хотеться
  TLS 1.0, который OpenSSL 3 запрещает — включи на сервере TLS 1.2
  (KB4019276). 2008 R2 и новее работают из коробки.
- **Сертификаты**: в `tc-session` стоит `/cert:ignore` — для доверенной
  локалки нормально; для строгого режима убери флаг и положи CA-сертификат.

## Траблшутинг

| Симптом | Причина / решение |
|---|---|
| BIOS не видит флешку | Режим USB-HDD в BIOS, отключить «USB legacy floppy» |
| Чёрный экран после загрузчика | Убери `vga=791` из APPEND (`syslinux.cfg`) — редкий BIOS без такого VESA-режима |
| В углу `thinclient  no ip` | Нет сетевухи или её драйвера: в VM выбери e1000/rtl8139; на железе смотри `ip a` в консоли |
| Меню не появилось | Ctrl+Alt+F2 → логи загрузки на tty2 |
| ssh предупреждает о смене host-ключа | Норма для ISO/CD: система в RAM, ключ генерится на каждую загрузку |
| RDP не подключается | С консоли: `ping <сервер>`, `tcpdump -i eth0 port 3389`; лог сессии в `/var/log/xsession.log` |

## Лицензия

[MIT](LICENSE). Патчи в `patches/` наследуют лицензии патчуемых пакетов;
состав лицензий собранного образа — `make legal-info` в Buildroot.
