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
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// Contexts are two arrays of equal length, which are linked together to
// describe "object-like" things (lists of TYPESET! keys and corresponding
// variable values).  They are used by OBJECT!, PORT!, FRAME!, etc.
//
// The REBCTX* is how contexts are passed around as a single pointer.  This
// pointer is actually just an array REBSER which represents the variable
// values.  The keylist can be reached through the ->link field of that
// REBSER, and the [0] value of the variable array is an "archetype instance"
// of whatever kind of REBVAL the context represents.
//
//
//             VARLIST ARRAY       ---Link-->         KEYLIST ARRAY
//  +------------------------------+        +-------------------------------+
//  +            "ROOTVAR"         |        |           "ROOTKEY"           |
//  | Archetype ANY-CONTEXT! Value |        |  Archetype ACTION!, or blank  |
//  +------------------------------+        +-------------------------------+
//  |             Value 1          |        |     Typeset (w/symbol) 1      |
//  +------------------------------+        +-------------------------------+
//  |             Value 2          |        |     Typeset (w/symbol) 2      |
//  +------------------------------+        +-------------------------------+
//  |             Value ...        |        |     Typeset (w/symbol) ...    |
//  +------------------------------+        +-------------------------------+
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
//  Alloc_Context_Core: C
//
// Create context of a given size, allocating space for both words and values.
//
// This context will not have its ANY-OBJECT! REBVAL in the [0] position fully
// configured, hence this is an "Alloc" instead of a "Make" (because there
// is still work to be done before it will pass ASSERT_CONTEXT).
//
REBCTX *Alloc_Context_Core(enum Reb_Kind kind, REBLEN capacity, REBFLGS flags)
{
    assert(not (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED));  // LINK is taken

    REBARR *varlist = Make_Array_Core(
        capacity + 1, // size + room for ROOTVAR
        SERIES_MASK_VARLIST // includes assurance of dynamic allocation
            | flags // e.g. NODE_FLAG_MANAGED
    );
    MISC_META_NODE(varlist) = nullptr; // GC sees meta object, must init

    // varlist[0] is a value instance of the OBJECT!/MODULE!/PORT!/ERROR! we
    // are building which contains this context.

    REBVAL *rootvar = RESET_CELL(
        Alloc_Tail_Array(varlist),
        kind,
        CELL_MASK_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(rootvar, varlist);
    INIT_VAL_CONTEXT_PHASE(rootvar, nullptr);
    INIT_BINDING(rootvar, UNBOUND);

    // keylist[0] is the "rootkey" which we currently initialize to an
    // unreadable BLANK!.  It is reserved for future use.

    REBARR *keylist = Make_Array_Core(
        capacity + 1, // size + room for ROOTKEY
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
    );
    Init_Unreadable_Void(Alloc_Tail_Array(keylist));

    // Default the ancestor link to be to this keylist itself.
    //
    LINK_ANCESTOR_NODE(keylist) = NOD(keylist);

    // varlists link keylists via LINK().keysource, sharable hence managed

    INIT_CTX_KEYLIST_UNIQUE(CTX(varlist), keylist);

    return CTX(varlist); // varlist pointer is context handle
}


//
//  Expand_Context_Keylist_Core: C
//
// Returns whether or not the expansion invalidated existing keys.
//
bool Expand_Context_Keylist_Core(REBCTX *context, REBLEN delta)
{
    REBARR *keylist = CTX_KEYLIST(context);

    assert(NOT_ARRAY_FLAG(keylist, IS_PARAMLIST)); // can't expand FRAME! list

    if (GET_SERIES_INFO(keylist, KEYLIST_SHARED)) {
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

        REBARR *copy = Copy_Array_Extra_Shallow(keylist, SPECIFIED, delta);

        // Preserve link to ancestor keylist.  Note that if it pointed to
        // itself, we update this keylist to point to itself.
        //
        // !!! Any extant derivations to the old keylist will still point to
        // that keylist at the time the derivation was performed...it will not
        // consider this new keylist to be an ancestor match.  Hence expanded
        // objects are essentially all new objects as far as derivation are
        // concerned, though they can still run against ancestor methods.
        //
        if (LINK_ANCESTOR(keylist) == keylist)
            LINK_ANCESTOR_NODE(copy) = NOD(copy);
        else
            LINK_ANCESTOR_NODE(copy) = LINK_ANCESTOR_NODE(keylist);

        Manage_Array(copy);
        INIT_CTX_KEYLIST_UNIQUE(context, copy);

        return true;
    }

    if (delta == 0)
        return false;

    // INIT_CTX_KEYLIST_UNIQUE was used to set this keylist in the
    // context, and no INIT_CTX_KEYLIST_SHARED was used by another context
    // to mark the flag indicating it's shared.  Extend it directly.

    Extend_Series(SER(keylist), delta);
    TERM_ARRAY_LEN(keylist, ARR_LEN(keylist));

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
    Extend_Series(SER(CTX_VARLIST(context)), delta);
    TERM_ARRAY_LEN(CTX_VARLIST(context), ARR_LEN(CTX_VARLIST(context)));

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
    RELVAL *opt_any_word,
    const REBSTR *opt_spelling
) {
    REBARR *keylist = CTX_KEYLIST(context);

    // Add the key to key list
    //
    // !!! This doesn't seem to consider the shared flag of the keylist (?)
    // though the callsites seem to pre-expand with consideration for that.
    // Review why this is expanding when the callers are expanding.  Should
    // also check that redundant keys aren't getting added here.
    //
    EXPAND_SERIES_TAIL(SER(keylist), 1);
    Init_Context_Key(
        ARR_LAST(keylist),
        opt_spelling ? opt_spelling : VAL_WORD_SPELLING(opt_any_word)
    );
    TERM_ARRAY_LEN(keylist, ARR_LEN(keylist));

    // Add a slot to the var list
    //
    EXPAND_SERIES_TAIL(SER(CTX_VARLIST(context)), 1);

    REBVAL *value = Init_Void(ARR_LAST(CTX_VARLIST(context)));
    TERM_ARRAY_LEN(CTX_VARLIST(context), ARR_LEN(CTX_VARLIST(context)));

    if (not opt_any_word)
        assert(opt_spelling);
    else {
        // We want to not just add a key/value pairing to the context, but we
        // want to bind a word while we are at it.  Make sure symbol is valid.
        //
        assert(not opt_spelling);

        REBLEN len = CTX_LEN(context); // length we just bumped
        INIT_BINDING(opt_any_word, context);
        INIT_WORD_INDEX(opt_any_word, len);
    }

    return value;  // location we just added (void cell)
}


//
//  Copy_Context_Shallow_Extra_Managed: C
//
// Makes a copy of a context.  If no extra storage space is requested, then
// the same keylist will be used.
//
REBCTX *Copy_Context_Shallow_Extra_Managed(REBCTX *src, REBLEN extra) {
    assert(GET_ARRAY_FLAG(CTX_VARLIST(src), IS_VARLIST));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(src));

    // Note that keylists contain only typesets (hence no relative values),
    // and no varlist is part of a function body.  All the values here should
    // be fully specified.
    //
    REBCTX *dest;
    REBARR *varlist;
    if (extra == 0) {
        varlist = Copy_Array_Shallow_Flags(
            CTX_VARLIST(src),
            SPECIFIED,
            SERIES_MASK_VARLIST // includes assurance of non-dynamic
                | NODE_FLAG_MANAGED
        );

        dest = CTX(varlist);

        // Leave ancestor link as-is in shared keylist.
        //
        INIT_CTX_KEYLIST_SHARED(dest, CTX_KEYLIST(src));
    }
    else {
        REBARR *keylist = Copy_Array_At_Extra_Shallow(
            CTX_KEYLIST(src),
            0,
            SPECIFIED,
            extra,
            SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
        );
        varlist = Copy_Array_At_Extra_Shallow(
            CTX_VARLIST(src),
            0,
            SPECIFIED,
            extra,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );

        dest = CTX(varlist);

        LINK_ANCESTOR_NODE(keylist) = NOD(CTX_KEYLIST(src));

        INIT_CTX_KEYLIST_UNIQUE(dest, keylist);
    }

    INIT_VAL_CONTEXT_VARLIST(CTX_ROOTVAR(dest), CTX_VARLIST(dest));

    // !!! Should the new object keep the meta information, or should users
    // have to copy that manually?  If it's copied would it be a shallow or
    // a deep copy?
    //
    MISC_META_NODE(varlist) = nullptr;

    return dest;
}


