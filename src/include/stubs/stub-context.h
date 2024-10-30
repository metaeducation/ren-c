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


#ifdef NDEBUG
    #define Assert_Varlist(c) cast(void, 0)
#else
    #define Assert_Varlist(c) Assert_Varlist_Core(c)
#endif


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


// VarList* properties (note: shares BONUS_KEYSOURCE() with Action*)
//
// Note: MODULE! contexts depend on a property stored in the META field, which
// is another object's-worth of data *about* the module's contents (e.g. the
// processed header)
//
#define CTX_ADJUNCT(c)     MISC(VarlistAdjunct, Varlist_Array(c))

#define Tweak_Cell_Context_Varlist            Tweak_Cell_Node1

#define Tweak_Cell_Frame_Phase_Or_Label       Tweak_Cell_Node2
#define Extract_Cell_Frame_Phase_Or_Label(v)  cast(Flex*, Cell_Node2(v))


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
// For the moment that is done with the CTX_ADJUNCT() field instead.
//

INLINE const Element* Varlist_Archetype(VarList* c) {  // read-only form
    const Flex* varlist = Varlist_Array(c);
    return c_cast(Element*, varlist->content.dynamic.data);
}

INLINE Heart CTX_TYPE(Context* c) {
    return Cell_Heart(Varlist_Archetype(cast(VarList*, c)));
}

INLINE Element* Rootvar_Of_Varlist(VarList* c)  // mutable archetype access
  { return m_cast(Element*, Varlist_Archetype(c)); }  // inline checks mutability

INLINE Phase* CTX_FRAME_PHASE(VarList* c) {
    const Value* archetype = Varlist_Archetype(c);
    assert(Cell_Heart_Ensure_Noquote(archetype) == REB_FRAME);
    return cast(Phase*, Extract_Cell_Frame_Phase_Or_Label(archetype));
}

INLINE VarList* CTX_FRAME_BINDING(VarList* c) {
    const Value* archetype = Varlist_Archetype(c);
    assert(Cell_Heart_Ensure_Noquote(archetype) == REB_FRAME);
    return cast(VarList*, BINDING(archetype));
}

//=//// FRAME TARGET //////////////////////////////////////////////////////=//
//
// A FRAME! cell can store a context as a target.  RETURN here would store
// the action that the return will return from.  A METHOD will store the
// object that the method is linked to.  Since it is per-cell, the same
// archetypal actoin can be specialized to many different targets.
//
// Note: The presence of targets in non-archetype values makes it possible
// for FRAME! values that have phases to carry the binding of that phase.
// This is a largely unexplored feature, but is used in REDO scenarios where
// a running frame gets re-executed.  More study is needed.
//

INLINE Option(VarList*) Cell_Frame_Coupling(const Cell* c) {
    assert(Cell_Heart(c) == REB_FRAME);
    return cast(VarList*, m_cast(Node*, EXTRA(Any, c).node));
}

INLINE void Tweak_Cell_Frame_Coupling(Cell* c, Option(VarList*) coupling) {
    assert(HEART_BYTE(c) == REB_FRAME);
    EXTRA(Any, c).node = maybe coupling;
}

