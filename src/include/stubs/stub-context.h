//
//  File: %stub-context.h
//  Summary: {Context definitions AFTER including %tmp-internals.h}
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
// A "context" is the abstraction behind OBJECT!, PORT!, FRAME!, ERROR!, etc.
// It maps keys to values using two parallel series, whose indices line up in
// correspondence:
//
//   "KEYLIST" - a series of pointer-sized elements to String* symbols.
//
//   "VARLIST" - an array which holds an archetypal ANY-CONTEXT! value in its
//   [0] element, and then a cell-sized slot for each variable.
//
// A `Context*` is an alias of the varlist's `Array*`, and keylists are
// reached through the `->link` of the varlist.  The reason varlists
// are used as the identity of the context is that keylists can be shared
// between contexts.
//
// Indices into the arrays are 0-based for keys and 1-based for values, with
// the [0] elements of the varlist used an archetypal value:
//
//    VARLIST ARRAY (aka Context*)  ---Link--+
//  +------------------------------+        |
//  +          "ROOTVAR"           |        |
//  | Archetype ANY-CONTEXT! Value |        v         KEYLIST SERIES
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! 1      |        |     Symbol* key symbol  1   |
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! 2      |        |     Symbol* key symbol 2    |
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! ...    |        |     Symbol* key symbol ...  |
//  +------------------------------+        +-------------------------------+
//
// (For executing frames, the ---Link--> is actually to its Level* structure
// so the paramlist of the CTX_FRAME_PHASE() must be consulted.  When the
// frame stops running, the paramlist is written back to the link again.)
//
// The "ROOTVAR" is a canon value image of an ANY-CONTEXT!'s `REBVAL`.  This
// trick allows a single Context* pointer to be passed around rather than the
// REBVAL struct which is 4x larger, yet use existing memory to make a REBVAL*
// when needed (using CTX_ARCHETYPE()).  ACTION!s have a similar trick.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's series pointer.  Then they cache the index of that
// word's symbol in the context's keylist, for a fast lookup to get to the
// corresponding var.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Once a word is bound to a context the index is treated as permanent.
//   This is why objects are "append only"...because disruption of the index
//   numbers would break the extant words with index numbers to that position.
//   (Appending to keylists involves making a copy if it is shared.)
//
// * Since varlists and keylists always have more than one element, they are
//   allocated with SERIES_FLAG_DYNAMIC and do not need to check for whether
//   the singular optimization when being used.  This does not apply when a
//   varlist becomes invalid (e.g. via FREE), when its data allocation is
//   released and it is decayed to a singular.
//


#ifdef NDEBUG
    #define Assert_Context(c) cast(void, 0)
#else
    #define Assert_Context(c) Assert_Context_Core(c)
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
    SERIES_FLAG_24


// Context* properties (note: shares BONUS_KEYSOURCE() with Action*)
//
// Note: MODULE! contexts depend on a property stored in the META field, which
// is another object's-worth of data *about* the module's contents (e.g. the
// processed header)
//
#define CTX_ADJUNCT(c)     MISC(VarlistAdjunct, CTX_VARLIST(c))

#define LINK_Patches_TYPE       Array*
#define HAS_LINK_Patches        FLAVOR_VARLIST


// ANY-CONTEXT! value cell schematic
//
#define VAL_CONTEXT_VARLIST(v)              cast(Array*, Cell_Node1(v))
#define INIT_VAL_CONTEXT_VARLIST            Init_Cell_Node1
#define VAL_FRAME_PHASE_OR_LABEL_NODE       Cell_Node2  // faster in debug
#define VAL_FRAME_PHASE_OR_LABEL(v)         cast(Series*, Cell_Node2(v))
#define INIT_VAL_FRAME_PHASE_OR_LABEL       Init_Cell_Node2


