//
//  File: %c-context.c
//  Summary: "Management routines for ANY-CONTEXT! key/value storage"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See comments in %sys-context.h for details on how contexts work.
//

#include "sys-core.h"


//
//  Alloc_Context_Core: C
//
// Create context with capacity, allocating space for both words and values.
// Context will report actual CTX_LEN() of 0 after this call.
//
REBCTX *Alloc_Context_Core(enum Reb_Kind kind, REBLEN capacity, REBFLGS flags)
{
    REBSER *keylist = Make_Series(
        capacity,  // no terminator
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED  // always shareable
    );
    mutable_LINK(Ancestor, keylist) = keylist;  // default to keylist itself
    assert(SER_USED(keylist) == 0);

    REBARR *varlist = Make_Array_Core(
        capacity + 1,  // size + room for rootvar (array terminator implicit)
        SERIES_MASK_VARLIST  // includes assurance of dynamic allocation
            | flags  // e.g. NODE_FLAG_MANAGED
    );
    mutable_MISC(VarlistMeta, varlist) = nullptr;
    mutable_BONUS(Patches, varlist) = nullptr;
    INIT_CTX_KEYLIST_UNIQUE(CTX(varlist), keylist);  // starts out unique

    RELVAL *rootvar = Alloc_Tail_Array(varlist);
    INIT_VAL_CONTEXT_ROOTVAR(rootvar, kind, varlist);

    return CTX(varlist);  // varlist pointer is context handle
}


//
//  Expand_Context_Keylist_Core: C
//
// Returns whether or not the expansion invalidated existing keys.
//
bool Expand_Context_Keylist_Core(REBCTX *context, REBLEN delta)
{
    REBSER *keylist = CTX_KEYLIST(context);
    assert(IS_KEYLIST(keylist));

    if (GET_SUBCLASS_FLAG(KEYLIST, keylist, SHARED)) {
        //
        // INIT_CTX_KEYLIST_SHARED was used to set the flag that indicates
        // this keylist is shared with one or more other contexts.  Can't
        // expand the shared copy without impacting the others, so break away
        // from the sharing group by making a new copy.
        //
        // (If all shared copies break away in this fashion, then the last
        // copy of the dangling keylist will be GC'd.)

        REBSER *copy = Copy_Series_At_Len_Extra(
            keylist,
            0,
            SER_USED(keylist),
            delta,
            SERIES_MASK_KEYLIST
        );

        // Preserve link to ancestor keylist.  Note that if it pointed to
        // itself, we update this keylist to point to itself.
        //
        // !!! Any extant derivations to the old keylist will still point to
        // that keylist at the time the derivation was performed...it will not
        // consider this new keylist to be an ancestor match.  Hence expanded
        // objects are essentially all new objects as far as derivation are
        // concerned, though they can still run against ancestor methods.
        //
        if (LINK(Ancestor, keylist) == keylist)
            mutable_LINK(Ancestor, copy) = copy;
        else
            mutable_LINK(Ancestor, copy) = LINK(Ancestor, keylist);

        Manage_Series(copy);
        INIT_CTX_KEYLIST_UNIQUE(context, copy);

        return true;
    }

    if (delta == 0)
        return false;

    // INIT_CTX_KEYLIST_UNIQUE was used to set this keylist in the
    // context, and no INIT_CTX_KEYLIST_SHARED was used by another context
    // to mark the flag indicating it's shared.  Extend it directly.

    Extend_Series(keylist, delta);

    return false;
}


//
//  Expand_Context: C
//
// Expand a context. Copy words if keylist is not unique.
//
void Expand_Context(REBCTX *context, REBLEN delta)
{
    // varlist is unique to each object--expand without making a copy.
    //
    Extend_Series(CTX_VARLIST(context), delta);

    Expand_Context_Keylist_Core(context, delta);
}


