//
//  File: %sys-bind.h
//  Summary: "System Binding Include"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// R3-Alpha had a per-thread "bind table"; a large and sparsely populated hash
// into which index numbers would be placed, for what index those words would
// have as keys or parameters.  Ren-C's strategy is that binding information
// is wedged into REBSER nodes that represent the canon words themselves.
//
// This would create problems if multiple threads were trying to bind at the
// same time.  While threading was never realized in R3-Alpha, Ren-C doesn't
// want to have any "less of a plan".  So the Reb_Binder is used by binding
// clients as a placeholder for whatever actual state would be used to augment
// the information in the canon word series about which client is making a
// request.  This could be coupled with some kind of lockfree adjustment
// strategy whereby a word that was contentious would cause a structure to
// "pop out" and be pointed to by some atomic thing inside the word.
//
// For the moment, a binder has some influence by saying whether the high 16
// bits or low 16 bits of the canon's misc.index are used.  If the index
// were atomic this would--for instance--allow two clients to bind at once.
// It's just a demonstration of where more general logic using atomics
// that could work for N clients would be.
//
// The debug build also adds another feature, that makes sure the clear count
// matches the set count.
//

// Modes allowed by Bind related functions:
enum {
    BIND_0 = 0, // Only bind the words found in the context.
    BIND_DEEP = 1 << 1, // Recurse into sub-blocks.
    BIND_FUNC = 1 << 2 // Recurse into functions.
};


struct Reb_Binder {
    REBOOL high;
#if !defined(NDEBUG)
    REBCNT count;
#endif
};


inline static void INIT_BINDER(struct Reb_Binder *binder) {
    binder->high = TRUE; //LOGICAL(SPORADICALLY(2)); sporadic?
#if !defined(NDEBUG)
    binder->count = 0;
#endif
}


inline static void SHUTDOWN_BINDER(struct Reb_Binder *binder) {
    assert(binder->count == 0);
}


// Tries to set the binder index, but return false if already there.
//
inline static REBOOL Try_Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    assert(index != 0);
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));
    if (binder->high) {
        if (canon->misc.bind_index.high != 0)
            return FALSE;
        canon->misc.bind_index.high = index;
    }
    else {
        if (canon->misc.bind_index.low != 0)
            return FALSE;
        canon->misc.bind_index.low = index;
    }

#if !defined(NDEBUG)
    ++binder->count;
#endif
    return TRUE;
}


inline static void Add_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon,
    REBINT index
){
    REBOOL success = Try_Add_Binder_Index(binder, canon, index);
    assert(success);
}


inline static REBINT Try_Get_Binder_Index( // 0 if not present
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

    if (binder->high)
        return canon->misc.bind_index.high;
    else
        return canon->misc.bind_index.low;
}


inline static REBINT Try_Remove_Binder_Index( // 0 if failure, else old index
    struct Reb_Binder *binder,
    REBSTR *canon
){
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

    REBINT old_index;
    if (binder->high) {
        old_index = canon->misc.bind_index.high;
        if (old_index == 0)
            return 0;
        canon->misc.bind_index.high = 0;
    }
    else {
        old_index = canon->misc.bind_index.low;
        if (old_index == 0)
            return 0;
        canon->misc.bind_index.low = 0;
    }

#if !defined(NDEBUG)
    --binder->count;
#endif
    return old_index;
}


inline static void Remove_Binder_Index(
    struct Reb_Binder *binder,
    REBSTR *canon
){
    REBINT old_index = Try_Remove_Binder_Index(binder, canon);
    assert(old_index != 0);
}


