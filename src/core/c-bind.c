//
//  File: %c-bind.c
//  Summary: "Word Binding Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// Binding relates a word to a context.  Every word can be either bound,
// specifically bound to a particular context, or bound relatively to a
// function (where additional information is needed in order to find the
// specific instance of the variable for that word as a key).
//

#include "sys-core.h"


//
//  Get_Var_Core: C
//
// Get the word--variable--value. (Generally, use the macros like
// GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
// called quite a lot and so attention to performance is important.
//
// Coded assuming most common case is to give an error on unbounds, and
// that only read access is requested (so no checking on protection)
//
REBVAL *Get_Var_Core(
    REBUPT *lookback, // either 1 or 0 (REBUPT to use w/Reb_Frame->eval_type)
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
        // R3-Alpha would look at the function call stack, and use the most
        // recent invocation.  This "dynamic binding" had undesirable
        // properties, and Ren-C achieves "specific binding" to make sure
        // that words preserve their linkage to the correct instance of the
        // function invocation they originated in.

        assert(GET_VAL_FLAG(any_word, WORD_FLAG_BOUND)); // should be set too

        // The only legal time to pass in SPECIFIED is if one is convinced
        // there are no relatively bound words in the array that one is
        // dealing with.  This assert will happen if that was not the
        // case, and there actually was one.
        //
    #if !defined(NDEBUG)
        if (specifier == SPECIFIED) {

            Debug_Fmt("Get_Var_Core on relative value without specifier");
            PROBE_MSG(any_word, "the word");
            assert(IS_FUNCTION(FUNC_VALUE(VAL_WORD_FUNC(any_word))));
            PROBE_MSG(FUNC_VALUE(VAL_WORD_FUNC(any_word)), "the function");
            PANIC_VALUE(any_word);
        }
    #endif

        // If a specifier is provided, then it must be a frame matching
        // the function in the relatively bound word.
        //
        assert(
            VAL_WORD_FUNC(any_word)
            == VAL_FUNC(CTX_FRAME_FUNC_VALUE(specifier))
        );
        context = specifier;
    }
    else if (GET_VAL_FLAG(any_word, WORD_FLAG_BOUND)) {
        //
        // SPECIFIC BINDING: The context the word is bound to is explicitly
        // contained in the `any_word` REBVAL payload.  Just extract it.
        //
        context = VAL_WORD_CONTEXT(const_KNOWN(any_word));
    }
    else {
        // If a word is neither relatively nor specifically bound, then it
        // is unbound and there is no way to get a REBVAL* from it.  Raise
        // an error, or just return NULL if told to trap it.

        if (flags & GETVAR_UNBOUND_OK) return NULL;

        fail (Error(RE_NOT_BOUND, any_word));
    }

    // If the word is bound, then it should have an index (currently nonzero)
    //
    REBCNT index = VAL_WORD_INDEX(any_word);
    assert(index != 0);

    REBVAL *key = CTX_KEY(context, index);

    // Check that the symbol matches the one the word thought it did.
    //
    // !!! Review if the symbol not matching could be used as a "cache miss"
    // and a way of being able to delete key/val pairs from objects.
    //
    assert(SAME_STR(VAL_WORD_CASED(any_word), VAL_KEY_CASED(key)));

    REBVAL *var;

    if (GET_CTX_FLAG(context, CONTEXT_FLAG_STACK)) {
        if (!GET_CTX_FLAG(context, SERIES_FLAG_ACCESSIBLE)) {
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
                VAL_WORD_CASED(any_word)
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
        *lookback = GET_VAL_FLAG(key, TYPESET_FLAG_LOOKBACK);
    }
    else {
        if (GET_VAL_FLAG(key, TYPESET_FLAG_LOCKED)) {
            //
            // The key corresponding to the var being looked up contains
            // some flags, including one of whether or not the variable is
            // locked from writes.  If mutable access was requested, deny
            // it if this flag is set.

            if (flags & GETVAR_UNBOUND_OK) return NULL;

            fail (Error(RE_LOCKED_WORD, any_word));
        }

        // If we are writing, then we write the state of the lookback boolean
        // but also return what it was before.

        if (*lookback != GET_VAL_FLAG(key, TYPESET_FLAG_LOOKBACK)) {
            //
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

            if (*lookback)
                SET_VAL_FLAG(key, TYPESET_FLAG_LOOKBACK);
            else
                CLEAR_VAL_FLAG(key, TYPESET_FLAG_LOOKBACK);

            *lookback = NOT(*lookback); // *effectively* return the *old* state
        }
        else {
            // We didn't have to change the lookback, so it must have matched
            // what was passed in...leave it alone.
        }
    }

    assert(*lookback == ET_FUNCTION || *lookback == ET_LOOKBACK);
    assert(!THROWN(var));
    return var;
}


