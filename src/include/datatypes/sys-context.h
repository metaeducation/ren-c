//
//  File: %sys-context.h
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
//   "KEYLIST" - a series of pointer-sized elements to REBSTR* symbols.
//
//   "VARLIST" - an array which holds an archetypal ANY-CONTEXT! value in its
//   [0] element, and then a cell-sized slot for each variable.
//
// A `REBCTX*` is an alias of the varlist's `REBARR*`, and keylists are
// reached through the `->link` of the varlist.  The reason varlists
// are used as the identity of the context is that keylists can be shared
// between contexts.
//
// Indices into the arrays are 0-based for keys and 1-based for values, with
// the [0] elements of the varlist used an archetypal value:
//
//    VARLIST ARRAY (aka REBCTX*)  ---Link--+
//  +------------------------------+        |
//  +          "ROOTVAR"           |        |
//  | Archetype ANY-CONTEXT! Value |        v         KEYLIST SERIES
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! 1      |        |     REBSTR* key symbol  1     |
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! 2      |        |     REBSTR* key symbol 2      |
//  +------------------------------+        +-------------------------------+
//  |      <opt> ANY-VALUE! ...    |        |     REBSTR* key symbol ...    |
//  +------------------------------+        +-------------------------------+
//
// (For executing frames, the ---Link--> is actually to the REBFRM* structure
// so the paramlist of the CTX_FRAME_ACTION() must be consulted.  When the
// frame stops running, the paramlist is written back to the link again.)
//
// The "ROOTVAR" is a canon value image of an ANY-CONTEXT!'s `REBVAL`.  This
// trick allows a single REBCTX* pointer to be passed around rather than the
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
// * R3-Alpha used a special kind of WORD! known as an "unword" for the
//   keylist keys.  Ren-C uses values whose "heart byte" are TYPESET!, but use
//   a kind byte that makes them a "Param".  They can also hold a symbol, as
//   this made certain kinds of corruption less likely.  The design is likely
//   to change as TYPESET! is slated to be replaced with "type predicates".
//
// * Since varlists and keylists always have more than one element, they are
//   allocated with SERIES_FLAG_DYNAMIC and do not need to check for whether
//   the singular optimization when being used.  This does not apply when a
//   varlist becomes invalid (e.g. via FREE), when its data allocation is
//   released and it is decayed to a singular.
//


#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
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


// REBCTX* properties (note: shares LINK_KEYSOURCE() with REBACT*)
//
// Note: MODULE! contexts depend on a property stored in the META field, which
// is another object's-worth of data *about* the module's contents (e.g. the
// processed header)
//
#define CTX_META(c)     MISC(VarlistMeta, CTX_VARLIST(c))

#define BONUS_Patches_TYPE      REBARR*
#define BONUS_Patches_CAST      ARR
#define HAS_BONUS_Patches       FLAVOR_VARLIST


// ANY-CONTEXT! value cell schematic
//
#define VAL_CONTEXT_VARLIST(v)                  ARR(VAL_NODE1(v))
#define INIT_VAL_CONTEXT_VARLIST                INIT_VAL_NODE1
#define VAL_FRAME_PHASE_OR_LABEL_NODE           VAL_NODE2  // faster in debug
#define VAL_FRAME_PHASE_OR_LABEL(v)             SER(VAL_NODE2(v))
#define INIT_VAL_FRAME_PHASE_OR_LABEL           INIT_VAL_NODE2


//=//// CONTEXT ARCHETYPE VALUE CELL (ROOTVAR)  ///////////////////////////=//
//
// A REBVAL* must contain enough information to find what is needed to define
// a context.  That fact is leveraged by the notion of keeping the information
// in the context itself as the [0] element of the varlist.  This means it is
// always on hand when a REBVAL* is needed, so you can do things like:
//
//     REBCTX *c = ...;
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
// For the moment that is done with the CTX_META() field instead.
//

