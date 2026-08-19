# Thin Client OS

Минимальный Linux для тонких клиентов на старых Intel Atom: загружается с
флешки, целиком живёт в RAM и умеет ровно одно — показать список серверов и
открыть полноэкранную RDP-сессию к выбранному.

Стек: Buildroot 2025.08 · ядро 6.12 LTS · busybox init · собственный
ncurses-лаунчер · Xorg (поднимается только на время сессии) · FreeRDP 2.11.

## Возможности

- **Загрузка с флешки** на legacy BIOS (syslinux/isolinux), вся система
  распаковывается в RAM — флешку после загрузки можно вынуть.
- **Старое железо**: сборка под i686, работает на любых Atom, включая
  32-битные N270/N280 и проблемные Cedar Trail (GMA 3600 — через
  KMS-драйвер `gma500`).
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
  паролем; на борту iproute2, ping, ethtool, tcpdump, lsusb.
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
    usr/bin/tc-menu                обёртка: исполняет выбор лаунчера
    usr/bin/tc-netup               идемпотентный подъём сети
    usr/bin/tc-session             запуск xfreerdp внутри X
build.sh                           сборка (Docker/нативно)
flash-usb.sh                       заливка usb.img на флешку (macOS/Linux)
```

## Тонкости железа

- **Разрядность**: i686 работает на всех Atom. Если весь парк 64-битный
  (D525/N2600+), можно переключить defconfig на `BR2_x86_64=y`, выигрыш
  минимален.
- **Cedar Trail (N2600/N2800/D2500/D2700)**: у GMA 3600 нет нормального
  драйвера; ядро использует `gma500` (KMS без ускорения) — для консоли и
  RDP достаточно.
- **RDP к Windows Server 2008 (не R2) без апдейтов**: ему может хотеться
  TLS 1.0, который OpenSSL 3 запрещает — включи на сервере TLS 1.2
  (KB4019276). 2008 R2 и новее работают из коробки.
- **Сертификаты**: в `tc-session` стоит `/cert:ignore` — для доверенной
  локалки нормально; для строгого режима убери флаг и положи CA-сертификат.

## Траблшутинг

| Симптом | Причина / решение |
|---|---|
| BIOS не видит флешку | Режим USB-HDD в BIOS, отключить «USB legacy floppy» |
| Чёрный экран после загрузчика | Добавь `vga=791` в APPEND (`syslinux.cfg`) |
| В углу `thinclient  no ip` | Нет сетевухи или её драйвера: в VM выбери e1000/rtl8139; на железе смотри `ip a` в консоли |
| Меню не появилось | Ctrl+Alt+F2 → логи загрузки на tty2 |
| RDP не подключается | С консоли: `ping <сервер>`, `tcpdump -i eth0 port 3389`; лог сессии в `/var/log/xsession.log` |

## Лицензия

[MIT](LICENSE). Патчи в `patches/` наследуют лицензии патчуемых пакетов;
состав лицензий собранного образа — `make legal-info` в Buildroot.
