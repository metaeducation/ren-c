/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-frame.c
**  Summary: frame management
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/
/*
        This structure is used for:

            1. Modules
            2. Objects
            3. Function frame (arguments)
            4. Closures

        A frame is a block that begins with a special FRAME! value
        (a datatype that links to the frame word list). That value
        (SELF) is followed by the values of the words for the frame.

        FRAME BLOCK:                            WORD LIST:
        +----------------------------+          +----------------------------+
        |    Frame Datatype Value    |--Series->|         SELF word          |
        +----------------------------+          +----------------------------+
        |          Value 1           |          |          Word 1            |
        +----------------------------+          +----------------------------+
        |          Value 2           |          |          Word 2            |
        +----------------------------+          +----------------------------+
        |          Value ...         |          |          Word ...          |
        +----------------------------+          +----------------------------+

        The word list holds word datatype values of the structure:

                Type:   word, 'word, :word, word:, /word
                Symbol: actual symbol
                Canon:  canonical symbol
                Typeset: index of the value's typeset, or zero

        This list is used for binding, evaluation, type checking, and
        can also be used for molding.

        When a frame is cloned, only the value block itself need be
        created. The word list remains the same. For functions, the
        value block can be pushed on the stack.

        Frame creation patterns:

            1. Function specification to frame. Spec is scanned for
            words and datatypes, from which the word list is created.
            Closures are identical.

            2. Object specification to frame. Spec is scanned for
            word definitions and merged with parent defintions. An
            option is to allow the words to be typed.

            3. Module words to frame. They are not normally known in
            advance, they are collected during the global binding of a
            newly loaded block. This requires either preallocation of
            the module frame, or some kind of special scan to track
            the new words.

            4. Special frames, such as system natives and actions
            may be created by specific block scans and appending to
            a given frame.
*/

#include "sys-core.h"

#define CHECK_BIND_TABLE

//
//  Check_Bind_Table: C
//
void Check_Bind_Table(void)
{
    REBCNT  n;
    REBINT *binds = WORDS_HEAD(Bind_Table);

    //Debug_Fmt("Bind Table (Size: %d)", SER_LEN(Bind_Table));
    for (n = 0; n < SER_LEN(Bind_Table); n++) {
        if (binds[n]) {
            Debug_Fmt("Bind table fault: %3d to %3d (%s)", n, binds[n], Get_Sym_Name(n));
        }
    }
}


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
    REBCTX *context;
    REBARR *keylist;
    REBVAL *value;

    keylist = Make_Array(len + 1); // size + room for ROOTKEY (SYM_0)
    context = AS_CONTEXT(Make_Array(len + 1));
    SET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT);

    // Note: cannot use Append_Frame for first word.

    // context[0] is a value instance of the OBJECT!/MODULE!/PORT!/ERROR! we
    // are building which contains this context
    //
    CTX_VALUE(context)->payload.any_context.context = context;
    INIT_CONTEXT_KEYLIST(context, keylist);
    MANAGE_ARRAY(keylist);

#if !defined(NDEBUG)
    //
    // Type of the embedded object cell must be set to REB_OBJECT, REB_MODULE,
    // REB_PORT, or REB_ERROR.  This information will be mirrored in instances
    // of an object initialized with this context.
    //
    VAL_RESET_HEADER(CTX_VALUE(context), REB_TRASH);

    // !!! Modules seemed to be using a CONTEXT-style series for a spec, as
    // opposed to a simple array.  This is contentious with the plan for what
    // an object spec will wind up looking like, and may end up being the
    // "meta" information.
    //
    // !!! This should likely corrupt the data for the CTX_FUNC as well
    //
    CTX_VALUE(context)->payload.any_context.more.spec
        =  cast(REBCTX*, 0xBAADF00D);

    // Allowed to be set to NULL, but must be done so explicitly
    //
    CTX_STACKVARS(context) = cast(REBVAL*, 0xBAADF00D);
#endif

    SET_END(CTX_VARS_HEAD(context));
    SET_ARRAY_LEN(CTX_VARLIST(context), 1);

    // keylist[0] is the "rootkey" which we currently initialize to SYM_0
    //
    value = Alloc_Tail_Array(keylist);
    Val_Init_Typeset(value, ALL_64, SYM_0);

    return context;
}


//
//  Expand_Context: C
// 
// Expand a context. Copy words if flagged.
//
void Expand_Context(REBCTX *context, REBCNT delta, REBCNT copy)
{
    REBARR *keylist = CTX_KEYLIST(context);

    Extend_Series(ARR_SERIES(CTX_VARLIST(context)), delta);
    TERM_ARRAY(CTX_VARLIST(context));

    // Expand or copy WORDS block:
    if (copy) {
        REBOOL managed = GET_ARR_FLAG(keylist, SERIES_FLAG_MANAGED);
        INIT_CONTEXT_KEYLIST(
            context, Copy_Array_Extra_Shallow(keylist, delta)
        );
        if (managed) MANAGE_ARRAY(CTX_KEYLIST(context));
    }
    else {
        Extend_Series(ARR_SERIES(keylist), delta);
        TERM_ARRAY(keylist);
    }
}


//
//  Append_Context: C
// 
// Append a word to the context word list. Expands the list
// if necessary. Returns the value cell for the word. (Set to
// UNSET by default to avoid GC corruption.)
// 
// If word is not NULL, use the word sym and bind the word value,
// otherwise use sym.
//
REBVAL *Append_Context(REBCTX *context, REBVAL *word, REBSYM sym)
{
    REBARR *keylist = CTX_KEYLIST(context);
    REBVAL *value;

    // Add the key to key list
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(keylist), 1);
    value = ARR_LAST(keylist);
    Val_Init_Typeset(value, ALL_64, word ? VAL_WORD_SYM(word) : sym);
    TERM_ARRAY(keylist);

    // Add an unset value to var list
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(CTX_VARLIST(context)), 1);
    value = ARR_LAST(CTX_VARLIST(context));
    SET_UNSET(value);
    TERM_ARRAY(CTX_VARLIST(context));

    if (word) {
        REBCNT len = CTX_LEN(context);

        //
        // We want to not just add a key/value pairing to the context, but we
        // want to bind a word while we are at it.  Make sure symbol is valid.
        //
        assert(sym == SYM_0);

        // When a binding is made to an ordinary context, the value list is
        // used as the target and the index is a positive number.  Note that
        // for stack-relative bindings, the index will be negative and the
        // target will be a function's PARAMLIST series.
        //
        SET_VAL_FLAG(word, WORD_FLAG_BOUND_SPECIFIC);
        INIT_WORD_SPECIFIC(word, context);
        INIT_WORD_INDEX(word, len); // length we just bumped
    }
    else
        assert(sym != SYM_0);

    return value; // The variable value location for the key we just added.
}


