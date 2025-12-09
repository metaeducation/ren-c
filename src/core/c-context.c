//
//  file: %c-context.c
//  summary: "Management routines for ANY-CONTEXT? key/value storage"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//  Alloc_Varlist_Core: C
//
// Create context with capacity, allocating space for both words and values.
// Context will report actual Varlist_Len() of 0 after this call.
//
VarList* Alloc_Varlist_Core(Flags flags, Heart heart, REBLEN capacity)
{
    assert(Flavor_From_Flags(flags) == FLAVOR_0);  // always make varlist
    assert(heart != TYPE_MODULE);

    Array* a = Make_Array_Core(
        STUB_MASK_VARLIST  // includes assurance of dynamic allocation
            | flags,  // e.g. BASE_FLAG_MANAGED
        capacity + 1  // size + room for rootvar (array terminator implicit)
    );
    Tweak_Misc_Varlist_Adjunct_Raw(a, nullptr);
    Tweak_Link_Inherit_Bind_Raw(a, nullptr);

    require (  // allocate rootvar
      Alloc_Tail_Array(a)
    );
    Tweak_Non_Frame_Varlist_Rootvar(a, heart);

    require (
      KeyList* keylist = u_downcast Make_Flex(
        STUB_MASK_KEYLIST | BASE_FLAG_MANAGED,  // always shareable
        capacity  // no terminator
    ));
    Tweak_Link_Keylist_Ancestor(keylist, keylist);  // default to self
    assert(Flex_Used(keylist) == 0);

    Tweak_Bonus_Keylist_Unique(a, keylist);  // not shared yet...

    return cast(VarList*, a);  // varlist pointer is context handle
}


//
//  Alloc_Sea_Core: C
//
SeaOfVars* Alloc_Sea_Core(Flags flags) {
    assert(Flavor_From_Flags(flags) == FLAVOR_0);  // always make sea

    require (
      Stub* s = Prep_Stub(flags | STUB_MASK_SEA_NO_MARKING, Alloc_Stub())
    );
    Force_Erase_Cell(&s->content.fixed.cell);
    Init_Space(Stub_Cell(s));
    LINK_CONTEXT_INHERIT_BIND(s) = nullptr;  // no LINK_NEEDS_MARK flag
    MISC_SEA_ADJUNCT(s) = nullptr;  // no MISC_NEEDS_MARK flag

    return cast(SeaOfVars*, s);
}


//
//  Keylist_Of_Expanded_Varlist: C
//
// Expand a varlist. Copy keylist if is not unique (returns it to help
// emphasize that the keylist you saw the varlist have before may change.)
//
// 1. Tweak_Bonus_Keylist_Shared was used to set the flag that indicates
//    this keylist is shared with one or more other contexts.  Can't expand
//    the shared copy without impacting the others, so break away from the
//    sharing group by making a new copy.
//
//    (If all shared copies break away in this fashion, then the last copy of
//    the dangling keylist will be GC'd.)
//
// 2. Preserve link to ancestor keylist.  Note that if it pointed to itself,
//    we update this keylist to point to itself.
//
//    !!! Any extant derivations to the old keylist will still point to that
//    keylist at the time the derivation was performed...it will not consider
//    this new keylist to be an ancestor match.  Hence expanded objects are
//    essentially all new objects as far as derivation are concerned, though
//    they can still run against ancestor methods.
//
//    !!! NOTE: Ancestor keylists are no longer used for what they used to be
//    and may be gotten rid of or rethought.
//
// 3. Tweak_Bonus_Keylist_Unique() was used to set this keylist in the
//    varlist, and no Tweak_Bonus_Keylist_Shared() was used by another
//    varlist to mark the flag indicating it's shared.  Extend it directly.
//
KeyList* Keylist_Of_Expanded_Varlist(VarList* varlist, REBLEN delta)
{
    KeyList* k = Bonus_Keylist(varlist);
    assert(Is_Stub_Keylist(k));
    if (delta == 0)  // should we allow 0 delta?
        return k;

    Length len = Varlist_Len(varlist);

    require (
      Extend_Flex_If_Necessary_But_Dont_Change_Used(
        Varlist_Array(varlist), delta
      )
    );
    Set_Flex_Len(Varlist_Array(varlist), len + delta + 1);  // include rootvar

    if (Get_Flavor_Flag(KEYLIST, k, SHARED)) {  // need new keylist [1]
        require (
          KeyList* k_copy = u_downcast Copy_Flex_At_Len_Extra(
            STUB_MASK_KEYLIST,
            k,
            0,
            Flex_Used(k),
            delta
        ));

        if (Link_Keylist_Ancestor(k) == k)  // preserve ancestor link [2]
            Tweak_Link_Keylist_Ancestor(k_copy, k_copy);
        else
            Tweak_Link_Keylist_Ancestor(k_copy, k);

        Manage_Stub(k_copy);
        Tweak_Bonus_Keylist_Unique(varlist, k_copy);

        Set_Flex_Len(k_copy, len + delta);
        return k_copy;
    }

    require (  // unshared, in place [3]
      Extend_Flex_If_Necessary_But_Dont_Change_Used(k, delta)
    );
    Set_Flex_Len(k, len + delta);

    return k;
}


