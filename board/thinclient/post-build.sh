#!/bin/sh
# Запускается Buildroot'ом после установки всех пакетов в target.
set -e

# Пакет xserver_xorg-server ставит автозапуск X демоном при загрузке.
# У нас X живёт только внутри RDP-сессии (xinit из tc-menu) — убираем,
# иначе пустой X перехватывает экран и меню на tty1 не видно.
rm -f "$TARGET_DIR/etc/init.d/S40xorg"

# Overlay не удаляет из target файлы, которые из него исчезли:
# S40dhcp — хвост переименования, tc-logo — убранный ASCII-логотип
rm -f "$TARGET_DIR/etc/init.d/S40dhcp"
rm -f "$TARGET_DIR/etc/tc-logo"

# Сеть переехала на S51network (после S50flash, чтобы читать tc.conf).
# Убираем скелетный/старый S40network, иначе сеть поднимется дважды.
rm -f "$TARGET_DIR/etc/init.d/S40network"