//
//  Bind_Values_Inner_Loop: C
//
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
static void Bind_Values_Inner_Loop(
    struct Reb_Binder *binder,
    RELVAL *head,
    REBCTX *context,
    REBU64 bind_types, // !!! REVIEW: force word types low enough for 32-bit?
    REBU64 add_midstream_types,
    REBFLGS flags
) {
    RELVAL *value = head;
    for (; NOT_END(value); value++) {
        REBU64 type_bit = FLAGIT_KIND(VAL_TYPE(value));

        if (type_bit & bind_types) {
            REBSTR *canon = VAL_WORD_CANON(value);
            REBCNT n = Try_Get_Binder_Index(binder, canon);
            if (n != 0) {
                assert(n <= CTX_LEN(context));

                // We're overwriting any previous binding, which may have
                // been relative.
                //
                CLEAR_VAL_FLAG(value, VALUE_FLAG_RELATIVE);

                SET_VAL_FLAG(value, WORD_FLAG_BOUND);
                INIT_WORD_CONTEXT(value, context);
                INIT_WORD_INDEX(value, n);
            }
            else if (type_bit & add_midstream_types) {
                //
                // Word is not in context, so add it if option is specified
                //
                Expand_Context(context, 1);
                Append_Context(context, value, 0);
                Add_Binder_Index(binder, canon, VAL_WORD_INDEX(value));
            }
        }
        else if (ANY_ARRAY(value) && (flags & BIND_DEEP)) {
            Bind_Values_Inner_Loop(
                binder,
                VAL_ARRAY_AT(value),
                context,
                bind_types,
                add_midstream_types,
                flags
            );
        }
        else if (
            IS_FUNCTION(value)
            && IS_FUNCTION_PLAIN(value)
            && (flags & BIND_FUNC)
        ) {
            // !!! Likely-to-be deprecated functionality--rebinding inside the
            // content of an already formed function.  :-/
            //
            Bind_Values_Inner_Loop(
                binder,
                VAL_FUNC_BODY(value),
                context,
                bind_types,
                add_midstream_types,
                flags
            );
        }
    }
}


//
//  Bind_Values_Core: C
//
// Bind words in an array of values terminated with END
// to a specified context.  See warnings on the functions like
// Bind_Values_Deep() about not passing just a singular REBVAL.
//
// NOTE: If types are added, then they will be added in "midstream".  Only
// bindings that come after the added value is seen will be bound.
//
void Bind_Values_Core(
    RELVAL *head,
    REBCTX *context,
    REBU64 bind_types,
    REBU64 add_midstream_types,
    REBFLGS flags // see %sys-core.h for BIND_DEEP, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    // Via the global hash table, each spelling of the word can find the
    // canon form of the word.  Associate that with an index number to signal
    // a binding should be created to this context (at that index.)

    REBCNT index = 1;
    REBVAL *key = CTX_KEYS_HEAD(context);
    for (; index <= CTX_LEN(context); key++, index++)
        if (!GET_VAL_FLAG(key, TYPESET_FLAG_UNBINDABLE))
            Add_Binder_Index(&binder, VAL_KEY_CANON(key), index);

    Bind_Values_Inner_Loop(
        &binder, head, context, bind_types, add_midstream_types, flags
    );

    // Reset all the binder indices to zero, balancing out what was added.

    key = CTX_KEYS_HEAD(context);
    for (; NOT_END(key); key++)
        Remove_Binder_Index(&binder, VAL_KEY_CANON(key));

    SHUTDOWN_BINDER(&binder);
}


