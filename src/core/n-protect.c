//
//  File: %n-protect.c
//  Summary: "native functions for series and object field protection"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#include "sys-core.h"


//
//  const: native [
//
//  {Return value whose access doesn't allow mutation to its argument}
//
//      return: [<opt> any-value!]
//      value "Argument to change access to (can be locked or not)"
//          [<opt> any-value!]  ; INTEGER!, etc. someday
//  ]
//
REBNATIVE(const) {
    INCLUDE_PARAMS_OF_CONST;

    REBVAL *v = ARG(value);
    if (IS_NULLED(v))
        return nullptr;

    CLEAR_CELL_FLAG(v, EXPLICITLY_MUTABLE);
    SET_CELL_FLAG(v, CONST);

    RETURN (v);
}


//
//  const?: native [
//
//  {Return if a value is a read-only view of its underlying data}
//
//      return: [logic!]
//      value [any-series! any-context!]
//  ]
//
REBNATIVE(const_q) {
    INCLUDE_PARAMS_OF_CONST_Q;

    // !!! Should this integrate the question of if the series is immutable,
    // besides just if the value is *const*, specifically?  Knowing the flag
    // is helpful for debugging at least.

    return Init_Logic(D_OUT, GET_CELL_FLAG(ARG(value), CONST));
}


//
//  mutable: native [
//
//  {Return value whose access allows mutation to its argument (if unlocked)}
//
//      return: "Same as input -- no errors are given if locked or immediate"
//          [<opt> any-value!]
//      value "Argument to change access to (if such access can be granted)"
//          [<opt> any-value!]  ; INTEGER!, etc. someday
//  ]
//
REBNATIVE(mutable)
{
    INCLUDE_PARAMS_OF_MUTABLE;

    REBVAL *v = ARG(value);

    if (IS_NULLED(v))
        return nullptr; // make it easier to pass through values

    // !!! The reason no error is given here is to make it easier to write
    // generic code which grants mutable access on things you might want
    // such access on, but passes through things like INTEGER!/etc.  If it
    // errored here, that would make the calling code more complex.  Better
    // to just error when they realize the thing is locked.

    CLEAR_CELL_FLAG(v, CONST);
    SET_CELL_FLAG(v, EXPLICITLY_MUTABLE);

    RETURN (v);
}


//
//  mutable?: native [
//
//  {Return if a value is a writable view of its underlying data}
//
//      return: [logic!]
//      value [any-series! any-context!]
//  ]
//
REBNATIVE(mutable_q) {
    INCLUDE_PARAMS_OF_MUTABLE_Q;

    // !!! Should this integrate the question of if the series is immutable,
    // besides just if the value is *const*, specifically?  Knowing the flag
    // is helpful for debugging at least.

    return Init_Logic(D_OUT, NOT_CELL_FLAG(ARG(value), CONST));
}


//
//  Protect_Key: C
//
static void Protect_Key(REBCTX *context, REBLEN index, REBFLGS flags)
{
    REBVAL *var = CTX_VAR(context, index);

    // Due to the fact that not all the bits in a value header are copied when
    // Move_Value is done, it's possible to set the protection status of a
    // variable on the value vs. the key.  This means the keylist does not
    // have to be modified, and hence it doesn't have to be made unique
    // from any objects that were sharing it.
    //
    if (flags & PROT_WORD) {
        ASSERT_CELL_READABLE_EVIL_MACRO(var, __FILE__, __LINE__);
        if (flags & PROT_SET)
            var->header.bits |= CELL_FLAG_PROTECTED;
        else
            var->header.bits &= ~CELL_FLAG_PROTECTED; // can't CLEAR_CELL_FLAG
    }

    if (flags & PROT_HIDE) {
        //
        // !!! For the moment, hiding is still implemented via typeset flags.
        // Since PROTECT/HIDE is something of an esoteric feature, keep it
        // that way for now, even though it means the keylist has to be
        // made unique.

        REBVAL *key = CTX_KEY(Force_Keylist_Unique(context), index);

        if (flags & PROT_SET) {
            TYPE_SET(key, REB_TS_HIDDEN);
            TYPE_SET(key, REB_TS_UNBINDABLE);
        }
        else {
            TYPE_CLEAR(key, REB_TS_HIDDEN);
            TYPE_CLEAR(key, REB_TS_UNBINDABLE);
        }
    }
}


