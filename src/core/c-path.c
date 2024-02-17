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
//  Try_Init_Any_Sequence_At_Arraylike: C
//
Value* Try_Init_Any_Sequence_At_Arraylike(
    Sink(Value*) out,  // NULL if array too short, violating value otherwise
    Heart heart,
    const Array* a,
    REBLEN index
){
    assert(Any_Sequence_Kind(heart));
    assert(Is_Node_Managed(a));
    Assert_Series_Term_If_Needed(a);
    assert(index == 0);  // !!! current rule
    assert(Is_Array_Frozen_Shallow(a));  // must be immutable (may be aliased)

    assert(index < Array_Len(a));
    REBLEN len_at = Array_Len(a) - index;

    if (len_at < 2) {
        Init_Nulled(out);  // signal that array is too short
        return nullptr;
    }

    if (len_at == 2) {
        if (a == PG_2_Blanks_Array)  // can get passed back in
            return Init_Any_Sequence_1(out, heart);

        // !!! Note: at time of writing, this may just fall back and make
        // a 2-element array vs. a pair optimization.
        //
        if (Try_Init_Any_Sequence_Pairlike(
            out,
            heart,
            Array_At(a, index),
            Array_At(a, index + 1)
        )){
            return out;
        }

        return nullptr;
    }

    if (Try_Init_Any_Sequence_All_Integers(
        out,
        heart,
        Array_At(a, index),
        len_at
    )){
        return out;
    }

    const Element* tail = Array_Tail(a);
    const Element* v = Array_Head(a);
    for (; v != tail; ++v) {
        if (not Is_Valid_Sequence_Element(heart, v)) {
            Copy_Cell(out, v);
            return nullptr;
        }
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

    Init_Series_Cell_At_Core(out, REB_BLOCK, a, index, SPECIFIED);
    HEART_BYTE(out) = heart;

    return out;
}


//
//  pick: native [
//
//  "Perform a path picking operation, same as `:(location).(picker)`"
//
//      return: "Picked value, or null if picker can't fulfill the request"
//          [any-value?]
//      location [element?]
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
//          [element?]
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
    REBVAL *location = ARG(location);
    REBVAL *v = ARG(value);

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
    const REBVAL *arg
){
    Heart heart = cast(Heart, k);

    if (parent)
        return RAISE(Error_Bad_Make_Parent(heart, unwrap(parent)));

    if (not Is_Block(arg))
        fail (Error_Bad_Make(heart, arg)); // "make path! 0" has no meaning

    Level* L = Make_Level_At(arg, LEVEL_MASK_NONE);

    Push_Level(OUT, L);

    StackIndex base = TOP_INDEX;

    for (; Not_Level_At_End(L); Restart_Evaluator_Level(L)) {
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

    REBVAL *p = Try_Pop_Sequence_Or_Element_Or_Nulled(OUT, heart, base);

    Drop_Level_Unbalanced(L); // !!! L's stack_base got captured each loop

    if (not p)
        fail (Error_Bad_Sequence_Init(stable_OUT));

    if (not Any_Path(OUT))  // e.g. `make path! ['x]` giving us the WORD! `x`
        return RAISE(Error_Sequence_Too_Short_Raw());

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
// TO must return the exact type requested, so this wouldn't be legal:
//
//     >> to path! 'a:
//     == /a:  ; !!! a SET-PATH!, which is not the promised PATH! return type
//
// So the only choice is to discard the decorators, or error.  Discarding is
// consistent with ANY-WORD? interconversion, and also allows another avenue
// for putting blocks as-is in paths by using the decorated type:
//
//     >> to path! ^[a b c]
//     == /[a b c]
//
Bounce TO_Sequence(Level* level_, Kind k, const REBVAL *arg) {
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
                    "[&any-sequence? | &any-array?] <end> accept (first v)",
                    "| accept (v)",  // try to convert whatever other block
                "]"
        );
    }

    if (Any_Sequence_Kind(arg_kind)) {  // e.g. `to set-path! 'a/b/c`
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
        Plainify(stable_OUT);  // remove any decorations like @ or :
        if (not Try_Leading_Blank_Pathify(stable_OUT, heart))
            return RAISE(Error_Bad_Sequence_Init(stable_OUT));
        return OUT;
    }

    // BLOCK! is universal container, and the only type that is converted.
    // Paths are not allowed... use MAKE PATH! for that.  Not all paths
    // will be valid here, so the initializatoinmay fail

    REBLEN len = Cell_Series_Len_At(arg);
    if (len < 2)
        return RAISE(Error_Sequence_Too_Short_Raw());

    if (len == 2) {
        const Element* at = Cell_Array_Item_At(arg);
        if (not Try_Init_Any_Sequence_Pairlike(
            OUT,
            heart,
            at,
            at + 1
        )){
            return RAISE(Error_Bad_Sequence_Init(stable_OUT));
        }
    }
    else {
        // Assume it needs an array.  This might be a wrong assumption, e.g.
        // if it knows other compressions (if there's no index, it could have
        // "head blank" and "tail blank" bits, for instance).

        Array* a = Copy_Array_At_Shallow(
            Cell_Array(arg),
            VAL_INDEX(arg)
        );
        Freeze_Array_Shallow(a);
        Force_Series_Managed(a);

        if (not Try_Init_Any_Sequence_Arraylike(OUT, heart, a))
            return RAISE(Error_Bad_Sequence_Init(stable_OUT));
    }

    if (VAL_TYPE(OUT) != heart) {
        assert(VAL_TYPE(OUT) == REB_WORD);
        return RAISE(Error_Bad_Sequence_Init(stable_OUT));
    }

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

    DECLARE_LOCAL (temp_a);
    DECLARE_LOCAL (temp_b);

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
