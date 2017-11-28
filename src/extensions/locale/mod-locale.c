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

// IS_ERROR might be defined in winerror.h and reb-types.h
#ifdef IS_ERROR
#undef IS_ERROR
#endif

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-locale-first.h"


//
//  locale: native/export [
//      "Get locale specific information"
//      category [word!]
//          {Language: English name of the language,
//          Territory: English name of the country/region,
//          Language*: Full localized primary name of the language
//          Territory*: Full localized name of the country/region}
//  ]
//  new-words: [Language Language* Territory Territory*]
//  new-errors: [
//      invalid-category: [{Invalid locale category:} :arg1]
//  ]
//
REBNATIVE(locale)
//
{
#ifdef TO_WINDOWS
    INCLUDE_PARAMS_OF_LOCALE;
    
    REBSTR *cat = VAL_WORD_CANON(ARG(category));
    
    LCTYPE type;
    if (cat == LOCALE_WORD_LANGUAGE)
        type = LOCALE_SENGLANGUAGE;
    else if (cat == LOCALE_WORD_LANGUAGE_P)
        type = LOCALE_SNATIVELANGNAME;
    else if (cat == LOCALE_WORD_TERRITORY)
        type = LOCALE_SENGCOUNTRY;
    else if (cat == LOCALE_WORD_TERRITORY_P)
        type = LOCALE_SCOUNTRY;
    else
        fail (Error(RE_EXT_LOCALE_INVALID_CATEGORY, ARG(category), END));

    int len_plus_term = GetLocaleInfo(0, type, 0, 0); // fetch needed length

    wchar_t *buffer = OS_ALLOC_N(wchar_t, len_plus_term);

    int len_check = GetLocaleInfo(0, type, buffer, len_plus_term);
    assert(len_check == len_plus_term);

    REBVAL *str = rebSizedStringW(buffer, len_plus_term - 1);
    Move_Value(D_OUT, str);
    rebRelease(str);

    return R_OUT;
#else
    UNUSED(frame_);
    fail ("Locale not implemented for non-windows");
#endif

}


//
//  setlocale: native/export [
//      {Set/Get current locale, just a simple wrapper around C version}
//      return: [string! blank!]
//      category [word!]
//      value [string!]
//  ]
//  new-words: [
//      all
//      address
//      collate
//      ctype
//      identification
//      measurement
//      messages
//      monetary
//      name
//      numeric
//      paper
//      telephone
//      time
//  ]
//  new-errors: [
//  ]
//
REBNATIVE(setlocale)
{
    INCLUDE_PARAMS_OF_SETLOCALE;

    struct cat_pair {
        REBSTR *word;
        int cat;
    } ctypes [] = {
        {LOCALE_WORD_ALL, LC_ALL},
#if defined(LC_ADDRESS) // GNU extension
        {LOCALE_WORD_ADDRESS, LC_ADDRESS},
#endif
        {LOCALE_WORD_COLLATE, LC_COLLATE},
        {LOCALE_WORD_CTYPE, LC_CTYPE},
#if defined(LC_IDENTIFICATION) // GNU extension
        {LOCALE_WORD_IDENTIFICATION, LC_IDENTIFICATION},
#endif
#if defined(LC_MEASUREMENT) // GNU extension
        {LOCALE_WORD_MEASUREMENT, LC_MEASUREMENT},
#endif
#if defined(LC_MESSAGES) // GNU extension
        {LOCALE_WORD_MESSAGES, LC_MESSAGES},
#endif
        {LOCALE_WORD_MONETARY, LC_MONETARY},
#if defined(LC_NAME) // GNU extension
        {LOCALE_WORD_NAME, LC_NAME},
#endif
        {LOCALE_WORD_NUMERIC, LC_NUMERIC},
#if defined(LC_TELEPHONE) // GNU extension
        {LOCALE_WORD_TELEPHONE, LC_TELEPHONE},
#endif
#if defined(LC_PAPER) // GNU extension
        {LOCALE_WORD_PAPER, LC_PAPER},
#endif
        {LOCALE_WORD_TIME, LC_TIME}
    };

    REBSTR *w_cat = VAL_WORD_CANON(ARG(category));

    int cat = -1; // avoid possibly uninitialized warning
    REBCNT i = 0;
    for (; i < sizeof (ctypes) / sizeof (ctypes[0]); ++i) {
        if (ctypes[i].word == w_cat) {
            cat = ctypes[i].cat;
            break;
        }
    }
    if (i == sizeof (ctypes) / sizeof (ctypes[0]))
        fail (Error(RE_EXT_LOCALE_INVALID_CATEGORY, ARG(category), END));

    const char *ret = setlocale(cat, cs_cast(VAL_BIN_AT(ARG(value))));
    if (ret == NULL) {
        Init_Blank(D_OUT);
    } else {
        REBCNT len = strlen(ret);
        REBSER *ser = Make_Binary(len);
        Append_Series(ser, cb_cast(ret), len);
        Init_String(D_OUT, ser);
    }

    return R_OUT;
}

#include "tmp-mod-locale-last.h"