//
//  Protect_Value: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Value(const RELVAL *v, REBFLGS flags)
{
    if (ANY_SERIES(v))
        Protect_Series(VAL_SERIES(v), VAL_INDEX(v), flags);
    else if (IS_MAP(v))
        Protect_Series(SER(MAP_PAIRLIST(VAL_MAP(v))), 0, flags);
    else if (ANY_CONTEXT(v))
        Protect_Context(VAL_CONTEXT(v), flags);
}


//
//  Protect_Series: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Series(const REBSER *s, REBLEN index, REBFLGS flags)
{
    if (Is_Series_Black(s))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            if (flags & PROT_DEEP)
                SET_SERIES_INFO(s, FROZEN_DEEP);
            SET_SERIES_INFO(s, FROZEN_SHALLOW);
        }
        else
            SET_SERIES_INFO(s, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        CLEAR_SERIES_INFO(s, PROTECTED);
    }

    if (not IS_SER_ARRAY(s) or not (flags & PROT_DEEP))
        return;

    Flip_Series_To_Black(s); // recursion protection

    const RELVAL *val = ARR_AT(ARR(s), index);
    for (; NOT_END(val); val++)
        Protect_Value(val, flags);
}


//
//  Protect_Context: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Context(REBCTX *c, REBFLGS flags)
{
    if (Is_Series_Black(SER(c)))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            if (flags & PROT_DEEP)
                SET_SERIES_INFO(c, FROZEN_DEEP);
            SET_SERIES_INFO(c, FROZEN_SHALLOW);
        }
        else
            SET_SERIES_INFO(c, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        CLEAR_SERIES_INFO(CTX_VARLIST(c), PROTECTED);
    }

    if (not (flags & PROT_DEEP))
        return;

    Flip_Series_To_Black(SER(CTX_VARLIST(c))); // for recursion

    REBVAL *var = CTX_VARS_HEAD(c);
    for (; NOT_END(var); ++var)
        Protect_Value(var, flags);
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(REBVAL *word, REBFLGS flags)
{
    if (ANY_WORD(word) and IS_WORD_BOUND(word)) {
        Protect_Key(VAL_WORD_CONTEXT(word), VAL_WORD_INDEX(word), flags);
        if (flags & PROT_DEEP) {
            //
            // Ignore existing mutability state so that it may be modified.
            // Most routines should NOT do this!
            //
            REBVAL *var = m_cast(
                REBVAL*,
                Lookup_Word_May_Fail(word, SPECIFIED)
            );
            Protect_Value(var, flags);
            Uncolor(var);
        }
    }
    else if (ANY_PATH(word)) {
        REBLEN index;
        REBCTX *context = Resolve_Path(word, &index);
        if (index == 0)
            fail ("Couldn't resolve PATH! in Protect_Word_Value");

        if (context != NULL) {
            Protect_Key(context, index, flags);
            if (flags & PROT_DEEP) {
                REBVAL *var = CTX_VAR(context, index);
                Protect_Value(var, flags);
                Uncolor(var);
            }
        }
    }
}


