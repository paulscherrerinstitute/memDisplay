#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __unix
#define HAVE_byteswap
#define HAVE_stdint
#define HAVE_setjmp_and_signal
#endif

#ifdef vxWorks
#define HAVE_setjmp_and_signal
#define bswap_16(x) WORDSWAP(x)
#define bswap_32(x) LONGSWAP(x)
#endif

#ifdef _WIN32
#define HAVE_stdint
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#endif

#ifdef HAVE_byteswap
#include <byteswap.h>
#endif

#ifndef bswap_64
#define bswap_64(x) ((((x) & 0xff00000000000000ull) >> 56) \
                   | (((x) & 0x00ff000000000000ull) >> 40) \
                   | (((x) & 0x0000ff0000000000ull) >> 24) \
                   | (((x) & 0x000000ff00000000ull) >>  8) \
                   | (((x) & 0x00000000ff000000ull) <<  8) \
                   | (((x) & 0x0000000000ff0000ull) << 24) \
                   | (((x) & 0x000000000000ff00ull) << 40) \
                   | (((x) & 0x00000000000000ffull) << 56))
#endif

#ifdef HAVE_stdint
#include <stdint.h>
#else
#define uint32_t unsigned int
#define uint64_t unsigned long long
#define UINT64_C(c) c ## ULL
#endif

#if !(XOPEN_SOURCE >= 600 || _BSD_SOURCE || _SVID_SOURCE || _ISOC99_SOURCE)
#define strtoull strtoul
#endif

#include "memDisplay.h"
#undef memDisplay

int memDisplayDebug;

int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes)
{
    return fmemDisplay(stdout, base, ptr, wordsize, bytes);
}

/* Split fmemDisplay into three functions to avoid warnings about clobbered variables */
static int fmemDisplay_loop(FILE* file, size_t base, volatile char* ptr, int wordsize, size_t bytes, int addr_wordsize, uint64_t offset, size_t size)
{
    unsigned char buffer[16];
    size_t i, j, len=0;
    unsigned long long x=0;
    int abswordsize=abs(wordsize);

    memset(buffer, ' ', sizeof(buffer));
    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: address %p\n", ptr+(base&15));
    for (i = 0; i < size; i += 16)
    {
        len += fprintf(file, "%0*llx: ", addr_wordsize, (unsigned long long)offset);
        for (j = 0; j < 16; j += abswordsize) {
            if (offset + j < base || i + j >= size)
                len += fprintf(file, "%*c ", 2*abswordsize, ' ');
            else
            {
                switch (wordsize)
                {
                    case 1:
                        x = *(uint8_t*)ptr;
                        *(uint8_t*)(buffer + j) = x;
                        break;
                    case 2:
                        x = *(uint16_t*)ptr;
                        *(uint16_t*)(buffer + j) = x;
                        break;
                    case 4:
                        x = *(uint32_t*)ptr;
                        *(uint32_t*)(buffer + j) = x;
                        break;
                    case 8:
                        x = *(uint64_t*)ptr;
                        *(uint64_t*)(buffer + j) = x;
                        break;
                    case -2:
                        x = bswap_16(*(uint16_t*)ptr);
                        *(uint16_t*)(buffer + j) = x;
                        break;
                    case -4:
                        x = bswap_32(*(uint32_t*)ptr);
                        *(uint32_t*)(buffer + j) = x;
                        break;
                    case -8:
                        x = bswap_64(*(uint64_t*)ptr);
                        *(uint64_t*)(buffer + j) = x;
                        break;
                }
                len += fprintf(file, "%0*llx ", 2*abswordsize, x);
            }
            ptr += abswordsize;
        }
        fprintf(file, "| ");
        for (j = 0; j < 16; j++)
        {
            if (i + j >= size) break;
            len += fprintf(file, "%c", isprint(buffer[j]) ? buffer[j] : '.');
        }
        offset += 16;
        len += fprintf(file, "\n");
    }
    return len;
}

#ifdef HAVE_setjmp_and_signal
/* Setup handler to catch access to invalid addresses (avoids crash) */
#include <signal.h>
#include <setjmp.h>

#ifdef vxWorks
/* have no strsignal() */
const char* strsignal(int sig)
{
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        default:
        {
            static char buffer[16];
            sprintf(buffer, "signal %d", sig);
            return buffer;
        }
    }
}
#endif

static jmp_buf memDisplayFail;
static void memDisplaySigAction(int sig, siginfo_t *info, void *ctx)
{
    printf("\n");
#ifdef si_addr
    fprintf(stderr, "memDisplay failed with %s at address %p.\n", strsignal(sig), info->si_addr);
#else
    fprintf(stderr, "memDisplay failed with %s\n", strsignal(sig));
#endif
    longjmp(memDisplayFail, 1);
}

