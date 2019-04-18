TOP=.
include $(TOP)/configure/CONFIG

memDisplayDev_DBD += base.dbd
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

# library
LIBRARY = memDisplay

INC += memDisplay.h
DBDS = memDisplay.dbd

LIB_SRCS += memDisplay.c
LIB_SRCS += memDisplay_shell.c

ifndef BASE_3_14
LIB_SRCS += memDisplay_init_3_13.cc
endif

ifdef symbolname
# have the optional symbolname package?
LIB_LIBS += symbolname
USR_CFLAGS_DEFAULT+= -D WITH_SYMBOLNAME
endif

include $(TOP)/configure/RULES
