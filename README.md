# Memory display utility

The function `memDisplay` can be used for hex + ASCII dump of
arbitrary memory regions. Access to invalid addresses it caught.

Output looks like this:

    7f11453f94d0: 8b48 b905 2010 4900 c889 d189 8948 48f2 H.... .I....H..H
    7f11453f94e0: fe89 8b48 e938 eb56 ffff 0f66 441f 0000 ..H.8.V...f..D..
    7f11453f94f0: 8348 08ec 8b48 9505 2010 4800 568b 4810 H...H.... .H.V.H
    7f11453f9500: 358d 08a5 0000 8b48 3138 e8c0 eb90 ffff .5....H.81......
    7f11453f9510: 8d48 a93d 2012 be00 0001 0000 3fe8 ffeb H.=.. .......?..
    7f11453f9520: 66ff 6666 6666 2e66 1f0f 0084 0000 0000 .ffffff.........
    7f11453f9530: 8948 246c 48d8 f589 8d48 8235 0008 4800 H.l$.H..H.5....H
    7f11453f9540: 5c89 d024 894c 2464 49e0 d489 894c 246c .\$.L.d$.I..L.l$

### memDisplay

    int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes);
    int fmemDisplay(FILE* outfile, size_t base, volatile void* ptr, int wordsize, size_t bytes);
    int fdmemDisplay(int outfd, size_t base, volatile void* ptr, int wordsize, size_t bytes);

Display memory region starting at `ptr` of length `bytes` in hex and ASCII.
Output goes to `stdout` or the file `outfile` or the file descriptor `outfd`.
Lines of 16 bytes are prefixed with a hex address starting at `base`
(which may be different from `ptr`, for example for memory mapped devices).
The hex numbers can be displayed as 1, 2, 4, or 8 (`wordsize`) bytes wide words.
If `wordsize` is negative, the words are displayed byte swapped.

A signal handler is active during the execution of `memDisplay` to catch any
access to invalid addresses so that the program will not crash.

## iocsh functions

### md

    md address wordsize bytes

This iocsh function calls memDisplay.
The `address` parameter can be a number (may be hex) to denote a
memory location, but it can also be the name of a global variable
(or other symbol, e.g. a function).
Furthermore, `address` can be a number prefixed by `A16:`, `A24:`,
`A32:` or `CRCSR:` to display VME address regions, for example `A16:0x8000`.

Other handlers of the form `xxx:` can be installed (see below).

If `wordsize` or `bytes` is not specified, the prevous value is used,
starting with wordsize 2 and 64 bytes.

If `address` is not specified, the memory block directly following the
block of the prevoius call is displayed.

### devReadProbe and devWriteProbe

    devReadProbe wordsize address
    devWriteProbe wordsize address value

These are iocsh wrappers for the devLib functions of the same name.

The parameters `wordsize` and `address` have the same meaning as for
the `md` function. In particular the same syntax for `address` can be used.

## Helper function

### memDisplayInstallAddrHandler

    typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr);
    void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr);

This function can be used to install new handlers for the `address`
parameter of the above iocsh functions.

The handler function is supposed to map an address region to a pointer.
The installer function binds the helper function to a string so that
this string followed by `:` can be used in the `address` parameter of
the above iocsh wrapper functions.
The parameter `usr` is an arbitrary value that can be used by the handler.