// 1. Low symbol IDs are all in g_lib_patches for fast access, and were
//    created as a continguous array of memory in Startup_Lib().
//
// 2. The GC behavior of these patches is special and does not fit into the
//    usual patterns.  There is a pass in the GC that propagates context
//    survival into the patches from the global bind table.  Although we say
//    INFO_NEEDS_MARK to keep a context alive, that marking isn't done in
//    that pass...otherwise the variables could never GC.  Instead, it only
//    happens if the patch is cached in a variable...then that reference
//    touches the patch which touches the context.  But if not cached, the
//    context keeps vars alive; not vice-versa (e.g. the mere existence of a
//    variable--not cached in a cell reference--should not keep it alive).
//    MISC_NEEDS_MARK is not done as that would keep alive patches from
//    other contexts in the hitch chain.
//
//    !!! Should there be a "decay and forward" general mechanic, so a base
//    can tell the GC to touch up all references and point to something else,
//    e.g. to forward references to a cache back to the context in order to
//    "delete" variables?
//
Init(Slot) Append_To_Sea_Core(
    SeaOfVars* sea,
    const Symbol* symbol,
    Option(Element*) any_word  // binding modified (Note: quoted words allowed)
){
    Option(SymId) id;
    if (sea == g_lib_context)
        id = Symbol_Id(symbol);
    else
        id = SYM_0;

    Patch* patch;
    if (id and cast(int, id) < MAX_SYM_LIB_PREMADE) {
        patch = &g_lib_patches[cast(int, id)];  // pre-allocated at boot [1]
        assert(INFO_PATCH_SEA(patch) == nullptr);  // don't double add
        // patch->header.bits should be already set

        TRACK(Erase_Cell(Stub_Cell(patch)));  // prepare for addition
    }
    else {
        require (
          patch = u_downcast Make_Untracked_Stub(STUB_MASK_PATCH)
        );
    }

  add_to_circularly_linked_list_hung_on_symbol: {

    // The variables are linked reachable from the symbol base for the word's
    // spelling, and can be directly linked to from a word as a singular value
    // (with binding index as INDEX_PATCHED).  A circularly linked list is
    // used, to facilitate circling around to remove from the list (in lieu of
    // a back pointer.)
    //
    // 1. During binding of non-sea-of-words contexts, another kind of link is
    //    added into the chain to help accelerate finding the slot to bind
    //    for that symbol.  We skip over those.

    Stub* updating = m_cast(Symbol*, symbol);  // skip binding hitches [1]
    if (Get_Flavor_Flag(SYMBOL, updating, HITCH_IS_BIND_STUMP))
        updating = Misc_Hitch(updating);  // skip

    Tweak_Misc_Hitch(patch, Misc_Hitch(updating));
    Tweak_Info_Patch_Sea(patch, sea);
    Tweak_Misc_Hitch(updating, patch);  // may be binding stump

    if (any_word) {  // bind word while we're at it
        Tweak_Cell_Binding(unwrap any_word, sea);
        Tweak_Word_Stub(unwrap any_word, patch);
    }

} assert_if_duplicate_patch: {

  #if RUNTIME_CHECKS  // ensure we didn't add a duplicate patch for this sea
    Stub *check = Misc_Hitch(patch);
    while (check != symbol) {  // walk chain to look for duplicates
        assert(Info_Patch_Sea(cast(Patch*, check)) != sea);
        check = Misc_Hitch(check);
    }
  #endif

} return_patch: {

    return u_cast(Init(Slot), u_cast(Cell*, Stub_Cell(patch)));
}}