//
//  Copy_Context_Shallow_Extra: C
//
// Makes a copy of a context.  If no extra storage space is requested, then
// the same keylist will be used.
//
REBCTX *Copy_Context_Shallow_Extra(REBCTX *src, REBCNT extra) {
    REBCTX *dest;

    assert(GET_ARR_FLAG(CTX_VARLIST(src), SERIES_FLAG_CONTEXT));
    assert(GET_ARR_FLAG(CTX_KEYLIST(src), SERIES_FLAG_MANAGED));

    if (extra == 0) {
        dest = AS_CONTEXT(Copy_Array_Shallow(CTX_VARLIST(src)));
        INIT_CONTEXT_KEYLIST(dest, CTX_KEYLIST(src));
    }
    else {
        dest = AS_CONTEXT(Copy_Array_Extra_Shallow(CTX_VARLIST(src), extra));
        INIT_CONTEXT_KEYLIST(
            dest,
            Copy_Array_Extra_Shallow(CTX_KEYLIST(src), extra)
        );
        MANAGE_ARRAY(CTX_KEYLIST(dest));
    }

    SET_ARR_FLAG(CTX_VARLIST(dest), SERIES_FLAG_CONTEXT);

    INIT_VAL_CONTEXT(CTX_VALUE(dest), dest);

    return dest;
}


//
//  Copy_Context_Shallow: C
//
// !!! Make this a macro when there's a place to put it.
//
REBCTX *Copy_Context_Shallow(REBCTX *src) {
    return Copy_Context_Shallow_Extra(src, 0);
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
void Collect_Keys_Start(REBCNT modes)
{
    CHECK_BIND_TABLE;

    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty

    // Add a key to slot zero.  When the keys are copied out to be the
    // keylist for a context it will be the CTX_ROOTKEY in the [0] slot.
    //
    Val_Init_Typeset(ARR_HEAD(BUF_COLLECT), ALL_64, SYM_0);

    SET_ARRAY_LEN(BUF_COLLECT, 1);
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
    TERM_ARRAY(BUF_COLLECT);

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
        keylist = Copy_Array_Shallow(BUF_COLLECT);
        MANAGE_ARRAY(keylist);
    }

    return keylist;
}


//
//  Collect_Keys_End: C
//
// Free the Bind_Table for reuse and empty the BUF_COLLECT.
//
void Collect_Keys_End(void)
{
    REBVAL *key;
    REBINT *binds = WORDS_HEAD(Bind_Table);

    // We didn't terminate as we were collecting, so terminate now.
    //
    assert(ARR_LEN(BUF_COLLECT) >= 1); // always at least [0] for rootkey
    TERM_ARRAY(BUF_COLLECT);

    // Reset binding table (note BUF_COLLECT may have expanded)
    //
    for (key = ARR_HEAD(BUF_COLLECT); NOT_END(key); key++) {
        assert(IS_TYPESET(key));
        binds[VAL_TYPESET_CANON(key)] = 0;
    }

    SET_ARRAY_LEN(BUF_COLLECT, 0); // allow reuse

    CHECK_BIND_TABLE;
}


//
//  Collect_Context_Keys: C
// 
// Collect words from a prior context.  If `check_dups` is passed in then
// there is a check for duplciates, otherwise the keys are assumed to
// be unique and copied in using `memcpy` as an optimization.
//
void Collect_Context_Keys(REBCTX *context, REBOOL check_dups)
{
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBINT *binds = WORDS_HEAD(Bind_Table);
    REBINT bind_index = ARR_LEN(BUF_COLLECT);
    REBVAL *collect; // can't set until after potential expansion...

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
    SET_SERIES_LEN(ARR_SERIES(BUF_COLLECT), bind_index);
    collect = ARR_TAIL(BUF_COLLECT);

    if (check_dups) {
        // We're adding onto the end of the collect buffer and need to
        // check for duplicates of what's already there.
        //
        for (; NOT_END(key); key++) {
            REBCNT canon = VAL_TYPESET_CANON(key);

            if (binds[canon] != 0) {
                //
                // If we found the typeset's symbol in the bind table already
                // then don't collect it in the buffer again.
                //
                continue;
            }

            // !!! At the moment objects do not heed the typesets in the
            // keys.  If they did, what sort of rule should the typesets
            // have when being inherited?
            //
            *collect++ = *key;

            binds[canon] = bind_index++;
        }

        // Increase the length of BUF_COLLLECT by how far `collect` advanced
        // (would be 0 if all the keys were duplicates...)
        //
        SET_ARRAY_LEN(
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
        SET_ARRAY_LEN(
            BUF_COLLECT, ARR_LEN(BUF_COLLECT) + CTX_LEN(context)
        );

        for (; NOT_END(key); key++) {
            REBCNT canon = VAL_TYPESET_CANON(key);
            binds[canon] = bind_index++;
        }
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
    REBINT *binds,
    REBVAL value[],
    REBCNT modes
) {
    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)) {
            if (!binds[VAL_WORD_CANON(value)]) {  // only once per word
                if (IS_SET_WORD(value) || modes & BIND_ALL) {
                    REBVAL *typeset;
                    binds[VAL_WORD_CANON(value)] = ARR_LEN(BUF_COLLECT);
                    EXPAND_SERIES_TAIL(ARR_SERIES(BUF_COLLECT), 1);
                    typeset = ARR_LAST(BUF_COLLECT);
                    Val_Init_Typeset(
                        typeset,
                        // Allow all datatypes but UNSET (initially):
                        ~FLAGIT_KIND(REB_UNSET),
                        VAL_WORD_SYM(value)
                    );
                }
            } else {
                // If word duplicated:
                if (modes & BIND_NO_DUP) {
                    // Reset binding table (note BUF_COLLECT may have expanded):
                    REBVAL *key = ARR_HEAD(BUF_COLLECT);
                    for (; NOT_END(key); key++)
                        binds[VAL_TYPESET_CANON(key)] = 0;
                    SET_ARRAY_LEN(BUF_COLLECT, 0);  // allow reuse
                    fail (Error(RE_DUP_VARS, value));
                }
            }
            continue;
        }
        // Recurse into sub-blocks:
        if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
            Collect_Context_Inner_Loop(binds, VAL_ARRAY_AT(value), modes);
        // In this mode (foreach native), do not allow non-words:
        //else if (modes & BIND_GET) fail (Error_Invalid_Arg(value));
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
// data coming from the value[] array.  If no new values are needed (the
// array has no relevant words, or all were just duplicates of words already
// in prior) then then `prior`'s keylist may be returned.  The result is
// always pre-managed, because it may not be legal to free prior's keylist.
//
// Returns:
//     A block of typesets that can be used for a context keylist.
//     If no new words, the prior list is returned.
// 
// Modes:
//     BIND_ALL  - scan all words, or just set words
//     BIND_DEEP - scan sub-blocks too
//     BIND_GET  - substitute :word with actual word
//     BIND_SELF - make sure a SELF key is added (if not already in prior)
//
REBARR *Collect_Keylist_Managed(
    REBCNT *self_index_out, // which context index SELF is in (if BIND_SELF)
    REBVAL value[],
    REBCTX *prior,
    REBCNT modes
) {
    REBINT *binds = WORDS_HEAD(Bind_Table);
    REBARR *keylist;

    Collect_Keys_Start(modes);

    if (modes & BIND_SELF) {
        if (
            !prior ||
            (*self_index_out = Find_Word_In_Context(prior, SYM_SELF, TRUE)) == 0
        ) {
            // No prior or no SELF in prior, so we'll add it as the first key
            //
            REBVAL *self_key = ARR_AT(BUF_COLLECT, 1);
            Val_Init_Typeset(self_key, ALL_64, SYM_SELF);

            // !!! See notes on the flags about why SELF is set hidden but
            // not unbindable with TYPESET_FLAG_UNBINDABLE.
            //
            SET_VAL_FLAG(self_key, TYPESET_FLAG_HIDDEN);

            binds[VAL_TYPESET_CANON(self_key)] = 1;
            *self_index_out = 1;
            SET_ARRAY_LEN(BUF_COLLECT, 2); // TASK_BUF_COLLECT is at least 2
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
    if (prior) Collect_Context_Keys(prior, FALSE);

    // Scan for words, adding them to BUF_COLLECT and bind table:
    Collect_Context_Inner_Loop(WORDS_HEAD(Bind_Table), &value[0], modes);

    keylist = Grab_Collected_Keylist_Managed(prior);

    Collect_Keys_End();

    return keylist;
}


//
//  Collect_Words_Inner_Loop: C
// 
// Used for Collect_Words() after the binds table has
// been set up.
//
static void Collect_Words_Inner_Loop(
    REBINT *binds,
    REBVAL value[],
    REBCNT modes
) {
    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)
            && !binds[VAL_WORD_CANON(value)]
            && (modes & BIND_ALL || IS_SET_WORD(value))
        ) {
            REBVAL *word;
            binds[VAL_WORD_CANON(value)] = 1;
            word = Alloc_Tail_Array(BUF_COLLECT);
            Val_Init_Word(word, REB_WORD, VAL_WORD_SYM(value));
        }
        else if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
            Collect_Words_Inner_Loop(binds, VAL_ARRAY_AT(value), modes);
    }
}


