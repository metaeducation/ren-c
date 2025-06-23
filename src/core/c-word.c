//
//  file: %c-word.c
//  summary: "symbol table and word related functions"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// * In R3-Alpha, symbols were permanently interned in a table as UTF-8, and
//   referenced in cells by an integer index.  This was distinct from String
//   Flexes which varied their encoding sie and could be GC'd.  Ren-C
//   unifies this where Symbols are String Flexes which are referenced in
//   Cells by pointers and can be GC'd, and all Strings use UTF-8 everywhere.
//
// * Ren-C binding is case-sensitive.  This is a difficult decision, but
//   there is a good reasoning in the sense that it must be case-preserving,
//   and case-preserving without case-sensitivity creates problems:
//
//     https://forum.rebol.info/t/1439
//


#include "sys-core.h"

#define WORD_TABLE_SIZE 1024  // initial size in words


// https://primes.utm.edu/lists/2small/0bit.html
//
static uint32_t const g_primes[] =
{
    7,
    13,
    31,
    61,
    127,
    251,
    509,
    1021,
    2039,
    4093,
    8191,
    16381,
    32749,
    65521,
    131071,
    262139,
    524287,
    1048573,
    2097143,
    4194301,
    8388593,
    16777213,
    33554393,
    67108859,
    134217689,
    268435399,
    536870909,
    1073741789,
    2147483647,
    0xFFFFFFFB, // 4294967291 = 2^32 - 5 (C89)
    0
};


//
//  Try_Get_Hash_Prime: C
//
// Given a value, return a prime number that is larger or equal.
//
Option(uint32_t) Try_Get_Hash_Prime(uint32_t minimum)
{
    uint32_t n = 0;
    while (minimum > g_primes[n]) {
        ++n;
        if (g_primes[n] == 0)
            return 0;
    }

    return g_primes[n];
}


//
//  Get_Hash_Prime_May_Panic: C
//
uint32_t Get_Hash_Prime_May_Panic(uint32_t minimum)
{
    Option(uint32_t) prime = Try_Get_Hash_Prime(minimum);
    if (not prime) {  // larger than hash prime table
        DECLARE_ELEMENT (temp);
        Init_Integer(temp, minimum);
        panic (Error_Size_Limit_Raw(temp));
    }
    return unwrap prime;
}


// Removals from linear probing lists can be complex, because the same
// overflow slot may be visited through different initial hashes:
//
// http://stackoverflow.com/a/279812/211160
//
// "For linear probing, Knuth suggests that a simple approach is to have a
//  way to mark a slot as empty, deleted, or occupied. Mark a removed occupant
//  slot as deleted so that overflow by linear probing will skip past it, but
//  if an insertion is needed, you can fill the first deleted slot that you
//  passed over.  This assumes that deletions are rather rare."
//
// Since it's not enough to make the spot nullptr when an interned string
// is GC'd, a special pointer signaling "deletedness" is used.  It does not
// cause a linear probe to terminate, but it is reused on insertions.
//
#define DELETED_SYMBOL &g_symbols.deleted_symbol


