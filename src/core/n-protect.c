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
// Copyright 2012-2016 Rebol Open Source Contributors
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
static void Protect_Key(RELVAL *key, REBFLGS flags)
{
    if (GET_FLAG(flags, PROT_WORD)) {
        if (GET_FLAG(flags, PROT_SET))
            SET_VAL_FLAG(key, TYPESET_FLAG_PROTECTED);
        else
            CLEAR_VAL_FLAG(key, TYPESET_FLAG_PROTECTED);
    }

    if (GET_FLAG(flags, PROT_HIDE)) {
        if (GET_FLAG(flags, PROT_SET))
            SET_VAL_FLAGS(key, TYPESET_FLAG_HIDDEN | TYPESET_FLAG_UNBINDABLE);
        else
            CLEAR_VAL_FLAGS(
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
            s->header.bits |= REBSER_REBVAL_FLAG_FROZEN;
        }
        else
            s->header.bits |= REBSER_FLAG_PROTECTED;
    }
    else {
        assert(!GET_FLAG(flags, PROT_FREEZE));
        s->header.bits &= ~cast(REBUPT, REBSER_FLAG_PROTECTED);
    }

    if (!Is_Array_Series(s) || !GET_FLAG(flags, PROT_DEEP))
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
    if (Is_Series_Black(ARR_SERIES(CTX_VARLIST(c))))
        return; // avoid loop

    if (GET_FLAG(flags, PROT_SET)) {
        if (GET_FLAG(flags, PROT_FREEZE)) {
            assert(GET_FLAG(flags, PROT_DEEP));
            ARR_SERIES(CTX_VARLIST(c))->header.bits
                |= REBSER_REBVAL_FLAG_FROZEN;
        }
        else {
            ARR_SERIES(CTX_VARLIST(c))->header.bits
                |= REBSER_FLAG_PROTECTED;
        }
    }
    else {
        assert(!GET_FLAG(flags, PROT_FREEZE));
        ARR_SERIES(CTX_VARLIST(c))->header.bits
            &= ~cast(REBUPT, REBSER_FLAG_PROTECTED);
    }

    // !!! Keylist may be shared!  R3-Alpha did not account for this, but
    // if you don't want a protect of one object to protect its instances
    // there will be a problem.
    //
    REBVAL *key = CTX_KEYS_HEAD(c);
    for (; NOT_END(key); ++key)
        Protect_Key(key, flags);

    if (!GET_FLAG(flags, PROT_DEEP)) return;

    Flip_Series_To_Black(ARR_SERIES(CTX_VARLIST(c))); // for recursion

    REBVAL *var = CTX_VARS_HEAD(c);
    for (; NOT_END(var); ++var)
        Protect_Value(var, flags);
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(REBVAL *word, REBFLGS flags)
{
    REBVAL *key;
    REBVAL *val;

    if (ANY_WORD(word) && IS_WORD_BOUND(word)) {
        key = CTX_KEY(VAL_WORD_CONTEXT(word), VAL_WORD_INDEX(word));
        Protect_Key(key, flags);
        if (GET_FLAG(flags, PROT_DEEP)) {
            //
            // Ignore existing mutability state so that it may be modified.
            // Most routines should NOT do this!
            //
            enum Reb_Kind eval_type; // unused
            val = Get_Var_Core(
                &eval_type,
                word,
                SPECIFIED,
                GETVAR_READ_ONLY
            );
            Protect_Value(val, flags);
            Uncolor(val);
        }
    }
    else if (ANY_PATH(word)) {
        REBCNT index;
        REBCTX *context;
        if ((context = Resolve_Path(word, &index))) {
            key = CTX_KEY(context, index);
            Protect_Key(key, flags);
            if (GET_FLAG(flags, PROT_DEEP)) {
                val = CTX_VAR(context, index);
                Protect_Value(val, flags);
                Uncolor(val);
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
                REBVAL word; // need binding intact, can't just pass RELVAL
                COPY_VALUE(&word, val, VAL_SPECIFIER(value));
                Protect_Word_Value(&word, flags);  // will unmark if deep
            }
            goto return_value_arg;
        }
        if (REF(values)) {
            REBVAL *var;
            RELVAL *item;

            REBVAL safe;

            for (item = VAL_ARRAY_AT(value); NOT_END(item); ++item) {
                if (IS_WORD(item)) {
                    //
                    // Since we *are* PROTECT we allow ourselves to get mutable
                    // references to even protected values to protect them.
                    //
                    enum Reb_Kind eval_type; // unused
                    var = Get_Var_Core(
                        &eval_type,
                        item,
                        VAL_SPECIFIER(value),
                        GETVAR_READ_ONLY
                    );
                }
                else if (IS_PATH(value)) {
                    if (Do_Path_Throws_Core(
                        &safe, NULL, value, SPECIFIED, NULL
                    ))
                        fail (Error_No_Catch_For_Throw(&safe));

                    var = &safe;
                }
                else {
                    safe = *value;
                    var = &safe;
                }

                Protect_Value(var, flags);
                if (GET_FLAG(flags, PROT_DEEP))
                    Uncolor(var);
            }
            goto return_value_arg;
        }
    }

    if (GET_FLAG(flags, PROT_HIDE)) fail (Error(RE_BAD_REFINES));

    Protect_Value(value, flags);

    if (GET_FLAG(flags, PROT_DEEP))
        Uncolor(value);

return_value_arg:
    *D_OUT = *ARG(value);
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

    if (REF(hide))
        fail (Error(RE_MISC));

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

    if (ANY_ARRAY(v) && Is_Array_Deeply_Frozen(VAL_ARRAY(v)))
        return TRUE;

    if (ANY_CONTEXT(v) && Is_Context_Deeply_Frozen(VAL_CONTEXT(v)))
        return TRUE;

    if (ANY_SERIES(v) && Is_Series_Frozen(VAL_SERIES(v)))
        return TRUE;

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

    REBVAL *value = ARG(value);

    if (Is_Value_Immutable){
        *D_OUT = *value;
        return R_OUT;
    }

    if (!REF(clone))
        *D_OUT = *value;
    else {
        if (ANY_ARRAY(value)) {
            Val_Init_Array_Index(
                D_OUT,
                VAL_TYPE(value),
                Copy_Array_Deep_Managed(
                    VAL_ARRAY(value),
                    VAL_SPECIFIER(value)
                ),
                VAL_INDEX(value)
            );
        }
        else if (ANY_CONTEXT(value)) {
            const REBOOL deep = TRUE;
            const REBU64 types = TS_STD_SERIES;

            Val_Init_Context(
                D_OUT,
                VAL_TYPE(value),
                Copy_Context_Core(VAL_CONTEXT(value), deep, types)
            );
        }
        else if (ANY_SERIES(value)) {
            Val_Init_Series_Index(
                D_OUT,
                VAL_TYPE(value),
                Copy_Sequence(VAL_SERIES(value)),
                VAL_INDEX(value)
            );
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(value))); // not yet implemented
    }

    if (ANY_ARRAY(D_OUT))
        Deep_Freeze_Array(VAL_ARRAY(D_OUT));
    else if (ANY_CONTEXT(D_OUT))
        Deep_Freeze_Context(VAL_CONTEXT(D_OUT));
    else if (ANY_SERIES(D_OUT))
        Freeze_Sequence(VAL_SERIES(D_OUT));
    else
        fail (Error_Invalid_Type(VAL_TYPE(D_OUT))); // not yet implemented

    return R_OUT;
}
