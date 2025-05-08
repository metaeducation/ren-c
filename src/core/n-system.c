//
//  file: %n-system.c
//  summary: "native functions for system operations"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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

#include "sys-core.h"


//
//  halt: native [
//
//  "Stops evaluation and returns to the input prompt."
//
//      ; No arguments
//  ]
//
DECLARE_NATIVE(HALT)
{
    INCLUDE_PARAMS_OF_HALT;

    Copy_Cell(OUT, NAT_VALUE(HALT));
    CONVERT_NAME_TO_THROWN(OUT, NULLED_CELL);
    return OUT;
}


//
//  quit: native [
//
//  {Stop evaluating and return control to command shell or calling script.}
//
//      atom "See: http://en.wikipedia.org/wiki/Exit_status"
//          [<end> any-value!]
//      /value "Allow non-integers for yielding values to calling scripts"
//  ]
//
DECLARE_NATIVE(QUIT)
//
// QUIT is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :quit`.
{
    INCLUDE_PARAMS_OF_QUIT;

    Value* atom = ARG(ATOM);

    if (Bool_ARG(VALUE)) {
        // don't convert end to integer 0 for success synonym
    }
    else {
        if (Is_Endish_Nulled(atom))
            Init_Integer(atom, 1);
        else if (not Is_Integer(atom))
            fail ("QUIT must receive INTEGER! unless /VALUE used");
    }

    Copy_Cell(OUT, NAT_VALUE(QUIT));

    CONVERT_NAME_TO_THROWN(OUT, ARG(ATOM));

    return BOUNCE_THROWN;
}


//
//  exit-rebol: native [
//
//  {Stop the current Rebol interpreter, cannot be caught by CATCH/QUIT.}
//
//      /with
//          {Yield a result (mapped to an integer if given to shell)}
//      value [any-element!]
//          "See: http://en.wikipedia.org/wiki/Exit_status"
//  ]
//
DECLARE_NATIVE(EXIT_REBOL)
{
    INCLUDE_PARAMS_OF_EXIT_REBOL;

    int code;
    if (Bool_ARG(WITH))
        code = VAL_INT32(ARG(VALUE));
    else
        code = EXIT_SUCCESS;

    exit(code);
}


//
//  recycle: native [
//
//  "Recycles unused memory."
//
//      return: [~null~ integer!]
//          {Number of series nodes recycled (if applicable)}
//      /off
//          "Disable auto-recycling"
//      /on
//          "Enable auto-recycling"
//      /ballast
//          "Trigger for auto-recycle (memory used)"
//      size [integer!]
//      /torture
//          "Constant recycle (for internal debugging)"
//      /watch
//          "Monitor recycling (debug only)"
//      /verbose
//          "Dump out information about series being recycled (debug only)"
//  ]
//
DECLARE_NATIVE(RECYCLE)
{
    INCLUDE_PARAMS_OF_RECYCLE;

    if (Bool_ARG(OFF)) {
        GC_Disabled = true;
        return nullptr;
    }

    if (Bool_ARG(ON)) {
        GC_Disabled = false;
        TG_Ballast = TG_Max_Ballast;
    }

    if (Bool_ARG(BALLAST)) {
        TG_Max_Ballast = VAL_INT32(ARG(SIZE));
        TG_Ballast = TG_Max_Ballast;
    }

    if (Bool_ARG(TORTURE)) {
        GC_Disabled = false;
        TG_Ballast = 0;
    }

    if (GC_Disabled)
        return nullptr; // don't give misleading "0", since no recycle ran

    REBLEN count;

    if (Bool_ARG(VERBOSE)) {
      #if NO_RUNTIME_CHECKS
        fail (Error_Debug_Only_Raw());
      #else
        Flex* sweeplist = Make_Flex(100, sizeof(Node*));
        count = Recycle_Core(false, sweeplist);
        assert(count == Flex_Len(sweeplist));

        REBLEN index = 0;
        for (index = 0; index < count; ++index) {
            Node* node = *Flex_At(Node*, sweeplist, index);
            PROBE(node);
            UNUSED(node);
        }

        Free_Unmanaged_Flex(sweeplist);

        REBLEN recount = Recycle_Core(false, nullptr);
        assert(recount == count);
      #endif
    }
    else {
        count = Recycle();
    }

    if (Bool_ARG(WATCH)) {
      #if NO_RUNTIME_CHECKS
        fail (Error_Debug_Only_Raw());
      #else
        // There might should be some kind of generic way to set these kinds
        // of flags individually, perhaps having them live in SYSTEM/...
        //
        Reb_Opts->watch_recycle = not Reb_Opts->watch_recycle;
        Reb_Opts->watch_expand = not Reb_Opts->watch_expand;
      #endif
    }

    return Init_Integer(OUT, count);
}