inline static const REBVAL *CTX_ARCHETYPE(REBCTX *c) {  // read-only form
    const REBSER *varlist = CTX_VARLIST(c);
    if (not IS_SER_DYNAMIC(varlist)) {  // a freed stub, variables are gone
        assert(GET_SERIES_FLAG(varlist, INACCESSIBLE));
        return cast(const REBVAL*, &varlist->content.fixed);
    }
    assert(NOT_SERIES_FLAG(varlist, INACCESSIBLE));
    return cast(const REBVAL*, varlist->content.dynamic.data);
}

inline static REBVAL *CTX_ROOTVAR(REBCTX *c)  // mutable archetype access
  { return m_cast(REBVAL*, CTX_ARCHETYPE(c)); }  // inline checks mutability

inline static REBACT *CTX_FRAME_ACTION(REBCTX *c) {
    const REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_TYPE(archetype) == REB_FRAME);
    return ACT(VAL_FRAME_PHASE_OR_LABEL_NODE(archetype));
}

inline static REBCTX *CTX_FRAME_BINDING(REBCTX *c) {
    const REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_TYPE(archetype) == REB_FRAME);
    return CTX(BINDING(archetype));
}

inline static void INIT_VAL_CONTEXT_ROOTVAR_Core(
    RELVAL *out,
    enum Reb_Kind kind,
    REBARR *varlist
){
    assert(kind != REB_FRAME);  // use INIT_VAL_FRAME_ROOTVAR() instead
    assert(out == ARR_HEAD(varlist));
    RESET_CELL(out, kind, CELL_MASK_CONTEXT);
    INIT_VAL_CONTEXT_VARLIST(out, varlist);
    mutable_BINDING(out) = UNBOUND;  // not a frame
    INIT_VAL_FRAME_PHASE_OR_LABEL(out, nullptr);  // not a frame
  #if !defined(NDEBUG)
    out->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define INIT_VAL_CONTEXT_ROOTVAR(out,kind,varlist) \
    INIT_VAL_CONTEXT_ROOTVAR_Core( \
        TRACK_CELL_IF_DEBUG(out), (kind), (varlist))

inline static void INIT_VAL_FRAME_ROOTVAR_Core(
    RELVAL *out,
    REBARR *varlist,
    REBACT *phase,
    REBCTX *binding  // allowed to be UNBOUND
){
    assert(
        (GET_SERIES_FLAG(varlist, INACCESSIBLE) and out == ARR_SINGLE(varlist))
        or out == ARR_HEAD(varlist)
    );
    assert(phase != nullptr);
    RESET_VAL_HEADER(out, REB_FRAME, CELL_MASK_CONTEXT);
    INIT_VAL_CONTEXT_VARLIST(out, varlist);
    mutable_BINDING(out) = binding;
    INIT_VAL_FRAME_PHASE_OR_LABEL(out, phase);
  #if !defined(NDEBUG)
    out->header.bits |= CELL_FLAG_PROTECTED;
  #endif
}

#define INIT_VAL_FRAME_ROOTVAR(out,varlist,phase,binding) \
    INIT_VAL_FRAME_ROOTVAR_Core( \
        TRACK_CELL_IF_DEBUG(out), (varlist), (phase), (binding))


//=//// CONTEXT KEYLISTS //////////////////////////////////////////////////=//
//
// If a context represents a FRAME! that is currently executing, one often
// needs to quickly navigate to the REBFRM* structure for the corresponding
// stack level.  This is sped up by swapping the REBFRM* into the LINK() of
// the varlist until the frame is finished.  In this state, the paramlist of
// the FRAME! action is consulted. When the action is finished, this is put
// back in LINK_KEYSOURCE().
//
// Note: Due to the sharing of keylists, features like whether a value in a
// context is hidden or protected are accomplished using special bits on the
// var cells, and *not the keys*.  These bits are not copied when the value
// is moved (see CELL_MASK_COPIED regarding this mechanic)
//

inline static REBSER *CTX_KEYLIST(REBCTX *c) {
    if (Is_Node_Cell(LINK(KeySource, CTX_VARLIST(c)))) {
        //
        // running frame, source is REBFRM*, so use action's paramlist.
        //
        return ACT_KEYLIST(CTX_FRAME_ACTION(c));
    }
    return SER(LINK(KeySource, CTX_VARLIST(c)));  // not a REBFRM, use keylist
}

static inline void INIT_CTX_KEYLIST_SHARED(REBCTX *c, REBSER *keylist) {
    SET_SUBCLASS_FLAG(KEYLIST, keylist, SHARED);
    INIT_LINK_KEYSOURCE(CTX_VARLIST(c), keylist);
}

static inline void INIT_CTX_KEYLIST_UNIQUE(REBCTX *c, REBSER *keylist) {
    assert(NOT_SUBCLASS_FLAG(KEYLIST, keylist, SHARED));
    INIT_LINK_KEYSOURCE(CTX_VARLIST(c), keylist);
}


//=//// REBCTX* ACCESSORS /////////////////////////////////////////////////=//
//
// These are access functions that should be used when what you have in your
// hand is just a REBCTX*.  THIS DOES NOT ACCOUNT FOR PHASE...so there can
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

inline static REBLEN CTX_LEN(REBCTX *c) {
    return CTX_VARLIST(c)->content.dynamic.used - 1;  // -1 for archetype
}

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_ARCHETYPE(c))

