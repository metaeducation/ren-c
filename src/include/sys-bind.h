//
//  File: %sys-bind.h
//  Summary: "System Binding Include"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// The GET_OPT_VAR_MAY_FAIL() function takes the conservative default that
// only const access is needed.  A const pointer to a REBVAL is given back
// which may be inspected, but the contents not modified.  While a bound
// variable that is not currently set will return a REB_MAX_VOID value, trying
// to GET_OPT_VAR_MAY_FAIL() on an *unbound* word will raise an error.
//
// TRY_GET_OPT_VAR() also provides const access.  But it will return NULL
// instead of fail on unbound variables.
//
// GET_MUTABLE_VAR_MAY_FAIL() and TRY_GET_MUTABLE_VAR() offer parallel
// facilities for getting a non-const REBVAL back.  They will fail if the
// variable is either unbound -or- marked with OPT_TYPESET_LOCKED to protect
// them against modification.  The TRY variation will fail quietly by
// returning NULL.
//


enum {
    GETVAR_READ_ONLY = 0,
    GETVAR_UNBOUND_OK = 1 << 0,
    GETVAR_IS_SETVAR = 1 << 1 // will clear infix bit, so "always writes"!
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
    enum Reb_Kind *eval_type, // REB_LOOKBACK or REB_FUNCTION
    const RELVAL *any_word,
    REBCTX *specifier,
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
        context = specifier;

    #if !defined(NDEBUG)
        assert(GET_VAL_FLAG(any_word, WORD_FLAG_BOUND)); // should be set too

        if (specifier == SPECIFIED) {
            Debug_Fmt("Get_Var_Core on relative value without specifier");
            PROBE_MSG(any_word, "the word");
            assert(IS_FUNCTION(FUNC_VALUE(VAL_WORD_FUNC(any_word))));
            PROBE_MSG(FUNC_VALUE(VAL_WORD_FUNC(any_word)), "the function");
            PANIC_VALUE(any_word);
        }
        assert(
            VAL_WORD_FUNC(any_word)
            == VAL_FUNC(CTX_FRAME_FUNC_VALUE(specifier))
        );
    #endif
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

        if (flags & GETVAR_UNBOUND_OK) return NULL;

        fail (Error(RE_NOT_BOUND, any_word));
    }

    REBCNT index = VAL_WORD_INDEX(any_word);
    assert(index != 0);

    REBVAL *key = CTX_KEY(context, index);
    assert(VAL_WORD_CANON(any_word) == VAL_KEY_CANON(key));

    REBVAL *var;

    if (GET_CTX_FLAG(context, CONTEXT_FLAG_STACK)) {
        if (GET_CTX_FLAG(context, SERIES_FLAG_INACCESSIBLE)) {
            //
            // Currently if a context has a stack component, then the vars
            // are "all stack"...so when that level is popped, all the vars
            // will be unavailable.  There is a <durable> mechanism, but that
            // makes all the variables come from an ordinary pool-allocated
            // series.  Hybrid approaches which have "some stack and some
            // durable" will be possible in the future, as a context can
            // mechanically have both stackvars and a dynamic data pointer.

            if (flags & GETVAR_UNBOUND_OK) return NULL;

            REBVAL unbound;
            Val_Init_Word(
                &unbound,
                VAL_TYPE(any_word),
                VAL_WORD_SPELLING(any_word)
            );

            fail (Error(RE_NO_RELATIVE, &unbound));
        }

        assert(CTX_STACKVARS(context) != NULL);

        var = FRM_ARG(CTX_FRAME(context), index);
    }
    else
        var = CTX_VAR(context, index);

    if (NOT(flags & GETVAR_IS_SETVAR)) {
        //
        // If we're just reading the variable, we don't touch its lookback
        // bit, but return the value for callers to check.  (E.g. the
        // evaluator wants to know when it fetches the value for a word
        // if it wants to lookback for infix purposes, if it's a function)
        //
        *eval_type = cast(
            enum Reb_Kind, GET_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK)
        ); // REB_FUNCTION = 1, REB_0_LOOKBACK = 0
    }
    else {
        assert(*eval_type == REB_FUNCTION || *eval_type == REB_0_LOOKBACK);

        if (GET_VAL_FLAG(key, TYPESET_FLAG_PROTECTED)) {
            //
            // The key corresponding to the var being looked up contains
            // some flags, including one of whether or not the variable is
            // locked from writes.  If mutable access was requested, deny
            // it if this flag is set.

            if (flags & GETVAR_UNBOUND_OK) return NULL;

            fail (Error(RE_PROTECTED_WORD, any_word));
        }

        // If we are writing, then we write the state of the lookback boolean
        // but also return what it was before.

        if (
            GET_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK)
            != cast(REBOOL, *eval_type)
        ) {
            // Because infixness is no longer a property of values but of
            // the key in a binding, this creates a problem if you want a
            // local in a function to serve as infix...because the effect
            // would be felt by all instances of that function.  One
            // recursion should not be able to affect another in that way,
            // so it is prohibited.
            //
            // !!! This problem already prohibits a PROTECT of function
            // words, so if a solution were engineered for one it would
            // likely be able to apply to both.
            //
            if (GET_CTX_FLAG(context, CONTEXT_FLAG_STACK))
                fail (Error(RE_MISC));

            // Make sure if this context shares a keylist that we aren't
            // setting the other object's lookback states.  Current price paid
            // is making an independent keylist (same issue as adding a key)
            //
            if (Ensure_Keylist_Unique_Invalidated(context))
                key = CTX_KEY(context, index); // refresh

            if (*eval_type) // 1 = REB_FUNCTION, 0 = REB_0_NO_LOOKBACK
                SET_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK);
            else
                CLEAR_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK);

            *eval_type = cast(enum Reb_Kind, NOT(cast(REBOOL, *eval_type)));
        }
        else {
            // We didn't have to change the lookback, so it must have matched
            // what was passed in...leave it alone.
        }
    }

    assert(*eval_type == REB_FUNCTION || *eval_type == REB_0_LOOKBACK);
    assert(!THROWN(var));
    return var;
}

