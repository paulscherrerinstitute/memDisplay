#ifndef memDisplay_h
#define memDisplay_h

#include <stdio.h>

#ifndef epicsShareExtern
#define epicsShareExtern extern
#endif
#ifndef epicsShareFunc
#define epicsShareFunc
#endif

#ifdef __cplusplus
extern "C" {
#endif

epicsShareExtern int memDisplayDebug;

epicsShareFunc int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes);
epicsShareFunc int fmemDisplay(FILE* outfile, size_t base, volatile void* ptr, int wordsize, size_t bytes);

typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr);
epicsShareFunc void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr);

epicsShareFunc unsigned long long strToSize(const char* str, char** endptr);
epicsShareFunc char* sizeToStr(unsigned long long size, char* str);

#ifdef __cplusplus
}
#endif
#endif