//
//  Unbind_Values_Core: C
//
// Unbind words in a block, optionally unbinding those which are
// bound to a particular target (if target is NULL, then all
// words will be unbound regardless of their VAL_WORD_CONTEXT).
//
void Unbind_Values_Core(RELVAL *head, REBCTX *context, REBOOL deep)
{
    RELVAL *value = head;
    for (; NOT_END(value); value++) {
        if (
            ANY_WORD(value)
            && (
                !context
                || (
                    IS_WORD_BOUND(value)
                    && !IS_RELATIVE(value)
                    && VAL_WORD_CONTEXT(KNOWN(value)) == context
                )
            )
        ) {
            UNBIND_WORD(value);
        }
        else if (ANY_ARRAY(value) && deep)
            Unbind_Values_Core(VAL_ARRAY_AT(value), context, TRUE);
    }
}


//
//  Try_Bind_Word: C
//
// Binds a word to a context. If word is not part of the context.
//
REBCNT Try_Bind_Word(REBCTX *context, REBVAL *word)
{
    REBCNT n = Find_Canon_In_Context(context, VAL_WORD_CANON(word), FALSE);
    if (n != 0) {
        //
        // Previously may have been bound relative, remove flag.
        //
        CLEAR_VAL_FLAG(word, VALUE_FLAG_RELATIVE);

        SET_VAL_FLAG(word, WORD_FLAG_BOUND);
        INIT_WORD_CONTEXT(word, context);
        INIT_WORD_INDEX(word, n);
    }
    return n;
}