//=//// CONTEXT ARCHETYPE VALUE CELL (ROOTVAR)  ///////////////////////////=//
//
// A REBVAL* must contain enough information to find what is needed to define
// a context.  That fact is leveraged by the notion of keeping the information
// in the context itself as the [0] element of the varlist.  This means it is
// always on hand when a REBVAL* is needed, so you can do things like:
//
//     Context* c = ...;
//     rebElide("print [pick", CTX_ARCHETYPE(c), "'field]");
//
// The archetype stores the varlist, and since it has a value header it also
// encodes which specific type of context (OBJECT!, FRAME!, MODULE!...) the
// context represents.
//
// In the case of a FRAME!, the archetype also stores an ACTION! pointer that
// represents the action the frame is for.  Since this information can be
// found in the archetype, non-archetype cells can use the cell slot for
// purposes other than storing the archetypal action (see PHASE/LABEL section)
//
// Note: Other context types could use the slots for binding and phase for
// other purposes.  For instance, MODULE! could store its header information.
// For the moment that is done with the CTX_ADJUNCT() field instead.
//

INLINE const REBVAL *CTX_ARCHETYPE(Context* c) {  // read-only form
    const Series* varlist = CTX_VARLIST(c);
    if (Get_Series_Flag(varlist, INACCESSIBLE)) {  // a freed stub
        assert(Not_Series_Flag(varlist, DYNAMIC));  // variables are gone
        return c_cast(REBVAL*, &varlist->content.fixed);
    }
    return c_cast(REBVAL*, varlist->content.dynamic.data);
}

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_ARCHETYPE(c))

INLINE REBVAL *CTX_ROOTVAR(Context* c)  // mutable archetype access
  { return m_cast(REBVAL*, CTX_ARCHETYPE(c)); }  // inline checks mutability

INLINE Phase* CTX_FRAME_PHASE(Context* c) {
    const REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_TYPE(archetype) == REB_FRAME);
    return cast(Phase*, VAL_FRAME_PHASE_OR_LABEL_NODE(archetype));
}

INLINE Context* CTX_FRAME_BINDING(Context* c) {
    const REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_TYPE(archetype) == REB_FRAME);
    return cast(Context*, BINDING(archetype));
}

