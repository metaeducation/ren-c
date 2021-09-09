//
//  File: %mod-uuid.c
//  Summary: "Native Functions manipulating UUID"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2017 Atronix Engineering
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "reb-config.h"

#if TO_WINDOWS
  #ifdef _MSC_VER
    #pragma comment(lib, "rpcrt4.lib")
  #endif

    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
    #undef IS_ERROR  // winerror.h defines, Rebol has a different meaning

    #include <rpc.h>  // for UuidCreate()
#elif TO_OSX
    #include <CoreFoundation/CFUUID.h>
#else
    #include <uuid.h>
#endif

#include "sys-core.h"

#include "tmp-mod-uuid.h"


//
//  generate: native [
//
//  "Generate a UUID"
//
//      return: [binary!]
//  ]
//
REBNATIVE(generate)
{
    UUID_INCLUDE_PARAMS_OF_GENERATE;

  #if TO_WINDOWS

    UUID uuid;  // uuid.data* is little endian, string form is big endian
    UuidCreate(&uuid);

    REBBIN *bin = Make_Binary(16);

    *BIN_AT(bin, 0) = cast(char*, &uuid.Data1)[3];
    *BIN_AT(bin, 1) = cast(char*, &uuid.Data1)[2];
    *BIN_AT(bin, 2) = cast(char*, &uuid.Data1)[1];
    *BIN_AT(bin, 3) = cast(char*, &uuid.Data1)[0];

    *BIN_AT(bin, 4) = cast(char*, &uuid.Data2)[1];
    *BIN_AT(bin, 5) = cast(char*, &uuid.Data2)[0];

    *BIN_AT(bin, 6) = cast(char*, &uuid.Data3)[1];
    *BIN_AT(bin, 7) = cast(char*, &uuid.Data3)[0];

    memcpy(BIN_AT(bin, 8), uuid.Data4, 8);

    TERM_BIN_LEN(bin, 16);

    Init_Binary(D_OUT, bin);

  #elif TO_OSX

    CFUUIDRef newId = CFUUIDCreate(NULL);
    CFUUIDBytes bytes = CFUUIDGetUUIDBytes(newId);
    CFRelease(newId);

    REBSER *bin = Make_Binary(16);
    *BIN_AT(bin, 0) = bytes.byte0;
    *BIN_AT(bin, 1) = bytes.byte1;
    *BIN_AT(bin, 2) = bytes.byte2;
    *BIN_AT(bin, 3) = bytes.byte3;
    *BIN_AT(bin, 4) = bytes.byte4;
    *BIN_AT(bin, 5) = bytes.byte5;
    *BIN_AT(bin, 6) = bytes.byte6;
    *BIN_AT(bin, 7) = bytes.byte7;
    *BIN_AT(bin, 8) = bytes.byte8;
    *BIN_AT(bin, 9) = bytes.byte9;
    *BIN_AT(bin, 10) = bytes.byte10;
    *BIN_AT(bin, 11) = bytes.byte11;
    *BIN_AT(bin, 12) = bytes.byte12;
    *BIN_AT(bin, 13) = bytes.byte13;
    *BIN_AT(bin, 14) = bytes.byte14;
    *BIN_AT(bin, 15) = bytes.byte15;

    TERM_BIN_LEN(bin, 16);

    Init_Binary(D_OUT, bin);

  #elif TO_LINUX

    uuid_t uuid;
    uuid_generate(uuid);

    Init_Binary(D_OUT, Copy_Bytes(uuid, sizeof(uuid)));

  #else

    fail ("UUID is not implemented");

  #endif

    return D_OUT;
}
