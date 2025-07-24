//
// Workaround for need to #include <CoreFoundation/CFUUID.h>
// It typedefs Size in a way incompatible with %sys-core.h
// Have to isolate it into a file that doesn't include %sys-core.h
//
// (Not a problem in modern Ren-C UUID module, as it uses the
// external %rebol.h API and not %sys-core.h)

#include <CoreFoundation/CFUUID.h>

#include "needful/needful.h"
#include "c-extras.h"

extern void Get_Sixteen_Uuid_Bytes(unsigned char buf[16]);

void Get_Sixteen_Uuid_Bytes(unsigned char buf[16])
{
    CFUUIDRef newId = CFUUIDCreate(nullptr);
    CFUUIDBytes bytes = CFUUIDGetUUIDBytes(newId);
    CFRelease(newId);

    buf[0] = bytes.byte0;
    buf[1] = bytes.byte1;
    buf[2] = bytes.byte2;
    buf[3] = bytes.byte3;
    buf[4] = bytes.byte4;
    buf[5] = bytes.byte5;
    buf[6] = bytes.byte6;
    buf[7] = bytes.byte7;
    buf[8] = bytes.byte8;
    buf[9] = bytes.byte9;
    buf[10] = bytes.byte10;
    buf[11] = bytes.byte11;
    buf[12] = bytes.byte12;
    buf[13] = bytes.byte13;
    buf[14] = bytes.byte14;
    buf[15] = bytes.byte15;
}
