#include "memDisplay.h"

extern "C" volatile void* memDisplayVMEAddrHandler(size_t addr, size_t size, size_t addrSpace);

extern "C" static int memDisplayInit()
{
#ifdef VME_AM_CSR
    memDisplayInstallAddrHandler("CRCSR", memDisplayVMEAddrHandler, VME_AM_CSR);
#endif
    memDisplayInstallAddrHandler("A32",   memDisplayVMEAddrHandler, VME_AM_EXT_SUP_DATA);
    memDisplayInstallAddrHandler("A24",   memDisplayVMEAddrHandler, VME_AM_STD_SUP_DATA);
    memDisplayInstallAddrHandler("A16",   memDisplayVMEAddrHandler, VME_AM_SUP_SHORT_IO);
    return 0;
}
static int init = memDisplayInit();
