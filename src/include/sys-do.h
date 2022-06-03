//
//  File: %sys-do.h
//  Summary: {DO-until-end (of block or variadic feed) evaluation API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// The "DO" helpers have names like Do_XXX(), and are a convenience layer
// over making repeated calls into the Eval_XXX() routines.  DO-ing things
// always implies running to the end of an input.  It also implies returning
// a BAD-WORD! value if nothing can be synthesized, but letting the last null
// or value fall out otherwise:
//
//     >> type of ^ do []
//     == bad-word!
//
//     >> type of ^ do [comment "hi"]
//     == bad-word!
//
//     >> do [1 comment "hi"]
//     == 1
//
//    >> do [null comment "hi"]
//    ; null
//
// See %sys-eval.h for the lower level routines if this isn't enough control.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Unlike single stepping, the stale flag from Do_XXX_Maybe_Stale() isn't
//   generally all that useful.  That's because heeding the stale flag after
//   multiple steps usually doesn't make any real sense.  If someone writes:
//
//        (1 + 2 if true [x] else [y] comment "hello")
//
//   ...what kind of actionability is there on the fact that the last step
//   vanished, if that's the only think you know?  For this reason, you'll
//   get an assert if you preload a frame with any values unless you use
//   the EVAL_FLAG_OVERLAP_OUTPUT option on the frame.
//


// This helper routine is able to take an arbitrary input cell to start with
// that may not be END.  It is code that DO shares with GROUP! evaluation
// in Eval_Core()--where being able to know if a group "completely vaporized"
// is important as distinct from an expression evaluating to void.
//
inline static bool Do_Feed_To_End_Maybe_Stale_Throws(
    REBVAL *out,  // must be initialized, unchanged if all empty/invisible
    REBFED *feed,  // feed mechanics always call va_end() if va_list
    REBFLGS flags
){
    // You can feed in something other than END here (and GROUP! handling in
    // the evaluator does do that).  But if you give it something stale then
    // that suggests you might be thinking you can infer some information
    // about the staleness after the run.  See comments at top of file for
    // why that's not the case--this assert helps avoid misunderstandings.
    //
    if (not (flags & EVAL_FLAG_OVERLAP_OUTPUT))
        assert(Is_Fresh(out));

    DECLARE_FRAME (f, feed, flags);

    bool threw;
    Push_Frame(out, f);
    do {
        threw = Eval_Maybe_Stale_Throws(f);
    } while (not threw and NOT_END(feed->value));
    Drop_Frame(f);

    return threw;
}

inline static bool Do_Any_Array_At_Maybe_Stale_Throws(
    REBVAL *out,
    const RELVAL *any_array,
    REBSPC *specifier
){
    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    bool threw = Do_Feed_To_End_Maybe_Stale_Throws(
        out,
        feed,
        EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
    );

    return threw;
}

inline static bool Do_Any_Array_At_Throws(
    REBVAL *out,
    const RELVAL *any_array,  // same as `out` is allowed
    REBSPC *specifier
){
    assert(Is_Fresh(out));  // better if caller's RESET() does the TRACK() cell

    bool threw = Do_Any_Array_At_Maybe_Stale_Throws(out, any_array, specifier);
    Clear_Stale_Flag(out);
    return threw;
}


// !!! When working with an array outside of the context of a REBVAL it was
// extracted from, then that means automatic determination of the CONST rules
// isn't possible.  This primitive is currently used in a few places where
// the desire is not to inherit any "wave of constness" from the parent's
// frame, or from a value.  The cases need review--in particular the use for
// the kind of shady frame translations used by HIJACK and ports.
//
inline static bool Do_At_Mutable_Maybe_Stale_Throws(
    REBVAL *out,
    option(const RELVAL*) first,  // element to inject *before* the array
    REBARR *array,
    REBLEN index,
    REBSPC *specifier  // must match array, but also first if relative
){
    // need to pass `first` parameter, so can't use DECLARE_ARRAY_FEED
    REBFED *feed = Alloc_Feed();  // need `first`
    Prep_Array_Feed(
        feed,
        first,
        array,
        index,
        specifier,
        FEED_MASK_DEFAULT  // different: does not
    );

    return Do_Feed_To_End_Maybe_Stale_Throws(
        out,
        feed,
        EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
            | EVAL_FLAG_OVERLAP_OUTPUT  // !!! Used for HIJACK, but always?
    );
}


