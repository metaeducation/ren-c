//
//  file: %mod-environment.c
//  summary: "Functionality for Setting and Getting Environment Variables"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    #include "needful/needful.h"
    #include "c-extras.h"  // for EXTERN_C, nullptr, etc.

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
        (CELL_FLAG_DONT_MARK_PAYLOAD_1)  // currently no details
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // none of it should be marked
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


// !!! WARNING: While reading environment variables from a C program is fine,
// writing them is a generally sketchy proposition and should probably be
// avoided.  On UNIX there is no thread-safe way to do it, and even in a
// thread-safe program the underlying fact that the system doesn't know
// where the pointers for the strings it has came from, leaks are inevitable.
//
//      http://stackoverflow.com/a/5876818/211160
//
IMPLEMENT_GENERIC(TWEAK_P, Is_Environment)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* env = Element_ARG(LOCATION);
    Stable* picker = ARG(PICKER);

    if (not Is_Word(picker) and not Is_Text(picker))
        panic ("ENVIRONMENT! picker must be WORD! or TEXT!");

    Stable* dual = ARG(DUAL);

    Option(const Stable*) poke;  // set to nullptr if removing

    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    if (Is_Any_Lifted_Void(dual)) {
        poke = nullptr;
        goto update_environment;
    }

    trap (
      poke = Unliftify_Decayed(dual)
    );

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    Option(Value*) value;
    Option(ErrorValue*) error = Trap_Get_Environment_Variable(&value, picker);
    if (error)
        return rebDelegate("panic", unwrap error);

    if (not value)  // return error if not present, must TRY or OPT
        return DUAL_SIGNAL_NULL_ABSENT;

    if (
        Environment_Conflates_Empty_Strings_As_Absent(env)
        and Series_Len_At(unwrap value) == 0
    ){
        rebRelease(unwrap value);
        return DUAL_SIGNAL_NULL_ABSENT;
    }

    return DUAL_LIFTED(unwrap value);

} handle_poke: { /////////////////////////////////////////////////////////////

  // 1. To raise awareness about the empty string and null equivalence, force
  //    callers to use VOID instead of empty strings to unset (since you would
  //    only be able to get null back if you set to either an empty string or
  //    a void in this mode).

    if (not Is_Text(unwrap poke))
        panic ("ENVIRONMENT! can only be poked with VOID or TEXT!");

    if (
        Environment_Conflates_Empty_Strings_As_Absent(env)
        and Series_Len_At(unwrap poke) == 0
    ){
        panic (
            "ENVIRONMENT! not configured to accept empty strings"  // [1]
        );
    }

    goto update_environment;

} update_environment: { //////////////////////////////////////////////////////

    Option(ErrorValue*) error = Trap_Update_Environment_Variable(picker, poke);
    if (error)
        return rebDelegate("panic", unwrap error);

    return NO_WRITEBACK_NEEDED;
}}


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
        return rebDelegate("panic", unwrap error);

    return map;
}
