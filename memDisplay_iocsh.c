#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <epicsTypes.h>
#include <epicsString.h>
#include <devLibVME.h>
#include <iocsh.h>

/* comment this out if you dont have/want symbolname.h */
#include "symbolname.h"

#include "memDisplay.h"
#include <epicsExport.h>

static volatile void* VME_AddrHandler(size_t addr, size_t size, size_t addrSpace)
{
    volatile void* ptr;
    if (!pdevLibVirtualOS) {
        printf("No VME support available.\n");
        return NULL;
    }
    return pdevLibVirtualOS->pDevMapAddr(addrSpace, 0, addr, size, &ptr) == S_dev_success ? ptr : NULL;
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
static remote_addr_t stringToAddr(const char* addrstr, size_t size)
{
    size_t addr = 0;
    volatile void* ptr;
    char* p;

    if ((p = strchr(addrstr, ':')) != NULL)
    {
        struct addressHandlerMap* map;
        addr = strtoul(p+1, NULL, 0);
        for (map = addressHandlerList; map != NULL; map = map->next)
        {
            if (strncmp(addrstr, map->str, p-addrstr) == 0)
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
#ifdef symbolname_h
    addr = (size_t)(ptr = symbolAddr(addrstr));
#endif
    if (!addr) ptr = (volatile void*)(addr = strtoul(addrstr, NULL, 0));
    if (!addr) printf("Invalid address %s.\n", addrstr);
    return (remote_addr_t){ptr, addr};
}
    
static const iocshFuncDef memDisplayDef =
    { "memDisplay", 3, (const iocshArg *[]) {
    &(iocshArg) { "[{A16|A24|A32|CRCSR}:]address", iocshArgString },
    &(iocshArg) { "[wordsize={1|2|4|8|-2|-4|-8}]", iocshArgInt },
    &(iocshArg) { "[bytes]", iocshArgInt },
}};

static void memDisplayFunc (const iocshArgBuf *args)
{
    const char* ptr = args[0].sval;
    int wordsize = args[1].ival;
    int bytes = args[2].ival;
    remote_addr_t addr;
    static remote_addr_t old_addr = {0};
    static int old_wordsize = 2;
    static int old_bytes = 0x80;
 
    if (bytes == 0) bytes = old_bytes;
    if (wordsize == 0) wordsize = old_wordsize;
    if (args[0].sval == NULL)
    {
        if (!old_addr.ptr)
        {
            iocshCmd("help memDisplay");
            return;
        }
        addr = old_addr;
    }
    else
        addr = stringToAddr(ptr, bytes);
    if (!addr.ptr)
        return;
    if (memDisplay(addr.offs, addr.ptr, wordsize, bytes) < 0)
    {
        old_addr = (remote_addr_t){0};
        return;
    }
    old_wordsize = wordsize;
    old_bytes = bytes;
    old_addr.ptr = addr.ptr + bytes;
    old_addr.offs = addr.offs + bytes;
}

static const iocshFuncDef devReadProbeDef =
    { "devReadProbe", 2, (const iocshArg *[]) {
    &(iocshArg) { "wordsize={1|2|4|8}", iocshArgInt },
    &(iocshArg) { "[{A16|A24|A32|CRCSR}:]address", iocshArgString },
}};

static void devReadProbeFunc (const iocshArgBuf *args)
{
    epicsUInt32 val = 0;
    remote_addr_t addr;
    int wordsize = args[0].ival;
    const char* ptr = args[1].sval;
        
    if (!ptr)
    {
        iocshCmd("help devReadProbe");
        return;
    }
    addr = stringToAddr(ptr, wordsize);
    
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

static void devWriteProbeFunc (const iocshArgBuf *args)
{
    epicsUInt32 val;
    remote_addr_t addr;
    int wordsize = args[0].ival;
    const char* ptr = args[1].sval;
    
    if (!ptr)
    {
        iocshCmd("help devWriteProbe");
        return;
    }
    addr = stringToAddr(ptr, wordsize);
    
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
    /* Make sure that devLibInit has been called (call will fail) */
    if (pdevLibVirtualOS) devRegisterAddress(NULL, 0, 0, 0, NULL);

    memDisplayInstallAddrHandler("A16",       VME_AddrHandler, atVMEA16);
    memDisplayInstallAddrHandler("A24",       VME_AddrHandler, atVMEA24);
    memDisplayInstallAddrHandler("A32",       VME_AddrHandler, atVMEA32);
    memDisplayInstallAddrHandler("CRCSR",     VME_AddrHandler, atVMECSR);

    iocshRegister(&memDisplayDef, memDisplayFunc);
    iocshRegister(&devReadProbeDef, devReadProbeFunc);
    iocshRegister(&devWriteProbeDef, devWriteProbeFunc);
}

epicsExportRegistrar(memDisplayRegistrar);
