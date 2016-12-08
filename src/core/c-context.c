//
//  File: %c-context.c
//  Summary: "Management routines for ANY-CONTEXT! key/value storage"
//  Section: core
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
// Contexts are two arrays of equal length, which are linked together to
// describe "object-like" things (lists of TYPESET! keys and corresponding
// variable values).  They are used by OBJECT!, PORT!, FRAME!, etc.
//
// The REBCTX* is how contexts are passed around as a single pointer.  This
// pointer is actually just an array REBSER which represents the variable
// values.  The keylist can be reached through the ->link field of that
// REBSER, and the [0] value of the variable array is a "canon instance" of
// whatever kind of REBVAL the context represents.
//
//
//      VARLIST ARRAY:                ---Link-> KEYLIST ARRAY:
//      +----------------------------+          +----------------------------+
//      +          "ROOTVAR"         |          |          "ROOTKEY"         |
//      |  Canon ANY-CONTEXT! Value  |          | Canon FUNCTION!, or blank  |
//      +----------------------------+          +----------------------------+
//      |          Value 1           |          |    Typeset w/symbol 1      |
//      +----------------------------+          +----------------------------+
//      |          Value 2           |          |    Typeset w/symbol 2      |
//      +----------------------------+          +----------------------------+
//      |          Value ...         |          |    Typeset w/symbol 3 ...  |
//      +----------------------------+          +----------------------------+
//
// While R3-Alpha used a special kind of WORD! known as an "unword" for the
// keys, Ren-C uses a special kind of TYPESET! which can also hold a symbol.
// The reason is that keylists are common to function paramlists and objects,
// and typesets are more complex than words (and destined to become even
// moreso with user defined types).  So it's better to take the small detail
// of storing a symbol in a typeset rather than try and enhance words to have
// typeset features.
//
// Keylists can be shared between objects, and if the context represents a
// call FRAME! then the keylist is actually the paramlist of that function
// being called.  If the keylist is not for a function, then the [0] cell
// (a.k.a. "ROOTKEY") is currently not used--and set to a BLANK!.
//

#include "sys-core.h"


//
//  Alloc_Context: C
//
// Create context of a given size, allocating space for both words and values.
//
// This context will not have its ANY-OBJECT! REBVAL in the [0] position fully
// configured, hence this is an "Alloc" instead of a "Make" (because there
// is still work to be done before it will pass ASSERT_CONTEXT).
//
REBCTX *Alloc_Context(REBCNT len)
{
    REBARR *varlist = Make_Array(len + 1); // size + room for ROOTVAR
    SET_ARR_FLAG(varlist, ARRAY_FLAG_VARLIST);

    // varlist[0] is a value instance of the OBJECT!/MODULE!/PORT!/ERROR! we
    // are building which contains this context.

    REBVAL *rootvar = Alloc_Tail_Array(varlist);
    SET_TRASH_IF_DEBUG(rootvar);
    rootvar->payload.any_context.varlist = varlist;

    // keylist[0] is the "rootkey" which we currently initialize to BLANK

    REBARR *keylist = Make_Array(len + 1); // size + room for ROOTKEY
    SET_BLANK(Alloc_Tail_Array(keylist));
    ARR_SERIES(keylist)->link.meta = NULL; // GC sees meta object, must init

    // varlists link keylists via REBSER.misc field, sharable hence managed

    INIT_CTX_KEYLIST_UNIQUE(AS_CONTEXT(varlist), keylist);
    MANAGE_ARRAY(keylist);

    return AS_CONTEXT(varlist); // varlist pointer is context handle
}


//
//  Expand_Context_Keylist_Core: C
//
// Returns whether or not the expansion invalidated existing keys.
//
REBOOL Expand_Context_Keylist_Core(REBCTX *context, REBCNT delta)
{
    if (delta == 0) return FALSE;

    REBARR *keylist = CTX_KEYLIST(context);
    if (GET_ARR_FLAG(keylist, KEYLIST_FLAG_SHARED)) {
        //
        // INIT_CTX_KEYLIST_SHARED was used to set the flag that indicates
        // this keylist is shared with one or more other contexts.  Can't
        // expand the shared copy without impacting the others, so break away
        // from the sharing group by making a new copy.
        //
        // (If all shared copies break away in this fashion, then the last
        // copy of the dangling keylist will be GC'd.)
        //
        // Keylists are only typesets, so no need for a specifier.

        REBCTX *meta = ARR_SERIES(keylist)->link.meta; // preserve meta object

        keylist = Copy_Array_Extra_Shallow(keylist, SPECIFIED, delta);

        ARR_SERIES(keylist)->link.meta = meta;

        MANAGE_ARRAY(keylist);
        INIT_CTX_KEYLIST_UNIQUE(context, keylist);

        return TRUE;
    }

    // INIT_CTX_KEYLIST_UNIQUE was used to set this keylist in the
    // context, and no INIT_CTX_KEYLIST_SHARED was used by another context
    // to mark the flag indicating it's shared.  Extend it directly.

    Extend_Series(ARR_SERIES(keylist), delta);
    TERM_ARRAY_LEN(keylist, ARR_LEN(keylist));

    return FALSE;
}


//
//  Expand_Context: C
//
// Expand a context. Copy words if keylist is not unique.
//
void Expand_Context(REBCTX *context, REBCNT delta)
{
    // varlist is unique to each object--expand without making a copy.
    //
    Extend_Series(ARR_SERIES(CTX_VARLIST(context)), delta);
    TERM_ARRAY_LEN(CTX_VARLIST(context), ARR_LEN(CTX_VARLIST(context)));

    Expand_Context_Keylist_Core(context, delta);
}


