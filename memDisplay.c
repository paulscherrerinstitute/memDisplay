#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#ifdef __unix
#define HAVE_byteswap
#define HAVE_stdint
#define HAVE_setjmp_and_signal
#endif

#ifdef vxWorks
#define bswap_16(x) (MSB(x) | (LSB(x) << 8))
#define bswap_32(x) LONGSWAP(x)
#include <tickLib.h>
#include <sysLib.h>
#define clock_gettime(clock, stamp) do {int tick = tickGet(); int rate = sysClkRateGet(); (stamp)->tv_sec=tick/rate; (stamp)->tv_nsec=1000000000ULL*(tick-(stamp)->tv_sec*rate)/rate;} while(0)
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
#define bswap_64(x) (bswap_32(x)<<32 | bswap_32(x>>32))
#endif

#ifdef HAVE_stdint
#include <stdint.h>
#else
#define UINT64_C(c) c ## ULL
#endif

#ifdef HAVE_setjmp_and_signal
#include <signal.h>
#include <setjmp.h>
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

#ifdef HAVE_setjmp_and_signal
/* Setup handler to catch access to invalid addresses (avoids crash) */

static sigjmp_buf addressFailEnv;
static struct sigaction oldsigsegv, oldsigbus;

static void signalsOff()
{
    sigaction(SIGSEGV, &oldsigsegv, NULL);
    sigaction(SIGBUS, &oldsigbus, NULL);
    if (memDisplayDebug)
        fprintf(stderr, "Signal handlers removed for SIGSEGV(%d) and SIGBUS(%d)\n",
            SIGSEGV, SIGBUS);
}

static void sigAction(int sig, siginfo_t *info, void *ctx)
{
#ifdef si_addr
    fprintf(stderr, "%s at address %p.\n", strsignal(sig), info->si_addr);
#else
    fprintf(stderr, "%s\n", strsignal(sig));
#endif
    signalsOff();
    siglongjmp(addressFailEnv, 1);
}

static void signalsOn()
{
    struct sigaction sa = {{0}};
    sa.sa_sigaction = sigAction;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &oldsigsegv);
    sigaction(SIGBUS, &sa, &oldsigbus);
    if (memDisplayDebug)
        fprintf(stderr, "Signal handlers installed for SIGSEGV(%d) and SIGBUS(%d)\n",
            SIGSEGV, SIGBUS);
}
#endif

int fmemDisplay(FILE* file, size_t base, volatile void* ptr, int wordsize, size_t bytes)
{
    unsigned char buffer[16];
    unsigned long long offset;
    size_t i, j, len = 0;
    unsigned long long x = 0;
    int abswordsize = abs(wordsize);
    size_t size, mask;

    int addr_wordsize = ((base + bytes - 1) & UINT64_C(0xffff000000000000)) ? 16 :
                        ((base + bytes - 1) &     UINT64_C(0xffff00000000)) ? 12 :
                        ((base + bytes - 1) &         UINT64_C(0xffff0000)) ? 8 : 4;

    switch (wordsize)
    {
        case 1:
        case 2:
        case 4:
        case 8:
        case -1:
        case -2:
        case -4:
        case -8:
            break;
        default:
            fprintf(stdout, "Invalid data wordsize %d\n", wordsize);
            return -1;
    }

    memset(buffer, ' ', sizeof(buffer));

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: base=0x%llx ptr=%p wordsize=%d bytes=%llu\n",
            (unsigned long long)base, ptr, wordsize, (unsigned long long)bytes);

    /* align start to wordsize */
    mask = abs(wordsize)-1;
    ptr = (volatile void*)((size_t)ptr - (base & mask));
    base &= ~mask;

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: Adjusted base=0x%llx ptr=%p wordsize=%d\n",
            (unsigned long long)base, ptr, wordsize);

    /* round down start address to multiple of 16 */
    offset = base & ~15;
    size = bytes + (base & 15);
    ptr = (void*)((size_t)ptr - (base & 15));

    if (memDisplayDebug)
        fprintf(stderr, "memDisplay: Round down base=0x%llx ptr=%p offset=%llu size=%llu\n",
            (unsigned long long)base, ptr, offset, (unsigned long long)size);

