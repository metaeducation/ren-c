//
//  File: %mod-environment.c
//  Summary: "Functionality for Setting and Getting Environment Variables"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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


#ifdef USING_LIBREBOL  // need %sys-core.h variation for IMPLEMENT_GENERIC()
    #include <assert.h>
    #include "c-enhanced.h"
    #define Sink SinkTypemacro

    #include "rebol.h"
    typedef RebolValue Value;
    typedef Value ErrorValue;
#else
    #include "sys-core.h"
    typedef Value ErrorValue;
#endif

#include "tmp-mod-environment.h"

#include "environment.h"



INLINE Element* Init_Environment(Init(Element) out) {
    Reset_Extended_Cell_Header_Noquote(
        out,
        EXTRA_HEART_ENVIRONMENT,
        (CELL_FLAG_DONT_MARK_NODE1)  // currently no details
            | CELL_FLAG_DONT_MARK_NODE2  // none of it should be marked
    );

    return out;
}


// Prescriptively speaking, it is typically considered a bad idea to treat
// an empty string environment variable as different from an unset one:
//
// https://unix.stackexchange.com/q/27708/
//
// When functions GET-ENV and SET-ENV existed, this could be done with a
// refinement.  But now ENV.SOME_VAR has nowhere to put a condition.  The
// only place to put the configuration is on the environment itself.
//
// For starters, let's make it the default to see what happens.
//
bool Environment_Conflates_Empty_Strings_As_Absent(Element* env)
{
    UNUSED(env);
    return true;
}


//
//  make-environment: native [
//
//  "Currently just creates an ENVIRONMENT! to represent current process"
//
//     return: [environment!]
//  ]
//
DECLARE_NATIVE(MAKE_ENVIRONMENT)
{
    INCLUDE_PARAMS_OF_MAKE_ENVIRONMENT;

    return Init_Environment(OUT);
}


IMPLEMENT_GENERIC(PICK, Is_Environment)
{
    INCLUDE_PARAMS_OF_PICK;

    Element* env = Element_ARG(LOCATION);
    Element* picker = Element_ARG(PICKER);

    if (not Is_Word(picker) and not Is_Text(picker))
        return FAIL("ENVIRONMENT! picker must be WORD! or TEXT!");

    Option(Value*) value;
    Option(ErrorValue*) error = Trap_Get_Environment_Variable(&value, picker);
    if (error)
        return rebDelegate("fail", unwrap error);

    if (not value)  // raise error if not present, must TRY or MAYBE
        return RAISE(Error_Bad_Pick_Raw(picker));

    if (
        Environment_Conflates_Empty_Strings_As_Absent(env)
        and Cell_Series_Len_At(unwrap value) == 0
    ){
        return RAISE(Error_Bad_Pick_Raw(picker));
    }

    return maybe value;
}


// !!! WARNING: While reading environment variables from a C program is fine,
// writing them is a generally sketchy proposition and should probably be
// avoided.  On UNIX there is no thread-safe way to do it, and even in a
// thread-safe program the underlying fact that the system doesn't know
// where the pointers for the strings it has came from, leaks are inevitable.
//
//      http://stackoverflow.com/a/5876818/211160
//
// 1. To raise awareness about the empty string and null equivalence, force
//    callers to use null instead of empty strings to unset (since you would
//    only be able to get null back if you set to either an empty string or
//    a null in this mode).
//
IMPLEMENT_GENERIC(POKE_P, Is_Environment)
{
    INCLUDE_PARAMS_OF_POKE_P;

    Element* env = Element_ARG(LOCATION);
    Element* picker = Element_ARG(PICKER);

    if (not Is_Word(picker) and not Is_Text(picker))
        return FAIL("ENVIRONMENT! picker must be WORD! or TEXT!");

    Option(const Value*) poke = Optional_ARG(VALUE);

    if (not poke) {
        // remove from environment (was a nihil)
    }
    else if (not Is_Text(unwrap poke)) {
        return FAIL("ENVIRONMENT! can only be poked with TRASH! or TEXT!");
    }
    else {
        if (
            Environment_Conflates_Empty_Strings_As_Absent(env)
            and Cell_Series_Len_At(unwrap poke) == 0
        ){
            return FAIL(
                "ENVIRONMENT! not configured to accept empty strings"  // [1]
            );
        }
    }

    Option(ErrorValue*) error = Trap_Update_Environment_Variable(picker, poke);
    if (error)
        return rebDelegate("fail", unwrap error);

    return nullptr;  // no writeback
}


//
//  export list-env: native [
//
//  "Returns a map of OS environment variables (for current process)"
//
//      return: [map!]
//  ]
//
DECLARE_NATIVE(LIST_ENV)
{
    INCLUDE_PARAMS_OF_LIST_ENV;

    Value* map;
    Option(ErrorValue*) error = Trap_List_Environment(&map);
    if (error)
        return rebDelegate("fail", unwrap error);

    return map;
}
