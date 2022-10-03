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
Context(*) Alloc_Context_Core(enum Reb_Kind kind, REBLEN capacity, Flags flags)
{
    Keylist(*) keylist = Make_Series(Keylist,
        capacity,  // no terminator
        SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED  // always shareable
    );
    mutable_LINK(Ancestor, keylist) = keylist;  // default to keylist itself
    assert(SER_USED(keylist) == 0);

    Array(*) varlist = Make_Array_Core(
        capacity + 1,  // size + room for rootvar (array terminator implicit)
        SERIES_MASK_VARLIST  // includes assurance of dynamic allocation
            | flags  // e.g. NODE_FLAG_MANAGED
    );
    mutable_MISC(VarlistMeta, varlist) = nullptr;
    mutable_LINK(Patches, varlist) = nullptr;
    INIT_CTX_KEYLIST_UNIQUE(CTX(varlist), keylist);  // starts out unique

    Cell(*) rootvar = Alloc_Tail_Array(varlist);
    INIT_VAL_CONTEXT_ROOTVAR(rootvar, kind, varlist);

    return CTX(varlist);  // varlist pointer is context handle
}


//
//  Expand_Context_Keylist_Core: C
//
// Returns whether or not the expansion invalidated existing keys.
//
bool Expand_Context_Keylist_Core(Context(*) context, REBLEN delta)
{
    Keylist(*) keylist = CTX_KEYLIST(context);
    assert(IS_KEYLIST(keylist));

    if (Get_Subclass_Flag(KEYLIST, keylist, SHARED)) {
        //
        // INIT_CTX_KEYLIST_SHARED was used to set the flag that indicates
        // this keylist is shared with one or more other contexts.  Can't
        // expand the shared copy without impacting the others, so break away
        // from the sharing group by making a new copy.
        //
        // (If all shared copies break away in this fashion, then the last
        // copy of the dangling keylist will be GC'd.)

        Keylist(*) copy = cast(Raw_Keylist*, Copy_Series_At_Len_Extra(
            keylist,
            0,
            SER_USED(keylist),
            delta,
            SERIES_MASK_KEYLIST
        ));

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

    Extend_Series_If_Necessary(keylist, delta);

    return false;
}


//
//  Expand_Context: C
//
// Expand a context. Copy words if keylist is not unique.
//
void Expand_Context(Context(*) context, REBLEN delta)
{
    // varlist is unique to each object--expand without making a copy.
    //
    Extend_Series_If_Necessary(CTX_VARLIST(context), delta);

    Expand_Context_Keylist_Core(context, delta);
}


// Append a word to the context word list. Expands the list if necessary.
// Returns the value cell for the word, which is reset.
//
// If word is not nullptr, use the word sym and bind the word value, otherwise
// use sym.  When using a word, it will be modified to be specifically bound
// to this context after the operation.
//
static REBVAR* Append_Context_Core(
    Context(*) context,
    Symbol(const*) symbol,
    option(Cell(*)) any_word  // binding modified (Note: quoted words allowed)
) {
    if (CTX_TYPE(context) == REB_MODULE) {
        //
        // !!! In order to make MODULE more friendly to the idea of very
        // large number of words, variable instances for a module are stored
        // not in an indexed block form...but distributed as individual series
        // node allocations.  The variables are linked reachable from the
        // symbol node for the word's spelling, and can be directly linked
        // to from a word as a singular value (with binding index "1").

        option(SymId) id;
        if (context == Lib_Context)
            id = ID_OF_SYMBOL(symbol);
        else
            id = SYM_0;

        Array(*) patch;
        if (id and id < LIB_SYMS_MAX) {
            //
            // Low symbol IDs are all in PG_Lib_Patches for fast access, and
            // were created as a continguous array of memory in Startup_Lib().
            //
            patch = &PG_Lib_Patches[id];
            assert(INODE(PatchContext, patch) == nullptr);  // don't double add
            // patch->leader.bits should be already set
        }
        else patch = Alloc_Singular(
            NODE_FLAG_MANAGED
            | FLAG_FLAVOR(PATCH)
            //
            // Note: The GC behavior of these patches is special and does not
            // fit into the usual patterns.  There is a pass in the GC
            // that propagates context survival into the patches from the
            // global bind table.  Although we say INFO_NODE_NEEDS_MARK to
            // keep a context alive, that marking isn't done in that pass...
            // otherwise the variables could never GC.  Instead, it only
            // happens if the patch is cached in a variable...then that
            // reference touches the patch which touches the context.  But
            // if not cached, the context keeps vars alive; not vice-versa
            // (e.g. the mere existence of a variable--not cached in a cell
            // reference--should not keep it alive).  MISC_NODE_NEEDS_MARK
            // is not done as that would keep alive patches from other
            // contexts in the hitch chain.
            //
            // !!! Should there be a "decay and forward" general mechanic,
            // so a node can tell the GC to touch up all references and point
            // to something else...e.g. to forward references to a cache back
            // to the context in order to "delete" variables?
            //
            | SERIES_FLAG_INFO_NODE_NEEDS_MARK  // mark context through cache
        );

        // We circularly link the variable into the list of hitches so that you
        // can find the spelling again.

        // skip over binding-related hitches
        //
        REBSER *updating = m_cast(Raw_Symbol*, symbol);
        while (GET_SERIES_FLAG(SER(node_MISC(Hitch, updating)), BLACK))
            updating = SER(node_MISC(Hitch, updating));

        node_MISC(Hitch, patch) = node_MISC(Hitch, updating);
        mutable_INODE(PatchContext, patch) = context;
        node_MISC(Hitch, updating) = patch;

        if (any_word) {  // bind word while we're at it
            INIT_VAL_WORD_BINDING(unwrap(any_word), patch);
            INIT_VAL_WORD_INDEX(unwrap(any_word), INDEX_ATTACHED);
        }

        return cast(REBVAR*, ARR_SINGLE(patch));
    }

    Keylist(*) keylist = CTX_KEYLIST(context);

    // Add the key to key list
    //
    // !!! This doesn't seem to consider the shared flag of the keylist (?)
    // though the callsites seem to pre-expand with consideration for that.
    // Review why this is expanding when the callers are expanding.  Should
    // also check that redundant keys aren't getting added here.
    //
    EXPAND_SERIES_TAIL(keylist, 1);  // updates the used count
    Init_Key(SER_LAST(REBKEY, keylist), symbol);

    // Add a slot to the var list
    //
    EXPAND_SERIES_TAIL(CTX_VARLIST(context), 1);

    Cell(*) value = Erase_Cell(ARR_LAST(CTX_VARLIST(context)));

    if (any_word) {
        REBLEN len = CTX_LEN(context);  // length we just bumped
        INIT_VAL_WORD_BINDING(unwrap(any_word), context);
        INIT_VAL_WORD_INDEX(unwrap(any_word), len);
    }

    return cast(REBVAR*, value);  // location we just added (void cell)
}


//
//  Append_Context_Bind_Word: C
//
REBVAR* Append_Context_Bind_Word(
    Context(*) context,
    Cell(*) any_word  // binding modified (Note: quoted words allowed)
){
    return Append_Context_Core(context, VAL_WORD_SYMBOL(any_word), any_word);
}

//
//  Apend_Context: C
//
REBVAR* Append_Context(Context(*) context, Symbol(const*) symbol)
{
    return Append_Context_Core(context, symbol, nullptr);
}


//
//  Collect_Start: C
//
// Begin using a "binder" to start mapping canon symbol names to integer
// indices.  The symbols are collected on the stack.  Use Collect_End() to
// free the map.
//
void Collect_Start(struct Reb_Collector* collector, Flags flags)
{
    collector->flags = flags;
    collector->stack_base = TOP_INDEX;
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
    StackIndex index = TOP_INDEX;
    for (; index != cl->stack_base; --index) {
        Symbol(const*) symbol = VAL_WORD_SYMBOL(TOP);
        Remove_Binder_Index(&cl->binder, symbol);
        DROP();
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
    option(Symbol(const*)*) duplicate,
    struct Reb_Collector *cl,
    Context(*) context
){
    const REBKEY *tail;
    const REBKEY *key = CTX_KEYS(&tail, context);

    if (duplicate)
        *unwrap(duplicate) = nullptr;

    for (; key != tail; ++key) {
        Symbol(const*) symbol = KEY_SYMBOL(key);
        if (not Try_Add_Binder_Index(
            &cl->binder,
            symbol,
            Collector_Index_If_Pushed(cl)
        )){
            if (duplicate and not *unwrap(duplicate))  // returns first dup
                *unwrap(duplicate) = symbol;

            continue;  // don't collect if already in bind table
        }
        Init_Word(PUSH(), symbol);
    }
}


//
//  Collect_Inner_Loop: C
//
// The inner recursive loop used for collecting context keys or ANY-WORD!s.
//
static void Collect_Inner_Loop(
    struct Reb_Collector *cl,
    Cell(const*) head,
    Cell(const*) tail
){
    Cell(const*) v = head;
    for (; v != tail; ++v) {
        enum Reb_Kind kind = CELL_HEART(v);

        if (ANY_WORD_KIND(kind)) {
            if (kind != REB_SET_WORD and not (cl->flags & COLLECT_ANY_WORD))
                continue;  // kind of word we're not interested in collecting

            Symbol(const*) symbol = VAL_WORD_SYMBOL(v);

            if (not Try_Add_Binder_Index(
                &cl->binder,
                symbol,
                Collector_Index_If_Pushed(cl)
            )){
                if (cl->flags & COLLECT_NO_DUP) {
                    Collect_End(cl);  // IMPORTANT: Can't fail with binder

                    DECLARE_LOCAL (duplicate);
                    Init_Word(duplicate, symbol);
                    fail (Error_Dup_Vars_Raw(duplicate));  // cleans bindings
                }
                continue;  // tolerate duplicate
            }

            Init_Word(PUSH(), symbol);
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
            Cell(const*) sub_tail;
            Cell(const*) sub_at = VAL_ARRAY_AT(&sub_tail, v);
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
Keylist(*) Collect_Keylist_Managed(
    option(Cell(const*)) head,
    option(Cell(const*)) tail,
    option(Context(*)) prior,
    Flags flags  // see %sys-core.h for COLLECT_ANY_WORD, etc.
){
    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    Collect_Start(cl, flags);

    if (prior) {
        Symbol(const*) duplicate;
        Collect_Context_Keys(&duplicate, cl, unwrap(prior));
        assert(not duplicate);  // context should have had all unique keys
    }

    if (head)
        Collect_Inner_Loop(cl, unwrap(head), unwrap(tail));
    else
        assert(not tail);

    Count num_collected = TOP_INDEX - cl->stack_base;

    // If new keys were added to the collect buffer (as evidenced by a longer
    // collect buffer than the original keylist) then make a new keylist
    // array, otherwise reuse the original
    //
    Keylist(*) keylist;
    if (prior and CTX_LEN(unwrap(prior)) == num_collected)
        keylist = CTX_KEYLIST(unwrap(prior));
    else {
        keylist = Make_Series(Keylist,
            num_collected,  // no terminator
            SERIES_MASK_KEYLIST | NODE_FLAG_MANAGED
        );

        StackValue(*) word = Data_Stack_At(cl->stack_base) + 1;
        REBKEY* key = SER_HEAD(REBKEY, keylist);
        for (; word != TOP + 1; ++word, ++key)
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
Array(*) Collect_Unique_Words_Managed(
    Cell(const*) head,
    Cell(const*) tail,
    Flags flags,  // See COLLECT_XXX
    const REBVAL *ignorables  // BLOCK!, ANY-CONTEXT!, or BLANK! for none
){
    // We do not want to fail() during the bind at this point in time (the
    // system doesn't know how to clean up, and the only cleanup it does
    // assumes you were collecting for a keylist...it doesn't have access to
    // the "ignore" bindings.)  Do a pre-pass to fail first, if there are
    // any non-words in a block the user passed in.
    //
    if (not Is_Nulled(ignorables)) {
        Cell(const*) check_tail;
        Cell(const*) check = VAL_ARRAY_AT(&check_tail, ignorables);
        for (; check != check_tail; ++check) {
            if (not ANY_WORD_KIND(CELL_HEART(check)))
                fail (Error_Bad_Value(check));
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
        Cell(const*) ignore_tail;
        Cell(const*) ignore = VAL_ARRAY_AT(&ignore_tail, ignorables);
        for (; ignore != ignore_tail; ++ignore) {
            noquote(Cell(const*)) cell = VAL_UNESCAPED(ignore);
            Symbol(const*) symbol = VAL_WORD_SYMBOL(cell);

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
        assert(Is_Nulled(ignorables));

    Collect_Inner_Loop(cl, head, tail);

    // We don't use Pop_Stack_Values_Core() because we want to keep the values
    // on the stack so that Collect_End() can remove them from the binder.
    //
    Array(*) array = Copy_Values_Len_Shallow_Core(
        Data_Stack_At(cl->stack_base + 1),
        SPECIFIED,
        TOP_INDEX - cl->stack_base,
        NODE_FLAG_MANAGED
    );

    if (IS_BLOCK(ignorables)) {
        Cell(const*) ignore_tail;
        Cell(const*) ignore = VAL_ARRAY_AT(&ignore_tail, ignorables);
        for (; ignore != ignore_tail; ++ignore) {
            noquote(Cell(const*)) cell = VAL_UNESCAPED(ignore);
            Symbol(const*) symbol = VAL_WORD_SYMBOL(cell);

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
        assert(Is_Nulled(ignorables));

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
    Context(*) source,
    Context(*) dest,
    option(struct Reb_Binder*) binder
){
    Cell(const*) tail = ARR_TAIL(CTX_VARLIST(dest));
    Cell(*) head = ARR_HEAD(CTX_VARLIST(dest));
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
Context(*) Make_Context_Detect_Managed(
    enum Reb_Kind kind,
    option(Cell(const*)) head,
    option(Cell(const*)) tail,
    option(Context(*)) parent
) {
    assert(kind != REB_MODULE);

    Keylist(*) keylist = Collect_Keylist_Managed(
        head,
        tail,
        parent,
        COLLECT_ONLY_SET_WORDS
    );

    REBLEN len = SER_USED(keylist);
    Array(*) varlist = Make_Array_Core(
        1 + len,  // needs room for rootvar
        SERIES_MASK_VARLIST
            | NODE_FLAG_MANAGED // Note: Rebind below requires managed context
    );
    SET_SERIES_LEN(varlist, 1 + len);
    mutable_MISC(VarlistMeta, varlist) = nullptr;
    mutable_LINK(Patches, varlist) = nullptr;  // start w/no virtual binds

    Context(*) context = CTX(varlist);

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

    Value(*) var = VAL(ARR_HEAD(varlist));
    INIT_VAL_CONTEXT_ROOTVAR(var, kind, varlist);

    ++var;

    for (; len > 0; --len, ++var)  // [0] is rootvar (context), already done
        Init_Nihil(var);

    if (parent) {
        //
        // Copy parent values, and for values we copied that were blocks and
        // strings, replace their series components with deep copies.
        //
        REBVAL *dest = CTX_VARS_HEAD(context);
        const REBVAR *src_tail;
        REBVAL *src = CTX_VARS(&src_tail, unwrap(parent));
        for (; src != src_tail; ++dest, ++src) {
            Flags flags = NODE_FLAG_MANAGED;  // !!! Review, what flags?
            assert(Is_Nihil(dest));
            Copy_Cell(dest, src);
            Clonify(dest, flags, TS_CLONE);
        }
    }

    if (parent)  // v-- passing in nullptr to indicate no more binds
        Rebind_Context_Deep(unwrap(parent), context, nullptr);

    ASSERT_CONTEXT(context);

  #if DEBUG_COLLECT_STATS
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
Context(*) Construct_Context_Managed(
    enum Reb_Kind kind,
    Cell(*) head,  // !!! Warning: modified binding
    Cell(const*) tail,
    REBSPC *specifier,
    option(Context(*)) parent
){
    Context(*) context = Make_Context_Detect_Managed(
        kind, // type
        head, // values to scan for toplevel set-words
        tail,
        parent // parent
    );

    if (not head)
        return context;

    Bind_Values_Shallow(head, tail, CTX_ARCHETYPE(context));

    Cell(const*) value = head;
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
Array(*) Context_To_Array(Cell(const*) context, REBINT mode)
{
    assert(!(mode & 4));

    StackIndex base = TOP_INDEX;

    EVARS e;
    Init_Evars(&e, context);

    while (Did_Advance_Evars(&e)) {
        if (mode & 1) {
            assert(e.index != 0);
            Init_Any_Word_Bound(
                PUSH(),
                (mode & 2) ? REB_SET_WORD : REB_WORD,
                KEY_SYMBOL(e.key),
                e.ctx,
                e.index
            );

            if (mode & 2)
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
        }

        if (mode & 2) {
            //
            // Context might have voids, which denote the value have not
            // been set.  These contexts cannot be converted to blocks,
            // since user arrays may not contain void.
            //
            if (Is_Nulled(e.var))
                fail (Error_Null_Object_Block_Raw());

            Copy_Cell(PUSH(), e.var);
        }
    }

    Shutdown_Evars(&e);

    return Pop_Stack_Values_Core(
        base,
        did (mode & 2) ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0
    );
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
    Cell(const*) context,
    Symbol(const*) symbol,
    bool strict
){
    if (IS_MODULE(context)) {
        //
        // Modules hang their variables off the symbol itself, in a linked
        // list with other modules who also have variables of that name.
        //
        Context(*) c = VAL_CONTEXT(context);
        return MOD_VAR(c, symbol, strict) ? INDEX_ATTACHED : 0;
    }

    EVARS e;
    Init_Evars(&e, context);

    while (Did_Advance_Evars(&e)) {
        if (strict) {
            if (symbol != KEY_SYMBOL(e.key))
                continue;
        }
        else {
            if (not Are_Synonyms(symbol, KEY_SYMBOL(e.key)))
                continue;
        }

        Shutdown_Evars(&e);
        return e.index;
    }

    Shutdown_Evars(&e);
    return 0;
}


//
//  Select_Symbol_In_Context: C
//
// Search a context's keylist looking for the given symbol, and return the
// value for the word.  Return NULL if the symbol is not found.
//
option(Value(*)) Select_Symbol_In_Context(
    Cell(const*) context,
    Symbol(const*) symbol
){
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
    Context(*) context = VAL_CONTEXT(value);

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
void Assert_Context_Core(Context(*) c)
{
    Array(*) varlist = CTX_VARLIST(c);

    if (
        (varlist->leader.bits & SERIES_MASK_VARLIST) != SERIES_MASK_VARLIST
    ){
        panic (varlist);
    }

    REBVAL *rootvar = CTX_ROOTVAR(c);
    if (not ANY_CONTEXT(rootvar) or VAL_CONTEXT(rootvar) != c)
        panic (rootvar);

    Keylist(*) keylist = CTX_KEYLIST(c);

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

      #if DEBUG_POISON_SERIES_TAILS
        if (Is_Cell_Poisoned(var)) {
            printf("** Early var end at index: %d\n", cast(int, n));
            panic (c);
        }
      #endif
    }

  #if DEBUG_POISON_SERIES_TAILS
    if (not Is_Cell_Poisoned(var)) {
        printf("** Missing var end at index: %d\n", cast(int, n));
        panic (var);
    }
  #endif
}

#endif