INLINE void Tweak_Non_Frame_Varlist_Rootvar_Untracked(
    Array* varlist,
    Heart heart
){
    assert(heart != REB_FRAME);  // use Tweak_Frame_Varlist_Rootvar() instead
    Cell* rootvar = Array_Head(varlist);
    Reset_Cell_Header_Untracked(
        rootvar,
        FLAG_HEART_BYTE(heart) | CELL_MASK_ANY_CONTEXT
    );
    Tweak_Cell_Context_Varlist(rootvar, varlist);
    BINDING(rootvar) = UNBOUND;  // not a frame
    Tweak_Cell_Frame_Phase_Or_Label(rootvar, nullptr);  // not a frame
  #if !defined(NDEBUG)
    rootvar->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define Tweak_Non_Frame_Varlist_Rootvar(heart,varlist) \
    Tweak_Non_Frame_Varlist_Rootvar_Untracked((heart), (varlist))

INLINE void Tweak_Frame_Varlist_Rootvar_Untracked(
    Array* varlist,
    Phase* phase,
    Option(VarList*) coupling
){
    Cell* rootvar = Array_Head(varlist);
    assert(phase != nullptr);
    Reset_Cell_Header_Untracked(rootvar, CELL_MASK_FRAME);
    Tweak_Cell_Context_Varlist(rootvar, varlist);
    Tweak_Cell_Frame_Coupling(rootvar, coupling);
    Tweak_Cell_Frame_Phase_Or_Label(rootvar, phase);
  #if !defined(NDEBUG)
    rootvar->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define Tweak_Frame_Varlist_Rootvar(varlist,phase,target) \
    Tweak_Frame_Varlist_Rootvar_Untracked((varlist), (phase), (target))


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
    if (Is_Node_A_Cell(BONUS(KeySource, Varlist_Array(c)))) {
        //
        // running frame, KeySource is Level*, so use action's paramlist.
        //
        return ACT_KEYLIST(CTX_FRAME_PHASE(c));
    }
    return cast(KeyList*, node_BONUS(KeySource, c));  // not Level
}

INLINE void Tweak_Keylist_Of_Varlist_Shared(Flex* f, KeyList* keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    Set_Subclass_Flag(KEYLIST, keylist, SHARED);
    Tweak_Bonus_Keysource(f, keylist);
}

INLINE void Tweak_Keylist_Of_Varlist_Unique(Flex* f, KeyList *keylist) {
    assert(Is_Stub_Varlist(f));  // may not be complete yet
    assert(Not_Subclass_Flag(KEYLIST, keylist, SHARED));
    Tweak_Bonus_Keysource(f, keylist);
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
    return cast(Value*, x_cast(Array*, c)->content.dynamic.data) + n;
}

INLINE Value* Mutable_Lib_Var_For_Id(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&PG_Lib_Patches[id]));
    assert(Not_Cell_Flag(slot, PROTECTED));
    return slot;
}

INLINE const Value* Lib_Var_For_Id(SymId id) {
    assert(id < LIB_SYMS_MAX);
    Value* slot = cast(Value*, Stub_Cell(&PG_Lib_Patches[id]));
    assert(not Is_Nothing(slot));
    return slot;
}

INLINE Sink(Value) Sink_Lib_Var_For_Id(SymId id) {
    assert(id < LIB_SYMS_MAX);
    return cast(Value*, Stub_Cell(&PG_Lib_Patches[id]));
}

