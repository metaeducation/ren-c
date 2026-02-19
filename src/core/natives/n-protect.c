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
//  /const: pure native [
//
//  "Return value whose access level doesn't allow mutation to its content"
//
//      return: [element?]
//      value "Argument to change access to (can be locked or not)"
//          [element?]  ; !!! affect INTEGER! when it's a bignum?
//  ]
//
DECLARE_NATIVE(CONST)
{
    INCLUDE_PARAMS_OF_CONST;

    Element* v = ARG(VALUE);
    Set_Cell_Flag(v, CONST);

    return COPY_TO_OUT(v);
}


//
//  /const?: pure native [
//
//  "Return if a value is a read-only view of its underlying data"
//
//      return: [logic!]
//      value [element?]
//  ]
//
DECLARE_NATIVE(CONST_Q)
//
// !!! Should this integrate the question of if the series is immutable, vs.
// just if the value is *const*, specifically?  Knowing the flag is helpful
// for debugging at least.
{
    INCLUDE_PARAMS_OF_CONST_Q;

    if (not Any_Series(ARG(VALUE)))
        return fail ("CONST? only works on SERIES?");

    return LOGIC_OUT(Get_Cell_Flag(ARG(VALUE), CONST));
}


//
//  /mutable: impure native [
//
//  "Return reference with mutable access to its argument (if not LOCK-ed)"
//
//      return: [element?]
//      value "Argument to change access to (if such access can be granted)"
//          [element?]  ; !!! affect INTEGER! when it's a bignum?
//  ]
//
DECLARE_NATIVE(MUTABLE)
{
    INCLUDE_PARAMS_OF_MUTABLE;

    Element* v = ARG(VALUE);
    Clear_Cell_Flag(v, CONST);

    return COPY_TO_OUT(v);
}


//
//  /mutable?: pure native [
//
//  "Return if a value is a writable view of its underlying data"
//
//      return: [logic!]
//      value [any-series? any-context?]
//  ]
//
DECLARE_NATIVE(MUTABLE_Q) {
    INCLUDE_PARAMS_OF_MUTABLE_Q;

    // !!! Should this integrate the question of if the series is immutable,
    // besides just if the value is *const*, specifically?  Knowing the flag
    // is helpful for debugging at least.

    return LOGIC_OUT(Not_Cell_Flag(ARG(VALUE), CONST));
}


//
//  Protect_Slot: C
//
// Anything that calls this must call Uncolor_Slot() when done.
//
void Protect_Slot(const Cell* v, Flags flags)
{
    if (LIFT_BYTE(v) == BEDROCK_0)
        return;  // !!! PROTECT should reach aliases; but SETTERS? GETTERS?

    Option(Heart) heart = Heart_Of(v);

    if (Any_Series_Type(heart))
        Protect_Flex(Cell_Flex(v), Series_Index(v), flags);
    else if (heart == TYPE_MAP)
        Protect_Flex(MAP_PAIRLIST(VAL_MAP(v)), 0, flags);
    else if (Any_Context_Type(heart))
        Protect_Varlist(Cell_Varlist(v), flags);
}


//
//  Protect_Flex: C
//
// Anything that calls this must call Uncolor_Slot() when done.
//
void Protect_Flex(const Flex* f, Index index, Flags flags)
{
    if (Is_Stub_Black(f))
        return;  // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            if (flags & PROT_DEEP)
                Set_Flex_Info(f, FROZEN_DEEP);
            Set_Flex_Info(f, FROZEN_SHALLOW);
        }
        else
            Set_Flex_Info(f, PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        Clear_Flex_Info(f, PROTECTED);
    }

    if (not Stub_Holds_Cells(f) or not (flags & PROT_DEEP))
        return;

    Flip_Stub_To_Black(f);  // recursion protection

    const Slot* tail = Flex_Tail(Slot, f);
    const Slot* at = Flex_At(Slot, f, index);
    for (; at != tail; at++)
        Protect_Slot(at, flags);
}


//
//  Protect_Varlist: C
//
// Anything that calls this must call Uncolor_Slot() when done.
//
void Protect_Varlist(VarList* varlist, Flags flags)
{
    if (Is_Stub_Black(varlist))
        return; // avoid loop

    if (flags & PROT_SET) {
        if (flags & PROT_FREEZE) {
            if (flags & PROT_DEEP)
                Set_Flex_Info(Varlist_Array(varlist), FROZEN_DEEP);
            Set_Flex_Info(Varlist_Array(varlist), FROZEN_SHALLOW);
        }
        else
            Set_Flex_Info(Varlist_Array(varlist), PROTECTED);
    }
    else {
        assert(not (flags & PROT_FREEZE));
        Clear_Flex_Info(Varlist_Array(varlist), PROTECTED);
    }

    if (not (flags & PROT_DEEP))
        return;

    Flip_Stub_To_Black(varlist);  // for recursion

    const Slot* tail;
    Slot* at = Varlist_Slots(&tail, varlist);
    for (; at != tail; ++at)
        Protect_Slot(at, flags);
}


