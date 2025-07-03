//
//  file: %stub-varlist.h
//  summary: "Extremely Simple Symbol/Value Array AFTER %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Once a word is bound to a context the index is treated as permanent.
//   This is why objects are "append only"...because disruption of the index
//   numbers would break the extant words with index numbers to that position.
//   (Appending to keylists involves making a copy if it is shared.)
//
// * Since varlists and keylists always have more than one element, they are
//   allocated with STUB_FLAG_DYNAMIC and do not need to check for whether
//   the singular optimization when being used.  This does not apply when a
//   varlist becomes invalid (e.g. via FREE), when its data allocation is
//   released and it is diminished to a singular.
//

#if NO_RUNTIME_CHECKS
    #define Assert_Varlist(c) NOOP
#else
    #define Assert_Varlist(c) Assert_Varlist_Core(c)
#endif


// VarList* properties (note: shares BONUS_KEYSOURCE() with Phase*)
//
// Note: MODULE! contexts depend on a property stored in the META field, which
// is another object's-worth of data *about* the module's contents (e.g. the
// processed header)
//
INLINE Option(VarList*) Misc_Varlist_Adjunct(VarList* varlist) {
    return cast(VarList*, MISC_VARLIST_ADJUNCT(varlist));
}

INLINE void Tweak_Misc_Varlist_Adjunct(
    Stub* varlist,
    Option(VarList*) adjunct
){
    assert(Is_Stub_Varlist(varlist));
    MISC_VARLIST_ADJUNCT(varlist) = maybe adjunct;
    if (adjunct)
        Set_Stub_Flag(varlist, MISC_NEEDS_MARK);
    else
        Clear_Stub_Flag(varlist, MISC_NEEDS_MARK);
}

INLINE void Tweak_Misc_Phase_Adjunct(Phase* a, Option(VarList*) adjunct) {
    if (Is_Stub_Details(a))
        Tweak_Misc_Details_Adjunct(cast(Details*, a), adjunct);
    else
        Tweak_Misc_Varlist_Adjunct(cast(ParamList*, a), adjunct);
}

INLINE Option(VarList*) Misc_Phase_Adjunct(Phase* a) {
    if (Is_Stub_Details(a))
        return Misc_Details_Adjunct(cast(Details*, a));
    return Misc_Varlist_Adjunct(cast(ParamList*, a));
}



#define CELL_CONTEXT_VARLIST(c)  CELL_PAYLOAD_1(c)


//=//// CONTEXT ARCHETYPE VALUE CELL (ROOTVAR)  ///////////////////////////=//
//
// A Value* must contain enough information to find what is needed to define
// a context.  That fact is leveraged by the notion of keeping the information
// in the context itself as the [0] element of the varlist.  This means it is
// always on hand when a Value* is needed, so you can do things like:
//
//     VarList* c = ...;
//     rebElide("print [pick", Varlist_Archetype(c), "'field]");
//
// The archetype stores the varlist, and since it has a value header it also
// encodes which specific type of context [OBJECT! FRAME! MODULE! ...] the
// context represents.
//
// In the case of a FRAME!, the archetype also stores an ACTION! pointer that
// represents the action the frame is for.  Since this information can be
// found in the archetype, non-archetype cells can use the cell slot for
// purposes other than storing the archetype action (see PHASE v LABEL section)
//
// Note: Other context types could use the slots for binding and phase for
// other purposes.  For instance, MODULE! could store its header information.
// For the moment that is done with the Misc_Varlist_Adjunct() field instead.
//

#if DEBUG_CELL_READ_WRITE
    INLINE void Protect_Rootvar_If_Debug(Cell* rootvar) {
        assert(Not_Cell_Flag(rootvar, PROTECTED));
        Set_Cell_Flag(rootvar, PROTECTED);
    }

    INLINE void Unprotect_Rootvar_If_Debug(Cell* rootvar) {
        assert(Get_Cell_Flag(rootvar, PROTECTED));
        Clear_Cell_Flag(rootvar, PROTECTED);
    }
#else
    #define Protect_Rootvar_If_Debug(rootvar)    NOOP
    #define Unprotect_Rootvar_If_Debug(rootvar)  NOOP
#endif


INLINE Element* Rootvar_Of_Varlist(VarList* c)  // mutable archetype access
  { return m_cast(Element*, Varlist_Archetype(c)); }  // inline checks mutable


//=//// FRAME COUPLING ////////////////////////////////////////////////////=//
//
// A FRAME! cell can store a context as a "coupling".  RETURN here would store
// the action that the return will return from.  A METHOD will store the
// object that the method is linked to.  Since it is per-cell, the same
// archetypal action can be specialized to many different targets.
//
// Note: The presence of targets in non-archetype values makes it possible
// for FRAME! values that have phases to carry the binding of that phase.
// This is a largely unexplored feature, but is used in REDO scenarios where
// a running frame gets re-executed.  More study is needed.
//
// 1. The way that a FRAME! cell made by METHOD gets connected with an object
//    is when the TUPLE! dispatch happens.  (foo: method [...]) is uncoupled
//    until the moment that you say (obj.foo), at which point the returned
//    action gets OBJ's pointer poked into the result.  But not all functions
//    have this happen: it would stow arbitrary unintentional data in
//    non-methods just because they were accessed from an object--and worse,
//    it would create contention where meanings of member words as `.member`
//    would be looked up in helper functions.  So only intentionally
//    uncoupled functions--not functions with mere couplings of NULL--are
//    processed by TUPLE! to embed the pointer.

