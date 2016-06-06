#ifndef memDisplay_h
#define memDisplay_h

#include <stdio.h>

int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes);
int fmemDisplay(FILE* outfile, size_t base, volatile void* ptr, int wordsize, size_t bytes);
int fdmemDisplay(int outfd, size_t base, volatile void* ptr, int wordsize, size_t bytes);

typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr);
void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr);

#endif