inline static const REBKEY *CTX_KEY(REBCTX *c, REBLEN n) {
    //
    // !!! Inaccessible contexts have to retain their keylists, at least
    // until all words bound to them have been adjusted somehow, because the
    // words depend on those keys for their spellings (once bound)
    //
    /* assert(NOT_SERIES_FLAG(c, INACCESSIBLE)); */

    assert(n != 0 and n <= CTX_LEN(c));
    return SER_AT(const REBKEY, CTX_KEYLIST(c), n - 1);
}

inline static REBVAR *CTX_VAR(REBCTX *c, REBLEN n) {  // 1-based, no RELVAL*
    assert(NOT_SERIES_FLAG(CTX_VARLIST(c), INACCESSIBLE));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(REBVAR*, cast(REBSER*, c)->content.dynamic.data) + n;
}

// CTX_VARS_HEAD() and CTX_KEYS_HEAD() allow CTX_LEN() to be 0, while
// CTX_VAR() does not.  Also, CTX_KEYS_HEAD() gives back a mutable slot.

#define CTX_KEYS_HEAD(c) \
    SER_AT(REBKEY, CTX_KEYLIST(c), 0)  // 0-based

#define CTX_VARS_HEAD(c) \
    (cast(REBVAR*, cast(REBSER*, (c))->content.dynamic.data) + 1)

inline static const REBKEY *CTX_KEYS(const REBKEY ** tail, REBCTX *c) {
    REBSER *keylist = CTX_KEYLIST(c);
    *tail = SER_TAIL(REBKEY, keylist);
    return SER_HEAD(REBKEY, keylist);
}

inline static REBVAR *CTX_VARS(const REBVAR ** tail, REBCTX *c) {
    REBVAR *head = CTX_VARS_HEAD(c);
    *tail = head + cast(REBSER*, (c))->content.dynamic.used - 1;
    return head;
}


//=//// FRAME! REBCTX* <-> REBFRM* STRUCTURE //////////////////////////////=//
//
// For a FRAME! context, the keylist is redundant with the paramlist of the
// CTX_FRAME_ACTION() that the frame is for.  That is taken advantage of when
// a frame is executing in order to use the LINK() keysource to point at the
// running REBFRM* structure for that stack level.  This provides a cheap
// way to navigate from a REBCTX* to the REBFRM* that's running it.
//

inline static bool Is_Frame_On_Stack(REBCTX *c) {
    assert(IS_FRAME(CTX_ARCHETYPE(c)));
    return Is_Node_Cell(LINK(KeySource, CTX_VARLIST(c)));
}