//
//  Collect_Words: C
// 
// Collect words from a prior block and new block.
//
REBARR *Collect_Words(REBVAL value[], REBVAL prior_value[], REBCNT modes)
{
    REBARR *array;
    REBCNT start;
    REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here
    CHECK_BIND_TABLE;

    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty

    if (prior_value)
        Collect_Words_Inner_Loop(binds, &prior_value[0], BIND_ALL);

    start = ARR_LEN(BUF_COLLECT);
    Collect_Words_Inner_Loop(binds, &value[0], modes);
    TERM_ARRAY(BUF_COLLECT);

    // Reset word markers:
    for (value = ARR_HEAD(BUF_COLLECT); NOT_END(value); value++)
        binds[VAL_WORD_CANON(value)] = 0;

    array = Copy_Array_At_Max_Shallow(
        BUF_COLLECT, start, ARR_LEN(BUF_COLLECT) - start
    );
    SET_ARRAY_LEN(BUF_COLLECT, 0);  // allow reuse

    CHECK_BIND_TABLE;
    return array;
}


//
//  Rebind_Context_Deep: C
// 
// Clone old context to new context knowing
// which types of values need to be copied, deep copied, and rebound.
//
void Rebind_Context_Deep(REBCTX *src, REBCTX *dst, REBINT *opt_binds)
{
    Rebind_Values_Deep(src, dst, CTX_VARS_HEAD(dst), opt_binds);
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
// to <transparent>.
//
REBCTX *Make_Selfish_Context_Detect(
    enum Reb_Kind kind,
    REBCTX *spec,
    REBVAL *stackvars,
    REBVAL value[],
    REBCTX *opt_parent
) {
    REBARR *keylist;
    REBCTX *context;
    REBCNT self_index;

#if !defined(NDEBUG)
    PG_Reb_Stats->Objects++;
#endif

    if (IS_END(value)) {
        if (opt_parent) {
            self_index = Find_Word_In_Context(opt_parent, SYM_SELF, TRUE);

            context = AS_CONTEXT(Copy_Array_Core_Managed(
                CTX_VARLIST(opt_parent),
                0, // at
                CTX_LEN(opt_parent) + 1, // tail (+1 for rootvar)
                (self_index == 0) ? 1 : 0, // one extra slot if self needed
                TRUE, // deep
                TS_CLONE // types
            ));
            SET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT);

            if (self_index == 0) {
                //
                // If we didn't find a SELF in the parent context, add it.
                // (this means we need a new keylist, too)
                //
                INIT_CONTEXT_KEYLIST(
                    context,
                    Copy_Array_Core_Managed(
                        CTX_KEYLIST(opt_parent),
                        0, // at
                        CTX_LEN(opt_parent) + 1, // tail (+1 for rootkey)
                        1, // one extra for self
                        FALSE, // !deep (keylists shouldn't need it...)
                        TS_CLONE // types (overkill for a keylist?)
                    )
                );

                self_index = CTX_LEN(opt_parent) + 1;
                Val_Init_Typeset(
                    CTX_KEY(context, self_index), ALL_64, SYM_SELF
                );

                // !!! See notes on the flags about why SELF is set hidden but
                // not unbindable with TYPESET_FLAG_UNBINDABLE.
                //
                SET_VAL_FLAG(
                    CTX_KEY(context, self_index), TYPESET_FLAG_HIDDEN
                );
            }
            else {
                // The parent had a SELF already, so we can reuse its keylist
                //
                INIT_CONTEXT_KEYLIST(context, CTX_KEYLIST(opt_parent));
            }

            INIT_VAL_CONTEXT(CTX_VALUE(context), context);
        }
        else {
            context = Alloc_Context(1); // just a self
            self_index = 1;
            Val_Init_Typeset(
                Alloc_Tail_Array(CTX_KEYLIST(context)), ALL_64, SYM_SELF
            );

            // !!! See notes on the flags about why SELF is set hidden but
            // not unbindable with TYPESET_FLAG_UNBINDABLE.
            //
            SET_VAL_FLAG(
                CTX_KEY(context, self_index), TYPESET_FLAG_HIDDEN
            );

            Alloc_Tail_Array(CTX_VARLIST(context));
        }
    }
    else {
        REBVAL *var;
        REBCNT len;

        keylist = Collect_Keylist_Managed(
            &self_index, &value[0], opt_parent, BIND_ONLY | BIND_SELF
        );
        len = ARR_LEN(keylist);

        // Make a context of same size as keylist (END already accounted for)
        //
        context = AS_CONTEXT(Make_Array(len));
        SET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT);
        INIT_CONTEXT_KEYLIST(context, keylist);

        // context[0] is an instance value of the OBJECT!/PORT!/ERROR!/MODULE!
        //
        CTX_VALUE(context)->payload.any_context.context = context;
        VAL_CONTEXT_SPEC(CTX_VALUE(context)) = NULL;
        VAL_CONTEXT_STACKVARS(CTX_VALUE(context)) = NULL;

        // !!! This code was inlined from Create_Frame() because it was only
        // used once here, and it filled the context vars with NONE!.  For
        // Ren-C we probably want to go with UNSET!, and also the filling
        // of parent vars will overwrite the work here.  Review.
        //
        SET_ARRAY_LEN(CTX_VARLIST(context), len);
        var = CTX_VARS_HEAD(context);
        for (; len > 1; len--, var++) // 1 is rootvar (context), already done
            SET_NONE(var);
        SET_END(var);

        if (opt_parent) {
            if (Reb_Opts->watch_obj_copy)
                Debug_Fmt(
                    cs_cast(BOOT_STR(RS_WATCH, 2)),
                    CTX_LEN(opt_parent),
                    CTX_KEYLIST(context)
                );

            // Bitwise copy parent values (will have bits fixed by Clonify)
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
                CTX_VAR(context, 1), CTX_LEN(context), TRUE, TS_CLONE
            );
        }
    }

    VAL_RESET_HEADER(CTX_VALUE(context), kind);
    assert(CTX_TYPE(context) == kind);

    INIT_CONTEXT_SPEC(context, spec);
    CTX_STACKVARS(context) = stackvars;

    // We should have a SELF key in all cases here.  Set it to be a copy of
    // the object we just created.  (It is indeed a copy of the [0] element,
    // but it doesn't need to be protected because the user overwriting it
    // won't destroy the integrity of the context.)
    //
    assert(CTX_KEY_CANON(context, self_index) == SYM_SELF);
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
        Rebind_Context_Deep(opt_parent, context, NULL);

    ASSERT_CONTEXT(context);

    return context;
}