//
//  Collect_Start: C
//
// Begin using a "binder" to start mapping canon symbol names to integer
// indices.  Use Collect_End() to free the map.
//
// WARNING: This routine uses the shared BUF_COLLECT rather than
// targeting a new series directly.  This way a context can be
// allocated at exactly the right length when contents are copied.
// Therefore do not call code that might call BIND or otherwise
// make use of the Bind_Table or BUF_COLLECT.
//
void Collect_Start(struct Reb_Collector* collector, REBFLGS flags)
{
    collector->flags = flags;
    collector->dsp_orig = DSP;
    collector->index = 1;
    INIT_BINDER(&collector->binder);

    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty
}


//
//  Grab_Collected_Array_Managed: C
//
REBARR *Grab_Collected_Array_Managed(
    struct Reb_Collector *collector,
    REBFLGS flags
){
    UNUSED(collector); // not needed at the moment

    // We didn't terminate as we were collecting, so terminate now.
    //
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // If no new words, prior context.  Note length must include the slot
    // for the rootkey...and note also this means the rootkey cell *may*
    // be shared between all keylists when you pass in a prior.
    //
    // All collected values should have been fully specified.
    //
    return Copy_Array_Shallow_Flags(
        BUF_COLLECT,
        SPECIFIED,
        flags | NODE_FLAG_MANAGED
    );
}


