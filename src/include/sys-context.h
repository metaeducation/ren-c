//
//  File: %sys-context.h
//  Summary: {context! defs AFTER %tmp-internals.h (see: %sys-context.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// In Rebol terminology, a "context" is an abstraction which gives two
// parallel arrays, whose indices line up in a correspondence:
//
// * "keylist" - an array that contains TYPESET! values, but which have a
//   symbol ID encoded as an extra piece of information for that key.
//
// * "varlist" - an array of equal length to the keylist, which holds an
//   arbitrary REBVAL in each position that corresponds to its key.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's series pointer.  Then they cache the index of that
// word's symbol in the context's keylist, for a fast lookup to get to the
// corresponding var.  The key is a typeset which has several flags
// controlling behaviors like whether the var is protected or hidden.
//
// !!! This "caching" mechanism is not actually "just a cache".  Once bound
// the index is treated as permanent.  This is why objects are "append only"
// because disruption of the index numbers would break the extant words
// with index numbers to that position.  Ren-C might wind up undoing this by
// paying for the check of the symbol number at the time of lookup, and if
// it does not match consider it a cache miss and re-lookup...adjusting the
// index inside of the word.  For efficiency, some objects could be marked
// as not having this property, but it may be just as efficient to check
// the symbol match as that bit.
//
// Frame key/var indices start at one, and they leave two REBVAL slots open
// in the 0 spot for other uses.  With an ANY-CONTEXT!, the use for the
// "ROOTVAR" is to store a canon value image of the ANY-CONTEXT!'s REBVAL
// itself.  This trick allows a single REBCTX* to be passed around rather
// than the REBVAL struct which is 4x larger, yet still reconstitute the
// entire REBVAL if it is needed.
//

#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
#endif

#define CTX_VARLIST(c) \
    (&(c)->varlist)


// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// REBSER node data itself.
//
inline static REBVAL *CTX_ARCHETYPE(REBCTX *c) {
    REBSER *varlist = SER(CTX_VARLIST(c));
    if (not IS_SER_DYNAMIC(varlist))
        return cast(REBVAL*, &varlist->content.fixed);

    // If a context has its data freed, it must be converted into non-dynamic
    // form if it wasn't already (e.g. if it wasn't a FRAME!)
    //
    assert(NOT_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE));
    return cast(REBVAL*, varlist->content.dynamic.data);
}

// CTX_KEYLIST is called often, and it's worth it to make it as fast as
// possible--even in an unoptimized build.
//
inline static REBARR *CTX_KEYLIST(REBCTX *c) {
    if (not (LINK(c).keysource->header.bits & NODE_FLAG_CELL))
        return ARR(LINK(c).keysource); // not a REBFRM, so use keylist

    // If the context in question is a FRAME! value, then the ->phase
    // of the frame presents the "view" of which keys should be visible at
    // this phase.  So if the phase is a specialization, then it should
    // not show all the underlying function's keys...just the ones that
    // are not hidden in the facade that specialization uses.  Since the
    // phase changes, a fixed value can't be put into the keylist...that is
    // just the keylist of the underlying function.
    //
    REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_TYPE_RAW(archetype) == REB_FRAME);
    return ACT_PARAMLIST(archetype->payload.any_context.phase);
}

static inline void INIT_CTX_KEYLIST_SHARED(REBCTX *c, REBARR *keylist) {
    SET_SER_INFO(keylist, SERIES_INFO_SHARED_KEYLIST);
    LINK(c).keysource = NOD(keylist);
}

static inline void INIT_CTX_KEYLIST_UNIQUE(REBCTX *c, REBARR *keylist) {
    assert(NOT_SER_INFO(keylist, SERIES_INFO_SHARED_KEYLIST));
    LINK(c).keysource = NOD(keylist);
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
    (cast(REBSER*, (c))->content.dynamic.len - 1) // len > 1 => dynamic

#define CTX_ROOTKEY(c) \
    cast(REBVAL*, SER(CTX_KEYLIST(c))->content.dynamic.data) // len > 1

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_ARCHETYPE(c))

// The keys and vars are accessed by positive integers starting at 1
//
#define CTX_KEYS_HEAD(c) \
    SER_AT(REBVAL, SER(CTX_KEYLIST(c)), 1) // a CTX_KEY can't hold a RELVAL