//
//  Append_Context_Core: C
//
// Append a word to the context word list. Expands the list if necessary.
// Returns the value cell for the word.  The new variable is unset by default.
//
// !!! Review if it would make more sense to use TRASH.
//
// If word is not NULL, use the word sym and bind the word value, otherwise
// use sym.  When using a word, it will be modified to be specifically bound
// to this context after the operation.
//
// !!! Should there be a clearer hint in the interface, with a REBVAL* out,
// to give a fully bound value as a result?  Given that the caller passed
// in the context and can get the index out of a relatively bound word,
// they usually likely don't need the result directly.
//
REBVAL *Append_Context_Core(
    REBCTX *context,
    RELVAL *opt_any_word,
    REBSTR *opt_name,
    REBOOL lookback
) {
    REBARR *keylist = CTX_KEYLIST(context);

    // Add the key to key list
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(keylist), 1);
    REBVAL *key = SINK(ARR_LAST(keylist));
    Val_Init_Typeset(
        key,
        ALL_64,
        opt_any_word != NULL ? VAL_WORD_SPELLING(opt_any_word) : opt_name
    );
    TERM_ARRAY_LEN(keylist, ARR_LEN(keylist));

    if (lookback)
        CLEAR_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK);

    // Add an unset value to var list
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(CTX_VARLIST(context)), 1);
    REBVAL *value = SINK(ARR_LAST(CTX_VARLIST(context)));
    SET_VOID(value);
    TERM_ARRAY_LEN(CTX_VARLIST(context), ARR_LEN(CTX_VARLIST(context)));

    if (opt_any_word) {
        REBCNT len = CTX_LEN(context);

        // We want to not just add a key/value pairing to the context, but we
        // want to bind a word while we are at it.  Make sure symbol is valid.
        //
        assert(opt_name == NULL);

        // When a binding is made to an ordinary context, the value list is
        // used as the target and the index is a positive number.  Note that
        // for stack-relative bindings, the index will be negative and the
        // target will be a function's PARAMLIST series.
        //
        assert(!GET_VAL_FLAG(opt_any_word, VALUE_FLAG_RELATIVE));
        SET_VAL_FLAG(opt_any_word, WORD_FLAG_BOUND);
        INIT_WORD_CONTEXT(opt_any_word, context);
        INIT_WORD_INDEX(opt_any_word, len); // length we just bumped
    }
    else
        assert(opt_name != NULL);

    // The variable value location for the key we just added.  It's currently
    // unset (maybe trash someday?) but in either case, known to not be
    // a relative any-word or any-array
    //
    return value;
}


//
//  Copy_Context_Shallow_Extra: C
//
// Makes a copy of a context.  If no extra storage space is requested, then
// the same keylist will be used.
//
REBCTX *Copy_Context_Shallow_Extra(REBCTX *src, REBCNT extra) {
    REBCTX *dest;

    assert(GET_ARR_FLAG(CTX_VARLIST(src), ARRAY_FLAG_VARLIST));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(src));

    REBCTX *meta = CTX_META(src); // preserve meta object (if any)

    // Note that keylists contain only typesets (hence no relative values),
    // and no varlist is part of a function body.  All the values here should
    // be fully specified.
    //
    if (extra == 0) {
        dest = AS_CONTEXT(Copy_Array_Shallow(CTX_VARLIST(src), SPECIFIED));
        INIT_CTX_KEYLIST_SHARED(dest, CTX_KEYLIST(src));
    }
    else {
        REBARR *keylist = Copy_Array_Extra_Shallow(
            CTX_KEYLIST(src), SPECIFIED, extra
        );
        dest = AS_CONTEXT(Copy_Array_Extra_Shallow(
            CTX_VARLIST(src), SPECIFIED, extra
        ));
        INIT_CTX_KEYLIST_UNIQUE(dest, keylist);
        MANAGE_ARRAY(CTX_KEYLIST(dest));
    }

    SET_ARR_FLAG(CTX_VARLIST(dest), ARRAY_FLAG_VARLIST);

    CTX_VALUE(dest)->payload.any_context.varlist = CTX_VARLIST(dest);

    INIT_CONTEXT_META(dest, meta); // will be placed on new keylist

    return dest;
}


//
//  Collect_Keys_Start: C
//
// Use the Bind_Table to start collecting new keys for a context.
// Use Collect_Keys_End() when done.
//
// WARNING: This routine uses the shared BUF_COLLECT rather than
// targeting a new series directly.  This way a context can be
// allocated at exactly the right length when contents are copied.
// Therefore do not call code that might call BIND or otherwise
// make use of the Bind_Table or BUF_COLLECT.
//
void Collect_Keys_Start(REBFLGS flags)
{
    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty

    // Leave the [0] slot empty while collecting.  This will become the
    // "rootparam" in function paramlists (where the FUNCTION! archetype
    // value goes), the [0] slot in varlists (where the ANY-CONTEXT! archetype
    // goes), and the [0] slot in keylists (which sometimes are FUNCTION! if
    // it's a FRAME! context...and not yet used in other context types)

    SET_TRASH_IF_DEBUG(ARR_HEAD(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);
}


//
//  Grab_Collected_Keylist_Managed: C
//
// The BUF_COLLECT is used to gather keys, which may wind up not requiring any
// new keys from the `prior` that was passed in.  If this is the case, then
// that prior keylist is returned...otherwise a new one is created.
//
// !!! "Grab" is used because "Copy_Or_Reuse" is long, and is picked to draw
// attention to look at the meaning.  Better short communicative name?
//
REBARR *Grab_Collected_Keylist_Managed(REBCTX *prior)
{
    REBARR *keylist;

    // We didn't terminate as we were collecting, so terminate now.
    //
    assert(ARR_LEN(BUF_COLLECT) >= 1); // always at least [0] for rootkey
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

#if !defined(NDEBUG)
    //
    // When the key collecting is done, we may be asked to give back a keylist
    // and when we do, if nothing was added beyond the `prior` then that will
    // be handed back.  The array handed back will always be managed, so if
    // we create it then it will be, and if we reuse the prior it will be.
    //
    if (prior) ASSERT_ARRAY_MANAGED(CTX_KEYLIST(prior));
#endif

    // If no new words, prior context.  Note length must include the slot
    // for the rootkey...and note also this means the rootkey cell *may*
    // be shared between all keylists when you pass in a prior.
    //
    if (prior && ARR_LEN(BUF_COLLECT) == CTX_LEN(prior) + 1) {
        keylist = CTX_KEYLIST(prior);
    }
    else {
        // The BUF_COLLECT should contain only typesets, so no relative values
        //
        keylist = Copy_Array_Shallow(BUF_COLLECT, SPECIFIED);
        MANAGE_ARRAY(keylist);
    }

    ARR_SERIES(keylist)->link.meta = NULL; // clear meta object (GC sees this)

    return keylist;
}


//
//  Collect_Keys_End: C
//
// Free the Bind_Table for reuse and empty the BUF_COLLECT.
//
void Collect_Keys_End(struct Reb_Binder *binder)
{
    // We didn't terminate as we were collecting, so terminate now.
    //
    assert(ARR_LEN(BUF_COLLECT) >= 1); // always at least [0] for rootkey
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Reset binding table (note BUF_COLLECT may have expanded)
    //
    RELVAL *key;
    for (key = ARR_HEAD(BUF_COLLECT) + 1; NOT_END(key); key++) {
        REBSTR *canon = VAL_KEY_CANON(key);

        if (binder != NULL) {
            Remove_Binder_Index(binder, canon);
            continue;
        }

        // !!! This doesn't have a "binder" available to clear out the
        // keys with.  The nature of handling error states means that if
        // a thread-safe binding system was implemented, we'd have to know
        // which thread had the error to roll back any binding structures.
        // For now just zero it out based on the collect buffer.
        //
        assert(
            canon->misc.bind_index.high != 0
            || canon->misc.bind_index.low != 0
        );
        canon->misc.bind_index.high = 0;
        canon->misc.bind_index.low = 0;
    }

    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 0);
}