//
//  Bind_Relative_Inner_Loop: C
//
// Recursive function for relative function word binding.  Returns TRUE if
// any relative bindings were made.
//
static void Bind_Relative_Inner_Loop(
    struct Reb_Binder *binder,
    RELVAL *head,
    REBARR *paramlist,
    REBU64 bind_types
) {
    RELVAL *value = head;

    for (; NOT_END(value); value++) {
        REBU64 type_bit = FLAGIT_KIND(VAL_TYPE(value));

        // The two-pass copy-and-then-bind should have gotten rid of all the
        // relative values to other functions during the copy.
        //
        // !!! Long term, in a single pass copy, this would have to deal
        // with relative values and run them through the specification
        // process if they were not just getting overwritten.
        //
        assert(!IS_RELATIVE(value));

        if (type_bit & bind_types) {
            REBINT n = Try_Get_Binder_Index(binder, VAL_WORD_CANON(value));
            if (n != 0) {
                //
                // Word's canon symbol is in frame.  Relatively bind it.
                // (clear out existing header flags first).
                //
                VAL_RESET_HEADER(value, VAL_TYPE(value));
                SET_VAL_FLAGS(value, WORD_FLAG_BOUND | VALUE_FLAG_RELATIVE);
                INIT_WORD_FUNC(value, AS_FUNC(paramlist)); // incomplete func
                INIT_WORD_INDEX(value, n);
            }
        }
        else if (ANY_ARRAY(value)) {
            Bind_Relative_Inner_Loop(
                binder, VAL_ARRAY_AT(value), paramlist, bind_types
            );

            // Set the bits in the ANY-ARRAY! REBVAL to indicate that it is
            // relative to the function.
            //
            // !!! Technically speaking it is not necessary for an array to
            // be marked relative if it doesn't contain any relative words
            // under it.  However, for uniformity in the near term, it's
            // easiest to debug if there is a clear mark on arrays that are
            // part of a deep copy of a function body either way.
            //
            SET_VAL_FLAG(value, VALUE_FLAG_RELATIVE);
            INIT_RELATIVE(value, AS_FUNC(paramlist)); // incomplete func
        }
    }
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Function in order to take the raw material
// given as a function body, and de-relativize any IS_RELATIVE(value)s that
// happen to be in it already (as any Copy does).  But it also needs to make
// new relative references to ANY-WORD! that are referencing function
// parameters, as well as to relativize the copies of ANY-ARRAY! that contain
// these relative words...so that they refer to the archetypal function
// to which they should be relative.
//
REBARR *Copy_And_Bind_Relative_Deep_Managed(
    const REBVAL *body,
    REBARR *paramlist, // body of function is not actually ready yet
    REBU64 bind_types
) {
    // !!! Currently this is done in two phases, because the historical code
    // would use the generic copying code and then do a bind phase afterward.
    // Both phases are folded into this routine to make it easier to make
    // a one-pass version when time permits.
    //
    REBARR *copy = COPY_ANY_ARRAY_AT_DEEP_MANAGED(body);

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    // Setup binding table from the argument word list
    //
    REBCNT index = 1;
    RELVAL *param = ARR_AT(paramlist, 1); // [0] is FUNCTION! value
    for (; NOT_END(param); param++, index++)
        Add_Binder_Index(&binder, VAL_KEY_CANON(param), index);

    Bind_Relative_Inner_Loop(&binder, ARR_HEAD(copy), paramlist, bind_types);

    // Reset binding table
    //
    param = ARR_AT(paramlist, 1); // [0] is FUNCTION! value
    for (; NOT_END(param); param++)
        Remove_Binder_Index(&binder, VAL_KEY_CANON(param));

    SHUTDOWN_BINDER(&binder);
    return copy;
}


//
//  Bind_Stack_Word: C
//
void Bind_Stack_Word(REBFUN *func, REBVAL *word)
{
    REBINT index = Find_Param_Index(FUNC_PARAMLIST(func), VAL_WORD_CANON(word));
    if (index == 0)
        fail (Error(RE_NOT_IN_CONTEXT, word));

    VAL_RESET_HEADER(word, VAL_TYPE(word));
    SET_VAL_FLAGS(word, WORD_FLAG_BOUND | VALUE_FLAG_RELATIVE);
    INIT_WORD_FUNC(word, func);
    INIT_WORD_INDEX(word, index);
}


//
//  Rebind_Values_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
void Rebind_Values_Deep(
    REBCTX *src,
    REBCTX *dst,
    RELVAL *head,
    struct Reb_Binder *opt_binder
) {
    RELVAL *value = head;
    for (; NOT_END(value); value++) {
        if (ANY_ARRAY(value)) {
            Rebind_Values_Deep(src, dst, VAL_ARRAY_AT(value), opt_binder);
        }
        else if (
            ANY_WORD(value)
            && GET_VAL_FLAG(value, WORD_FLAG_BOUND)
            && !GET_VAL_FLAG(value, VALUE_FLAG_RELATIVE)
            && VAL_WORD_CONTEXT(KNOWN(value)) == src
        ) {
            INIT_WORD_CONTEXT(value, dst);

            if (opt_binder != NULL) {
                INIT_WORD_INDEX(
                    value,
                    Try_Get_Binder_Index(opt_binder, VAL_WORD_CANON(value))
                );
            }
        }
        else if (IS_FUNCTION(value) && IS_FUNCTION_PLAIN(value)) {
            //
            // !!! Extremely questionable feature--walking into function
            // bodies and changing them.  This R3-Alpha concept was largely
            // broken (didn't work for closures) and created a lot of extra
            // garbage (inheriting an object's methods meant making deep
            // copies of all that object's method bodies...each time).
            // Ren-C has a different idea in the works.
            //
            Rebind_Values_Deep(
                src, dst, VAL_FUNC_BODY(value), opt_binder
            );
        }
    }
}