#define UNCOUPLED  g_empty_varlist  // instruct TUPLE! processing to couple [1]

#define NONMETHOD  u_cast(Option(VarList*), nullptr)  // nonmethods not coupled

INLINE Option(VarList*) Cell_Frame_Coupling(const Value* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    return cast(VarList*, CELL_FRAME_PAYLOAD_2_COUPLING(c));
}

INLINE void Tweak_Frame_Coupling(Value* c, Option(VarList*) coupling) {
    assert(Heart_Of(c) == TYPE_FRAME);
    CELL_FRAME_PAYLOAD_2_COUPLING(c) = maybe coupling;
    if (coupling)
        Clear_Cell_Flag(c, DONT_MARK_PAYLOAD_2);
    else
        Set_Cell_Flag(c, DONT_MARK_PAYLOAD_2);
}


INLINE void Tweak_Non_Frame_Varlist_Rootvar_Untracked(
    Array* varlist,
    Heart heart
){
    assert(heart != TYPE_FRAME);  // use Tweak_Frame_Varlist_Rootvar() instead
    Sink(Element) rootvar = Array_Head(varlist);
    Reset_Cell_Header_Noquote(
        rootvar,
        FLAG_HEART(heart)
            | CELL_MASK_ANY_CONTEXT
            | CELL_FLAG_PROTECTED  // should not be modified
    );
    CELL_CONTEXT_VARLIST(rootvar) = varlist;
    CELL_FRAME_PAYLOAD_2_COUPLING(rootvar) = nullptr;  // not a frame
    CELL_FRAME_EXTRA_LENS_OR_LABEL(rootvar) = nullptr;  // not a frame
}

#define Tweak_Non_Frame_Varlist_Rootvar(heart,varlist) \
    Tweak_Non_Frame_Varlist_Rootvar_Untracked((heart), (varlist))


//=//// CONTEXT KEYLISTS //////////////////////////////////////////////////=//
//
// Context types use this field of their varlist (which is the identity of
// an ANY-CONTEXT?) to find their "keylist".
//
// Note: Due to the sharing of keylists, features like whether a value in a
// context is hidden or protected are accomplished using special bits on the
// var cells, and *not the keys*.  These bits are not copied when the value
// is moved (see CELL_MASK_COPY regarding this mechanic)
//
// Note: At one time Level->varlist would swap in a Level* in this spot, in
// order to be able to find a running Level* from a VarList.  This was due to
// the belief that the Stub.misc field could not be sacrificed on FRAME! to
// store that Level*, because it was needed to store a link to the "adjunct
// object" which all VarList* wanted to offer.  It turns out that adjunct
// objects are not needed on running frame varlists, they can be on the phase.
//

INLINE KeyList* Bonus_Keylist(VarList* c) {
    assert(Is_Stub_Varlist(c));
    return cast(KeyList*, BONUS_VARLIST_KEYLIST(c));
}

INLINE void Tweak_Bonus_Keylist_Shared(Flex* f, KeyList* keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    Set_Flavor_Flag(KEYLIST, keylist, SHARED);
    BONUS_VARLIST_KEYLIST(f) = keylist;
}

INLINE void Tweak_Bonus_Keylist_Unique(Flex* f, KeyList *keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    assert(Not_Flavor_Flag(KEYLIST, keylist, SHARED));
    BONUS_VARLIST_KEYLIST(f) = keylist;
}


//=//// VarList* ACCESSORS /////////////////////////////////////////////////=//
//
// These are access functions that should be used when what you have in your
// hand is just a VarList*.  THIS DOES NOT ACCOUNT FOR PHASE...so there can
// actually be a difference between these two expressions for FRAME!s:
//
//     Value* x = VAL_CONTEXT_KEYS_HEAD(context);  // accounts for phase
//     Value* y = Varlist_Keys_Head(Cell_Varlist(context), n);  // no phase
//
// Context's "length" does not count the [0] cell of either the varlist or
// the keylist arrays.  Hence it must subtract 1.  STUB_MASK_VARLIST
// includes STUB_FLAG_DYNAMIC, so a dyamic Array can be assumed so long
// as it is valid.
//

INLINE REBLEN Varlist_Len(VarList* c) {
    assert(Is_Stub_Varlist(c));
    return c->content.dynamic.used - 1;  // -1 for archetype
}

INLINE const Key* Varlist_Key(VarList* c, Index n) {  // 1-based
    assert(n != 0 and n <= Varlist_Len(c));
    return Flex_At(const Key, Bonus_Keylist(c), n - 1);
}

INLINE Slot* Varlist_Slot(VarList* c, Index n) {  // 1-based
    assert(n != 0 and n <= Varlist_Len(c));
    return Flex_Head_Dynamic(Slot, c) + n;
}

