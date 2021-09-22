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
//  PD_Fail: C
//
// In order to avoid having to pay for a check for NULL in the path dispatch
// table for types with no path dispatch, a failing handler is in the slot.
//
REB_R PD_Fail(
    REBPVS *pvs,
    const RELVAL *picker
){
    UNUSED(picker);
    fail (pvs->out);
}


//
//  PD_Unhooked: C
//
// As a temporary workaround for not having real user-defined types, an
// extension can overtake an "unhooked" type slot to provide behavior.
//
REB_R PD_Unhooked(
    REBPVS *pvs,
    const RELVAL *picker
){
    UNUSED(picker);

    const REBVAL *type = Datatype_From_Kind(VAL_TYPE(pvs->out));
    UNUSED(type); // !!! put in error message?

    fail ("Datatype is provided by an extension which is not loaded.");
}


//
//  Next_Path_Throws: C
//
// Evaluate next part of a path.
//
// !!! This is done as a recursive function instead of iterating in a loop due
// to the unusual nature of some path dispatches that call Next_Path_Throws()
// inside their implementation.  Those two cases (FFI array writeback and
// writing GOB x and y coordinates) are intended to be revisited after this
// code gets more reorganized.
//
bool Next_Path_Throws(REBPVS *pvs)
{
    REBFRM * const f = pvs;  // to use the f_xxx macros

    if (IS_NULLED(pvs->out))
        fail (Error_No_Value(f_value));

    bool actions_illegal = false;

    if (IS_BLANK(f_value)) {
        //
        // !!! Literal BLANK!s in sequences are for internal "doubling up"
        // of delimiters, like `a..b`, or they can be used for prefixes like
        // `/foo` or suffixes like `bar/` -- the meaning of blanks at prefixes
        // is to cause the sequence to behave inertly.  But terminal blanks
        // were conceived as ensuring things are either actions or not.
        //
        // At the moment this point in the code doesn't know if we're dealing
        // with a PATH! or a TUPLE!, but assume we're dealing with slashes and
        // raise an error if the thing on the left of a slash is not a
        // function when we are at the end.
        //
        Fetch_Next_Forget_Lookback(pvs);  // may be at end

        if (NOT_END(f_value))
           fail ("Literal BLANK!s not executable internal to sequences ATM");

        if (not IS_ACTION(pvs->out))
            fail (Error_Inert_With_Slashed_Raw());

        PVS_PICKER(pvs) = Lib(NULL);  // no-op
        goto redo;
    }
    else if (ANY_TUPLE(f_value)) {
        //
        // !!! Tuples in PATH!s will require some thinking...especially since
        // it's not necessarily going to be useful to reflect the hierarchy
        // of tuples-in-paths for picking.  However, the special case of
        // a terminal tuple enforcing a non-action is very useful.  This
        // tweak implements *just that*.
        //
        DECLARE_LOCAL (temp);
        if (
            VAL_SEQUENCE_LEN(f_value) != 2 or
            not IS_BLANK(VAL_SEQUENCE_AT(temp, f_value, 1))
        ){
            fail ("TUPLE! support in PATH! processing limited to `a.` forms");
        }
        Derelativize(
            f_spare,
            VAL_SEQUENCE_AT(temp, f_value, 0),
            VAL_SEQUENCE_SPECIFIER(f_value)
        );
        PVS_PICKER(pvs) = f_spare;
        actions_illegal = true;
    }
    else if (IS_GET_WORD(f_value)) {  // e.g. object/:field
        PVS_PICKER(pvs) = Get_Word_May_Fail(f_spare, f_value, f_specifier);
    }
    else if (
        IS_GROUP(f_value)  // object/(expr) case:
        and NOT_EVAL_FLAG(pvs, PATH_HARD_QUOTE)  // not precomposed
    ){
        if (GET_EVAL_FLAG(pvs, NO_PATH_GROUPS))
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        REBSPC *derived = Derive_Specifier(f_specifier, f_value);
        if (Do_Any_Array_At_Throws(f_spare, f_value, derived)) {
            Move_Cell(pvs->out, f_spare);
            return true; // thrown
        }
        Decay_If_Isotope(f_spare);
        PVS_PICKER(pvs) = f_spare;
    }
    else { // object/word and object/value case:
        PVS_PICKER(pvs) = f_value;  // relative value--cannot look up
    }

    Fetch_Next_Forget_Lookback(pvs);  // may be at end

  redo:;

    PATH_HOOK *hook = Path_Hook_For_Type_Of(pvs->out);

    const REBVAL *r = hook(pvs, PVS_PICKER(pvs));

    if (r == pvs->out) {
        // Common case... result where we expect it
    }
    else if (not r) {
        Init_Nulled(pvs->out);
    }
    else if (not IS_RETURN_SIGNAL(r)) {
        assert(GET_CELL_FLAG(r, ROOT));  // API, from Alloc_Value()
        Handle_Api_Dispatcher_Result(pvs, r);
    }
    else switch (VAL_RETURN_SIGNAL(r)) {
      case C_UNHANDLED: {
        if (IS_NULLED(PVS_PICKER(pvs)))
            fail ("NULL used in path picking but was not handled");
        DECLARE_LOCAL (specific);
        Derelativize(specific, PVS_PICKER(pvs), f_specifier);
        fail (Error_Bad_Pick_Raw(specific)); }

      case C_THROWN:
        panic ("Path dispatch isn't allowed to throw, only GROUP!s");

      default:
        panic ("REB_R value not supported for path dispatch");
    }

    // A function being refined does not actually update pvs->out with
    // a "more refined" function value, it holds the original function and
    // accumulates refinement state on the stack.  The label should only
    // be captured the first time the function is seen, otherwise it would
    // capture the last refinement's name, so check label for non-NULL.
    //
    if (IS_ACTION(pvs->out)) {
        if (actions_illegal)
            fail (Error_Action_With_Dotted_Raw());

        if (IS_WORD(PVS_PICKER(pvs))) {
            if (not pvs->label) {  // !!! only used for this "bit" signal
                pvs->label = VAL_WORD_SYMBOL(PVS_PICKER(pvs));
                INIT_VAL_ACTION_LABEL(pvs->out, unwrap(pvs->label));
            }
        }
    }

    if (IS_END(f_value))
        return false; // did not throw

    return Next_Path_Throws(pvs);
}