//
//  Append_Context: C
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
REBVAL *Append_Context(
    REBCTX *context,
    option(RELVAL*) any_word,  // allowed to be quoted as well
    option(const REBSYM*) symbol
) {
    REBSER *keylist = CTX_KEYLIST(context);

    // Add the key to key list
    //
    // !!! This doesn't seem to consider the shared flag of the keylist (?)
    // though the callsites seem to pre-expand with consideration for that.
    // Review why this is expanding when the callers are expanding.  Should
    // also check that redundant keys aren't getting added here.
    //
    EXPAND_SERIES_TAIL(keylist, 1);  // updates the used count
    Init_Key(
        SER_LAST(REBKEY, keylist),
        symbol
            ? unwrap(symbol)
            : VAL_WORD_SYMBOL(VAL_UNESCAPED(unwrap(any_word)))
    );

    // Add a slot to the var list
    //
    EXPAND_SERIES_TAIL(CTX_VARLIST(context), 1);

    REBVAL *value = Init_Unset(ARR_LAST(CTX_VARLIST(context)));

    if (not any_word)
        assert(symbol);
    else {
        // We want to not just add a key/value pairing to the context, but we
        // want to bind a word while we are at it.  Make sure symbol is valid.
        //
        assert(not symbol);

        REBLEN len = CTX_LEN(context); // length we just bumped
        INIT_VAL_WORD_BINDING(unwrap(any_word), context);
        INIT_VAL_WORD_PRIMARY_INDEX(unwrap(any_word), len);
    }

    return value;  // location we just added (void cell)
}


//
//  Collect_Start: C
//
// Begin using a "binder" to start mapping canon symbol names to integer
// indices.  The symbols are collected on the stack.  Use Collect_End() to
// free the map.
//
void Collect_Start(struct Reb_Collector* collector, REBFLGS flags)
{
    collector->flags = flags;
    collector->dsp_orig = DSP;
    INIT_BINDER(&collector->binder);
}


//
//  Collect_End: C
//
// Reset the bind markers in the canon series nodes so they can be reused,
// and drop the collected words from the stack.
//
void Collect_End(struct Reb_Collector *cl)
{
    REBDSP dsp = DSP;
    for (; dsp != cl->dsp_orig; --dsp) {
        const REBSYM *symbol = VAL_WORD_SYMBOL(DS_TOP);
        Remove_Binder_Index(&cl->binder, symbol);
        DS_DROP();
    }

    SHUTDOWN_BINDER(&cl->binder);
}


//
//  Collect_Context_Keys: C
//
// Collect keys from a context to the data stack, indexing them in a binder.
// If requested, it will return the first duplicate found (or null).
//
void Collect_Context_Keys(
    option(const REBSTR**) duplicate,
    struct Reb_Collector *cl,
    REBCTX *context
){
    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, context);

    if (duplicate)
        *unwrap(duplicate) = nullptr;

    for (; key != tail; ++key) {
        const REBSYM *symbol = KEY_SYMBOL(key);
        if (not Try_Add_Binder_Index(
            &cl->binder,
            symbol,
            Collector_Index_If_Pushed(cl)
        )){
            if (duplicate and not *unwrap(duplicate))  // returns first dup
                *unwrap(duplicate) = symbol;

            continue;  // don't collect if already in bind table
        }
        Init_Word(DS_PUSH(), symbol);
    }
}