static Init(Slot) Append_To_Varlist_Core(
    VarList* varlist,
    const Symbol* symbol,
    Option(Element*) any_word
){
  catch_duplicate_insertions: {

  // 1. If objects have identical keys, they may share the same keylist.  But
  //    when an object gets expanded, that shared keylist has to be copied to
  //    become unique to that object.  When this happens, the keylist identity
  //    can change.

  #if RUNTIME_CHECKS
    KeyList* before = Bonus_Keylist(varlist);  // may change if shared [1]
    const Key* check_tail = Flex_Tail(Key, before);
    const Key* check = Flex_Head(Key, before);
    for (; check != check_tail; ++check)
        assert(Key_Symbol(check) != symbol);
  #endif

} perform_append: {

    KeyList* keylist = Keylist_Of_Expanded_Varlist(varlist, 1);  // unique [1]
    Init_Key(Flex_Last(Key, keylist), symbol);

    Cell* slot = Array_Last(Varlist_Array(varlist));
    // leave uninitialized (if caller wants an unset variable, they do that)

    if (any_word) {
        Length len = Varlist_Len(varlist);  // length we just bumped
        Tweak_Cell_Binding(unwrap any_word, varlist);
        Tweak_Word_Index(unwrap any_word, len);
    }

    return u_cast(Init(Slot), slot);  // location we just added (void cell)
}}


// Append a word to the context word list. Expands the list if necessary.
// Returns the value cell for the word, which is reset.
//
// If word is not nullptr, use the word sym and bind the word value, otherwise
// use sym.  When using a word, it will be modified to be specifically bound
// to this context after the operation.
//
static Init(Slot) Append_Context_Core(
    Context* context,
    const Symbol* symbol,
    Option(Element*) any_word  // binding modified (Note: quoted words allowed)
){
    if (Is_Stub_Sea(context))
        return Append_To_Sea_Core(cast(SeaOfVars*, context), symbol, any_word);

    return Append_To_Varlist_Core(cast(VarList*, context), symbol, any_word);
}


//
//  Append_Context_Bind_Word: C
//
Init(Slot) Append_Context_Bind_Word(
    Context* context,
    Element* any_word  // binding modified (Note: quoted words allowed)
){
    return Append_Context_Core(context, Word_Symbol(any_word), any_word);
}

//
//  Append_Context: C
//
Init(Slot) Append_Context(Context* context, const Symbol* symbol)
{
    return Append_Context_Core(context, symbol, nullptr);
}


//
//  Construct_Collector_Core: C
//
// Begin using a "binder" to start mapping canon symbol names to integer
// indices.  The symbols are collected on the stack.  Use Destruct_Collector()
// to free the binder.
//
// 1. If you're doing a collection on behalf of a module, its variables are
//    already distributed across the symbol table.  There is no need to put
//    entries in the bind table for what's already in it, duplicate detection
//    will already be fast enough.
//
// 2. If you're collecting on behalf of a varlist-based object, each check
//    of a word would require a linear search of the keylist to see if there
//    were duplicates.  For small objects that might be fast, but we put
//    a binder link right on the symbol itself to be even faster.  (Review
//    the actual performance tradeoffs of this, esp. for small objects).
//
void Construct_Collector_Core(
    Collector* cl,
    CollectFlags flags,
    Option(Context*) context
){
    cl->initial_flags = flags;
    cl->next_index = 1;

    Construct_Binder_Core(&cl->binder);

    if (context) {
        if (Is_Stub_Sea(unwrap context))  // no binder preload [1]
            cl->sea = cast(SeaOfVars*, unwrap context);
        else {
            cl->sea = nullptr;
            const Symbol* duplicate;
            Collect_Context_Keys(  // preload binder, assist dup detection [2]
                &duplicate,
                cl,
                cast(VarList*, unwrap context)
            );
            assert(not duplicate);  // context should have had all unique keys
        }
    }
    else
        cl->sea = nullptr;

    cl->base_stump = cl->binder.stump_list;
}