#ifdef HAVE_setjmp_and_signal
    signalsOn();
    if (sigsetjmp(addressFailEnv, 1) != 0) {
        fprintf(file, "<aborted>\n");
        return -1;
    }
#endif

    for (i = 0; i < size; i += 16)
    {
        len += fprintf(file, "%0*llx: ", addr_wordsize, offset);
        for (j = 0; j < 16; j += abswordsize) {
            if (offset + j < base || i + j >= size)
                len += fprintf(file, "%*c ", 2*abswordsize, ' ');
            else
            {
                switch (wordsize)
                {
                    case 1:
                    case -1:
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
                        x = *(uint16_t*)ptr;
                        x = bswap_16(x);
                        *(uint16_t*)(buffer + j) = x;
                        break;
                    case -4:
                        x = *(uint32_t*)ptr;
                        x = bswap_32(x);
                        *(uint32_t*)(buffer + j) = x;
                        break;
                    case -8:
                        x = *(uint64_t*)ptr;
                        x = bswap_64(x);
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
#ifdef HAVE_setjmp_and_signal
    signalsOff();
#endif
    return len;
}

int memfill(volatile void* address, int pattern, size_t size, int wordsize, int increment)
{
    size_t i;
    int abswordsize;

    if (!wordsize) {
        if (pattern & 0xffff0000) wordsize=4;
        else if (pattern & 0xff00) wordsize=2;
        else wordsize=1;
    }
    abswordsize = abs(wordsize);

#ifdef HAVE_setjmp_and_signal
    signalsOn();
    if (sigsetjmp(addressFailEnv, 1) != 0)
        return -1;
#endif
    for (i = 0; i < size/abswordsize; i++)
    {
        switch (wordsize)
        {
            case 0:
            case 1:
            case -1:
                ((volatile uint8_t*)address)[i] = pattern;
                break;
            case 2:
                ((volatile uint16_t*)address)[i] = pattern;
                break;
            case 4:
                ((volatile uint32_t*)address)[i] = pattern;
                break;
            case -2:
                ((volatile uint16_t*)address)[i] = bswap_16(pattern);
                break;
            case -4:
                ((volatile uint32_t*)address)[i] = bswap_32(pattern);
                break;
            default:
                fprintf(stderr, "Illegal wordsize %d: must be 1, 2, 4, -2, -4\n", wordsize);
#ifdef HAVE_setjmp_and_signal
                signalsOff();
#endif
                return -1;
        }
        pattern += increment;
    }
#ifdef HAVE_setjmp_and_signal
    signalsOff();
#endif
    return 0;
}

int memcopy(const volatile void* source, volatile void* dest, size_t size, int wordsize)
{
    size_t i;
    struct timespec start, finished;
    double sec;

#ifdef HAVE_setjmp_and_signal
    signalsOn();
    if (sigsetjmp(addressFailEnv, 1) != 0)
        return -1;
#endif
    clock_gettime(CLOCK_MONOTONIC, &start);
    switch (wordsize)
    {
        case 0:
            memcpy((void*)dest, (const void*)source, size);
            break;
        case 1:
        case -1:
            for (i = 0; i < size; i++)
                ((volatile uint8_t*)dest)[i] = ((const volatile uint8_t*)source)[i];
            break;
        case 2:
            for (i = 0; i < size/2; i++)
                ((volatile uint16_t*)dest)[i] = ((const volatile uint16_t*)source)[i];
            break;
        case 4:
            for (i = 0; i < size/4; i++)
                ((volatile uint32_t*)dest)[i] = ((const volatile uint32_t*)source)[i];
            break;
        case 8:
            for (i = 0; i < size/8; i++)
                ((volatile uint64_t*)dest)[i] = ((const volatile uint64_t*)source)[i];
            break;
        case -2:
            for (i = 0; i < size/2; i++)
                ((volatile uint16_t*)dest)[i] = bswap_16(((const volatile uint16_t*)source)[i]);
            break;
        case -4:
            for (i = 0; i < size/4; i++)
                ((volatile uint32_t*)dest)[i] = bswap_32(((const volatile uint32_t*)source)[i]);
            break;
        case -8:
            for (i = 0; i < size/8; i++)
                ((volatile uint64_t*)dest)[i] = bswap_64(((const volatile uint64_t*)source)[i]);
            break;
        default:
            fprintf(stderr, "Illegal wordsize %d: must be 1, 2, 4, 8, -2, -4, -8\n", wordsize);
#ifdef HAVE_setjmp_and_signal
            signalsOff();
#endif
            return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &finished);
#ifdef HAVE_setjmp_and_signal
    signalsOff();
#endif
    finished.tv_sec  -= start.tv_sec;
    if ((finished.tv_nsec -= start.tv_nsec) < 0)
    {
        finished.tv_nsec += 1000000000;
        finished.tv_sec--;
    }
    sec = finished.tv_sec + finished.tv_nsec * 1e-9;
    printf("%u %sB / %.3f msec (%.1f MiB/s = %.1f MB/s)\n",
        (unsigned) (size >= 0x00100000 ? (size >> 20) : size >= 0x00000400 ? (size >> 10) : size),
        size >= 0x00100000 ? "Mi" : size >= 0x00000400 ? "Ki" : "",
        sec * 1000, size/sec/0x00100000, size/sec/1000000);
    return 0;
}

int memcomp(const volatile void* source, const volatile void* dest, size_t size, int wordsize)
{
    size_t i;
    int abswordsize = abs(wordsize);
    unsigned long long s = 0, d = 0;

#ifdef HAVE_setjmp_and_signal
    signalsOn();
    if (sigsetjmp(addressFailEnv, 1) != 0)
        return -1;
#endif
    switch (wordsize)
    {
        case 0:
        case 1:
        case -1:
            for (i = 0; i < size; i++)
            {
                s = ((const volatile uint8_t*)source)[i];
                d = ((const volatile uint8_t*)dest)[i];
                if (s != d) break;
            }
            break;
        case 2:
            for (i = 0; i < size; i+=2)
            {
                s = ((const volatile uint16_t*)source)[i/2];
                d = ((const volatile uint16_t*)dest)[i/2];
                if (s != d) break;
            }
            break;
        case 4:
            for (i = 0; i < size; i+=4)
            {
                s = ((const volatile uint32_t*)source)[i/4];
                d = ((const volatile uint32_t*)dest)[i/4];
                if (s != d) break;
            }
            break;
        case 8:
            for (i = 0; i < size; i+=8)
            {
                s = ((const volatile uint64_t*)source)[i/8];
                d = ((const volatile uint64_t*)dest)[i/8];
                if (s != d) break;
            }
            break;
        case -2:
            for (i = 0; i < size; i+=2)
            {
                s = bswap_16(((const volatile uint16_t*)source)[i/2]);
                d = ((const volatile uint16_t*)dest)[i/2];
                if (s != d) break;
            }
            break;
        case -4:
            for (i = 0; i < size; i+=4)
            {
                s = bswap_32(((const volatile uint32_t*)source)[i/4]);
                d = ((const volatile uint32_t*)dest)[i/4];
                if (s != d) break;
            }
            break;
        case -8:
            for (i = 0; i < size; i+=8)
            {
                s = bswap_64(((const volatile uint64_t*)source)[i/8]);
                d = ((const volatile uint64_t*)dest)[i/8];
                if (s != d) break;
            }
            break;
        default:
            fprintf(stderr, "Illegal wordsize %d: must be 1, 2, 4, 8, -2, -4, -8\n", wordsize);
#ifdef HAVE_setjmp_and_signal
            signalsOff();
#endif
            return -1;
    }
#ifdef HAVE_setjmp_and_signal
    signalsOff();
#endif
    if (i < size) {
        printf("Mismatch: at offset %#llx: 0x%0*llx != 0x%0*llx\n", (unsigned long long)i, abswordsize*2, s, abswordsize*2, d);
        return 1;
    }
    else
        printf("OK\n");
    return 0;
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
