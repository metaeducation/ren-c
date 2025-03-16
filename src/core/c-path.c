//
//  File: %c-path.h
//  Summary: "Core Path Dispatching and Chaining"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! See notes in %sys-path.h regarding the R3-Alpha path dispatch concept
// and regarding areas that need improvement.
//

#include "sys-core.h"


//
//  Trap_Init_Any_Sequence_At_Listlike: C
//
// REVIEW: This tries to do optimizations on the array you give it.
//
Option(Error*) Trap_Init_Any_Sequence_At_Listlike(
    Sink(Element) out,
    Heart heart,
    const Source* a,
    Offset offset
){
    assert(Any_Sequence_Kind(heart));
    assert(Is_Node_Managed(a));
    Assert_Flex_Term_If_Needed(a);
    assert(Is_Source_Frozen_Shallow(a));  // must be immutable (may be aliased)

    assert(offset < Array_Len(a));
    Length len_at = Array_Len(a) - offset;

    if (len_at < 2)
        return Error_Sequence_Too_Short_Raw();

    if (len_at == 2) {  // use optimization
        return Trap_Init_Any_Sequence_Pairlike(
            out,
            heart,
            Array_At(a, offset),
            Array_At(a, offset + 1)
        );
    }

    if (Try_Init_Any_Sequence_All_Integers(
        out,
        heart,
        Array_At(a, offset),
        len_at
    )){
        return nullptr;
    }

    const Element* tail = Array_Tail(a);
    const Element* head = Array_At(a, offset);  // head of what sequence uses
    const Element* at = head;
    for (; at != tail; ++at) {
        if (at == head and Is_Blank(at))
            continue;  // blank valid at head
        if (at == tail - 1 and Is_Blank(at))
            continue;  // blank valid at tail

        Option(Error*) error = Trap_Check_Sequence_Element(
            heart,
            at,
            at == head  // sigils and quotes not legal at head
        );
        if (error)
            return error;
    }

    // Since sequences are always at their head, it might seem the index
    // could be storage space for other forms of compaction (like counting
    // blanks at head and tail).  Otherwise it just sits at zero.
    //
    // One *big* reason to not use the space is because that creates a new
    // basic type that would require special handling in things like binding
    // code, vs. just running the paths for blocks.  A smaller reason not to
    // do it is that leaving it as an index allows for aliasing BLOCK! as
    // PATH! from non-head positions.

    Init_Series_At_Core(out, heart, a, offset, SPECIFIED);
    return nullptr;
}


//
//  /pick: native [
//
//  "Perform a path picking operation, same as `:(location).(picker)`"
//
//      return: "Picked value, or null if picker can't fulfill the request"
//          [any-value?]
//      location [<maybe> <unrun> element?]
//      picker "Index offset, symbol, or other value to use as index"
//          [<maybe> element? logic?]
//  ]
//
DECLARE_NATIVE(pick)
//
// In R3-Alpha, PICK was an "action", which dispatched on types through the
// "action mechanic" for the following types:
//
//     [any-series? map! gob! pair! date! time! tuple! bitset! port! varargs!]
//
// In Ren-C, PICK is rethought to use the same dispatch mechanic as tuples,
// to cut down on the total number of operations the system has to define.
{
    INCLUDE_PARAMS_OF_PICK;

    Value* picker = ARG(picker);

    if (Is_Okay(picker)) {
        Init_Integer(picker, 1);
    }
    else if (Is_Nulled(picker)) {
        Init_Integer(picker, 2);
    }

    Element* location = cast(Element*, ARG(location));
    return Run_Generic_Dispatch(location, LEVEL, CANON(PICK));
}


//
//  /poke: native [
//
//  "Perform a path poking operation, same as `(location).(picker): :value`"
//
//      return: "Updated location state"  ; not the input value, see [1]
//          [~null~ element?]
//      location "(modified)"
//          [<maybe> element?]
//      picker "Index offset, symbol, or other value to use as index"
//          [<maybe> element?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(poke)
//
// Note: In Ren-C, POKE underlies the implementation of SET on TUPLE!.
// For it to work, the return value is the cell contents that should be
// written back for immediate types.  This makes its return value somewhat
// useless for users, as it's an implementation detail, that if anything
// signals an error.
//
// For instance, if the overall result isn't null here, that means there was
// a modification which nothing is writing back.  It would be like saying:
//
//     >> poke 12-Dec-2012 'year 1999
//     == 12-Dec-1999
//
// Because the return value is not null, it's telling you that if a tuple
// was being poked with the value (e.g. obj.date.year: 1999) then the bits
// in obj.date would have to be changed.
{
    Element* location = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(location, LEVEL, CANON(POKE));
}


//
//  CT_Sequence: C
//
// "Compare Type" dispatcher for ANY-PATH? and ANY-TUPLE?.
//
// Note: R3-Alpha considered TUPLE! with any number of trailing zeros to
// be equivalent.  This meant `255.255.255.0` was equal to `255.255.255`.
// Why this was considered useful is not clear...as that would make a
// fully transparent alpha channel pixel equal to a fully opaque color.
// This behavior is not preserved in Ren-C, so `same-color?` or something
// else would be needed to get that intent.
//
REBINT CT_Sequence(const Cell* a, const Cell* b, bool strict)
{
    REBLEN len_a = Cell_Sequence_Len(a);
    REBLEN len_b = Cell_Sequence_Len(b);

    if (len_a != len_b)
        return len_a < len_b ? -1 : 1;

    DECLARE_ATOM (temp_a);
    DECLARE_ATOM (temp_b);

    REBLEN n;
    for (n = 0; n < len_a; ++n) {
        int compare = Cmp_Value(
            Copy_Sequence_At(temp_a, c_cast(Cell*, a), n),  // !!! cast
            Copy_Sequence_At(temp_b, c_cast(Cell*, b), n),  // !!! cast
            strict
        );
        if (compare != 0)
            return compare;
    }

    return 0;
}


IMPLEMENT_GENERIC(equal_q, any_sequence)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Sequence(ARG(value1), ARG(value2), REF(strict)) == 0);
}


IMPLEMENT_GENERIC(lesser_q, any_sequence)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    return LOGIC(CT_Sequence(ARG(value1), ARG(value2), true) == -1);
}


// !!! We need to zeroify 1.2.3 as 0.0.0 which is used in things like the
// ZERO? test (we don't have a hook for ZERO?, it's built on top of EQUAL?
// and zeroify).  However should it be willing to zeroify any tuple, such
// as A.B.C, or should it only be willing to zeroify things that are all
// numbers?  For now, let's insist on zeroification of numeric sequences
// and wait to see if a more general need arises.
//
IMPLEMENT_GENERIC(zeroify, any_sequence)
{
    INCLUDE_PARAMS_OF_ZEROIFY;

    Element* sequence = Element_ARG(example);

    Heart heart = Cell_Heart(sequence);
    assert(Any_Sequence_Kind(heart));

    REBLEN len = Cell_Sequence_Len(sequence);
    REBLEN n;
    for (n = 0; n < len; ++n) {
        if (not Is_Integer(Copy_Sequence_At(SPARE, sequence, n)))
            return FAIL("Can only zeroify sequences of integers at this time");
        Init_Integer(PUSH(), 0);
    }
    Option(Error*) error = Trap_Pop_Sequence(OUT, heart, STACK_BASE);
    assert(not error);
    UNUSED(error);

    return OUT;
}
