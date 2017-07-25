//
//  File: %sys-context.h
//  Summary: {Definitions for REBCTX}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

struct Reb_Context {
    struct Reb_Array varlist; // keylist is held in ->link.keylist
};

#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
#endif

// Series-to-Frame coercion, see notes in %sys-array.h header
//
inline static REBCTX *CTX(void *p) {
    REBARR *a = ARR(p);
    assert(GET_SER_FLAG(a, ARRAY_FLAG_VARLIST));
    return cast(REBCTX*, a);
}

inline static REBARR *CTX_VARLIST(REBCTX *c) {
    return &c->varlist;
}


//
// Special property: keylist pointer is stored in the misc field of REBSER
//

inline static REBARR *CTX_KEYLIST(REBCTX *c) {
    return SER(CTX_VARLIST(c))->link.keylist;
}

static inline void INIT_CTX_KEYLIST_SHARED(REBCTX *c, REBARR *keylist) {
    SET_SER_INFO(keylist, SERIES_INFO_SHARED_KEYLIST);
    SER(CTX_VARLIST(c))->link.keylist = keylist;
}

static inline void INIT_CTX_KEYLIST_UNIQUE(REBCTX *c, REBARR *keylist) {
    assert(NOT_SER_INFO(keylist, SERIES_INFO_SHARED_KEYLIST));
    SER(CTX_VARLIST(c))->link.keylist = keylist;
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
    (ARR_LEN(CTX_KEYLIST(c)) - 1)

#define CTX_ROOTKEY(c) \
    SER_HEAD(REBVAL, SER(CTX_KEYLIST(c)))

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_VALUE(c))

// The keys and vars are accessed by positive integers starting at 1.  If
// indexed access is used then the debug build will check to be sure that
// the indexing is legal.  To get a pointer to the first key or value
// regardless of length (e.g. will be an END if 0 keys/vars) use HEAD
//
// Rather than use ARR_AT (which returns RELVAL*) for the vars, this uses
// SER_AT to get REBVALs back, because the values of the context are known to
// not live in function body arrays--hence they can't hold relative words.
// Keys can't hold relative values either.
//
inline static REBVAL *CTX_KEYS_HEAD(REBCTX *c) {
    return SER_AT(REBVAL, SER(CTX_KEYLIST(c)), 1);
}

// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// REBSER node data itself.
//
inline static REBVAL *CTX_VALUE(REBCTX *c) {
    return GET_SER_INFO(CTX_VARLIST(c), CONTEXT_INFO_STACK)
        ? KNOWN(&SER(CTX_VARLIST(c))->content.values[0])
        : KNOWN(ARR_HEAD(CTX_VARLIST(c))); // not a RELVAL
}

inline static REBFRM *CTX_FRAME_IF_ON_STACK(REBCTX *c) {
    assert(IS_FRAME(CTX_VALUE(c)));
    REBFRM *f = SER(CTX_VARLIST(c))->misc.f;
    assert(
        f == NULL
        || (
            f->eval_type <= REB_FUNCTION
            && f->label != NULL
        ) // Note: inlining of Is_Any_Function_Frame() to break dependency
    );
    return f;
}

inline static REBVAL *CTX_VARS_HEAD(REBCTX *c) {
    if (NOT(GET_SER_INFO(CTX_VARLIST(c), CONTEXT_INFO_STACK)))
        return KNOWN(ARR_AT(CTX_VARLIST(c), 1));

    REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
    assert(f != NULL);
    return f->args_head;
}

inline static REBVAL *CTX_KEY(REBCTX *c, REBCNT n) {
    assert(n != 0 && n <= CTX_LEN(c));
    REBVAL *key = CTX_KEYS_HEAD(c) + (n) - 1;
    assert(key->extra.key_spelling != NULL);
    return key;
}

inline static REBVAL *CTX_VAR(REBCTX *c, REBCNT n) {
    REBVAL *var;
    assert(n != 0 && n <= CTX_LEN(c));
    assert(GET_SER_FLAG(CTX_VARLIST(c), ARRAY_FLAG_VARLIST));

    var = CTX_VARS_HEAD(c) + (n) - 1;

    assert(NOT(var->header.bits & VALUE_FLAG_RELATIVE));

    return var;
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

inline static REBCTX *CTX_META(REBCTX *c) {
    return SER(CTX_KEYLIST(c))->link.meta;
}

#define FAIL_IF_READ_ONLY_CONTEXT(c) \
    FAIL_IF_READ_ONLY_ARRAY(CTX_VARLIST(c))

inline static void FREE_CONTEXT(REBCTX *c) {
    Free_Array(CTX_KEYLIST(c));
    Free_Array(CTX_VARLIST(c));
}

#define PUSH_GUARD_CONTEXT(c) \
    PUSH_GUARD_ARRAY(CTX_VARLIST(c)) // varlist points to/guards keylist

#define DROP_GUARD_CONTEXT(c) \
    DROP_GUARD_ARRAY(CTX_VARLIST(c))


inline static REBOOL CTX_VARS_UNAVAILABLE(REBCTX *c) {
    //
    // Mechanically any array can become inaccessible, but really the varlist
    // of a stack context is the only case that should happen today.
    //
    if (GET_SER_INFO(CTX_VARLIST(c), SERIES_INFO_INACCESSIBLE)) {
        assert(GET_SER_INFO(CTX_VARLIST(c), CONTEXT_INFO_STACK));
        return TRUE;
    }
    return FALSE;
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

#ifdef NDEBUG
    #define ANY_CONTEXT_FLAG(n) \
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n))
#else
    #define ANY_CONTEXT_FLAG(n) \
        (FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_OBJECT))
