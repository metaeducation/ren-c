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
Option(VarList*) Trap_Init_Any_Sequence_At_Listlike(
    Sink(Element*) out,
    Heart heart,
    const Array* a,
    Offset offset
){
    assert(Any_Sequence_Kind(heart));
    assert(Is_Node_Managed(a));
    Assert_Flex_Term_If_Needed(a);
    assert(Is_Array_Frozen_Shallow(a));  // must be immutable (may be aliased)

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
    const Element* at = Array_At(a, offset);
    const Element* item = at;
    for (; item != tail; ++item) {
        if (item == at and Is_Blank(item))
            continue;  // blank valid at head
        if (item == tail - 1 and Is_Blank(item))
            continue;  // blank valid at tail

        Option(VarList*) error = Trap_Check_Sequence_Element(heart, item);
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

    Init_Series_At_Core(out, REB_BLOCK, a, offset, SPECIFIED);
    HEART_BYTE(out) = heart;
    return nullptr;
}


//
//  pick: native [
//
//  "Perform a path picking operation, same as `:(location).(picker)`"
//
//      return: "Picked value, or null if picker can't fulfill the request"
//          [any-value?]
//      location [<maybe> element?]
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

    UNUSED(ARG(picker));

    // !!! Here we are assuming frame compatibility of PICK with PICK*.
    // This would be more formalized if we were writing this in usermode and
    // made PICK an ENCLOSE of PICK*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    return Run_Generic_Dispatch_Core(ARG(location), level_, Canon(PICK_P));
}


//
//  poke: native [
//
//  "Perform a path poking operation, same as `(location).(picker): :value`"
//
//      return: "Same as poked value"
//          [any-value?]
//      location "(modified)"
//          [<maybe> element?]
//      picker "Index offset, symbol, or other value to use as index"
//          [<maybe> element?]
//      value [any-value?]
//      /immediate "Allow modification even if it will not mutate location"
//  ]
//
DECLARE_NATIVE(poke)
//
// As with PICK, POKE is changed in Ren-C from its own action to "whatever
// tuple-setting (now tuple-poking) would do".
//
// !!! Frame compatibility is assumed here with PICK-POKE*, for efficiency.
{
    INCLUDE_PARAMS_OF_POKE;

    UNUSED(ARG(picker));
    Value* location = ARG(location);
    Value* v = ARG(value);

    Set_Cell_Flag(v, PROTECTED);  // want to return as final result

    // !!! Here we are assuming frame compatibility of POKE with POKE*.
    // This would be more formalized if we were writing this in usermode and
    // made POKE an ENCLOSE of POKE*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    Bounce r = Run_Generic_Dispatch_Core(location, level_, Canon(POKE_P));
    if (r == BOUNCE_THROWN)
        return THROWN;
    assert(r == nullptr or Is_Bounce_An_Atom(r));  // other signals invalid

    // Note: if r is not nullptr here, that means there was a modification
    // which nothing is writing back.  It would be like saying:
    //
    //    >> (12-Dec-2012).year: 1999
    //    == 1999
    //
    // The date was changed, but there was no side effect.  These types of
    // operations are likely accidents and should raise errors.
    //
    if (r != nullptr and not REF(immediate))
        fail ("POKE of immediate won't change value, use /IMMEDIATE if okay");

    return COPY(v);  // return the value we got in
}