//
//  Collect_Context_Keys: C
//
// Collect words from a prior context.  If `check_dups` is passed in then
// there is a check for duplicates, otherwise the keys are assumed to
// be unique and copied in using `memcpy` as an optimization.
//
void Collect_Context_Keys(
    struct Reb_Binder *binder,
    REBCTX *context,
    REBOOL check_dups
) {
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBINT bind_index = ARR_LEN(BUF_COLLECT);
    RELVAL *collect; // can't set until after potential expansion...

    // The BUF_COLLECT buffer should at least have the SYM_0 in its first slot
    // to use as a "rootkey" in the generated keylist (and also that the first
    // binding index we give out is at least 1, since 0 is used in the
    // Bind_Table to mean "word not collected yet").
    //
    assert(bind_index >= 1);

    // this is necessary for memcpy below to not overwrite memory BUF_COLLECT
    // does not own.  (It may make the buffer capacity bigger than necessary
    // if duplicates are found, but the actual buffer length will be set
    // correctly by the end.)
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(BUF_COLLECT), CTX_LEN(context));

    // EXPAND_SERIES_TAIL will increase the ARR_LEN, even though we intend
    // to overwrite it with a possibly shorter length.  Put the length back
    // and now that the expansion is done, get the pointer to where we want
    // to start collecting new typesets.
    //
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, bind_index);
    collect = ARR_TAIL(BUF_COLLECT);

    if (check_dups) {
        // We're adding onto the end of the collect buffer and need to
        // check for duplicates of what's already there.
        //
        for (; NOT_END(key); key++) {
            REBSTR *canon = VAL_KEY_CANON(key);
            if (NOT(Try_Add_Binder_Index(binder, canon, bind_index))) {
                //
                // If we found the typeset's symbol in the bind table already
                // then don't collect it in the buffer again.
                //
                continue;
            }

            ++bind_index;

            // !!! At the moment objects do not heed the typesets in the
            // keys.  If they did, what sort of rule should the typesets
            // have when being inherited?
            //
            *collect = *key;
            ++collect;
        }

        // Increase the length of BUF_COLLLECT by how far `collect` advanced
        // (would be 0 if all the keys were duplicates...)
        //
        SET_ARRAY_LEN_NOTERM(
            BUF_COLLECT,
            ARR_LEN(BUF_COLLECT) + (collect - ARR_TAIL(BUF_COLLECT))
        );
    }
    else {
        // Optimized copy of the keys.  We can use `memcpy` because these are
        // typesets that are just 64-bit bitsets plus a symbol ID; there is
        // no need to clone the REBVALs to give the copies new identity.
        //
        // Add the keys and bump the length of the collect buffer after
        // (prior to that, the tail should be on the END marker of
        // the existing content--if any)
        //
        memcpy(collect, key, CTX_LEN(context) * sizeof(REBVAL));
        SET_ARRAY_LEN_NOTERM(
            BUF_COLLECT, ARR_LEN(BUF_COLLECT) + CTX_LEN(context)
        );

        for (; NOT_END(key); ++key, ++bind_index)
            Add_Binder_Index(binder, VAL_KEY_CANON(key), bind_index);
    }

    // BUF_COLLECT doesn't get terminated as its being built, but it gets
    // terminated in Collect_Keys_End()
}


//
//  Collect_Context_Inner_Loop: C
//
// The inner recursive loop used for Collect_Context function below.
//
static void Collect_Context_Inner_Loop(
    struct Reb_Binder *binder,
    const RELVAL *head,
    REBFLGS flags
) {
    const RELVAL *value = head;
    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)) {
            REBSTR *canon = VAL_WORD_CANON(value);
            if (Try_Get_Binder_Index(binder, canon) == 0) {
                // once per word
                if (IS_SET_WORD(value) || (flags & COLLECT_ANY_WORD)) {
                    Add_Binder_Index(binder, canon, ARR_LEN(BUF_COLLECT));
                    EXPAND_SERIES_TAIL(ARR_SERIES(BUF_COLLECT), 1);
                    REBVAL *typeset = SINK(ARR_LAST(BUF_COLLECT));
                    Val_Init_Typeset(
                        typeset,
                        // Allow all datatypes but no void (initially):
                        ~FLAGIT_KIND(REB_MAX_VOID),
                        VAL_WORD_SPELLING(value)
                    );
                }
            }
            else { // Word is duplicated
                if (flags & COLLECT_NO_DUP)
                    fail (Error(RE_DUP_VARS, value)); // cleans binding table
            }
            continue;
        }
        // Recurse into sub-blocks:
        if (ANY_EVAL_BLOCK(value) && (flags & COLLECT_DEEP))
            Collect_Context_Inner_Loop(binder, VAL_ARRAY_AT(value), flags);
    }
}


