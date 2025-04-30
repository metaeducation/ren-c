//
//  file: %mod-uuid.c
//  summary: "Native Functions manipulating UUID"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    #include <rpc.h>  // for UuidCreate()
#elif TO_OSX
    //
    // CoreFoundation has definitions that conflict with %sys-core.h, so the
    // UUID extension was the first to be librebol-based, that defines the
    // `DECLARE_NATIVE` without include params macros.
    //
    #include <CoreFoundation/CFUUID.h>
#else
    #include <uuid.h>
#endif

#include <string.h>  // memcpy

#include "assert-fix.h"
#include "c-enhanced.h"

#include "rebol.h"
#include "tmp-mod-uuid.h"

typedef RebolValue Value;


//
//  generate: native [
//
//  "Generate a UUID"
//
//      return: [blob!]
//  ]
//
DECLARE_NATIVE(GENERATE)
{
    INCLUDE_PARAMS_OF_GENERATE;

  #if TO_WINDOWS

    Value* binary = rebUninitializedBinary_internal(16);
    unsigned char* bp = rebBinaryHead_internal(binary);

    UUID uuid;  // uuid.data* is little endian, string form is big endian
    switch (UuidCreate(&uuid)) {
      case RPC_S_OK:
        break;

      /* case RPC_S_LOCAL_ONLY: */  // in UuidCreate() docs, but not defined?
      case RPC_S_UUID_NO_ADDRESS:
      default:
        return "fail -[UuidCreate() could not make a unique address]-";
    }

    bp[0] = cast(char*, &uuid.Data1)[3];
    bp[1] = cast(char*, &uuid.Data1)[2];
    bp[2] = cast(char*, &uuid.Data1)[1];
    bp[3] = cast(char*, &uuid.Data1)[0];

    bp[4] = cast(char*, &uuid.Data2)[1];
    bp[5] = cast(char*, &uuid.Data2)[0];

    bp[6] = cast(char*, &uuid.Data3)[1];
    bp[7] = cast(char*, &uuid.Data3)[0];

    memcpy(bp + 8, uuid.Data4, 8);

    return binary;

  #elif TO_OSX

    CFUUIDRef newId = CFUUIDCreate(NULL);
    CFUUIDBytes bytes = CFUUIDGetUUIDBytes(newId);
    CFRelease(newId);

    unsigned char* buf = rebAllocBytes(16);

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

    return rebRepossess(buf, 16);

  #elif TO_LINUX || TO_HAIKU

    Value* binary = rebUninitializedBinary_internal(16);
    unsigned char* bp = rebBinaryHead_internal(binary);

    uuid_t uuid;
    uuid_generate(uuid);

    memcpy(bp, uuid, sizeof(uuid));

    return binary;

  #else

    return "fail -[UUID is not implemented]-";

  #endif
}
