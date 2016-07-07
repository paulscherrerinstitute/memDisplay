#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __unix
#define HAVE_byteswap
#define HAVE_stdint
#define HAVE_setjmp_and_signal
#define SIGNAL SIGSEGV
#endif

#ifdef vxWorks
#define HAVE_setjmp
#define HAVE_setjmp_and_signal
#define SIGNAL SIGBUS
#endif

#ifdef _WIN32
#define HAVE_stdint
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#endif

#ifdef HAVE_byteswap
#include <byteswap.h>
#else
#define UINT64_C(c) c ## ULL
#ifndef bswap_16
#define bswap_16(x) ((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8))
#endif
#ifndef bswap_32
#define bswap_32(x) ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
                    (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif
#ifndef bswap_64
#define bswap_64(x) ((((x) & 0xff00000000000000ull) >> 56) \
                   | (((x) & 0x00ff000000000000ull) >> 40) \
                   | (((x) & 0x0000ff0000000000ull) >> 24) \
                   | (((x) & 0x000000ff00000000ull) >> 8)  \
                   | (((x) & 0x00000000ff000000ull) << 8)  \
                   | (((x) & 0x0000000000ff0000ull) << 24) \
                   | (((x) & 0x000000000000ff00ull) << 40) \
                   | (((x) & 0x00000000000000ffull) << 56))
#endif
#endif

#ifdef HAVE_stdint
#include <stdint.h>
#else
#define uint32_t unsigned int
#endif

#ifdef HAVE_setjmp_and_signal
#include <signal.h>
#include <setjmp.h>
#endif

#include "memDisplay.h"


int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes)
{
    return fmemDisplay(stdout, base, ptr, wordsize, bytes);
}

#ifdef HAVE_setjmp_and_signal
/* setup handler to catch unmapped addresses */
static jmp_buf memDisplayFail;
static void memDisplaySigAction(int sig, siginfo_t *info, void *ctx)
{
#ifdef si_addr
    fprintf(stdout, "\nInvalid address %p.\n", info->si_addr);
#else
    fprintf(stdout, "\nInvalid address\n");
#endif
    longjmp(memDisplayFail, 1);
}
#endif /* HAVE_setjmp_and_signal */

int fmemDisplay(FILE* file, size_t base, volatile void* ptr, int wordsize, size_t bytes)
{
    int addr_wordsize = ((base + bytes - 1) & UINT64_C(0xffff000000000000)) ? 16 :
                        ((base + bytes - 1) &     UINT64_C(0xffff00000000)) ? 12 :
                        ((base + bytes - 1) &         UINT64_C(0xffff0000)) ? 8 : 4;

    uint64_t offset;
    size_t i, j, size, len=0;

#ifdef HAVE_setjmp_and_signal
    struct sigaction sa = {{0}}, oldsa;
#endif /* HAVE_setjmp_and_signal */

    unsigned char buffer[16];
    int swap = wordsize < 0;
    wordsize = abs(wordsize);
    
    switch (wordsize)
    {
        case 8:
        case 4:
        case 2:
            /* align start */
            base &= ~(size_t)(wordsize-1);
            ptr = (volatile void*)((size_t)ptr & ~(size_t)(wordsize-1));
            base &= ~(size_t)(wordsize-1);
        case 1:
            break;
        default:
            fprintf(stdout, "Invalid data wordsize %d\n", wordsize);
            return -1;
    }

    /* round down start address to multiple of 16 */
    offset = base & ~15;
    size = bytes + (base & 15);
    ptr = (void*)(((size_t)ptr) & ~15);
    memset(buffer, ' ', sizeof(buffer));
    
#ifdef HAVE_setjmp_and_signal
    sa.sa_sigaction = memDisplaySigAction;
    sa.sa_flags = SA_SIGINFO;
#ifdef SA_NODEFER
    sa.sa_flags |= SA_NODEFER; /* Do not block signal */
#endif
    sigaction(SIGNAL, &sa, &oldsa);
    
    if (setjmp(memDisplayFail) != 0)
    {
#ifndef SA_NODEFER
        /* Unblock signal */
        sigset_t sigmask;
        sigemptyset(&sigmask);
        sigaddset(&sigmask, SIGNAL);
        sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
#endif
        sigaction(SIGNAL, &oldsa, NULL);
        return -1;
    }
#endif /* HAVE_setjmp_and_signal */

    for (i = 0; i < size; i += 16)
    {   
        len += fprintf(file, "%0*llx: ", addr_wordsize, (long long unsigned int)offset);
        switch (wordsize)
        {
            case 1:
                for (j = 0; j < 16; j++)
                {
                    uint8_t x;
                    if (offset + j < base || i + j >= size)
                    {
                        len += fprintf(file, "   ");
                    }
                    else
                    {
                        x = *(uint8_t*)((char*)ptr + j);
                        *(uint8_t*)(buffer + j) = x;
                        len += fprintf(file, "%02x ", x);
                    }
                }
                break;
            case 2:
                for (j = 0; j < 16; j+=2)
                {
                    uint16_t x;
                    if (offset + j < base || i + j >= size)
                    {
                        len += fprintf(file, "     ");
                    }
                    else
                    {
                        x = *(uint16_t*)((char*)ptr + j);
                        if (swap) x = bswap_16(x);
                        *(uint16_t*)(buffer + j) = x;
                        len += fprintf(file, "%04x ", x);
                    }
                }
                break;
            case 4:
                for (j = 0; j < 16; j+=4)
                {
                    uint32_t x;
                    if (offset + j < base || i + j >= size)
                    {
                        len += fprintf(file, "         ");
                    }
                    else
                    {
                        x = *(uint32_t*)((char*)ptr + j);
                        if (swap) x = bswap_32(x);
                        *(uint32_t*)(buffer + j) = x;
                        len += fprintf(file, "%08x ", x);
                    }
                }
                break;
            case 8:
                for (j = 0; j < 16; j+=8)
                {
                    uint64_t x;
                    if (offset + j < base || i + j >= size)
                    {
                        len += fprintf(file, "                 ");
                    }
                    else
                    {
                        x = *(uint64_t*)((char*)ptr + j);
                        if (swap) x = bswap_64(x);
                        *(uint64_t*)(buffer + j) = x;
                        len += fprintf(file, "%016llx ", (long long unsigned int)x);
                    }
                }
                break;
        }
        for (j = 0; j < 16; j++)
        {
            if (i + j >= size) break;
            len += fprintf(file, "%c", isprint(buffer[j]) ? buffer[j] : '.');
        }
        offset += 16;
        ptr = (char*)ptr + 16;
        len += fprintf(file, "\n");
    }
#ifdef HAVE_setjmp_and_signal
    sigaction(SIGNAL, &oldsa, NULL);
#endif /* HAVE_setjmp_and_signal */
    return len;
}