//
//  Collect_Inner_Loop: C
//
// The inner recursive loop used for collecting context keys or ANY-WORD!s.
//
static void Collect_Inner_Loop(
    struct Reb_Collector *cl,
    const RELVAL *head,
    const RELVAL *tail
){
    const RELVAL *v = head;
    for (; v != tail; ++v) {
        REBCEL(const*) cell = VAL_UNESCAPED(v);  // X from ''''X
        enum Reb_Kind kind = CELL_KIND(cell);

        if (ANY_WORD_KIND(kind)) {
            if (kind != REB_SET_WORD and not (cl->flags & COLLECT_ANY_WORD))
                continue;  // kind of word we're not interested in collecting

            const REBSYM *symbol = VAL_WORD_SYMBOL(cell);
            if (not Try_Add_Binder_Index(
                &cl->binder,
                symbol,
                Collector_Index_If_Pushed(cl)
            )){
                if (cl->flags & COLLECT_NO_DUP) {
                    Collect_End(cl);  // IMPORTANT: Can't fail with binder

                    DECLARE_LOCAL (duplicate);
                    Init_Word(duplicate, VAL_WORD_SYMBOL(cell));
                    fail (Error_Dup_Vars_Raw(duplicate));  // cleans bindings
                }
                continue;  // tolerate duplicate
            }

            Init_Word(DS_PUSH(), VAL_WORD_SYMBOL(cell));

            continue;
        }

        if (not (cl->flags & COLLECT_DEEP))
            continue;

        // !!! Should this consider paths, or their embedded groups/arrays?
        // This is less certain as the purpose of collect words is not clear
        // given stepping away from SET-WORD! gathering as locals.
        // https://github.com/rebol/rebol-issues/issues/2276
        //
        if (ANY_ARRAY_KIND(kind)) {
            const RELVAL *sub_tail;
            const RELVAL *sub_at = VAL_ARRAY_AT(&sub_tail, cell);
            Collect_Inner_Loop(cl, sub_at, sub_tail);
        }
    }
}


//
//  Collect_Keylist_Managed: C
//
// Scans a block for words to extract and make into symbol keys to use for
// a context.  The Bind_Table is used to quickly determine duplicate entries.
//
// A `prior` context can be provided to serve as a basis; all the keys in
// the prior will be returned, with only new entries contributed by the
// data coming from the head[] array.  If no new values are needed (the
// array has no relevant words, or all were just duplicates of words already
// in prior) then then `prior`'s keylist may be returned.  The result is
// always pre-managed, because it may not be legal to free prior's keylist.
//
REBSER *Collect_Keylist_Managed(
    const RELVAL *head,
    const RELVAL *tail,
    option(REBCTX*) prior,
    REBFLGS flags  // see %sys-core.h for COLLECT_ANY_WORD, etc.
) {
    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    Collect_Start(cl, flags);

    if (prior) {
        const REBSTR *duplicate;
        Collect_Context_Keys(&duplicate, cl, unwrap(prior));
        assert(not duplicate);  // context should have had all unique keys
    }

    Collect_Inner_Loop(cl, head, tail);

    REBLEN num_collected = DSP - cl->dsp_orig;

    // If new keys were added to the collect buffer (as evidenced by a longer
    // collect buffer than the original keylist) then make a new keylist
    // array, otherwise reuse the original
    //
    REBSER *keylist;
    if (prior and CTX_LEN(unwrap(prior)) == num_collected)
        keylist = CTX_KEYLIST(unwrap(prior));
    else {
        keylist = Make_Series(
            num_collected,  // no terminator
            SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
        );

        STKVAL(*) word = DS_AT(cl->dsp_orig) + 1;
        REBKEY* key = SER_HEAD(REBKEY, keylist);
        for (; word != DS_TOP + 1; ++word, ++key)
            Init_Key(key, VAL_WORD_SYMBOL(word));

        SET_SERIES_USED(keylist, num_collected);  // no terminator
    }

    Collect_End(cl);
    return keylist;
}


