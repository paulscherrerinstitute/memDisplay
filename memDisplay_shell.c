#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <epicsTypes.h>
#include <epicsString.h>
#include <epicsVersion.h>

#ifndef BASE_VERSION
/* 3.14+ */
#include <epicsStdioRedirect.h>
#include <devLibVME.h>
#include <iocsh.h>
#else
#define EPICS_3_13
#endif

#ifdef vxWorks
#include <sysLib.h>
#endif


#if !(XOPEN_SOURCE >= 600 || _BSD_SOURCE || _SVID_SOURCE || _ISOC99_SOURCE)
#define strtoull strtoul
#endif

#ifdef WITH_SYMBOLNAME
#include "symbolname.h"
#endif

#include <epicsExport.h>
#include "memDisplay.h"

epicsExportAddress(int, memDisplayDebug);

volatile void* memDisplayVMEAddrHandler(size_t addr, size_t size, size_t addrSpace)
{
    volatile void* ptr;
#ifndef EPICS_3_13
    static int first_time = 1;
    if (!pdevLibVirtualOS) {
        printf("No VME support available.\n");
        return NULL;
    }
    if (first_time)
    {
        /* Make sure that devLibInit has been called (call will fail) */
        /* We want to map but not register and thus block the address space.*/
        devRegisterAddress(NULL, (epicsAddressType)0, 0, 0, NULL);
        first_time = 0;
    }
    return pdevLibVirtualOS->pDevMapAddr((epicsAddressType)addrSpace, 0, addr, size, &ptr) == S_dev_success ? ptr : NULL;
#else
    int status = sysBusToLocalAdrs((int)addrSpace, (char*)addr, (char**)&ptr);
    return status == OK ? ptr : NULL;
#endif
    
}

struct addressHandlerItem {
    const char* str;
    memDisplayAddrHandler handler;
    size_t usr;
    struct addressHandlerItem* next;
} *addressHandlerList = NULL;

void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr)
{
    struct addressHandlerItem* item =
        (struct addressHandlerItem*) malloc(sizeof(struct addressHandlerItem));
    if (!item)
    {
        printf("Out of memory.\n");
        return;
    }
    item->str = epicsStrDup(str);
    item->handler = handler;
    item->usr = usr;
    item->next = addressHandlerList;
    addressHandlerList = item;       
}

struct addressTranslatorItem {
    memDisplayAddrTranslator translator;
    struct addressTranslatorItem* next;
} *addressTranslatorList = NULL;

void memDisplayInstallAddrTranslator(memDisplayAddrTranslator translator)
{
    struct addressTranslatorItem* item =
        (struct addressTranslatorItem*) malloc(sizeof(struct addressTranslatorItem));
    if (!item)
    {
        printf("Out of memory.\n");
        return;
    }
    item->translator = translator;
    item->next = addressTranslatorList;
    addressTranslatorList = item;       
}

typedef struct {volatile void* ptr; size_t offs;} remote_addr_t;
static remote_addr_t stringToAddr(const char* addrstr, size_t offs, size_t size)
{
    unsigned long long addr = 0;
    size_t len;
    volatile void* ptr = NULL;
    struct addressHandlerItem* hitem;
    struct addressTranslatorItem* titem;
    char *p, *q;

    for (hitem = addressHandlerList; hitem != NULL; hitem = hitem->next)
    {
        len = strlen(hitem->str);
        if (strncmp(addrstr, hitem->str, len) == 0 &&
            (addrstr[len] == 0 || addrstr[len] == ':'))
        {
            if (addrstr[len])
            {
                addr = strToSize(addrstr+len+1, &q) + offs;
                if (*q != 0)
                {
                    /* rubbish at end */
                    printf("Invalid address %s.\n", addrstr);
                    return (remote_addr_t){NULL, 0};
                }
                if (addr & ~(unsigned long long)((size_t)-1))
                {
                    printf("Too large address %s for %u bit.\n", addrstr, (int) sizeof(void*)*8);
                    return (remote_addr_t){NULL, 0};
                }
            }
            errno = 0;
            ptr = hitem->handler(addr, size, hitem->usr);
            if (!ptr)
            {
                if (errno)
                    fprintf(stderr, "Getting address 0x%llx in %s address space failed: %s\n",
                        addr, hitem->str, strerror(errno));
                else
                    fprintf(stderr, "Getting address 0x%llx in %s address space failed.\n",
                        addr, hitem->str);
            }
            return (remote_addr_t){ptr, addr};
        }
    }
    for (titem = addressTranslatorList; titem != NULL; titem = titem->next)
    {
        if ((p = strrchr((char*)addrstr, ':')) != NULL)
        {
            addr = strToSize(p+1, &q);
        }
        ptr = titem->translator(addrstr, offs, size);
        if (ptr) return (remote_addr_t){ptr, addr + offs};
    }

    /* no aspace */
#ifdef WITH_SYMBOLNAME
    if (!addr && (ptr = symbolAddr(addrstr)) != NULL)
    {
        /* global variable name */
        return (remote_addr_t){ptr + offs, (size_t)ptr + offs};
    }
#endif
    addr = strToSize(addrstr, &q) + offs;
    if (q > addrstr)
    {
        /* something like a number */
        if (*q != 0)
        {
            /* rubbish at end */
            printf("Invalid address %s.\n", addrstr);
            return (remote_addr_t){NULL, 0};
        }
        if (addr & ~(unsigned long long)((size_t)-1))
        {
            printf("Too large address %s for %u bit.\n", addrstr, (int) sizeof(void*)*8);
            return (remote_addr_t){NULL, 0};
        }
        return (remote_addr_t){(void*)(size_t) addr, addr};
    }
    fprintf(stderr, "Unknown address %s\n", addrstr);
    return (remote_addr_t){NULL, 0};
}

