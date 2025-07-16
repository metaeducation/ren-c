//
//  file: %c-context.c
//  summary: "Management routines for ANY-CONTEXT! key/value storage"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Contexts are two arrays of equal length, which are linked together to
// describe "object-like" things (lists of TYPESET! keys and corresponding
// variable values).  They are used by OBJECT!, PORT!, FRAME!, etc.
//
// The VarList* is how contexts are passed around as a single pointer.  This
// pointer is actually just an Array Flex which represents the variable
// values.  The keylist can be reached through the ->link field of that
// stub, and the [0] value of the variable array is an "archetype instance"
// of whatever kind of cell the context represents.
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
// This context will not have its ANY-OBJECT! cell in the [0] position fully
// configured, hence this is an "Alloc" instead of a "Make" (because there
// is still work to be done before it will pass ASSERT_CONTEXT).
//
VarList* Alloc_Context_Core(enum Reb_Kind kind, REBLEN capacity, Flags flags)
{
    assert(not (flags & ARRAY_FLAG_HAS_FILE_LINE)); // LINK and MISC are taken

    Array* varlist = Make_Array_Core(
        capacity + 1, // size + room for ROOTVAR
        SERIES_MASK_CONTEXT // includes assurance of dynamic allocation
            | flags // e.g. NODE_FLAG_MANAGED
    );
    MISC(varlist).meta = nullptr; // GC sees meta object, must init

    // varlist[0] is a value instance of the OBJECT!/MODULE!/PORT!/ERROR! we
    // are building which contains this context.

    Value* rootvar = RESET_CELL(Alloc_Tail_Array(varlist), kind);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = nullptr;
    INIT_BINDING(rootvar, UNBOUND);

    // keylist[0] is the "rootkey" which we currently initialize to an
    // unreadable BLANK!.  It is reserved for future use.

    Array* keylist = Make_Array_Core(
        capacity + 1, // size + room for ROOTKEY
        NODE_FLAG_MANAGED // No keylist flag, but we don't want line numbers
    );
    Init_Unreadable(Alloc_Tail_Array(keylist));

    // Default the ancestor link to be to this keylist itself.
    //
    LINK(keylist).ancestor = keylist;

    // varlists link keylists via LINK().keysource, sharable hence managed

    Tweak_Keylist_Of_Varlist_Unique(CTX(varlist), keylist);

    return CTX(varlist); // varlist pointer is context handle
}


//
//  Expand_Context_Keylist_Core: C
//
// Returns whether or not the expansion invalidated existing keys.
//
bool Expand_Context_Keylist_Core(VarList* context, REBLEN delta)
{
    Array* keylist = Keylist_Of_Varlist(context);

    // can't expand or unshare a FRAME!'s list
    //
    assert(Not_Array_Flag(keylist, IS_PARAMLIST));

    if (Get_Flex_Info(keylist, SHARED_KEYLIST)) {
        //
        // Tweak_Keylist_Of_Varlist_Shared was used to set the flag that indicates
        // this keylist is shared with one or more other contexts.  Can't
        // expand the shared copy without impacting the others, so break away
        // from the sharing group by making a new copy.
        //
        // (If all shared copies break away in this fashion, then the last
        // copy of the dangling keylist will be GC'd.)
        //
        // Keylists are only typesets, so no need for a specifier.

        Array* copy = Copy_Array_Extra_Shallow(keylist, SPECIFIED, delta);

        // Preserve link to ancestor keylist.  Note that if it pointed to
        // itself, we update this keylist to point to itself.
        //
        // !!! Any extant derivations to the old keylist will still point to
        // that keylist at the time the derivation was performed...it will not
        // consider this new keylist to be an ancestor match.  Hence expanded
        // objects are essentially all new objects as far as derivation are
        // concerned, though they can still run against ancestor methods.
        //
        if (LINK(keylist).ancestor == keylist)
            LINK(copy).ancestor = copy;
        else
            LINK(copy).ancestor = LINK(keylist).ancestor;

        Manage_Flex(copy);
        Tweak_Keylist_Of_Varlist_Unique(context, copy);

        return true;
    }

    if (delta == 0)
        return false;

    // Tweak_Keylist_Of_Varlist_Unique was used to set this keylist in the
    // context, and no Tweak_Keylist_Of_Varlist_Shared was used by another context
    // to mark the flag indicating it's shared.  Extend it directly.

    Extend_Flex(keylist, delta);
    Term_Array_Len(keylist, Array_Len(keylist));

    return false;
}


//
//  Expand_Context: C
//
// Expand a context. Copy words if keylist is not unique.
//
void Expand_Context(VarList* context, REBLEN delta)
{
    // varlist is unique to each object--expand without making a copy.
    //
    Extend_Flex(Varlist_Array(context), delta);
    Term_Array_Len(Varlist_Array(context), Array_Len(Varlist_Array(context)));

    Expand_Context_Keylist_Core(context, delta);
}