//
//  Expand_Word_Table: C
//
// Expand the hash table part of the word_table by allocating the next larger
// table size and rehashing all the words of the current table.
//
// 1. The only full list of symbol words available is the old hash table.
//    Hold onto it while creating the new hash table, and free it once the
//    new table is built.
//
static void Expand_Word_Table(void)
{
    Length old_num_slots = Flex_Used(g_symbols.by_hash);
    Symbol** old_symbols_by_hash = Flex_Head(  // hold on temporarily [1]
        Symbol*,
        g_symbols.by_hash
    );

    Length num_slots = Get_Hash_Prime_May_Panic(old_num_slots + 1);
    assert(Flex_Wide(g_symbols.by_hash) == sizeof(Symbol*));

    Flex* table = Make_Flex(
        FLAG_FLAVOR(CANONTABLE) | FLEX_FLAG_POWER_OF_2,
        Flex,
        num_slots
    );
    Clear_Flex(table);
    Set_Flex_Len(table, num_slots);

    // Rehash all the symbols:

    Symbol** new_symbols_by_hash = Flex_Head(Symbol*, table);

    Offset old_slot;
    for (old_slot = 0; old_slot != old_num_slots; ++old_slot) {
        Symbol* symbol = old_symbols_by_hash[old_slot];
        if (not symbol)
            continue;

        if (symbol == DELETED_SYMBOL) {  // clean out deleted symbol entries
            g_symbols.num_slots_in_use -= 1;
          #if RUNTIME_CHECKS
            g_symbols.num_deleteds -= 1;  // keep track for shutdown assert
          #endif
            continue;
        }

        Length skip;
        Offset slot = First_Hash_Candidate_Slot(
            &skip,
            Hash_String(symbol),
            num_slots
        );

        while (new_symbols_by_hash[slot]) {  // skip occupied slots
            slot += skip;
            if (slot >= num_slots)
                slot -= num_slots;
        }
        new_symbols_by_hash[slot] = symbol;
    }

    Free_Unmanaged_Flex(g_symbols.by_hash);
    g_symbols.by_hash = table;
}


