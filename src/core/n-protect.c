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
    if (GET_FLAG(flags, PROT_WORD)) {
        if (GET_FLAG(flags, PROT_SET))
            Set_Val_Flag(var, VALUE_FLAG_PROTECTED);
        else
            Clear_Val_Flag(var, VALUE_FLAG_PROTECTED);
    }

    if (GET_FLAG(flags, PROT_HIDE)) {
        //
        // !!! For the moment, hiding is still implemented via typeset flags.
        // Since PROTECT/HIDE is something of an esoteric feature, keep it
        // that way for now, even though it means the keylist has to be
        // made unique.
        //
        Ensure_Keylist_Unique_Invalidated(context);

        REBVAL *key = CTX_KEY(context, index);

        if (GET_FLAG(flags, PROT_SET))
            Set_Val_Flags(key, TYPESET_FLAG_HIDDEN | TYPESET_FLAG_UNBINDABLE);
        else
            Clear_Val_Flags(
                key, TYPESET_FLAG_HIDDEN | TYPESET_FLAG_UNBINDABLE
            );
    }
}


//
//  Protect_Value: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Value(RELVAL *value, REBFLGS flags)
{
    if (ANY_SERIES(value) || IS_MAP(value))
        Protect_Series(VAL_SERIES(value), VAL_INDEX(value), flags);
    else if (ANY_CONTEXT(value))
        Protect_Context(VAL_CONTEXT(value), flags);
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

    if (GET_FLAG(flags, PROT_SET)) {
        if (GET_FLAG(flags, PROT_FREEZE)) {
            assert(GET_FLAG(flags, PROT_DEEP));
            Set_Ser_Info(s, SERIES_INFO_FROZEN);
        }
        else
            Set_Ser_Info(s, SERIES_INFO_PROTECTED);
    }
    else {
        assert(!GET_FLAG(flags, PROT_FREEZE));
        Clear_Ser_Info(s, SERIES_INFO_PROTECTED);
    }

    if (Not_Ser_Flag(s, SERIES_FLAG_ARRAY) || !GET_FLAG(flags, PROT_DEEP))
        return;

    Flip_Series_To_Black(s); // recursion protection

    RELVAL *val = ARR_AT(AS_ARRAY(s), index);
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
    if (Is_Series_Black(AS_SERIES(CTX_VARLIST(c))))
        return; // avoid loop

    if (GET_FLAG(flags, PROT_SET)) {
        if (GET_FLAG(flags, PROT_FREEZE)) {
            assert(GET_FLAG(flags, PROT_DEEP));
            Set_Ser_Info(CTX_VARLIST(c), SERIES_INFO_FROZEN);
        }
        else
            Set_Ser_Info(CTX_VARLIST(c), SERIES_INFO_PROTECTED);
    }
    else {
        assert(!GET_FLAG(flags, PROT_FREEZE));
        Clear_Ser_Info(CTX_VARLIST(c), SERIES_INFO_PROTECTED);
    }

    if (!GET_FLAG(flags, PROT_DEEP)) return;

    Flip_Series_To_Black(AS_SERIES(CTX_VARLIST(c))); // for recursion

    REBVAL *var = CTX_VARS_HEAD(c);
    for (; NOT_END(var); ++var)
        Protect_Value(var, flags);
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(REBVAL *word, REBFLGS flags)
{
    if (ANY_WORD(word) && IS_WORD_BOUND(word)) {
        Protect_Key(VAL_WORD_CONTEXT(word), VAL_WORD_INDEX(word), flags);
        if (GET_FLAG(flags, PROT_DEEP)) {
            //
            // Ignore existing mutability state so that it may be modified.
            // Most routines should NOT do this!
            //
            REBVAL *var = Get_Var_Core(
                word,
                SPECIFIED,
                GETVAR_READ_ONLY
            );
            Protect_Value(var, flags);
            Uncolor(var);
        }
    }
    else if (ANY_PATH(word)) {
        REBCNT index;
        REBCTX *context = Resolve_Path(word, &index);

        if (context != NULL) {
            Protect_Key(context, index, flags);
            if (GET_FLAG(flags, PROT_DEEP)) {
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

    if (REF(deep)) SET_FLAG(flags, PROT_DEEP);
    //if (REF(words)) SET_FLAG(flags, PROT_WORD);

    if (IS_WORD(value) || IS_PATH(value)) {
        Protect_Word_Value(value, flags); // will unmark if deep
        goto return_value_arg;
    }

    if (IS_BLOCK(value)) {
        if (REF(words)) {
            RELVAL *val;
            for (val = VAL_ARRAY_AT(value); NOT_END(val); val++) {
                DECLARE_LOCAL (word); // need binding, can't pass RELVAL
                Derelativize(word, val, VAL_SPECIFIER(value));
                Protect_Word_Value(word, flags);  // will unmark if deep
            }
            goto return_value_arg;
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
                    var = Get_Var_Core(
                        item,
                        VAL_SPECIFIER(value),
                        GETVAR_READ_ONLY
                    );
                }
                else if (IS_PATH(value)) {
                    if (Do_Path_Throws_Core(
                        safe, NULL, value, SPECIFIED, NULL
                    ))
                        fail (Error_No_Catch_For_Throw(safe));

                    var = safe;
                }
                else {
                    Move_Value(safe, value);
                    var = safe;
                }

                Protect_Value(var, flags);
                if (GET_FLAG(flags, PROT_DEEP))
                    Uncolor(var);
            }
            goto return_value_arg;
        }
    }

    if (GET_FLAG(flags, PROT_HIDE)) fail (Error_Bad_Refines_Raw());

    Protect_Value(value, flags);

    if (GET_FLAG(flags, PROT_DEEP))
        Uncolor(value);

return_value_arg:
    Move_Value(D_OUT, ARG(value));
    return R_OUT;
}


//
//  protect: native [
//
//  {Protect a series or a variable from being modified.}
//
//      value [word! any-series! bitset! map! object! module!]
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

    REBFLGS flags = FLAGIT(PROT_SET);

    if (REF(hide))
        SET_FLAG(flags, PROT_HIDE);
    else
        SET_FLAG(flags, PROT_WORD); // there is no unhide

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

    return Protect_Unprotect_Core(frame_, FLAGIT(PROT_WORD));
}


//
//  Is_Value_Immutable: C
//
REBOOL Is_Value_Immutable(const RELVAL *v) {
    if (
        IS_BLANK(v)
        || IS_BAR(v)
        || IS_LIT_BAR(v)
        || ANY_SCALAR(v)
        || ANY_WORD(v)
    ){
        return TRUE;
    }

    if (ANY_ARRAY(v))
        return Is_Array_Deeply_Frozen(VAL_ARRAY(v));

    if (ANY_CONTEXT(v))
        return Is_Context_Deeply_Frozen(VAL_CONTEXT(v));

    if (ANY_SERIES(v))
        return Is_Series_Frozen(VAL_SERIES(v));

    return FALSE;
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

    return R_FROM_BOOL(Is_Value_Immutable(ARG(value)));
}


//
//  Ensure_Value_Immutable: C
//
void Ensure_Value_Immutable(REBVAL *v) {
    if (Is_Value_Immutable(v))
        return;

    if (ANY_ARRAY(v))
        Deep_Freeze_Array(VAL_ARRAY(v));
    else if (ANY_CONTEXT(v))
        Deep_Freeze_Context(VAL_CONTEXT(v));
    else if (ANY_SERIES(v))
        Freeze_Sequence(VAL_SERIES(v));
    else
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
        if (ANY_ARRAY(v)) {
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
            const REBOOL deep = TRUE;
            const REBU64 types = TS_STD_SERIES;

            Init_Any_Context(
                D_OUT,
                VAL_TYPE(v),
                Copy_Context_Core(VAL_CONTEXT(v), deep, types)
            );
        }
        else if (ANY_SERIES(v)) {
            Init_Any_Series_At(
                D_OUT,
                VAL_TYPE(v),
                Copy_Sequence(VAL_SERIES(v)),
                VAL_INDEX(v)
            );
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(v))); // not yet implemented
    }

    Ensure_Value_Immutable(D_OUT);

    return R_OUT;
}