//
//  Append_Context: C
//
// Append a word to the context word list. Expands the list if necessary.
// Returns the value cell for the word.  The new variable is unset by default.
//
// If word is not nullptr, use the word sym and bind the word value, otherwise
// use sym.  When using a word, it will be modified to be specifically bound
// to this context after the operation.
//
// !!! Should there be a clearer hint in the interface, with a Value* out,
// to give a fully bound value as a result?  Given that the caller passed
// in the context and can get the index out of a relatively bound word,
// they usually likely don't need the result directly.
//
Value* Append_Context(
    VarList* context,
    Option(Cell*) any_word,
    Option(Symbol*) symbol
) {
    Array* keylist = Keylist_Of_Varlist(context);
    if (any_word) {
        assert(not symbol);
        symbol = Word_Symbol(unwrap any_word);
    }
    else
        assert(symbol);

    // Add the key to key list
    //
    // !!! This doesn't seem to consider the shared flag of the keylist (?)
    // though the callsites seem to pre-expand with consideration for that.
    // Review why this is expanding when the callers are expanding.  Should
    // also check that redundant keys aren't getting added here.
    //
    Expand_Flex_Tail(keylist, 1);
    Value* key = Init_Typeset(
        Array_Last(keylist), // !!! non-dynamic, could optimize
        TS_VALUE, // !!! Currently not paid attention to
        unwrap symbol
    );
    UNUSED(key);
    Term_Array_Len(keylist, Array_Len(keylist));

    // Add a slot to the var list
    //
    Expand_Flex_Tail(Varlist_Array(context), 1);
    REBLEN len = Array_Len(Varlist_Array(context)); // length we just bumped
    REBLEN index = len - 1;

    Value* value = Init_Trash(Array_Last(Varlist_Array(context)));
    Term_Array_Len(Varlist_Array(context), len);

    if (any_word) {
        //
        // We want to not just add a key/value pairing to the context, but we
        // want to bind a word while we are at it.  Make sure symbol is valid.
        //
        INIT_BINDING(unwrap any_word, context);
        INIT_WORD_INDEX(unwrap any_word, index);
    }

    // Make sure fast cache of index in lib in canon symbol is up to date
    //
    if (context == Lib_Context)
        MISC(Canon_Symbol(unwrap symbol)).bind_index.lib = index;

    return value;  // location we just added (void cell)
}


