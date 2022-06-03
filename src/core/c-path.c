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
//  Try_Init_Any_Sequence_At_Arraylike_Core: C
//
REBVAL *Try_Init_Any_Sequence_At_Arraylike_Core(
    RELVAL *out,  // NULL if array is too short, violating value otherwise
    enum Reb_Kind kind,
    const REBARR *a,
    REBSPC *specifier,
    REBLEN index
){
    assert(ANY_SEQUENCE_KIND(kind));
    assert(GET_SERIES_FLAG(a, MANAGED));
    ASSERT_SERIES_TERM_IF_NEEDED(a);
    assert(index == 0);  // !!! current rule
    assert(Is_Array_Frozen_Shallow(a));  // must be immutable (may be aliased)

    assert(index < ARR_LEN(a));
    REBLEN len_at = ARR_LEN(a) - index;

    if (len_at < 2) {
        Init_Nulled(out);  // signal that array is too short
        return nullptr;
    }

    if (len_at == 2) {
        if (a == PG_2_Blanks_Array) {  // can get passed back in
            assert(specifier == SPECIFIED);
            return Init_Any_Sequence_1(out, kind);
        }

        // !!! Note: at time of writing, this may just fall back and make
        // a 2-element array vs. a pair optimization.
        //
        if (Try_Init_Any_Sequence_Pairlike_Core(
            out,
            kind,
            ARR_AT(a, index),
            ARR_AT(a, index + 1),
            specifier
        )){
            return cast(REBVAL*, out);
        }

        return nullptr;
    }

    if (Try_Init_Any_Sequence_All_Integers(
        out,
        kind,
        ARR_AT(a, index),
        len_at
    )){
        return cast(REBVAL*, out);
    }

    const RELVAL *tail = ARR_TAIL(a);
    const RELVAL *v = ARR_HEAD(a);
    for (; v != tail; ++v) {
        if (not Is_Valid_Sequence_Element(kind, v)) {
            Derelativize(out, v, specifier);
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

    Init_Any_Series_At_Core(out, REB_BLOCK, a, index, specifier);
    mutable_KIND3Q_BYTE(out) = kind;
    assert(HEART_BYTE(out) == REB_BLOCK);

    return cast(REBVAL*, out);
}


//
//  pick: native [
//
//  {Perform a path picking operation, same as `:(:location)/(:picker)`}
//
//      return: [<opt> any-value!]
//          {Picked value, or null if picker can't fulfill the request}
//      location [any-value!]
//      picker [any-value!]
//          {Index offset, symbol, or other value to use as index}
//  ]
//
REBNATIVE(pick)
//
// In R3-Alpha, PICK was an "action", which dispatched on types through the
// "action mechanic" for the following types:
//
//     [any-series! map! gob! pair! date! time! tuple! bitset! port! varargs!]
//
// In Ren-C, PICK is rethought to use the same dispatch mechanic as paths,
// to cut down on the total number of operations the system has to define.
{
    INCLUDE_PARAMS_OF_PICK;

    UNUSED(ARG(picker));

    // !!! Here we are assuming frame compatibility of PICK with PICK*.
    // This would be more formalized if we were writing this in usermode and
    // made PICK an ENCLOSE of PICK*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    return Run_Generic_Dispatch_Core(ARG(location), frame_, Canon(PICK_P));
}


//
//  poke: native [
//
//  {Perform a path poking operation, same as `(:location)/(:picker): :value`}
//
//      return: [<opt> any-value!]
//          {Same as value}
//      location [any-value!]
//          {(modified)}
//      picker [any-value!]
//          {Index offset, symbol, or other value to use as index}
//      ^value [<opt> any-value!]
//          {The new value}
//      /immediate "Allow modification even if it will not mutate location"
//  ]
//
REBNATIVE(poke)
//
// As with PICK, POKE is changed in Ren-C from its own action to "whatever
// path-setting (now path-poking) would do".
//
// !!! Frame compatibility is assumed here with PICK-POKE*, for efficiency.
{
    INCLUDE_PARAMS_OF_POKE;

    UNUSED(ARG(picker));
    REBVAL *location = ARG(location);

    // !!! Here we are assuming frame compatibility of POKE with POKE*.
    // This would be more formalized if we were writing this in usermode and
    // made POKE an ENCLOSE of POKE*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    REB_R r = Run_Generic_Dispatch_Core(location, frame_, Canon(POKE_P));
    if (r == R_THROWN)
        return_thrown (OUT);
    assert(r == nullptr or not IS_RETURN_SIGNAL(r));  // other signals invalid

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

    return ARG(value);  // return the value we got in
}


//
//  MAKE_Path: C
//
// A MAKE of a PATH! is experimentally being thought of as evaluative.  This
// is in line with the most popular historical interpretation of MAKE, for
// MAKE OBJECT!--which evaluates the object body block.
//
REB_R MAKE_Path(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (not IS_BLOCK(arg))
        fail (Error_Bad_Make(kind, arg)); // "make path! 0" has no meaning

    DECLARE_FRAME_AT (f, arg, EVAL_MASK_DEFAULT);

    Push_Frame(nullptr, f);

    REBDSP dsp_orig = DSP;

    while (NOT_END(f->feed->value)) {
        if (Eval_Step_Maybe_Stale_Throws(out, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (Is_Stale(out))
            continue;

        if (IS_NULLED(out))
            fail (out);  // !!! BLANK! is legit in paths, should null opt out?

        Move_Cell(DS_PUSH(), out);
    }

    REBVAL *p = Try_Pop_Sequence_Or_Element_Or_Nulled(out, kind, dsp_orig);

    Drop_Frame_Unbalanced(f); // !!! f->baseline.dsp got captured each loop

    if (not p)
        fail (Error_Bad_Sequence_Init(out));

    if (not ANY_PATH(out))  // e.g. `make path! ['x]` giving us the WORD! `x`
        fail (Error_Sequence_Too_Short_Raw());

    return out;
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
// consistent with ANY-WORD! interconversion, and also allows another avenue
// for putting blocks as-is in paths by using the decorated type:
//
//     >> to path! ^[a b c]
//     == /[a b c]
//
REB_R TO_Sequence(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    enum Reb_Kind arg_kind = VAL_TYPE(arg);

    if (IS_TEXT(arg)) {
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
            "as", Datatype_From_Kind(kind), "catch [",
                "parse3 let v: load @", arg, "[",
                    "[any-sequence! | any-array!] end (throw first v)",
                    "| (throw v)",  // try to convert whatever other block
                "]",
            "]"
        );
    }

    if (ANY_SEQUENCE_KIND(arg_kind)) {  // e.g. `to set-path! 'a/b/c`
        assert(kind != arg_kind);  // TO should have called COPY

        // !!! If we don't copy an array, we don't get a new form to use for
        // new bindings in lookups.  Review!
        //
        Copy_Cell(out, arg);
        mutable_KIND3Q_BYTE(out) = kind;
        return out;
    }

    if (arg_kind != REB_BLOCK) {
        Copy_Cell(out, arg);  // move value so we can modify it
        Dequotify(out);  // remove quotes (should TO take a REBCEL()?)
        Plainify(out);  // remove any decorations like @ or :
        if (not Try_Leading_Blank_Pathify(out, kind))
            fail (Error_Bad_Sequence_Init(out));
        return out;
    }

    // BLOCK! is universal container, and the only type that is converted.
    // Paths are not allowed... use MAKE PATH! for that.  Not all paths
    // will be valid here, so the initializatoinmay fail

    REBLEN len = VAL_LEN_AT(arg);
    if (len < 2)
        fail (Error_Sequence_Too_Short_Raw());

    if (len == 2) {
        const RELVAL *at = VAL_ARRAY_ITEM_AT(arg);
        if (not Try_Init_Any_Sequence_Pairlike_Core(
            out,
            kind,
            at,
            at + 1,
            VAL_SPECIFIER(arg)
        )){
            fail (Error_Bad_Sequence_Init(out));
        }
    }
    else {
        // Assume it needs an array.  This might be a wrong assumption, e.g.
        // if it knows other compressions (if there's no index, it could have
        // "head blank" and "tail blank" bits, for instance).

        REBARR *a = Copy_Array_At_Shallow(
            VAL_ARRAY(arg),
            VAL_INDEX(arg),
            VAL_SPECIFIER(arg)
        );
        Freeze_Array_Shallow(a);
        Force_Series_Managed(a);

        if (not Try_Init_Any_Sequence_Arraylike(out, kind, a))
            fail (Error_Bad_Sequence_Init(out));
    }

    if (VAL_TYPE(out) != kind) {
        assert(VAL_TYPE(out) == REB_WORD);
        fail (Error_Bad_Sequence_Init(out));
    }

    return out;
}


//
//  CT_Sequence: C
//
// "Compare Type" dispatcher for ANY-PATH! and ANY-TUPLE!.
//
// Note: R3-Alpha considered TUPLE! with any number of trailing zeros to
// be equivalent.  This meant `255.255.255.0` was equal to `255.255.255`.
// Why this was considered useful is not clear...as that would make a
// fully transparent alpha channel pixel equal to a fully opaque color.
// This behavior is not preserved in Ren-C, so `same-color?` or something
// else would be needed to get that intent.
//
REBINT CT_Sequence(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    // If the internal representations used do not match, then the sequences
    // can't match.  For this to work reliably, there can't be aliased
    // internal representations like [1 2] the array and #{0102} the bytes.
    // See the Try_Init_Sequence() pecking order for how this is guaranteed.
    //
    int heart_diff = cast(int, CELL_HEART(a)) - CELL_HEART(b);
    if (heart_diff != 0)
        return heart_diff > 0 ? 1 : -1;

    switch (CELL_HEART(a)) {  // now known to be same as CELL_HEART(b)
      case REB_BYTES: {  // packed bytes
        REBLEN a_len = VAL_SEQUENCE_LEN(a);
        int diff = cast(int, a_len) - VAL_SEQUENCE_LEN(b);
        if (diff != 0)
            return diff > 0 ? 1 : -1;

        int cmp = memcmp(
            &PAYLOAD(Bytes, a).at_least_8,
            &PAYLOAD(Bytes, b).at_least_8,
            a_len  // same as b_len at this point
        );
        if (cmp == 0)
            return 0;
        return cmp > 0 ? 1 : -1; }

      case REB_WORD:  // `/` or `.`
      case REB_GET_WORD:  // `/foo` or `.foo`
      case REB_META_WORD:  // `foo/ or `foo.`
        return CT_Word(a, b, strict);

      case REB_GROUP:
      case REB_GET_GROUP:
      case REB_META_GROUP:
      case REB_BLOCK:
      case REB_GET_BLOCK:
      case REB_META_BLOCK:
        return CT_Array(a, b, strict);

      default:
        panic (nullptr);
    }
}