//
//  Destruct_Collector_Core: C
//
// Reset the bind markers in the canon Stubs so they can be reused, and drop
// the collected words from the stack.
//
void Destruct_Collector_Core(Collector *cl)
{
    Destruct_Binder_Core(&cl->binder);
    Corrupt_If_Needful(cl->base_stump);
}


//
//  Collect_Context_Keys: C
//
// Collect keys from a context to the data stack, indexing them in a binder.
// If requested, it will return the first duplicate found (or null).
//
void Collect_Context_Keys(
    Option(const Symbol**) duplicate,
    Collector *cl,
    VarList* context
){
    assert(CTX_TYPE(context) != TYPE_MODULE);

    const Key* tail;
    const Key* key = Varlist_Keys(&tail, context);

    if (duplicate)
        *(unwrap duplicate) = nullptr;

    for (; key != tail; ++key) {
        const Symbol* symbol = Key_Symbol(key);
        if (Try_Add_Binder_Index(
            &cl->binder,
            symbol,
            cl->next_index
        )){
            ++cl->next_index;
        }
        else {  // don't collect if already in bind table
            if (duplicate and not *(unwrap duplicate))  // returns first dup
                *(unwrap duplicate) = symbol;
        }
    }
}


//
//  Collect_Inner_Loop: C
//
// The inner recursive loop used for collecting context keys or ANY-WORD?s.
//
// 1. !!! Should this consider sequences, or their embedded groups/arrays?
//    This is less certain as the purpose of collect words is not clear given
//    stepping away from SET-WORD! gathering as locals.
//
//      https://github.com/rebol/rebol-issues/issues/2276
//
static Result(None) Collect_Inner_Loop(
    Collector *cl,
    CollectFlags flags,
    const Element* head,
    const Element* tail
){
    const Element* e = head;
    for (; e != tail; ++e) {
        bool bound;
        Option(const Symbol*) symbol = Try_Get_Settable_Word_Symbol(&bound, e);

        if (
            symbol or (
                (flags & COLLECT_ANY_WORD)
                and Is_Cell_Wordlike(e)
                and (bound = IS_WORD_BOUND(e), symbol = Word_Symbol(e))
            )
        ){
            if (bound) {
                if (flags & COLLECT_TOLERATE_PREBOUND)
                    continue;

                return fail (Error_Collectable_Bound_Raw(e));
            }

            if (cl->sea) {
                bool strict = true;
                if (Sea_Slot(unwrap cl->sea, unwrap symbol, strict))
                    continue;
            }

            if (Try_Add_Binder_Index(
                &cl->binder,
                unwrap symbol,
                cl->next_index
            )){
                ++cl->next_index;
            }
            else if (flags & COLLECT_NO_DUP) {
                DECLARE_ELEMENT (duplicate);
                Init_Word(duplicate, unwrap symbol);
                return fail (Error_Dup_Vars_Raw(duplicate));
            }
            else {
                // tolerate duplicate
            }

            continue;
        }

        if (Is_Set_Block(e)) {  // `[[a b] ^c :d (e)]:` collects all but E
            const Element* sub_tail;
            const Element* sub_at = List_At(&sub_tail, e);

            trap (
              Collect_Inner_Loop(
                cl,
                COLLECT_ANY_WORD | COLLECT_DEEP_BLOCKS | COLLECT_DEEP_FENCES,
                sub_at,
                sub_tail
            ));

            continue;
        }

        if (
            not ((flags & COLLECT_ANY_LIST_DEEP) and Any_List(e))  // !!! [1]
            and not ((flags & COLLECT_DEEP_BLOCKS) and Is_Block(e))
            and not ((flags & COLLECT_DEEP_FENCES) and Is_Fence(e))
        ){
            continue;
        }

        const Element* sub_tail;
        const Element* sub_at = List_At(&sub_tail, e);

        trap (
          Collect_Inner_Loop(cl, flags, sub_at, sub_tail)
        );
    }

    return none;
}