#define Lib(name) \
    Lib_Var_For_Id(SYM_##name)

#define Sink_Lib_Var(name) \
    Sink_Lib_Var_For_Id(SYM_##name)

#define Mutable_Lib_Var(name) \
    Mutable_Lib_Var_For_Id(SYM_##name)


#define SysUtil(name) \
    cast(const Value*, MOD_VAR(Sys_Context, Canon_Symbol(SYM_##name), true))

// Optimization for Lib_Context for datatypes + natives + generics; usage is
// tailored in order for SYM_XXX constants to beeline for the storage.  The
// entries were all allocated during Startup_Lib().
//
// Note: Call Lib() macro directly if you have a SYM in hand vs. a canon.
//
// 1. !!! We need to consider the strictness here, with case sensitive binding
//    we can't be sure it's a match.  :-/  For this moment hope lib doesn't
//    have two-cased variations of anything.
//
INLINE Option(Stub*) MOD_PATCH(SeaOfVars* sea, const Symbol* sym, bool strict) {
    if (sea == Lib_Context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id != 0 and id < LIB_SYMS_MAX) {
            if (INODE(PatchContext, &PG_Lib_Patches[id]) == nullptr)  // [1]
                return nullptr;

            return &PG_Lib_Patches[id];
        }
    }

    const Symbol* synonym = sym;
    do {
        Stub* patch = MISC(Hitch, sym);
        if (Get_Subclass_Flag(SYMBOL, sym, MISC_IS_BINDINFO))
            patch = cast(Stub*, node_MISC(Hitch, patch));  // skip bindinfo

        for (; patch != sym; patch = cast(Stub*, node_MISC(Hitch, patch))) {
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
    (cast(Value*, x_cast(Flex*, (c))->content.dynamic.data) + 1)

INLINE const Key* Varlist_Keys(const Key* * tail_out, VarList* c) {
    KeyList* keylist = Keylist_Of_Varlist(c);
    *tail_out = Flex_Tail(Key, keylist);
    return Flex_Head(Key, keylist);
}

INLINE Value* Varlist_Slots(const Value* * tail_out, VarList* v) {
    Value* head = Varlist_Slots_Head(v);
    *tail_out = head + x_cast(Flex*, (v))->content.dynamic.used - 1;
    return head;
}


//=//// FRAME! VarList* <-> Level* STRUCTURE //////////////////////////=//
//
// For a FRAME! context, the keylist is redundant with the paramlist of the
// CTX_FRAME_PHASE() that the frame is for.  That is taken advantage of when
// a frame is executing in order to use the LINK() keysource to point at the
// running Level* structure for that stack level.  This provides a cheap
// way to navigate from a VarList* to the Level* that's running it.
//

INLINE bool Is_Frame_On_Stack(VarList* c) {
    assert(Is_Frame(Varlist_Archetype(c)));
    return Is_Node_A_Cell(BONUS(KeySource, Varlist_Array(c)));
}

INLINE Level* Level_Of_Varlist_If_Running(VarList* c) {
    Node* keysource = BONUS(KeySource, Varlist_Array(c));
    if (not Is_Node_A_Cell(keysource))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(Is_Frame(Varlist_Archetype(c)));

    Level* L = cast(Level*, keysource);
    assert(L->executor == &Action_Executor);
    return L;
}

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
    Alloc_Varlist_Core((kind), (capacity), FLEX_FLAGS_NONE)


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
    Is_Array_Frozen_Deep(Varlist_Array(c))


//
//  Steal_Varlist_Vars: C
//
// This is a low-level trick which mutates a context's varlist into a stub
// "free" node, while grabbing the underlying memory for its variables into
// an array of values.
//
// It has a notable use by EVAL of a heap-based FRAME!, so that the frame's
// filled-in heap memory can be directly used as the args for the invocation,
// instead of needing to push a redundant run of stack-based memory cells.
//
// 1. Rather than Mem_Copy() the whole stub and touch up the header and info
//    to remove FLEX_INFO_HOLD from DETAILS_FLAG_IS_NATIVE, or things like
//    NODE_FLAG_MANAGED, etc.--use constant assignments and only copy the
//    remaining fields.
//
// 2. Once this tried to leave some amount of "information" in the stolen
//    from context, despite marking it inaccessible.  The modern idea is
//    that once a stub becomes inaccessible, the GC canonizes it to a
//    single inaccessible node so it can free up the other stubs.  So it
//    should lose all of its information.
//
INLINE VarList* Steal_Varlist_Vars(VarList* c, Node* keysource) {
    UNUSED(keysource);

    Stub* stub = c;

    Stub* copy = Prep_Stub(  // don't Mem_Copy() the incoming stub [1]
        Alloc_Stub(),  // not preallocated
        FLEX_MASK_VARLIST
            | FLEX_FLAG_FIXED_SIZE
    );
    Corrupt_Pointer_If_Debug(BONUS(KeySource, copy)); // needs update
    Mem_Copy(&copy->content, &stub->content, sizeof(union StubContentUnion));
    MISC(VarlistAdjunct, copy) = nullptr;  // let stub have the meta
    node_LINK(NextVirtual, copy) = nullptr;

    Value* rootvar = cast(Value*, copy->content.dynamic.data);
    Tweak_Cell_Context_Varlist(rootvar, x_cast(Array*, copy));

    Set_Flex_Inaccessible(stub);  // Make unusable [2]
  #if DEBUG
    FLAVOR_BYTE(stub) = FLAVOR_CORRUPT;
    Corrupt_Pointer_If_Debug(stub->link.any.corrupt);
    Corrupt_Pointer_If_Debug(stub->misc.any.corrupt);
    Corrupt_If_Debug(stub->content);
    Corrupt_Pointer_If_Debug(stub->info.any.corrupt);
  #endif

    return cast(VarList*, copy);
}