//
//  Collect_End: C
//
// Reset the bind markers in the canon series nodes so they can be reused,
// and empty the BUF_COLLECT.
//
void Collect_End(struct Reb_Collector *cl)
{
    // We didn't terminate as we were collecting, so terminate now.
    //
    TERM_ARRAY_LEN(BUF_COLLECT, ARR_LEN(BUF_COLLECT));

    // Reset binding table (note BUF_COLLECT may have expanded)
    //
    RELVAL *v =
        (cl == NULL or (cl->flags & COLLECT_AS_TYPESET))
            ? ARR_HEAD(BUF_COLLECT) + 1
            : ARR_HEAD(BUF_COLLECT);
    for (; NOT_END(v); ++v) {
        const REBSTR *canon =
            (cl == NULL or (cl->flags & COLLECT_AS_TYPESET))
                ? VAL_KEY_CANON(v)
                : VAL_WORD_CANON(v);

        if (cl != NULL) {
            Remove_Binder_Index(&cl->binder, canon);
            continue;
        }

        // !!! This doesn't have a "binder" available to clear out the
        // keys with.  The nature of handling error states means that if
        // a thread-safe binding system was implemented, we'd have to know
        // which thread had the error to roll back any binding structures.
        // For now just zero it out based on the collect buffer.
        //
        assert(
            MISC(canon).bind_index.high != 0
            or MISC(canon).bind_index.low != 0
        );
        MISC(canon).bind_index.high = 0;
        MISC(canon).bind_index.low = 0;
    }

    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 0);

    if (cl != NULL)
        SHUTDOWN_BINDER(&cl->binder);
}


//
//  Collect_Context_Keys: C
//
// Collect keys from a prior context into BUF_COLLECT for a new context.
//
void Collect_Context_Keys(
    struct Reb_Collector *cl,
    REBCTX *context,
    bool check_dups // check for duplicates (otherwise assume unique)
){
    assert(cl->flags & COLLECT_AS_TYPESET);

    REBVAL *key = CTX_KEYS_HEAD(context);

    assert(cl->index >= 1); // 0 in bind table means "not present"

    // This is necessary so Blit_Cell() below isn't overwriting memory that
    // BUF_COLLECT does not own.  (It may make the buffer capacity bigger than
    // necessary if duplicates are found, but the actual buffer length will be
    // set correctly by the end.)
    //
    EXPAND_SERIES_TAIL(SER(BUF_COLLECT), CTX_LEN(context));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, cl->index);

    RELVAL *collect = ARR_TAIL(BUF_COLLECT); // get address *after* expansion

    if (check_dups) {
        for (; NOT_END(key); key++) {
            const REBSTR *canon = VAL_KEY_CANON(key);
            if (not Try_Add_Binder_Index(&cl->binder, canon, cl->index))
                continue; // don't collect if already in bind table

            ++cl->index;

            Blit_Cell(collect, key); // fast copy, matching cell formats
            ++collect;
        }

        // Mark length of BUF_COLLECT by how far `collect` advanced
        // (would be 0 if all the keys were duplicates...)
        //
        SET_ARRAY_LEN_NOTERM(
            BUF_COLLECT,
            ARR_LEN(BUF_COLLECT) + (collect - ARR_TAIL(BUF_COLLECT))
        );
    }
    else {
        // Optimized add of all keys to bind table and collect buffer.
        //
        for (; NOT_END(key); ++key, ++collect, ++cl->index) {
            Blit_Cell(collect, key);
            Add_Binder_Index(&cl->binder, VAL_KEY_CANON(key), cl->index);
        }
        SET_ARRAY_LEN_NOTERM(
            BUF_COLLECT, ARR_LEN(BUF_COLLECT) + CTX_LEN(context)
        );
    }

    // BUF_COLLECT doesn't get terminated as its being built, but it gets
    // terminated in Collect_Keys_End()
}