//
//  Protect_Unprotect_Core: C
//
// Common arguments between protect and unprotect:
//
static REB_R Protect_Unprotect_Core(REBFRM *frame_, REBFLGS flags)
{
    INCLUDE_PARAMS_OF_PROTECT;

    UNUSED(PAR(hide)); // unused here, but processed in caller

    REBVAL *value = ARG(value);

    // flags has PROT_SET bit (set or not)

    Check_Security_Placeholder(Canon(SYM_PROTECT), SYM_WRITE, value);

    if (REF(deep))
        flags |= PROT_DEEP;
    //if (REF(words))
    //  flags |= PROT_WORDS;

    if (IS_WORD(value) || IS_PATH(value)) {
        Protect_Word_Value(value, flags); // will unmark if deep
        RETURN (ARG(value));
    }

    if (IS_BLOCK(value)) {
        if (REF(words)) {
            const RELVAL *val;
            for (val = VAL_ARRAY_AT(value); NOT_END(val); val++) {
                DECLARE_LOCAL (word); // need binding, can't pass RELVAL
                Derelativize(word, val, VAL_SPECIFIER(value));
                Protect_Word_Value(word, flags);  // will unmark if deep
            }
            RETURN (ARG(value));
        }
        if (REF(values)) {
            REBVAL *var;
            const RELVAL *item;

            DECLARE_LOCAL (safe);

            for (item = VAL_ARRAY_AT(value); NOT_END(item); ++item) {
                if (IS_WORD(item)) {
                    //
                    // Since we *are* PROTECT we allow ourselves to get mutable
                    // references to even protected values to protect them.
                    //
                    var = m_cast(
                        REBVAL*,
                        Lookup_Word_May_Fail(item, VAL_SPECIFIER(value))
                    );
                }
                else if (IS_PATH(value)) {
                    Get_Path_Core(safe, value, SPECIFIED);
                    var = safe;
                }
                else {
                    Move_Value(safe, value);
                    var = safe;
                }

                Protect_Value(var, flags);
                if (flags & PROT_DEEP)
                    Uncolor(var);
            }
            RETURN (ARG(value));
        }
    }

    if (flags & PROT_HIDE)
        fail (Error_Bad_Refines_Raw());

    Protect_Value(value, flags);

    if (flags & PROT_DEEP)
        Uncolor(value);

    RETURN (ARG(value));
}


//
//  protect: native [
//
//  {Protect a series or a variable from being modified.}
//
//      value [word! path! any-series! bitset! map! object! module!]
//      /deep
//          "Protect all sub-series/objects as well"
//      /words
//          "Process list as words (and path words)"
//      /values
//          "Process list of values (implied GET)"
//      /hide
//          "Hide variables (avoid binding and lookup)"
//  ]
//
REBNATIVE(protect)
{
    INCLUDE_PARAMS_OF_PROTECT;

    // Avoid unused parameter warnings (core routine handles them via frame)
    //
    UNUSED(PAR(value));
    UNUSED(PAR(deep));
    UNUSED(PAR(words));
    UNUSED(PAR(values));

    REBFLGS flags = PROT_SET;

    if (REF(hide))
        flags |= PROT_HIDE;
    else
        flags |= PROT_WORD; // there is no unhide

    return Protect_Unprotect_Core(frame_, flags);
}


//
//  unprotect: native [
//
//  {Unprotect a series or a variable (it can again be modified).}
//
//      value [word! any-series! bitset! map! object! module!]
//      /deep
//          "Protect all sub-series as well"
//      /words
//          "Block is a list of words"
//      /values
//          "Process list of values (implied GET)"
//      /hide
//          "HACK to make PROTECT and UNPROTECT have the same signature"
//  ]
//
REBNATIVE(unprotect)
{
    INCLUDE_PARAMS_OF_UNPROTECT;

    // Avoid unused parameter warnings (core handles them via frame)
    //
    UNUSED(PAR(value));
    UNUSED(PAR(deep));
    UNUSED(PAR(words));
    UNUSED(PAR(values));

    if (REF(hide))
        fail ("Cannot un-hide an object field once hidden");

    return Protect_Unprotect_Core(frame_, PROT_WORD);
}