//
//  Intern_UTF8_Managed_Core: C
//
// Makes only one copy of each distinct character string.
//
//   https://en.wikipedia.org/wiki/String_interning
//
// The hashing technique used is called "linear probing":
//
//   https://en.wikipedia.org/wiki/Linear_probing
//
// 1. The result Symbol must be managed, because if they were not there could
//    be no clear contract on the return result--as it wouldn't be possible to
//    know if a shared instance had been managed by someone else or not.
//
// 2. Interning is case-sensitive, but a "synonym" linkage is established
//    between instances that are just differently upper-or-lower-"cased".
//    They agree on one "canon" interning to use for fast case-insensitive
//    compares.  If that canon form is GC'd, the agreed upon canon for the
//    group will change.
//
// 3. For the hash search to be guaranteed to terminate, the table must be
//    large enough that we are able to find nullptr if there's a miss.  (It's
//    actually kept larger than that, but to be on the right side of theory,
//    the table is always checked for expansion needs *before* the search.)
//
const Symbol* Intern_UTF8_Managed_Core(  // results implicitly managed [1]
    Option(void*) preallocated,  // most calls don't know if allocation needed
    const Byte* utf8,  // case-sensitive [2]
    Size utf8_size
){
    Length num_slots = Flex_Used(g_symbols.by_hash);
    if (g_symbols.num_slots_in_use > num_slots / 2) {
        Expand_Word_Table();  // must be able to find nullptr if miss [3]
        num_slots = Flex_Used(g_symbols.by_hash);  // got larger, update
    }

    Symbol** symbols_by_hash = Flex_Head(Symbol*, g_symbols.by_hash);

    Length skip;  // how many slots to skip when occupied candidates found
    Offset slot = First_Hash_Candidate_Slot(
        &skip,
        Hash_Scan_UTF8_Caseless_May_Panic(utf8, utf8_size),
        num_slots
    );

    Symbol* synonym = nullptr;
    Symbol* symbol;
    Symbol** deleted_slot = nullptr;

  find_synonym_or_unused_hash_slot: { ////////////////////////////////////////

    // 1. The > 0 result means that the canon word that was found is an
    //    alternate casing ("synonym") for the string we're interning.
    //    Synonyms are attached to the canon form with a circular list.

    while ((symbol = symbols_by_hash[slot])) {
        if (symbol == DELETED_SYMBOL) {
            deleted_slot = &symbols_by_hash[slot];
            goto next_candidate_slot;
        }

        REBINT cmp;  // initialization would be crossed by goto
        cmp = Compare_UTF8(String_Head(symbol), utf8, utf8_size);
        if (cmp == 0) {
            assert(not preallocated);
            return symbol;  // was a case-sensitive match
        }
        if (cmp < 0)
            goto next_candidate_slot;  // wasn't an alternate casing

        synonym = symbol;  // save for linking into synonyms list [1]

      next_candidate_slot: { /////////////////////////////////////////////

        slot += skip;  // https://en.wikipedia.org/wiki/Linear_probing
        if (slot >= num_slots)
            slot -= num_slots;
    }}

} new_interning: { ///////////////////////////////////////////////////////////

    Binary* b = cast(Binary*, Make_Flex_Into(
        FLEX_MASK_SYMBOL
            | SYMBOL_FLAG_ALL_ASCII,  // removed below if non-ascii found
        preallocated ? unwrap preallocated : Alloc_Stub(),
        utf8_size + 1  // small sizes fit in a Stub (no dynamic allocation)
    ));

  detect_arrow_words: { ///////////////////////////////////////////////////////

    // !!! Note: The scanner should already know if the word has > or < in
    // it, also we could calculate it during the hash.  But it's not such a
    // huge deal because we only run this the first time a symbol is interned.
    //
    // (If we *have* to do this here, we should be copying as we did it vs.
    // doing the memcpy() as a separate step.)

    assert(Get_Lex_Class(utf8[0]) != LEX_CLASS_NUMBER);  // no leading digit
    for (Offset i = 0; i < utf8_size; ++i) {
        assert(not Is_Lex_Whitespace(utf8[i]));  // spaces/newlines illegal

        if (Is_Continuation_Byte(utf8[i]))
            Clear_Flavor_Flag(SYMBOL, b, ALL_ASCII);

        if (utf8[i] == 0xC2 and utf8[i + 1] == 0xA0)
            panic ("Non-breaking space illegal in WORD!");

        assert(
            utf8[i] != '$'
            and utf8[i] != '@'
            and utf8[i] != '^'
            and utf8[i] != '&'
        );  // sigil characters not legal in symbols either

        if (
            utf8[i] == '/'
            or utf8[i] == '<'
            or utf8[i] == '>'
        ){
            Set_Flavor_Flag(SYMBOL, b, ILLEGAL_IN_ANY_SEQUENCE);
            continue;
        }

        if (utf8[i] == ':') {
            Set_Flavor_Flag(SYMBOL, b, ILLEGAL_IN_CHAIN);
            continue;
        }

        if (utf8[i] == '.') {
            Set_Flavor_Flag(SYMBOL, b, ILLEGAL_IN_TUPLE);
            continue;
        }
    }

} copy_terminate_and_freeze: { ///////////////////////////////////////////////

    // 1. The incoming string isn't always null terminated, e.g. if you are
    //    interning `foo` in `foo: bar + 1` it would be colon-terminated.
    //
    // 2. The UTF-8 Flex can be aliased with AS to become an ANY-STRING? or a
    //    BLOB!.  If it is, then it should not be modified.

    memcpy(Binary_Head(b), utf8, utf8_size);
    Term_Binary_Len(b, utf8_size);  // not always terminated [1]
    Freeze_Flex(b);  // signal immutability to non-WORD! aliasese [2]

} setup_synonyms_and_symbol_id: { ////////////////////////////////////////////

    // 1. Newly interned Symbols will have SYM_0 as the answer to Symbol_Id().
    //    Startup_Symbols() tags the builtin SYM_XXX values from %symbols.r and
    //    %lib-words.r on the terms that it interns after they are returned.
    //    This lets them be used in compiled C switch() cases (e.g. SYM_ANY,
    //    SYM_INTEGER_X, etc.)  But non-builtin words will be left at SYM_0.
    //
    // 2. !!! The system is getting more strict about case-sensitivity, but
    //    it may still be useful to store the SymId in synonyms...but not
    //    give the answer as the SymId of the word unless it's a canon.
    //
    // 3. In addition to a circularly linked list of synonyms via Symbol.link,
    //    Symbol.misc field has another circularly linked list of Stubs called
    //    "Patch" that hold module-level variables with that Symbol as a name.
    //    Upon the initial interning of a Symbol, this list is empty.

    if (not synonym) {
        Tweak_Link_Next_Synonym(b, c_cast(Symbol*, b));  // 1-item circle list
        assert(SECOND_UINT16(&b->info) == SYM_0);  // Startup may assign [1]
    }
    else {  // synonym for existing canon, add to circularly linked list
        Tweak_Link_Next_Synonym(b, Link_Next_Synonym(synonym));
        Tweak_Link_Next_Synonym(synonym, c_cast(Symbol*, b));

        assert(SECOND_UINT16(&b->info) == SYM_0);
        SET_SECOND_UINT16(&b->info, Symbol_Id(synonym));  // same symid [2]
    }

    Tweak_Misc_Hitch(b, b);  // circular list of module vars and bind info [3]

} add_to_symbol_hash_table: { /////////////////////////////////////////////////

    if (deleted_slot) {
        *deleted_slot = cast(Symbol*, b);  // reuse the deleted slot
      #if RUNTIME_CHECKS
        g_symbols.num_deleteds -= 1;  // note slot usage count stays constant
      #endif
    }
    else {
        symbols_by_hash[slot] = cast(Symbol*, b);
        ++g_symbols.num_slots_in_use;
    }

    return cast(Symbol*, b);
}}}


