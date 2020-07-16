//
//  File: %mod-locale.c
//  Summary: "Native Functions for spawning and controlling processes"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
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
    #include <windows.h>
#endif
#include <locale.h>

// IS_ERROR might be defined in winerror.h and tmp-kinds.h
#ifdef IS_ERROR
#undef IS_ERROR
#endif

#include "sys-core.h"

#include "tmp-mod-locale.h"


//
//  export locale: native [
//      "Get locale specific information"
//      category [word!]
//          {Language: English name of the language,
//          Territory: English name of the country/region,
//          Language*: Full localized primary name of the language
//          Territory*: Full localized name of the country/region}
//  ]
//
REBNATIVE(locale)
//
{
  #ifdef TO_WINDOWS
    LOCALE_INCLUDE_PARAMS_OF_LOCALE;

    REBVAL *cat = ARG(category);

    LCTYPE type = rebUnbox(
        "select [",
            "language", rebI(LOCALE_SENGLANGUAGE),
            "language*", rebI(LOCALE_SNATIVELANGNAME),
            "territory", rebI(LOCALE_SENGCOUNTRY),
            "territory*", rebI(LOCALE_SCOUNTRY),
        "]", cat, "else [",
            "fail [{Invalid locale category:}", rebUneval(cat), "]",
        "]", rebEND // !!! review using fail with ID-based errors
    );

    // !!! MS docs say: "For interoperability reasons, the application should
    // prefer the GetLocaleInfoEx function to GetLocaleInfo because Microsoft
    // is migrating toward the use of locale names instead of locale
    // identifiers for new locales. Any application that runs only on Windows
    // Vista and later should use GetLocaleInfoEx."
    //
    int len_plus_term = GetLocaleInfo(0, type, 0, 0); // fetch needed length

    WCHAR *buffer = rebAllocN(WCHAR, len_plus_term);

    int len_check = GetLocaleInfo(0, type, buffer, len_plus_term); // now get
    assert(len_check == len_plus_term);
    UNUSED(len_check);

    REBVAL *text = rebLengthedTextW(buffer, len_plus_term - 1);
    rebFree(buffer);

    return text;
  #else
    UNUSED(frame_);
    fail ("LOCALE not implemented natively for non-Windows");
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
//  {Set/Get current locale, just a simple wrapper around C version}
//
//      return: [<opt> text!]
//      category [word!]
//      value [text!]
//  ]
//
REBNATIVE(setlocale)
{
    LOCALE_INCLUDE_PARAMS_OF_SETLOCALE;

    // GNU extensions are #define'd to -1 above this routine if not available
    //
    REBVAL *map = rebValue(
        "make map! [",
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
        "]", rebEND
    );

    int cat = rebUnbox("-1 unless select", map, ARG(category), rebEND);
    rebRelease(map);

    if (cat == -1)
        rebJumps(
            "fail [{Invalid locale category:}", ARG(category), "]", rebEND
        );

    char *value_utf8 = rebSpell(ARG(value), rebEND);
    const char *result = setlocale(cat, value_utf8);
    rebFree(value_utf8);

    if (not result)
        return nullptr;

    return rebText(result);
}