inline static REBFRM *CTX_FRAME_IF_ON_STACK(REBCTX *c) {
    REBNOD *keysource = LINK(c).keysource;
    if (not (keysource->header.bits & NODE_FLAG_CELL))
        return nullptr; // e.g. came from MAKE FRAME! or Encloser_Dispatcher

    assert(NOT_SER_INFO(CTX_VARLIST(c), SERIES_INFO_INACCESSIBLE));
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

inline static REBVAL *CTX_KEY(REBCTX *c, REBCNT n) {
    assert(NOT_SER_FLAG(c, SERIES_INFO_INACCESSIBLE));
    assert(GET_SER_FLAG(c, ARRAY_FLAG_VARLIST));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(REBVAL*, cast(REBSER*, CTX_KEYLIST(c))->content.dynamic.data)
        + n;
}

inline static REBVAL *CTX_VAR(REBCTX *c, REBCNT n) {
    assert(NOT_SER_FLAG(c, SERIES_INFO_INACCESSIBLE));
    assert(GET_SER_FLAG(c, ARRAY_FLAG_VARLIST));
    assert(n != 0 and n <= CTX_LEN(c));
    return cast(REBVAL*, cast(REBSER*, c)->content.dynamic.data) + n;
}

inline static REBSTR *CTX_KEY_SPELLING(REBCTX *c, REBCNT n) {
    return CTX_KEY(c, n)->extra.key_spelling;
}

inline static REBSTR *CTX_KEY_CANON(REBCTX *c, REBCNT n) {
    return STR_CANON(CTX_KEY_SPELLING(c, n));
}

inline static REBSYM CTX_KEY_SYM(REBCTX *c, REBCNT n) {
    return STR_SYMBOL(CTX_KEY_SPELLING(c, n)); // should be same as canon
}

#define FAIL_IF_READ_ONLY_CONTEXT(c) \
    FAIL_IF_READ_ONLY_ARRAY(CTX_VARLIST(c))

inline static void FREE_CONTEXT(REBCTX *c) {
    Free_Unmanaged_Array(CTX_KEYLIST(c));
    Free_Unmanaged_Array(CTX_VARLIST(c));
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
    if (GET_SER_INFO(c, SERIES_INFO_INACCESSIBLE)) {
        if (CTX_TYPE(c) == REB_FRAME)
            fail (Error_Do_Expired_Frame_Raw()); // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }
}

inline static REBCTX *VAL_CONTEXT(const RELVAL *v) {
    assert(ANY_CONTEXT(v));
    assert(not v->payload.any_context.phase or VAL_TYPE(v) == REB_FRAME);
    REBCTX *c = CTX(v->payload.any_context.varlist);
    FAIL_IF_INACCESSIBLE_CTX(c);
    return c;
}

inline static void INIT_VAL_CONTEXT(REBVAL *v, REBCTX *c) {
    v->payload.any_context.varlist = CTX_VARLIST(c);
}

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
    assert(IS_ARRAY_MANAGED(CTX_VARLIST(c)));
    assert(IS_ARRAY_MANAGED(CTX_KEYLIST(c)));
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

// Returns true if the keylist had to be changed to make it unique.
//
#define Ensure_Keylist_Unique_Invalidated(context) \
    Expand_Context_Keylist_Core((context), 0)

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

inline static bool Is_Context_Deeply_Frozen(REBCTX *c) {
    return GET_SER_INFO(c, SERIES_INFO_FROZEN);
}


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
        SERIES_MASK_CONTEXT
            | SERIES_FLAG_STACK
            | SERIES_FLAG_FIXED_SIZE
    );
    copy->info = Endlike_Header(
        FLAG_WIDE_BYTE_OR_0(0) // implicit termination, and indicates array
            | FLAG_LEN_BYTE_OR_255(255) // indicates dynamic (varlist rule)
    );
    TRASH_POINTER_IF_DEBUG(copy->link_private.keysource); // needs update
    memcpy(
        cast(char*, &copy->content),
        cast(char*, &stub->content),
        sizeof(union Reb_Series_Content)
    );
    copy->misc_private.meta = nullptr; // let stub have the meta

    REBVAL *rootvar = cast(REBVAL*, copy->content.dynamic.data);

    // Convert the old varlist that had outstanding references into a
    // singular "stub", holding only the CTX_ARCHETYPE.  This is needed
    // for the ->binding to allow Derelativize(), see SPC_BINDING().
    //
    // Note: previously this had to preserve FRAME_INFO_FAILED, but now
    // those marking failure are asked to do so manually to the stub
    // after this returns (hence they need to cache the varlist first).
    //
    stub->info = Endlike_Header(
        SERIES_INFO_INACCESSIBLE // args memory now "stolen" by copy
            | FLAG_WIDE_BYTE_OR_0(0) // width byte is 0 for array series
            | FLAG_LEN_BYTE_OR_255(1) // not dynamic any more, new len is 1
    );

    REBVAL *single = cast(REBVAL*, &stub->content.fixed);
    single->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL | FLAG_KIND_BYTE(REB_FRAME);
    INIT_BINDING(single, VAL_BINDING(rootvar));
    single->payload.any_context.varlist = ARR(stub);
    TRASH_POINTER_IF_DEBUG(single->payload.any_context.phase);
    /* single->payload.any_context.phase = f->original; */ // !!! needed?

    rootvar->payload.any_context.varlist = ARR(copy);

    // Disassociate the stub from the frame, by degrading the link field
    // to a keylist.  !!! Review why this was needed, vs just nullptr
    //
    LINK(stub).keysource = keysource;

    return CTX(copy);
}