//
//  GC_Kill_Interning: C
//
// Unlink this spelling out of the circularly linked list of synonyms.
// Further, if it happens to be canon, we need to pick one of the remaining
// synonyms as a new canon.
//
// 1. Symbols should not be GC'd while a binding is in progress.
//
// 2. We should only be GC'ing a symbol if all the sea-of-words module
//    variables referring to it are also being freed.
//
// 3. This ripples the collision slots back until nullptr is found, to
//    reduce search times:
//
//  "Knuth gives a nice refinment as Algorithm R6.4 [pp. 533-534] that
//  instead marks the cell as empty rather than deleted, and then finds ways
//  to move table entries back closer to their initial-probe location by
//  moving the hole that was just made until it ends up next to another hole.
//
void GC_Kill_Interning(const Symbol* symbol)
{
    assert(Not_Flavor_Flag(SYMBOL, symbol, HITCH_IS_BIND_STUMP));  // [1]

    const Symbol* synonym = Link_Next_Synonym(symbol);  // maybe same as symbol
    const Symbol* temp = synonym;
    while (Link_Next_Synonym(temp) != symbol)
        temp = Link_Next_Synonym(temp);
    Tweak_Link_Next_Synonym(m_cast(Symbol*, temp), synonym);  // maybe noop

    Stub* stub = m_cast(Symbol*, symbol);  // cut symbol from module vars list
    while (Misc_Hitch(stub) != symbol) {
        assert(Not_Base_Marked(stub));  // no live vars with symbol's name [2]
        stub = Misc_Hitch(stub);
    }
    Tweak_Misc_Hitch(stub, Misc_Hitch(symbol));  // may be no-op

    Length num_slots = Flex_Used(g_symbols.by_hash);
    Symbol** symbols_by_hash = Flex_Head(Symbol*, g_symbols.by_hash);

    Length skip;
    Offset slot = First_Hash_Candidate_Slot(
        &skip,
        Hash_String(symbol),
        num_slots
    );

    while (symbols_by_hash[slot] != symbol) {  // *will* be found in table
        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
    }

    Offset previous_slot = slot;
    while (symbols_by_hash[slot]) {  // ripple collision slots back [3]
        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
        symbols_by_hash[previous_slot] = symbols_by_hash[slot];
    }

    symbols_by_hash[previous_slot] = DELETED_SYMBOL;  // see DELETED_SYMBOL

  #if RUNTIME_CHECKS
    g_symbols.num_deleteds += 1;  // total use same (num_symbols_or_deleteds)
  #endif
}