//
//  Collect_Unique_Words_Managed: C
//
// Collect unique words from a block, possibly deeply...maybe just SET-WORD!s.
//
REBARR *Collect_Unique_Words_Managed(
    const RELVAL *head,
    const RELVAL *tail,
    REBFLGS flags,  // See COLLECT_XXX
    const REBVAL *ignorables  // BLOCK!, ANY-CONTEXT!, or BLANK! for none
){
    // We do not want to fail() during the bind at this point in time (the
    // system doesn't know how to clean up, and the only cleanup it does
    // assumes you were collecting for a keylist...it doesn't have access to
    // the "ignore" bindings.)  Do a pre-pass to fail first, if there are
    // any non-words in a block the user passed in.
    //
    if (not IS_NULLED(ignorables)) {
        const RELVAL *check_tail;
        const RELVAL *check = VAL_ARRAY_AT(&check_tail, ignorables);
        for (; check != check_tail; ++check) {
            if (not ANY_WORD_KIND(CELL_KIND(VAL_UNESCAPED(check))))
                fail (Error_Bad_Value_Core(check, VAL_SPECIFIER(ignorables)));
        }
    }

    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    Collect_Start(cl, flags);

    // The way words get "ignored" in the collecting process is to give them
    // dummy bindings so it appears they've "already been collected", but
    // not actually add them to the collection.  Then, duplicates don't cause
    // an error...so they will just be skipped when encountered.
    //
    if (IS_BLOCK(ignorables)) {
        const RELVAL *ignore_tail;
        const RELVAL *ignore = VAL_ARRAY_AT(&ignore_tail, ignorables);
        for (; ignore != ignore_tail; ++ignore) {
            REBCEL(const*) cell = VAL_UNESCAPED(ignore);
            const REBSYM *symbol = VAL_WORD_SYMBOL(cell);

            // A block may have duplicate words in it (this situation could
            // arise when `function [/test /test] []` calls COLLECT-WORDS
            // and tries to ignore both tests.  Have debug build count the
            // number (overkill, but helps test binders).
            //
            if (not Try_Add_Binder_Index(&cl->binder, symbol, -1)) {
              #if !defined(NDEBUG)
                REBINT i = Get_Binder_Index_Else_0(&cl->binder, symbol);
                assert(i < 0);
                Remove_Binder_Index_Else_0(&cl->binder, symbol);
                Add_Binder_Index(&cl->binder, symbol, i - 1);
              #endif
            }
        }
    }
    else if (ANY_CONTEXT(ignorables)) {
        const REBKEY *key_tail;
        const REBKEY *key = CTX_KEYS(&key_tail, VAL_CONTEXT(ignorables));
        for (; key != key_tail; ++key) {
            //
            // Shouldn't be possible to have an object with duplicate keys,
            // use plain Add_Binder_Index.
            //
            Add_Binder_Index(&cl->binder, KEY_SYMBOL(key), -1);
        }
    }
    else
        assert(IS_NULLED(ignorables));

    Collect_Inner_Loop(cl, head, tail);

    // We don't use Pop_Stack_Values_Core() because we want to keep the values
    // on the stack so that Collect_End() can remove them from the binder.
    //
    REBARR *array = Copy_Values_Len_Shallow_Core(
        DS_AT(cl->dsp_orig + 1),
        SPECIFIED,
        DSP - cl->dsp_orig,
        NODE_FLAG_MANAGED
    );

    if (IS_BLOCK(ignorables)) {
        const RELVAL *ignore_tail;
        const RELVAL *ignore = VAL_ARRAY_AT(&ignore_tail, ignorables);
        for (; ignore != ignore_tail; ++ignore) {
            REBCEL(const*) cell = VAL_UNESCAPED(ignore);
            const REBSYM *symbol = VAL_WORD_SYMBOL(cell);

          #if !defined(NDEBUG)
            REBINT i = Get_Binder_Index_Else_0(&cl->binder, symbol);
            assert(i < 0);
            if (i != -1) {
                Remove_Binder_Index_Else_0(&cl->binder, symbol);
                Add_Binder_Index(&cl->binder, symbol, i + 1);
                continue;
            }
          #endif

            Remove_Binder_Index(&cl->binder, symbol);
        }
    }
    else if (ANY_CONTEXT(ignorables)) {
        const REBKEY *key_tail;
        const REBKEY *key = CTX_KEYS(&key_tail, VAL_CONTEXT(ignorables));
        for (; key != key_tail; ++key)
            Remove_Binder_Index(&cl->binder, KEY_SYMBOL(key));
    }
    else
        assert(IS_NULLED(ignorables));

    Collect_End(cl);
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
    option(struct Reb_Binder*) binder
){
    const RELVAL *tail = ARR_TAIL(CTX_VARLIST(dest));
    RELVAL *head = ARR_HEAD(CTX_VARLIST(dest));
    Rebind_Values_Deep(head, tail, source, dest, binder);
}