//
//  Construct_Context: C
// 
// Construct an object (partial evaluation of block).
// Parent can be null. Values are rebound.
//
REBCTX *Construct_Context(
    enum Reb_Kind kind,
    REBVAL value[],
    REBOOL as_is,
    REBCTX *opt_parent
) {
    REBCTX *context = Make_Selfish_Context_Detect(
        kind, // type
        NULL, // spec
        NULL, // body
        &value[0], // values to scan for toplevel set-words
        opt_parent // parent
    );

    if (NOT_END(value)) Bind_Values_Shallow(&value[0], context);

    if (as_is) Do_Min_Construct(&value[0]);
    else Do_Construct(&value[0]);

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
                INIT_WORD_SYM(value, VAL_TYPESET_SYM(key));
                SET_VAL_FLAG(value, WORD_FLAG_BOUND_SPECIFIC);
                INIT_WORD_SPECIFIC(value, context);
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
    REBARR *keylist;
    REBCTX *child;
    REBVAL *key;
    REBVAL *value;
    REBCNT n;
    REBINT *binds = WORDS_HEAD(Bind_Table);

    assert(CTX_TYPE(parent1) == CTX_TYPE(parent2));

    // Merge parent1 and parent2 words.
    // Keep the binding table.
    Collect_Keys_Start(BIND_ALL | BIND_SELF);

    // Setup binding table and BUF_COLLECT with parent1 words.  Don't bother
    // checking for duplicates, buffer is empty.
    //
    Collect_Context_Keys(parent1, FALSE);

    // Add parent2 words to binding table and BUF_COLLECT, and since we know
    // BUF_COLLECT isn't empty then *do* check for duplicates.
    //
    Collect_Context_Keys(parent2, TRUE);

    // Collect_Keys_End() terminates, but Collect_Context_Inner_Loop() doesn't.
    //
    TERM_ARRAY(BUF_COLLECT);

    // Allocate child (now that we know the correct size).  Obey invariant
    // that keylists are always managed.
    //
    keylist = Copy_Array_Shallow(BUF_COLLECT);
    MANAGE_ARRAY(keylist);
    child = AS_CONTEXT(Make_Array(ARR_LEN(keylist)));
    SET_ARR_FLAG(CTX_VARLIST(child), SERIES_FLAG_CONTEXT);

    value = Alloc_Tail_Array(CTX_VARLIST(child));

    // !!! Currently we assume the child will be of the same type as the
    // parent...so if the parent was an OBJECT! so will the child be, if
    // the parent was an ERROR! so will the child be.  This is a new idea,
    // so review consequences.
    //
    VAL_RESET_HEADER(value, CTX_TYPE(parent1));
    INIT_CONTEXT_KEYLIST(child, keylist);
    INIT_VAL_CONTEXT(value, child);
    VAL_CONTEXT_SPEC(value) = NULL;
    VAL_CONTEXT_STACKVARS(value) = NULL;

    // Copy parent1 values:
    memcpy(
        CTX_VARS_HEAD(child),
        CTX_VARS_HEAD(parent1),
        CTX_LEN(parent1) * sizeof(REBVAL)
    );

    // Update the child tail before making calls to CTX_VAR(), because the
    // debug build does a length check.
    //
    SET_ARRAY_LEN(CTX_VARLIST(child), ARR_LEN(keylist));

    // Copy parent2 values:
    key = CTX_KEYS_HEAD(parent2);
    value = CTX_VARS_HEAD(parent2);
    for (; NOT_END(key); key++, value++) {
        // no need to search when the binding table is available
        n = binds[VAL_TYPESET_CANON(key)];
        *CTX_VAR(child, n) = *value;
    }

    // Terminate the child context:
    TERM_ARRAY(CTX_VARLIST(child));

    // Deep copy the child
    Clonify_Values_Len_Managed(
        CTX_VARS_HEAD(child), CTX_LEN(child), TRUE, TS_CLONE
    );

    // Rebind the child
    Rebind_Context_Deep(parent1, child, NULL);
    Rebind_Context_Deep(parent2, child, WORDS_HEAD(Bind_Table));

    // release the bind table
    Collect_Keys_End();

    // We should have gotten a SELF in the results, one way or another.
    {
        REBCNT self_index = Find_Word_In_Context(child, SYM_SELF, TRUE);
        assert(self_index != 0);
        assert(CTX_KEY_CANON(child, self_index) == SYM_SELF);
        *CTX_VAR(child, self_index) = *CTX_VALUE(child);
    }

    return child;
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
    REBINT *binds  = WORDS_HEAD(Bind_Table); // GC safe to do here
    REBVAL *key;
    REBVAL *var;
    REBINT n;
    REBINT m;
    REBCNT i = 0;

    CHECK_BIND_TABLE;

    FAIL_IF_LOCKED_CONTEXT(target);

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
    Collect_Keys_Start(BIND_ONLY);

    n = 0;

    // If limited resolve, tag the word ids that need to be copied:
    if (i != 0) {
        // Only the new words of the target:
        for (key = CTX_KEY(target, i); NOT_END(key); key++)
            binds[VAL_TYPESET_CANON(key)] = -1;
        n = CTX_LEN(target);
    }
    else if (IS_BLOCK(only_words)) {
        // Limit exports to only these words:
        REBVAL *words = VAL_ARRAY_AT(only_words);
        for (; NOT_END(words); words++) {
            if (IS_WORD(words) || IS_SET_WORD(words)) {
                binds[VAL_WORD_CANON(words)] = -1;
                n++;
            }
        }
    }

    // Expand target as needed:
    if (expand && n > 0) {
        // Determine how many new words to add:
        for (key = CTX_KEYS_HEAD(target); NOT_END(key); key++)
            if (binds[VAL_TYPESET_CANON(key)]) n--;

        // Expand context by the amount required:
        if (n > 0) Expand_Context(target, n, 0);
        else expand = FALSE;
    }

    // Maps a word to its value index in the source context.
    // Done by marking all source words (in bind table):
    key = CTX_KEYS_HEAD(source);
    for (n = 1; NOT_END(key); n++, key++) {
        if (IS_UNSET(only_words) || binds[VAL_TYPESET_CANON(key)])
            binds[VAL_TYPESET_CANON(key)] = n;
    }

    // Foreach word in target, copy the correct value from source:
    //
    var = i != 0 ? CTX_VAR(target, i) : CTX_VARS_HEAD(target);
    key = i != 0 ? CTX_KEY(target, i) : CTX_KEYS_HEAD(target);
    for (; NOT_END(key); key++, var++) {
        if ((m = binds[VAL_TYPESET_CANON(key)])) {
            binds[VAL_TYPESET_CANON(key)] = 0; // mark it as set
            if (
                !GET_VAL_FLAG(key, TYPESET_FLAG_LOCKED)
                && (all || IS_UNSET(var))
            ) {
                if (m < 0) SET_UNSET(var); // no value in source context
                else *var = *CTX_VAR(source, m);
                //Debug_Num("type:", VAL_TYPE(vals));
                //Debug_Str(Get_Word_Name(words));
            }
        }
    }

    // Add any new words and values:
    if (expand) {
        key = CTX_KEYS_HEAD(source);
        for (n = 1; NOT_END(key); n++, key++) {
            if (binds[VAL_TYPESET_CANON(key)]) {
                // Note: no protect check is needed here
                binds[VAL_TYPESET_CANON(key)] = 0;
                var = Append_Context(target, 0, VAL_TYPESET_CANON(key));
                *var = *CTX_VAR(source, n);
            }
        }
    }
    else {
        // Reset bind table (do not use Collect_End):
        if (i != 0) {
            for (key = CTX_KEY(target, i); NOT_END(key); key++)
                binds[VAL_TYPESET_CANON(key)] = 0;
        }
        else if (IS_BLOCK(only_words)) {
            REBVAL *words = VAL_ARRAY_AT(only_words);
            for (; NOT_END(words); words++) {
                if (IS_WORD(words) || IS_SET_WORD(words))
                    binds[VAL_WORD_CANON(words)] = 0;
            }
        }
        else {
            for (key = CTX_KEYS_HEAD(source); NOT_END(key); key++)
                binds[VAL_TYPESET_CANON(key)] = 0;
        }
    }

    CHECK_BIND_TABLE;

    // !!! Note we explicitly do *not* use Collect_Keys_End().  See warning
    // about errors, out of memory issues, etc. at Collect_Keys_Start()
    //
    SET_ARRAY_LEN(BUF_COLLECT, 0);  // allow reuse
}


