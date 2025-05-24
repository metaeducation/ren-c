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


IMPLEMENT_GENERIC(PICK_P, Is_Environment)
{
    INCLUDE_PARAMS_OF_PICK_P;

    Element* env = Element_ARG(LOCATION);
    Element* picker = Element_ARG(PICKER);

    if (not Is_Word(picker) and not Is_Text(picker))
        return "panic ENVIRONMENT! picker must be WORD! or TEXT!";

    Option(Value*) value;
    Option(ErrorValue*) error = Trap_Get_Environment_Variable(&value, picker);
    if (error)
        return rebDelegate("panic", unwrap error);

    if (not value)  // return error if not present, must TRY or OPT
        return DUAL_SIGNAL_NULL;

    if (
        Environment_Conflates_Empty_Strings_As_Absent(env)
        and Cell_Series_Len_At(unwrap value) == 0
    ){
        return DUAL_SIGNAL_NULL;
    }

    if (not value)
        return DUAL_SIGNAL_NULL;

    return DUAL_LIFTED(unwrap value);
}


// !!! WARNING: While reading environment variables from a C program is fine,
// writing them is a generally sketchy proposition and should probably be
// avoided.  On UNIX there is no thread-safe way to do it, and even in a
// thread-safe program the underlying fact that the system doesn't know
// where the pointers for the strings it has came from, leaks are inevitable.
//
//      http://stackoverflow.com/a/5876818/211160
//
IMPLEMENT_GENERIC(POKE_P, Is_Environment)
{
    INCLUDE_PARAMS_OF_POKE_P;

    Element* env = Element_ARG(LOCATION);
    Element* picker = Element_ARG(PICKER);

    if (not Is_Word(picker) and not Is_Text(picker))
        return PANIC("ENVIRONMENT! picker must be WORD! or TEXT!");

    Value* dual = ARG(DUAL);

    Option(const Value*) poke;  // set to nullptr if removing

  handle_dual_signals: {

    if (Any_Lifted(dual)) {
        if (Is_Dual_Null_Remove_Signal(dual)) {
            poke = nullptr;
            goto update_environment;
        }

        return PANIC(Error_Bad_Poke_Dual_Raw(dual));
    }

} handle_normal_values: {

  // 1. To raise awareness about the empty string and null equivalence, force
  //    callers to use VOID instead of empty strings to unset (since you would
  //    only be able to get null back if you set to either an empty string or
  //    a void in this mode).

    poke = Unliftify_Known_Stable(dual);

    if (not Is_Text(unwrap poke))
        return PANIC("ENVIRONMENT! can only be poked with VOID or TEXT!");

    if (
        Environment_Conflates_Empty_Strings_As_Absent(env)
        and Cell_Series_Len_At(unwrap poke) == 0
    ){
        return PANIC(
            "ENVIRONMENT! not configured to accept empty strings"  // [1]
        );
    }

} update_environment: {

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
