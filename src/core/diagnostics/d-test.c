//
//  file: %d-test.c
//  summary: "Test routines for things only testable from within Rebol"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2025 Ren-C Open Source Contributors
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
// It's a paltry number of tests for the API!
//
// What needs to be done instead is to include C compilation in the test
// suite against libr3.a, and drive those tests accordingly.  It should be
// many files with many tests each (like at least one file per API function).
// This would involve setting up separate compilation and running those
// programs with CALL.
//
// But until someone makes time to rig that up, this is better than nothing.
// Generally speaking the real testing the API gets right now is that it's
// used extensively in the codebase--both in the core and in extensions.
//

#include "sys-core.h"

#if INCLUDE_TEST_LIBREBOL_NATIVE
    // Note: This demo is described in rebol.h, next to RebolActionCFunction
    // Altered to fit into this file's automated testing.

    static int Subroutine(void) {
        return rebUnboxInteger(
            "assert [action? print/]",
            "add 304 696"
        );
    }

    static const char* Sum_Plus_1000_Spec = "[ \
        -[Demonstration native that shadows ASSERT and ADD]- \
        return: [integer!] \
        assert [integer!] \
        add [integer!] \
    ]";
    static RebolBounce Sum_Plus_1000_Impl(RebolContext* librebol_binding) {
        int thousand = Subroutine();
        return rebValue("add + assert +", rebI(thousand));
    }
#endif