//
//  Is_Value_Frozen_Deep: C
//
// "Frozen" is a stronger term here than "Immutable".  Mutable refers to the
// mutable/const distinction, where a value being immutable doesn't mean its
// series will never change in the future.  The frozen requirement is needed
// in order to do things like use blocks as map keys, etc.
//
bool Is_Value_Frozen_Deep(const RELVAL *v) {
    REBCEL(const*) cell = VAL_UNESCAPED(v);
    UNUSED(v); // debug build trashes, to avoid accidental usage below

    enum Reb_Kind kind = CELL_KIND(cell);
    if (
        kind == REB_BLANK
        or ANY_SCALAR_KIND(kind)
        or ANY_WORD_KIND(kind)
        or kind == REB_ACTION // paramlist is identity, hash
    ){
        return true;
    }

    if (ANY_ARRAY_OR_PATH_KIND(kind))
        return Is_Array_Frozen_Deep(VAL_ARRAY(cell));

    if (ANY_CONTEXT_KIND(kind))
        return Is_Context_Frozen_Deep(VAL_CONTEXT(cell));

    if (ANY_SERIES_KIND(kind))
        return Is_Series_Frozen(VAL_SERIES(cell));

    return false;
}


//
//  locked?: native [
//
//  {Determine if the value is locked (deeply and permanently immutable)}
//
//      return: [logic!]
//      value [any-value!]
//  ]
//
REBNATIVE(locked_q)
{
    INCLUDE_PARAMS_OF_LOCKED_Q;

    return Init_Logic(D_OUT, Is_Value_Frozen_Deep(ARG(value)));
}


//
//  Force_Value_Frozen: C
//
// !!! The concept behind `opt_locker` is that it might be able to give the
// user more information about why data would be automatically locked, e.g.
// if locked for reason of using as a map key...for instance.  It could save
// the map, or the file and line information for the interpreter at that
// moment, etc.  Just put a flag at the top level for now, since that is
// "better than nothing", and revisit later in the design.
//
// !!! Note this is currently allowed to freeze CONST values.  Review, as
// the person who gave const access may have intended to prevent changes
// that would prevent *them* from later mutating it.
//
void Force_Value_Frozen_Core(
    const RELVAL *v,
    bool deep,
    REBSER *opt_locker
){
    if (Is_Value_Frozen_Deep(v))
        return;

    REBCEL(const*) cell = VAL_UNESCAPED(v);
    enum Reb_Kind kind = CELL_KIND(cell);

    if (ANY_ARRAY_KIND(kind)) {
        if (deep)
            Freeze_Array_Deep(m_cast(REBARR*, VAL_ARRAY(cell)));
        else
            Freeze_Array_Shallow(m_cast(REBARR*, VAL_ARRAY(cell)));
        if (opt_locker)
            SET_SERIES_INFO(VAL_ARRAY(cell), AUTO_LOCKED);
    }
    else if (ANY_CONTEXT_KIND(kind)) {
        if (deep)
            Deep_Freeze_Context(VAL_CONTEXT(cell));
        else
            fail ("What does a shallow freeze of a context mean?");
        if (opt_locker)
            SET_SERIES_INFO(VAL_CONTEXT(cell), AUTO_LOCKED);
    }
    else if (ANY_SERIES_KIND(kind)) {
        Freeze_Series(VAL_SERIES(cell));
        UNUSED(deep);
        if (opt_locker)
            SET_SERIES_INFO(VAL_SERIES(cell), AUTO_LOCKED);
    } else
        fail (Error_Invalid_Type(kind)); // not yet implemented
}


//
//  freeze: native [
//
//  {Permanently lock values (if applicable) so they can be immutably shared.}
//
//      value "Value to make permanently immutable"
//          [any-value!]
//      /deep "Freeze deeply"
//  ;   /blame "What to report as source of lock in error"
//  ;       [any-series!]  ; not exposed for the moment
//  ]
//
REBNATIVE(freeze)
{
    INCLUDE_PARAMS_OF_FREEZE;

    // REF(blame) is not exposed as a feature because there's nowhere to store
    // locking information in the series.  So the only thing that happens if
    // you pass in something other than null is SERIES_FLAG_AUTO_LOCKED is set
    // to deliver a message that the system locked something implicitly.  We
    // don't want to say that here, so hold off on the feature.
    //
    REBSER *locker = nullptr;
    Force_Value_Frozen_Core(ARG(value), did REF(deep), locker);

    RETURN (ARG(value));
}