//
//  Protect_Unprotect_Core: C
//
// Common arguments between protect and unprotect:
//
static Bounce Protect_Unprotect_Core(Level* level_, Flags flags)
{
    INCLUDE_PARAMS_OF_PROTECT;

    USED(PARAM(HIDE)); // unused here, but processed in caller

    Stable* value = ARG(VALUE);
    assert(not Any_Word(value) and not Is_Tuple(value));

    // flags has PROT_SET bit (set or not)

    if (ARG(DEEP))
        flags |= PROT_DEEP;
    //if (ARG(WORDS))
    //  flags |= PROT_WORDS;

    if (Is_Block(value)) {
        Element* block = As_Element(value);

        if (ARG(WORDS))
            panic ("WORDS not currently implemented in PROTECT");

        if (ARG(VALUES)) {
            const Cell* slot;
            const Element* tail;
            const Element* item = List_At(&tail, block);

            DECLARE_STABLE (safe);

            for (; item != tail; ++item) {
                if (Is_Word(item)) {
                    panic ("WORDS! in VALUES needs work in PROTECT");
                }
                else if (Is_Path(item)) {
                    panic ("PATH! handling no longer in Protect_Unprotect");
                }
                else {
                    Copy_Cell(safe, item);
                    slot = safe;
                }

                Protect_Slot(slot, flags);
                if (flags & PROT_DEEP)
                    Uncolor_Slot(slot);
            }
            return COPY_TO_OUT(ARG(VALUE));
        }
    }

    if (flags & PROT_HIDE)
        panic (Error_Bad_Refines_Raw());

    Protect_Slot(value, flags);

    if (flags & PROT_DEEP)
        Uncolor_Slot(value);

    return COPY_TO_OUT(ARG(VALUE));
}


//
//  /protect: native [
//
//  "Protect a series or a variable from being modified"
//
//      return: [
//          any-word? tuple! any-series? bitset! map! object! module!
//      ]
//      value [
//          any-word? tuple! any-series? bitset! map! object! module!
//      ]
//      :deep "Protect all sub-series/objects as well"
//      :words "Process list as words (and path words)"
//      :values "Process list of values (implied GET)"
//      :hide "Hide variables (avoid binding and lookup)"
//  ]
//
DECLARE_NATIVE(PROTECT)
{
    INCLUDE_PARAMS_OF_PROTECT;

    Element* v = Element_ARG(VALUE);

    if (Any_Word(v) or Is_Tuple(v)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        if (ARG(HIDE))
            heeded (Init_Word(SCRATCH, CANON(HIDE)));
        else
            heeded (Init_Word(SCRATCH, CANON(PROTECT)));

        STATE = ST_TWEAK_TWEAKING;

        Option(Error*) e = Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
            v,
            NO_STEPS
        );
        if (e)
            panic (unwrap e);

        return COPY_TO_OUT(v);
    }

    // Core routine handles these via LEVEL
    //
    USED(PARAM(DEEP));
    USED(PARAM(WORDS));
    USED(PARAM(VALUES));

    Flags flags = PROT_SET;

    if (ARG(HIDE))
        flags |= PROT_HIDE;
    else
        flags |= PROT_WORD; // there is no unhide

    return Protect_Unprotect_Core(LEVEL, flags);
}


//
//  /unprotect: impure native [  ; can give non-FINAL access to FINAL variables
//
//  "Unprotect a series or a variable (it can again be modified)"
//
//      return: [word! tuple! any-series? bitset! map! object! module!]
//      value [word! tuple! any-series? bitset! map! object! module!]
//      :deep "Protect all sub-series as well"
//      :words "Block is a list of words"
//      :values "Process list of values (implied GET)"
//      :hide "HACK to make PROTECT and UNPROTECT have the same signature"
//  ]
//
DECLARE_NATIVE(UNPROTECT)
{
    INCLUDE_PARAMS_OF_UNPROTECT;

    // Core handles these via LEVEL
    //
    USED(PARAM(DEEP));
    USED(PARAM(WORDS));
    USED(PARAM(VALUES));

    if (ARG(HIDE))
        panic ("Cannot un-hide an object field once hidden");

    Element* v = Element_ARG(VALUE);

    if (Any_Word(v) or Is_Tuple(v)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        heeded (Init_Word(SCRATCH, CANON(UNPROTECT)));

        STATE = ST_TWEAK_TWEAKING;

        Option(Error*) e = Tweak_Var_With_Dual_Scratch_To_Spare_Use_Toplevel(
            v,
            NO_STEPS
        );
        if (e)
            panic (unwrap e);

        return COPY_TO_OUT(v);
    }

    return Protect_Unprotect_Core(level_, PROT_WORD);
}


