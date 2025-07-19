//
//  file: %mod-uuid.c
//  summary: "Native Functions manipulating UUID"
//  section: Extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    // Can't include <CoreFoundation/CFUUID.h> in a file including %sys-core.h
    // due to incompatibly typedef'ing Size.
    //
    extern void Get_Sixteen_Uuid_Bytes(unsigned char buf[16]);
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
//      return: [blob!]
//  ]
//
DECLARE_NATIVE(GENERATE)
{
    UUID_INCLUDE_PARAMS_OF_GENERATE;

#ifdef TO_WINDOWS
    UUID uuid;
    UuidCreate(&uuid);

    // uuid.data* is in litte endian
    // the string form is in big endian
    Binary* flex = Make_Binary(16);
    *Binary_At(flex, 0) = cast(char*, &uuid.Data1)[3];
    *Binary_At(flex, 1) = cast(char*, &uuid.Data1)[2];
    *Binary_At(flex, 2) = cast(char*, &uuid.Data1)[1];
    *Binary_At(flex, 3) = cast(char*, &uuid.Data1)[0];

    *Binary_At(flex, 4) = cast(char*, &uuid.Data2)[1];
    *Binary_At(flex, 5) = cast(char*, &uuid.Data2)[0];

    *Binary_At(flex, 6) = cast(char*, &uuid.Data3)[1];
    *Binary_At(flex, 7) = cast(char*, &uuid.Data3)[0];

    memcpy(Binary_At(flex, 8), uuid.Data4, 8);

    Term_Binary_Len(flex, 16);

    Init_Blob(OUT, flex);

#elif defined(TO_OSX)

    Binary* flex = Make_Binary(16);
    Get_Sixteen_Uuid_Bytes(Binary_Head(flex));
    Term_Binary_Len(flex, 16);

    Init_Blob(OUT, flex);

    Init_Blank(OUT);

#elif defined(TO_LINUX)
    uuid_t uuid;
    uuid_generate(uuid);

    Init_Blob(OUT, Copy_Bytes(uuid, sizeof(uuid)));

#else
    panic ("UUID is not implemented");
#endif

    return OUT;
}