// Modes allowed by Collect keys functions:
enum {
    COLLECT_ONLY_SET_WORDS = 0,
    COLLECT_ANY_WORD = 1 << 1,
    COLLECT_DEEP = 1 << 2,
    COLLECT_NO_DUP = 1 << 3, // Do not allow dups during collection (for specs)
    COLLECT_ENSURE_SELF = 1 << 4 // !!! Ensure SYM_SELF in context (temp)
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  VARIABLE ACCESS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a word is bound to a context by an index, it becomes a means of
// reading and writing from a persistent storage location.  We use "variable"
// or just VAR to refer to REBVAL slots reached via binding in this way.
// More narrowly, a VAR that represents an argument to a function invocation
// may be called an ARG (and an ARG's "persistence" is only as long as that
// function call is on the stack).
//
// All variables can be put in a protected state where they cannot be written.
// This protection status is marked on the KEY of the context.  Again, more
// narrowly we may refer to a KEY that represents a parameter to a function
// as a PARAM.
//
// The Get_Opt_Var_May_Fail() function takes the conservative default that
// only const access is needed.  A const pointer to a REBVAL is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return a REB_MAX_VOID value, trying
// to Get_Opt_Var_May_Fail() on an *unbound* word will raise an error.
//
// Get_Mutable_Var_May_Fail() offers a parallel facility for getting a
// non-const REBVAL back.  It will fail if the variable is either unbound
// -or- marked with OPT_TYPESET_LOCKED to protect against modification.
//


enum {
    GETVAR_READ_ONLY = 0,
    GETVAR_MUTABLE = 1 << 0
};


// Get the word--variable--value. (Generally, use the macros like
// GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
// called quite a lot and so attention to performance is important.
//
// Coded assuming most common case is to give an error on unbounds, and
// that only read access is requested (so no checking on protection)
//
// Due to the performance-critical nature of this routine, it is declared
// as inline so that locations using it can avoid overhead in invocation.
//
inline static REBVAL *Get_Var_Core(
    const RELVAL *any_word,
    REBSPC *specifier,
    REBFLGS flags
) {
    REBCTX *context;

    assert(ANY_WORD(any_word));

    if (GET_VAL_FLAG(any_word, VALUE_FLAG_RELATIVE)) {
        //
        // RELATIVE BINDING: The word was made during a deep copy of the block
        // that was given as a function's body, and stored a reference to that
        // FUNCTION! as its binding.  To get a variable for the word, we must
        // find the right function call on the stack (if any) for the word to
        // refer to (the FRAME!)
        //
        assert(GET_VAL_FLAG(any_word, WORD_FLAG_BOUND)); // should be set too

    #if !defined(NDEBUG)
        if (specifier == SPECIFIED) {
            printf("Get_Var_Core on relative value without specifier\n");
            panic (any_word);
        }
    #endif

        context = AS_CONTEXT(specifier);

        assert(
            VAL_WORD_FUNC(any_word) == VAL_FUNC(CTX_FRAME_FUNC_VALUE(context))
        );
    }
    else if (GET_VAL_FLAG(any_word, WORD_FLAG_BOUND)) {
        //
        // SPECIFIC BINDING: The context the word is bound to is explicitly
        // contained in the `any_word` REBVAL payload.  Just extract it.
        //
        // We use VAL_SPECIFIC_COMMON() here instead of the heavy-checked
        // VAL_WORD_CONTEXT(), because const_KNOWN() checks for specificity
        // and the context operations will ensure it's a context.
        //
        context = VAL_SPECIFIC_COMMON(const_KNOWN(any_word));
    }
    else {
        // UNBOUND: No variable location to retrieve.

        fail (Error(RE_NOT_BOUND, any_word));
    }

    REBCNT index = VAL_WORD_INDEX(any_word);
    assert(index != 0);

    REBVAL *key = CTX_KEY(context, index);
    assert(VAL_WORD_CANON(any_word) == VAL_KEY_CANON(key));

    if (CTX_VARS_UNAVAILABLE(context)) {
        //
        // Currently if a context has a stack component, then the vars
        // are "all stack"...so when that level is popped, all the vars
        // will be unavailable.  There is a <durable> mechanism, but that
        // makes all the variables come from an ordinary pool-allocated
        // series.  Hybrid approaches which have "some stack and some
        // durable" will be possible in the future, as a context can
        // mechanically have both stackvars and a dynamic data pointer.

        DECLARE_LOCAL (unbound);
        Init_Any_Word(
            unbound,
            VAL_TYPE(any_word),
            VAL_WORD_SPELLING(any_word)
        );

        fail (Error(RE_NO_RELATIVE, unbound));
    }

    REBVAL *var = CTX_VAR(context, index);

    if (flags & GETVAR_MUTABLE) {
        //
        // A context can be permanently frozen (`lock obj`) or temporarily
        // protected, e.g. `protect obj | unprotect obj`.
        //
        // !!! Technically speaking it could also be marked as immutable due
        // to "running", though that feature is not used at this time.
        // All 3 bits are checked in the same instruction.
        //
        FAIL_IF_READ_ONLY_CONTEXT(context);

        // The PROTECT command has a finer-grained granularity for marking
        // not just contexts, but individual fields as protected.
        //
        if (GET_VAL_FLAG(var, VALUE_FLAG_PROTECTED))
            fail (Error(RE_PROTECTED_WORD, any_word));

    }

    assert(!THROWN(var));
    return var;
}

static inline const REBVAL *Get_Opt_Var_May_Fail(
    const RELVAL *any_word,
    REBSPC *specifier
) {
    return Get_Var_Core(any_word, specifier, GETVAR_READ_ONLY);
}

inline static void Copy_Opt_Var_May_Fail(
    REBVAL *out,
    const RELVAL *any_word,
    REBSPC *specifier
) {
    Move_Value(out, Get_Var_Core(any_word, specifier, GETVAR_READ_ONLY));
}

static inline REBVAL *Get_Mutable_Var_May_Fail(
    const RELVAL *any_word,
    REBSPC *specifier
) {
    return Get_Var_Core(any_word, specifier, GETVAR_MUTABLE);
}

#define Sink_Var_May_Fail(any_word,specifier) \
    SINK(Get_Mutable_Var_May_Fail(any_word, specifier))


//=////////////////////////////////////////////////////////////////////////=//
//
//  DETERMINING SPECIFIER FOR CHILDREN IN AN ARRAY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A relative array must be combined with a specifier in order to find the
// actual context instance where its values can be found.  Since today's
// specifiers are always nothing or a FRAME!'s context, this is fairly easy...
// if you find a specific child value living inside a relative array then
// it's that child's specifier that overrides the specifier in effect.
//
// With virtual binding this could get more complex, since a specifier may
// wish to augment or override the binding in a deep way on read-only blocks.
// That means specifiers may need to be chained together.  This would create
// needs for GC or reference counting mechanics, which may defy a simple
// solution in C89.
//
// But as a first step, this function locates all the places in the code that
// would need such derivation.
//

inline static REBSPC *Derive_Specifier(REBSPC *parent, const RELVAL *child) {
    if (IS_SPECIFIC(child))
        return VAL_SPECIFIER(const_KNOWN(child));
    return parent;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  COPYING RELATIVE VALUES TO SPECIFIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This can be used to turn a RELVAL into a REBVAL.  If the RELVAL is indeed
// relative and needs to be made specific to be put into the target, then the
// specifier is used to do that.
//
// It is nearly as fast as just assigning the value directly in the release
// build, though debug builds assert that the function in the specifier
// indeed matches the target in the relative value (because relative values
// in an array may only be relative to the function that deep copied them, and
// that is the only kind of specifier you can use with them).
//
// Interface designed to line up with Move_Value()
//

inline static REBVAL *Derelativize(
    RELVAL *out, // relative destinations are overwritten with specified value
    const RELVAL *v,
    REBSPC *specifier
) {
    assert(NOT_END(v));
    assert(!IS_TRASH_DEBUG(v));

    ASSERT_CELL_WRITABLE(out, __FILE__, __LINE__);

    out->header.bits &= CELL_MASK_RESET;

    if (IS_RELATIVE(v)) {
    #if !defined(NDEBUG)
        assert(ANY_WORD(v) || ANY_ARRAY(v));
        if (specifier == SPECIFIED) {
            printf("Relative item used with SPECIFIED\n");
            panic (v);
        }
        else if (
            VAL_RELATIVE(v)
            != VAL_FUNC(CTX_FRAME_FUNC_VALUE(AS_CONTEXT(specifier)))
        ){
            printf("Function mismatch in specific binding, expected:\n");
            PROBE(FUNC_VALUE(VAL_RELATIVE(v)));
            printf("Panic on relative value\n");
            panic (v);
        }
    #endif

        out->header.bits |=
            v->header.bits
            & CELL_MASK_COPY
            & ~cast(REBUPT, VALUE_FLAG_RELATIVE); // !!! flag is going away

        out->extra.binding = cast(REBARR*, specifier);
    }
    else {
        out->header.bits |= v->header.bits & CELL_MASK_COPY;
        out->extra.binding = v->extra.binding;
    }
    out->payload = v->payload;

    // in case the caller had a relative value slot and wants to use its
    // known non-relative form... this is inline, so no cost if not used.
    //
    return KNOWN(out);
}


// In the C++ build, defining this overload that takes a REBVAL* instead of
// a RELVAL*, and then not defining it...will tell you that you do not need
// to use Derelativize.  Juse Move_Value() if your source is a REBVAL!
//
#ifdef __cplusplus
    REBVAL *Derelativize(RELVAL *dest, const REBVAL *v, REBSPC *specifier);
#endif


inline static void DS_PUSH_RELVAL(const RELVAL *v, REBSPC *specifier) {
    ASSERT_VALUE_MANAGED(v); // would fail on END marker
    DS_PUSH_TRASH;
    Derelativize(DS_TOP, v, specifier);
}


//
// BINDING CONVENIENCE MACROS
//
// WARNING: Don't pass these routines something like a singular REBVAL* (such
// as a REB_BLOCK) which you wish to have bound.  You must pass its *contents*
// as an array...as the plural "values" in the name implies!
//
// So don't do this:
//
//     REBVAL *block = ARG(block);
//     REBVAL *something = ARG(next_arg_after_block);
//     Bind_Values_Deep(block, context);
//
// What will happen is that the block will be treated as an array of values
// and get incremented.  In the above case it would reach to the next argument
// and bind it too (likely crashing at some point not too long after that).
//
// Instead write:
//
//     Bind_Values_Deep(VAL_ARRAY_HEAD(block), context);
//
// That will pass the address of the first value element of the block's
// contents.  You could use a later value element, but note that the interface
// as written doesn't have a length limit.  So although you can control where
// it starts, it will keep binding until it hits an end marker.
//

#define Bind_Values_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, 0, BIND_DEEP)

#define Bind_Values_All_Deep(values,context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, TS_ANY_WORD, BIND_DEEP)

#define Bind_Values_Shallow(values, context) \
    Bind_Values_Core((values), (context), TS_ANY_WORD, 0, BIND_0)

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Midstream_Shallow(values, context) \
    Bind_Values_Core( \
        (values), (context), TS_ANY_WORD, FLAGIT_KIND(REB_SET_WORD), BIND_0)

#define Unbind_Values_Deep(values) \
    Unbind_Values_Core((values), NULL, TRUE)