//
//  Wrap_Extend_Core: C
//
// This exposes the functionality of WRAP* so it can be used by the boot
// process on LIB before natives can be called.
//
Result(None) Wrap_Extend_Core(
    Context* context,
    const Element* list,
    CollectFlags flags
){
    DECLARE_COLLECTOR (cl);
    Construct_Collector(cl, flags, context);  // no-op preload if SeaOfVars

    const Element* tail;
    const Element* at = List_At(&tail, list);

    Collect_Inner_Loop(
        cl, flags, at, tail
    ) except (Error* e) {
        Destruct_Collector(cl);
        return fail (e);
    }

    Option(Stump*) stump = cl->binder.stump_list;
    for (; stump != cl->base_stump; stump = Link_Stump_Next(unwrap stump)) {
        const Symbol* symbol = Info_Stump_Bind_Symbol(unwrap stump);
        Init_Ghost_For_Unset(Append_Context(context, symbol));
    }

    Destruct_Collector(cl);
    return none;
}


//
//  wrap*: native [
//
//  "Expand context with top-level set-words from a block"
//
//      return: ~
//      context [any-context?]
//      list [<opt-out> any-list?]
//  ]
//
DECLARE_NATIVE(WRAP_P)
//
// 1. !!! It's not clear what the right set of primitives are...we may want
//    to expand based on a block and then run the block with a different
//    binding.  Be conservative for now...routine will need review and
//    renaming based on emerging uses.
{
    INCLUDE_PARAMS_OF_WRAP_P;

    CollectFlags flags = COLLECT_ONLY_SET_WORDS;

    Context* context = Cell_Context(ARG(CONTEXT));
    Element* list = cast(Element*, ARG(LIST));

    require (
      Wrap_Extend_Core(context, list, flags)
    );
    /*
        Tweak_Cell_Binding(list, use);  // what should do what here?
        return COPY(list);  // should this return a list?
    */

    return TRASH;
}


//
//  wrap: native [
//
//  "Bind code in context made from top-level set-words from a block"
//
//      return: [
//          ~[any-list? object!]~ "List with new binding, and created context"
//      ]
//      list [<opt-out> any-list?]
//      :deep "Look for assigning constructs deeply"
//      :set "Use semantics for WRAP of a SET-BLOCK for list argument"
//  ]
//
DECLARE_NATIVE(WRAP)
{
    INCLUDE_PARAMS_OF_WRAP;

    Element* list = cast(Element*, ARG(LIST));

    const Element* tail;
    const Element* at = List_At(&tail, list);
    VarList* parent = nullptr;

    CollectFlags flags = COLLECT_ONLY_SET_WORDS;
    if (Bool_ARG(SET))
        flags = COLLECT_DEEP_BLOCKS | COLLECT_DEEP_FENCES | COLLECT_ANY_WORD;
    if (Bool_ARG(DEEP))
        flags |= COLLECT_ANY_LIST_DEEP;

    VarList* varlist = Make_Varlist_Detect_Managed(
        flags,
        TYPE_OBJECT,  // !!! Presume object?
        at,
        tail,
        parent
    );
    Tweak_Link_Inherit_Bind(varlist, Cell_Binding(list));
    Tweak_Cell_Binding(list, varlist);

    Source* pack = Make_Source_Managed(2);
    Set_Flex_Len(pack, 2);
    Copy_Lifted_Cell(Array_At(pack, 0), list);
    Liftify(Init_Object(Array_At(pack, 1), varlist));

    return Init_Pack(OUT, pack);
}


