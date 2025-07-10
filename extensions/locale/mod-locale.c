//
//  file: %mod-locale.c
//  summary: "Native Functions for spawning and controlling processes"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
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
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
#endif

#include <locale.h>

#include "assert-fix.h"
#include "needful/needful.h"
#include "c-extras.h"  // for EXTERN_C, nullptr, etc.


#include "rebol.h"
#include "tmp-mod-locale.h"

typedef RebolValue Value;


//
//  export locale: native [
//
//   "Get locale specific information"
//
//      return: [null? text!]
//      category [~(language language* territory territory*)~]
//          --[language: English name of the language,
//          territory: English name of the country/region,
//          language*: Full localized primary name of the language
//          territory*: Full localized name of the country/region]--
//  ]
//
DECLARE_NATIVE(LOCALE)
//
// 1. This function only needs to make OS calls on Windows.  The POSIX version
//    parses environment variables and uses compiled-in tables.  See the HIJACK
//    in %ext-locale-init.r for that.
{
    INCLUDE_PARAMS_OF_LOCALE;

  #if TO_WINDOWS
    LCTYPE type = rebUnbox(
        "select [",
            "language", rebI(LOCALE_SENGLANGUAGE),
            "language*", rebI(LOCALE_SNATIVELANGNAME),
            "territory", rebI(LOCALE_SENGCOUNTRY),
            "territory*", rebI(LOCALE_SCOUNTRY),
        "] category else [",
            "panic [-[Invalid locale category:]- category]",
        "]"  // !!! review using panic with ID-based errors
    );

    // !!! MS docs say: "For interoperability reasons, the application should
    // prefer the GetLocaleInfoEx function to GetLocaleInfo because Microsoft
    // is migrating toward the use of locale names instead of locale
    // identifiers for new locales. Any application that runs only on Windows
    // Vista and later should use GetLocaleInfoEx."
    //
    int len_plus_term = GetLocaleInfo(0, type, 0, 0); // fetch needed length

    WCHAR* buffer = rebAllocN(WCHAR, len_plus_term);

    int len_check = GetLocaleInfo(0, type, buffer, len_plus_term); // now get
    assert(len_check == len_plus_term);
    UNUSED(len_check);

    Value* text = rebLengthedTextWide(buffer, len_plus_term - 1);
    rebFree(buffer);

    return text;
  #else
    return "panic -[LOCALE not implemented natively for non-Windows]-";  // [1]
  #endif
}


// Some locales are GNU extensions; define them as -1 if not present:
//
// http://man7.org/linux/man-pages/man7/locale.7.html

#ifndef LC_ADDRESS
    #define LC_ADDRESS -1
#endif

#ifndef LC_IDENTIFICATION
    #define LC_IDENTIFICATION -1
#endif

#ifndef LC_MEASUREMENT
    #define LC_MEASUREMENT -1
#endif

#ifndef LC_MESSAGES
    #define LC_MESSAGES -1
#endif

#ifndef LC_NAME
    #define LC_NAME -1
#endif

#ifndef LC_PAPER
    #define LC_PAPER -1
#endif

#ifndef LC_TELEPHONE
    #define LC_TELEPHONE -1
#endif


//
//  export setlocale: native [
//
//  "Set/Get current locale, just a simple wrapper around C version"
//
//      return: [null? text!]
//      category [word!]
//      value [text!]
//  ]
//
DECLARE_NATIVE(SETLOCALE)
{
    INCLUDE_PARAMS_OF_SETLOCALE;

    // GNU extensions are #define'd to -1 above this routine if not available
    //
    Value* map = rebValue(
        "to map! [",
            "all", rebI(LC_ALL),
            "address", rebI(LC_ADDRESS), // GNU extension
            "collate", rebI(LC_COLLATE),
            "ctype", rebI(LC_CTYPE),
            "identification", rebI(LC_IDENTIFICATION), // GNU extension
            "measurement", rebI(LC_MEASUREMENT), // GNU extension
            "messages", rebI(LC_MESSAGES), // GNU extension
            "monetary", rebI(LC_MONETARY), // GNU extension
            "name", rebI(LC_NAME), // GNU extension
            "numeric", rebI(LC_NUMERIC),
            "paper", rebI(LC_PAPER), // GNU extension
            "telephone", rebI(LC_TELEPHONE), // GNU extension
            "time", rebI(LC_TIME),
        "]"
    );

    int cat = rebUnbox("select", map, "category else [-1]");
    rebRelease(map);

    if (cat == -1)
        return rebDelegate("panic [-[Invalid locale category:]- category]");

    char* value_utf8 = rebSpell("value");
    const char *result = setlocale(cat, value_utf8);
    rebFree(value_utf8);

    if (not result)
        return nullptr;

    return rebText(result);
}
