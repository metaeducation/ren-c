//
//  file: %n-protect.c
//  summary: "native functions for series and object field protection"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
static void Protect_Key(VarList* context, REBLEN index, Flags flags)
{
    Value* var = Varlist_Slot(context, index);

    // Due to the fact that not all the bits in a value header are copied when
    // Copy_Cell is done, it's possible to set the protection status of a
    // variable on the value vs. the key.  This means the keylist does not
    // have to be modified, and hence it doesn't have to be made unique
    // from any objects that were sharing it.
    //
    if (flags & PROT_WORD) {
        if (flags & PROT_SET)
            Set_Cell_Flag(var, PROTECTED);
        else
            Clear_Cell_Flag(var, PROTECTED);
    }

    if (flags & PROT_HIDE) {
        //
        // !!! For the moment, hiding is still implemented via typeset flags.
        // Since PROTECT/HIDE is something of an esoteric feature, keep it
        // that way for now, even though it means the keylist has to be
        // made unique.
        //
        Ensure_Keylist_Unique_Invalidated(context);

        Value* key = Varlist_Key(context, index);

        if (flags & PROT_SET) {
            Set_Typeset_Flag(key, TYPE_TS_HIDDEN);
            Set_Typeset_Flag(key, TYPE_TS_UNBINDABLE);
        }
        else {
            Clear_Typeset_Flag(key, TYPE_TS_HIDDEN);
            Clear_Typeset_Flag(key, TYPE_TS_UNBINDABLE);
        }
    }
}


//
//  Protect_Value: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Value(Cell* v, Flags flags)
{
    if (Any_Series(v))
        Protect_Flex(Cell_Flex(v), VAL_INDEX(v), flags);
    else if (Is_Map(v))
        Protect_Flex(MAP_PAIRLIST(VAL_MAP(v)), 0, flags);
    else if (Any_Context(v))
        Protect_Context(Cell_Varlist(v), flags);
}


//
//  Protect_Flex: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Flex(Flex* s, REBLEN index, Flags flags)
{
    if (Is_Flex_Black(s))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            assert(flags & PROT_DEEP);
            Set_Flex_Info(s, FROZEN_DEEP);
        }
        else
            Set_Flex_Info(s, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        Clear_Flex_Info(s, PROTECTED);
    }

    if (not Is_Flex_Array(s) or not (flags & PROT_DEEP))
        return;

    Flip_Flex_To_Black(s); // recursion protection

    Cell* val = Array_At(cast_Array(s), index);
    for (; NOT_END(val); val++)
        Protect_Value(val, flags);
}


//
//  Protect_Context: C
//
// Anything that calls this must call Uncolor() when done.
//
void Protect_Context(VarList* c, Flags flags)
{
    if (Is_Flex_Black(Varlist_Array(c)))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            assert(flags & PROT_DEEP);
            Set_Flex_Info(c, FROZEN_DEEP);
        }
        else
            Set_Flex_Info(c, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        Clear_Flex_Info(Varlist_Array(c), PROTECTED);
    }

    if (not (flags & PROT_DEEP))
        return;

    Flip_Flex_To_Black(Varlist_Array(c));  // for recursion

    Value* var = Varlist_Slots_Head(c);
    for (; NOT_END(var); ++var)
        Protect_Value(var, flags);
}