static inline const REBVAL *GET_OPT_VAR_MAY_FAIL(
    const RELVAL *any_word,
    REBCTX *specifier
) {
    enum Reb_Kind eval_type;
    return Get_Var_Core(&eval_type, any_word, specifier, 0);
}

static inline const REBVAL *TRY_GET_OPT_VAR(
    const RELVAL *any_word,
    REBCTX *specifier
) {
    enum Reb_Kind eval_type; // unused
    return Get_Var_Core(&eval_type, any_word, specifier, GETVAR_UNBOUND_OK);
}

static inline REBVAL *GET_MUTABLE_VAR_MAY_FAIL(
    const RELVAL *any_word,
    REBCTX *specifier
) {
    enum Reb_Kind eval_type = REB_FUNCTION; // reset infix/postfix/etc.
    return Get_Var_Core(&eval_type, any_word, specifier, GETVAR_IS_SETVAR);
}

static inline REBVAL *TRY_GET_MUTABLE_VAR(
    const RELVAL *any_word,
    REBCTX *specifier
) {
    enum Reb_Kind eval_type = REB_FUNCTION; // reset infix/postfix/etc.
    return Get_Var_Core(
        &eval_type, any_word, specifier, GETVAR_IS_SETVAR | GETVAR_UNBOUND_OK
    );
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

inline static void COPY_VALUE(
    RELVAL *dest, // relative destinations are overwritten with specified value
    const RELVAL *src,
    REBCTX *specifier
) {
    assert(!IS_END(src));
    assert(!IS_TRASH_DEBUG(src));

    ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(dest, __FILE__, __LINE__);

    if (IS_RELATIVE(src)) {
    #if !defined(NDEBUG)
        assert(ANY_WORD(src) || ANY_ARRAY(src));
        if (specifier == SPECIFIED) {
            Debug_Fmt("Internal Error: Relative item used with SPECIFIED");
            PROBE_MSG(src, "word or array");
            PROBE_MSG(FUNC_VALUE(VAL_RELATIVE(src)), "func");
            assert(FALSE);
        }
        else if (
            VAL_RELATIVE(src)
            != VAL_FUNC(CTX_FRAME_FUNC_VALUE(specifier))
        ){
            Debug_Fmt("Internal Error: Function mismatch in specific binding");
            PROBE_MSG(src, "word or array");
            PROBE_MSG(FUNC_VALUE(VAL_RELATIVE(src)), "expected func");
            PROBE_MSG(CTX_FRAME_FUNC_VALUE(specifier), "actual func");
            assert(FALSE);
        }
    #endif

        dest->header.bits
            = src->header.bits & ~cast(REBUPT, VALUE_FLAG_RELATIVE);
        dest->extra.binding = cast(REBARR*, specifier);
    }
    else {
        dest->header = src->header;
        dest->extra.binding = src->extra.binding;
    }
    dest->payload = src->payload;
}

inline static void DS_PUSH_RELVAL(const RELVAL *v, REBCTX *specifier) {
    ASSERT_VALUE_MANAGED(v); // would fail on END marker
    DS_PUSH_TRASH;
    COPY_VALUE(DS_TOP, v, specifier);
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
// it starts, it will keep binding until it hits an END_CELL.
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
