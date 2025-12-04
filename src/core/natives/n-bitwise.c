//
//  file: %n-bitwise.c
//  summary: "Native functions for bitwise math"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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
// Note: Instead of individual functions, it was thought that there might
// be a dialect called BITWISE more generically suited to bitwise manipuation,
// where words like AND, OR, etc. would not be logic based but bitwise based.
//
//     foo: bitwise [(a and b) or c]
//
// This idea was held up by questions about binding, e.g. whether the AND,
// OR, etc. would be keywords or if binding would just override in the
// block.  With pure virtual binding, it may be feasible to do this using
// binding instead of recognizing 'and 'or etc. literally.
//

#include "sys-core.h"


//
//  bitwise-not: native:generic [
//
//  "Returns the one's complement value"
//
//      return: [logic? integer! tuple! blob!]
//      value [logic? integer! tuple! blob!]
//  ]
//
DECLARE_NATIVE(BITWISE_NOT)
{
    if (Is_Logic(ARG_N(1))) {
        bool b1 = Cell_Logic(ARG_N(1));
        return LOGIC(not b1);
    }

    Element* e = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e, LEVEL, CANON(BITWISE_NOT));
}


INLINE bool Math_Arg_For_Logic(Stable* arg)
{
    if (Is_Logic(arg))
        return Cell_Logic(arg);

    if (Is_Space(arg))
        return false;

    panic (Error_Unexpected_Type(TYPE_INTEGER, Datatype_Of(arg)));
}


//
//  bitwise-and: native:generic [
//
//  "Bitwise AND of two values"
//
//      return: [logic? integer! char? tuple! blob!]
//      value1 [logic? integer! char? tuple! blob!]
//      value2 [logic? integer! char? tuple! blob!]
//  ]
//
DECLARE_NATIVE(BITWISE_AND)
{
    if (Is_Logic(ARG_N(1))) {
        bool b1 = Cell_Logic(ARG_N(1));
        bool b2 = Math_Arg_For_Logic(ARG_N(2));
        return LOGIC(b1 and b2);
    }

    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(BITWISE_AND));
}


//
//  bitwise-or: native:generic [
//
//  "Bitwise OR of two values"
//
//      return: [logic? integer! char? tuple! blob!]
//      value1 [logic? integer! char? tuple! blob!]
//      value2 [logic? integer! char? tuple! blob!]
//  ]
//
DECLARE_NATIVE(BITWISE_OR)
{
    if (Is_Logic(ARG_N(1))) {
        bool b1 = Cell_Logic(ARG_N(1));
        bool b2 = Math_Arg_For_Logic(ARG_N(2));
        return LOGIC(b1 or b2);
    }

    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(BITWISE_OR));
}


//
//  bitwise-xor: native:generic [
//
//  "Bitwise XOR of two values"
//
//      return: [logic? integer! char? tuple! blob!]
//      value1 [logic? integer! char? tuple! blob!]
//      value2 [logic? integer! char? tuple! blob!]
//  ]
//
DECLARE_NATIVE(BITWISE_XOR)
{
   if (Is_Logic(ARG_N(1))) {
        bool b1 = Cell_Logic(ARG_N(1));
        bool b2 = Math_Arg_For_Logic(ARG_N(2));
        return LOGIC(b1 != b2);
    }

    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(BITWISE_XOR));
}


//
//  bitwise-and-not: native:generic [
//
//  "Bitwise AND NOT of two values"
//
//      return: [logic? integer! char? tuple! blob!]
//      value1 [logic? integer! char? tuple! blob!]
//      value2 [logic? integer! char? tuple! blob!]
//  ]
//
DECLARE_NATIVE(BITWISE_AND_NOT)
{
   if (Is_Logic(ARG_N(1))) {
        bool b1 = Cell_Logic(ARG_N(1));
        bool b2 = Math_Arg_For_Logic(ARG_N(2));
        return LOGIC(b1 and not b2);
    }

    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(BITWISE_AND_NOT));
}


//
// The SHIFT native uses negation of an unsigned number.  Although the
// operation is well-defined in the C language, it is usually a mistake.
// MSVC warns about it, so temporarily disable that.
//
// !!! The usage of negation of unsigned in SHIFT is from R3-Alpha.  Should it
// be rewritten another way?
//
// http://stackoverflow.com/a/36349666/211160
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (disable : 4146)
#endif


//
//  shift: native [
//
//  "Shifts an integer left or right by a number of bits"
//
//      return: [integer!]
//      value [integer!]
//      bits "Positive for left shift, negative for right shift"
//          [integer!]
//      :logical "Logical shift (sign bit ignored)"
//  ]
//
DECLARE_NATIVE(SHIFT)
{
    INCLUDE_PARAMS_OF_SHIFT;

    REBI64 b = VAL_INT64(ARG(BITS));
    Stable* a = ARG(VALUE);

    if (b < 0) {
        REBU64 c = - cast(REBU64, b); // defined, see note on #pragma above
        if (c >= 64) {
            if (Bool_ARG(LOGICAL))
                mutable_VAL_INT64(a) = 0;
            else
                mutable_VAL_INT64(a) >>= 63;
        }
        else {
            if (Bool_ARG(LOGICAL))
                mutable_VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) >> c;
            else
                mutable_VAL_INT64(a) >>= cast(REBI64, c);
        }
    }
    else {
        if (b >= 64) {
            if (Bool_ARG(LOGICAL))
                mutable_VAL_INT64(a) = 0;
            else if (VAL_INT64(a) != 0)
                panic (Error_Overflow_Raw());
        }
        else {
            if (Bool_ARG(LOGICAL))
                mutable_VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) << b;
            else {
                REBU64 c = cast(REBU64, INT64_MIN) >> b;
                REBU64 d = VAL_INT64(a) < 0
                    ? - cast(REBU64, VAL_INT64(a)) // again, see #pragma
                    : cast(REBU64, VAL_INT64(a));
                if (c <= d) {
                    if ((c < d) || (VAL_INT64(a) >= 0))
                        panic (Error_Overflow_Raw());

                    mutable_VAL_INT64(a) = INT64_MIN;
                }
                else
                    mutable_VAL_INT64(a) <<= b;
            }
        }
    }

    return COPY(ARG(VALUE));
}


// See above for the temporary disablement and reasoning.
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (default : 4146)
#endif
