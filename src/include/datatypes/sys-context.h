//
//  File: %sys-context.h
//  Summary: {context! defs AFTER %tmp-internals.h (see: %sys-context.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// In Rebol terminology, a "context" is an abstraction which gives two
// parallel arrays, whose indices line up in a correspondence:
//
// * "keylist" - an array that contains IS_PARAM() cells, but which have a
//   symbol ID encoded as an extra piece of information for that key.
//
// * "varlist" - an array of equal length to the keylist, which holds an
//   arbitrary REBVAL in each position that corresponds to its key.
//
// Frame key/var indices start at one, and they leave two REBVAL slots open
// in the 0 spot for other uses.  With an ANY-CONTEXT!, the use for the
// "ROOTVAR" is to store a canon value image of the ANY-CONTEXT!'s REBVAL
// itself.  This trick allows a single REBCTX* to be passed around rather
// than the REBVAL struct which is 4x larger, yet still reconstitute the
// entire REBVAL if it is needed.
//
// (The "ROOTKEY" of the keylist is currently only used a context is a FRAME!.
// It is using a paramlist as the keylist, so the [0] is the archetype action
// value of that paramlist).
//
// The `keylist` is held in the varlist's LINK().keysource field, and it may
// be shared with an arbitrary number of other contexts.  Changing the keylist
// involves making a copy if it is shared.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's series pointer.  Then they cache the index of that
// word's symbol in the context's keylist, for a fast lookup to get to the
// corresponding var.  The key is a typeset which has several flags
// controlling behaviors like whether the var is protected or hidden.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTES:
//
// * Once a word is bound to a context the index is treated as permanent.
//   This is why objects are "append only"...because disruption of the index
//   numbers would break the extant words with index numbers to that position.
//
// * !!! Ren-C might wind up undoing this by paying for the check of the
//   symbol number at the time of lookup, and if it does not match consider it
//   a cache miss and re-lookup...adjusting the index inside of the word.
//   For efficiency, some objects could be marked as not having this property,
//   but it may be just as efficient to check the symbol match as that bit.
//
// * REB_MODULE depends on a property stored in the "meta" Reb_Series.link
//   field of the keylist, which is another object's-worth of data *about*
//   the module's contents (e.g. the processed header)
//

#define CELL_MASK_CONTEXT \
    (CELL_FLAG_FIRST_IS_NODE  /* varlist */ \
        | CELL_FLAG_SECOND_IS_NODE  /* phase (for FRAME!) */)


//=//// SERIES_FLAG_VARLIST_FRAME_FAILED //////////////////////////////////=//
//
// In the specific case of a frame being freed due to a failure, this mark
// is put on the context node.  What this allows is for the system to account
// for which nodes are being GC'd due to lack of a rebRelease(), as opposed
// to those being GC'd due to failure.
//
// What this means is that the system can use managed handles by default
// while still letting "rigorous" code track cases where it made use of the
// GC facility vs. doing explicit tracking.  Essentially, it permits a kind
// of valgrind/address-sanitizer way of looking at a codebase vs. just taking
// for granted that it will GC things.
//
#define SERIES_FLAG_VARLIST_FRAME_FAILED \
    ARRAY_FLAG_23


#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
#endif

// On the keylist of an object, this points at a keylist which has the
// same number of keys or fewer, which represents an object which this
// object is derived from.  Note that when new object instances are
// created which do not require expanding the object, their keylist will
// be the same as the object they are derived from.
//
#define LINK_ANCESTOR_NODE(s)       LINK(s).custom.node
#define LINK_ANCESTOR(s)            ARR(LINK_ANCESTOR_NODE(s))


#define CTX_VARLIST(c) \
    (&(c)->varlist)

#define VAL_PHASE_NODE(v) \
    PAYLOAD(Any, (v)).second.node

#define VAL_PHASE_UNCHECKED(v) \
    ACT(VAL_PHASE_NODE(v))

inline static REBACT *VAL_PHASE(const REBVAL *frame) {
    assert(IS_FRAME(frame));
    REBACT *phase = VAL_PHASE_UNCHECKED(frame);
    assert(phase != nullptr);
    return phase;
}

// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// REBSER node data itself.
//
inline static const REBVAL *CTX_ARCHETYPE(const REBCTX *c) {
    const REBSER *varlist = SER(CTX_VARLIST(c));
    if (not IS_SER_DYNAMIC(varlist))
        return cast(const REBVAL*, &varlist->content.fixed);

    // If a context has its data freed, it must be converted into non-dynamic
    // form if it wasn't already (e.g. if it wasn't a FRAME!)
    //
    assert(NOT_SERIES_INFO(varlist, INACCESSIBLE));
    return cast(const REBVAL*, varlist->content.dynamic.data);
}