void md(const char* addressStr, int wordsize, int bytes)
{
    remote_addr_t addr;
    static remote_addr_t old_addr = {0};
    static int old_wordsize = 2;
    static int old_bytes = 0x80;
    static char* old_addressStr;
    static size_t old_offs;
 
    if ((!addressStr && !old_addr.ptr) || (addressStr && addressStr[0] == '?'))
    {
        printf("md \"[addrspace:]address\", [wordsize={1|2|4|8|-2|-4|-8}], [bytes]\n");
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
        old_wordsize = 2;
    }
    else
    {
        addressStr = old_addressStr;
    }
    if (bytes == 0) bytes = old_bytes;
    if (wordsize == 0) wordsize = old_wordsize;
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
static const iocshArg mdArg0 = { "[addrspace:]address", iocshArgString };
static const iocshArg mdArg1 = { "[wordsize={1|2|4|8|-2|-4|-8}]", iocshArgInt };
static const iocshArg mdArg2 = { "[bytes]", iocshArgInt };
static const iocshArg *mdArgs[] = {&mdArg0, &mdArg1, &mdArg2};
static const iocshFuncDef mdDef = { "md", 3, mdArgs };

static void mdFunc(const iocshArgBuf *args)
{
    md(args[0].sval, args[1].ival, args[2].ival);
}

static const iocshArg devReadProbeArg0 = { "wordsize={1|2|4|8}", iocshArgInt };
static const iocshArg devReadProbeArg1 = { "[{A16|A24|A32|CRCSR}:]address", iocshArgString };
static const iocshArg *devReadProbeArgs[] = {&devReadProbeArg0, &devReadProbeArg1 };
static const iocshFuncDef devReadProbeDef = { "devReadProbe", 2, devReadProbeArgs };

static void devReadProbeFunc(const iocshArgBuf *args)
{
    union {epicsUInt8 u8; epicsUInt16 u16; epicsUInt32 u32;} val;
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
            printf("Cannot get VME mapping for %s.\n", address);
            break;
        case S_dev_badArgument:
            printf("Illegal word size %d\n", wordsize);
            break;
        case S_dev_noDevice:
            printf("Bus error at %s\n", address);
            break;
        case S_dev_success:
            switch (wordsize)
            {
                case 1:
                    printf("%#x\n", val.u8);
                    break;
                case 2:
                    printf("%#x\n", val.u16);
                    break;
                default:
                    printf("%#x\n", val.u32);
            }
            break;
        default:
            printf("Error.\n");
    }
}

static const iocshArg devWriteProbeArg0 = { "wordsize={1|2|4|8}", iocshArgInt };
static const iocshArg devWriteProbeArg1 = { "[{A16|A24|A32|CRCSR}:]address", iocshArgString };
static const iocshArg devWriteProbeArg2 = { "value", iocshArgInt };
static const iocshArg *devWriteProbeArgs[] = {&devWriteProbeArg0, &devWriteProbeArg1 };
static const iocshFuncDef devWriteProbeDef = { "devWriteProbe", 3, devWriteProbeArgs };

static void devWriteProbeFunc(const iocshArgBuf *args)
{
    union {epicsUInt8 u8; epicsUInt16 u16; epicsUInt32 u32;} val;
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
            val.u8 = args[2].ival;
            break;
        case 2:
            val.u16 = args[2].ival;
            break;
        default:
            val.u32 = args[2].ival;
    }
    switch (devWriteProbe(wordsize, addr.ptr, &val))
    {
        case S_dev_addressNotFound:
            printf("Cannot get VME mapping for %s.\n", address);
            break;
        case S_dev_badArgument:
            printf("Illegal word size %d.\n", wordsize);
            break;
        case S_dev_noDevice:
            printf("Bus error at %s\n", address);
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
    memDisplayInstallAddrHandler("CRCSR", memDisplayVMEAddrHandler, atVMECSR);
    memDisplayInstallAddrHandler("A32",   memDisplayVMEAddrHandler, atVMEA32);
    memDisplayInstallAddrHandler("A24",   memDisplayVMEAddrHandler, atVMEA24);
    memDisplayInstallAddrHandler("A16",   memDisplayVMEAddrHandler, atVMEA16);

    iocshRegister(&mdDef, mdFunc);
    iocshRegister(&devReadProbeDef, devReadProbeFunc);
    iocshRegister(&devWriteProbeDef, devWriteProbeFunc);
}

epicsExportRegistrar(memDisplayRegistrar);

#endif /* EPICS_3_13 */
