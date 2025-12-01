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
        if (at == head and Is_Space(at))
            continue;  // (_) valid at head
        if (at == tail - 1 and Is_Space(at))
            continue;  // (_) valid at tail

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



// Use Level flag vs. a state byte, so that we can reuse the same frame
// for the calls to the generic TWEAK* implementations, even if it wants to
// use the state byte and do continuations/delegations.
//
#define LEVEL_FLAG_PICK_NOT_INITIAL_ENTRY  LEVEL_FLAG_MISCELLANEOUS


//
//  pick: native [
//
//  "Perform a path picking operation, same as `:(location).(picker)`"
//
//      return: "Picked value, or null if picker can't fulfill the request"
//          [any-stable?]
//      location [<opt-out> <unrun> plain?]  ; can't pick sigil'd/quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [<opt-out> any-stable?]
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

    Element* location = Element_ARG(LOCATION);
    Value* picker = ARG(PICKER);

    if (Get_Level_Flag(LEVEL, PICK_NOT_INITIAL_ENTRY))
        goto dispatch_generic;

  initial_entry: {

    Set_Level_Flag(LEVEL, PICK_NOT_INITIAL_ENTRY);

    Init_Dual_Nulled_Pick_Signal(LOCAL(DUAL));  // PICK, not POKE

    if (Is_Keyword(picker) or Is_Trash(picker))
        panic ("PICK with keyword or trash picker never allowed");

} dispatch_generic: { ////////////////////////////////////////////////////////

    Bounce bounce = opt Irreducible_Bounce(
        LEVEL,
        Dispatch_Generic(TWEAK_P, location, LEVEL)
    );

    if (bounce)
        return bounce;  // we will get a callback (if not error/etc.)

    if (Any_Lifted(OUT)) // if a value was found, it's returned as LIFTED
        goto pick_succeeded_out_is_lifted;

} tweak_gave_dual_signal: {

    // Non-LIFTED?s are signals in dual protocol

    if (Is_Error(OUT))
        return OUT;

    Value* dual = Known_Stable(OUT);

    if (Is_Action(dual))
        panic ("TWEAK* delegation machinery not done yet");

    if (Is_Dual_Nulled_Absent_Signal(dual))  // lifted is "NULL-but-present"
        return fail (Error_Bad_Pick_Raw(ARG(PICKER)));

    panic ("Non-ACTION! antiform returned by TWEAK* dual protocol");

} pick_succeeded_out_is_lifted: {

    require (
      Unliftify_Undecayed(OUT)
    );

    if (Not_Cell_Stable(OUT)) {
        assert(false);  // Note: once usermode TWEAK* exists, it may screw up
        panic ("TWEAK* returned a lifted unstable antiform");
    }

    return OUT;
}}


// Because TWEAK* is fundamental to getting and setting all WORD!s, bootstrap
// needs it to be able to be established before it can look up words that
// are in type specs.  So it has two variations: bootstrap-tweak* and tweak*.
//
// (The NATIVE native also has this dual nature.)
//
static Bounce Tweak_P_Native_Core(Level* level_)
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
//  tweak*: native:generic [  ; can call directly, but 99.9% want PICK/POKE
//
//  "Implementation detail of PICK and POKE, also underlies SET and GET"
//
//      return: "DUAL PROTOCOL: null means no writeback, lifted null is null"
//          [<null> word! frame! quoted! quasiform!]
//      location [<opt-out> fundamental?]  ; can't poke a quoted/quasi
//      picker [<opt-out> element?]
//      dual "DUAL PROTOCOL: action is accessor, lifted action is action"
//          [<null> word! frame! quoted! quasiform!]
//  ]
//
DECLARE_NATIVE(TWEAK_P)
//
// TWEAK* underlies the implementation of SET/GET (on TUPLE!, WORD!, etc.)
//
// If it receives a SPACE as the DUAL, then it acts "pick-like", and will
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
//
// ACTION!s are used as a currency to help with situations like in the FFI:
//
//    struct.million_ints_field.10
//
// TWEAK* is called on each path step.  But if the underlying C data for the
// STRUCT! is a C array of a million `int`s, then you don't want to explode
// that into a BLOCK! of a million INTEGER!s... to then only pick the 10th!
//
// Hence, being able to return an ACTION! to be a "lazy" result that can
// narrowly do the 10th pick is useful.  But this must be distinguishable
// from a PICK that actually returns an ACTION! as the value (e.g. if an
// OBJECT! had an ACTION! as a field).  Hence, TWEAK* uses the dual protocol.
{
    return Tweak_P_Native_Core(LEVEL);
}