//
//  check: native [
//
//  "Run an integrity check on a value in debug builds of the interpreter"
//
//      value [any-value!]
//          {System will terminate abnormally if this value is corrupt.}
//  ]
//
DECLARE_NATIVE(CHECK)
//
// This forces an integrity check to run on a series.  In R3-Alpha there was
// no debug build, so this was a simple validity check and it returned an
// error on not passing.  But Ren-C is designed to have a debug build with
// checks that aren't designed to fail gracefully.  So this just runs that
// assert rather than replicating code here that can "tolerate" a bad series.
// Review the necessity of this native.
{
    INCLUDE_PARAMS_OF_CHECK;

#if NO_RUNTIME_CHECKS
    UNUSED(ARG(VALUE));

    fail (Error_Debug_Only_Raw());
#else
    Value* value = ARG(VALUE);

    // For starters, check the memory (if it's bad, all other bets are off)
    //
    Check_Memory_Debug();

    // !!! Should call generic ASSERT_VALUE macro with more cases
    //
    if (Any_Series(value)) {
        Assert_Flex(Cell_Flex(value));
    }
    else if (Any_Context(value)) {
        ASSERT_CONTEXT(Cell_Varlist(value));
    }
    else if (Is_Action(value)) {
        Assert_Array(VAL_ACT_PARAMLIST(value));
        Assert_Array(VAL_ACT_DETAILS(value));
    }

    return LOGIC(true);
#endif
}


// Fast count of number of binary digits in a number:
//
// https://stackoverflow.com/a/15327567/211160
//
int ceil_log2(unsigned long long x) {
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for (i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return y;
}


//
//  c-debug-break-at: native [
//
//  {Break at known evaluation point (only use when running under C debugger}
//
//      return: [~null~]
//      tick [<opt-out> integer!]
//          {Get from PANIC, Level.tick, Stub.tick, Cell.tick}
//      /relative
//          {TICK parameter represents a count relative to the current tick}
//      /compensate
//          {Round tick up, as in https://math.stackexchange.com/q/2521219/}
// ]
//
DECLARE_NATIVE(C_DEBUG_BREAK_AT)
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK_AT;

  #if RUNTIME_CHECKS && DEBUG_COUNT_TICKS
    if (Bool_ARG(COMPENSATE)) {
        //
        // Imagine two runs of Rebol console initialization.  In the first,
        // the tick count is 304 when C-DEBUG-BREAK/COMPENSATE is called,
        // right after command line parsing.  Later on a panic() is hit and
        // reports tick count 1020 in the crash log.
        //
        // Wishing to pick apart the bug before it happens, the Rebol Core
        // Developer then re-runs the program with `--breakpoint=1020`, hoping
        // to break at that tick, to catch the downstream appearance of the
        // tick in the panic().  But since command-line processing is in
        // usermode, the addition of the parameter throws off the ticks!
        //
        // https://en.wikipedia.org/wiki/Observer_effect_(physics)
        //
        // Let's say that after the command line processing, it still runs
        // C-DEBUG-BREAK/COMPENSATE, this time at tick 403.  Imagine our goal
        // is to make the parameter to /COMPENSATE something that can be used
        // to conservatively guess the same value to set the tick to, and
        // that /COMPENSATE ARG(BOUND) that gives a maximum of how far off we
        // could possibly be from the "real" tick. (e.g. "argument processing
        // took no more than 200 additional ticks", which this is consistent
        // with...since 403-304 = 99).
        //
        // The reasoning for why the formula below works for this rounding is
        // given in this StackExchange question and answer:
        //
        // https://math.stackexchange.com/q/2521219/
        //
        Tick one = 1; // MSVC gives misguided warning for cast(Tick, 1)
        TG_Tick =
            (one << (ceil_log2(TG_Tick) + 1))
            + VAL_INT64(ARG(TICK))
            - 1;
        return nullptr;
    }

    if (Bool_ARG(RELATIVE))
        TG_Break_At_Tick = level_->tick + 1 + VAL_INT64(ARG(TICK));
    else
        TG_Break_At_Tick = VAL_INT64(ARG(TICK));
    return nullptr;
  #else
    UNUSED(ARG(TICK));
    UNUSED(ARG(RELATIVE));
    UNUSED(Bool_ARG(COMPENSATE));

    fail (Error_Debug_Only_Raw());
  #endif
}


//
//  c-debug-break: native [
//
//  "Break at next evaluation point (only use when running under C debugger)"
//
//      return: [~void~]
//  ]
//
DECLARE_NATIVE(C_DEBUG_BREAK)
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK;

  #if RUNTIME_CHECKS && DEBUG_COUNT_TICKS
    //
    // For instance with:
    //
    //    print c-debug-break mold value
    //
    // Queue it so the break happens right before the MOLD, not after it
    // happened and has been passed as an argument.
    //
    TG_Break_At_Tick = level_->tick + 1;

    return Init_Void(OUT);;
  #else
    fail (Error_Debug_Only_Raw());
  #endif
}


//
//  test: native [
//
//  "This is a place to put test code in debug builds."
//
//      return: [any-value!]
//          {For maximum freedom, can be anything}
//      :value [<end> any-element!]
//          {An argument (which test code may or may not use)}
//  ]
//
DECLARE_NATIVE(TEST)
{
    INCLUDE_PARAMS_OF_TEST;
    UNUSED(ARG(VALUE));

    rebElide(
        "print", rebI(10), // won't leak, rebI() releases during variadic walk
        "if null [print", rebInteger(30), "]"  // rebInteger() leaks here
    );

    return nullptr;
}