//
//  Bind_Values_Inner_Loop: C
// 
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
static void Bind_Values_Inner_Loop(
    REBINT *binds,
    REBVAL value[],
    REBCTX *context,
    REBCNT mode
) {
    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)) {
            REBCNT n = binds[VAL_WORD_CANON(value)];
            if (n != 0) {
                //
                // Word is in context, bind it.  Note that VAL_RESET_HEADER
                // is a macro and VAL_TYPE is a macro, so we cannot directly
                // initialize the header while also needing the type.
                //
                enum Reb_Kind kind = VAL_TYPE(value);
                assert(n <= CTX_LEN(context));
                VAL_RESET_HEADER(value, kind);
                SET_VAL_FLAG(value, WORD_FLAG_BOUND_SPECIFIC);
                INIT_WORD_SPECIFIC(value, context);
                INIT_WORD_INDEX(value, n);
            }
            else {
                // Word is not in context, so add it if option is specified
                //
                if (
                    (mode & BIND_ALL)
                    || ((mode & BIND_SET) && (IS_SET_WORD(value)))
                ) {
                    Expand_Context(context, 1, 1);
                    Append_Context(context, value, 0);
                    binds[VAL_WORD_CANON(value)] = VAL_WORD_INDEX(value);
                }
            }
        }
        else if (ANY_ARRAY(value) && (mode & BIND_DEEP)) {
            Bind_Values_Inner_Loop(
                binds, VAL_ARRAY_AT(value), context, mode
            );
        }
        else if (IS_FUNCTION(value) && (mode & BIND_FUNC)) {
            Bind_Values_Inner_Loop(
                binds, ARR_HEAD(VAL_FUNC_BODY(value)), context, mode
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
// Different modes may be applied:
// 
//     BIND_ONLY - Only bind words found in the context.
//     BIND_ALL  - Add words to the context during the bind.
//     BIND_SET  - Add set-words to the context during the bind.
//                 (note: word must not occur before the SET)
//     BIND_DEEP - Recurse into sub-blocks.
// 
// NOTE: BIND_SET must be used carefully, because it does not
// bind prior instances of the word before the set-word. That is
// to say that forward references are not allowed.
//
void Bind_Values_Core(REBVAL value[], REBCTX *context, REBCNT mode)
{
    REBVAL *key;
    REBCNT index;
    REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

    CHECK_BIND_TABLE;

    // Note about optimization: it's not a big win to avoid the
    // binding table for short blocks (size < 4), because testing
    // every block for the rare case adds up.

    // Setup binding table
    index = 1;
    key = CTX_KEYS_HEAD(context);
    for (; index <= CTX_LEN(context); key++, index++) {
        if (!GET_VAL_FLAG(key, TYPESET_FLAG_UNBINDABLE))
            binds[VAL_TYPESET_CANON(key)] = index;
    }

    Bind_Values_Inner_Loop(binds, &value[0], context, mode);

    // Reset binding table:
    key = CTX_KEYS_HEAD(context);
    for (; NOT_END(key); key++)
        binds[VAL_TYPESET_CANON(key)] = 0;

    CHECK_BIND_TABLE;
}


//
//  Unbind_Values_Core: C
// 
// Unbind words in a block, optionally unbinding those which are
// bound to a particular target (if target is NULL, then all
// words will be unbound regardless of their VAL_WORD_CONTEXT).
//
void Unbind_Values_Core(REBVAL value[], REBCTX *context, REBOOL deep)
{
    for (; NOT_END(value); value++) {
        if (
            ANY_WORD(value)
            && (
                !context
                || (
                    IS_WORD_BOUND(value)
                    && VAL_WORD_CONTEXT(value) == context
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
    REBCNT n;

    n = Find_Word_In_Context(context, VAL_WORD_SYM(word), FALSE);
    if (n != 0) {
        UNBIND_WORD(word); // get rid of binding if it had one
        SET_VAL_FLAG(word, WORD_FLAG_BOUND_SPECIFIC);
        INIT_WORD_SPECIFIC(word, context);
        INIT_WORD_INDEX(word, n);
    }
    return n;
}


//
//  Bind_Relative_Inner_Loop: C
// 
// Recursive function for relative function word binding.
// 
static void Bind_Relative_Inner_Loop(
    REBINT *binds,
    REBFUN *func,
    REBARR *block
) {
    REBVAL *value = ARR_HEAD(block);

    for (; NOT_END(value); value++) {
        if (ANY_WORD(value)) {
            REBINT n = binds[VAL_WORD_CANON(value)];
            if (n != 0) {
                //
                // Word's canon symbol is in frame.  Relatively bind it.
                // (clear out existing header flags first).  Note that
                // VAL_RESET_HEADER is a macro and it's not safe to pass
                // it VAL_TYPE(value) directly while initializing value...
                //
                enum Reb_Kind kind = VAL_TYPE(value);
                VAL_RESET_HEADER(value, kind);
                SET_VAL_FLAG(value, WORD_FLAG_BOUND_RELATIVE);
                INIT_WORD_RELATIVE(value, func);
                INIT_WORD_INDEX(value, n);
            }
        }
        else if (ANY_ARRAY(value))
            Bind_Relative_Inner_Loop(binds, func, VAL_ARRAY(value));
    }
}


//
//  Bind_Relative_Deep: C
// 
// Bind the words of a function block to a stack frame.
// To indicate the relative nature of the index, it is set to
// a negative offset.
//
void Bind_Relative_Deep(REBFUN *func, REBARR *block)
{
    REBVAL *param;
    REBCNT index;
    REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

    // !!! Historically, relative binding was not allowed for NATIVE! or
    // other function types.  It was not desirable for user code to be
    // capable of binding to the parameters of a native.  However, for
    // purposes of debug inspection, read-only access presents an
    // interesting case.  While this avenue is explored, relative bindings
    // for all function types are being permitted.
    //
    // NOTE: This cannot work if the native is invoked framelessly.  A
    // debug mode must be enabled that prohibits the native from being
    // frameless if it's to be introspected.
    //
    /*assert(
        IS_FUNCTION(ARR_HEAD(paramlist)) || IS_CLOSURE(ARR_HEAD(paramlist))
    );*/

    CHECK_BIND_TABLE;

    //Dump_Block(words);

    // Setup binding table from the argument word list
    //
    index = 1;
    param = FUNC_PARAMS_HEAD(func);
    for (; NOT_END(param); param++, index++)
        binds[VAL_TYPESET_CANON(param)] = index;

    Bind_Relative_Inner_Loop(binds, func, block);

    // Reset binding table
    //
    param = FUNC_PARAMS_HEAD(func);
    for (; NOT_END(param); param++)
        binds[VAL_TYPESET_CANON(param)] = 0;

    CHECK_BIND_TABLE;
}


//
//  Bind_Stack_Word: C
//
void Bind_Stack_Word(REBFUN *func, REBVAL *word)
{
    REBINT index;
    enum Reb_Kind kind;

    index = Find_Param_Index(FUNC_PARAMLIST(func), VAL_WORD_SYM(word));
    if (index == 0)
        fail (Error(RE_NOT_IN_CONTEXT, word));

    kind = VAL_TYPE(word); // safe--can't pass VAL_TYPE(value) while resetting
    VAL_RESET_HEADER(word, kind);
    SET_VAL_FLAG(word, WORD_FLAG_BOUND_RELATIVE);
    INIT_WORD_RELATIVE(word, func);
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
    REBVAL value[],
    REBINT *opt_binds
) {
    for (; NOT_END(value); value++) {
        if (ANY_ARRAY(value)) {
            Rebind_Values_Deep(src, dst, VAL_ARRAY_AT(value), opt_binds);
        }
        else if (
            ANY_WORD(value)
            && GET_VAL_FLAG(value, WORD_FLAG_BOUND_SPECIFIC)
            && VAL_WORD_CONTEXT(value) == src
        ) {
            INIT_WORD_SPECIFIC(value, dst);

            if (opt_binds) {
                REBCNT canon = VAL_WORD_CANON(value);
                INIT_WORD_INDEX(value, opt_binds[canon]);
            }
        }
        else if (IS_FUNCTION(value)) {
            //
            // !!! Extremely questionable feature--walking into function
            // bodies and changing them.  This R3-Alpha concept was largely
            // broken (didn't work for closures) and created a lot of extra
            // garbage (inheriting an object's methods meant making deep
            // copies of all that object's method bodies...each time).
            // Ren-C has a different idea in the works.
            //
            Rebind_Values_Deep(
                src, dst, ARR_HEAD(VAL_FUNC_BODY(value)), opt_binds
            );
        }
    }
}


//
//  Rebind_Values_Relative_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
// !!! This function is temporary and should not be necessary after the FRAME!
// is implemented.
//
void Rebind_Values_Relative_Deep(
    REBFUN *src,
    REBFUN *dst,
    REBVAL value[]
) {
    for (; NOT_END(value); value++) {
        if (ANY_ARRAY(value)) {
            Rebind_Values_Relative_Deep(src, dst, VAL_ARRAY_AT(value));
        }
        else if (
            ANY_WORD(value)
            && GET_VAL_FLAG(value, WORD_FLAG_BOUND_RELATIVE)
            && value->payload.any_word.binding.relative == src
        ) {
            INIT_WORD_RELATIVE(value, dst);
        }
    }
}


//
//  Rebind_Values_Specifically_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
// !!! This function is temporary and should not be necessary after the FRAME!
// is implemented.
//
void Rebind_Values_Specifically_Deep(REBFUN *src, REBCTX *dst, REBVAL value[]) {
    for (; NOT_END(value); value++) {
        if (ANY_ARRAY(value)) {
            Rebind_Values_Specifically_Deep(src, dst, VAL_ARRAY_AT(value));
        }
        else if (
            ANY_WORD(value)
            && GET_VAL_FLAG(value, WORD_FLAG_BOUND_RELATIVE)
            && value->payload.any_word.binding.relative == src
        ) {
            // Note that VAL_RESET_HEADER(value...) is a macro for setting
            // value, so passing VAL_TYPE(value) which is also a macro can be
            // dangerous...
            //
            enum Reb_Kind kind = VAL_TYPE(value);
            VAL_RESET_HEADER(value, kind);
            SET_VAL_FLAG(value, WORD_FLAG_BOUND_SPECIFIC);
            INIT_WORD_SPECIFIC(value, dst);
        }
    }
}


//
//  Find_Param_Index: C
// 
// Find function param word in function "frame".
//
// !!! This is semi-redundant with similar functions for Find_Word_In_Array
// and key finding for objects, review...
//
REBCNT Find_Param_Index(REBARR *paramlist, REBSYM sym)
{
    REBVAL *params = ARR_AT(paramlist, 1);
    REBCNT len = ARR_LEN(paramlist);

    REBCNT canon = SYMBOL_TO_CANON(sym); // don't recalculate each time

    REBCNT n;
    for (n = 1; n < len; n++, params++) {
        if (
            sym == VAL_TYPESET_SYM(params)
            || canon == VAL_TYPESET_CANON(params)
        ) {
            return n;
        }
    }

    return 0;
}


//
//  Find_Word_In_Context: C
// 
// Search a context looking for the given word symbol.
// Return the context index for a word. Locate it by matching
// the canon word identifiers. Return 0 if not found.
//
REBCNT Find_Word_In_Context(REBCTX *context, REBSYM sym, REBOOL always)
{
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBCNT len = CTX_LEN(context);

    REBCNT canon = SYMBOL_TO_CANON(sym); // always compare to CANON sym

    REBCNT n;
    for (n = 1; n <= len; n++, key++) {
        if (
            sym == VAL_TYPESET_SYM(key)
            || canon == VAL_TYPESET_CANON(key)
        ) {
            return (!always && GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) ? 0 : n;
        }
    }

    // !!! Should this be changed to NOT_FOUND?
    return 0;
}


//
//  Find_Word_Value: C
// 
// Search a frame looking for the given word symbol and
// return the value for the word. Locate it by matching
// the canon word identifiers. Return NULL if not found.
//
REBVAL *Find_Word_Value(REBCTX *context, REBSYM sym)
{
    REBINT n;

    if (!context) return 0;
    n = Find_Word_In_Context(context, sym, FALSE);
    if (n == 0) return 0;
    return CTX_VAR(context, n);
}


//
//  Find_Word_In_Array: C
// 
// Find word (of any type) in an array of values with linear search.
//
REBCNT Find_Word_In_Array(REBARR *array, REBCNT index, REBSYM sym)
{
    REBVAL *value;

    for (; index < ARR_LEN(array); index++) {
        value = ARR_AT(array, index);
        if (ANY_WORD(value) && sym == VAL_WORD_CANON(value))
            return index;
    }

    return NOT_FOUND;
}


//
//  Call_For_Relative_Word: C
//
// Looks up word from a relative binding to get a specific context.  Currently
// this uses the stack (dynamic binding) but a better idea is in the works.
//
struct Reb_Call *Call_For_Relative_Word(const REBVAL *any_word, REBOOL trap) {

    // !!! This is the temporary answer to relative binding.  NewFunction
    // aims to resolve relative bindings with the help of an extra
    // parameter to Get_Var, that will be "tunneled" through ANY-SERIES!
    // REBVALs that are "viewing" an array that contains relatively
    // bound elements.  That extra parameter will fill in the *actual*
    // frame so this code will not have to guess that "the last stack
    // level is close enough"

    struct Reb_Call *call = DSF;

    for (; call != NULL; call = PRIOR_DSF(call)) {
        if (
            call->mode != CALL_MODE_FUNCTION
            || DSF_FUNC(call) != any_word->payload.any_word.binding.relative
        ) {
            continue;
        }

        if (DSF_FRAMELESS(call)) {
            //
            // !!! Trying to get a variable from a frameless native is a
            // little bit different and probably shouldn't be willing to
            // fail in an "oh it's unbound but that's okay" way.  Because
            // the data should be there, it's just been "optimized out"
            //
            // We ignore the `trap` setting for this unusual case, which
            // generally should only be possible in debugging scenarios
            // (how else would one get access to a binding to a native's
            // locals and args??)
            //
            fail (Error(RE_FRAMELESS_WORD, any_word));
        }

        // Currently the only `mode` in which a frame should be
        // considered as a legitimate match is CALL_MODE_FUNCTION.
        // Other call types include a GROUP! being recursed or
        // a function whose frame is pending and doesn't have all
        // the arguments ready yet... these shouldn't count.
        //
        assert(
            SAME_SYM(
                VAL_WORD_SYM(any_word),
                VAL_TYPESET_SYM(
                    FUNC_PARAM(DSF_FUNC(call), VAL_WORD_INDEX(any_word))
                )
            )
        );

        // Shouldn't be doing relative word lookups in durables ATM...they
        // copied their bodies in the current implementation.
        //
        assert(!IS_FUNC_DURABLE(FUNC_VALUE(DSF_FUNC(call))));

        return call;
    }

    // Historically, trying to get a value from a context not
    // on the stack in non-trapping concepts has been treated
    // the same as an unbound.  See #1914.
    //
    // !!! Is trying to access a variable that is no longer
    // available via a FRAME! that's gone off stack materially
    // different in the sense it should warrant an error in
    // all cases, trap or not?

    if (trap) return NULL;

    fail (Error(RE_NO_RELATIVE, any_word));
}


//
//  Get_Var_Core: C
// 
// Get the word--variable--value. (Generally, use the macros like
// GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
// called quite a lot and so attention to performance is important.
//
// If `trap` is TRUE, return NULL instead of raising errors on unbounds.
//
// Coded assuming most common case is trap=FALSE and writable=FALSE
//
REBVAL *Get_Var_Core(const REBVAL *any_word, REBOOL trap, REBOOL writable)
{
    if (GET_VAL_FLAG(any_word, WORD_FLAG_BOUND_SPECIFIC)) {
        //
        // The word is bound directly to a value inside a varlist, and
        // represents the zero-based offset into that series.  This is how
        // values would be picked out of object-like things...
        //
        // (Including e.g. looking up 'append' in the user context.)

        REBCTX *context = VAL_WORD_CONTEXT(any_word);
        REBCNT index = VAL_WORD_INDEX(any_word);
        REBVAL *value;

        assert(
            SAME_SYM(
                VAL_WORD_SYM(any_word), CTX_KEY_SYM(context, index)
            )
        );

        if (
            GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_STACK)
            && !GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_ACCESSIBLE)
        ) {
            // In R3-Alpha, the closure construct created a persistent object
            // which would keep all of its args, refinements, and locals
            // alive after the closure ended.  In trying to eliminate the
            // distinction between FUNCTION! and CLOSURE! in Ren-C, the
            // default is for them not to survive...though a mechanism for
            // allowing some to be marked ("<durable>") is under development.
            //
            // In the meantime, report the same error as a function which
            // is no longer on the stack.

            if (trap) return NULL;

            fail (Error(RE_NO_RELATIVE, any_word));
        }

        if (
            writable &&
            GET_VAL_FLAG(CTX_KEY(context, index), TYPESET_FLAG_LOCKED)
        ) {
            if (trap) return NULL;

            fail (Error(RE_LOCKED_WORD, any_word));
        }

        value = CTX_VAR(context, index);
        assert(!THROWN(value));
        return value;
    }
    else if (GET_VAL_FLAG(any_word, WORD_FLAG_BOUND_RELATIVE)) {
        //
        // RELATIVE CONTEXT: Word is stack-relative bound to a function with
        // no persistent varlist held by the GC.  The value *might* be found
        // on the stack (or not, if all instances of the function on the
        // call stack have finished executing).  We walk backward in the call
        // stack to see if we can find the function's "identifying series"
        // in a call frame...and take the first instance we see (even if
        // multiple invocations are on the stack, most recent wins)
        //
        // !!! This is the temporary answer to relative binding.  NewFunction
        // aims to resolve relative bindings with the help of an extra
        // parameter to Get_Var, that will be "tunneled" through ANY-SERIES!
        // REBVALs that are "viewing" an array that contains relatively
        // bound elements.  That extra parameter will fill in the *actual*
        // frame so this code will not have to guess that "the last stack
        // level is close enough"

        REBCNT index = VAL_WORD_INDEX(any_word);
        REBVAL *value;

        struct Reb_Call *call
            = Call_For_Relative_Word(any_word, trap);

        if (!call) {
            assert(trap);
            return NULL;
        }

        if (
            writable &&
            GET_VAL_FLAG(FUNC_PARAM(DSF_FUNC(call), index), TYPESET_FLAG_LOCKED)
        ) {
            if (trap) return NULL;

            fail (Error(RE_LOCKED_WORD, any_word));
        }

        value = DSF_ARG(call, index);
        assert(!THROWN(value));
        return value;
    }

    // If none of the above cases matched, then it's not bound at all.

    if (trap) return NULL;

    fail (Error(RE_NOT_BOUND, any_word));
}


//
//  Obj_Word: C
// 
// Return pointer to the nth WORD of an object.
//
REBVAL *Obj_Word(const REBVAL *value, REBCNT index)
{
    REBARR *keylist = CTX_KEYLIST(VAL_CONTEXT(value));
    return ARR_AT(keylist, index);
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
//  Init_Obj_Value: C
//
void Init_Obj_Value(REBVAL *value, REBCTX *context)
{
    assert(context);
    CLEARS(value);
    Val_Init_Object(value, context);
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
//  CTX_KEY_Debug: C
//
REBVAL *CTX_KEY_Debug(REBCTX *c, REBCNT n) {
    assert(n != 0 && n <= CTX_LEN(c));
    return CTX_KEYS_HEAD(c) + (n) - 1;
}


//
//  CTX_VAR_Debug: C
//
REBVAL *CTX_VAR_Debug(REBCTX *c, REBCNT n) {
    assert(n != 0 && n <= CTX_LEN(c));
    return CTX_VARS_HEAD(c) + (n) - 1;
}


//
//  Assert_Context_Core: C
//
void Assert_Context_Core(REBCTX *context)
{
    REBCNT n;
    REBVAL *key;
    REBVAL *var;

    REBCNT keys_len;
    REBCNT vars_len;

    if (!GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_CONTEXT)) {
        Debug_Fmt("Frame series does not have SERIES_FLAG_CONTEXT flag set");
        Panic_Context(context);
    }

    if (!ANY_CONTEXT(CTX_VALUE(context))) {
        Debug_Fmt("Element at head of frame is not an ANY_CONTEXT");
        Panic_Context(context);
    }

    if (!CTX_KEYLIST(context)) {
        Debug_Fmt("Null keylist found in frame");
        Panic_Context(context);
    }

    vars_len = ARR_LEN(CTX_VARLIST(context));
    keys_len = ARR_LEN(CTX_KEYLIST(context));

    if (keys_len < 1) {
        Debug_Fmt("Keylist length less than one--cannot hold rootkey");
        Panic_Context(context);
    }

    if (GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_STACK)) {
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
    key = CTX_ROOTKEY(context);
    var = CTX_VALUE(context);

    if (
        (IS_TYPESET(key) && VAL_TYPESET_SYM(key) == SYM_0) || ANY_FUNC(key)
    ) {
        // It's okay.  Note that in the future the rootkey for ordinary
        // OBJECT!/ERROR!/PORT! etc. may be more interesting than SYM_0
    }
    else {
        Debug_Fmt("Rootkey in context not SYM_0, CLOSURE!, FUNCTION!.");
        Panic_Context(context);
    }

    if (!ANY_CONTEXT(var)) {
        Debug_Fmt("First value slot in context not ANY-CONTEXT!");
        Panic_Context(context);
    }

    if (var->payload.any_context.context != context) {
        Debug_Fmt("Embedded ANY-CONTEXT!'s context doesn't match context");
        Panic_Context(context);
    }

    key = CTX_KEYS_HEAD(context);
    var = CTX_VARS_HEAD(context);

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