static int fmemDisplay_wrap(FILE* file, size_t base, volatile void* ptr, int wordsize, size_t bytes, int addr_wordsize, uint64_t offset, size_t size)
{
    int len;
    struct sigaction sa = {{0}}, oldsigsegv, oldsigbus;
    sa.sa_sigaction = memDisplaySigAction;
    sa.sa_flags = SA_SIGINFO;
#ifdef SA_NODEFER
    sa.sa_flags |= SA_NODEFER; /* Do not block signal */
#endif
    sigaction(SIGSEGV, &sa, &oldsigsegv);
    sigaction(SIGBUS, &sa, &oldsigbus);
    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: signal handlers installed for SIGSEGV(%d) and SIGBUS(%d)\n",
            SIGSEGV, SIGBUS);

    if (setjmp(memDisplayFail) != 0)
    {
#ifndef SA_NODEFER
        /* Unblock signal */
        sigset_t sigmask;
        sigemptyset(&sigmask);
        sigaddset(&sigmask, SIGSEGV);
        sigaddset(&sigmask, SIGBUS);
        sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
#endif
        fflush(file);
        len = -1;
    }
    else
    {
        len = fmemDisplay_loop(file, base, ptr, wordsize, bytes, addr_wordsize, offset, size);
    }
    sigaction(SIGSEGV, &oldsigsegv, NULL);
    sigaction(SIGBUS, &oldsigbus, NULL);
    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: signal handlers removed for SIGSEGV and SIGBUS\n");
    return (int)len;
}
#endif /* HAVE_setjmp_and_signal */

int fmemDisplay(FILE* file, size_t base, volatile void* ptr, int wordsize, size_t bytes)
{
    uint64_t offset;
    size_t size, mask;
    int addr_wordsize = ((base + bytes - 1) & UINT64_C(0xffff000000000000)) ? 16 :
                        ((base + bytes - 1) &     UINT64_C(0xffff00000000)) ? 12 :
                        ((base + bytes - 1) &         UINT64_C(0xffff0000)) ? 8 : 4;

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay base=0x%llx ptr=%p wordsize=%d bytes=%llu\n",
            (unsigned long long)base, ptr, wordsize, (unsigned long long)bytes);

    switch (wordsize)
    {
        case 1:
        case 2:
        case 4:
        case 8:
        case -2:
        case -4:
        case -8:
            break;
        default:
            fprintf(stdout, "Invalid data wordsize %d\n", wordsize);
            return -1;
    }

    /* align start */
    mask = abs(wordsize)-1;
    ptr = (volatile void*)((size_t)ptr - (base & mask));
    base &= ~mask;

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay adjusted base=0x%llx ptr=%p wordsize=%d\n",
            (unsigned long long)base, ptr, wordsize);

    /* round down start address to multiple of 16 */
    offset = base & ~15;
    size = bytes + (base & 15);
    ptr = (void*)((size_t)ptr - (base & 15));

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay round down base=0x%llx ptr=%p offset=%llu size=%llu\n",
            (unsigned long long)base, ptr, (unsigned long long)offset, (unsigned long long)size);

#ifdef HAVE_setjmp_and_signal
    return fmemDisplay_wrap(file, base, ptr, wordsize, bytes, addr_wordsize, offset, size);
#else
    return fmemDisplay_loop(file, base, ptr, wordsize, bytes, addr_wordsize, offset, size);
#endif
}

unsigned long long strToSize(const char* str, char** endptr)
{
    char* p = (char*)str, *q;
    unsigned long long size = 0, n;

    if (endptr) *endptr = p;
    if (!str) return 0;
    while (1)
    {
        n = strtoull(p, &q, 0);
        if (p == q)
        {
            size += n;
            if (endptr) *endptr = p;
            return size;
        }
        switch (*(p = q))
        {
            case 'e':
            case 'E':
                n <<= 60; break;
            case 'p':
            case 'P':
                n <<= 50; break;
            case 't':
            case 'T':
                n <<= 40; break;
            case 'g':
            case 'G':
                n <<= 30; break;
            case 'm':
            case 'M':
                n <<= 20; break;
            case 'k':
            case 'K':
                n <<= 10; break;
            default:
                p--;
        }
        p++;
        size += n;
    }
}

char* sizeToStr(unsigned long long size, char* str)
{
    int l;

    l = sprintf(str, "0x%llx", size);
    l += sprintf(str+l, "=");
    if (size >= 1ULL<<60)
        l += sprintf(str+l, "%lluE", size>>50);
    size &= (1ULL<<60)-1;
    if (size >= 1ULL<<50)
        l += sprintf(str+l, "%lluP", size>>40);
    size &= (1ULL<<50)-1;
    if (size >= 1ULL<<40)
        l += sprintf(str+l, "%lluT", size>>30);
    size &= (1ULL<<40)-1;
    if (size >= 1UL<<30)
        l += sprintf(str+l, "%lluG", size>>30);
    size &= (1UL<<30)-1;
    if (size >= 1UL<<20)
        l += sprintf(str+l, "%lluM", size>>20);
    size &= (1UL<<20)-1;
    if (size >= 1UL<<10)
        l += sprintf(str+l, "%lluK", size>>10);
    size &= (1UL<<10)-1;
    if (size > 0)
        l += sprintf(str+l, "%llu", size);
    return str;
}