//
//  Eval_Path_Throws_Core: C
//
// Evaluate an ANY_PATH! REBVAL, starting from the index position of that
// path value and continuing to the end.
//
// The evaluator may throw because GROUP! is evaluated, e.g. `foo/(throw 1020)`
//
// If label_sym is passed in as being non-null, then the caller is implying
// readiness to process a path which may be a function with refinements.
// These refinements will be left in order on the data stack in the case
// that `out` comes back as IS_ACTION().  If it is NULL then a new ACTION!
// will be allocated, in the style of the REFINE native, which will have the
// behavior of refinement partial specialization.
//
// If `setval` is given, the path operation will be done as a "SET-PATH!"
// if the path evaluation did not throw or error.  HOWEVER the set value
// is NOT put into `out`.  This provides more flexibility on performance in
// the evaluator, which may already have the `val` where it wants it, and
// so the extra assignment would just be overhead.
//
// !!! Path evaluation is one of the parts of R3-Alpha that has not been
// vetted very heavily by Ren-C, and needs a review and overhaul.
//
bool Eval_Path_Throws_Core(
    REBVAL *out, // if setval, this is only used to return a thrown value
    const RELVAL *sequence,
    REBSPC *sequence_specifier,
    REBFLGS flags
){
    REBLEN index = 0;

    enum Reb_Kind heart = CELL_HEART(cast(REBCEL(const*), sequence));

    // The evaluator has the behavior that inert-headed paths will just
    // give themselves back.  But this code path is for GET, where getting
    // something like `/a` will actually look up the word.

    switch (heart) {
      case REB_ISSUE:
        fail ("Cannot GET or SET a numeric-headed ANY-SEQUENCE!");

      case REB_WORD:  // get or set `'/` or `'.`
        assert(
            VAL_WORD_SYMBOL(sequence) == PG_Slash_1_Canon
            or VAL_WORD_SYMBOL(sequence) == PG_Dot_1_Canon
        );
        goto handle_word;

      case REB_GET_WORD:  // get or set `/foo` or `.foo`
        //
        // The idea behind terminal dots and slashes is to distinguish "never
        // a function" vs. "always a function".  These sequence forms fit
        // entirely inside a cell, so they make this a relatively cheap way
        // to make asserts which can help toughen library code.
        //
        goto handle_word;

      case REB_META_WORD:  // get or set `foo/` or `foo.`
      handle_word: {
        Get_Word_May_Fail(out, sequence, sequence_specifier);

        if (heart == REB_META_WORD) {
            if (ANY_TUPLE_KIND(VAL_TYPE(sequence))) {
                if (IS_ACTION(out))
                    fail (Error_Action_With_Dotted_Raw());
            }
            else {
                if (not IS_ACTION(out))
                    fail (Error_Inert_With_Slashed_Raw());
            }
        }
        return false; }

      case REB_BLOCK:
        break;

      default:
        panic (nullptr);
    }

    // We extract the array.  Note that if the input value was a REBVAL* it
    // may have been "specific" because it was coupled with a specifier that
    // was passed in, but to get the specifier of the embedded array we have
    // to use Derive_Specifier().
    //
    const REBARR *array = VAL_ARRAY(sequence);
    REBSPC *specifier = Derive_Specifier(sequence_specifier, sequence);

    while (KIND3Q_BYTE(ARR_AT(array, index)) == REB_BLANK)
        ++index; // pre-feed any blanks

    assert(NOT_END(ARR_AT(array, index)));

    DECLARE_ARRAY_FEED (feed, array, index, specifier);
    DECLARE_FRAME (
        pvs,
        feed,
        flags | EVAL_FLAG_PATH_MODE | EVAL_FLAG_ALLOCATED_FEED
    );
    REBFRM * const f = pvs;  // to use the f_xxx macros

    assert(NOT_END(f_value));  // tested 0-length path previously

    Push_Frame(out, pvs);

    REBDSP dsp_orig = DSP;

    assert(out != FRM_SPARE(pvs));

    pvs->label = nullptr;

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (IS_TUPLE(f_value)) {
        //
        // !!! As commented upon multiple times in this work-in-progress,
        // the meaning of a TUPLE! in a PATH! needs work as it's a "new thing"
        // but a few limited forms are supported for now.  In this case,
        // we allow a leading TUPLE! in a PATH! of the form `.a` to act like
        // `a` when requested via GET or SET (the whole path would be inert
        // in the evaluator with such a tuple in the first position)
        //
        DECLARE_LOCAL (temp);
        if (
            VAL_SEQUENCE_LEN(f_value) != 2
            or not IS_BLANK(VAL_SEQUENCE_AT(temp, f_value, 0))
        ){
            fail ("Head TUPLE! support in PATH! limited to `.a` at moment");
        }
        const RELVAL *second = VAL_SEQUENCE_AT(temp, f_value, 1);
        if (not IS_WORD(second))
            fail ("Head TUPLE support in PATH! limited to `.a` at moment");

        Copy_Cell(pvs->out, Lookup_Mutable_Word_May_Fail(
            second,
            VAL_SEQUENCE_SPECIFIER(f_value)
        ));
        if (IS_ACTION(pvs->out))
            pvs->label = VAL_WORD_SYMBOL(second);
    }
    else if (IS_WORD(f_value)) {
        Copy_Cell(pvs->out, Lookup_Word_May_Fail(f_value, specifier));

        if (IS_ACTION(pvs->out)) {
            pvs->label = VAL_WORD_SYMBOL(f_value);
            INIT_VAL_ACTION_LABEL(pvs->out, unwrap(pvs->label));
        }
    }
    else if (
        IS_GROUP(f_value)
        and NOT_EVAL_FLAG(pvs, PATH_HARD_QUOTE)  // not precomposed
    ){
        if (GET_EVAL_FLAG(pvs, NO_PATH_GROUPS))
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        REBSPC *derived = Derive_Specifier(specifier, f_value);
        if (Do_Any_Array_At_Throws(pvs->out, f_value, derived))
            goto return_thrown;

        Decay_If_Isotope(pvs->out);
    }
    else
        Derelativize(pvs->out, f_value, specifier);

    const RELVAL *lookback;
    lookback = Lookback_While_Fetching_Next(pvs);

    if (NOT_END(f_value)) {
        if (IS_NULLED(pvs->out))
            fail (Error_No_Value(lookback));

        if (Next_Path_Throws(pvs))
            goto return_thrown;

        assert(IS_END(f_value));
    }

    TRASH_POINTER_IF_DEBUG(lookback);  // goto crosses it, don't use below

    if (dsp_orig != DSP) {
        //
        // To make things easier for processing, reverse any refinements
        // pushed as ISSUE!s (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if not back to `dsp_orig` by the end.
        //
      blockscope {
        STKVAL(*) bottom = DS_AT(dsp_orig + 1);
        STKVAL(*) top = DS_TOP;

        while (top > bottom) {
            assert(IS_WORD(bottom) and not IS_WORD_BOUND(bottom));
            assert(IS_WORD(top) and not IS_WORD_BOUND(top));

            Move_Cell(f_spare, bottom);
            Move_Cell(bottom, top);
            Move_Cell(top, f_spare);

            top--;
            bottom++;
        }
      }

        assert(IS_ACTION(pvs->out));

        if (GET_EVAL_FLAG(pvs, PUSH_PATH_REFINES)) {
            //
            // The caller knows how to handle the refinements-pushed-to-stack
            // in-reverse-order protocol, and doesn't want to pay for making
            // a new ACTION!.
        }
        else {
            // The caller actually wants an ACTION! value to store or use
            // for later, as opposed to just calling it once.  It costs a
            // bit to do this, but unlike in R3-Alpha, it's possible to do!
            //
            // Code for specialization via refinement order works from the
            // data stack.  (It can't use direct value pointers because it
            // pushes to the stack itself, hence may move it on expansion.)
            //
            if (Specialize_Action_Throws(
                FRM_SPARE(pvs),
                pvs->out,
                nullptr, // optional def
                dsp_orig // first_refine_dsp
            )){
                panic ("REFINE-only specializations should not THROW");
            }

            Copy_Cell(pvs->out, FRM_SPARE(pvs));
        }
    }

    Abort_Frame(pvs);
    assert(not Is_Evaluator_Throwing_Debug());

    return false; // not thrown

  return_thrown:;
    Abort_Frame(pvs);
    assert(Is_Evaluator_Throwing_Debug());
    return true; // thrown
}