//
//  Collect_Keylist_Managed: C
//
// Scans a block for words to extract and make into typeset keys to go in
// a context.  The Bind_Table is used to quickly determine duplicate entries.
//
// A `prior` context can be provided to serve as a basis; all the keys in
// the prior will be returned, with only new entries contributed by the
// data coming from the head[] array.  If no new values are needed (the
// array has no relevant words, or all were just duplicates of words already
// in prior) then then `prior`'s keylist may be returned.  The result is
// always pre-managed, because it may not be legal to free prior's keylist.
//
// Returns:
//     A block of typesets that can be used for a context keylist.
//     If no new words, the prior list is returned.
//
// !!! There was previously an optimization in object creation which bypassed
// key collection in the case where head[] was empty.  Revisit if it is worth
// the complexity to move handling for that case in this routine.
//
REBARR *Collect_Keylist_Managed(
    REBCNT *self_index_out, // which context index SELF is in (if COLLECT_SELF)
    const RELVAL *head,
    REBCTX *prior,
    REBFLGS flags // see %sys-core.h for COLLECT_ANY_WORD, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    Collect_Keys_Start(flags);

    if (flags & COLLECT_ENSURE_SELF) {
        if (
            !prior
            || (
                (*self_index_out = Find_Canon_In_Context(
                    prior, Canon(SYM_SELF), TRUE)
                )
                == 0
            )
        ) {
            // No prior or no SELF in prior, so we'll add it as the first key
            //
            RELVAL *self_key = ARR_AT(BUF_COLLECT, 1);
            Val_Init_Typeset(self_key, ALL_64, Canon(SYM_SELF));

            // !!! See notes on the flags about why SELF is set hidden but
            // not unbindable with TYPESET_FLAG_UNBINDABLE.
            //
            SET_VAL_FLAG(self_key, TYPESET_FLAG_HIDDEN);

            Add_Binder_Index(&binder, VAL_KEY_CANON(self_key), 1);
            *self_index_out = 1;
            SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 2); // [0] rootkey, plus SELF
        }
        else {
            // No need to add SELF if it's going to be added via the `prior`
            // so just return the `self_index_out` as-is.
        }
    }
    else {
        assert(self_index_out == NULL);
    }

    // Setup binding table with existing words, no need to check duplicates
    //
    if (prior) Collect_Context_Keys(&binder, prior, FALSE);

    // Scan for words, adding them to BUF_COLLECT and bind table:
    Collect_Context_Inner_Loop(&binder, head, flags);

    // Grab the keylist, and set its rootkey in [0] to BLANK! (CTX_KEY and
    // CTX_VAR indexing start at 1, and [0] for the variables is an instance
    // of the ANY-CONTEXT! value itself).
    //
    // !!! Usages of the rootkey for non-FRAME! contexts is open for future.
    //
    REBARR *keylist = Grab_Collected_Keylist_Managed(prior);
    SET_BLANK(ARR_HEAD(keylist));

    Collect_Keys_End(&binder);

    SHUTDOWN_BINDER(&binder);
    return keylist;
}


//
//  Collect_Words_Inner_Loop: C
//
// Used for Collect_Words() after the binds table has
// been set up.
//
static void Collect_Words_Inner_Loop(
    struct Reb_Binder *binder,
    const RELVAL *head,
    REBFLGS flags
) {
    const RELVAL *value = head;
    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)
            && Try_Get_Binder_Index(binder, VAL_WORD_CANON(value)) == 0
            && (IS_SET_WORD(value) || (flags & COLLECT_ANY_WORD))
        ){
            Add_Binder_Index(binder, VAL_WORD_CANON(value), 1);

            REBVAL *word = Alloc_Tail_Array(BUF_COLLECT);
            Val_Init_Word(word, REB_WORD, VAL_WORD_SPELLING(value));
        }
        else if (ANY_EVAL_BLOCK(value) && (flags & COLLECT_DEEP))
            Collect_Words_Inner_Loop(binder, VAL_ARRAY_AT(value), flags);
    }
}


//
//  Collect_Words: C
//
// Collect words from a prior block and new block.
//
REBARR *Collect_Words(
    const RELVAL *head,
    RELVAL *opt_prior_head,
    REBFLGS flags
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty

    if (opt_prior_head)
        Collect_Words_Inner_Loop(&binder, opt_prior_head, COLLECT_ANY_WORD);

    REBCNT start = ARR_LEN(BUF_COLLECT);
    Collect_Words_Inner_Loop(&binder, head, flags);
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Reset word markers:
    //
    RELVAL *word;
    for (word = ARR_HEAD(BUF_COLLECT); NOT_END(word); word++)
        Remove_Binder_Index(&binder, VAL_WORD_CANON(word));

    // The words in BUF_COLLECT are newly created, and should not be bound
    // at all... hence fully specified with no relative words
    //
    REBARR *array = Copy_Array_At_Max_Shallow(
        BUF_COLLECT, start, SPECIFIED, ARR_LEN(BUF_COLLECT) - start
    );
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 0);

    SHUTDOWN_BINDER(&binder);
    return array;
}


//
//  Rebind_Context_Deep: C
//
// Clone old context to new context knowing
// which types of values need to be copied, deep copied, and rebound.
//
void Rebind_Context_Deep(
    REBCTX *source,
    REBCTX *dest,
    struct Reb_Binder *opt_binder
) {
    Rebind_Values_Deep(source, dest, CTX_VARS_HEAD(dest), opt_binder);
}