//
//  Copy_Context_Shallow_Extra_Managed: C
//
// Makes a copy of a context.  If no extra storage space is requested, then
// the same keylist will be used.
//
VarList* Copy_Context_Shallow_Extra_Managed(VarList* src, REBLEN extra) {
    assert(Get_Array_Flag(src, IS_VARLIST));
    Assert_Flex_Managed(Keylist_Of_Varlist(src));

    // Note that keylists contain only typesets (hence no relative values),
    // and no varlist is part of a function body.  All the values here should
    // be fully specified.
    //
    VarList* dest;
    Array* varlist;
    if (extra == 0) {
        varlist = Copy_Array_Shallow_Flags(
            Varlist_Array(src),
            SPECIFIED,
            SERIES_MASK_CONTEXT // includes assurance of non-dynamic
                | NODE_FLAG_MANAGED
        );

        dest = CTX(varlist);

        // Leave ancestor link as-is in shared keylist.
        //
        Tweak_Keylist_Of_Varlist_Shared(dest, Keylist_Of_Varlist(src));
    }
    else {
        Array* keylist = Copy_Array_At_Extra_Shallow(
            Keylist_Of_Varlist(src),
            0,
            SPECIFIED,
            extra,
            NODE_FLAG_MANAGED
        );
        varlist = Copy_Array_At_Extra_Shallow(
            Varlist_Array(src),
            0,
            SPECIFIED,
            extra,
            SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
        );

        dest = CTX(varlist);

        LINK(keylist).ancestor = Keylist_Of_Varlist(src);

        Tweak_Keylist_Of_Varlist_Unique(dest, keylist);
    }

    Varlist_Archetype(dest)->payload.any_context.varlist = Varlist_Array(dest);

    // !!! Should the new object keep the meta information, or should users
    // have to copy that manually?  If it's copied would it be a shallow or
    // a deep copy?
    //
    MISC(varlist).meta = nullptr;

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
void Collect_Start(struct Reb_Collector* collector, Flags flags)
{
    collector->flags = flags;
    collector->base = TOP_INDEX;
    collector->index = 1;
    INIT_BINDER(&collector->binder, nullptr);

    assert(Array_Len(BUF_COLLECT) == 0); // should be empty
}


//
//  Grab_Collected_Array_Managed: C
//
Array* Grab_Collected_Array_Managed(struct Reb_Collector *collector)
{
    UNUSED(collector); // not needed at the moment

    // We didn't terminate as we were collecting, so terminate now.
    //
    Term_Array_Len(BUF_COLLECT, Array_Len(BUF_COLLECT));

    // If no new words, prior context.  Note length must include the slot
    // for the rootkey...and note also this means the rootkey cell *may*
    // be shared between all keylists when you pass in a prior.
    //
    // All collected values should have been fully specified.
    //
    return Copy_Array_Shallow_Flags(
        BUF_COLLECT,
        SPECIFIED,
        NODE_FLAG_MANAGED
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
    Term_Array_Len(BUF_COLLECT, Array_Len(BUF_COLLECT));

    // Reset binding table (note BUF_COLLECT may have expanded)
    //
    Cell* v =
        (cl == nullptr or (cl->flags & COLLECT_AS_TYPESET))
            ? Array_Head(BUF_COLLECT) + 1
            : Array_Head(BUF_COLLECT);
    for (; NOT_END(v); ++v) {
        Symbol* canon =
            (cl == nullptr or (cl->flags & COLLECT_AS_TYPESET))
                ? Key_Canon(v)
                : VAL_WORD_CANON(v);

        if (cl != nullptr) {
            Remove_Binder_Index(&cl->binder, canon);
            continue;
        }

        // !!! This doesn't have a "binder" available to clear out the
        // keys with.  The nature of handling error states means that if
        // a thread-safe binding system was implemented, we'd have to know
        // which thread had the error to roll back any binding structures.
        // For now just zero it out based on the collect buffer.
        //
        assert(MISC(canon).bind_index.other != 0);
        MISC(canon).bind_index.other = 0;
    }

    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 0);

    if (cl != nullptr)
        SHUTDOWN_BINDER(&cl->binder);
}


//
//  Collect_Context_Keys: C
//
// Collect keys from a prior context into BUF_COLLECT for a new context.
//
void Collect_Context_Keys(
    struct Reb_Collector *cl,
    VarList* context,
    bool check_dups // check for duplicates (otherwise assume unique)
){
    assert(cl->flags & COLLECT_AS_TYPESET);

    Value* key = Varlist_Keys_Head(context);

    assert(cl->index >= 1); // 0 in bind table means "not present"

    // This is necessary so Blit_Cell() below isn't overwriting memory that
    // BUF_COLLECT does not own.  (It may make the buffer capacity bigger than
    // necessary if duplicates are found, but the actual buffer length will be
    // set correctly by the end.)
    //
    Expand_Flex_Tail(BUF_COLLECT, Varlist_Len(context));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, cl->index);

    Cell* collect = Array_Tail(BUF_COLLECT); // get address *after* expansion

    if (check_dups) {
        for (; NOT_END(key); key++) {
            Symbol* canon = Key_Canon(key);
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
            Array_Len(BUF_COLLECT) + (collect - Array_Tail(BUF_COLLECT))
        );
    }
    else {
        // Optimized add of all keys to bind table and collect buffer.
        //
        for (; NOT_END(key); ++key, ++collect, ++cl->index) {
            Blit_Cell(collect, key);
            Add_Binder_Index(&cl->binder, Key_Canon(key), cl->index);
        }
        SET_ARRAY_LEN_NOTERM(
            BUF_COLLECT, Array_Len(BUF_COLLECT) + Varlist_Len(context)
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
static void Collect_Inner_Loop(struct Reb_Collector *cl, const Cell* head)
{
    const Cell* v = head;
    for (; NOT_END(v); ++v) {
        enum Reb_Kind kind = Type_Of(v);
        if (Any_Word_Kind(kind)) {
            if (kind != TYPE_SET_WORD and not (cl->flags & COLLECT_ANY_WORD))
                continue; // kind of word we're not interested in collecting

            Symbol* canon = VAL_WORD_CANON(v);
            if (not Try_Add_Binder_Index(&cl->binder, canon, cl->index)) {
                if (cl->flags & COLLECT_NO_DUP) {
                    DECLARE_VALUE (duplicate);
                    Init_Word(duplicate, Word_Symbol(v));
                    panic (Error_Dup_Vars_Raw(duplicate)); // cleans bindings
                }
                continue; // tolerate duplicate
            }

            ++cl->index;

            Expand_Flex_Tail(BUF_COLLECT, 1);
            if (cl->flags & COLLECT_AS_TYPESET)
                Init_Typeset(
                    Array_Last(BUF_COLLECT),
                    TS_VALUE, // !!! Not used at the moment
                    Word_Symbol(v)
                );
            else
                Init_Word(Array_Last(BUF_COLLECT), Word_Symbol(v));

            continue;
        }

        if (not (cl->flags & COLLECT_DEEP))
            continue;

        // Recurse into BLOCK! and GROUP!
        //
        // !!! Why aren't ANY-PATH! considered?  They may have GROUP! in
        // them which could need to be collected.  This is historical R3-Alpha
        // behavior which is probably wrong.
        //
        if (kind == TYPE_BLOCK or kind == TYPE_GROUP)
            Collect_Inner_Loop(cl, Cell_List_At(v));
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
Array* Collect_Keylist_Managed(
    REBLEN *self_index_out, // which context index SELF is in (if COLLECT_SELF)
    const Cell* head,
    VarList* prior,
    Flags flags // see %sys-core.h for COLLECT_ANY_WORD, etc.
) {
    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    assert(not (flags & COLLECT_AS_TYPESET)); // not optional, we add it
    Collect_Start(cl, flags | COLLECT_AS_TYPESET);

    // Leave the [0] slot blank while collecting (ROOTKEY/ROOTPARAM), but
    // valid (but "unreadable") bits so that the copy will still work.
    //
    Init_Unreadable(Array_Head(BUF_COLLECT));
    SET_ARRAY_LEN_NOTERM(BUF_COLLECT, 1);

    if (flags & COLLECT_ENSURE_SELF) {
        if (
            not prior or (
                0 == (*self_index_out = Find_Canon_In_Context(
                    prior,
                    CANON(SELF),
                    true
                ))
            )
        ) {
            // No prior or no SELF in prior, so we'll add it as the first key
            //
            Cell* self_key = Init_Typeset(
                Array_At(BUF_COLLECT, 1),
                TS_VALUE, // !!! Currently not paid attention to
                CANON(SELF)
            );

            // !!! See notes on the flags about why SELF is set hidden but
            // not unbindable with TYPE_TS_UNBINDABLE.
            //
            Set_Typeset_Flag(self_key, TYPE_TS_HIDDEN);

            assert(cl->index == 1);
            Add_Binder_Index(&cl->binder, Key_Canon(self_key), cl->index);
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
        assert(self_index_out == nullptr);
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
    Array* keylist;
    if (prior != nullptr and Array_Len(Keylist_Of_Varlist(prior)) == Array_Len(BUF_COLLECT))
        keylist = Keylist_Of_Varlist(prior);
    else
        keylist = Grab_Collected_Array_Managed(cl);

    // !!! Usages of the rootkey for non-FRAME! contexts is open for future,
    // but it's set to an unreadable blank at the moment just to make sure it
    // doesn't get used on accident.
    //
    assert(Is_Cell_Unreadable(Array_Head(keylist)));

    Collect_End(cl);
    return keylist;
}


//
//  Collect_Unique_Words_Managed: C
//
// Collect unique words from a block, possibly deeply...maybe just SET-WORD!s.
//
Array* Collect_Unique_Words_Managed(
    const Cell* head,
    Flags flags, // See COLLECT_XXX
    const Value* ignore // BLOCK!, ANY-CONTEXT!, or void for none
){
    // We do not want to panic() during the bind at this point in time (the
    // system doesn't know how to clean up, and the only cleanup it does
    // assumes you were collecting for a keylist...it doesn't have access to
    // the "ignore" bindings.)  Do a pre-pass to panic first.

    Cell* check = Cell_List_At(ignore);
    for (; NOT_END(check); ++check) {
        if (not Any_Word(check))
            panic (Error_Invalid_Core(check, VAL_SPECIFIER(ignore)));
    }

    struct Reb_Collector collector;
    struct Reb_Collector *cl = &collector;

    assert(not (flags & COLLECT_AS_TYPESET)); // only used for making keylists
    Collect_Start(cl, flags);

    assert(Array_Len(BUF_COLLECT) == 0); // should be empty

    // The way words get "ignored" in the collecting process is to give them
    // dummy bindings so it appears they've "already been collected", but
    // not actually add them to the collection.  Then, duplicates don't cause
    // an error...so they will just be skipped when encountered.
    //
    if (Is_Block(ignore)) {
        Cell* item = Cell_List_At(ignore);
        for (; NOT_END(item); ++item) {
            assert(Any_Word(item)); // pre-pass checked this
            Symbol* canon = VAL_WORD_CANON(item);

            // A block may have duplicate words in it (this situation could
            // arise when `function [/test /test] []` calls COLLECT-WORDS
            // and tries to ignore both tests.  Have debug build count the
            // number (overkill, but helps test binders).
            //
            if (not Try_Add_Binder_Index(&cl->binder, canon, -1)) {
            #if RUNTIME_CHECKS
                REBINT i = Get_Binder_Index_Else_0(&cl->binder, canon);
                assert(i < 0);
                Remove_Binder_Index_Else_0(&cl->binder, canon);
                Add_Binder_Index(&cl->binder, canon, i - 1);
            #endif
            }
        }
    }
    else if (Any_Context(ignore)) {
        Value* key = Varlist_Keys_Head(Cell_Varlist(ignore));
        for (; NOT_END(key); ++key) {
            //
            // Shouldn't be possible to have an object with duplicate keys,
            // use plain Add_Binder_Index.
            //
            Add_Binder_Index(&cl->binder, Key_Canon(key), -1);
        }
    }
    else
        assert(Is_Nulled(ignore));

    Collect_Inner_Loop(cl, head);

    Array* array = Grab_Collected_Array_Managed(cl);

    if (Is_Block(ignore)) {
        Cell* item = Cell_List_At(ignore);
        for (; NOT_END(item); ++item) {
            assert(Any_Word(item));
            Symbol* canon = VAL_WORD_CANON(item);

        #if RUNTIME_CHECKS
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
    else if (Any_Context(ignore)) {
        Value* key = Varlist_Keys_Head(Cell_Varlist(ignore));
        for (; NOT_END(key); ++key) {
            Remove_Binder_Index(&cl->binder, Key_Canon(key));
        }
    }
    else
        assert(Is_Nulled(ignore));

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
    VarList* source,
    VarList* dest,
    struct Reb_Binder *opt_binder
) {
    Rebind_Values_Deep(source, dest, Varlist_Slots_Head(dest), opt_binder);
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
// This routine will *always* make a context with a SELF.
//
VarList* Make_Selfish_Context_Detect_Managed(
    enum Reb_Kind kind,
    const Cell* head,
    VarList* opt_parent
) {
    REBLEN self_index;
    Array* keylist = Collect_Keylist_Managed(
        &self_index,
        head,
        opt_parent,
        COLLECT_ONLY_SET_WORDS | COLLECT_ENSURE_SELF
    );

    REBLEN len = Array_Len(keylist);
    Array* varlist = Make_Array_Core(
        len,
        SERIES_MASK_CONTEXT
            | NODE_FLAG_MANAGED // Note: Rebind below requires managed context
    );
    Term_Array_Len(varlist, len);
    MISC(varlist).meta = nullptr;  // clear meta object (GC sees this)

    VarList* context = CTX(varlist);

    // This isn't necessarily the clearest way to determine if the keylist is
    // shared.  Note Collect_Keylist_Managed() isn't called from anywhere
    // else, so it could probably be inlined here and it would be more
    // obvious what's going on.
    //
    if (opt_parent == nullptr) {
        Tweak_Keylist_Of_Varlist_Unique(context, keylist);
        LINK(keylist).ancestor = keylist;
    }
    else {
        if (keylist == Keylist_Of_Varlist(opt_parent)) {
            Tweak_Keylist_Of_Varlist_Shared(context, keylist);

            // We leave the ancestor link as-is in the shared keylist--so
            // whatever the parent had...if we didn't have to make a new
            // keylist.  This means that an object may be derived, even if you
            // look at its keylist and its ancestor link points at itself.
        }
        else {
            Tweak_Keylist_Of_Varlist_Unique(context, keylist);
            LINK(keylist).ancestor = Keylist_Of_Varlist(opt_parent);
        }
    }

    // context[0] is an instance value of the OBJECT!/PORT!/ERROR!/MODULE!
    //
    Value* var = RESET_CELL(Array_Head(varlist), kind);
    var->payload.any_context.varlist = varlist;
    var->payload.any_context.phase = nullptr;
    INIT_BINDING(var, UNBOUND);

    ++var;

    for (; len > 1; --len, ++var) // [0] is rootvar (context), already done
        Init_Nulled(var);

    if (opt_parent != nullptr) {
        //
        // Copy parent values (will have bits fixed by Clonify).
        // None of these should be relative, because they came from object
        // vars (that were not part of the deep copy of a function body)
        //
        Value* dest = Varlist_Slots_Head(context);
        Value* src = Varlist_Slots_Head(opt_parent);
        for (; NOT_END(src); ++dest, ++src)
            Move_Var(dest, src);

        // For values we copied that were blocks and strings, replace
        // their series components with deep copies of themselves:
        //
        Clonify_Values_Len_Managed(
            Varlist_Slots_Head(context),
            SPECIFIED,
            Varlist_Len(context),
            TS_CLONE
        );
    }

    // We should have a SELF key in all cases here.  Set it to be a copy of
    // the object we just created.  (It is indeed a copy of the [0] element,
    // but it doesn't need to be protected because the user overwriting it
    // won't destroy the integrity of the context.)
    //
    assert(CTX_KEY_SYM(context, self_index) == SYM_SELF);
    Copy_Cell(Varlist_Slot(context, self_index), Varlist_Archetype(context));

    if (opt_parent)
        Rebind_Context_Deep(opt_parent, context, nullptr);  // no more binds

    ASSERT_CONTEXT(context);

#if RUNTIME_CHECKS
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
VarList* Construct_Context_Managed(
    enum Reb_Kind kind,
    Cell* head, // !!! Warning: modified binding
    Specifier* specifier,
    VarList* opt_parent
) {
    VarList* context = Make_Selfish_Context_Detect_Managed(
        kind, // type
        head, // values to scan for toplevel set-words
        opt_parent // parent
    );

    if (not head)
        return context;

    Bind_Values_Shallow(head, context);

    const Cell* value = head;
    for (; NOT_END(value); value += 2) {
        if (not Is_Set_Word(value))
            panic (Error_Invalid_Type(Type_Of(value)));

        if (IS_END(value + 1))
            panic ("Unexpected end in context spec block.");

        if (Is_Set_Word(value + 1))
            panic (Error_Invalid_Type(Type_Of(value + 1))); // TBD: support

        Value* var = Sink_Var_May_Panic(value, specifier);
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
Array* Context_To_Array(VarList* context, REBINT mode)
{
    StackIndex base = TOP_INDEX;

    Value* key = Varlist_Keys_Head(context);
    Value* var = Varlist_Slots_Head(context);

    assert(!(mode & 4));

    REBLEN n = 1;
    for (; NOT_END(key); n++, key++, var++) {
        if (not Is_Param_Hidden(key)) {
            if (mode & 1) {
                Init_Any_Word_Bound(
                    PUSH(),
                    (mode & 2) ? TYPE_SET_WORD : TYPE_WORD,
                    Key_Symbol(key),
                    context,
                    n
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
                if (Is_Nulled(var))
                    panic (Error_Null_Object_Block_Raw());

                Copy_Cell(PUSH(), var);
            }
        }
    }

    return Pop_Stack_Values_Core(
        base,
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
VarList* Merge_Contexts_Selfish_Managed(VarList* parent1, VarList* parent2)
{
    if (parent2 != nullptr) {
        assert(CTX_TYPE(parent1) == CTX_TYPE(parent2));
        panic ("Multiple inheritance of object support removed from Ren-C");
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
    Init_Unreadable(Array_Head(BUF_COLLECT));
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
    Term_Array_Len(BUF_COLLECT, Array_Len(BUF_COLLECT));

    // Allocate child (now that we know the correct size).  Obey invariant
    // that keylists are always managed.  The BUF_COLLECT contains only
    // typesets, so no need for a specifier in the copy.
    //
    // !!! Review: should child start fresh with no meta information, or get
    // the meta information held by parents?
    //
    Array* keylist = Copy_Array_Shallow_Flags(
        BUF_COLLECT,
        SPECIFIED,
        NODE_FLAG_MANAGED
    );
    Init_Unreadable(Array_Head(keylist)); // Currently no rootkey usage

    if (parent1 == nullptr)
        LINK(keylist).ancestor = keylist;
    else
        LINK(keylist).ancestor = Keylist_Of_Varlist(parent1);

    Array* varlist = Make_Array_Core(
        Array_Len(keylist),
        SERIES_MASK_CONTEXT
            | NODE_FLAG_MANAGED // rebind below requires managed context
    );
    MISC(varlist).meta = nullptr;  // GC sees this, it must be initialized

    VarList* merged = CTX(varlist);
    Tweak_Keylist_Of_Varlist_Unique(merged, keylist);

    // !!! Currently we assume the child will be of the same type as the
    // parent...so if the parent was an OBJECT! so will the child be, if
    // the parent was an ERROR! so will the child be.  This is a new idea,
    // so review consequences.
    //
    Value* rootvar = RESET_CELL(Array_Head(varlist), CTX_TYPE(parent1));
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = nullptr;
    INIT_BINDING(rootvar, UNBOUND);

    // Copy parent1 values.  (Can't use memcpy() because it would copy things
    // like protected bits...)
    //
    Value* copy_dest = Varlist_Slots_Head(merged);
    const Value* copy_src = Varlist_Slots_Head(parent1);
    for (; NOT_END(copy_src); ++copy_src, ++copy_dest)
        Move_Var(copy_dest, copy_src);

    // Update the child tail before making calls to Varlist_Slot(), because the
    // debug build does a length check.
    //
    Term_Array_Len(varlist, Array_Len(keylist));

    // Copy parent2 values:
    Value* key = Varlist_Keys_Head(parent2);
    Value* value = Varlist_Slots_Head(parent2);
    for (; NOT_END(key); key++, value++) {
        // no need to search when the binding table is available
        REBINT n = Get_Binder_Index_Else_0(
            &collector.binder, Key_Canon(key)
        );
        assert(n != 0);
        Move_Var(Varlist_Slot(merged, n), value);
    }

    // Deep copy the child.  Context vars are REBVALs, already fully specified
    //
    Clonify_Values_Len_Managed(
        Varlist_Slots_Head(merged),
        SPECIFIED,
        Varlist_Len(merged),
        TS_CLONE
    );

    // Rebind the child
    //
    Rebind_Context_Deep(parent1, merged, nullptr);
    Rebind_Context_Deep(parent2, merged, &collector.binder);

    // release the bind table
    //
    Collect_End(&collector);

    // We should have gotten a SELF in the results, one way or another.
    //
    REBLEN self_index = Find_Canon_In_Context(merged, CANON(SELF), true);
    assert(self_index != 0);
    assert(CTX_KEY_SYM(merged, self_index) == SYM_SELF);
    Copy_Cell(Varlist_Slot(merged, self_index), Varlist_Archetype(merged));

    return merged;
}


//
//  Resolve_Context: C
//
// Only_words can be a block of words or an index in the target
// (for new words).
//
void Resolve_Context(
    VarList* target,
    VarList* source,
    Value* only_words,
    bool all,
    bool expand
) {
    PANIC_IF_READ_ONLY_CONTEXT(target);

    REBLEN i;
    if (Is_Integer(only_words)) { // Must be: 0 < i <= tail
        i = VAL_INT32(only_words);
        if (i == 0)
            i = 1;
        if (i > Varlist_Len(target))
            return;
    }
    else
        i = 0;

    struct Reb_Binder binder;
    INIT_BINDER(&binder, nullptr);  // don't use lib context speedup

    Value* key;
    Value* var;

    REBINT n = 0;

    // If limited resolve, tag the word ids that need to be copied:
    if (i != 0) {
        // Only the new words of the target:
        for (key = Varlist_Key(target, i); NOT_END(key); key++)
            Add_Binder_Index(&binder, Key_Canon(key), -1);
        n = Varlist_Len(target);
    }
    else if (Is_Block(only_words)) {
        // Limit exports to only these words:
        Cell* word = Cell_List_At(only_words);
        for (; NOT_END(word); word++) {
            if (Is_Word(word) or Is_Set_Word(word)) {
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
        for (key = Varlist_Keys_Head(target); NOT_END(key); key++)
            if (Get_Binder_Index_Else_0(&binder, Key_Canon(key)) != 0)
                --n;

        // Expand context by the amount required:
        if (n > 0)
            Expand_Context(target, n);
        else
            expand = false;
    }

    // Maps a word to its value index in the source context.
    // Done by marking all source words (in bind table):
    key = Varlist_Keys_Head(source);
    for (n = 1; NOT_END(key); n++, key++) {
        Symbol* canon = Key_Canon(key);
        if (Is_Nulled(only_words))
            Add_Binder_Index(&binder, canon, n);
        else {
            if (Get_Binder_Index_Else_0(&binder, canon) != 0) {
                Remove_Binder_Index(&binder, canon);
                Add_Binder_Index(&binder, canon, n);
            }
        }
    }

    // Foreach word in target, copy the correct value from source:
    //
    var = i != 0 ? Varlist_Slot(target, i) : Varlist_Slots_Head(target);
    key = i != 0 ? Varlist_Key(target, i) : Varlist_Keys_Head(target);
    for (; NOT_END(key); key++, var++) {
        REBINT m = Remove_Binder_Index_Else_0(&binder, Key_Canon(key));
        if (m != 0) {
            // "the remove succeeded, so it's marked as set now" (old comment)
            if (
                Not_Cell_Flag(var, PROTECTED)
                and (all or Is_Trash(var))
            ){
                if (m < 0)
                    Init_Trash(var);  // treat as undefined in source context
                else
                    Move_Var(var, Varlist_Slot(source, m));
            }
        }
    }

    // Add any new words and values:
    if (expand) {
        key = Varlist_Keys_Head(source);
        for (n = 1; NOT_END(key); n++, key++) {
            Symbol* canon = Key_Canon(key);
            if (Remove_Binder_Index_Else_0(&binder, canon) != 0) {
                //
                // Note: no protect check is needed here
                //
                var = Append_Context(target, nullptr, canon);
                Move_Var(var, Varlist_Slot(source, n));
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
            for (key = Varlist_Key(target, i); NOT_END(key); key++)
                Remove_Binder_Index_Else_0(&binder, Key_Canon(key));
        }
        else if (Is_Block(only_words)) {
            Cell* word = Cell_List_At(only_words);
            for (; NOT_END(word); word++) {
                if (Is_Word(word) or Is_Set_Word(word))
                    Remove_Binder_Index_Else_0(&binder, VAL_WORD_CANON(word));
            }
        }
        else {
            for (key = Varlist_Keys_Head(source); NOT_END(key); key++)
                Remove_Binder_Index_Else_0(&binder, Key_Canon(key));
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
REBLEN Find_Canon_In_Context(VarList* context, Symbol* canon, bool always)
{
    assert(Get_Flex_Info(canon, CANON_SYMBOL));

    Value* key = Varlist_Keys_Head(context);
    REBLEN len = Varlist_Len(context);

    REBLEN n;
    for (n = 1; n <= len; n++, key++) {
        if (canon == Key_Canon(key)) {
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
// the value for the word.  Return nullptr if the canon is not found.
//
Value* Select_Canon_In_Context(VarList* context, Symbol* canon)
{
    const bool always = false;
    REBLEN n = Find_Canon_In_Context(context, canon, always);
    if (n == 0)
        return nullptr;

    return Varlist_Slot(context, n);
}


//
//  Obj_Value: C
//
// Return pointer to the nth VALUE of an object.
// Return nullptr if the index is not valid.
//
// !!! All cases of this should be reviewed...mostly for getting an indexed
// field out of a port.  If the port doesn't have the index, should it always
// be an error?
//
Value* Obj_Value(Value* value, REBLEN index)
{
    VarList* context = Cell_Varlist(value);

    if (index > Varlist_Len(context)) return 0;
    return Varlist_Slot(context, index);
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
    Free_Unmanaged_Flex(TG_Buf_Collect);
    TG_Buf_Collect = nullptr;
}


#if RUNTIME_CHECKS

//
//  Assert_Context_Core: C
//
void Assert_Context_Core(VarList* c)
{
    Array* varlist = Varlist_Array(c);

    if (not (varlist->leader.bits & SERIES_MASK_CONTEXT))
        crash (varlist);

    Array* keylist = Keylist_Of_Varlist(c);
    if (keylist == nullptr)
        crash (c);

    Value* rootvar = Varlist_Archetype(c);
    if (not Any_Context(rootvar))
        crash (rootvar);

    REBLEN keys_len = Array_Len(keylist);
    REBLEN vars_len = Array_Len(varlist);

    if (keys_len < 1)
        crash (keylist);

    if (keys_len != vars_len)
        crash (c);

    if (rootvar->payload.any_context.varlist != varlist)
        crash (rootvar);

    if (Get_Flex_Info(c, INACCESSIBLE)) {
        //
        // !!! For the moment, don't check inaccessible stack frames any
        // further.  This includes varless reified frames and those reified
        // frames that are no longer on the stack.
        //
        return;
    }

    Value* rootkey = CTX_ROOTKEY(c);
    if (Is_Cell_Unreadable(rootkey)) {
        //
        // Note that in the future the rootkey for ordinary OBJECT! or ERROR!
        // PORT! etc. may be more interesting than BLANK.  But it uses that
        // for now--unreadable.
        //
        if (Is_Frame(rootvar))
            crash (c);
    }
    else if (Is_Action(rootkey)) {
        //
        // At the moment, only FRAME! is able to reuse an ACTION!'s keylist.
        // There may be reason to relax this, if you wanted to make an
        // ordinary object that was a copy of a FRAME! but not a FRAME!.
        //
        if (not Is_Frame(rootvar))
            crash (rootvar);

        // In a FRAME!, the keylist is for the underlying function.  So to
        // know what function the frame is actually for, one must look to
        // the "phase" field...held in the rootvar.
        //
        if (
            ACT_UNDERLYING(rootvar->payload.any_context.phase)
            != VAL_ACTION(rootkey)
        ){
            crash (rootvar);
        }

        Option(Level*) L = Level_Of_Varlist_If_Running(c);
        if (L) {
            //
            // If the frame is on the stack, the phase should be something
            // with the same underlying function as the rootkey.
            //
            if (
                ACT_UNDERLYING(rootvar->payload.any_context.phase)
                != VAL_ACTION(rootkey)
            ){
                crash (rootvar);
            }
        }
    }
    else
        crash (rootkey);

    Value* key = Varlist_Keys_Head(c);
    Value* var = Varlist_Slots_Head(c);

    REBLEN n;
    for (n = 1; n < keys_len; n++, var++, key++) {
        if (IS_END(key)) {
            printf("** Early key end at index: %d\n", cast(int, n));
            crash (c);
        }

        if (not Is_Typeset(key))
            crash (key);

        if (IS_END(var)) {
            printf("** Early var end at index: %d\n", cast(int, n));
            crash (c);
        }
    }

    if (NOT_END(key)) {
        printf("** Missing key end at index: %d\n", cast(int, n));
        crash (key);
    }

    if (NOT_END(var)) {
        printf("** Missing var end at index: %d\n", cast(int, n));
        crash (var);
    }
}

#endif