//
//  Make_Context_Detect_Managed: C
//
// Create a context by detecting top-level set-words in an array of values.
// So if the values were the contents of the block `[a: 10 b: 20]` then the
// resulting context would be for two words, `a` and `b`.
//
// Optionally a parent context may be passed in, which will contribute its
// keylist of words to the result if provided.
//
REBCTX *Make_Context_Detect_Managed(
    enum Reb_Kind kind,
    const RELVAL *head,
    const RELVAL *tail,
    option(REBCTX*) parent
) {
    REBSER *keylist = Collect_Keylist_Managed(
        head,
        tail,
        parent,
        COLLECT_ONLY_SET_WORDS
    );

    REBLEN len = SER_USED(keylist);
    REBARR *varlist = Make_Array_Core(
        1 + len,  // needs room for rootvar
        SERIES_MASK_VARLIST
            | NODE_FLAG_MANAGED // Note: Rebind below requires managed context
    );
    SET_SERIES_LEN(varlist, 1 + len);
    mutable_MISC(VarlistMeta, varlist) = nullptr;
    mutable_BONUS(Patches, varlist) = nullptr;  // start w/no virtual binds

    REBCTX *context = CTX(varlist);

    // This isn't necessarily the clearest way to determine if the keylist is
    // shared.  Note Collect_Keylist_Managed() isn't called from anywhere
    // else, so it could probably be inlined here and it would be more
    // obvious what's going on.
    //
    if (not parent) {
        INIT_CTX_KEYLIST_UNIQUE(context, keylist);
        mutable_LINK(Ancestor, keylist) = keylist;
    }
    else {
        if (keylist == CTX_KEYLIST(unwrap(parent))) {
            INIT_CTX_KEYLIST_SHARED(context, keylist);

            // We leave the ancestor link as-is in the shared keylist--so
            // whatever the parent had...if we didn't have to make a new
            // keylist.  This means that an object may be derived, even if you
            // look at its keylist and its ancestor link points at itself.
        }
        else {
            INIT_CTX_KEYLIST_UNIQUE(context, keylist);
            mutable_LINK(Ancestor, keylist) = CTX_KEYLIST(unwrap(parent));
        }
    }

    RELVAL *var = ARR_HEAD(varlist);
    INIT_VAL_CONTEXT_ROOTVAR(var, kind, varlist);

    ++var;

    for (; len > 0; --len, ++var)  // [0] is rootvar (context), already done
        Init_Nulled(var);

    if (parent) {
        //
        // Copy parent values, and for values we copied that were blocks and
        // strings, replace their series components with deep copies.
        //
        REBVAL *dest = CTX_VARS_HEAD(context);
        const REBVAR *src_tail;
        REBVAL *src = CTX_VARS(&src_tail, unwrap(parent));
        for (; src != src_tail; ++dest, ++src) {
            REBFLGS flags = NODE_FLAG_MANAGED;  // !!! Review, what flags?
            Copy_Cell(dest, src);
            Clonify(dest, flags, TS_CLONE);
        }
    }

    if (parent)  // v-- passing in nullptr to indicate no more binds
        Rebind_Context_Deep(unwrap(parent), context, nullptr);

    ASSERT_CONTEXT(context);

  #if defined(DEBUG_COLLECT_STATS)
    PG_Reb_Stats->Objects++;
  #endif

    return context;
}


