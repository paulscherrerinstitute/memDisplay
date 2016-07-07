#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <epicsTypes.h>
#include <epicsString.h>
#include <epicsVersion.h>

#ifndef BASE_VERSION
/* 3.14+ */
#include <devLibVME.h>
#include <iocsh.h>
#else
#define EPICS_3_13
#endif

#ifdef vxWorks
#include <sysLib.h>
#endif

#ifdef WITH_SYMBOLNAME
#include "symbolname.h"
#endif

#include "memDisplay.h"
#include <epicsExport.h>

static volatile void* VME_AddrHandler(size_t addr, size_t size, size_t addrSpace)
{
    volatile void* ptr;
#ifndef vxWorks
    static int first_time = 1;
    if (!pdevLibVirtualOS) {
        printf("No VME support available.\n");
        return NULL;
    }
    if (first_time)
    {
        /* Make sure that devLibInit has been called (call will fail) */
        /* We want to map but not register and thus block the address space.*/
        devRegisterAddress(NULL, 0, 0, 0, NULL);
        first_time = 0;
    }
    return pdevLibVirtualOS->pDevMapAddr(addrSpace, 0, addr, size, &ptr) == S_dev_success ? ptr : NULL;
#else
    int status = sysBusToLocalAdrs((int)addrSpace, (char*)addr, (char**)&ptr);
    return status == OK ? ptr : NULL;
#endif
    
}

struct addressHandlerMap {
    const char* str;
    memDisplayAddrHandler handler;
    size_t usr;
    struct addressHandlerMap* next;
} *addressHandlerList = NULL;

void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr)
{
    struct addressHandlerMap* map = malloc(sizeof(struct addressHandlerMap));
    if (!map)
    {
        printf("Out of memory.\n");
        return;
    }
    map->str = epicsStrDup(str);
    map->handler = handler;
    map->usr = usr;
    map->next = addressHandlerList;
    addressHandlerList = map;       
}

typedef struct {volatile void* ptr; size_t offs;} remote_addr_t;
static remote_addr_t stringToAddr(const char* addrstr, size_t offs, size_t size)
{
    size_t addr = 0;
    volatile void* ptr;
    char* p;

#ifdef vxWorks
    if (addrstr < sysMemTop() && (size_t)addrstr >0x100000)
    {
#endif
        if ((p = strchr(addrstr, ':')) != NULL)
        {
            struct addressHandlerMap* map;
            addr = strtoul(p+1, NULL, 0) + offs;
            for (map = addressHandlerList; map != NULL; map = map->next)
            {
                if (strlen(map->str) == p-addrstr &&
                    strncmp(addrstr, map->str, p-addrstr) == 0)
                {
                    ptr = map->handler(addr, size, map->usr);
                    if (!ptr)
                    {
                        printf("Invalid address in %s address space.\n", map->str);
                        return (remote_addr_t){NULL, 0};
                    }
                    return (remote_addr_t){ptr, addr};
                }
            }
            printf("Invalid address space %.*s.\n", (int)(p-addrstr), addrstr);
            return (remote_addr_t){NULL, 0};
        }
#ifdef WITH_SYMBOLNAME
        addr = (size_t)(ptr = (char*)symbolAddr(addrstr) + offs);
#endif
        if (!addr) ptr = (volatile void*)(addr = strtoul(addrstr, NULL, 0) + offs);
#ifdef vxWorks
    }
    if (!addr) return (remote_addr_t){(volatile void*)addrstr, (size_t)addrstr};
#else
    if (!addr) printf("Invalid address %s.\n", addrstr);
#endif
    return (remote_addr_t){ptr, addr};
}

void md(const char* addressStr, int wordsize, int bytes)
{
    remote_addr_t addr;
    static remote_addr_t old_addr = {0};
    static int old_wordsize = 2;
    static int old_bytes = 0x80;
    static char* old_addressStr;
    static size_t old_offs;
 
    if (bytes == 0) bytes = old_bytes;
    if (wordsize == 0) wordsize = old_wordsize;
    if ((!addressStr && !old_addr.ptr) || (addressStr && addressStr[0] == '?'))
    {
        struct addressHandlerMap* map;

        printf("md \"[addrspace:]address\", [wordsize={1|2|4|8|-2|-4|-8}], [bytes]");
        printf("Installed address spaces:\n");
        for (map = addressHandlerList; map != NULL; map = map->next)
            printf("%s ", map->str);
        printf("\n");
        return;
    }
    if (addressStr)
    {
#ifdef vxWorks
        old_addressStr = (char*)addressStr;
#else
        free(old_addressStr);
        old_addressStr = epicsStrDup(addressStr);
#endif
        old_offs = 0;
    }
    else
    {
        addressStr = old_addressStr;
    }
    addr = stringToAddr(addressStr, old_offs, bytes);
    if (!addr.ptr)
        return;
    if (memDisplay(addr.offs, addr.ptr, wordsize, bytes) < 0)
    {
        old_addr = (remote_addr_t){0};
        return;
    }
    old_offs += bytes;
    old_wordsize = wordsize;
    old_bytes = bytes;
    old_addr = addr;
}

