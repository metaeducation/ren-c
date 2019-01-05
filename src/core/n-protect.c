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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
//          [<opt> any-value!] ;-- INTEGER!, etc. someday
//  ]
//
REBNATIVE(const) {
    INCLUDE_PARAMS_OF_CONST;

    REBVAL *v = ARG(value);
    if (IS_NULLED(v))
        return nullptr;

    CLEAR_VAL_FLAG(v, VALUE_FLAG_EXPLICITLY_MUTABLE);
    SET_VAL_FLAG(v, VALUE_FLAG_CONST);

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

    return Init_Logic(D_OUT, GET_VAL_FLAG(ARG(value), VALUE_FLAG_CONST));
}


//
//  mutable: native [
//
//  {Return value whose access allows mutation to its argument (if unlocked)}
//
//      return: [<opt> any-value!]
//      value "Argument to change access to (errors if locked)"
//          [<opt> any-value!] ;-- INTEGER!, etc. someday
//  ]
//
REBNATIVE(mutable)
{
    INCLUDE_PARAMS_OF_MUTABLE;

    REBVAL *v = ARG(value);
    if (IS_NULLED(v))
        return nullptr; // make it easier to pass through values

    CLEAR_VAL_FLAG(v, VALUE_FLAG_CONST); // don't trip const test for readonly
    if (ANY_SERIES(v) or ANY_CONTEXT(v))
        FAIL_IF_READ_ONLY_SERIES(v);
    SET_VAL_FLAG(v, VALUE_FLAG_EXPLICITLY_MUTABLE);

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

    return Init_Logic(D_OUT, NOT_VAL_FLAG(ARG(value), VALUE_FLAG_CONST));
}