//
//  Make_Selfish_Context_Detect: C
//
// Create a context by detecting top-level set-words in an array of values.
// So if the values were the contents of the block `[a: 10 b: 20]` then the
// resulting context would be for two words, `a` and `b`.
//
// Optionally a parent context may be passed in, which will contribute its
// keylist of words to the result if provided.
//
// The resulting context will have a SELF: defined as a hidden key (will not
// show up in `words-of` but will be bound during creation).  As part of
// the migration away from SELF being a keyword, the logic for adding and
// managing SELF has been confined to this function (called by `make object!`
// and some other context-creating routines).  This will ultimately turn
// into something paralleling the non-keyword definitional RETURN:, where
// the generators (like OBJECT) will be taking responsibility for it.
//
// This routine will *always* make a context with a SELF.  This lacks the
// nuance that is expected of the generators, which will have an equivalent
// to `<with> return` or `<with> leave` to suppress it.
//
REBCTX *Make_Selfish_Context_Detect(
    enum Reb_Kind kind,
    REBARR *binding,
    const RELVAL *head,
    REBCTX *opt_parent
) {
    REBCNT self_index;
    REBARR *keylist = Collect_Keylist_Managed(
        &self_index,
        head,
        opt_parent,
        COLLECT_ONLY_SET_WORDS | COLLECT_ENSURE_SELF
    );

    REBCNT len = ARR_LEN(keylist);

    // Make a context of same size as keylist (END already accounted for)
    //
    REBARR *varlist = Make_Array(len);
    SET_ARR_FLAG(varlist, ARRAY_FLAG_VARLIST);

    REBCTX *context = AS_CONTEXT(varlist);

    // !!! We actually don't know if the keylist coming back from
    // Collect_Keylist_Managed was created new or reused.  Err on the safe
    // side for now, but it could also return a result so we could know
    // if it would be legal to call INIT_CTX_KEYLIST_UNIQUE.
    //
    INIT_CTX_KEYLIST_SHARED(context, keylist);

    // context[0] is an instance value of the OBJECT!/PORT!/ERROR!/MODULE!
    //
    CTX_VALUE(context)->payload.any_context.varlist = varlist;
    CTX_VALUE(context)->extra.binding = NULL;

    TERM_ARRAY_LEN(CTX_VARLIST(context), len);

    // !!! This code was inlined from Create_Frame() because it was only
    // used once here, and it filled the context vars with NONE!.  For
    // Ren-C we probably want to go with void, and also the filling
    // of parent vars will overwrite the work here.  Review.
    //
    {
        REBVAL *var = CTX_VARS_HEAD(context);
        for (; len > 1; len--, var++) // 1 is rootvar (context), already done
            SET_BLANK(var);
    }

    if (opt_parent) {
        //
        // Bitwise copy parent values (will have bits fixed by Clonify).
        // None of these should be relative, because they came from object
        // vars (that were not part of the deep copy of a function body)
        //
        memcpy(
            CTX_VARS_HEAD(context),
            CTX_VARS_HEAD(opt_parent),
            (CTX_LEN(opt_parent)) * sizeof(REBVAL)
        );

        // For values we copied that were blocks and strings, replace
        // their series components with deep copies of themselves:
        //
        Clonify_Values_Len_Managed(
            CTX_VARS_HEAD(context),
            SPECIFIED,
            CTX_LEN(context),
            TRUE,
            TS_CLONE
        );
    }

    VAL_RESET_HEADER(CTX_VALUE(context), kind);
    assert(CTX_TYPE(context) == kind);

    CTX_VALUE(context)->extra.binding = binding;

    // We should have a SELF key in all cases here.  Set it to be a copy of
    // the object we just created.  (It is indeed a copy of the [0] element,
    // but it doesn't need to be protected because the user overwriting it
    // won't destroy the integrity of the context.)
    //
    assert(CTX_KEY_SYM(context, self_index) == SYM_SELF);
    *CTX_VAR(context, self_index) = *CTX_VALUE(context);

    // !!! In Ren-C, the idea that functions are rebound when a context is
    // inherited is being deprecated.  It simply isn't viable for objects
    // with N methods to have those N methods permanently cloned in the
    // copies and have their bodies rebound to the new object.  A more
    // conventional method of `this->method()` access is needed with
    // cooperation from the evaluator, and that is slated to be `/method`
    // as a practical use of paths that implicitly start from "wherever
    // you dispatched from"
    //
    // Temporarily the old behavior is kept, so we deep copy and rebind.
    //
    if (opt_parent)
        Rebind_Context_Deep(opt_parent, context, NULL); // NULL=no more binds

    ASSERT_CONTEXT(context);

#if !defined(NDEBUG)
    PG_Reb_Stats->Objects++;
#endif

    return context;
}


//
//  Construct_Context: C
//
// Construct an object without evaluation.
// Parent can be null. Values are rebound.
//
// In R3-Alpha the CONSTRUCT native supported a mode where the following:
//
//      [a: b: 1 + 2 d: a e:]
//
// ...would have `a` and `b` will be set to 1, while `+` and `2` will be
// ignored, `d` will be the word `a` (where it knows to be bound to the a
// of the object) and `e` would be left as it was.
//
// Ren-C retakes the name CONSTRUCT to be the arity-2 object creation
// function with evaluation, and makes "raw" construction (via /ONLY on both
// 1-arity HAS and CONSTRUCT) more regimented.  The requirement for a raw
// construct is that the fields alternate SET-WORD! and then value, with
// no evaluation--hence it is possible to use any value type (a GROUP! or
// another SET-WORD!, for instance) as the value.
//
// !!! Because this is a work in progress, set-words would be gathered if
// they were used as values, so they are not currently permitted.
//
REBCTX *Construct_Context(
    enum Reb_Kind kind,
    RELVAL *head, // !!! Warning: modified binding
    REBCTX *specifier,
    REBCTX *opt_parent
) {
    REBCTX *context = Make_Selfish_Context_Detect(
        kind, // type
        NULL, // body
        head, // values to scan for toplevel set-words
        opt_parent // parent
    );

    if (head == NULL)
        return context;

    Bind_Values_Shallow(head, context);

    const RELVAL *value = head;
    for (; NOT_END(value); value += 2) {
        //
        // !!! Objects are a rewrite in progress; error messages need to
        // be improved.

        if (!IS_SET_WORD(value))
            fail (Error(RE_INVALID_TYPE, Type_Of(value)));

        if (IS_END(value + 1))
            fail (Error(RE_MISC));

        assert(!IS_SET_WORD(value + 1)); // TBD: support set words!

        REBVAL *var = GET_MUTABLE_VAR_MAY_FAIL(value, specifier);
        COPY_VALUE(var, value + 1, specifier);
    }

    return context;
}


