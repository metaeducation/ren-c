//
//  File: %d-test.c
//  Summary: "Test routines for things only testable from within Rebol"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2024 Ren-C Open Source Contributors
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
// built only in certain checked builds.
//

#define LIBREBOL_BINDING (&librebol_binding)

#include "sys-core.h"

#if INCLUDE_TEST_LIBREBOL_NATIVE
    // Note: This demo is described in rebol.h, next to RebolActionCFunction
    // Altered to fit into this file's automated testing.

    static int Subroutine(void) {
        RebolContext* librebol_binding = nullptr;

        return rebUnboxInteger(
            "assert [action? :print]",
            "add 304 696"
        );
    }

    static const char* Sum_Plus_1000_Spec = "[ \
        -{Demonstration native that shadows ASSERT and ADD}- \
        assert [integer!] \
        add [integer!] \
    ]";
    static RebolBounce Sum_Plus_1000_Impl(RebolContext* librebol_binding) {
        int thousand = Subroutine();
        return rebValue("add + assert +", rebI(thousand));
    }
#endif


//
//  /test-librebol: native [
//
//  "libRebol tests (ultimately should build as separate EXEs)"
//
//      return: "Block of test numbers and failures"
//          [text! block!]
//      value "Argument that may be useful for ad hoc tests"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(test_librebol)
{
    INCLUDE_PARAMS_OF_TEST_LIBREBOL;
    UNUSED(ARG(value));

  #if (! INCLUDE_TEST_LIBREBOL_NATIVE)
    return Init_Text(  // text! vs. failing to distinguish from test failure
        OUT,
        Make_String_UTF8(
            "TEST-LIBREBOL only if #define INCLUDE_TEST_LIBREBOL_NATIVE"
        )
    );
  #else
    RebolContext* librebol_binding = nullptr;

  // !!! NOTICE: We are pushing values to the data stack, but we can't hold
  // a pointer to the stack via PUSH() on the same line as doing an API
  // call, because API calls can move the stack.  This doesn't always make
  // an assert since argument order can vary across compilers.

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 1), NEWLINE_BEFORE);
    int i = rebUnboxInteger("1 +", rebI(2));

    Init_Boolean(PUSH(), i == 3);  // ^-- see NOTICE
  }

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 2), NEWLINE_BEFORE);
    intptr_t getter = rebUnboxInteger("api-transient -{Hello}-");
    Recycle();  // transient should survive a recycle
    Node* getter_node = p_cast(Node*, getter);
    bool equal = rebUnboxLogic("-{Hello}- = @", getter_node);

    Init_Boolean(PUSH(), equal);  // ^-- see NOTICE
  }

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 3), NEWLINE_BEFORE);
    Value* macro = rebValue("macro [x] [[append x first]]");
    Value* mtest1 = rebValue(rebRUN(macro), "[1 2 3]", "[d e f]");
    Copy_Cell(PUSH(), mtest1);  // ^-- see NOTICE
    rebRelease(mtest1);

    Set_Cell_Flag(Init_Integer(PUSH(), 4), NEWLINE_BEFORE);
    Value* numbers = rebValue("[1 2 3]");
    Value* letters = rebValue("[d e f]");
    Value* mtest2 = rebValue(rebRUN(macro), rebR(numbers), rebR(letters));
    Copy_Cell(PUSH(), mtest2);  // ^-- see NOTICE
    rebRelease(mtest2);

    rebRelease(macro);
  }

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 5), NEWLINE_BEFORE);
    bool is_null = rebUnboxLogic("null? @", nullptr);

    Init_Boolean(PUSH(), is_null);
  }

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 6), NEWLINE_BEFORE);
    Value* action = rebFunction(
        Sum_Plus_1000_Spec,
        &Sum_Plus_1000_Impl
    );

    int sum = rebUnboxInteger(
        "let sum-plus-1000: @", action,
        "sum-plus-1000 5 15"
    );

    rebRelease(action);
    Init_Integer(PUSH(), sum);
  }

  blockscope {
    Set_Cell_Flag(Init_Integer(PUSH(), 7), NEWLINE_BEFORE);
  #if (! CPLUSPLUS_11)
    Init_Integer(PUSH(), 1020);  // fake success result
  #else
    Value* action = rebFunction(R"([
        -{Demonstration native that shadows ASSERT and ADD (C++ version)}-
        assert [integer!]
        add [integer!]
    ])",
    [](RebolContext* librebol_binding) -> RebolBounce {
        int thousand = Subroutine();
        return rebValue("add + assert +", rebI(thousand));
    });

    int sum = rebUnboxInteger(
        "let sum-plus-1000: @", action,
        "sum-plus-1000 5 15"
    );

    rebRelease(action);
    Init_Integer(PUSH(), sum);
  #endif
  }

  blockscope {  // !!! Note: FEED_FLAG_NEEDS_SYNC needs review
    Set_Cell_Flag(Init_Integer(PUSH(), 8), NEWLINE_BEFORE);

    Value* noop = rebValue("");
    assert(Is_Void(noop));
    rebRelease(noop);
    Init_Meta_Of_Void(PUSH());
  }

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));
  #endif
}


//
//  /diagnose: native [
//
//  "Prints some basic internal information about the value (debug only)"
//
//      return: "Same as input value (for passthru similar to PROBE)"
//          [any-value?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(diagnose)
{
  INCLUDE_PARAMS_OF_DIAGNOSE;

  #if RUNTIME_CHECKS
    Value* v = ARG(value);

  #if TRAMPOLINE_COUNTS_TICKS
    Tick tick = level_->tick;
  #else
    Tick tick = 0
  #endif

    printf(
        ">>> DIAGNOSE @ tick %ld in file %s at line %d\n",
        cast(long, tick),
        level_->file,
        level_->line
    );

    Dump_Value_Debug(v);

    return NOTHING;
  #else
    UNUSED(ARG(value));
    return FAIL(Error_Checked_Build_Only_Raw());
  #endif
}


//
//  /fuzz: native [
//
//  "Introduce periodic or deterministic fuzzing of out of memory errors"
//
//      return: [~]
//      factor "Ticks or percentage of time to cause allocation errors"
//          [integer! percent!]
//  ]
//
DECLARE_NATIVE(fuzz)
//
// 1. A negative g_mem.fuzz_factor will just count ticks.
//
// 2. A positive g_mem.fuzz_factor is used with SPORADICALLY(10000) as the
//    number it is compared against.  If the result is less than the specified
//    amount it's a hit.  1.0 is thus 10000, which will always trigger.  0.0
//    is thus 0 and will never trigger.
{
    INCLUDE_PARAMS_OF_FUZZ;

  #if RUNTIME_CHECKS
    if (Is_Integer(ARG(factor))) {
        g_mem.fuzz_factor = - VAL_INT32(ARG(factor));  // negative [1]
    }
    else {
        assert(Is_Percent(ARG(factor)));
        g_mem.fuzz_factor = 10000 * VAL_DECIMAL(ARG(factor));  // positive [2]
    }
    return NOTHING;
  #else
    UNUSED(ARG(factor));
    return FAIL("FUZZ is only availble in RUNTIME_CHECKS builds");
  #endif
}
