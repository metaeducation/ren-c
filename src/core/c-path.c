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
//  Init_Any_Sequence_At_Listlike: C
//
// REVIEW: This tries to do optimizations on the array you give it.
//
Result(Element*) Init_Any_Sequence_At_Listlike(
    Sink(Element) out,
    Heart heart,
    const Source* a,
    Offset offset
){
    assert(Any_Sequence_Type(heart));
    assert(Is_Base_Managed(a));
    Assert_Flex_Term_If_Needed(a);
    assert(Is_Source_Frozen_Shallow(a));  // must be immutable (may be aliased)

    assert(offset < Array_Len(a));
    Length len_at = Array_Len(a) - offset;

    if (len_at < 2)
        return fail (Error_Sequence_Too_Short_Raw());

    if (len_at == 2) {  // use optimization
        return Init_Any_Sequence_Pairlike(
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
        return out;
    }

    const Element* tail = Array_Tail(a);
    const Element* head = Array_At(a, offset);  // head of what sequence uses
    const Element* at = head;
    for (; at != tail; ++at) {
        if (at == head and Is_Blank(at))
            continue;  // blank valid at head
        if (at == tail - 1 and Is_Blank(at))
            continue;  // blank valid at tail

        trap (
          Check_Sequence_Element(
            heart,
            at,
            at == head  // sigils and quotes not legal at head
        ));
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

    return Init_Series_At_Core(out, heart, a, offset, SPECIFIED);
}


//
//  /pick: native [
//
//  "Perform a path picking operation; same code as `(location).(picker)`"
//
//      return: [any-stable? failure!]
//      ^location [
//          plain? "Ordinary value to pick from"  ; can't pick sigil'd/quoted
//          action! "Give back PARAMETER! for the given picker"
//      ]
//      picker "Index offset, symbol, or other value to use as index"
//          [any-stable?]
//      {dual}  ; slot in position of DUAL for TWEAK*
//  ]
//
DECLARE_NATIVE(PICK)
//
// R3-Alpha had PD_Xxx() functions which were hooks for "Path Dispatch", which
// was distinct from the code that ran the PICK action.
//
// Ren-C rethought this to build tuple dispatch on top of PICK and POKE.
// So `foo.(expr)` and `pick foo (expr)` will always give the same answer.
// There is one function called TWEAK* that performs a superset of PICK/POKE
// for one step's-worth of tuple.
{
    INCLUDE_PARAMS_OF_PICK;

    Value* location = ARG(LOCATION);
    Stable* picker = ARG(PICKER);

    Copy_Lifted_Cell(PUSH(), location);
    Copy_Lifted_Cell(PUSH(), picker);

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));
    heeded (Init_Null_Signifying_Tweak_Is_Pick(SCRATCH));
    heeded (STATE = ST_TWEAK_GETTING);

    Option(Error*) e = Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare();
    Drop_Data_Stack_To(STACK_BASE);

    if (e)  // should be willing to bounce to trampoline...
        return fail (unwrap e);

    require (
      Unlift_Cell_No_Decay(SPARE)  // !!! faster version, known ok antiform?
    );

    if (Is_Void(SPARE)) // workaround for old pick tolerance on missing
        return fail (Error_Bad_Pick_Raw(picker));

    require (
      Decay_If_Unstable(SPARE)
    );
    return COPY_TO_OUT(SPARE);
}


//
//  /tweak*: native:generic [  ; can call directly, but 99.9% want PICK/POKE
//
//  "Implementation detail of PICK and POKE, also underlies SET and GET"
//
//      return: [
//          logic!  "null: bad picker, okay: no writeback needed"
//          quoted! quasiform!  "lifted cell for bits to update in container"
//          frame! ^word! ^tuple! space? parameter! "indirect writeback"
//      ]
//      location [fundamental?]  ; can't poke a quoted/quasi
//      picker [any-stable?]
//      dual [
//          <null>  "pick semantics (vs. poke)"
//          quoted! quasiform!  "lifted value to poke"
//          frame! ^word! ^tuple! space?  "store indirection instruction"
//          word!  "protect/unprotect signal (temporary!)"
//      ]
//  ]
//
DECLARE_NATIVE(TWEAK_P)
//
// TWEAK* underlies the implementation of SET/GET (on TUPLE!, WORD!, etc.)
//
// If it receives NULL as the DUAL, then it acts "pick-like", and will
// tell you what's in that cell as the result...using the dual protocol.
//
// If it receives any other state, then it will use that to modify the
// target... and it will return whatever bits would be required to "write
// back" the cell in the stored location to reflect the updated state.  This
// update can be a complete overwrite of the value, or it can be a "tweak"
// e.g. to ask to update the PROTECTED bits of a cell.
//
// To give an example: if you're asking to poke, that is translated into a
// TWEAK request.  If TWEAK's result isn't null here, that means there was
// a modification which nothing is writing back.  It would be like saying:
//
//     >> poke 12-Dec-2012 'year 1999
//     == 12-Dec-1999
//
// Because the return value is not null, it's telling you that if a tuple
// was being poked with the value (e.g. obj.date.year: 1999) then the bits
// in obj.date would have to be changed.
{
    INCLUDE_PARAMS_OF_TWEAK_P;  // TWEAK* must be frame compatible w/PICK+POKE

    Element* location = Element_ARG(LOCATION);
    USED(ARG(PICKER));
    USED(ARG(DUAL));

    // more ARG(...) may be in this location if POKE called us, reusing frame

    possibly(Get_Level_Flag(LEVEL, MISCELLANEOUS));  // reserved for POKE's use

    return Dispatch_Generic(TWEAK_P, location, LEVEL);
}