//
//  collect-words: native [
//
//  "Collect unique words used in a block (used for context construction)"
//
//      return: [block!]
//      block [block!]
//      :deep "Include nested blocks"
//      :set "Only include set-words"
//      :ignore "Ignore these words"
//          [block! object!]
//  ]
//
DECLARE_NATIVE(COLLECT_WORDS)
{
    INCLUDE_PARAMS_OF_COLLECT_WORDS;

    Flags flags;
    if (Bool_ARG(SET))
        flags = COLLECT_ONLY_SET_WORDS;
    else
        flags = COLLECT_ANY_WORD;

    if (Bool_ARG(DEEP))
        flags |= COLLECT_ANY_LIST_DEEP;

  //=//// GENERATE DUMMY BINDINGS FOR THE IGNORED SYMBOLS /////////////////=//

    // 1. We do not want to panic() during the bind at this point in time (the
    //    system doesn't know how to clean up, and the only cleanup it does
    //    assumes you were collecting for a keylist...it doesn't have access to
    //    the "ignore" bindings.)  Do a pre-pass to panic first, if there are
    //    any non-words in a block the user passed in.
    //
    // 2. The way words get ignored in the collecting process is to give them
    //    dummy bindings so it appears they've "already been collected", but
    //    not actually add them to the collection.  Then, duplicates don't
    //    cause an error...so they will just be skipped when encountered.
    //
    // 3. /IGNORE may have duplicate words in it (this situation arises when
    //    `function [/test /test] []` calls COLLECT-WORDS and tries to ignore
    //    both tests.  Debug build counts the number (overkill, tests binder).

    Stable* ignore = ARG(IGNORE);

    if (Is_Block(ignore)) {  // avoid panic in mid-collect [1]
        const Element* check_tail;
        const Element* check = List_At(&check_tail, ignore);
        for (; check != check_tail; ++check) {
            if (not Any_Word(check))
                panic (Error_Bad_Value(check));
        }
    }

    DECLARE_COLLECTOR (cl);
    Option(Context*) no_context = nullptr;
    Construct_Collector(cl, flags, no_context);

    if (Is_Block(ignore)) {  // ignore via dummy bindings [2]
        const Element* ignore_tail;
        const Element* ignore_at = List_At(&ignore_tail, ignore);
        for (; ignore_at != ignore_tail; ++ignore_at) {
            const Symbol* symbol = Word_Symbol(ignore_at);

            if (not Try_Add_Binder_Index(&cl->binder, symbol, -1)) {
              #if RUNTIME_CHECKS  // count dups, overkill [3]
                REBINT i = unwrap Try_Get_Binder_Index(&cl->binder, symbol);
                assert(i < 0);
                Update_Binder_Index(&cl->binder, symbol, i - 1);
              #endif
            }
        }
    }
    else if (Is_Object(ignore)) {
        const Key* key_tail;
        const Key* key = Varlist_Keys(&key_tail, Cell_Varlist(ignore));
        for (; key != key_tail; ++key)
            Add_Binder_Index(&cl->binder, Key_Symbol(key), -1);  // no dups
    }
    else
        assert(Is_Nulled(ignore));

  //=//// RUN COMMON COLLECTION CODE //////////////////////////////////////=//

    const Element* block_tail;
    const Element* block_at = List_At(&block_tail, ARG(BLOCK));

    require (
      Collect_Inner_Loop(cl, flags, block_at, block_tail)
    );

    StackIndex base = TOP_INDEX;  // could be more efficient to calc/add

    Option(Stump*) stump = cl->binder.stump_list;
    for (; stump != cl->base_stump; stump = Link_Stump_Next(unwrap stump)) {
        REBINT index = VAL_INT32(Known_Element(Stub_Cell(unwrap stump)));
        assert(index != 0);
        if (index < 0)
            continue;
        Init_Word(PUSH(), Info_Stump_Bind_Symbol(unwrap stump));
    }

    Source* array = Pop_Managed_Source_From_Stack(base);

  //=//// REMOVE DUMMY BINDINGS FOR THE IGNORED SYMBOLS ///////////////////=//

    Destruct_Collector(cl);  // does removal automatically

    return Init_Block(OUT, array);
}