//
//  Protect_Key: C
//
static void Protect_Key(REBCTX *context, REBCNT index, REBFLGS flags)
{
    REBVAL *var = CTX_VAR(context, index);

    // Due to the fact that not all the bits in a value header are copied when
    // Move_Value is done, it's possible to set the protection status of a
    // variable on the value vs. the key.  This means the keylist does not
    // have to be modified, and hence it doesn't have to be made unique
    // from any objects that were sharing it.
    //
    if (flags & PROT_WORD) {
        if (flags & PROT_SET)
            SET_VAL_FLAG(var, CELL_FLAG_PROTECTED);
        else
            CLEAR_VAL_FLAG(var, CELL_FLAG_PROTECTED);
    }

    if (flags & PROT_HIDE) {
        //
        // !!! For the moment, hiding is still implemented via typeset flags.
        // Since PROTECT/HIDE is something of an esoteric feature, keep it
        // that way for now, even though it means the keylist has to be
        // made unique.
        //
        Ensure_Keylist_Unique_Invalidated(context);

        REBVAL *key = CTX_KEY(context, index);

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
void Protect_Value(RELVAL *v, REBFLGS flags)
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
void Protect_Series(REBSER *s, REBCNT index, REBFLGS flags)
{
    if (Is_Series_Black(s))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            assert(flags & PROT_DEEP);
            SET_SER_INFO(s, SERIES_INFO_FROZEN);
        }
        else
            SET_SER_INFO(s, SERIES_INFO_PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        CLEAR_SER_INFO(s, SERIES_INFO_PROTECTED);
    }

    if (not IS_SER_ARRAY(s) or not (flags & PROT_DEEP))
        return;

    Flip_Series_To_Black(s); // recursion protection

    RELVAL *val = ARR_AT(ARR(s), index);
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
            assert(flags & PROT_DEEP);
            SET_SER_INFO(c, SERIES_INFO_FROZEN);
        }
        else
            SET_SER_INFO(c, SERIES_INFO_PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        CLEAR_SER_INFO(CTX_VARLIST(c), SERIES_INFO_PROTECTED);
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
                Get_Opt_Var_May_Fail(word, SPECIFIED)
            );
            Protect_Value(var, flags);
            Uncolor(var);
        }
    }
    else if (ANY_PATH(word)) {
        REBCNT index;
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

    Check_Security(Canon(SYM_PROTECT), POL_WRITE, value);

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
            RELVAL *val;
            for (val = VAL_ARRAY_AT(value); NOT_END(val); val++) {
                DECLARE_LOCAL (word); // need binding, can't pass RELVAL
                Derelativize(word, val, VAL_SPECIFIER(value));
                Protect_Word_Value(word, flags);  // will unmark if deep
            }
            RETURN (ARG(value));
        }
        if (REF(values)) {
            REBVAL *var;
            RELVAL *item;

            DECLARE_LOCAL (safe);

            for (item = VAL_ARRAY_AT(value); NOT_END(item); ++item) {
                if (IS_WORD(item)) {
                    //
                    // Since we *are* PROTECT we allow ourselves to get mutable
                    // references to even protected values to protect them.
                    //
                    var = m_cast(
                        REBVAL*,
                        Get_Opt_Var_May_Fail(item, VAL_SPECIFIER(value))
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
//  Is_Value_Frozen: C
//
// "Frozen" is a stronger term here than "Immutable".  Mutable refers to the
// mutable/const distinction, where a value being immutable doesn't mean its
// series will never change in the future.  The frozen requirement is needed
// in order to do things like use blocks as map keys, etc.
//
bool Is_Value_Frozen(const RELVAL *v) {
    if (
        IS_BLANK(v)
        || IS_BAR(v)
        || ANY_SCALAR(v)
        || ANY_WORD(v)
        || IS_ACTION(v) // paramlist is identity, hash
    ){
        return true;
    }

    if (ANY_ARRAY_OR_PATH(v))
        return Is_Array_Deeply_Frozen(VAL_ARRAY(v));

    if (ANY_CONTEXT(v))
        return Is_Context_Deeply_Frozen(VAL_CONTEXT(v));

    if (ANY_SERIES(v))
        return Is_Series_Frozen(VAL_SERIES(v));

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

    return Init_Logic(D_OUT, Is_Value_Frozen(ARG(value)));
}


//
//  Ensure_Value_Frozen: C
//
// !!! The concept behind `opt_locker` is that it might be able to give the
// user more information about why data would be automatically locked, e.g.
// if locked for reason of using as a map key...for instance.  It could save
// the map, or the file and line information for the interpreter at that
// moment, etc.  Just put a flag at the top level for now, since that is
// "better than nothing", and revisit later in the design.
//
void Ensure_Value_Frozen(const RELVAL *v, REBSER *opt_locker) {
    if (Is_Value_Frozen(v))
        return;

    if (ANY_ARRAY_OR_PATH(v)) {
        Deep_Freeze_Array(VAL_ARRAY(v));
        if (opt_locker)
            SET_SER_INFO(VAL_ARRAY(v), SERIES_INFO_AUTO_LOCKED);
    }
    else if (ANY_CONTEXT(v)) {
        Deep_Freeze_Context(VAL_CONTEXT(v));
        if (opt_locker)
            SET_SER_INFO(VAL_CONTEXT(v), SERIES_INFO_AUTO_LOCKED);
    }
    else if (ANY_SERIES(v)) {
        Freeze_Sequence(VAL_SERIES(v));
        if (opt_locker != NULL)
            SET_SER_INFO(VAL_SERIES(v), SERIES_INFO_AUTO_LOCKED);
    } else
        fail (Error_Invalid_Type(VAL_TYPE(v))); // not yet implemented
}


//
//  lock: native [
//
//  {Permanently lock values (if applicable) so they can be immutably shared.}
//
//      value [any-value!]
//          {Value to lock (will be locked deeply if an ANY-ARRAY!)}
//      /clone
//          {Will lock a clone of the original (if not already immutable)}
//  ]
//
REBNATIVE(lock)
//
// !!! COPY in Rebol truncates before the index.  You can't `y: copy next x`
// and then `first back y` to get at a copy of the the original `first x`.
//
// This locking operation is opportunistic in terms of whether it actually
// copies the data or not.  But if it did just a normal COPY, it'd truncate,
// while if it just passes the value through it does not truncate.  So
// `lock/copy x` wouldn't be semantically equivalent to `lock copy x` :-/
//
// So the strategy here is to go with a different option, CLONE.  CLONE was
// already being considered as an operation due to complaints about backward
// compatibility if COPY were changed to /DEEP by default.
//
// The "freezing" bit can only be used on deep copies, so it would not make
// sense to use with a shallow one.  However, a truncating COPY/DEEP could
// be made to have a version operating on read only data that reused a
// subset of the data.  This would use a "slice"; letting one series refer
// into another, with a different starting point.  That would complicate the
// garbage collector because multiple REBSER would be referring into the same
// data.  So that's a possibility.
{
    INCLUDE_PARAMS_OF_LOCK;

    REBVAL *v = ARG(value);

    if (!REF(clone))
        Move_Value(D_OUT, v);
    else {
        if (ANY_ARRAY_OR_PATH(v)) {
            Init_Any_Array_At(
                D_OUT,
                VAL_TYPE(v),
                Copy_Array_Deep_Managed(
                    VAL_ARRAY(v),
                    VAL_SPECIFIER(v)
                ),
                VAL_INDEX(v)
            );
        }
        else if (ANY_CONTEXT(v)) {
            Init_Any_Context(
                D_OUT,
                VAL_TYPE(v),
                Copy_Context_Core_Managed(VAL_CONTEXT(v), TS_STD_SERIES)
            );
        }
        else if (ANY_SERIES(v)) {
            Init_Any_Series_At(
                D_OUT,
                VAL_TYPE(v),
                Copy_Sequence_Core(VAL_SERIES(v), NODE_FLAG_MANAGED),
                VAL_INDEX(v)
            );
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(v))); // not yet implemented
    }

    REBSER *locker = NULL;
    Ensure_Value_Frozen(D_OUT, locker);

    return D_OUT;
}
