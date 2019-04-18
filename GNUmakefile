ifeq ($(wildcard /ioc/tools/driver.makefile),)
$(warning It seems you do not have the PSI build environment. Remove GNUmakefile.)
include Makefile
else
include /ioc/tools/driver.makefile

BUILDCLASSES += Linux
SOURCES = memDisplay.c memDisplay_shell.c
USR_CFLAGS += -D WITH_SYMBOLNAME

HEADERS = memDisplay.h

endif
