//
//  File: %stub-context.h
//  Summary: "Context definitions AFTER including %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//   released and it is decayed to a singular.
//


#if NO_RUNTIME_CHECKS
    #define Assert_Varlist(c) NOOP
#else
    #define Assert_Varlist(c) Assert_Varlist_Core(c)
#endif


//=//// INHERITED BINDING LINK ////////////////////////////////////////////=//
//
// All Context* subtypes use their Stub.link.node field to point to the next
// context in their inheritance chain.  So a Stub representing a Let might
// point to a VarList for a FRAME! which might in turn point to a SeaOfVars
// for a MODULE!.  This is how "Virtual Binding" works.
//

INLINE Option(Context*) Link_Inherit_Bind(Context* context)
  { return u_cast(Context*, m_cast(Node*, context->link.node)); }

INLINE void Tweak_Link_Inherit_Bind(Context* context, Option(Context*) next)
  { context->link.node = maybe next; }

INLINE void Add_Link_Inherit_Bind(Context* context, Option(Context*) next) {
    assert(context->link.node == nullptr);
    context->link.node = maybe next;
}


//=//// KEYLIST_FLAG_SHARED ///////////////////////////////////////////////=//
//
// This is indicated on the keylist array of a context when that same array
// is the keylist for another object.  If this flag is set, then modifying an
// object using that keylist (such as by adding a key/value pair) will require
// that object to make its own copy.
//
// Note: This flag did not exist in R3-Alpha, so all expansions would copy--
// even if expanding the same object by 1 item 100 times with no sharing of
// the keylist.  That would make 100 copies of an arbitrary long keylist that
// the GC would have to clean up.
//
#define KEYLIST_FLAG_SHARED \
    STUB_SUBCLASS_FLAG_24


// VarList* properties (note: shares BONUS_KEYSOURCE() with Phase*)
//
// Note: MODULE! contexts depend on a property stored in the META field, which
// is another object's-worth of data *about* the module's contents (e.g. the
// processed header)
//
INLINE Option(VarList*) Misc_Varlist_Adjunct(VarList* varlist) {
    return cast(VarList*, m_cast(Node*, Varlist_Array(varlist)->misc.node));
}