inline static REBVAL *CTX_ROOTVAR(REBCTX *c)  // mutable archetype access
  { return m_cast(REBVAL*, CTX_ARCHETYPE(c)); }


// CTX_KEYLIST is called often, and it's worth it to make it as fast as
// possible--even in an unoptimized build.
//
inline static REBARR *CTX_KEYLIST(REBCTX *c) {
    if (not Is_Node_Cell(LINK_KEYSOURCE(c)))
        return ARR(LINK_KEYSOURCE(c)); // not a REBFRM, so use keylist

    // If the context in question is a FRAME! value, then the ->phase
    // of the frame presents the "view" of which keys should be visible at
    // this phase.  So if the phase is a specialization, then it should
    // not show all the underlying function's keys...just the ones that
    // are not hidden in the facade that specialization uses.  Since the
    // phase changes, a fixed value can't be put into the keylist...that is
    // just the keylist of the underlying function.
    //
    return ACT_PARAMLIST(VAL_PHASE(CTX_ARCHETYPE(c)));
}

static inline void INIT_CTX_KEYLIST_SHARED(REBCTX *c, REBARR *keylist) {
    SET_SERIES_INFO(keylist, KEYLIST_SHARED);
    INIT_LINK_KEYSOURCE(c, NOD(keylist));
}

static inline void INIT_CTX_KEYLIST_UNIQUE(REBCTX *c, REBARR *keylist) {
    assert(NOT_SERIES_INFO(keylist, KEYLIST_SHARED));
    INIT_LINK_KEYSOURCE(c, NOD(keylist));
}

// Navigate from context to context components.  Note that the context's
// "length" does not count the [0] cell of either the varlist or the keylist.
// Hence it must subtract 1.  Internally to the context building code, the
// real length of the two series must be accounted for...so the 1 gets put
// back in, but most clients are only interested in the number of keys/values
// (and getting an answer for the length back that was the same as the length
// requested in context creation).
//
#define CTX_LEN(c) \
    (cast(REBSER*, (c))->content.dynamic.used - 1) // used > 1, so dynamic

#define CTX_ROOTKEY(c) \
    cast(REBVAL*, SER(CTX_KEYLIST(c))->content.dynamic.data) // used > 1

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_ARCHETYPE(c))

// The keys and vars are accessed by positive integers starting at 1
//
#define CTX_KEYS_HEAD(c) \
    SER_AT(REBVAL, SER(CTX_KEYLIST(c)), 1) // a CTX_KEY can't hold a RELVAL

inline static bool Is_Frame_On_Stack(REBCTX *c) {
    assert(IS_FRAME(CTX_ARCHETYPE(c)));
    return Is_Node_Cell(LINK_KEYSOURCE(c));
}

inline static REBFRM *CTX_FRAME_IF_ON_STACK(REBCTX *c) {
    REBNOD *keysource = LINK_KEYSOURCE(c);
    if (not Is_Node_Cell(keysource))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(NOT_SERIES_INFO(CTX_VARLIST(c), INACCESSIBLE));
    assert(IS_FRAME(CTX_ARCHETYPE(c)));

    REBFRM *f = FRM(keysource);
    assert(f->original); // inline Is_Action_Frame() to break dependency
    return f;
}

inline static REBFRM *CTX_FRAME_MAY_FAIL(REBCTX *c) {
    REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
    if (not f)
        fail (Error_Frame_Not_On_Stack_Raw());
    return f;
}

#define CTX_VARS_HEAD(c) \
    SER_AT(REBVAL, SER(CTX_VARLIST(c)), 1) // may fail() if inaccessible

inline static REBVAL *CTX_KEY(REBCTX *c, REBLEN n) {
    assert(NOT_SERIES_INFO(c, INACCESSIBLE));
    assert(GET_ARRAY_FLAG(CTX_VARLIST(c), IS_VARLIST));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(REBVAL*, cast(REBSER*, CTX_KEYLIST(c))->content.dynamic.data)
        + n;
}

inline static REBVAL *CTX_VAR(REBCTX *c, REBLEN n) {
    assert(NOT_SERIES_INFO(c, INACCESSIBLE));
    assert(GET_ARRAY_FLAG(CTX_VARLIST(c), IS_VARLIST));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(REBVAL*, cast(REBSER*, c)->content.dynamic.data) + n;
}

inline static const REBSTR *CTX_KEY_SPELLING(REBCTX *c, REBLEN n) {
    return VAL_TYPESET_STRING(CTX_KEY(c, n));
}