//
//  tweak*-bootstrap: native [  ; don't put :GENERIC (covered by TWEAK*)
//
//  "(Bootstrap Variation of TWEAK*, before type spec lookups work)"
//
//      location
//      picker
//      dual
//  ]
//
DECLARE_NATIVE(TWEAK_P_BOOTSTRAP)
{
    return Tweak_P_Native_Core(LEVEL);
}


// Use Level flag vs. a state byte, so that we can reuse the same frame
// for the calls to the generic TWEAK* implementations, even if it wants to
// use the state byte and do continuations/delegations.
//
#define LEVEL_FLAG_POKE_NOT_INITIAL_ENTRY  LEVEL_FLAG_MISCELLANEOUS


//
//  poke: native [
//
//  "Perform a path poking operation, same as `(location).(picker): value`"
//
//      return: "Input value, or propagated error (no assignment on errors)"
//          [any-stable? error!]
//      location "(modified)"
//          [<opt-out> fundamental?]  ; can't poke a quoted/quasi
//      picker "Index offset, symbol, or other value to use as index"
//          [<opt-out> any-stable?]
//      ^value [any-stable? error! <void>]
//      {store}
//  ]
//
DECLARE_NATIVE(POKE)
{
    INCLUDE_PARAMS_OF_POKE;

    Element* location = Element_ARG(LOCATION);
    Value* picker = ARG(PICKER);
    Atom* atom = Atom_ARG(VALUE);

    if (Get_Level_Flag(LEVEL, POKE_NOT_INITIAL_ENTRY))
        goto dispatch_generic;

  initial_entry: {

    // 1. We don't want to limit the TWEAK* function from changing value, and
    //    also want it to have full use of SPARE, SCRATCH, and OUT.  So POKE
    //    has a slightly larger frame where it stores the value in a local.
    //
    // 2. We produce the DUAL argument in the same frame.  However, we don't
    //    have a way to produce the dual ACTION! to indicate an accessor.
    //    Should there be a POKE:DUAL, or just a SET:DUAL?

    if (Is_Keyword(picker) or Is_Trash(picker))
        panic ("PICK with keyword or trash picker never allowed");

    if (Is_Error(atom))
        return COPY(atom);  // bypass and don't do the poke

    Set_Level_Flag(LEVEL, POKE_NOT_INITIAL_ENTRY);

    Copy_Cell(Atom_ARG(STORE), atom);  // save value to return [1]

    Value* dual = ARG(VALUE);  // same slot (TWEAK* reuses this frame!) [2]

    if (Is_Void(atom)) {
        Init_Dual_Word_Remove_Signal(dual);  // signal to TWEAK*
    }
    else {
        Liftify(dual);  // TWEAK* expects QUOTED!/QUASIFORM! for literal DUAL
    }

    goto dispatch_generic;

} dispatch_generic: { ////////////////////////////////////////////////////////

    // 1. Though the POKE frame is slightly larger than that for TWEAK*, its
    //    memory layout is compatible with TWEAK*, and can be reused.

    Bounce bounce = opt Irreducible_Bounce(
        LEVEL,
        Dispatch_Generic(TWEAK_P, location, LEVEL)
    );

    if (bounce)
        return bounce;  // we will get a callback (if not error/etc.)

    Value* writeback = Known_Stable(OUT);

    if (not Is_Nulled(writeback))  // see TWEAK* for meaning of non-null
        panic (
            "Can't writeback to immediate in POKE (use TWEAK* if intentional)"
        );

    return COPY(Atom_ARG(STORE));  // stored ^VALUE argument was meta
}}


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

    Length a_len = Sequence_Len(a);
    Length b_len = Sequence_Len(b);

    if (a_len != b_len)  // different lengths not considered EQUAL? [1]
        return LOGIC(false);

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
            return LOGIC(false);
    }

    return LOGIC(true);
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
            return LOGIC(lesser);  // LESSER? result was meaningful

        bool strict = true;
        require (
          bool equal = Equal_Values(a_item, b_item, strict)
        );
        if (equal)
            continue;  // don't fret they couldn't compare with LESSER?

        return fail ("Couldn't compare values");  // fret
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