//
//  Make_Varlist_Detect_Managed: C
//
// Create a context by detecting top-level set-words in an array of values.
// So if the values were the contents of the block `[a: 10 b: 20]` then the
// resulting context would be for two words, `a` and `b`.
//
// Optionally a parent context may be passed in, which will contribute its
// keylist of words to the result if provided, as well as give defaults for
// the values of those keys.
//
VarList* Make_Varlist_Detect_Managed(
    CollectFlags flags,
    Heart heart,
    const Element* head,
    const Element* tail,
    Option(VarList*) parent
) {
    assert(heart != TYPE_MODULE);

  //=//// COLLECT KEYS (FROM PARENT AND WALKING HEAD->TAIL) ///////////////=//

    DECLARE_COLLECTOR (cl);
    Construct_Collector(cl, flags, parent);  // preload binder with parent's keys

    require (
      Collect_Inner_Loop(cl, flags, head, tail)
    );

    Length len = cl->next_index - 1;  // is next index, so subtract 1

  //=//// CREATE NEW VARLIST AND CREATE (OR REUSE) KEYLIST ////////////////=//

    Array* a = Make_Array_Core(
        STUB_MASK_VARLIST
            | BASE_FLAG_MANAGED, // Note: Rebind below requires managed context
        1 + len  // needs room for rootvar
    );
    Set_Flex_Len(a, 1 + len);
    Tweak_Misc_Varlist_Adjunct_Raw(a, nullptr);
    Tweak_Link_Inherit_Bind_Raw(a, nullptr);

    if (
        parent
        and Varlist_Len(unwrap parent) == len  // no new keys, reuse list
    ){
        Tweak_Bonus_Keylist_Shared(
            a,
            Bonus_Keylist(unwrap parent)  // leave ancestor link as-is
        );
    }
    else {  // new keys, need new keylist
        require (
          KeyList* keylist = u_downcast Make_Flex(
            STUB_MASK_KEYLIST | BASE_FLAG_MANAGED,
            len  // no terminator, 0-based
        ));

        Set_Flex_Used(keylist, len);

        Key* key = Flex_Tail(Key, keylist);  // keys are backwards order
        Option(Stump*) stump = cl->binder.stump_list;  // ALL, not base_stump
        for (; stump != nullptr; stump = Link_Stump_Next(unwrap stump)) {
            const Symbol* s = Info_Stump_Bind_Symbol(unwrap stump);
            --key;
            Init_Key(key, s);
        }

        Tweak_Bonus_Keylist_Unique(a, keylist);
        if (parent)
            Tweak_Link_Keylist_Ancestor(
                keylist, Bonus_Keylist(unwrap parent)
            );
        else
            Tweak_Link_Keylist_Ancestor(
                keylist, keylist  // ancestors terminate in self
            );
    }

    Destruct_Collector(cl);  // !!! binder might be useful for ensuing operations...

  //=//// COPY INHERITED VALUES FROM PARENT, OR INIT TO TRASH ///////////=//

    // 1. !!! Lacking constructors, there is an idea that extending an object
    //    means copying its series values deeply.  This is kind of clearly
    //    dumb...what should happen depends on the semantics of why you are
    //    doing the instantiation and what the thing is.  Better ideas are
    //    hopefully coming down the pipe, but this is what R3-Alpha did.  :-/

    Value* var = Flex_Head(Stable, a);
    Tweak_Non_Frame_Varlist_Rootvar(a, heart);  // rootvar
    ++var;

    REBINT i;
    for (i = 1; i <= len; ++i, ++var)  // 0th item is rootvar, already filled
        Init_Ghost_For_Unset(var);  // need all slots valid before Read_Slot()?

    if (parent) {
        Value* dest = Flex_At(Value, a, 1);
        const Slot* src_tail;
        Slot* src = Varlist_Slots(&src_tail, unwrap parent);
        for (; src != src_tail; ++dest, ++src) {
            Flags clone_flags = BASE_FLAG_MANAGED;  // !!! Review, what flags?

            // !!! If we are creating a derived object, should it be able
            // to copy the ACCESSOR/etc.?
            //
            require (
              Read_Slot_Meta(dest, src)
            );

            bool deeply = true;  // !!! Copies series deeply, why? [1]
            if (not Is_Antiform(dest)) {  // !!! whole model needs review
                require (
                  Clonify(Known_Element(dest), clone_flags, deeply)
                );
                Clear_Cell_Flag(dest, CONST);  // remove constness from copies
            }
        }
    }

    VarList* varlist = cast(VarList*, a);
    Assert_Varlist(varlist);

  #if DEBUG_COLLECT_STATS
    g_mem.objects_made += 1;
  #endif

    return varlist;
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
Result(Source*) Context_To_Array(const Element* context, REBINT mode)
{
    assert(!(mode & 4));

    StackIndex base = TOP_INDEX;

    EVARS e;
    Init_Evars(&e, context);

    while (Try_Advance_Evars(&e)) {
        if (mode & 1) {
            assert(e.index != 0);
            Init_Word(PUSH(), Key_Symbol(e.key));
            if (mode & 2) {
                trap (
                  Setify(TOP_ELEMENT)
                );
            }
            if (Is_Module(context)) {
                Tweak_Cell_Binding(TOP_ELEMENT, e.ctx);
            }
            else {
                Tweak_Cell_Binding(TOP_ELEMENT, e.ctx);
                Tweak_Word_Index(TOP_ELEMENT, e.index);
            }

            if (mode & 2)
                Set_Cell_Flag(TOP_ELEMENT, NEWLINE_BEFORE);
        }

        if (mode & 2) {
            //
            // Context might have antiforms, which cannot be put in blocks.
            // This whole idea needs review.
            //
            if (Is_Antiform(Slot_Hack(e.slot)))
                return fail (Error_Anti_Object_Block_Raw());

            Copy_Cell(PUSH(), Slot_Hack(e.slot));
        }
    }

    Shutdown_Evars(&e);

    Source* a = Pop_Source_From_Stack(base);
    if (mode & 2)
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    return a;
}


//
//  Find_Symbol_In_Context: C
//
// Search a context looking for the given symbol.  Return the index or 0 if
// not found.
//
// Note that since contexts like FRAME! can have multiple keys with the same
// name, the Frame_Lens() of the context has to be taken into account.
//
Option(Index) Find_Symbol_In_Context(
    const Element* context,
    const Symbol* symbol,
    bool strict
){
    Heart heart = Heart_Of_Builtin(context);

    if (heart == TYPE_MODULE) {
        //
        // Modules hang their variables off the symbol itself, in a linked
        // list with other modules who also have variables of that name.
        //
        SeaOfVars* sea = Cell_Module_Sea(context);
        return Sea_Patch(sea, symbol, strict) ? INDEX_PATCHED : 0;
    }

    EVARS e;
    Init_Evars(&e, context);

    while (Try_Advance_Evars(&e)) {
        if (strict) {
            if (symbol != Key_Symbol(e.key))
                continue;
        }
        else {
            if (not Are_Synonyms(symbol, Key_Symbol(e.key)))
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
Option(Slot*) Select_Symbol_In_Context(
    const Element* context,
    const Symbol* symbol
){
    const bool strict = false;
    Option(Index) index = Find_Symbol_In_Context(context, symbol, strict);
    if (not index)
        return nullptr;

    return Varlist_Slot(Cell_Varlist(context), unwrap index);
}


//
//  Obj_Slot: C
//
// !!! All instances of this should be reviewed...
//
Slot* Obj_Slot(Stable* value, Index index)
{
    VarList* context = Cell_Varlist(value);

    if (index > Varlist_Len(context))
        panic ("Could not pick index out of object");  // !!! Review [1]

    return Varlist_Slot(context, index);
}


//
//  Startup_Collector: C
//
void Startup_Collector(void)
{
  #if RUNTIME_CHECKS
    assert(g_num_evars_outstanding == 0);
  #endif
}


//
//  Shutdown_Collector: C
//
void Shutdown_Collector(void)
{
  #if RUNTIME_CHECKS
    assert(g_num_evars_outstanding == 0);
  #endif
}


#if RUNTIME_CHECKS

//
//  Assert_Varlist_Core: C
//
void Assert_Varlist_Core(VarList* varlist)
{
    Array* a = Varlist_Array(varlist);

    if ((a->header.bits & STUB_MASK_VARLIST) != STUB_MASK_VARLIST)
        crash (varlist);

    Stable* rootvar = Rootvar_Of_Varlist(varlist);
    if (not Any_Context(rootvar) or Cell_Varlist(rootvar) != varlist)
        crash (rootvar);

    KeyList* keylist = Bonus_Keylist(varlist);

    Length keys_len = Flex_Used(keylist);
    Length array_len = Array_Len(a);

    if (array_len < 1)
        crash (varlist);

    if (keys_len + 1 != array_len)
        crash (varlist);

    const Key* key = Varlist_Keys_Head(varlist);
    Slot* slot = Varlist_Slots_Head(varlist);

    Length n;
    for (n = 1; n < array_len; ++n, ++slot, ++key) {
        if (Stub_Flavor(*key) != FLAVOR_SYMBOL)
            crash (*key);

      #if DEBUG_POISON_FLEX_TAILS
        if (Is_Cell_Poisoned(slot)) {
            printf("** Early var end at index: %d\n", cast(int, n));
            crash (varlist);
        }
      #endif
    }

  #if DEBUG_POISON_FLEX_TAILS
    if (not Is_Cell_Poisoned(slot)) {
        printf("** Missing var end at index: %d\n", cast(int, n));
        crash (slot);
    }
  #endif
}

#endif