#ifndef EPICS_3_13
static const iocshFuncDef mdDef =
    { "md", 3, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]address", iocshArgString },
    &(iocshArg) { "[wordsize={1|2|4|8|-2|-4|-8}]", iocshArgInt },
    &(iocshArg) { "[bytes]", iocshArgInt },
}};

static void mdFunc(const iocshArgBuf *args)
{
    md(args[0].sval, args[1].ival, args[2].ival);
}
    
static const iocshFuncDef devReadProbeDef =
    { "devReadProbe", 2, (const iocshArg *[]) {
    &(iocshArg) { "wordsize={1|2|4|8}", iocshArgInt },
    &(iocshArg) { "[{A16|A24|A32|CRCSR}:]address", iocshArgString },
}};

static void devReadProbeFunc(const iocshArgBuf *args)
{
    epicsUInt32 val = 0;
    remote_addr_t addr;
    int wordsize = args[0].ival;
    const char* address = args[1].sval;
        
    if (!address)
    {
        iocshCmd("help devReadProbe");
        return;
    }
    addr = stringToAddr(address, 0, wordsize);
    
    switch (devReadProbe(wordsize, addr.ptr, &val))
    {
        case S_dev_addressNotFound:
            printf("%p is not a VME address.\n", addr.ptr);
            break;
        case S_dev_badArgument:
            printf("Illegal word size %d\n", wordsize);
            break;
        case S_dev_noDevice:
            printf("Bus error at %p\n", addr.ptr);
            break;
        case S_dev_success:
            switch (wordsize)
            {
                case 1:
                    printf("%#x\n", *(epicsUInt8*)&val);
                    break;
                case 2:
                    printf("%#x\n", *(epicsUInt16*)&val);
                    break;
                default:
                    printf("%#x\n", val);
            }
            break;
        default:
            printf("Error.\n");
    }
}

static const iocshFuncDef devWriteProbeDef =
    { "devWriteProbe", 3, (const iocshArg *[]) {
    &(iocshArg) { "wordsize={1|2|4|8}", iocshArgInt },
    &(iocshArg) { "[{A16|A24|A32|CRCSR}:]address", iocshArgString },
    &(iocshArg) { "value", iocshArgInt },
}};

static void devWriteProbeFunc(const iocshArgBuf *args)
{
    epicsUInt32 val;
    remote_addr_t addr;
    int wordsize = args[0].ival;
    const char* address = args[1].sval;
    
    if (!address)
    {
        iocshCmd("help devWriteProbe");
        return;
    }
    addr = stringToAddr(address, 0, wordsize);
    
    switch (wordsize)
    {
        case 1:
            *(epicsUInt8*)&val = args[2].ival;
            break;
        case 2:
            *(epicsUInt16*)&val = args[2].ival;
            break;
        default:
            val = args[2].ival;
    }
    switch (devWriteProbe(wordsize, addr.ptr, &val))
    {
        case S_dev_addressNotFound:
            printf("%p is not a VME address.\n", addr.ptr);
            break;
        case S_dev_badArgument:
            printf("Illegal word size %d.\n", wordsize);
            break;
        case S_dev_noDevice:
            printf("Bus error at %p\n", addr.ptr);
            break;
        case S_dev_success:
            printf("Success.\n");
            break;
        default:
            printf("Error.\n");
    }
}

static void memDisplayRegistrar(void)
{
#ifndef vxWorks
    memDisplayInstallAddrHandler("A16",   VME_AddrHandler, atVMEA16);
    memDisplayInstallAddrHandler("A24",   VME_AddrHandler, atVMEA24);
    memDisplayInstallAddrHandler("A32",   VME_AddrHandler, atVMEA32);
    memDisplayInstallAddrHandler("CRCSR", VME_AddrHandler, atVMECSR);
#endif

    iocshRegister(&mdDef, mdFunc);
    iocshRegister(&devReadProbeDef, devReadProbeFunc);
    iocshRegister(&devWriteProbeDef, devWriteProbeFunc);
}

epicsExportRegistrar(memDisplayRegistrar);
#endif

#ifdef vxWorks
static void memDisplayInit() __attribute__((constructor));
static void memDisplayInit()
{
    memDisplayInstallAddrHandler("A16",   VME_AddrHandler, VME_AM_SUP_SHORT_IO);
    memDisplayInstallAddrHandler("A24",   VME_AddrHandler, VME_AM_STD_SUP_DATA);
    memDisplayInstallAddrHandler("A32",   VME_AddrHandler, VME_AM_EXT_SUP_DATA);
#ifdef VME_AM_CSR
    memDisplayInstallAddrHandler("CRCSR", VME_AddrHandler, VME_AM_CSR);
#endif
}

#endif
