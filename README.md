# Memory display utility

The function `memDisplay` can be used for hex + ASCII dump of
arbitrary memory regions. Access to invalid addresses it caught.

### memDisplay

`int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes)`

Display memory region starting at `ptr` of length `bytes` in hex and ASCII.
Lines of 16 bytes are prefixed with a hex address starting at `base` which
may be different from `ptr`, for example for memory mapped devices.
The hex numbers can be displays 1, 2, 4, or 8 (`wordsize`) bytes wide words.
If `wordsize` is negative, the words are displayed bytes swapped.

A signal handler is active during the execution of `memDisplay` in order
to catch access to invalid addresses so that the program will noch crash.

## iocsh functions

### md

`md address wordsize bytes`

This iocsh function calls memDisplay.
The `address` parameter can be a number (may be hex) to denote a
memory location, but it can also be the name of a global variable
(or other symbol, e.g. a function).
Furthermore, `address` can be a number prefixed by `A16:`, `A24:`,
`A32:` or `CRCSR:` to display VME address regions, for example `A16:0x8000`.

Other handlers of the form `xxx:` can be installed (see below).

If `wordsize` or `bytes` is not specified, the prevous value is used,
starting with wordsize 2 and 64 bytes.

If `address` is not specified, the block directly following the block
from the prevoius call is displayed.

### devReadProbe and devWriteProbe

`devReadProbe wordsize address` 
`devWriteProbe wordsize address value`

These are iocsh wrappers for the devLib functions of the same name.

The parameters `wordsize` and `address` have the same meaning as for
the `md` function. In particular the same syntax for `address` can be used.

## Helper function

### memDisplayInstallAddrHandler

`typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr)` 
`void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr)`

This function can be used to install new handlers for the address
parameter of the above iocsh functions.

The handler function is supposed to map an address region to a pointer.
The installer function binds the helper function to a string so that
this string followed by `:` can be used in the `address` parameter of
the above iocsh wrapper functions.
The parameter `usr` is an arbitrary value that can be used by the handler.
