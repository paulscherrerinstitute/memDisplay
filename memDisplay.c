#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>

#ifdef __unix
#define HAVE_byteswap
#define HAVE_stdint
#endif

#ifdef HAVE_byteswap
#include <byteswap.h>
#else
#define UINT64_C(c) c ## ULL
#define bswap_16(x) ((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8))
#define bswap_32(x) ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
                    (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define bswap_64(x) ((((x) & 0xff00000000000000ull) >> 56) \
                   | (((x) & 0x00ff000000000000ull) >> 40) \
                   | (((x) & 0x0000ff0000000000ull) >> 24) \
                   | (((x) & 0x000000ff00000000ull) >> 8)  \
                   | (((x) & 0x00000000ff000000ull) << 8)  \
                   | (((x) & 0x0000000000ff0000ull) << 24) \
                   | (((x) & 0x000000000000ff00ull) << 40) \
                   | (((x) & 0x00000000000000ffull) << 56))
#endif

#ifdef HAVE_stdint
#include <stdint.h>
#else
#define uint32_t unsigned int
#endif

#include "memDisplay.h"


int memDisplay(size_t base, volatile void* ptr, int width, size_t bytes)
{
    return fmemDisplay(stdout, base, ptr, width, bytes);
}

int fdmemDisplay(int fd, size_t base, volatile void* ptr, int width, size_t bytes)
{
    int n=-1;
    FILE* file = fdopen(fd, "w");
    if (file) n = fmemDisplay(file, base, ptr, width, bytes);
    free(file); /* do not fclose(file) file because that would close(fd) */
    return n;
}

#ifdef vxWorks
#define SIGNAL SIGBUS
#else
#define SIGNAL SIGSEGV
#endif

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

int fmemDisplay(FILE* file, size_t base, volatile void* ptr, int width, size_t bytes)
{
    int addr_width = ((base + bytes - 1) & UINT64_C(0xffff000000000000)) ? 16 :
                     ((base + bytes - 1) &     UINT64_C(0xffff00000000)) ? 12 :
                     ((base + bytes - 1) &         UINT64_C(0xffff0000)) ? 8 : 4;

    uint64_t offset;
    size_t i, j, size, len=0;

    struct sigaction sa = {{0}}, oldsa;

    unsigned char buffer[16];
    int swap = width < 0;
    width = abs(width);
    
    switch (width)
    {
        case 8:
        case 4:
        case 2:
            /* align start */
            base &= ~(size_t)(width-1);
            ptr = (volatile void*)((size_t)ptr & ~(size_t)(width-1));
            base &= ~(size_t)(width-1);
        case 1:
            break;
        default:
            fprintf(stdout, "Invalid data width %d\n", width);
            return -1;
    }

    /* round down start address to multiple of 16 */
    offset = base & ~15;
    size = bytes + (base & 15);
    ptr = (void*)(((size_t)ptr) & ~15);
    memset(buffer, ' ', sizeof(buffer));
    
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

    for (i = 0; i < size; i += 16)
    {   
        len += fprintf(file, "%0*llx: ", addr_width, (long long unsigned int)offset);
        switch (width)
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
                        x = *(uint8_t*)(ptr + j);
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
                        x = *(uint16_t*)(ptr + j);
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
                        x = *(uint32_t*)(ptr + j);
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
                        x = *(uint64_t*)(ptr + j);
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
        ptr += 16;
        len += fprintf(file, "\n");
    }
    sigaction(SIGNAL, &oldsa, NULL);
    return len;
}