//
//  Context_To_Array: C
//
// Return a block containing words, values, or set-word: value
// pairs for the given object. Note: words are bound to original
// object.
//
// Modes:
//     1 for word
//     2 for value
//     3 for words and values
//
REBARR *Context_To_Array(REBCTX *context, REBINT mode)
{
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);
    REBARR *block;
    REBVAL *value;
    REBCNT n;

    assert(!(mode & 4));
    block = Make_Array(CTX_LEN(context) * (mode == 3 ? 2 : 1));

    n = 1;
    for (; !IS_END(key); n++, key++, var++) {
        if (!GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) {
            if (mode & 1) {
                value = Alloc_Tail_Array(block);
                if (mode & 2) {
                    VAL_RESET_HEADER(value, REB_SET_WORD);
                    SET_VAL_FLAG(value, VALUE_FLAG_LINE);
                }
                else VAL_RESET_HEADER(value, REB_WORD);

                INIT_WORD_SPELLING(value, VAL_KEY_SPELLING(key));
                SET_VAL_FLAG(value, WORD_FLAG_BOUND); // hdr reset, !relative
                INIT_WORD_CONTEXT(value, context);
                INIT_WORD_INDEX(value, n);
            }
            if (mode & 2) {
                Append_Value(block, var);
            }
        }
    }

    return block;
}


//
//  Merge_Contexts_Selfish: C
//
// Create a child context from two parent contexts. Merge common fields.
// Values from the second parent take precedence.
//
// Deep copy and rebind the child.
//
REBCTX *Merge_Contexts_Selfish(REBCTX *parent1, REBCTX *parent2)
{
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    assert(CTX_TYPE(parent1) == CTX_TYPE(parent2));

    // Merge parent1 and parent2 words.
    // Keep the binding table.
    Collect_Keys_Start(COLLECT_ANY_WORD | COLLECT_ENSURE_SELF);

    // Setup binding table and BUF_COLLECT with parent1 words.  Don't bother
    // checking for duplicates, buffer is empty.
    //
    Collect_Context_Keys(&binder, parent1, FALSE);

    // Add parent2 words to binding table and BUF_COLLECT, and since we know
    // BUF_COLLECT isn't empty then *do* check for duplicates.
    //
    Collect_Context_Keys(&binder, parent2, TRUE);

    // Collect_Keys_End() terminates, but Collect_Context_Inner_Loop() doesn't.
    //
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Allocate child (now that we know the correct size).  Obey invariant
    // that keylists are always managed.  The BUF_COLLECT contains only
    // typesets, so no need for a specifier in the copy.
    //
    // !!! Review: should child start fresh with no meta information, or get
    // the meta information held by parents?
    //
    REBARR *keylist = Copy_Array_Shallow(BUF_COLLECT, SPECIFIED);
    MANAGE_ARRAY(keylist);
    SET_BLANK(ARR_HEAD(keylist)); // Currently no rootkey usage
    ARR_SERIES(keylist)->link.meta = NULL;

    REBCTX *merged = AS_CONTEXT(Make_Array(ARR_LEN(keylist)));
    SET_ARR_FLAG(CTX_VARLIST(merged), ARRAY_FLAG_VARLIST);
    INIT_CTX_KEYLIST_UNIQUE(merged, keylist);

    REBVAL *rootvar = Alloc_Tail_Array(CTX_VARLIST(merged));

    // !!! Currently we assume the child will be of the same type as the
    // parent...so if the parent was an OBJECT! so will the child be, if
    // the parent was an ERROR! so will the child be.  This is a new idea,
    // so review consequences.
    //
    VAL_RESET_HEADER(rootvar, CTX_TYPE(parent1));
    rootvar->payload.any_context.varlist = CTX_VARLIST(merged);
    rootvar->extra.binding = NULL;

    // Copy parent1 values:
    memcpy(
        CTX_VARS_HEAD(merged),
        CTX_VARS_HEAD(parent1),
        CTX_LEN(parent1) * sizeof(REBVAL)
    );

    // Update the child tail before making calls to CTX_VAR(), because the
    // debug build does a length check.
    //
    TERM_ARRAY_LEN(CTX_VARLIST(merged), ARR_LEN(keylist));

    // Copy parent2 values:
    REBVAL *key = CTX_KEYS_HEAD(parent2);
    REBVAL *value = CTX_VARS_HEAD(parent2);
    for (; NOT_END(key); key++, value++) {
        // no need to search when the binding table is available
        REBINT n = Try_Get_Binder_Index(&binder, VAL_KEY_CANON(key));
        assert(n != 0);
        *CTX_VAR(merged, n) = *value;
    }

    // Deep copy the child.  Context vars are REBVALs, already fully specified
    //
    Clonify_Values_Len_Managed(
        CTX_VARS_HEAD(merged),
        SPECIFIED,
        CTX_LEN(merged),
        TRUE,
        TS_CLONE
    );

    // Rebind the child
    Rebind_Context_Deep(parent1, merged, NULL);
    Rebind_Context_Deep(parent2, merged, &binder);

    // release the bind table
    Collect_Keys_End(&binder);

    // We should have gotten a SELF in the results, one way or another.
    {
        REBCNT self_index = Find_Canon_In_Context(merged, Canon(SYM_SELF), TRUE);
        assert(self_index != 0);
        assert(CTX_KEY_SYM(merged, self_index) == SYM_SELF);
        *CTX_VAR(merged, self_index) = *CTX_VALUE(merged);
    }

    SHUTDOWN_BINDER(&binder);
    return merged;
}


