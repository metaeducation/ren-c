//
//  File: %d-test.c
//  Summary: "Test routines for things only testable from inside Rebol"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019 Ren-C Open Source Contributors
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
// This file was created in order to have a place to put tests of libRebol.
// A better way to do this would be to include C compilation in the test
// suite against libr3.a, and drive those tests accordingly.  But this would
// involve setting up separate compilation and running those programs with
// CALL.  So this is an expedient way to do it just within a native that is
// built only in certain debug builds.
//

#include "sys-core.h"


//
//  test-librebol: native [
//
//  "libRebol tests (ultimately should build as separate EXEs)"
//
//      return: [text! block!]
//          {Block of test numbers and failures}
//      :value [<end> <opt> any-value!]
//          {Optional argument that may be useful for ad hoc tests}
//  ]
//
REBNATIVE(test_librebol)
{
    INCLUDE_PARAMS_OF_TEST_LIBREBOL;
    UNUSED(ARG(value));

  #if (! INCLUDE_TEST_LIBREBOL_NATIVE)
    return Init_Text(  // text! vs. failing to distinguish from test failure
        D_OUT,
        Make_String_UTF8(
            "TEST-LIBREBOL only if #define INCLUDE_TEST_LIBREBOL_NATIVE"
        )
    );
  #else
    REBDSP dsp_orig = DSP;

    SET_CELL_FLAG(Init_Integer(DS_PUSH(), 1), NEWLINE_BEFORE);
    Init_Logic(DS_PUSH(), 3 == rebUnboxInteger("1 +", rebI(2)));

    SET_CELL_FLAG(Init_Integer(DS_PUSH(), 2), NEWLINE_BEFORE);
    intptr_t getter = rebUnboxInteger("api-transient {Hello}");
    Recycle();  // transient should survive a recycle
    REBNOD *getter_node = cast(REBNOD*, cast(void*, getter));
    Init_Logic(DS_PUSH(), rebUnboxLogic("{Hello} = @", getter_node));

    SET_CELL_FLAG(Init_Integer(DS_PUSH(), 3), NEWLINE_BEFORE);
    REBVAL *macro = rebValue("macro [x] [[append x ^ first]]");
    REBVAL *mtest1 = rebValue(macro, "[1 2 3]", "[d e f]");
    Copy_Cell(DS_PUSH(), mtest1);
    rebRelease(mtest1);

    SET_CELL_FLAG(Init_Integer(DS_PUSH(), 4), NEWLINE_BEFORE);
    REBVAL *numbers = rebValue("[1 2 3]");
    REBVAL *letters = rebValue("[d e f]");
    REBVAL *mtest2 = rebValue(macro, rebR(numbers), rebR(letters));
    Copy_Cell(DS_PUSH(), mtest2);
    rebRelease(mtest2);

    SET_CELL_FLAG(Init_Integer(DS_PUSH(), 5), NEWLINE_BEFORE);
    Init_Logic(DS_PUSH(), rebUnboxLogic("null? @", nullptr));

    rebRelease(macro);

    return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
  #endif
}


//
//  diagnose: native [
//
//  {Prints some basic internal information about the value (debug only)}
//
//      return: "Same as input value (for passthru similar to PROBE)"
//          [<opt> any-value!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(diagnose)
{
  INCLUDE_PARAMS_OF_DIAGNOSE;

  #if defined(NDEBUG)
    UNUSED(ARG(value));
    fail ("DIAGNOSE is only available in debug builds");
  #else
    REBVAL *v = ARG(value);

  #if DEBUG_COUNT_TICKS
    REBTCK tick = frame_->tick;
  #else
    REBTCK tick = 0
  #endif

    printf(
        ">>> DIAGNOSE @ tick %ld in file %s at line %d\n",
        cast(long, tick),
        frame_->file,
        frame_->line
    );

    Dump_Value_Debug(v);

    return Init_None(D_OUT);
  #endif
}


//
//  fuzz: native [
//
//  {Introduce periodic or deterministic fuzzing of out of memory errors}
//
//      return: <none>
//      factor "Ticks or percentage of time to cause allocation errors"
//          [integer! percent!]
//  ]
//
REBNATIVE(fuzz)
{
    INCLUDE_PARAMS_OF_FUZZ;

  #if defined(NDEBUG)
    UNUSED(ARG(factor));
    fail ("FUZZ is only availble in DEBUG builds");
  #else
    if (IS_INTEGER(ARG(factor))) {
        PG_Fuzz_Factor = - VAL_INT32(ARG(factor));  // negative counts ticks
    }
    else {
        // Positive number is used with SPORADICALLY(10000) as the number
        // it is compared against.  If the result is less than the specified
        // amount it's a hit.  1.0 is thus 10000, which will always trigger.
        // 0.0 is thus 0, which never will.
        //
        assert(IS_PERCENT(ARG(factor)));
        PG_Fuzz_Factor = 10000 * VAL_DECIMAL(ARG(factor));
    }
    return Init_None(D_OUT);
  #endif
}
