#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
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

#ifdef WITH_SYMBOLNAME
#include "symbolname.h"
#endif

#include <epicsExport.h>
#include "memDisplay.h"

epicsExportAddress(int, memDisplayDebug);

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

static void memDisplayRegistrar(void)
{
    iocshRegister(&mdDef, mdFunc);
}

epicsExportRegistrar(memDisplayRegistrar);

#endif /* EPICS_3_13 */