//
//  test-librebol: native [
//
//  "libRebol tests (ultimately should build as separate EXEs)"
//
//      return: [
//          block!  "Block of test numbers and failures"
//          text!   "Message if INCLUDE_TEST_LIBREBOL_NATIVE not defined"
//      ]
//      value "Argument that may be useful for ad hoc tests"
//          [any-stable?]
//  ]
//
DECLARE_NATIVE(TEST_LIBREBOL)
{
    INCLUDE_PARAMS_OF_TEST_LIBREBOL;

    Stable* v = ARG(VALUE);
    USED(v);

  start: {

  #if (! INCLUDE_TEST_LIBREBOL_NATIVE)
    return Init_Text(  // text! vs. failing to distinguish from test failure
        OUT,
        Make_Strand_UTF8(
            "TEST-LIBREBOL only if #define INCLUDE_TEST_LIBREBOL_NATIVE"
        )
    );
  #else
    // !!! NOTICE: We are pushing values to the data stack, but we can't hold
    // a pointer to the stack via PUSH() on the same line as doing an API
    // call, because API calls can move the stack.  This doesn't always make
    // an assert since argument order can vary across compilers.

} simple_add_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 1), NEWLINE_BEFORE);
    int i = rebUnboxInteger("1 +", rebI(2));

    Init_Boolean(PUSH(), i == 3);  // ^-- see NOTICE

} api_transient_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 2), NEWLINE_BEFORE);
    intptr_t getter = rebUnboxInteger64("api-transient -[Hello]-");
    Recycle();  // transient should survive a recycle
    Base* getter_base = p_cast(Base*, getter);
    bool equal = rebUnboxLogic("-[Hello]- = @", getter_base);

    Init_Boolean(PUSH(), equal);  // ^-- see NOTICE

} macro_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 3), NEWLINE_BEFORE);
    Api(Stable*) macro = rebStable(
        "inliner [x] [spread compose [append (x) first]]"
    );
    Api(Stable*) mtest1 = rebStable(rebRUN(macro), "[1 2 3]", "[d e f]");
    Copy_Cell(PUSH(), mtest1);  // ^-- see NOTICE
    rebRelease(mtest1);

    Set_Cell_Flag(Init_Integer(PUSH(), 4), NEWLINE_BEFORE);
    Api(Stable*) numbers = rebStable("[1 2 3]");
    Api(Stable*) letters = rebStable("[d e f]");
    Api(Stable*) mtest2 = rebStable(
        rebRUN(macro), rebR(numbers), rebR(letters)
    );
    Copy_Cell(PUSH(), mtest2);  // ^-- see NOTICE
    rebRelease(mtest2);

    rebRelease(macro);

} null_splicing_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 5), NEWLINE_BEFORE);
    bool is_null = rebUnboxLogic("null? @", nullptr);

    Init_Boolean(PUSH(), is_null);

} define_function_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 6), NEWLINE_BEFORE);
    Api(Stable*) action = Known_Stable_Api(rebFunction(
        Sum_Plus_1000_Spec,
        &Sum_Plus_1000_Impl
    ));

    int sum = rebUnboxInteger(
        "let /sum-plus-1000: @", action,
        "sum-plus-1000 5 15"
    );

    rebRelease(action);
    Init_Integer(PUSH(), sum);

} define_cpp_function_test: {

  #if NO_CPLUSPLUS_11
    Set_Cell_Flag(Init_Integer(PUSH(), 7), NEWLINE_BEFORE);
    Init_Integer(PUSH(), 1020);  // fake success result

    Set_Cell_Flag(Init_Integer(PUSH(), 8), NEWLINE_BEFORE);
    Api(Stable*) result_type = rebStable("[integer!]");  // fake success
    Copy_Cell(PUSH(), result_type);
    rebRelease(result_type);
  #else
    Api(Stable*) action = Known_Stable_Api(rebFunction(R"([
        -[Demonstration native that shadows ASSERT and ADD (C++ version)]-
        return: [integer!]
        assert [integer!]
        add [integer!]
    ])",
    [](RebolContext* librebol_binding) -> RebolBounce {
        int thousand = Subroutine();
        return rebValue("add + assert +", rebI(thousand));
    }));

    int sum = rebUnboxInteger(
        "let /sum-plus-1000: @", action,
        "sum-plus-1000 5 15"
    );
    Set_Cell_Flag(Init_Integer(PUSH(), 7), NEWLINE_BEFORE);
    Init_Integer(PUSH(), sum);

    RebolValue* result_type = rebValue(
        "pick return of", rebQ(action), "'spec"
    );
    Set_Cell_Flag(Init_Integer(PUSH(), 8), NEWLINE_BEFORE);
    Copy_Cell(PUSH(), result_type);
    rebRelease(result_type);

    rebRelease(action);
  #endif

} empty_variadic_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 9), NEWLINE_BEFORE);

    Value* noop = rebUndecayed("");
    assert(Is_Ghost(noop));
    Copy_Lifted_Cell(PUSH(), noop);
    rebRelease(noop);

} unstable_antiform_test: {

    Set_Cell_Flag(Init_Integer(PUSH(), 10), NEWLINE_BEFORE);

    Value* pack = rebUndecayed("pack [1 2]");
    Value* first = rebValue("@", pack);  // should decay automatically
    Value* second = rebValue("[_ {_}]: ^ ", pack);  // no decay, circled result
    Value* block = rebValue("reduce [", rebR(first), rebR(second), "]");
    rebRelease(pack);
    Copy_Cell(PUSH(), block);  // ^-- see NOTICE
    rebRelease(block);

} finish: {

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));

  #endif
}}


//
//  fuzz: native [
//
//  "Introduce periodic or deterministic fuzzing of out of memory errors"
//
//      return: []
//      factor [integer! percent!]
//  ]
//
DECLARE_NATIVE(FUZZ)
{
    INCLUDE_PARAMS_OF_FUZZ;

  #if TRAMPOLINE_COUNTS_TICKS && RUNTIME_CHECKS
    if (Is_Integer(ARG(FACTOR))) {
        g_mem.fuzz_factor = VAL_UINT32(ARG(FACTOR));
    }
    else {
        assert(Is_Percent(ARG(FACTOR)));
        g_mem.fuzz_factor = 10000 * VAL_DECIMAL(ARG(FACTOR));
    }
    return TRASH;
  #else
    UNUSED(ARG(FACTOR));
    panic ("FUZZ is only availble in RUNTIME_CHECKS builds");
  #endif
}