// Conditional constructs allow branches that are either BLOCK!s or ACTION!s.
// If an action, the triggering condition is passed to it as an argument:
// https://trello.com/c/ay9rnjIe
//
// Allowing other values was deemed to do more harm than good:
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
// Review if @word, @pa/th, @tu.p.le would make good branch types.  Issue
// would be that it would only be a shorthand for what could be said another
// way, and would conflate a fetching shorthand with non-isotopifying.  :-/
//
inline static bool Do_Branch_Core_Throws(
    REBVAL *out,
    const REBVAL *branch,
    const REBVAL *condition  // can be END, but use nullptr vs. a NULLED cell!
){
    assert(branch != out and condition != out);

    DECLARE_LOCAL (cell);

    enum Reb_Kind kind = VAL_TYPE(branch);

  redo:

    switch (kind) {
      case REB_BLANK:
        Init_Nulled(out);
        break;

      case REB_QUOTED:
        Unquotify(Copy_Cell(out, branch), 1);
        break;

      case REB_BLOCK:
        if (Do_Any_Array_At_Throws(SET_END(out), branch, SPECIFIED))
            return true;
        break;

      case REB_GET_BLOCK: {
        if (Eval_Value_Maybe_Stale_Throws(out, branch, SPECIFIED))
            return true;
        assert(IS_BLOCK(out));
        assert(not Is_Stale(out));
        break; }

      case REB_ACTION: {
        PUSH_GC_GUARD(branch);  // may be stored in `cell`, needs protection

        // If branch function argument isn't "meta" then we decay any isotopes.
        // Do the decay test first to avoid needing to scan parameters unless
        // it's one of those cases.
        //
        // !!! The theory here is that we're not throwing away any safety, as
        // the isotopification process was usually just for the purposes of
        // making the branch trigger or not.  With that addressed, it's just
        // inconvenient to force functions to be meta to get things like NULL.
        //
        //     if true [null] then x -> [
        //         ;
        //         ; Why would we want to have to make it ^x, when we know any
        //         ; nulls that triggered the branch would have been isotopic?
        //     ]
        //
        if (condition != nullptr and NOT_END(condition)) {
            const REBVAL *decayed = rebPointerToDecayed(condition);
            if (decayed != condition) {
                const REBKEY *key;
                const REBPAR *param = First_Unspecialized_Param(
                    &key,
                    VAL_ACTION(branch)
                );
                if (
                    param != nullptr
                    and VAL_PARAM_CLASS(param) != PARAM_CLASS_META
                ){
                    condition = decayed;
                }
            }
        }
        bool threw = rebRunThrows(
            SET_END(out),
            false,  // !fully, e.g. arity-0 functions can ignore condition
            branch,
            (condition != nullptr and IS_END(condition))
                ? rebEND
                : rebQ(condition)
        );
        DROP_GC_GUARD(branch);
        if (threw)
            return true;
        break; }

      case REB_GROUP:
        if (Do_Any_Array_At_Throws(cell, branch, SPECIFIED))
            return true;
        if (ANY_GROUP(cell))
            fail ("Branch evaluation cannot produce GROUP!");
        branch = cell;
        kind = VAL_TYPE(branch);
        goto redo;

      case REB_META_BLOCK:
        if (Do_Any_Array_At_Throws(out, branch, SPECIFIED))
            return true;
        if (IS_NULLED(out))
            Init_Bad_Word(out, Canon(NULL));  // branch taken, so not pure NULL
        else
            Meta_Quotify(out);  // result can't be null or void isotope
        break;

      default:
        fail (Error_Bad_Branch_Type_Raw());
    }

    // Note: At one time, this code ensured the result could not be null and
    // could not be void.  It makes a more homogenous model to put that
    // decision in the hands of the caller, where this "Do Branch" is actually
    // a more generic handler which may or may not want branch conventions.
    // So it is `return_branched` which does the null => null isotope and the
    // void => none conversion.  So perhaps it's more like "Do Clause" which
    // could be used on loop conditions as well as bodies.

    return false;
}

#define Do_Branch_With_Throws(out,branch,condition) \
    Do_Branch_Core_Throws((out), (branch), NULLIFY_NULLED(condition))

#define Do_Branch_Throws(out,branch) \
    Do_Branch_Core_Throws((out), (branch), END_CELL)


inline static REB_R Run_Generic_Dispatch_Core(
    const REBVAL *first_arg,  // !!! Is this always same as FRM_ARG(f, 1)?
    REBFRM *f,
    const REBSYM *verb
){
    GENERIC_HOOK *hook = IS_QUOTED(first_arg)
        ? &T_Quoted  // a few things like COPY are supported by QUOTED!
        : Generic_Hook_For_Type_Of(first_arg);

    REB_R r = hook(f, verb);  // Note that QUOTED! has its own hook & handling
    if (r == R_UNHANDLED)  // convenience for error handling
        fail (Error_Cannot_Use(verb, first_arg));

    return r;
}


// Some routines invoke Run_Generic_Dispatch(), go ahead and reduce the
// cases they have to look at by moving any ordinary outputs into f->out, and
// make throwing the only exceptional case they have to handle.
//
inline static bool Run_Generic_Dispatch_Throws(
    const REBVAL *first_arg,  // !!! Is this always same as FRM_ARG(f, 1)?
    REBFRM *f,
    const REBSYM *verb
){
    REB_R r = Run_Generic_Dispatch_Core(first_arg, f, verb);

    if (r == f->out) {
         // common case
    }
    else if (r == nullptr) {
        Init_Nulled(f->out);
    }
    else if (IS_RETURN_SIGNAL(r)) {
        if (r == R_THROWN)
            return true;
        assert(!"Unhandled return signal from Run_Generic_Dispatch_Core");
    }
    else {
        assert(not Is_Stale(r));
        Copy_Cell(f->out, r);
        if (Is_Api_Value(r))
            Release_Api_Value_If_Unmanaged(r);
    }
    return false;
}