INLINE void Tweak_Misc_Varlist_Adjunct(
    Stub* varlist,
    Option(VarList*) adjunct
){
    assert(Is_Stub_Varlist(varlist));
    varlist->misc.node = maybe adjunct;
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



#define Tweak_Cell_Context_Varlist            Tweak_Cell_Node1


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


INLINE const Element* Varlist_Archetype(VarList* c) {  // read-only form
    return Flex_Head_Dynamic(Element, c);
}

INLINE Heart CTX_TYPE(Context* c) {
    return Cell_Heart(Varlist_Archetype(cast(VarList*, c)));
}

INLINE Element* Rootvar_Of_Varlist(VarList* c)  // mutable archetype access
  { return m_cast(Element*, Varlist_Archetype(c)); }  // inline checks mutability


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
//    is when the TUPLE! dispatch happens.  `/foo: method [...]` is uncoupled
//    until the moment that you say `obj.foo`, at which point the returned
//    action gets OBJ's pointer poked into the result.  But not all functions
//    have this happen: it would stow arbitrary unintentional data in
//    non-methods just because they were accessed from an object--and worse,
//    it would create contention where meanings of member words as `.member`
//    would be looked up in helper functions.  So only intentionally
//    uncoupled functions--not functions with mere couplings of NULL--are
//    processed by TUPLE! to embed the pointer.

#define UNCOUPLED  g_empty_varlist  // instruct TUPLE! processing to couple [1]

#define NONMETHOD  nullptr  // non-methods aren't coupled

INLINE Option(VarList*) Cell_Frame_Coupling(const Cell* c) {
    assert(Cell_Heart(c) == REB_FRAME);
    return cast(VarList*, m_cast(Node*, c->extra.node));
}

INLINE void Tweak_Cell_Frame_Coupling(Cell* c, Option(VarList*) coupling) {
    assert(Cell_Heart(c) == REB_FRAME);
    c->extra.node = maybe coupling;
}


INLINE void Tweak_Non_Frame_Varlist_Rootvar_Untracked(
    Array* varlist,
    Heart heart
){
    assert(heart != REB_FRAME);  // use Tweak_Frame_Varlist_Rootvar() instead
    Cell* rootvar = Array_Head(varlist);
    Reset_Cell_Header_Noquote(
        rootvar,
        FLAG_HEART_BYTE(heart)
            | CELL_MASK_ANY_CONTEXT
            | CELL_FLAG_PROTECTED  // should not be modified
    );
    Tweak_Cell_Context_Varlist(rootvar, varlist);
    rootvar->extra.node = nullptr;  // no coupling, but extra is marked
    Tweak_Cell_Frame_Lens_Or_Label(rootvar, nullptr);  // not a frame
}

#define Tweak_Non_Frame_Varlist_Rootvar(heart,varlist) \
    Tweak_Non_Frame_Varlist_Rootvar_Untracked((heart), (varlist))


//=//// CONTEXT KEYLISTS //////////////////////////////////////////////////=//
//
// If a context represents a FRAME! that is currently executing, one often
// needs to quickly navigate to the Level* structure for the corresponding
// stack level.  This is sped up by swapping the Level* into the LINK() of
// the varlist until the frame is finished.  In this state, the paramlist of
// the FRAME! action is consulted. When the action is finished, this is put
// back in BONUS_KEYSOURCE().
//
// Note: Due to the sharing of keylists, features like whether a value in a
// context is hidden or protected are accomplished using special bits on the
// var cells, and *not the keys*.  These bits are not copied when the value
// is moved (see CELL_MASK_COPY regarding this mechanic)
//

INLINE KeyList* Keylist_Of_Varlist(VarList* c) {
    assert(CTX_TYPE(c) != REB_MODULE);
    return BONUS(KeyList, c);
}

INLINE void Tweak_Keylist_Of_Varlist_Shared(Flex* f, KeyList* keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    Set_Flavor_Flag(KEYLIST, keylist, SHARED);
    BONUS(KeyList, f) = keylist;
}

INLINE void Tweak_Keylist_Of_Varlist_Unique(Flex* f, KeyList *keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    assert(Not_Flavor_Flag(KEYLIST, keylist, SHARED));
    BONUS(KeyList, f) = keylist;
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
// the keylist arrays.  Hence it must subtract 1.  FLEX_MASK_VARLIST
// includes STUB_FLAG_DYNAMIC, so a dyamic Array can be assumed so long
// as it is valid.
//

INLINE REBLEN Varlist_Len(VarList* c) {
    assert(CTX_TYPE(c) != REB_MODULE);
    return c->content.dynamic.used - 1;  // -1 for archetype
}

INLINE const Key* Varlist_Key(VarList* c, Index n) {  // 1-based
    assert(n != 0 and n <= Varlist_Len(c));
    return Flex_At(const Key, Keylist_Of_Varlist(c), n - 1);
}

INLINE Value* Varlist_Slot(VarList* c, Index n) {  // 1-based
    assert(n != 0 and n <= Varlist_Len(c));
    return Flex_Head_Dynamic(Value, c) + n;
}

INLINE Value* Mutable_Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(Not_Cell_Flag(slot, PROTECTED));
    return slot;
}

INLINE const Value* Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&g_lib_patches[id]));
    assert(not Is_Nothing(slot));
    return slot;
}

INLINE Sink(Value) Sink_Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);
    return cast(Value*, Stub_Cell(&g_lib_patches[id]));
}

#define LIB(name)  Lib_Var(SYM_##name)