//
//  Startup_Interning: C
//
// Get the engine ready to do Intern_UTF8_Managed().  We start the hash table
// out at a fixed size.  When collisions occur, it causes a skipping pattern
// that continues until it finds the desired slot.  The method is known as
// linear probing:
//
//   https://en.wikipedia.org/wiki/Linear_probing
//
// It must always be at least as big as the total number of words, in order
// for it to uniquely be able to locate each symbol pointer.  But to reduce
// long probing chains, it should be significantly larger than that.  R3-Alpha
// used a heuristic of 4 times as big as the number of words.
//
void Startup_Interning(void)
{
    g_symbols.num_slots_in_use = 0;
  #if RUNTIME_CHECKS
    g_symbols.num_deleteds = 0;
  #endif

    Length n = Get_Hash_Prime_May_Panic(
        WORD_TABLE_SIZE * 4  // * 4 reduces rehashing
    );

  #if RUNTIME_CHECKS
    if (SPORADICALLY_NON_DETERMINISTIC(2))  // see definition, use rarely!
        n = 1; // force exercise of rehashing logic half the time on startup
  #endif

    ensure(nullptr, g_symbols.by_hash) = Make_Flex(
        FLAG_FLAVOR(CANONTABLE) | FLEX_FLAG_POWER_OF_2,
        Flex,
        n
    );
    Clear_Flex(g_symbols.by_hash);  // all slots start as nullptr
    Set_Flex_Len(g_symbols.by_hash, n);
}


//
//  Startup_Builtin_Symbols: C
//
// Initializes a table for mapping from SYM_XXX => Symbol Flex.  This is used
// by Canon_Symbol(id) and CANON(XXX) to get the Symbol from SymId.
//
// 1. All words that do not have a SYM_XXX get back Cell_Word_Id(w) == SYM_0.
//    Hence CANON(0) is illegal, to avoid `CANON(X) == CANON(Y)` being true
//    when X and Y are different symbols with no SYM_XXX id.
//
// 2. A Symbol Flex stores its SymId in the header's 2nd uint16_t.  Could
//    probably use less than 16 bits, but 8 is insufficient (there are more
//    than 256 SYM_XXX values)
//
void Startup_Builtin_Symbols(
    const Byte* compressed_strings,
    Size compressed_size
){
    Size uncompressed_size;
    const int max = -1;  // trust size in gzip data
    Byte* bytes = Decompress_Alloc_Core(
        &uncompressed_size,
        compressed_strings,
        compressed_size,
        max,
        SYM_GZIP
    );

    assert(Is_Stub_Erased(&g_symbols.builtin_canons[SYM_0]));  // invalid [1]

    Byte* tail = bytes + uncompressed_size;
    Byte* at = bytes;
    for (SymId16 id = 1; id <= MAX_SYM_BUILTIN; ++id) {
        assert(at < tail);

        Size size = *at;  // length prefix byte
        ++at;

        Symbol* canon = &g_symbols.builtin_canons[id];  // not a Symbol*...yet
        Intern_UTF8_Managed_Core(canon, at, size);  // now it is!

        at += size;

        assert(SECOND_UINT16(&canon->info) == 0);
        SET_SECOND_UINT16(&canon->info, id);  // store ID in canon [2]
        assert(u_cast(SymId, id) == unwrap Symbol_Id(canon));
    }

    assert(at == tail);
    UNUSED(tail);

    rebFree(bytes);

    if (0 != strcmp("integer!", String_UTF8(CANON(INTEGER_X))))
        crash (CANON(INTEGER_X));

    if (0 != strcmp("true", String_UTF8(CANON(TRUE))))
        crash (CANON(TRUE));

    if (0 != strcmp("open", String_UTF8(CANON(OPEN))))
        crash (CANON(OPEN));

    if (0 != strcmp("parse-reject", String_UTF8(CANON(PARSE_REJECT))))
        crash (CANON(PARSE_REJECT));
}