//
//  Construct_Context_Managed: C
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
REBCTX *Construct_Context_Managed(
    enum Reb_Kind kind,
    RELVAL *head,  // !!! Warning: modified binding
    const RELVAL *tail,
    REBSPC *specifier,
    option(REBCTX*) parent
){
    REBCTX *context = Make_Context_Detect_Managed(
        kind, // type
        head, // values to scan for toplevel set-words
        tail,
        parent // parent
    );

    if (not head)
        return context;

    Bind_Values_Shallow(head, tail, CTX_ARCHETYPE(context));

    const RELVAL *value = head;
    for (; value != tail; value += 2) {
        if (not IS_SET_WORD(value))
            fail (Error_Invalid_Type(VAL_TYPE(value)));

        if (value + 1 == tail)
            fail ("Unexpected end in context spec block.");

        if (IS_SET_WORD(value + 1))
            fail (Error_Invalid_Type(VAL_TYPE(value + 1))); // TBD: support

        REBVAL *var = Sink_Word_May_Fail(value, specifier);
        Derelativize(var, value + 1, specifier);
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
REBARR *Context_To_Array(const RELVAL *context, REBINT mode)
{
    REBCTX *c = VAL_CONTEXT(context);
    REBDSP dsp_orig = DSP;

    bool honor_hidden = true;  // default to not  showing hidden things
    if (IS_FRAME(context))
        honor_hidden = not IS_FRAME_PHASED(context);

    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);

    const REBPAR *param = IS_FRAME(context)
        ? ACT_PARAMS_HEAD(VAL_FRAME_PHASE(context))
        : cast_PAR(var);

    assert(!(mode & 4));

    REBLEN n = 1;
    for (; key != tail; ++key, ++var, ++param, ++n) {
        if (honor_hidden and Is_Param_Hidden(param))
            continue;

        if (mode & 1) {
            Init_Any_Word_Bound(
                DS_PUSH(),
                (mode & 2) ? REB_SET_WORD : REB_WORD,
                VAL_CONTEXT(context),
                n
            );

            if (mode & 2)
                SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
        }

        if (mode & 2) {
            //
            // Context might have voids, which denote the value have not
            // been set.  These contexts cannot be converted to blocks,
            // since user arrays may not contain void.
            //
            if (IS_NULLED(var))
                fail (Error_Null_Object_Block_Raw());

            Copy_Cell(DS_PUSH(), var);
        }
    }

    return Pop_Stack_Values_Core(
        dsp_orig,
        did (mode & 2) ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0
    );
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
    bool all,
    bool expand
) {
    FAIL_IF_READ_ONLY_SER(CTX_VARLIST(target));  // !!! should heed CONST

    REBLEN i;
    if (IS_INTEGER(only_words)) { // Must be: 0 < i <= tail
        i = VAL_INT32(only_words);
        if (i == 0)
            i = 1;
        if (i > CTX_LEN(target))
            return;
    }
    else
        i = 0;

    struct Reb_Binder binder;
    INIT_BINDER(&binder);

  blockscope {
    REBINT n = 0;

    // If limited resolve, tag the word ids that need to be copied:
    if (i != 0) {
        // Only the new words of the target:
        const REBKEY *tail;
        const REBKEY *key = CTX_KEYS(&tail, target);
        key += (i - 1);
        for (; key != tail; key++)
            Add_Binder_Index(&binder, KEY_SYMBOL(key), -1);
        n = CTX_LEN(target);
    }
    else if (IS_BLOCK(only_words)) {
        // Limit exports to only these words:
        const RELVAL *tail;
        const RELVAL *word = VAL_ARRAY_AT(&tail, only_words);
        for (; word != tail; word++) {
            if (IS_WORD(word) or IS_SET_WORD(word)) {
                Add_Binder_Index(&binder, VAL_WORD_SYMBOL(word), -1);
                n++;
            }
            else {
                // !!! There was no error here.  :-/  Should it be one?
            }
        }
    }

    // Expand target as needed:
    if (expand and n > 0) {
        // Determine how many new words to add:
        const REBKEY *tail;
        const REBKEY *key = CTX_KEYS(&tail, target);
        for (; key != tail; key++)
            if (Get_Binder_Index_Else_0(&binder, KEY_SYMBOL(key)) != 0)
                --n;

        // Expand context by the amount required:
        if (n > 0)
            Expand_Context(target, n);
        else
            expand = false;
    }
  }

    // Maps a word to its value index in the source context.
    // Done by marking all source words (in bind table):
    //
  blockscope {
    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, source);
    REBINT n = 1;
    for (; key != tail; n++, key++) {
        const REBSYM *symbol = KEY_SYMBOL(key);
        if (IS_NULLED(only_words))
            Add_Binder_Index(&binder, symbol, n);
        else {
            if (Get_Binder_Index_Else_0(&binder, symbol) != 0) {
                Remove_Binder_Index(&binder, symbol);
                Add_Binder_Index(&binder, symbol, n);
            }
        }
    }
  }

    // Foreach word in target, copy the correct value from source:
    //
  blockscope {
    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, target);
    if (i != 0)
        key += (i - 1);

    REBVAL *var = i != 0 ? CTX_VAR(target, i) : CTX_VARS_HEAD(target);
    for (; key != tail; key++, var++) {
        REBINT m = Remove_Binder_Index_Else_0(&binder, KEY_SYMBOL(key));
        if (m != 0) {
            // "the remove succeeded, so it's marked as set now" (old comment)

            if (NOT_CELL_FLAG(var, PROTECTED) and (all or IS_BAD_WORD(var))) {
                if (m < 0)
                    Init_Unset(var);  // not in source context
                else
                    Copy_Cell_Core(  // Need to preserve context flags
                        var,
                        CTX_VAR(source, m),
                        CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
                    );
            }
        }
    }
  }

    // Add any new words and values:
    if (expand) {
        const REBKEY *tail;
        const REBKEY *key = CTX_KEYS(&tail, source);
        REBINT n = 1;
        for (; key != tail; n++, key++) {
            const REBSYM *canon = KEY_SYMBOL(key);
            if (Remove_Binder_Index_Else_0(&binder, canon) != 0) {
                //
                // Note: no protect check is needed here
                //
                REBVAL *var = Append_Context(target, nullptr, canon);
                Copy_Cell_Core(  // need to preserve slot's flags
                    var,
                    CTX_VAR(source, n),
                    CELL_MASK_COPY | CELL_FLAG_VAR_MARKED_HIDDEN
                );
            }
        }
    }
    else {
        // Reset bind table.
        //
        // !!! Whatever this is doing, it doesn't appear to be able to assure
        // that the keys are there.  Hence doesn't use Remove_Binder_Index()
        // but the fault-tolerant Remove_Binder_Index_Else_0()
        //
        if (i != 0) {
            const REBKEY *tail;
            const REBKEY *key = CTX_KEYS(&tail, target);
            key += (i - 1);
            for (; key != tail; key++)
                Remove_Binder_Index_Else_0(&binder, KEY_SYMBOL(key));
        }
        else if (IS_BLOCK(only_words)) {
            const RELVAL *tail;
            const RELVAL *word = VAL_ARRAY_AT(&tail, only_words);
            for (; word != tail; word++) {
                if (IS_WORD(word) or IS_SET_WORD(word))
                    Remove_Binder_Index_Else_0(&binder, VAL_WORD_SYMBOL(word));
            }
        }
        else {
            const REBKEY *tail;
            const REBKEY *key = CTX_KEYS(&tail, source);
            for (; key != tail; key++)
                Remove_Binder_Index_Else_0(&binder, KEY_SYMBOL(key));
        }
    }

    SHUTDOWN_BINDER(&binder);
}