inline static const REBSTR *CTX_KEY_CANON(REBCTX *c, REBLEN n) {
    return STR_CANON(CTX_KEY_SPELLING(c, n));
}

inline static REBSYM CTX_KEY_SYM(REBCTX *c, REBLEN n) {
    return STR_SYMBOL(CTX_KEY_SPELLING(c, n)); // should be same as canon
}


//=////////////////////////////////////////////////////////////////////////=//
//
// ANY-CONTEXT! (`struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The Reb_Any_Context is the basic struct used currently for OBJECT!,
// MODULE!, ERROR!, and PORT!.  It builds upon the context datatype REBCTX,
// which permits the storage of associated KEYS and VARS.
//

inline static void FAIL_IF_INACCESSIBLE_CTX(REBCTX *c) {
    if (GET_SERIES_INFO(c, INACCESSIBLE)) {
        if (CTX_TYPE(c) == REB_FRAME)
            fail (Error_Expired_Frame_Raw()); // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }
}

inline static REBCTX *VAL_CONTEXT(REBCEL(const*) v) {
    assert(ANY_CONTEXT_KIND(CELL_KIND(v)));
    assert(
        (VAL_PHASE_UNCHECKED(v) != nullptr) == (CELL_KIND(v) == REB_FRAME)
    );
    REBCTX *c = CTX(PAYLOAD(Any, v).first.node);
    FAIL_IF_INACCESSIBLE_CTX(c);
    return c;
}

inline static REBCTX *VAL_WORD_CONTEXT(const REBVAL *v) {
    assert(IS_WORD_BOUND(v));
    REBNOD *binding = VAL_BINDING(v);
    assert(
        GET_SERIES_FLAG(binding, MANAGED)
        or IS_END(FRM(LINK_KEYSOURCE(binding))->param)  // not "fulfilling"
    );
    binding->header.bits |= NODE_FLAG_MANAGED;  // !!! review managing needs
    REBCTX *c = CTX(binding);
    FAIL_IF_INACCESSIBLE_CTX(c);
    return c;
}

#define INIT_VAL_CONTEXT_VARLIST(v,varlist) \
    (PAYLOAD(Any, (v)).first.node = NOD(varlist))

#define INIT_VAL_CONTEXT_PHASE(v,phase) \
    (PAYLOAD(Any, (v)).second.node = NOD(phase))

#define VAL_PHASE(v) \
    ACT(PAYLOAD(Any, (v)).second.node)

// Convenience macros to speak in terms of object values instead of the context
//
#define VAL_CONTEXT_VAR(v,n) \
    CTX_VAR(VAL_CONTEXT(v), (n))

#define VAL_CONTEXT_KEY(v,n) \
    CTX_KEY(VAL_CONTEXT(v), (n))


// The movement of the SELF word into the domain of the object generators
// means that an object may wind up having a hidden SELF key (and it may not).
// Ultimately this key may well occur at any position.  While user code is
// discouraged from accessing object members by integer index (`pick obj 1`
// is an error), system code has historically relied upon this.
//
// During a transitional period where all MAKE OBJECT! constructs have a
// "real" SELF key/var in the first position, there needs to be an adjustment
// to the indexing of some of this system code.  Some of these will be
// temporary, because not all objects will need a definitional SELF (just as
// not all functions need a definitional RETURN).  Exactly which require it
// and which do not remains to be seen, so this macro helps review the + 1
// more easily than if it were left as just + 1.
//
#define SELFISH(n) \
    ((n) + 1)

// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the context's varlist.
//
static inline REBVAL *Init_Any_Context(
    RELVAL *out,
    enum Reb_Kind kind,
    REBCTX *c
){
  #if !defined(NDEBUG)
    Extra_Init_Any_Context_Checks_Debug(kind, c);
  #endif
    UNUSED(kind);
    ASSERT_SERIES_MANAGED(CTX_VARLIST(c));
    ASSERT_SERIES_MANAGED(CTX_KEYLIST(c));
    return Move_Value(out, CTX_ARCHETYPE(c));
}

#define Init_Object(out,c) \
    Init_Any_Context((out), REB_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Any_Context((out), REB_PORT, (c))

#define Init_Frame(out,c) \
    Init_Any_Context((out), REB_FRAME, (c))


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
    Copy_Context_Shallow_Extra_Managed((src), 0)

// Make sure a context's keylist is not shared.  Note any CTX_KEY() values
// may go stale from this context after this call.
//
inline static REBCTX *Force_Keylist_Unique(REBCTX *context) {
    bool was_changed = Expand_Context_Keylist_Core(context, 0);
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

inline static void Deep_Freeze_Context(REBCTX *c) {
    Protect_Context(
        c,
        PROT_SET | PROT_DEEP | PROT_FREEZE
    );
    Uncolor_Array(CTX_VARLIST(c));
}

#define Is_Context_Frozen_Deep(c) \
    Is_Array_Frozen_Deep(CTX_VARLIST(c))


//=////////////////////////////////////////////////////////////////////////=//
//
// ERROR! (uses `struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Errors are a subtype of ANY-CONTEXT! which follow a standard layout.
// That layout is in %boot/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %boot/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `fail()` C macro inside the source, and the various routines in %c-error.c
//

#define ERR_VARS(e) \
    cast(ERROR_VARS*, CTX_VARS_HEAD(e))

#define VAL_ERR_VARS(v) \
    ERR_VARS(VAL_CONTEXT(v))

#define Init_Error(v,c) \
    Init_Any_Context((v), REB_ERROR, (c))


// Ports are unusual hybrids of user-mode code dispatched with native code, so
// some things the user can do to the internals of a port might cause the
// C code to crash.  This wasn't very well thought out in R3-Alpha, but there
// was some validation checking.  This factors out that check instead of
// repeating the code.
//
inline static void FAIL_IF_BAD_PORT(REBVAL *port) {
    if (not ANY_CONTEXT(port))
        fail (Error_Invalid_Port_Raw());

    REBCTX *ctx = VAL_CONTEXT(port);
    if (
        CTX_LEN(ctx) < (STD_PORT_MAX - 1)
        or not IS_OBJECT(CTX_VAR(ctx, STD_PORT_SPEC))
    ){
        fail (Error_Invalid_Port_Raw());
    }
}

// It's helpful to show when a test for a native port actor is being done,
// rather than just having the code say IS_HANDLE().
//
inline static bool Is_Native_Port_Actor(const REBVAL *actor) {
    if (IS_HANDLE(actor))
        return true;
    assert(IS_OBJECT(actor));
    return false;
}


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
inline static REBCTX *Steal_Context_Vars(REBCTX *c, REBNOD *keysource) {
    REBSER *stub = SER(c);

    // Rather than memcpy() and touch up the header and info to remove
    // SERIES_INFO_HOLD put on by Enter_Native(), or NODE_FLAG_MANAGED,
    // etc.--use constant assignments and only copy the remaining fields.
    //
    REBSER *copy = Alloc_Series_Node(
        SERIES_MASK_VARLIST
            | SERIES_FLAG_FIXED_SIZE
    );
    copy->info = Endlike_Header(
        FLAG_WIDE_BYTE_OR_0(0) // implicit termination, and indicates array
            | FLAG_LEN_BYTE_OR_255(255) // indicates dynamic (varlist rule)
    );
    TRASH_POINTER_IF_DEBUG(LINK_KEYSOURCE(copy)); // needs update
    memcpy(  // https://stackoverflow.com/q/57721104/
        cast(char*, &copy->content),
        cast(char*, &stub->content),
        sizeof(union Reb_Series_Content)
    );
    MISC_META_NODE(copy) = nullptr;  // let stub have the meta

    REBVAL *rootvar = cast(REBVAL*, copy->content.dynamic.data);

    // Convert the old varlist that had outstanding references into a
    // singular "stub", holding only the CTX_ARCHETYPE.  This is needed
    // for the ->binding to allow Derelativize(), see SPC_BINDING().
    //
    // Note: previously this had to preserve VARLIST_FLAG_FRAME_FAILED, but
    // now those marking failure are asked to do so manually to the stub
    // after this returns (hence they need to cache the varlist first).
    //
    stub->info = Endlike_Header(
        SERIES_INFO_INACCESSIBLE // args memory now "stolen" by copy
            | FLAG_WIDE_BYTE_OR_0(0) // width byte is 0 for array series
            | FLAG_LEN_BYTE_OR_255(1) // not dynamic any more, new len is 1
    );

    REBVAL *single = cast(REBVAL*, &stub->content.fixed);
    single->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_KIND_BYTE(REB_FRAME)
            | FLAG_MIRROR_BYTE(REB_FRAME)
            | CELL_MASK_CONTEXT;
    INIT_BINDING(single, VAL_BINDING(rootvar));
    INIT_VAL_CONTEXT_VARLIST(single, ARR(stub));
    TRASH_POINTER_IF_DEBUG(PAYLOAD(Any, single).second.node);  // phase

    INIT_VAL_CONTEXT_VARLIST(rootvar, ARR(copy));

    // Disassociate the stub from the frame, by degrading the link field
    // to a keylist.  !!! Review why this was needed, vs just nullptr
    //
    INIT_LINK_KEYSOURCE(CTX(stub), keysource);

    return CTX(copy);
}
