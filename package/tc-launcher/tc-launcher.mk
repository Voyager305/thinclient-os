################################################################################
#
# tc-launcher — консольный UI тонкого клиента (см. src/tc-launcher.c)
#
################################################################################

TC_LAUNCHER_VERSION = 1.0
TC_LAUNCHER_SITE = $(BR2_EXTERNAL_THINCLIENT_PATH)/package/tc-launcher/src
TC_LAUNCHER_SITE_METHOD = local
TC_LAUNCHER_DEPENDENCIES = ncurses
TC_LAUNCHER_LICENSE = MIT

define TC_LAUNCHER_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/tc-launcher $(@D)/tc-launcher.c -lncursesw
endef

define TC_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tc-launcher \
		$(TARGET_DIR)/usr/bin/tc-launcher
endef

$(eval $(generic-package))