//
//  Collect_Inner_Loop: C
//
// The inner recursive loop used for collecting context keys or ANY-WORD!s.
//
static void Collect_Inner_Loop(struct Reb_Collector *cl, const RELVAL *head)
{
    for (; NOT_END(head); ++head) {
        REBCEL(const*) cell = VAL_UNESCAPED(head); // cell of X from '''X
        enum Reb_Kind kind = CELL_KIND(cell);

        if (ANY_WORD_KIND(kind)) {
            if (kind != REB_SET_WORD and not (cl->flags & COLLECT_ANY_WORD))
                continue; // kind of word we're not interested in collecting

            const REBSTR *canon = VAL_WORD_CANON(cell);
            if (not Try_Add_Binder_Index(&cl->binder, canon, cl->index)) {
                if (cl->flags & COLLECT_NO_DUP) {
                    DECLARE_LOCAL (duplicate);
                    Init_Word(duplicate, VAL_WORD_SPELLING(cell));
                    fail (Error_Dup_Vars_Raw(duplicate)); // cleans bindings
                }
                continue; // tolerate duplicate
            }

            ++cl->index;

            EXPAND_SERIES_TAIL(SER(BUF_COLLECT), 1);
            if (cl->flags & COLLECT_AS_TYPESET)
                Init_Context_Key(
                    ARR_LAST(BUF_COLLECT),
                    VAL_WORD_SPELLING(cell)
                );
            else
                Init_Word(ARR_LAST(BUF_COLLECT), VAL_WORD_SPELLING(cell));

            continue;
        }

        if (not (cl->flags & COLLECT_DEEP))
            continue;

        // !!! Should this consider paths, or their embedded groups/arrays?
        // This is less certain as the purpose of collect words is not clear
        // given stepping away from SET-WORD! gathering as locals.
        // https://github.com/rebol/rebol-issues/issues/2276
        //
        if (ANY_ARRAY_KIND(kind))
            Collect_Inner_Loop(cl, VAL_ARRAY_AT(cell));
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
    REBLEN *self_index_out, // which context index SELF is in (if COLLECT_SELF)
    const RELVAL *head,
    REBCTX *prior,
    REBFLGS flags // see %sys-core.h for COLLECT_ANY_WORD, etc.
) {
    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    assert(not (flags & COLLECT_AS_TYPESET)); // not optional, we add it
    Collect_Start(cl, flags | COLLECT_AS_TYPESET);

    // Leave the [0] slot blank while collecting (ROOTKEY/ROOTPARAM), but
    // valid (but "unreadable") bits so that the copy will still work.
    //
    Init_Unreadable_Void(ARR_HEAD(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);

    if (flags & COLLECT_ENSURE_SELF) {
        if (
            not prior or (
                0 == (*self_index_out = Find_Canon_In_Context(
                    prior,
                    Canon(SYM_SELF),
                    true
                ))
            )
        ) {
            // No prior or no SELF in prior, so we'll add it as the first key
            //
            RELVAL *self_key = Init_Context_Key(
                ARR_AT(BUF_COLLECT, 1),
                Canon(SYM_SELF)
            );

            // !!! See notes on the flags about why SELF is set hidden but
            // not unbindable with REB_TS_UNBINDABLE.
            //
            TYPE_SET(self_key, REB_TS_HIDDEN);

            assert(cl->index == 1);
            Add_Binder_Index(&cl->binder, VAL_KEY_CANON(self_key), cl->index);
            *self_index_out = cl->index;
            ++cl->index;
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
    if (prior)
        Collect_Context_Keys(cl, prior, false);

    // Scan for words, adding them to BUF_COLLECT and bind table:
    Collect_Inner_Loop(cl, head);

    // If new keys were added to the collect buffer (as evidenced by a longer
    // collect buffer than the original keylist) then make a new keylist
    // array, otherwise reuse the original
    //
    REBARR *keylist;
    if (prior != NULL and ARR_LEN(CTX_KEYLIST(prior)) == ARR_LEN(BUF_COLLECT))
        keylist = CTX_KEYLIST(prior);
    else
        keylist = Grab_Collected_Array_Managed(cl, SERIES_MASK_KEYLIST);

    // !!! Usages of the rootkey for non-FRAME! contexts is open for future,
    // but it's set to an unreadable void at the moment just to make sure it
    // doesn't get used on accident.
    //
    ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(keylist));

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
    REBFLGS flags,  // See COLLECT_XXX
    const REBVAL *ignore  // BLOCK!, ANY-CONTEXT!, or BLANK! for none
){
    // We do not want to fail() during the bind at this point in time (the
    // system doesn't know how to clean up, and the only cleanup it does
    // assumes you were collecting for a keylist...it doesn't have access to
    // the "ignore" bindings.)  Do a pre-pass to fail first, if there are
    // any non-words in a block the user passed in.
    //
    if (not IS_NULLED(ignore)) {
        const RELVAL *check = VAL_ARRAY_AT(ignore);
        for (; NOT_END(check); ++check) {
            if (not ANY_WORD_KIND(CELL_KIND(VAL_UNESCAPED(check))))
                fail (Error_Bad_Value_Core(check, VAL_SPECIFIER(ignore)));
        }
    }

    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    assert(not (flags & COLLECT_AS_TYPESET)); // only used for making keylists
    Collect_Start(cl, flags);

    assert(ARR_LEN(BUF_COLLECT) == 0); // should be empty

    // The way words get "ignored" in the collecting process is to give them
    // dummy bindings so it appears they've "already been collected", but
    // not actually add them to the collection.  Then, duplicates don't cause
    // an error...so they will just be skipped when encountered.
    //
    if (IS_BLOCK(ignore)) {
        const RELVAL *item = VAL_ARRAY_AT(ignore);
        for (; NOT_END(item); ++item) {
            REBCEL(const*) unescaped = VAL_UNESCAPED(item); // allow 'X, ''#Y
            const REBSTR *canon = VAL_WORD_CANON(unescaped);

            // A block may have duplicate words in it (this situation could
            // arise when `function [/test /test] []` calls COLLECT-WORDS
            // and tries to ignore both tests.  Have debug build count the
            // number (overkill, but helps test binders).
            //
            if (not Try_Add_Binder_Index(&cl->binder, canon, -1)) {
              #if !defined(NDEBUG)
                REBINT i = Get_Binder_Index_Else_0(&cl->binder, canon);
                assert(i < 0);
                Remove_Binder_Index_Else_0(&cl->binder, canon);
                Add_Binder_Index(&cl->binder, canon, i - 1);
              #endif
            }
        }
    }
    else if (ANY_CONTEXT(ignore)) {
        REBVAL *key = CTX_KEYS_HEAD(VAL_CONTEXT(ignore));
        for (; NOT_END(key); ++key) {
            //
            // Shouldn't be possible to have an object with duplicate keys,
            // use plain Add_Binder_Index.
            //
            Add_Binder_Index(&cl->binder, VAL_KEY_CANON(key), -1);
        }
    }
    else
        assert(IS_NULLED(ignore));

    Collect_Inner_Loop(cl, head);

    REBARR *array = Grab_Collected_Array_Managed(cl, SERIES_FLAGS_NONE);

    if (IS_BLOCK(ignore)) {
        const RELVAL *item = VAL_ARRAY_AT(ignore);
        for (; NOT_END(item); ++item) {
            REBCEL(const*) unescaped = VAL_UNESCAPED(item); // allow 'X, ''#Y
            const REBSTR *canon = VAL_WORD_CANON(unescaped);

          #if !defined(NDEBUG)
            REBINT i = Get_Binder_Index_Else_0(&cl->binder, canon);
            assert(i < 0);
            if (i != -1) {
                Remove_Binder_Index_Else_0(&cl->binder, canon);
                Add_Binder_Index(&cl->binder, canon, i + 1);
                continue;
            }
          #endif

            Remove_Binder_Index(&cl->binder, canon);
        }
    }
    else if (ANY_CONTEXT(ignore)) {
        REBVAL *key = CTX_KEYS_HEAD(VAL_CONTEXT(ignore));
        for (; NOT_END(key); ++key)
            Remove_Binder_Index(&cl->binder, VAL_KEY_CANON(key));
    }
    else
        assert(IS_NULLED(ignore));

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
    struct Reb_Binder *opt_binder
) {
    Rebind_Values_Deep(CTX_VARS_HEAD(dest), source, dest, opt_binder);
}


//
//  Make_Selfish_Context_Detect_Managed: C
//
// Create a context by detecting top-level set-words in an array of values.
// So if the values were the contents of the block `[a: 10 b: 20]` then the
// resulting context would be for two words, `a` and `b`.
//
// Optionally a parent context may be passed in, which will contribute its
// keylist of words to the result if provided.
//
// The resulting context will have a SELF: defined as a hidden key (will not
// show up in `words of` but will be bound during creation).  As part of
// the migration away from SELF being a keyword, the logic for adding and
// managing SELF has been confined to this function (called by `make object!`
// and some other context-creating routines).  This will ultimately turn
// into something paralleling the non-keyword definitional RETURN:, where
// the generators (like OBJECT) will be taking responsibility for it.
//
// This routine will *always* make a context with a SELF.  This lacks the
// nuance that is expected of the generators, which will have an equivalent
// to `<with> return` to suppress it.
//
REBCTX *Make_Selfish_Context_Detect_Managed(
    enum Reb_Kind kind,
    const RELVAL *head,
    REBCTX *opt_parent
) {
    REBLEN self_index;
    REBARR *keylist = Collect_Keylist_Managed(
        &self_index,
        head,
        opt_parent,
        COLLECT_ONLY_SET_WORDS | COLLECT_ENSURE_SELF
    );

    REBLEN len = ARR_LEN(keylist);
    REBARR *varlist = Make_Array_Core(
        len,
        SERIES_MASK_VARLIST
            | NODE_FLAG_MANAGED // Note: Rebind below requires managed context
    );
    TERM_ARRAY_LEN(varlist, len);
    MISC_META_NODE(varlist) = nullptr;  // clear meta object (GC sees this)

    REBCTX *context = CTX(varlist);

    // This isn't necessarily the clearest way to determine if the keylist is
    // shared.  Note Collect_Keylist_Managed() isn't called from anywhere
    // else, so it could probably be inlined here and it would be more
    // obvious what's going on.
    //
    if (opt_parent == NULL) {
        INIT_CTX_KEYLIST_UNIQUE(context, keylist);
        LINK_ANCESTOR_NODE(keylist) = NOD(keylist);
    }
    else {
        if (keylist == CTX_KEYLIST(opt_parent)) {
            INIT_CTX_KEYLIST_SHARED(context, keylist);

            // We leave the ancestor link as-is in the shared keylist--so
            // whatever the parent had...if we didn't have to make a new
            // keylist.  This means that an object may be derived, even if you
            // look at its keylist and its ancestor link points at itself.
        }
        else {
            INIT_CTX_KEYLIST_UNIQUE(context, keylist);
            LINK_ANCESTOR_NODE(keylist) = NOD(CTX_KEYLIST(opt_parent));
        }
    }

    // context[0] is an instance value of the OBJECT!/PORT!/ERROR!/MODULE!
    //
    REBVAL *var = RESET_CELL(
        ARR_HEAD(varlist),
        kind,
        CELL_MASK_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(var, varlist);
    INIT_VAL_CONTEXT_PHASE(var, nullptr);
    INIT_BINDING(var, UNBOUND);

    ++var;

    for (; len > 1; --len, ++var) // [0] is rootvar (context), already done
        Init_Nulled(var);

    if (opt_parent != NULL) {
        //
        // Copy parent values, and for values we copied that were blocks and
        // strings, replace their series components with deep copies.
        //
        REBVAL *dest = CTX_VARS_HEAD(context);
        REBVAL *src = CTX_VARS_HEAD(opt_parent);
        for (; NOT_END(src); ++dest, ++src) {
            REBFLGS flags = NODE_FLAG_MANAGED;  // !!! Review, what flags?
            Move_Value(dest, src);
            Clonify(dest, flags, TS_CLONE);
        }
    }

    // We should have a SELF key in all cases here.  Set it to be a copy of
    // the object we just created.  (It is indeed a copy of the [0] element,
    // but it doesn't need to be protected because the user overwriting it
    // won't destroy the integrity of the context.)
    //
    assert(CTX_KEY_SYM(context, self_index) == SYM_SELF);
    Move_Value(CTX_VAR(context, self_index), CTX_ARCHETYPE(context));

    if (opt_parent)
        Rebind_Context_Deep(opt_parent, context, NULL); // NULL=no more binds

    ASSERT_CONTEXT(context);

#if !defined(NDEBUG)
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
    RELVAL *head, // !!! Warning: modified binding
    REBSPC *specifier,
    REBCTX *opt_parent
) {
    REBCTX *context = Make_Selfish_Context_Detect_Managed(
        kind, // type
        head, // values to scan for toplevel set-words
        opt_parent // parent
    );

    if (not head)
        return context;

    Bind_Values_Shallow(head, context);

    const RELVAL *value = head;
    for (; NOT_END(value); value += 2) {
        if (not IS_SET_WORD(value))
            fail (Error_Invalid_Type(VAL_TYPE(value)));

        if (IS_END(value + 1))
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
REBARR *Context_To_Array(REBCTX *context, REBINT mode)
{
    REBDSP dsp_orig = DSP;

    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);

    assert(!(mode & 4));

    REBLEN n = 1;
    for (; NOT_END(key); n++, key++, var++) {
        if (not Is_Param_Hidden(key)) {
            if (mode & 1) {
                Init_Any_Word_Bound(
                    DS_PUSH(),
                    (mode & 2) ? REB_SET_WORD : REB_WORD,
                    VAL_KEY_SPELLING(key),
                    context,
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

                Move_Value(DS_PUSH(), var);
            }
        }
    }

    return Pop_Stack_Values_Core(
        dsp_orig,
        did (mode & 2) ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0
    );
}


//
//  Merge_Contexts_Selfish_Managed: C
//
// Create a child context from two parent contexts. Merge common fields.
// Values from the second parent take precedence.
//
// Deep copy and rebind the child.
//
REBCTX *Merge_Contexts_Selfish_Managed(REBCTX *parent1, REBCTX *parent2)
{
    if (parent2 != NULL) {
        assert(CTX_TYPE(parent1) == CTX_TYPE(parent2));
        fail ("Multiple inheritance of object support removed from Ren-C");
    }

    // Merge parent1 and parent2 words.
    // Keep the binding table.

    struct Reb_Collector collector;
    Collect_Start(
        &collector,
        COLLECT_ANY_WORD | COLLECT_ENSURE_SELF | COLLECT_AS_TYPESET
    );

    // Leave the [0] slot blank while collecting (ROOTKEY/ROOTPARAM), but
    // valid (but "unreadable") bits so that the copy will still work.
    //
    Init_Unreadable_Void(ARR_HEAD(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);

    // Setup binding table and BUF_COLLECT with parent1 words.  Don't bother
    // checking for duplicates, buffer is empty.
    //
    Collect_Context_Keys(&collector, parent1, false);

    // Add parent2 words to binding table and BUF_COLLECT, and since we know
    // BUF_COLLECT isn't empty then *do* check for duplicates.
    //
    Collect_Context_Keys(&collector, parent2, true);

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
    REBARR *keylist = Copy_Array_Shallow_Flags(
        BUF_COLLECT,
        SPECIFIED,
        NODE_FLAG_MANAGED
    );
    Init_Unreadable_Void(ARR_HEAD(keylist)); // Currently no rootkey usage

    if (parent1 == NULL)
        LINK_ANCESTOR_NODE(keylist) = NOD(keylist);
    else
        LINK_ANCESTOR_NODE(keylist) = NOD(CTX_KEYLIST(parent1));

    REBARR *varlist = Make_Array_Core(
        ARR_LEN(keylist),
        SERIES_MASK_VARLIST
            | NODE_FLAG_MANAGED // rebind below requires managed context
    );
    MISC_META_NODE(varlist) = nullptr;  // GC sees, it must be initialized

    REBCTX *merged = CTX(varlist);
    INIT_CTX_KEYLIST_UNIQUE(merged, keylist);

    // !!! Currently we assume the child will be of the same type as the
    // parent...so if the parent was an OBJECT! so will the child be, if
    // the parent was an ERROR! so will the child be.  This is a new idea,
    // so review consequences.
    //
    REBVAL *rootvar = RESET_CELL(
        ARR_HEAD(varlist),
        CTX_TYPE(parent1),
        CELL_MASK_CONTEXT
    );
    INIT_VAL_CONTEXT_VARLIST(rootvar, varlist);
    INIT_VAL_CONTEXT_PHASE(rootvar, nullptr);
    INIT_BINDING(rootvar, UNBOUND);

    // Copy parent1 values.  (Can't use memcpy() because it would copy things
    // like protected bits...)
    //
    REBVAL *copy_dest = CTX_VARS_HEAD(merged);
    const REBVAL *copy_src = CTX_VARS_HEAD(parent1);
    for (; NOT_END(copy_src); ++copy_src, ++copy_dest)
        Move_Var(copy_dest, copy_src);

    // Update the child tail before making calls to CTX_VAR(), because the
    // debug build does a length check.
    //
    TERM_ARRAY_LEN(varlist, ARR_LEN(keylist));

    // Copy parent2 values:
    REBVAL *key = CTX_KEYS_HEAD(parent2);
    REBVAL *value = CTX_VARS_HEAD(parent2);
    for (; NOT_END(key); key++, value++) {
        // no need to search when the binding table is available
        REBINT n = Get_Binder_Index_Else_0(
            &collector.binder, VAL_KEY_CANON(key)
        );
        assert(n != 0);

        // Deep copy the child.
        // Context vars are REBVALs, already fully specified
        //
        REBFLGS flags = NODE_FLAG_MANAGED;  // !!! Review, which flags?
        Clonify(
            Move_Value(CTX_VAR(merged, n), value),
            flags,
            TS_CLONE
        );
    }

    // Rebind the child
    //
    Rebind_Context_Deep(parent1, merged, NULL);
    Rebind_Context_Deep(parent2, merged, &collector.binder);

    // release the bind table
    //
    Collect_End(&collector);

    // We should have gotten a SELF in the results, one way or another.
    //
    REBLEN self_index = Find_Canon_In_Context(merged, Canon(SYM_SELF), true);
    assert(self_index != 0);
    assert(CTX_KEY_SYM(merged, self_index) == SYM_SELF);
    Move_Value(CTX_VAR(merged, self_index), CTX_ARCHETYPE(merged));

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
    bool all,
    bool expand
) {
    FAIL_IF_READ_ONLY_SER(SER(CTX_VARLIST(target))); // !!! should heed CONST

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
        REBVAL *key = CTX_KEY(target, i);
        for (; NOT_END(key); key++)
            Add_Binder_Index(&binder, VAL_KEY_CANON(key), -1);
        n = CTX_LEN(target);
    }
    else if (IS_BLOCK(only_words)) {
        // Limit exports to only these words:
        const RELVAL *word = VAL_ARRAY_AT(only_words);
        for (; NOT_END(word); word++) {
            if (IS_WORD(word) or IS_SET_WORD(word)) {
                Add_Binder_Index(&binder, VAL_WORD_CANON(word), -1);
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
        REBVAL *key = CTX_KEYS_HEAD(target);
        for (; NOT_END(key); key++)
            if (Get_Binder_Index_Else_0(&binder, VAL_KEY_CANON(key)) != 0)
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
    REBVAL *key = CTX_KEYS_HEAD(source);
    REBINT n = 1;
    for (; NOT_END(key); n++, key++) {
        const REBSTR *canon = VAL_KEY_CANON(key);
        if (IS_NULLED(only_words))
            Add_Binder_Index(&binder, canon, n);
        else {
            if (Get_Binder_Index_Else_0(&binder, canon) != 0) {
                Remove_Binder_Index(&binder, canon);
                Add_Binder_Index(&binder, canon, n);
            }
        }
    }
  }

    // Foreach word in target, copy the correct value from source:
    //
  blockscope {
    REBVAL *var = i != 0 ? CTX_VAR(target, i) : CTX_VARS_HEAD(target);
    REBVAL *key = i != 0 ? CTX_KEY(target, i) : CTX_KEYS_HEAD(target);
    for (; NOT_END(key); key++, var++) {
        REBINT m = Remove_Binder_Index_Else_0(&binder, VAL_KEY_CANON(key));
        if (m != 0) {
            // "the remove succeeded, so it's marked as set now" (old comment)

            if (NOT_CELL_FLAG(var, PROTECTED) and (all or IS_VOID(var))) {
                if (m < 0)
                    Init_Void(var);  // no value in source context
                else
                    Move_Var(var, CTX_VAR(source, m));  // preserves flags
            }
        }
    }
  }

    // Add any new words and values:
    if (expand) {
        REBVAL *key = CTX_KEYS_HEAD(source);
        REBINT n = 1;
        for (; NOT_END(key); n++, key++) {
            const REBSTR *canon = VAL_KEY_CANON(key);
            if (Remove_Binder_Index_Else_0(&binder, canon) != 0) {
                //
                // Note: no protect check is needed here
                //
                REBVAL *var = Append_Context(target, 0, canon);
                Move_Var(var, CTX_VAR(source, n));  // preserves flags
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
            REBVAL *key = CTX_KEY(target, i);
            for (; NOT_END(key); key++)
                Remove_Binder_Index_Else_0(&binder, VAL_KEY_CANON(key));
        }
        else if (IS_BLOCK(only_words)) {
            const RELVAL *word = VAL_ARRAY_AT(only_words);
            for (; NOT_END(word); word++) {
                if (IS_WORD(word) or IS_SET_WORD(word))
                    Remove_Binder_Index_Else_0(&binder, VAL_WORD_CANON(word));
            }
        }
        else {
            REBVAL *key = CTX_KEYS_HEAD(source);
            for (; NOT_END(key); key++)
                Remove_Binder_Index_Else_0(&binder, VAL_KEY_CANON(key));
        }
    }

    SHUTDOWN_BINDER(&binder);
}


//
//  Find_Canon_In_Context: C
//
// Search a context looking for the given canon symbol.  Return the index or
// 0 if not found.
//
REBLEN Find_Canon_In_Context(
    REBCTX *context,
    const REBSTR *canon,
    bool always
){
    assert(GET_SERIES_INFO(canon, STRING_CANON));

    REBVAL *key = CTX_KEYS_HEAD(context);
    REBLEN len = CTX_LEN(context);

    REBLEN n;
    for (n = 1; n <= len; n++, key++) {
        if (canon == VAL_KEY_CANON(key)) {
            if (Is_Param_Unbindable(key)) {
                if (not always)
                    return 0;
            }
            return n;
        }
    }

    // !!! Should this be changed to NOT_FOUND?
    return 0;
}


//
//  Select_Canon_In_Context: C
//
// Search a context's keylist looking for the given canon symbol, and return
// the value for the word.  Return NULL if the canon is not found.
//
REBVAL *Select_Canon_In_Context(REBCTX *context, const REBSTR *canon)
{
    const bool always = false;
    REBLEN n = Find_Canon_In_Context(context, canon, always);
    if (n == 0)
        return NULL;

    return CTX_VAR(context, n);
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
    // Temporary block used while scanning for words.
    //
    // Note that the logic inside Collect_Keylist managed assumes it's at
    // least 2 long to hold the rootkey (SYM_0) and a possible SYM_SELF
    // hidden actual key.
    //
    TG_Buf_Collect = Make_Array_Core(2 + 98, 0);
}


//
//  Shutdown_Collector: C
//
void Shutdown_Collector(void)
{
    Free_Unmanaged_Array(TG_Buf_Collect);
    TG_Buf_Collect = NULL;
}


#ifndef NDEBUG

//
//  Assert_Context_Core: C
//
void Assert_Context_Core(REBCTX *c)
{
    REBARR *varlist = CTX_VARLIST(c);

    if (
        (SER(varlist)->header.bits & SERIES_MASK_VARLIST)
        != SERIES_MASK_VARLIST
    ){
        panic (varlist);
    }

    REBARR *keylist = CTX_KEYLIST(c);
    if (keylist == NULL)
        panic (c);

    REBVAL *rootvar = CTX_ROOTVAR(c);
    if (not ANY_CONTEXT(rootvar))
        panic (rootvar);

    REBLEN keys_len = ARR_LEN(keylist);
    REBLEN vars_len = ARR_LEN(varlist);

    if (keys_len < 1)
        panic (keylist);

    if (keys_len != vars_len)
        panic (c);

    if (VAL_CONTEXT(rootvar) != c)
        panic (rootvar);

    if (GET_SERIES_INFO(c, INACCESSIBLE)) {
        //
        // !!! For the moment, don't check inaccessible stack frames any
        // further.  This includes varless reified frames and those reified
        // frames that are no longer on the stack.
        //
        return;
    }

    REBVAL *rootkey = CTX_ROOTKEY(c);
    if (IS_VOID_RAW(rootkey)) {
        //
        // Note that in the future the rootkey for ordinary OBJECT! or ERROR!
        // PORT! etc. may be more interesting than BLANK.  But it uses that
        // for now--unreadable.
        //
        if (IS_FRAME(rootvar))
            panic (c);
    }
    else if (IS_ACTION(rootkey)) {
        //
        // At the moment, only FRAME! is able to reuse an ACTION!'s keylist.
        // There may be reason to relax this, if you wanted to make an
        // ordinary object that was a copy of a FRAME! but not a FRAME!.
        //
        if (not IS_FRAME(rootvar))
            panic (rootvar);

        // In a FRAME!, the keylist is for the underlying function.  So to
        // know what function the frame is actually for, one must look to
        // the "phase" field...held in the rootvar.
        //
        if (ACT_UNDERLYING(VAL_PHASE(rootvar)) != VAL_ACTION(rootkey))
            panic (rootvar);

        REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
        if (f) {
            //
            // If the frame is on the stack, the phase should be something
            // with the same underlying function as the rootkey.
            //
            if (ACT_UNDERLYING(VAL_PHASE(rootvar)) != VAL_ACTION(rootkey))
                panic (rootvar);
        }
    }
    else
        panic (rootkey);

    REBVAL *key = CTX_KEYS_HEAD(c);
    REBVAL *var = CTX_VARS_HEAD(c);

    REBLEN n;
    for (n = 1; n < keys_len; n++, var++, key++) {
        if (IS_END(key)) {
            printf("** Early key end at index: %d\n", cast(int, n));
            panic (c);
        }

        if (not IS_PARAM(key))
            panic (key);

        if (IS_END(var)) {
            printf("** Early var end at index: %d\n", cast(int, n));
            panic (c);
        }
    }

    if (NOT_END(key)) {
        printf("** Missing key end at index: %d\n", cast(int, n));
        panic (key);
    }

    if (NOT_END(var)) {
        printf("** Missing var end at index: %d\n", cast(int, n));
        panic (var);
    }
}

#endif