//
//  /tweak*-unchecked: native [  ; don't put :GENERIC (covered by TWEAK*)
//
//  "(Bootstrap Variation of TWEAK*, before type spec lookups work)"
//
//      location
//      picker
//      dual
//  ]
//
DECLARE_NATIVE(TWEAK_P_UNCHECKED)
//
// This variation of TWEAK* has no typechecking.  During bootstrap it is put
// in LIB(TWEAK_P) to use while TWEAK* is unavailable to look up the words in
// type specs.  Once TWEAK* is available, LIB(TWEAK_P) is overwritten by the
// typechecked version defined in DECLARE_NATIVE(TWEAK_P).
//
// 1. To minimize visibility of the trick, there is no LIB(TWEAK_P_UNCHECKED)
{
    STATIC_ASSERT(SYM_TWEAK_P_UNCHECKED > MAX_SYM_LIB_PREMADE);  // [1]

    return Apply_Cfunc(NATIVE_CFUNC(TWEAK_P), LEVEL);
}


// Use Level flag vs. a state byte, so that we can reuse the same frame
// for the calls to the generic TWEAK* implementations, even if it wants to
// use the state byte and do continuations/delegations.
//
#define LEVEL_FLAG_POKE_NOT_INITIAL_ENTRY  LEVEL_FLAG_MISCELLANEOUS


//
//  /poke: native [
//
//  "Poke a tuple as in `(location).(picker): value`, returns value"
//
//      return: [any-value? failure!]
//      location "(modified)"
//          [fundamental?]  ; can't poke a quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [any-stable?]
//      ^value "FAILURE! will be piped through without doing the poke"
//          [any-value? failure!]
//      {store}
//  ]
//
DECLARE_NATIVE(POKE)
{
    INCLUDE_PARAMS_OF_POKE;

    Element* location = Element_ARG(LOCATION);
    Stable* picker = ARG(PICKER);
    Value* v = ARG(VALUE);

    Copy_Lifted_Cell(PUSH(), location);
    Copy_Lifted_Cell(PUSH(), picker);

    heeded (Corrupt_Cell_If_Needful(SPARE));

    Copy_Lifted_Cell(SCRATCH, v);  // !!! review decaying logic

    STATE = ST_TWEAK_SETTING;

    Option(Error*) e = Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare();
    Drop_Data_Stack_To(STACK_BASE);

    if (e)
        return fail (unwrap e);

    return Copy_Cell(OUT, v);
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
    bool strict = not ARG(RELAX);

    Length a_len = Sequence_Len(a);
    Length b_len = Sequence_Len(b);

    if (a_len != b_len)  // different lengths not considered EQUAL? [1]
        return LOGIC_OUT(false);

    Sink(Element) a_item = SCRATCH;
    Sink(Element) b_item = SPARE;

    Offset n;
    for (n = 0; n < a_len; ++n) {
        Copy_Sequence_At(a_item, a, n);
        Copy_Sequence_At(b_item, b, n);

        require (
          bool equal = Equal_Values(a_item, b_item, strict)
        );
        if (not equal)
            return LOGIC_OUT(false);
    }

    return LOGIC_OUT(true);
}


IMPLEMENT_GENERIC(LESSER_Q, Any_Sequence)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);

    Length a_len = Sequence_Len(a);
    Length b_len = Sequence_Len(b);

    if (a_len != b_len)
        return fail ("Temporarily disallow compare unequal length sequences");

    Sink(Element) a_item = SCRATCH;
    Sink(Element) b_item = SPARE;

    Offset n;
    for (n = 0; n < a_len; ++n) {
        Copy_Sequence_At(a_item, a, n);
        Copy_Sequence_At(b_item, b, n);

        bool lesser;
        if (Try_Lesser_Value(&lesser, a_item, b_item))
            return LOGIC_OUT(lesser);  // LESSER? result was meaningful

        bool strict = true;
        require (
          bool equal = Equal_Values(a_item, b_item, strict)
        );
        if (equal)
            continue;  // don't fret they couldn't compare with LESSER?

        return fail ("Couldn't compare values");  // fret
    }

    return LOGIC_OUT(true);
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

    REBLEN len = Sequence_Len(sequence);
    REBLEN n;
    for (n = 0; n < len; ++n) {
        if (not Is_Integer(Copy_Sequence_At(SPARE, sequence, n)))
            panic ("Can only zeroify sequences of integers at this time");
        Init_Integer(PUSH(), 0);
    }

    assume (
      Pop_Sequence(OUT, heart, STACK_BASE)
    );
    return OUT;
}