inline static REBFRM *CTX_FRAME_IF_ON_STACK(REBCTX *c) {
    REBNOD *keysource = LINK(KeySource, CTX_VARLIST(c));
    if (not Is_Node_Cell(keysource))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(NOT_SERIES_FLAG(CTX_VARLIST(c), INACCESSIBLE));
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

inline static void FAIL_IF_INACCESSIBLE_CTX(REBCTX *c) {
    if (GET_SERIES_FLAG(CTX_VARLIST(c), INACCESSIBLE)) {
        if (CTX_TYPE(c) == REB_FRAME)
            fail (Error_Expired_Frame_Raw()); // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }
}


//=//// CONTEXT EXTRACTION ////////////////////////////////////////////////=//
//
// Extraction of a context from a value is a place where it is checked for if
// it is valid or has been "decayed" into a stub.  Thus any extraction of
// stored contexts from other locations (e.g. a META field) must either put
// the pointer directly into a value without dereferencing it and trust it to
// be checked elsewhere...or also check it before use.
//

inline static REBCTX *VAL_CONTEXT(REBCEL(const*) v) {
    assert(ANY_CONTEXT_KIND(CELL_HEART(v)));
    REBCTX *c = CTX(VAL_NODE1(v));
    FAIL_IF_INACCESSIBLE_CTX(c);
    return c;
}


//=//// FRAME BINDING /////////////////////////////////////////////////////=//
//
// Only FRAME! contexts store bindings at this time.  The reason is that a
// unique binding can be stored by individual ACTION! values, so when you make
// a frame out of an action it has to preserve that binding.
//
// Note: The presence of bindings in non-archetype values makes it possible
// for FRAME! values that have phases to carry the binding of that phase.
// This is a largely unexplored feature, but is used in REDO scenarios where
// a running frame gets re-executed.  More study is needed.
//

inline static void INIT_VAL_FRAME_BINDING(RELVAL *v, REBCTX *binding) {
    assert(
        IS_FRAME(v)  // may be marked protected (e.g. archetype)
        or IS_ACTION(v)  // used by UNWIND
    );
    EXTRA(Binding, v) = binding;
}

inline static REBCTX *VAL_FRAME_BINDING(REBCEL(const*) v) {
    assert(REB_FRAME == CELL_HEART(v));
    return CTX(BINDING(v));
}


//=//// FRAME PHASE AND LABELING //////////////////////////////////////////=//
//
// A frame's phase is usually a pointer to the component action in effect for
// a composite function (e.g. an ADAPT).
//
// But if the node where a phase would usually be found is a REBSTR* then that
// implies there isn't any special phase besides the action stored by the
// archetype.  Hence the value cell is storing a name to be used with the
// action when it is extracted from the frame.  That's why this works:
//
//     >> f: make frame! :append
//     >> label of f
//     == append  ; useful in debug stack traces if you `do f`
//
// So extraction of the phase has to be sensitive to this.
//

inline static void INIT_VAL_FRAME_PHASE(RELVAL *v, REBACT *phase) {
    assert(IS_FRAME(v));  // may be marked protected (e.g. archetype)
    INIT_VAL_FRAME_PHASE_OR_LABEL(v, phase);
}

inline static REBACT *VAL_FRAME_PHASE(REBCEL(const*) v) {
    REBSER *s = VAL_FRAME_PHASE_OR_LABEL(v);
    if (not s or IS_SYMBOL(s))  // ANONYMOUS or label, not a phase
        return CTX_FRAME_ACTION(VAL_CONTEXT(v));  // so use archetype
    return ACT(s);  // cell has its own phase, return it
}

inline static bool IS_FRAME_PHASED(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_FRAME);
    REBSER *s = VAL_FRAME_PHASE_OR_LABEL(v);
    return s and not IS_SYMBOL(s);
}

inline static option(const REBSYM*) VAL_FRAME_LABEL(const RELVAL *v) {
    REBSER *s = VAL_FRAME_PHASE_OR_LABEL(v);
    if (s and IS_SYMBOL(s))  // label in value
        return SYM(s);
    return ANONYMOUS;  // has a phase, so no label (maybe findable if running)
}

inline static void INIT_VAL_FRAME_LABEL(
    RELVAL *v,
    option(const REBSTR*) label
){
    assert(IS_FRAME(v));
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v);  // No label in archetype
    INIT_VAL_FRAME_PHASE_OR_LABEL(v, try_unwrap(label));
}