INLINE Fixed(Slot*) Varlist_Fixed_Slot(VarList* c, Index n) {  // 1-based
    assert(Get_Flex_Flag(c, FIXED_SIZE));  // not movable, see #2274
    return Varlist_Slot(c, n);
}


//=//// TRANSITIONAL HACK FOR SLOT=>VALUE //////////////////////////////////=//
//
// This is a temporary workaround.  Ultimately slots should only be converted
// to Value* directly in a narrow set of cases, when dual representation is
// not a possibility.

INLINE Init(Slot) Slot_Init_Hack(Slot* slot) {
    return u_cast(Init(Slot), slot);
}

MUTABLE_IF_C(Value*, INLINE) Slot_Hack(
    CONST_IF_C(Slot*) slot
){
    CONSTABLE(Slot*) s = m_cast(Slot*, slot);
    assert(LIFT_BYTE(s) != DUAL_0);
    return u_cast(Value*, s);
}


// Varlist_Slots_Head() and Varlist_Keys_Head() allow Varlist_Len() to be 0,
// Varlist_Slot() does not.  Also, Varlist_Keys_Head() gives a mutable slot.

#define Varlist_Keys_Head(c) \
    Flex_At(Key, Bonus_Keylist(c), 0)  // 0-based

#define Varlist_Slots_Head(c) \
    (Flex_Head_Dynamic(Slot, c) + 1)

INLINE const Key* Varlist_Keys(Sink(const Key*) tail, VarList* c) {
    KeyList* keylist = Bonus_Keylist(c);
    *tail = Flex_Tail(Key, keylist);
    return Flex_Head(Key, keylist);
}

INLINE Slot* Varlist_Slots(Sink(const Slot*) tail, VarList* v) {
    Slot* head = Varlist_Slots_Head(v);
    *tail = head + v->content.dynamic.used - 1;
    return head;
}

INLINE Fixed(Slot*) Varlist_Fixed_Slots(Sink(const Slot*) tail, VarList* v) {
    assert(Get_Flex_Flag(v, FIXED_SIZE));  // not movable, see #2274
    return Varlist_Slots(tail, v);
}


//=//// FRAME! VarList* <-> Level* STRUCTURE //////////////////////////////=//
//
// The Stub.misc field of frames which can be tied to levels can be a Level*,
// instead of an "adjunct" object.
//

INLINE Option(Level*) Misc_Runlevel(Stub* varlist) {
    assert(Is_Stub_Varlist(varlist));
    assert(CTX_TYPE(varlist) == TYPE_FRAME);
    assert(Not_Stub_Flag(varlist, MISC_NEEDS_MARK));
    return cast(Level*, MISC_VARLIST_RUNLEVEL(varlist));
}

INLINE void Tweak_Misc_Runlevel(Stub* varlist, Option(Level*) L) {
    assert(Is_Stub_Varlist(varlist));
    possibly(CTX_TYPE(varlist) == TYPE_FRAME);  // may not be fully formed yet
    MISC_VARLIST_RUNLEVEL(varlist) = maybe L;
    assert(Not_Stub_Flag(varlist, MISC_NEEDS_MARK));

}

INLINE Level* Level_Of_Varlist_If_Running(VarList* varlist) {
    assert(Is_Frame(Varlist_Archetype(varlist)));
    if (Get_Stub_Flag(varlist, MISC_NEEDS_MARK))
        return nullptr;  // Stub.misc is Misc_Varlist_Adjunct(), not Level*

    Level* L = maybe Misc_Runlevel(varlist);
    if (not L)
        return nullptr;

    assert(L->executor == &Action_Executor);
    return L;
}

#define Is_Frame_On_Stack(varlist) \
    (Level_Of_Varlist_If_Running(varlist) != nullptr)


INLINE Level* Level_Of_Varlist_May_Panic(VarList* c) {
    Level* L = Level_Of_Varlist_If_Running(c);
    if (not L)
        abrupt_panic (Error_Frame_Not_On_Stack_Raw());
    return L;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// COMMON INLINES (macro-like)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// By putting these functions in a header file, they can be inlined by the
// compiler, rather than add an extra layer of function call.
//

#define Copy_Varlist_Shallow_Managed(src) \
    Copy_Varlist_Extra_Managed((src), 0, 0)

// Useful if you want to start a context out as BASE_FLAG_MANAGED so it does
// not have to go in the unmanaged roots list and be removed later.  (Be
// careful not to do any evaluations or trigger GC until it's well formed)
//
#define Alloc_Varlist(kind,capacity) \
    Alloc_Varlist_Core(FLEX_FLAGS_NONE, (kind), (capacity))


//=////////////////////////////////////////////////////////////////////////=//
//
// LOCKING
//
//=////////////////////////////////////////////////////////////////////////=//

INLINE void Deep_Freeze_Context(VarList* c) {
    Protect_Varlist(
        c,
        PROT_SET | PROT_DEEP | PROT_FREEZE
    );
    Uncolor_Array(Varlist_Array(c));
}

#define Is_Context_Frozen_Deep(c) \
    Is_Source_Frozen_Deep(Varlist_Array(c))
