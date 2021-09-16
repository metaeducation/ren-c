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
//  Protect_Var: C
//
// In R3-Alpha, protection status was put on context key cells.  This made for
// problems when keylists were reused.  Ren-C goes even further to reduce
// keylists to being just lists of symbols, not full cells.  The key is not
// the right place for the flag.
//
// So the flag is put in a bit on the variable storage cell which is not
// copied when the cell is copied.  This "active masking" in cell copying is
// a new-to-Ren-C feature; you have to use Copy_Cell(), Move_Cell() and
// Derelativize() vs. just blitting the raw bits of a cell around.  (The C++
// build enforces this by disallowing direct bit assignment via `=`).
//
static void Protect_Var(REBVAL *var, REBFLGS flags)
{
    if (flags & PROT_WORD) {
        assert(READABLE(var));
        if (flags & PROT_SET)
            var->header.bits |= CELL_FLAG_PROTECTED;
        else
            var->header.bits &= ~CELL_FLAG_PROTECTED; // can't CLEAR_CELL_FLAG
    }

    if (flags & PROT_HIDE) {
        //
        // R3-Alpha implemented hiding via typeset flags, which would have
        // meant making a new keylist.  Ren-C does this with a flag that lives
        // in the cell of the variable.

        if (flags & PROT_SET)
            var->header.bits |= CELL_FLAG_VAR_MARKED_HIDDEN;
        else
            fail ("Un-hiding is not supported");
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
        Protect_Series(MAP_PAIRLIST(VAL_MAP(v)), 0, flags);
    else if (ANY_CONTEXT(v))
        Protect_Context(VAL_CONTEXT(v), flags);
}


//
//  Protect_Series: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Series(const REBSER *s_const, REBLEN index, REBFLGS flags)
{
    REBSER *s = m_cast(REBSER*, s_const);  // mutate flags only

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

    const RELVAL *val_tail = ARR_TAIL(ARR(s));
    const RELVAL *val = ARR_AT(ARR(s), index);
    for (; val != val_tail; val++)
        Protect_Value(val, flags);
}


//
//  Protect_Context: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Context(REBCTX *c, REBFLGS flags)
{
    REBARR *varlist = m_cast(REBARR*, CTX_VARLIST(c));  // mutate flags only

    if (Is_Series_Black(varlist))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            if (flags & PROT_DEEP)
                SET_SERIES_INFO(varlist, FROZEN_DEEP);
            SET_SERIES_INFO(varlist, FROZEN_SHALLOW);
        }
        else
            SET_SERIES_INFO(varlist, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        CLEAR_SERIES_INFO(varlist, PROTECTED);
    }

    if (not (flags & PROT_DEEP))
        return;

    Flip_Series_To_Black(varlist); // for recursion

    const REBVAR *var_tail;
    REBVAL *var = CTX_VARS(&var_tail, c);
    for (; var != var_tail; ++var)
        Protect_Value(var, flags);
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(REBVAL *word, REBFLGS flags)
{
    if (ANY_WORD(word) and IS_WORD_BOUND(word)) {
        //
        // Ignore existing mutability state so that it may be modified.
        // Most routines should NOT do this!
        //
        REBVAL *var = m_cast(
            REBVAL*,
            Lookup_Word_May_Fail(word, SPECIFIED)
        );

        Protect_Var(var, flags);
        if (flags & PROT_DEEP) {
            Protect_Value(var, flags);
            Uncolor(var);
        }
    }
    else if (ANY_SEQUENCE(word)) {
        REBLEN index;
        REBCTX *context = Resolve_Path(word, &index);
        if (index == 0)
            fail ("Couldn't resolve PATH! in Protect_Word_Value");

        if (context != NULL) {
            REBVAL *var = CTX_VAR(context, index);
            Protect_Var(var, flags);
            if (flags & PROT_DEEP) {
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

    if (REF(deep))
        flags |= PROT_DEEP;
    //if (REF(words))
    //  flags |= PROT_WORDS;

    if (ANY_WORD(value) || ANY_SEQUENCE(value)) {
        Protect_Word_Value(value, flags); // will unmark if deep
        RETURN (ARG(value));
    }

    if (IS_BLOCK(value)) {
        if (REF(words)) {
            const RELVAL *tail;
            const RELVAL *item = VAL_ARRAY_AT(&tail, value);
            for (; item != tail; ++item) {
                DECLARE_LOCAL (word); // need binding, can't pass RELVAL
                Derelativize(word, item, VAL_SPECIFIER(value));
                Protect_Word_Value(word, flags);  // will unmark if deep
            }
            RETURN (ARG(value));
        }
        if (REF(values)) {
            REBVAL *var;
            const RELVAL *tail;
            const RELVAL *item = VAL_ARRAY_AT(&tail, value);

            DECLARE_LOCAL (safe);

            for (; item != tail; ++item) {
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
                    if (Eval_Path_Throws_Core(
                        safe,
                        value,
                        SPECIFIED,
                        EVAL_MASK_DEFAULT | EVAL_FLAG_NO_PATH_GROUPS
                    )){
                        panic (safe); // shouldn't be possible... no executions!
                    }

                    var = safe;
                }
                else {
                    Copy_Cell(safe, value);
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
//      return: [
//          any-word! any-sequence! any-series! bitset! map! object! module!
//      ]
//      value [
//          any-word! any-sequence! any-series! bitset! map! object! module!
//      ]
//      /deep "Protect all sub-series/objects as well"
//      /words "Process list as words (and path words)"
//      /values "Process list of values (implied GET)"
//      /hide "Hide variables (avoid binding and lookup)"
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
//      return: [word! any-series! bitset! map! object! module!]
//      value [word! any-series! bitset! map! object! module!]
//      /deep "Protect all sub-series as well"
//      /words "Block is a list of words"
//      /values "Process list of values (implied GET)"
//      /hide "HACK to make PROTECT and UNPROTECT have the same signature"
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

    if (NOT_CELL_FLAG(cell, FIRST_IS_NODE))
        return true;  // payloads that live in cell are immutable

    REBNOD *node = VAL_NODE1(cell);
    if (node == nullptr or Is_Node_Cell(node))
        return true;  // !!! Will all non-quoted Pairings be frozen?

    // Frozen deep should be set even on non-arrays, e.g. all frozen shallow
    // strings should also have SERIES_INFO_FROZEN_DEEP.
    //
    return GET_SERIES_INFO(SER(node), FROZEN_DEEP);
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
// !!! The concept behind `locker` is that it might be able to give the
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
    option(REBSER*) locker
){
    if (Is_Value_Frozen_Deep(v))
        return;

    REBCEL(const*) cell = VAL_UNESCAPED(v);
    enum Reb_Kind kind = CELL_KIND(cell);

    if (ANY_ARRAY_KIND(kind)) {
        REBARR *a = m_cast(REBARR*, VAL_ARRAY(cell));  // mutate flags only
        if (deep)
            Freeze_Array_Deep(a);
        else
            Freeze_Array_Shallow(a);
        if (locker)
            SET_SERIES_INFO(a, AUTO_LOCKED);
    }
    else if (ANY_CONTEXT_KIND(kind)) {
        REBCTX *c = VAL_CONTEXT(cell);
        if (deep)
            Deep_Freeze_Context(c);
        else
            fail ("What does a shallow freeze of a context mean?");
        if (locker)
            SET_SERIES_INFO(m_cast(REBARR*, CTX_VARLIST(c)), AUTO_LOCKED);
    }
    else if (ANY_SERIES_KIND(kind)) {
        REBSER *s = m_cast(REBSER*, VAL_SERIES(cell));  // mutate flags only
        Freeze_Series(s);
        UNUSED(deep);
        if (locker)
            SET_SERIES_INFO(s, AUTO_LOCKED);
    } else if (kind == REB_ACTION or kind == REB_DATATYPE) {
        // No freezing needed
    } else if (ANY_SEQUENCE_KIND(kind)) {
        // No freezing needed
    }
    else
        fail (Error_Invalid_Type(kind)); // not yet implemented
}


//
//  freeze: native [
//
//  {Permanently lock values (if applicable) so they can be immutably shared.}
//
//      return: [any-value!]
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