//
//  Resolve_Context: C
//
// Only_words can be a block of words or an index in the target
// (for new words).
//
void Resolve_Context(
    REBCTX *target,
    REBCTX *source,
    REBVAL *only_words,
    REBOOL all,
    REBOOL expand
) {
    FAIL_IF_READ_ONLY_CONTEXT(target);

    REBVAL *key;
    REBVAL *var;
    REBCNT i = 0;

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    if (IS_INTEGER(only_words)) { // Must be: 0 < i <= tail
        i = VAL_INT32(only_words); // never <= 0
        if (i == 0) i = 1;
        if (i > CTX_LEN(target)) return;
    }

    // !!! This function does its own version of resetting the bind table
    // and hence the Collect_Keys_End that would be performed in the case of
    // a `fail (Error(...))` will not properly reset it.  Because the code
    // does array expansion it cannot guarantee a fail won't happen, hence
    // the method needs to be reviewed to something that could properly
    // reset in the case of an out of memory error.
    //
    Collect_Keys_Start(COLLECT_ONLY_SET_WORDS);

    REBINT n = 0;

    // If limited resolve, tag the word ids that need to be copied:
    if (i != 0) {
        // Only the new words of the target:
        for (key = CTX_KEY(target, i); NOT_END(key); key++)
            Add_Binder_Index(&binder, VAL_KEY_CANON(key), -1);
        n = CTX_LEN(target);
    }
    else if (IS_BLOCK(only_words)) {
        // Limit exports to only these words:
        RELVAL *word = VAL_ARRAY_AT(only_words);
        for (; NOT_END(word); word++) {
            if (IS_WORD(word) || IS_SET_WORD(word)) {
                Add_Binder_Index(&binder, VAL_WORD_CANON(word), -1);
                n++;
            }
            else {
                // !!! There was no error here.  :-/  Should it be one?
            }
        }
    }

    // Expand target as needed:
    if (expand && n > 0) {
        // Determine how many new words to add:
        for (key = CTX_KEYS_HEAD(target); NOT_END(key); key++)
            if (Try_Get_Binder_Index(&binder, VAL_KEY_CANON(key)) != 0)
                --n;

        // Expand context by the amount required:
        if (n > 0) Expand_Context(target, n);
        else expand = FALSE;
    }

    // Maps a word to its value index in the source context.
    // Done by marking all source words (in bind table):
    key = CTX_KEYS_HEAD(source);
    for (n = 1; NOT_END(key); n++, key++) {
        REBSTR *canon = VAL_KEY_CANON(key);
        if (IS_VOID(only_words))
            Add_Binder_Index(&binder, canon, n);
        else {
            if (Try_Get_Binder_Index(&binder, canon) != 0) {
                Remove_Binder_Index(&binder, canon);
                Add_Binder_Index(&binder, canon, n);
            }
        }
    }

    // Foreach word in target, copy the correct value from source:
    //
    var = i != 0 ? CTX_VAR(target, i) : CTX_VARS_HEAD(target);
    key = i != 0 ? CTX_KEY(target, i) : CTX_KEYS_HEAD(target);
    for (; NOT_END(key); key++, var++) {
        REBINT m = Try_Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
        if (m != 0) {
            // "the remove succeeded, so it's marked as set now" (old comment)
            if (
                !GET_VAL_FLAG(key, TYPESET_FLAG_PROTECTED)
                && (all || IS_VOID(var))
            ) {
                if (m < 0) SET_VOID(var); // no value in source context
                else {
                    *var = *CTX_VAR(source, m);

                    // Need to also copy if the binding is lookahead (e.g.
                    // would be an infix call).
                    //
                    if (GET_VAL_FLAG(
                        CTX_KEY(source, m), TYPESET_FLAG_NO_LOOKBACK
                    )) {
                        SET_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK);
                    }
                    else
                        CLEAR_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK);
                }
            }
        }
    }

    // Add any new words and values:
    if (expand) {
        key = CTX_KEYS_HEAD(source);
        for (n = 1; NOT_END(key); n++, key++) {
            REBSTR *canon = VAL_KEY_CANON(key);
            if (Try_Remove_Binder_Index(&binder, canon) != 0) {
                // Note: no protect check is needed here
                var = Append_Context_Core(
                    target,
                    0,
                    canon,
                    NOT(GET_VAL_FLAG(key, TYPESET_FLAG_NO_LOOKBACK))
                );
                *var = *CTX_VAR(source, n);
            }
        }
    }
    else {
        // Reset bind table (do not use Collect_End):
        if (i != 0) {
            for (key = CTX_KEY(target, i); NOT_END(key); key++)
                Try_Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
        }
        else if (IS_BLOCK(only_words)) {
            RELVAL *word = VAL_ARRAY_AT(only_words);
            for (; NOT_END(word); word++) {
                if (IS_WORD(word) || IS_SET_WORD(word))
                    Try_Remove_Binder_Index(&binder, VAL_WORD_CANON(word));
            }
        }
        else {
            for (key = CTX_KEYS_HEAD(source); NOT_END(key); key++)
                Try_Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
        }
    }

    // !!! Note we explicitly do *not* use Collect_Keys_End().  See warning
    // about errors, out of memory issues, etc. at Collect_Keys_Start()
    //
    SET_SERIES_LEN(ARR_SERIES(BUF_COLLECT), 0);  // allow reuse, no terminator

    SHUTDOWN_BINDER(&binder);
}


//
//  Find_Canon_In_Context: C
//
// Search a context looking for the given canon symbol.  Return the index or
// 0 if not found.
//
REBCNT Find_Canon_In_Context(REBCTX *context, REBSTR *canon, REBOOL always)
{
    assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

    REBVAL *key = CTX_KEYS_HEAD(context);
    REBCNT len = CTX_LEN(context);

    REBCNT n;
    for (n = 1; n <= len; n++, key++) {
        if (canon == VAL_KEY_CANON(key))
            return (!always && GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) ? 0 : n;
    }

    // !!! Should this be changed to NOT_FOUND?
    return 0;
}