//
//  Register_Symbol: C
//
// This is used by extensions to register a symbol under a fixed ID number so
// that it can be used in switch() statements.  It's required that all
// extensions that register the same symbol use the same ID number.
//
// The symbol will be protected from GC, so that it doesn't get garbage
// collected and then come back into existence without an ID number.
//
// There's not really anywhere to put a refcount in the Symbol, so the best
// way to do that counting is to give back an API handle of the WORD! which
// has the symbol in it that the caller has to hang on to.  They pass it back
// in when they want to unregister.
//
RebolValue* Register_Symbol(const char* utf8, SymId16 id16)
{
    assert(id16 > MAX_SYM_BUILTIN);

    const Symbol* symbol = Intern_UTF8_Managed(cb_cast(utf8), strlen(utf8));
    Option(SymId) id = Symbol_Id(symbol);
    if (id) {
        if (not (id16 == u_cast(SymId16, id)))
            panic ("Extensions using conflicting Register_Symbol() IDs");
    }

    const Symbol* synonym = symbol;
    do {
        assert(SECOND_UINT16(&synonym->info) == 0);
        SET_SECOND_UINT16(&m_cast(Symbol*, synonym)->info, id16);
        assert(u_cast(SymId, id16) == unwrap Symbol_Id(synonym));

        synonym = Link_Next_Synonym(synonym);
    } while (synonym != symbol);

    RebolValue* word = Alloc_Value();
    Init_Word(word, symbol);
    return rebUnmanage(word);
}


//
//  Unregister_Symbol: C
//
// Just frees the API handle that was returned from Register_Symbol().
//
// It does not clear out the ID number, because other extensions may have also
// registered the Symbol and depend on it.
//
void Unregister_Symbol(RebolValue* word, SymId16 id16)
{
    assert(id16 != 0);

    assert(Is_Word(word));
    assert(Cell_Word_Id(word) == u_cast(SymIdEnum, id16));  // unnecessary?
    UNUSED(id16);

    rebRelease(word);
}


//
//  Shutdown_Builtin_Symbols: C
//
// The Shutdown_Interning() code checks for g_symbols.by_hash to be empty...
// the necessary removal happens in Diminish_Stub().  (Note that a "dirty"
// shutdown--used in release builds--avoids all these balancing checks!)
//
void Shutdown_Builtin_Symbols(void)
{
    assert(Is_Stub_Erased(&g_symbols.builtin_canons[SYM_0]));

    for (SymId16 id = 1; id <= MAX_SYM_BUILTIN; ++id) {
        Symbol* canon = &g_symbols.builtin_canons[id];
        Diminish_Stub(canon);
    }
}


//
//  Shutdown_Interning: C
//
void Shutdown_Interning(void)
{
  #if RUNTIME_CHECKS
    if (g_symbols.num_slots_in_use - g_symbols.num_deleteds != 0) {
        //
        // !!! There needs to be a more user-friendly output for this,
        // and to detect if it really was an API problem or something else
        // that needs to be paid attention to in the core.  Right now the
        // two scenarios are conflated into this one crash.
        //
        printf(
            "!!! %d leaked canons found in shutdown\n",
            cast(int, g_symbols.num_slots_in_use - g_symbols.num_deleteds)
        );
        printf("!!! LIKELY rebUnmanage() without a rebRelease() in API\n");

        fflush(stdout);

        REBLEN slot;
        for (slot = 0; slot < Flex_Used(g_symbols.by_hash); ++slot) {
            Symbol* symbol = *Flex_At(Symbol*, g_symbols.by_hash, slot);
            if (symbol and symbol != DELETED_SYMBOL)
                crash (symbol);
        }
    }
  #endif

    Free_Unmanaged_Flex(g_symbols.by_hash);
    g_symbols.by_hash = nullptr;
}


#if RUNTIME_CHECKS

//
//  Assert_No_Symbols_Have_Bindinfo: C
//
void Assert_No_Symbols_Have_Bindinfo(void) {
    Length num_slots = Flex_Used(g_symbols.by_hash);
    Symbol** symbols_by_hash = Flex_Head(  // hold on temporarily [1]
        Symbol*,
        g_symbols.by_hash
    );

    Count i;
    for (i = 0; i < num_slots; ++i) {
        if (not symbols_by_hash[i] or symbols_by_hash[i] == DELETED_SYMBOL)
            continue;
        assert(Not_Flavor_Flag(SYMBOL, symbols_by_hash[i], HITCH_IS_BIND_STUMP));
    }
}

#endif