#define SYS_UTIL(name) \
    cast(const Value*, MOD_VAR(g_sys_util_context, Canon_Symbol(SYM_##name), true))

// Optimization for g_lib_context for datatypes + natives + generics; usage is
// tailored in order for SYM_XXX constants to beeline for the storage.  The
// entries were all allocated during Startup_Lib().
//
// Note: Call LIB() macro directly if you have a SYM in hand vs. a canon.
//
// 1. !!! We need to consider the strictness here, with case sensitive binding
//    we can't be sure it's a match.  :-/  For this moment hope lib doesn't
//    have two-cased variations of anything.
//
INLINE Option(Stub*) MOD_PATCH(SeaOfVars* sea, const Symbol* sym, bool strict) {
    if (sea == g_lib_context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id != 0 and id < LIB_SYMS_MAX) {
            if (INODE(PatchContext, &g_lib_patches[id]) == nullptr)  // [1]
                return nullptr;

            return &g_lib_patches[id];
        }
    }

    Symbol* synonym = m_cast(Symbol*, sym);
    do {
        Stub* patch = Misc_Hitch(sym);
        if (Get_Flavor_Flag(SYMBOL, sym, MISC_IS_BINDINFO))
            patch = Misc_Hitch(patch);  // skip bindinfo

        for (; patch != sym; patch = Misc_Hitch(patch)) {
            if (INODE(PatchContext, patch) == sea)
                return patch;
        }
        if (strict)
            return nullptr;
        sym = LINK(Synonym, sym);
    } while (synonym != sym);

    return nullptr;
}

INLINE Value* MOD_VAR(SeaOfVars* sea, const Symbol* sym, bool strict) {
    Stub* patch = maybe MOD_PATCH(sea, sym, strict);
    if (not patch)
        return nullptr;
    return cast(Value*, Stub_Cell(patch));
}


// Varlist_Slots_Head() and Varlist_Keys_Head() allow Varlist_Len() to be 0,
// Varlist_Slot() does not.  Also, Varlist_Keys_Head() gives a mutable slot.

#define Varlist_Keys_Head(c) \
    Flex_At(Key, Keylist_Of_Varlist(c), 0)  // 0-based

#define Varlist_Slots_Head(c) \
    (Flex_Head_Dynamic(Value, c) + 1)

INLINE const Key* Varlist_Keys(Sink(const Key*) tail, VarList* c) {
    KeyList* keylist = Keylist_Of_Varlist(c);
    *tail = Flex_Tail(Key, keylist);
    return Flex_Head(Key, keylist);
}

INLINE Value* Varlist_Slots(Sink(const Value*) tail, VarList* v) {
    Value* head = Varlist_Slots_Head(v);
    *tail = head + v->content.dynamic.used - 1;
    return head;
}


//=//// FRAME! VarList* <-> Level* STRUCTURE //////////////////////////////=//
//
// The Stub.misc field of frames which can be tied to levels can be a Level*,
// instead of an "adjunct" object.
//

INLINE Option(Level*) Misc_Runlevel(Stub* varlist) {
    assert(Is_Stub_Varlist(varlist));
    assert(CTX_TYPE(varlist) == REB_FRAME);
    assert(Not_Stub_Flag(varlist, MISC_NODE_NEEDS_MARK));
    return varlist->misc.runlevel;
}

INLINE void Tweak_Misc_Runlevel(Stub* varlist, Option(Level*) L) {
    assert(Is_Stub_Varlist(varlist));
    possibly(CTX_TYPE(varlist) == REB_FRAME);  // may not be fully formed yet
    assert(Not_Stub_Flag(varlist, MISC_NODE_NEEDS_MARK));
    varlist->misc.runlevel = maybe L;
}

INLINE Level* Level_Of_Varlist_If_Running(VarList* varlist) {
    assert(Is_Frame(Varlist_Archetype(varlist)));
    if (Get_Stub_Flag(varlist, MISC_NODE_NEEDS_MARK))
        return nullptr;  // Stub.misc is Misc_Varlist_Adjunct(), not Level*

    Level* L = maybe Misc_Runlevel(varlist);
    if (not L)
        return nullptr;

    assert(L->executor == &Action_Executor);
    return L;
}

#define Is_Frame_On_Stack(varlist) \
    (Level_Of_Varlist_If_Running(varlist) != nullptr)


INLINE Level* Level_Of_Varlist_May_Fail(VarList* c) {
    Level* L = Level_Of_Varlist_If_Running(c);
    if (not L)
        fail (Error_Frame_Not_On_Stack_Raw());
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

// Useful if you want to start a context out as NODE_FLAG_MANAGED so it does
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