//
//  Select_Canon_In_Context: C
//
// Search a frame looking for the given word symbol and
// return the value for the word. Locate it by matching
// the canon word identifiers. Return NULL if not found.
//
REBVAL *Select_Canon_In_Context(REBCTX *context, REBSTR *sym)
{
    REBCNT n = Find_Canon_In_Context(context, sym, FALSE);
    if (n == 0) return NULL;

    return CTX_VAR(context, n);
}


//
//  Find_Word_In_Array: C
//
// Find word (of any type) in an array of values with linear search.
//
REBCNT Find_Word_In_Array(REBARR *array, REBCNT index, REBSTR *sym)
{
    RELVAL *value;

    for (; index < ARR_LEN(array); index++) {
        value = ARR_AT(array, index);
        if (ANY_WORD(value) && sym == VAL_WORD_CANON(value))
            return index;
    }

    return NOT_FOUND;
}


//
//  Obj_Value: C
//
// Return pointer to the nth VALUE of an object.
// Return zero if the index is not valid.
//
REBVAL *Obj_Value(REBVAL *value, REBCNT index)
{
    REBCTX *context = VAL_CONTEXT(value);

    if (index > CTX_LEN(context)) return 0;
    return CTX_VAR(context, index);
}


//
//  Init_Collector: C
//
void Init_Collector(void)
{
    // Temporary block used while scanning for frame words:
    // "just holds typesets, no GC behavior" (!!! until typeset symbols or
    // embedded tyeps are GC'd...!)
    //
    // Note that the logic inside Collect_Keylist managed assumes it's at
    // least 2 long to hold the rootkey (SYM_0) and a possible SYM_SELF
    // hidden actual key.
    //
    Set_Root_Series(TASK_BUF_COLLECT, ARR_SERIES(Make_Array(2 + 98)));
}


#ifndef NDEBUG

//
//  Assert_Context_Core: C
//
void Assert_Context_Core(REBCTX *context)
{
    REBARR *varlist = CTX_VARLIST(context);

    if (!GET_ARR_FLAG(varlist, ARRAY_FLAG_VARLIST)) {
        Debug_Fmt("Context varlist doesn't have ARRAY_FLAG_VARLIST");
        Panic_Array(varlist);
    }

    REBARR *keylist = CTX_KEYLIST(context);

    if (!CTX_KEYLIST(context)) {
        Debug_Fmt("Null keylist found in frame");
        Panic_Context(context);
    }

    if (GET_ARR_FLAG(keylist, CONTEXT_FLAG_STACK)) {
        Debug_Fmt("Keylist has a CONTEXT_FLAG_STACK, why?");
        Panic_Array(keylist);
    }

    REBVAL *rootvar = CTX_VALUE(context);
    if (!ANY_CONTEXT(rootvar)) {
        Debug_Fmt("Element at head of frame is not an ANY_CONTEXT");
        Panic_Context(context);
    }

    REBCNT keys_len = ARR_LEN(keylist);
    REBCNT vars_len = ARR_LEN(varlist);

    if (keys_len < 1) {
        Debug_Fmt("Keylist length less than one--cannot hold rootkey");
        Panic_Context(context);
    }

    if (GET_CTX_FLAG(context, CONTEXT_FLAG_STACK)) {
        assert(vars_len == 1);
    }
    else {
        if (keys_len != vars_len) {
            Debug_Fmt("Unequal lengths of key/var series in Assert_Context");
            Panic_Context(context);
        }
    }

    // The 0th key and var are special and can't be accessed with CTX_VAR
    // or CTX_KEY
    //
    if (!ANY_CONTEXT(rootvar)) {
        Debug_Fmt("First value slot in context not ANY-CONTEXT!");
        Panic_Context(context);
    }

    if (rootvar->payload.any_context.varlist != varlist) {
        Debug_Fmt("Embedded ANY-CONTEXT!'s context doesn't match context");
        Panic_Context(context);
    }

    if (GET_CTX_FLAG(context, SERIES_FLAG_INACCESSIBLE)) {
        //
        // !!! For the moment, don't check inaccessible stack frames any
        // further.  This includes varless reified frames and those reified
        // frames that are no longer on the stack.
        //
        assert(GET_CTX_FLAG(context, CONTEXT_FLAG_STACK));
        return;
    }

    REBVAL *rootkey = CTX_ROOTKEY(context);
    if (IS_FUNCTION(rootkey)) {
        if (!IS_FRAME(rootvar)) {
            Debug_Fmt("FUNCTION! found in [0] rootkey slot of non-FRAME!");
            Panic_Context(context);
        }
    }
    else if (IS_BLANK(rootkey)) {
        if (IS_FRAME(rootvar)) {
            Debug_Fmt("BLANK! found in [0] rootkey slot of FRAME!");
            Panic_Context(context);
        }

        // Note that in the future the rootkey for ordinary OBJECT! or ERROR!
        // PORT! etc. may be more interesting than BLANK
    }
    else {
        Debug_Fmt("Rootkey in context not BLANK! or FUNCTION!.");
        Panic_Context(context);
    }

    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);

    REBCNT n;
    for (n = 1; n < keys_len; n++, var++, key++) {
        if (IS_END(key) || IS_END(var)) {
            Debug_Fmt(
                "** Early %s end at index: %d",
                IS_END(key) ? "key" : "var",
                n
            );
            Panic_Context(context);
        }

        if (!IS_TYPESET(key)) {
            Debug_Fmt("** Non-typeset in context keys: %d\n", VAL_TYPE(key));
            Panic_Context(context);
        }
    }

    if (NOT_END(key) || NOT_END(var)) {
        Debug_Fmt(
            "** Missing %s end at index: %d type: %d",
            NOT_END(key) ? "key" : "var",
            n,
            NOT_END(key) ? VAL_TYPE(key) : VAL_TYPE(var)
        );
        Panic_Context(context);
    }
}
#endif