//
//  Protect_Word_Value: C
//
static void Protect_Word_Value(Value* word, Flags flags)
{
    if (Any_Word(word) and IS_WORD_BOUND(word)) {
        Protect_Key(VAL_WORD_CONTEXT(word), VAL_WORD_INDEX(word), flags);
        if (flags & PROT_DEEP) {
            //
            // Ignore existing mutability state so that it may be modified.
            // Most routines should NOT do this!
            //
            Value* var = m_cast(
                Value*,
                Get_Opt_Var_May_Panic(word, SPECIFIED)
            );
            Protect_Value(var, flags);
            Uncolor(var);
        }
    }
    else if (Any_Path(word)) {
        REBLEN index;
        VarList* context = Resolve_Path(word, &index);
        if (index == 0)
            panic ("Couldn't resolve PATH! in Protect_Word_Value");

        if (context != nullptr) {
            Protect_Key(context, index, flags);
            if (flags & PROT_DEEP) {
                Value* var = Varlist_Slot(context, index);
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
static Bounce Protect_Unprotect_Core(Level* level_, Flags flags)
{
    INCLUDE_PARAMS_OF_PROTECT;

    UNUSED(PARAM(HIDE)); // unused here, but processed in caller

    Value* value = ARG(VALUE);

    // flags has PROT_SET bit (set or not)

    if (Bool_ARG(DEEP))
        flags |= PROT_DEEP;
    //if (Bool_ARG(WORDS))
    //  flags |= PROT_WORDS;

    if (Is_Word(value) || Is_Path(value)) {
        Protect_Word_Value(value, flags); // will unmark if deep
        RETURN (ARG(VALUE));
    }

    if (Is_Block(value)) {
        if (Bool_ARG(WORDS)) {
            Cell* val;
            for (val = List_At(value); NOT_END(val); val++) {
                DECLARE_VALUE (word); // need binding, can't pass Cell
                Derelativize(word, val, VAL_SPECIFIER(value));
                Protect_Word_Value(word, flags);  // will unmark if deep
            }
            RETURN (ARG(VALUE));
        }
        if (Bool_ARG(VALUES)) {
            Value* var;
            Cell* item;

            DECLARE_VALUE (safe);

            for (item = List_At(value); NOT_END(item); ++item) {
                if (Is_Word(item)) {
                    //
                    // Since we *are* PROTECT we allow ourselves to get mutable
                    // references to even protected values to protect them.
                    //
                    var = m_cast(
                        Value*,
                        Get_Opt_Var_May_Panic(item, VAL_SPECIFIER(value))
                    );
                }
                else if (Is_Path(value)) {
                    Get_Path_Core(safe, value, SPECIFIED);
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
            RETURN (ARG(VALUE));
        }
    }

    if (flags & PROT_HIDE)
        panic (Error_Bad_Refines_Raw());

    Protect_Value(value, flags);

    if (flags & PROT_DEEP)
        Uncolor(value);

    RETURN (ARG(VALUE));
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
DECLARE_NATIVE(PROTECT)
{
    INCLUDE_PARAMS_OF_PROTECT;

    // Avoid unused parameter warnings (core routine handles them via frame)
    //
    UNUSED(PARAM(VALUE));
    UNUSED(PARAM(DEEP));
    UNUSED(PARAM(WORDS));
    UNUSED(PARAM(VALUES));

    Flags flags = PROT_SET;

    if (Bool_ARG(HIDE))
        flags |= PROT_HIDE;
    else
        flags |= PROT_WORD; // there is no unhide

    return Protect_Unprotect_Core(level_, flags);
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
DECLARE_NATIVE(UNPROTECT)
{
    INCLUDE_PARAMS_OF_UNPROTECT;

    // Avoid unused parameter warnings (core handles them via frame)
    //
    UNUSED(PARAM(VALUE));
    UNUSED(PARAM(DEEP));
    UNUSED(PARAM(WORDS));
    UNUSED(PARAM(VALUES));

    if (Bool_ARG(HIDE))
        panic ("Cannot un-hide an object field once hidden");

    return Protect_Unprotect_Core(level_, PROT_WORD);
}


//
//  Is_Value_Immutable: C
//
bool Is_Value_Immutable(const Cell* v) {
    if (
        Is_Blank(v)
        || Any_Scalar(v)
        || Any_Word(v)
        || Is_Action(v) // paramlist is identity, hash
    ){
        return true;
    }

    if (Any_List(v))
        return Is_Array_Deeply_Frozen(Cell_Array(v));

    if (Any_Context(v))
        return Is_Context_Deeply_Frozen(Cell_Varlist(v));

    if (Any_Series(v))
        return Is_Flex_Frozen(Cell_Flex(v));

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
DECLARE_NATIVE(LOCKED_Q)
{
    INCLUDE_PARAMS_OF_LOCKED_Q;

    return Init_Logic(OUT, Is_Value_Immutable(ARG(VALUE)));
}


//
//  Force_Value_Frozen_Deep: C
//
// !!! The concept behind `opt_locker` is that it might be able to give the
// user more information about why data would be automatically locked, e.g.
// if locked for reason of using as a map key...for instance.  It could save
// the map, or the file and line information for the interpreter at that
// moment, etc.  Just put a flag at the top level for now, since that is
// "better than nothing", and revisit later in the design.
//
void Force_Value_Frozen_Deep(const Cell* v, Flex* opt_locker) {
    if (Is_Value_Immutable(v))
        return;

    if (Any_List(v)) {
        Deep_Freeze_Array(Cell_Array(v));
        if (opt_locker)
            Set_Flex_Info(Cell_Array(v), AUTO_LOCKED);
    }
    else if (Any_Context(v)) {
        Deep_Freeze_Context(Cell_Varlist(v));
        if (opt_locker)
            Set_Flex_Info(Cell_Varlist(v), AUTO_LOCKED);
    }
    else if (Any_Series(v)) {
        Freeze_Non_Array_Flex(Cell_Flex(v));
        if (opt_locker != nullptr)
            Set_Flex_Info(Cell_Flex(v), AUTO_LOCKED);
    } else
        panic (Error_Invalid_Type(Type_Of(v))); // not yet implemented
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
DECLARE_NATIVE(LOCK)
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
// garbage collector because multiple Stubs would be referring into the same
// data.  So that's a possibility.
{
    INCLUDE_PARAMS_OF_LOCK;

    Value* v = ARG(VALUE);

    if (!Bool_ARG(CLONE))
        Copy_Cell(OUT, v);
    else {
        if (Any_List(v)) {
            Init_Any_List_At(
                OUT,
                Type_Of(v),
                Copy_Array_Deep_Managed(
                    Cell_Array(v),
                    VAL_SPECIFIER(v)
                ),
                VAL_INDEX(v)
            );
        }
        else if (Any_Context(v)) {
            Init_Any_Context(
                OUT,
                Type_Of(v),
                Copy_Context_Core_Managed(Cell_Varlist(v), TS_STD_SERIES)
            );
        }
        else if (Any_Series(v)) {
            Init_Any_Series_At(
                OUT,
                Type_Of(v),
                Copy_Non_Array_Flex_Core(Cell_Flex(v), NODE_FLAG_MANAGED),
                VAL_INDEX(v)
            );
        }
        else
            panic (Error_Invalid_Type(Type_Of(v))); // not yet implemented
    }

    Flex* locker = nullptr;
    Force_Value_Frozen_Deep(OUT, locker);

    return OUT;
}
