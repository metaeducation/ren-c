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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#ifdef TO_WINDOWS
#ifdef _MSC_VER
#pragma comment(lib, "rpcrt4.lib")
#endif
    #include <windows.h>

    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it

#elif defined(TO_OSX)
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
DECLARE_NATIVE(generate)
{
    UUID_INCLUDE_PARAMS_OF_GENERATE;

#ifdef TO_WINDOWS
    UUID uuid;
    UuidCreate(&uuid);

    // uuid.data* is in litte endian
    // the string form is in big endian
    Binary* ser = Make_Binary(16);
    *Binary_At(ser, 0) = cast(char*, &uuid.Data1)[3];
    *Binary_At(ser, 1) = cast(char*, &uuid.Data1)[2];
    *Binary_At(ser, 2) = cast(char*, &uuid.Data1)[1];
    *Binary_At(ser, 3) = cast(char*, &uuid.Data1)[0];

    *Binary_At(ser, 4) = cast(char*, &uuid.Data2)[1];
    *Binary_At(ser, 5) = cast(char*, &uuid.Data2)[0];

    *Binary_At(ser, 6) = cast(char*, &uuid.Data3)[1];
    *Binary_At(ser, 7) = cast(char*, &uuid.Data3)[0];

    memcpy(Binary_At(ser, 8), uuid.Data4, 8);

    Term_Binary_Len(ser, 16);

    Init_Binary(OUT, ser);

#elif defined(TO_OSX)
    CFUUIDRef newId = CFUUIDCreate(nullptr);
    CFUUIDBytes bytes = CFUUIDGetUUIDBytes(newId);
    CFRelease(newId);

    Binary* ser = Make_Binary(16);
    *Binary_At(ser, 0) = bytes.byte0;
    *Binary_At(ser, 1) = bytes.byte1;
    *Binary_At(ser, 2) = bytes.byte2;
    *Binary_At(ser, 3) = bytes.byte3;
    *Binary_At(ser, 4) = bytes.byte4;
    *Binary_At(ser, 5) = bytes.byte5;
    *Binary_At(ser, 6) = bytes.byte6;
    *Binary_At(ser, 7) = bytes.byte7;
    *Binary_At(ser, 8) = bytes.byte8;
    *Binary_At(ser, 9) = bytes.byte9;
    *Binary_At(ser, 10) = bytes.byte10;
    *Binary_At(ser, 11) = bytes.byte11;
    *Binary_At(ser, 12) = bytes.byte12;
    *Binary_At(ser, 13) = bytes.byte13;
    *Binary_At(ser, 14) = bytes.byte14;
    *Binary_At(ser, 15) = bytes.byte15;

    Term_Binary_Len(ser, 16);

    Init_Binary(OUT, ser);

#elif defined(TO_LINUX)
    uuid_t uuid;
    uuid_generate(uuid);

    Init_Binary(OUT, Copy_Bytes(uuid, sizeof(uuid)));

#else
    fail ("UUID is not implemented");
#endif

    return OUT;
}