//
//  Find_Symbol_In_Context: C
//
// Search a context looking for the given symbol.  Return the index or 0 if
// not found.
//
// Note that since contexts like FRAME! can have multiple keys with the same
// name, the VAL_FRAME_PHASE() of the context has to be taken into account.
//
REBLEN Find_Symbol_In_Context(
    const RELVAL *context,
    const REBSYM *symbol,
    bool strict
){
    REBCTX *c = VAL_CONTEXT(context);

    bool honor_hidden = true;
    if (IS_FRAME(context)) {
        if (IS_FRAME_PHASED(context))
            honor_hidden = false;
        else {
            REBARR *varlist = CTX_VARLIST(c);
            if (GET_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED))
                fail (Error_Stale_Frame_Raw());
        }
    }

    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, c);
    const REBVAR *var = CTX_VARS_HEAD(c);

    const REBPAR *param = IS_FRAME(context)
        ? ACT_PARAMS_HEAD(VAL_FRAME_PHASE(context))
        : cast_PAR(var);

    REBLEN n;
    for (n = 1; key != tail; ++n, ++key, ++var, ++param) {
        if (strict) {
            if (symbol != KEY_SYMBOL(key))
                continue;
        }
        else {
            if (not Are_Synonyms(symbol, KEY_SYMBOL(key)))
                continue;
        }

        if (honor_hidden and Is_Param_Hidden(param))
            return 0;

        return n;
    }

    return 0;
}