//
//  MAKE_Path: C
//
// A MAKE of a PATH! is experimentally being thought of as evaluative.  This
// is in line with the most popular historical interpretation of MAKE, for
// MAKE OBJECT!--which evaluates the object body block.
//
Bounce MAKE_Path(
    Level* level_,
    Kind k,
    Option(const Value*) parent,
    const Value* arg
){
    Heart heart = cast(Heart, k);

    if (parent)
        return RAISE(Error_Bad_Make_Parent(heart, unwrap parent));

    if (not Is_Block(arg))
        fail (Error_Bad_Make(heart, arg)); // "make path! 0" has no meaning

    Level* L = Make_Level_At(&Stepper_Executor, arg, LEVEL_MASK_NONE);

    Push_Level(OUT, L);

    StackIndex base = TOP_INDEX;

    for (; Not_Level_At_End(L); Restart_Stepper_Level(L)) {
        if (Eval_Step_Throws(OUT, L)) {
            Drop_Level(L);
            return BOUNCE_THROWN;
        }

        Decay_If_Unstable(OUT);

        if (Is_Void(OUT))
            continue;

        if (Is_Nulled(OUT))
            return RAISE(Error_Need_Non_Null_Raw());

        if (Is_Antiform(OUT))
            fail (Error_Bad_Antiform(OUT));

        Move_Cell(PUSH(), cast(Element*, OUT));
        L->baseline.stack_base += 1;  // compensate for push
    }

    Option(VarList*) error = Trap_Pop_Sequence(OUT, heart, base);

    Drop_Level_Unbalanced(L); // !!! L's stack_base got captured each loop

    if (error)
        fail (unwrap error);

    return OUT;
}


//
//  TO_Path: C
//
// BLOCK! is the "universal container".  So note the following behavior:
//
//     >> to path! 'a
//     == /a
//
//     >> to path! '(a b c)
//     == /(a b c)  ; does not splice
//
//     >> to path! [a b c]
//     == a/b/c  ; not /[a b c]
//
// There is no "TO/ONLY" to address this as with APPEND.  But there are
// other options:
//
//     >> to path! [_ [a b c]]
//     == /[a b c]
//
//     >> compose /(block)
//     == /[a b c]
//
// TO must return the exact type requested.
//
Bounce TO_Sequence(Level* level_, Kind k, const Value* arg) {
    Heart heart = cast(Heart, k);

    Kind arg_kind = VAL_TYPE(arg);

    if (Is_Text(arg)) {
        //
        // R3-Alpha considered `to tuple! "1.2.3"` to be 1.2.3, consistent with
        // `to path! "a/b/c"` being `a/b/c`...but it allowed `to path! "a b c"`
        // as well.  :-/
        //
        // Essentially, this sounds like "if it's a string, invoke the
        // scanner and then see if the thing you get back can be converted".
        // Try something along those lines for now...use LOAD so that it
        // gets [1.2.3] on "1.2.3" and a [[1 2 3]] on "[1 2 3]" and
        // [1 2 3] on "1 2 3".
        //
        // (Inefficient!  But just see how it feels before optimizing.)
        //
        return rebValue(
            "as", Datatype_From_Kind(heart),
                "parse3 let v: load @", arg, "[",
                    "[&any-sequence? | &any-list?] <end> accept (first v)",
                    "| accept (v)",  // try to convert whatever other block
                "]"
        );
    }

    if (Any_Sequence_Kind(arg_kind)) {  // e.g. `to set-tuple! 'a.b.c`
        assert(heart != arg_kind);  // TO should have called COPY

        // !!! If we don't copy an array, we don't get a new form to use for
        // new bindings in lookups.  Review!
        //
        Copy_Cell(OUT, arg);
        HEART_BYTE(OUT) = heart;
        return OUT;
    }

    if (arg_kind != REB_BLOCK) {
        Copy_Cell(OUT, arg);  // move value so we can modify it
        Dequotify(stable_OUT);  // !!! should TO take Cell*?
        Plainify(cast(Element*, OUT));  // remove any decorations like @ or :

        Option(VarList*) error = Trap_Leading_Blank_Pathify(
            cast(Element*, stable_OUT),
            heart
        );
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }

    // Easiest reuse of the scanner's work that won't create arrays without
    // needing to: push everything to the stack.

    const Element* tail;
    const Element* at = Cell_List_At(&tail, arg);
    for (; at != tail; ++at)
        Copy_Cell(PUSH(), at);

    Option(VarList*) trap = Trap_Pop_Sequence(OUT, heart, STACK_BASE);
    if (trap)
        return RAISE(unwrap trap);

    assert(VAL_TYPE(OUT) == heart);
    return OUT;
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