#endif

// `ANY_CONTEXT_FLAG_OWNS_PAIRED` is particular to the idea of a "Paired"
// REBSER, which is actually just two REBVALs.  For purposes of the API,
// it is possible for one of those values to be used to manage the
// lifetime of the pair.  One technique is to tie the value's lifetime
// to that of a particular FRAME!
//
#define ANY_CONTEXT_FLAG_OWNS_PAIRED ANY_CONTEXT_FLAG(0)


inline static REBCTX *VAL_CONTEXT(const RELVAL *v) {
    assert(ANY_CONTEXT(v));
    assert(v->payload.any_context.phase == NULL || VAL_TYPE(v) == REB_FRAME);
    return CTX(v->payload.any_context.varlist);
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

inline static REBCTX *VAL_CONTEXT_META(const RELVAL *v) {
    return SER(
        CTX_KEYLIST(CTX(v->payload.any_context.varlist))
    )->link.meta;
}

#define VAL_CONTEXT_KEY_SYM(v,n) \
    CTX_KEY_SYM(VAL_CONTEXT(v), (n))

inline static void INIT_CONTEXT_META(REBCTX *c, REBCTX *m) {
    SER(CTX_KEYLIST(c))->link.meta = m;
}

inline static REBVAL *CTX_FRAME_FUNC_VALUE(REBCTX *c) {
    assert(IS_FUNCTION(CTX_ROOTKEY(c)));
    return CTX_ROOTKEY(c);
}

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

#define Init_Any_Context(out,kind,context) \
    Init_Any_Context_Core((out), (kind), (context))

#define Init_Object(v,c) \
    Init_Any_Context((v), REB_OBJECT, (c))

#define Init_Port(v,c) \
    Init_Any_Context((v), REB_PORT, (c))


//=////////////////////////////////////////////////////////////////////////=//
//
// COMMON INLINES (macro-like)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// By putting these functions in a header file, they can be inlined by the
// compiler, rather than add an extra layer of function call.
//

#define Copy_Context_Shallow(src) \
    Copy_Context_Shallow_Extra((src), 0)

// Returns true if the keylist had to be changed to make it unique.
//
#define Ensure_Keylist_Unique_Invalidated(context) \
    Expand_Context_Keylist_Core((context), 0)


//=////////////////////////////////////////////////////////////////////////=//
//
// FIELD SELECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// For performance reasons, most code within the core does not use lookups
// by symbol in objects.  The specific objects the core deals with (e.g. in
// %sysobj.r) have the indexes hardcoded for the fields it wants to access,
// so it can just use CTX_VAR() to get the pointer directly, without needing
// to canonize symbols or walk the keylist.  These routines are provided as
// a convenience.
//

#define Get_Field(c, spelling) \
    Get_Typed_Field((c), (spelling), REB_0) // will canonize

#define Sink_Field(c, spelling) \
    SINK(Get_Typed_Field(c, (spelling), REB_0)) // will canonize


//=////////////////////////////////////////////////////////////////////////=//
//
// LOCKING
//
//=////////////////////////////////////////////////////////////////////////=//

inline static void Deep_Freeze_Context(REBCTX *c) {
    Protect_Context(
        c,
        FLAGIT(PROT_SET) | FLAGIT(PROT_DEEP) | FLAGIT(PROT_FREEZE)
    );
    Uncolor_Array(CTX_VARLIST(c));
}

inline static REBOOL Is_Context_Deeply_Frozen(REBCTX *c) {
    return GET_SER_INFO(CTX_VARLIST(c), SERIES_INFO_FROZEN);
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

inline static REBCNT ERR_NUM(REBCTX *e) {
    ERROR_VARS* vars = ERR_VARS(e);
    if (IS_BLANK(&vars->code))
        return RE_USER;
    assert(IS_INTEGER(&vars->code)); // Note: C build doesn't check VAL_INT32
    return cast(REBCNT, VAL_INT32(&vars->code));
}

#define VAL_ERR_VARS(v) \
    ERR_VARS(VAL_CONTEXT(v))

#define VAL_ERR_NUM(v) \
    ERR_NUM(VAL_CONTEXT(v))

#define Init_Error(v,c) \
    Init_Any_Context((v), REB_ERROR, (c))


// Ports are unusual hybrids of user-mode code dispatched with native code, so
// some things the user can do to the internals of a port might cause the
// C code to crash.  This wasn't very well thought out in R3-Alpha, but there
// was some validation checking.  This factors out that check instead of
// repeating the code.
//
inline static void FAIL_IF_BAD_PORT(REBCTX *port) {
    assert(GET_SER_FLAG(CTX_VARLIST(port), ARRAY_FLAG_VARLIST));

    if (
        (CTX_LEN(port) < STD_PORT_MAX - 1) ||
        !IS_OBJECT(CTX_VAR(port, STD_PORT_SPEC))
    ) {
        fail (Error_Invalid_Port_Raw());
    }
}

// It's helpful to show when a test for a native port actor is being done,
// rather than just having the code say IS_HANDLE().
//
inline static REBOOL Is_Native_Port_Actor(const REBVAL *actor) {
    if (IS_HANDLE(actor))
        return TRUE;
    assert(IS_OBJECT(actor));
    return FALSE;
}