//
//  Is_Value_Frozen_Deep: C
//
// "Frozen" is a stronger term here than "Immutable".  Mutable refers to the
// mutable/const distinction, where a value being immutable doesn't mean its
// series will never change in the future.  The frozen requirement is needed
// in order to do things like use blocks as map keys, etc.
//
bool Is_Value_Frozen_Deep(const Cell* v) {
    if (not Cell_Payload_1_Needs_Mark(v))
        return true;  // payloads that live in cell are already immutable

    Base* base = CELL_PAYLOAD_1(v);
    if (base == nullptr or Is_Base_A_Cell(base))
        return true;  // !!! Will all non-quoted Pairings be frozen?

    // Frozen deep should be set even on non-Arrays, e.g. all frozen shallow
    // Strings should also have FLEX_INFO_FROZEN_DEEP.
    //
    return Get_Flex_Info(u_cast(Flex*, base), FROZEN_DEEP);
}


//
//  /locked?: native [
//
//  "Determine if the value is locked (deeply and permanently immutable)"
//
//      return: [logic!]
//      value [any-stable?]
//  ]
//
DECLARE_NATIVE(LOCKED_Q)
{
    INCLUDE_PARAMS_OF_LOCKED_Q;

    return LOGIC_OUT(Is_Value_Frozen_Deep(ARG(VALUE)));
}


//
//  Force_Value_Frozen_Core: C
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
    const Stable* v,
    bool deep,
    Option(Flex*) locker
){
    if (Is_Value_Frozen_Deep(v))
        return;

    possibly(Is_Quoted(v) or Is_Quasiform(v));
    Heart heart = Heart_Of_Builtin(v);

    if (heart == TYPE_FRAME and Is_Frame_Details(v))
        return;  // special form, immutable

    if (Any_List_Type(heart)) {
        const Source* a = Cell_Array(v);
        if (deep) {
            if (not Is_Source_Frozen_Deep(a)) {
                Freeze_Source_Deep(a);
                if (locker)
                    Set_Flex_Info(a, AUTO_LOCKED);
            }
        }
        else {
            if (not Is_Source_Frozen_Shallow(a)) {
                Freeze_Source_Shallow(a);
                if (locker)
                    Set_Flex_Info(a, AUTO_LOCKED);
            }
        }
    }
    else if (Any_Context_Type(heart)) {
        VarList* c = Cell_Varlist(v);
        if (deep) {
            /*if (not Is_Context_Frozen_Deep(c)) {*/  // !!! review
                Deep_Freeze_Context(c);
                if (locker)
                    Set_Flex_Info(Varlist_Array(c), AUTO_LOCKED);
            /*}*/
        }
        else
            panic ("What does a shallow freeze of a context mean?");
    }
    else if (Any_Series_Type(heart)) {
        UNUSED(deep);

        const Flex* f = Cell_Flex(v);
        if (not Is_Flex_Frozen(f)) {
            Freeze_Flex(f);
            if (locker)
                Set_Flex_Info(f, AUTO_LOCKED);
        }
    }
    else if (Any_Sequence_Type(heart)) {
        // No freezing needed
    }
    else
        panic (Error_Invalid_Type(heart));  // not yet implemented
}


//
//  /freeze: native [
//
//  "Permanently lock values (if applicable) so they can be immutably shared"
//
//      return: [any-stable?]
//      value "Value to make permanently immutable"
//          [any-stable?]
//      :deep "Freeze deeply"
//  ;   :blame "What to report as source of lock in error"
//  ;       [any-series?]  ; not exposed for the moment
//  ]
//
DECLARE_NATIVE(FREEZE)
//
// 1. ARG(BLAME) is not exposed as a feature because there's nowhere to store
//    locking information in the Flex.  So the only thing that happens if
//    you pass in something other than null is FLEX_FLAG_AUTO_LOCKED is set
//    to deliver a message that the system locked something implicitly.  We
//    don't want to say that here, so hold off on the feature.
{
    INCLUDE_PARAMS_OF_FREEZE;

    Flex* locker = nullptr;
    Force_Value_Frozen_Core(ARG(VALUE), did ARG(DEEP), locker);

    return COPY_TO_OUT(ARG(VALUE));
}


//
//  /final: native [
//
//  "Make an immutable value that has CELL_FLAG_FINAL for a variable"
//
//      return: [any-value?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(FINAL)
{
    INCLUDE_PARAMS_OF_FINAL;

    Value* v = ARG(VALUE);
    Copy_Cell(OUT, v);

    Flex* locker = nullptr;
    bool deep = true;
    Element* lifted = Lift_Cell(v);
    Force_Value_Frozen_Core(lifted, deep, locker);

    Set_Cell_Flag(OUT, FINAL);
    return OUT;
}


//
//  /nonfinal: native:intrinsic [  ; not necessarily IMPURE
//
//  "Make an mutable value that does not have CELL_FLAG_FINAL for a variable"
//
//      return: [any-value?]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(NONFINAL)  // "VARIES" was considered, but this is clearer
{
    INCLUDE_PARAMS_OF_NONFINAL;

    Value* v = ARG(VALUE);
    Copy_Cell(OUT, v);
    Clear_Cell_Flag(OUT, FINAL);  // still immutable, but variable can change

    return OUT;
}
