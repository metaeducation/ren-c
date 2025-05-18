//
//  file: %c-path.h
//  summary: "Core Path Dispatching and Chaining"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    assert(Any_Sequence_Type(heart));
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
        return SUCCESS;
    }

    const Element* tail = Array_Tail(a);
    const Element* head = Array_At(a, offset);  // head of what sequence uses
    const Element* at = head;
    for (; at != tail; ++at) {
        if (at == head and Is_Space(at))
            continue;  // (_) valid at head
        if (at == tail - 1 and Is_Space(at))
            continue;  // (_) valid at tail

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
    return SUCCESS;
}


//
//  pick: native:generic [
//
//  "Perform a path picking operation, same as `:(location).(picker)`"
//
//      return: "Picked value, or null if picker can't fulfill the request"
//          [any-value?]
//      location [<opt-out> <unrun> fundamental?]  ; can't pick from quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [<opt-out> element? logic?]
//  ]
//
DECLARE_NATIVE(PICK)
//
// R3-Alpha had PD_Xxx() functions which were hooks for "Path Dispatch", which
// was distinct from the code that ran the PICK action.
//
// Ren-C rethought this to build tuple dispatch on top of PICK and POKE.
// So `foo.(expr)` and `pick foo (expr)` will always give the same answer
//
// 1. !!! PICK in R3-Alpha historically would use a logic TRUE to get the first
//    element in a list, and a logic FALSE to get the second.  It did this
//    regardless of how many elements were in the list.  For safety, it has
//    been suggested lists > length 2 should fail).
{
    INCLUDE_PARAMS_OF_PICK;

    Value* picker = ARG(PICKER);

    if (Is_Okay(picker)) {  // !!! should we verify that LENGTH-OF is 2? [1]
        Init_Integer(picker, 1);
    }
    else if (Is_Nulled(picker)) {
        Init_Integer(picker, 2);
    }
    assert(not Is_Antiform(picker));  // LOGIC? is the only supported antiform

    Element* location = Element_ARG(LOCATION);
    return Dispatch_Generic(PICK, location, LEVEL);
}


//
//  poke: native [
//
//  "Perform a path poking operation, same as `(location).(picker): ^value`"
//
//      return: "Input value, or propagated error (no assignment on errors)"
//          [any-value? error!]
//      location "(modified)"
//          [<opt-out> fundamental?]  ; can't poke a quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [<opt-out> element?]
//      ^value [any-value? error! void?]
//      <local> store
//  ]
//
DECLARE_NATIVE(POKE)
//
// 1. We don't want to limit the POKE* function from changing value, and
//    also want it to have full use of SPARE, SCRATCH, and OUT.  So POKE
//    just has a slightly larger frame where it stores the value in a local.
{
    INCLUDE_PARAMS_OF_POKE;

    Element* location = Element_ARG(LOCATION);
    USED(ARG(PICKER));  // passed to handler via LEVEL
    Element* meta_value = Element_ARG(VALUE);

    if (Is_Meta_Of_Error(meta_value)) {
        Copy_Cell(OUT, meta_value);
        return Meta_Unquotify_Undecayed(OUT);
    }

    Copy_Cell(ARG(STORE), meta_value);  // save value to return [1]

    Bounce bounce = Dispatch_Generic(POKE_P, location, LEVEL);

    if (bounce != nullptr)
        return PANIC("Cannot write-back to location in POKE");

    Copy_Cell(OUT, ARG(STORE));
    return Meta_Unquotify_Known_Stable(OUT);
}


//
//  poke*: native:generic [
//
//  "Implementation helper for POKE"
//
//      return: "Updated location state"  ; not the input value, see [1]
//          [null? any-value?]
//      location "(modified)"
//          [<opt-out> fundamental?]  ; can't poke a quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [<opt-out> element?]
//      ^value [any-value? void?]
//  ]
//
DECLARE_NATIVE(POKE_P)
//
// Users can call POKE* directly, but usually they will use POKE which gives
// back the value that was poked.
//
// Note: In Ren-C, POKE* underlies the implementation of SET on TUPLE!.
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
    return Dispatch_Generic(POKE_P, location, LEVEL);
}


// 1. R3-Alpha and Red considered TUPLE! with any number of trailing zeros to
//    be equivalent when not strict.  So (255.255.255.0 = 255.255.255), but
//    if this is interesting it should be SAME-COLOR? or something like that.
//
IMPLEMENT_GENERIC(EQUAL_Q, Any_Sequence)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);
    bool strict = not Bool_ARG(RELAX);

    Length a_len = Cell_Sequence_Len(a);
    Length b_len = Cell_Sequence_Len(b);

    if (a_len != b_len)  // different lengths not considered EQUAL? [1]
        return LOGIC(false);

    Sink(Element) a_item = SCRATCH;
    Sink(Element) b_item = SPARE;

    Offset n;
    for (n = 0; n < a_len; ++n) {
        Copy_Sequence_At(a_item, a, n);
        Copy_Sequence_At(b_item, b, n);

        if (not Equal_Values(a_item, b_item, strict))
            return LOGIC(false);
    }

    return LOGIC(true);
}


IMPLEMENT_GENERIC(LESSER_Q, Any_Sequence)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);

    Length a_len = Cell_Sequence_Len(a);
    Length b_len = Cell_Sequence_Len(b);

    if (a_len != b_len)
        return FAIL("Temporarily disallow compare unequal length sequences");

    Sink(Element) a_item = SCRATCH;
    Sink(Element) b_item = SPARE;

    Offset n;
    for (n = 0; n < a_len; ++n) {
        Copy_Sequence_At(a_item, a, n);
        Copy_Sequence_At(b_item, b, n);

        bool lesser;
        if (Try_Lesser_Value(&lesser, a_item, b_item))
            return LOGIC(lesser);  // LESSER? result was meaningful

        bool strict = true;
        if (Equal_Values(a_item, b_item, strict))
            continue;  // don't fret they couldn't compare with LESSER?

        return FAIL("Couldn't compare values");  // fret
    }

    return LOGIC(true);
}


// !!! We need to zeroify 1.2.3 as 0.0.0 which is used in things like the
// ZERO? test (we don't have a hook for ZERO?, it's built on top of EQUAL?
// and zeroify).  However should it be willing to zeroify any tuple, such
// as A.B.C, or should it only be willing to zeroify things that are all
// numbers?  For now, let's insist on zeroification of numeric sequences
// and wait to see if a more general need arises.
//
IMPLEMENT_GENERIC(ZEROIFY, Any_Sequence)
{
    INCLUDE_PARAMS_OF_ZEROIFY;

    Element* sequence = Element_ARG(EXAMPLE);

    Heart heart = Heart_Of_Builtin(sequence);
    assert(Any_Sequence_Type(heart));

    REBLEN len = Cell_Sequence_Len(sequence);
    REBLEN n;
    for (n = 0; n < len; ++n) {
        if (not Is_Integer(Copy_Sequence_At(SPARE, sequence, n)))
            return PANIC("Can only zeroify sequences of integers at this time");
        Init_Integer(PUSH(), 0);
    }
    Option(Error*) error = Trap_Pop_Sequence(OUT, heart, STACK_BASE);
    assert(not error);
    UNUSED(error);

    return OUT;
}