//
//  Select_Symbol_In_Context: C
//
// Search a context's keylist looking for the given symbol, and return the
// value for the word.  Return NULL if the symbol is not found.
//
REBVAL *Select_Symbol_In_Context(const RELVAL *context, const REBSYM *symbol)
{
    const bool strict = false;
    REBLEN n = Find_Symbol_In_Context(context, symbol, strict);
    if (n == 0)
        return nullptr;

    return CTX_VAR(VAL_CONTEXT(context), n);
}


//
//  Obj_Value: C
//
// Return pointer to the nth VALUE of an object.
// Return NULL if the index is not valid.
//
// !!! All cases of this should be reviewed...mostly for getting an indexed
// field out of a port.  If the port doesn't have the index, should it always
// be an error?
//
REBVAL *Obj_Value(REBVAL *value, REBLEN index)
{
    REBCTX *context = VAL_CONTEXT(value);

    if (index > CTX_LEN(context)) return 0;
    return CTX_VAR(context, index);
}


//
//  Startup_Collector: C
//
void Startup_Collector(void)
{
}


//
//  Shutdown_Collector: C
//
void Shutdown_Collector(void)
{
}


#ifndef NDEBUG

//
//  Assert_Context_Core: C
//
void Assert_Context_Core(REBCTX *c)
{
    REBARR *varlist = CTX_VARLIST(c);

    if (
        (varlist->leader.bits & SERIES_MASK_VARLIST) != SERIES_MASK_VARLIST
    ){
        panic (varlist);
    }

    REBVAL *rootvar = CTX_ROOTVAR(c);
    if (not ANY_CONTEXT(rootvar) or VAL_CONTEXT(rootvar) != c)
        panic (rootvar);

    REBSER *keylist = CTX_KEYLIST(c);

    REBLEN keys_len = SER_USED(keylist);
    REBLEN vars_len = ARR_LEN(varlist);

    if (vars_len < 1)
        panic (varlist);

    if (keys_len + 1 != vars_len)
        panic (c);

    if (GET_SERIES_FLAG(CTX_VARLIST(c), INACCESSIBLE)) {
        //
        // !!! For the moment, don't check inaccessible stack frames any
        // further.  This includes varless reified frames and those reified
        // frames that are no longer on the stack.
        //
        return;
    }

    const REBKEY *key = CTX_KEYS_HEAD(c);
    REBVAL *var = CTX_VARS_HEAD(c);

    REBLEN n;
    for (n = 1; n < vars_len; n++, var++, key++) {
        if (not IS_SYMBOL(*key))
            panic (*key);

      #ifdef DEBUG_TERM_ARRAYS
        if (IS_TRASH(var)) {
            printf("** Early var end at index: %d\n", cast(int, n));
            panic (c);
        }
      #endif
    }

  #ifdef DEBUG_TERM_ARRAYS
    if (not IS_TRASH(var)) {
        printf("** Missing var end at index: %d\n", cast(int, n));
        panic (var);
    }
  #endif
}

#endif