//
//  Resolve_Path: C
//
// Given a path, determine if it is ultimately specifying a selection out
// of a context...and if it is, return that context.  So `a/obj/key` would
// return the object assocated with obj, while `a/str/1` would return
// NULL if `str` were a string as it's not an object selection.
//
// !!! This routine overlaps the logic of Eval_Path, and should potentially
// be a mode of that instead.  It is not very complete, considering that it
// does not execute GROUP! (and perhaps shouldn't?) and only supports a
// path that picks contexts out of other contexts, via word selection.
//
REBCTX *Resolve_Path(const REBVAL *path, REBLEN *index_out)
{
    REBLEN len = VAL_SEQUENCE_LEN(path);
    if (len == 0)  // !!! e.g. `/`, what should this do?
        return nullptr;
    if (len == 1)  // !!! "does not handle single element paths"
        return nullptr;

    DECLARE_LOCAL (temp);

    REBLEN index = 0;
    const RELVAL *picker = VAL_SEQUENCE_AT(temp, path, index);

    if (not ANY_WORD(picker))
        return nullptr;  // !!! only handles heads of paths that are ANY-WORD!

    const RELVAL *var = Lookup_Word_May_Fail(picker, VAL_SPECIFIER(path));

    ++index;
    picker = VAL_SEQUENCE_AT(temp, path, index);

    while (ANY_CONTEXT(var) and IS_WORD(picker)) {
        const bool strict = false;
        REBLEN i = Find_Symbol_In_Context(
            var,
            VAL_WORD_SYMBOL(picker),
            strict
        );
        ++index;
        if (index == len) {
            *index_out = i;
            return VAL_CONTEXT(var);
        }

        var = CTX_VAR(VAL_CONTEXT(var), i);
    }

    return nullptr;
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

    REBVAL *location = ARG(location);
    REBVAL *picker = ARG(picker);

    // !!! We pay the cost for a block here, because the interface of PICK
    // is geared around PATH! and moving in steps.  Review.
    //
    REBARR *a = Alloc_Singular(NODE_FLAG_MANAGED);
    Move_Cell(ARR_SINGLE(a), picker);
    Init_Block(picker, a);

    // !!! Here we are assuming frame compatibility of PICK with PICK*.
    // This would be more formalized if we were writing this in usermode and
    // made PICK an ENCLOSE of PICK*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    return Run_Generic_Dispatch(location, frame_, Canon(PICK_P));
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
//      picker
//          {Index offset, symbol, or other value to use as index}
//      ^value [<opt> any-value!]
//          {The new value}
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

    REBVAL *picker = ARG(picker);
    REBVAL *location = ARG(location);

    REBARR *a = Alloc_Singular(NODE_FLAG_MANAGED);
    Move_Cell(ARR_SINGLE(a), picker);
    Init_Block(picker, a);

    // !!! Here we are assuming frame compatibility of POKE with POKE*.
    // This would be more formalized if we were writing this in usermode and
    // made POKE an ENCLOSE of POKE*.  But to get a fast native, we don't have
    // enclose...so this is an approximation.  Review ensuring this is "safe".
    //
    REB_R r = Run_Generic_Dispatch(location, frame_, Canon(POKE_P));
    if (r == R_THROWN)
        return R_THROWN;

    // Note: if r is not nullptr here, that means there was a modification
    // which nothing is writing back.  It would be like saying:
    //
    //    >> (12-Dec-2012).year: 1999
    //    == 1999
    //
    // The date was changed, but there was no side effect.  These types of
    // operations are likely accidents and should raise errors.
    //
    // !!! Consider offering a refinement to allow this, but returns the
    // updated value, e.g. would return 12-Dec-1999
    //
    if (r != nullptr)
        fail ("Updating immediate value in POKE, results would be discarded");

    RETURN (ARG(value));  // return the value we got in
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
        if (Eval_Step_Throws(out, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(out))
            break;
        if (IS_NULLED(out))
            fail (out);  // !!! BLANK! is legit in paths, should null opt out?

        Move_Cell(DS_PUSH(), out);
    }

    REBVAL *p = Try_Pop_Sequence_Or_Element_Or_Nulled(out, kind, dsp_orig);

    Drop_Frame_Unbalanced(f); // !!! f->dsp_orig got captured each loop

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
                "parse let v: load @", arg, "[",
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
    // will be valid here, so the Init_Any_Path_Arraylike may fail (should
    // probably be Try_Init_Any_Path_Arraylike()...)

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