//=//// ANY-CONTEXT! VALUE EXTRACTORS /////////////////////////////////////=//
//
// There once were more helpers like `VAL_CONTEXT_VAR(v,n)` which were macros
// for things like `CTX_VAR(VAL_CONTEXT(v), n)`.  However, once VAL_CONTEXT()
// became a test point for failure on inaccessibility, it's not desirable to
// encourage calling with repeated extractions that pay that cost each time.
//
// However, this does not mean that all functions should early extract a
// VAL_CONTEXT() and then do all operations in terms of that...because this
// potentially loses information present in the RELVAL* cell.  If the value
// is a frame, then the phase information conveys which fields should be
// visible for that phase of execution and which aren't.
//

inline static const REBKEY *VAL_CONTEXT_KEYS_HEAD(REBCEL(const*) context)
{
    if (CELL_KIND(context) != REB_FRAME)
        return CTX_KEYS_HEAD(VAL_CONTEXT(context));

    REBACT *phase = VAL_FRAME_PHASE(context);
    return ACT_KEYS_HEAD(phase);
}

#define VAL_CONTEXT_VARS_HEAD(context) \
    CTX_VARS_HEAD(VAL_CONTEXT(context))  // all views have same varlist


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
    return Copy_Cell(out, CTX_ARCHETYPE(c));
}

#define Init_Object(out,c) \
    Init_Any_Context((out), REB_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Any_Context((out), REB_PORT, (c))

inline static REBVAL *Init_Frame(
    RELVAL *out,
    REBCTX *c,
    option(const REBSTR*) label  // nullptr (ANONYMOUS) is okay
){
    Init_Any_Context(out, REB_FRAME, c);
    INIT_VAL_FRAME_LABEL(out, label);
    return cast(REBVAL*, out);
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
    REBSER *stub = CTX_VARLIST(c);

    // Rather than memcpy() and touch up the header and info to remove
    // SERIES_INFO_HOLD from DETAILS_FLAG_IS_NATIVE, or NODE_FLAG_MANAGED,
    // etc.--use constant assignments and only copy the remaining fields.
    //
    REBSER *copy = Alloc_Series_Node(
        SERIES_MASK_VARLIST
            | SERIES_FLAG_FIXED_SIZE
    );
    SER_INFO(copy) = SERIES_INFO_MASK_NONE;
    TRASH_POINTER_IF_DEBUG(node_LINK(KeySource, copy)); // needs update
    memcpy(  // https://stackoverflow.com/q/57721104/
        cast(char*, &copy->content),
        cast(char*, &stub->content),
        sizeof(union Reb_Series_Content)
    );
    mutable_MISC(VarlistMeta, copy) = nullptr;  // let stub have the meta
    mutable_BONUS(Patches, copy) = nullptr;  // don't carry forward patches

    REBVAL *rootvar = cast(REBVAL*, copy->content.dynamic.data);

    // Convert the old varlist that had outstanding references into a
    // singular "stub", holding only the CTX_ARCHETYPE.  This is needed
    // for the ->binding to allow Derelativize(), see SPC_BINDING().
    //
    // Note: previously this had to preserve VARLIST_FLAG_FRAME_FAILED, but
    // now those marking failure are asked to do so manually to the stub
    // after this returns (hence they need to cache the varlist first).
    //
    SET_SERIES_FLAG(stub, INACCESSIBLE);

    REBVAL *single = cast(REBVAL*, &stub->content.fixed);
    single->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_KIND3Q_BYTE(REB_FRAME)
            | FLAG_HEART_BYTE(REB_FRAME)
            | CELL_MASK_CONTEXT;
    INIT_VAL_CONTEXT_VARLIST(single, ARR(stub));
    INIT_VAL_FRAME_BINDING(single, VAL_FRAME_BINDING(rootvar));

  #if !defined(DEBUG)
    INIT_VAL_FRAME_PHASE_OR_LABEL(single, nullptr);  // can't trash
  #endif

    INIT_VAL_CONTEXT_VARLIST(rootvar, ARR(copy));

    // Disassociate the stub from the frame, by degrading the link field
    // to a keylist.  !!! Review why this was needed, vs just nullptr
    //
    INIT_LINK_KEYSOURCE(ARR(stub), keysource);

    CLEAR_SERIES_FLAG(stub, DYNAMIC);  // mark stub as no longer dynamic

    return CTX(copy);
}