INLINE void INIT_VAL_CONTEXT_ROOTVAR_Core(
    Cell* out,
    enum Reb_Kind kind,
    Array* varlist
){
    assert(kind != REB_FRAME);  // use INIT_VAL_FRAME_ROOTVAR() instead
    assert(out == Array_Head(varlist));
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(kind) | CELL_MASK_ANY_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(out, varlist);
    mutable_BINDING(out) = UNBOUND;  // not a frame
    INIT_VAL_FRAME_PHASE_OR_LABEL(out, nullptr);  // not a frame
  #if !defined(NDEBUG)
    out->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define INIT_VAL_CONTEXT_ROOTVAR(out,kind,varlist) \
    INIT_VAL_CONTEXT_ROOTVAR_Core(TRACK(out), (kind), (varlist))

INLINE void INIT_VAL_FRAME_ROOTVAR_Core(
    Cell* out,
    Array* varlist,
    Phase* phase,
    Context* binding  // allowed to be UNBOUND
){
    assert(
        (Get_Series_Flag(varlist, INACCESSIBLE) and out == Stub_Cell(varlist))
        or out == Array_Head(varlist)
    );
    assert(phase != nullptr);
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_FRAME);
    INIT_VAL_CONTEXT_VARLIST(out, varlist);
    mutable_BINDING(out) = binding;
    INIT_VAL_FRAME_PHASE_OR_LABEL(out, phase);
  #if !defined(NDEBUG)
    out->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define INIT_VAL_FRAME_ROOTVAR(out,varlist,phase,binding) \
    INIT_VAL_FRAME_ROOTVAR_Core(TRACK(out), (varlist), (phase), (binding))


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

INLINE KeyList* CTX_KEYLIST(Context* c) {
    assert(CTX_TYPE(c) != REB_MODULE);
    if (Is_Node_A_Cell(BONUS(KeySource, CTX_VARLIST(c)))) {
        //
        // running frame, KeySource is Level*, so use action's paramlist.
        //
        return ACT_KEYLIST(CTX_FRAME_PHASE(c));
    }
    return cast(KeyList*, node_BONUS(KeySource, CTX_VARLIST(c)));  // not Level
}

INLINE void INIT_CTX_KEYLIST_SHARED(Context* c, Series* keylist) {
    Set_Subclass_Flag(KEYLIST, keylist, SHARED);
    INIT_BONUS_KEYSOURCE(CTX_VARLIST(c), keylist);
}

INLINE void INIT_CTX_KEYLIST_UNIQUE(Context* c, KeyList *keylist) {
    assert(Not_Subclass_Flag(KEYLIST, keylist, SHARED));
    INIT_BONUS_KEYSOURCE(CTX_VARLIST(c), keylist);
}


//=//// Context* ACCESSORS /////////////////////////////////////////////////=//
//
// These are access functions that should be used when what you have in your
// hand is just a Context*.  THIS DOES NOT ACCOUNT FOR PHASE...so there can
// actually be a difference between these two expressions for FRAME!s:
//
//     REBVAL *x = VAL_CONTEXT_KEYS_HEAD(context);  // accounts for phase
//     REBVAL *y = CTX_KEYS_HEAD(VAL_CONTEXT(context), n);  // no phase
//
// Context's "length" does not count the [0] cell of either the varlist or
// the keylist arrays.  Hence it must subtract 1.  SERIES_MASK_VARLIST
// includes SERIES_FLAG_DYNAMIC, so a dyamic series can be assumed so long
// as it is valid.
//

INLINE REBLEN CTX_LEN(Context* c) {
    assert(CTX_TYPE(c) != REB_MODULE);
    return CTX_VARLIST(c)->content.dynamic.used - 1;  // -1 for archetype
}

INLINE const Key* CTX_KEY(Context* c, REBLEN n) {
    //
    // !!! Inaccessible contexts have to retain their keylists, at least
    // until all words bound to them have been adjusted somehow, because the
    // words depend on those keys for their spellings (once bound)
    //
    /* assert(Not_Series_Flag(c, INACCESSIBLE)); */

    assert(n != 0 and n <= CTX_LEN(c));
    return Series_At(const Key, CTX_KEYLIST(c), n - 1);
}

INLINE Value(*) CTX_VAR(Context* c, REBLEN n) {  // 1-based, no Cell*
    assert(Not_Series_Flag(CTX_VARLIST(c), INACCESSIBLE));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(Value(*), cast(Series*, c)->content.dynamic.data) + n;
}

INLINE Value(const*) Try_Lib_Var(SymId id) {
    assert(id < LIB_SYMS_MAX);

    // !!! We allow a "removed state", in case modules implement a
    // feature for dropping variables.
    //
    if (INODE(PatchContext, &PG_Lib_Patches[id]) == nullptr)
        return nullptr;

    return cast(Value(*), Stub_Cell(&PG_Lib_Patches[id]));
}

#define Lib(name) \
    Try_Lib_Var(SYM_##name)

INLINE Value(*) Force_Lib_Var(SymId id) {
    Value(*) var = m_cast(Value(*), Try_Lib_Var(id));
    if (var)
        return var;
    return Append_Context(Lib_Context, Canon_Symbol(id));
}

#define force_Lib(name) \
    Force_Lib_Var(SYM_##name)

#define SysUtil(name) \
    cast(const Value(*), MOD_VAR(Sys_Context, Canon_Symbol(SYM_##name), true))


INLINE Value(*) MOD_VAR(Context* c, const Symbol* sym, bool strict) {
    //
    // Optimization for Lib_Context for datatypes + natives + generics; use
    // tailored order of SYM_XXX constants to beeline for the storage.  The
    // entries were all allocated during Startup_Lib().
    //
    // Note: Call Lib() macro directly if you have a SYM in hand vs. a canon.
    //
    if (c == Lib_Context) {
        Option(SymId) id = Symbol_Id(sym);
        if (id != 0 and id < LIB_SYMS_MAX) {
            //
            // !!! We need to consider the strictness here, with case sensitive
            // binding we can't be sure it's a match.  :-/  For this moment
            // hope lib doesn't have two-cased variations of anything.
            //
            return m_cast(Value(*), Try_Lib_Var(unwrap(id)));
        }
    }

    const Symbol* synonym = sym;
    do {
        Stub* patch = MISC(Hitch, sym);
        while (Get_Series_Flag(patch, BLACK))  // binding temps
            patch = cast(Stub*, node_MISC(Hitch, patch));

        for (; patch != sym; patch = cast(Stub*, node_MISC(Hitch, patch))) {
            if (INODE(PatchContext, patch) == c)
                return cast(Value(*), Stub_Cell(patch));
        }
        if (strict)
            return nullptr;
        sym = LINK(Synonym, sym);
    } while (synonym != sym);
    return nullptr;
}


// CTX_VARS_HEAD() and CTX_KEYS_HEAD() allow CTX_LEN() to be 0, while
// CTX_VAR() does not.  Also, CTX_KEYS_HEAD() gives back a mutable slot.

#define CTX_KEYS_HEAD(c) \
    Series_At(Key, CTX_KEYLIST(c), 0)  // 0-based

#define CTX_VARS_HEAD(c) \
    (cast(Value(*), x_cast(Series*, (c))->content.dynamic.data) + 1)

INLINE const Key* CTX_KEYS(const Key* * tail, Context* c) {
    Series* keylist = CTX_KEYLIST(c);
    *tail = Series_Tail(Key, keylist);
    return Series_Head(Key, keylist);
}

INLINE Value(*) CTX_VARS(Value(const*) * tail, Context* c) {
    Value(*) head = CTX_VARS_HEAD(c);
    *tail = head + x_cast(Series*, (c))->content.dynamic.used - 1;
    return head;
}


//=//// FRAME! Context* <-> Level* STRUCTURE //////////////////////////=//
//
// For a FRAME! context, the keylist is redundant with the paramlist of the
// CTX_FRAME_PHASE() that the frame is for.  That is taken advantage of when
// a frame is executing in order to use the LINK() keysource to point at the
// running Level* structure for that stack level.  This provides a cheap
// way to navigate from a Context* to the Level* that's running it.
//

INLINE bool Is_Frame_On_Stack(Context* c) {
    assert(Is_Frame(CTX_ARCHETYPE(c)));
    return Is_Node_A_Cell(BONUS(KeySource, CTX_VARLIST(c)));
}

INLINE Level* CTX_LEVEL_IF_ON_STACK(Context* c) {
    Node* keysource = BONUS(KeySource, CTX_VARLIST(c));
    if (not Is_Node_A_Cell(keysource))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(Not_Series_Flag(CTX_VARLIST(c), INACCESSIBLE));
    assert(Is_Frame(CTX_ARCHETYPE(c)));

    Level* L = cast(Level*, keysource);
    assert(L->executor == &Action_Executor);
    return L;
}

INLINE Level* CTX_LEVEL_MAY_FAIL(Context* c) {
    Level* L = CTX_LEVEL_IF_ON_STACK(c);
    if (not L)
        fail (Error_Frame_Not_On_Stack_Raw());
    return L;
}

INLINE void FAIL_IF_INACCESSIBLE_CTX(Context* c) {
    if (Get_Series_Flag(CTX_VARLIST(c), INACCESSIBLE)) {
        if (CTX_TYPE(c) == REB_FRAME)
            fail (Error_Expired_Frame_Raw()); // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }
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

#define Copy_Context_Shallow_Managed(src) \
    Copy_Context_Extra_Managed((src), 0, 0)

// Make sure a context's keylist is not shared.  Note any CTX_KEY() values
// may go stale from this context after this call.
//
INLINE Context* Force_KeyList_Unique(Context* context) {
    bool was_changed = Expand_Context_KeyList_Core(context, 0);
    UNUSED(was_changed);  // keys wouldn't go stale if this was false
    return context;
}

// Useful if you want to start a context out as NODE_FLAG_MANAGED so it does
// not have to go in the unmanaged roots list and be removed later.  (Be
// careful not to do any evaluations or trigger GC until it's well formed)
//
#define Alloc_Context(kind,capacity) \
    Alloc_Context_Core((kind), (capacity), SERIES_FLAGS_NONE)


//=////////////////////////////////////////////////////////////////////////=//
//
// LOCKING
//
//=////////////////////////////////////////////////////////////////////////=//

INLINE void Deep_Freeze_Context(Context* c) {
    Protect_Context(
        c,
        PROT_SET | PROT_DEEP | PROT_FREEZE
    );
    Uncolor_Array(CTX_VARLIST(c));
}

#define Is_Context_Frozen_Deep(c) \
    Is_Array_Frozen_Deep(CTX_VARLIST(c))


//
//  Steal_Context_Vars: C
//
// This is a low-level trick which mutates a context's varlist into a stub
// "free" node, while grabbing the underlying memory for its variables into
// an array of values.
//
// It has a notable use by DO of a heap-based FRAME!, so that the frame's
// filled-in heap memory can be directly used as the args for the invocation,
// instead of needing to push a redundant run of stack-based memory cells.
//
INLINE Context* Steal_Context_Vars(Context* c, Node* keysource) {
    Stub* stub = CTX_VARLIST(c);

    // Rather than memcpy() and touch up the header and info to remove
    // SERIES_INFO_HOLD from DETAILS_FLAG_IS_NATIVE, or NODE_FLAG_MANAGED,
    // etc.--use constant assignments and only copy the remaining fields.
    //
    Stub* copy = Prep_Stub(
        Alloc_Stub(),  // not preallocated
        SERIES_MASK_VARLIST
            | SERIES_FLAG_FIXED_SIZE
    );
    SERIES_INFO(copy) = SERIES_INFO_MASK_NONE;
    Trash_Pointer_If_Debug(BONUS(KeySource, copy)); // needs update
    Mem_Copy(&copy->content, &stub->content, sizeof(union StubContentUnion));
    MISC(VarlistAdjunct, copy) = nullptr;  // let stub have the meta
    LINK(Patches, copy) = nullptr;  // don't carry forward patches

    REBVAL *rootvar = cast(REBVAL*, copy->content.dynamic.data);

    // Convert the old varlist that had outstanding references into a
    // singular "stub", holding only the CTX_ARCHETYPE.  This is needed
    // for the ->binding to allow Derelativize(), see SPC_BINDING().
    //
    // Note: previously this had to preserve VARLIST_FLAG_FRAME_FAILED, but
    // now those marking failure are asked to do so manually to the stub
    // after this returns (hence they need to cache the varlist first).
    //
    Set_Series_Flag(stub, INACCESSIBLE);

    REBVAL *single = cast(REBVAL*, &stub->content.fixed);
    single->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | CELL_MASK_FRAME;
    INIT_VAL_CONTEXT_VARLIST(single, x_cast(Array*, stub));
    mutable_BINDING(single) = BINDING(rootvar);

  #if !defined(NDEBUG)
    INIT_VAL_FRAME_PHASE_OR_LABEL(single, nullptr);  // can't trash
  #endif

    INIT_VAL_CONTEXT_VARLIST(rootvar, x_cast(Array*, copy));

    // Disassociate the stub from the frame, by degrading the link field
    // to a keylist.  !!! Review why this was needed, vs just nullptr
    //
    INIT_BONUS_KEYSOURCE(cast(Array*, stub), keysource);

    Clear_Series_Flag(stub, DYNAMIC);  // mark stub as no longer dynamic

    return cast(Context*, copy);
}
