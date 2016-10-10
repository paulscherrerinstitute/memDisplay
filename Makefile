TOP=.
include $(TOP)/configure/CONFIG

memDisplayDev_DBD += base.dbd
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

# library
LIBRARY = memDisplay
LIB_SRCS += memDisplay_registerRecordDeviceDriver.cpp

INC += memDisplay.h
DBDS = memDisplay.dbd

LIB_SRCS += memDisplay.c
LIB_SRCS += memDisplay_iocsh.c

USR_CFLAGS_WIN = -nil-
USR_CFLAGS_DEFAULT+= -D WITH_SYMBOLNAME

include $(TOP)/configure/RULES
