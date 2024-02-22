//
//  File: %c-word.c
//  Summary: "symbol table and word related functions"
//  Section: core
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//   referenced in cells by an integer index.  This was distinct from string
//   series which were variable-length encoded and could be GC'd.  Ren-C
//   unifies this where Symbols are String series which are referenced in
//   cells by pointers and can be GC'd, and all strings use UTF-8 everywhere.
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
//  Get_Hash_Prime_May_Fail: C
//
uint32_t Get_Hash_Prime_May_Fail(uint32_t minimum)
{
    Option(uint32_t) prime = Try_Get_Hash_Prime(minimum);
    if (not prime) {  // larger than hash prime table
        DECLARE_ATOM (temp);
        Init_Integer(temp, minimum);
        fail (Error_Size_Limit_Raw(temp));
    }
    return unwrap(prime);
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
    Length old_num_slots = Series_Used(g_symbols.by_hash);
    Symbol** old_symbols_by_hash = Series_Head(  // hold on temporarily [1]
        Symbol*,
        g_symbols.by_hash
    );

    Length num_slots = Get_Hash_Prime_May_Fail(old_num_slots + 1);
    assert(Series_Wide(g_symbols.by_hash) == sizeof(Symbol*));

    Series* table = Make_Series_Core(
        num_slots, FLAG_FLAVOR(CANONTABLE) | SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(table);
    Set_Series_Len(table, num_slots);

    // Rehash all the symbols:

    Symbol** new_symbols_by_hash = Series_Head(Symbol*, table);

    Offset old_slot;
    for (old_slot = 0; old_slot != old_num_slots; ++old_slot) {
        Symbol* symbol = old_symbols_by_hash[old_slot];
        if (not symbol)
            continue;

        if (symbol == DELETED_SYMBOL) {  // clean out deleted symbol entries
            g_symbols.num_slots_in_use -= 1;
          #if !defined(NDEBUG)
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

    Free_Unmanaged_Series(g_symbols.by_hash);
    g_symbols.by_hash = table;
}


//
//  Intern_UTF8_Managed: C
//
// Makes only one copy of each distinct character string.
//
//   https://en.wikipedia.org/wiki/String_interning
//
// The hashing technique used is called "linear probing":
//
//   https://en.wikipedia.org/wiki/Linear_probing
//
// 1. The result series must be managed, because if they were not there could
//    be no clear contract on the return result--as it wouldn't be possible to
//    know if a shared instance had been managed by someone else or not.
//
// 2. Interning is case-sensitive, but a "synonym" linkage is established
//    between instances that are just differently upper-or-lower-"cased".
//    They agree on one "canon" interning to use for fast case-insensitive
//    compares.  If that canon form is GC'd, the agreed upon canon for the
//    group will change.
//
// 3. Newly interned symbols will have SYM_0 as the answer to Symbol_Id().
//    Startup_Symbols() tags the builtin SYM_XXX values from %symbols.r and
//    %lib-words.r on the terms that it interns after they are returned.
//    This lets them be used in compiled C switch() cases (e.g. SYM_ANY,
//    SYM_INTEGER_X, etc.)  But non-builtin words will be left at SYM_0.
//
//    (Idea to extend SYM_XXX values: https://forum.rebol.info/t/1188)
//
// 4. In addition to a circularly linked list of synonyms via LINK(), the
//    MISC() field of the Symbol is another circularly linked list of stubs
//    holding module-level variables with that symbol as a name.  Upon the
//    initial interning of a symbol, this list is empty.
//
const Symbol* Intern_UTF8_Managed_Core(  // results implicitly managed [1]
    Option(void*) preallocated,  // most calls don't know if allocation needed
    const Byte* utf8,  // case-sensitive [2]
    Size utf8_size
){
    // For the hash search to be guaranteed to terminate, the table must be
    // large enough that we are able to find nullptr if there's a miss.  (It's
    // actually kept larger than that, but to be on the right side of theory,
    // the table is always checked for expansion needs *before* the search.)
    //
    Length num_slots = Series_Used(g_symbols.by_hash);
    if (g_symbols.num_slots_in_use > num_slots / 2) {
        Expand_Word_Table();
        num_slots = Series_Used(g_symbols.by_hash);  // got larger, update
    }

    Symbol** symbols_by_hash = Series_Head(Symbol*, g_symbols.by_hash);

    Length skip;  // how many slots to skip when occupied candidates found
    Offset slot = First_Hash_Candidate_Slot(
        &skip,
        Hash_Scan_UTF8_Caseless_May_Fail(utf8, utf8_size),
        num_slots
    );

    Symbol* synonym = nullptr;
    Symbol** deleted_slot = nullptr;
    Symbol* symbol;
    while ((symbol = symbols_by_hash[slot])) {
        if (symbol == DELETED_SYMBOL) {
            deleted_slot = &symbols_by_hash[slot];
            goto next_candidate_slot;
        }

      blockscope {
        REBINT cmp = Compare_UTF8(String_Head(symbol), utf8, utf8_size);
        if (cmp == 0) {
            assert(not preallocated);
            return symbol;  // was a case-sensitive match
        }
        if (cmp < 0)
            goto next_candidate_slot;  // wasn't an alternate casing

        // The > 0 result means that the canon word that was found is an
        // alternate casing ("synonym") for the string we're interning.  The
        // synonyms are attached to the canon form with a circular list.
        //
        synonym = symbol;  // save for linking into synonyms list
        goto next_candidate_slot;
      }

        goto new_interning;  // no synonym matched, make new synonym for canon

      next_candidate_slot:  // https://en.wikipedia.org/wiki/Linear_probing

        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
    }

  new_interning: {

    Binary* s = cast(Binary*, Make_Series_Into(
        preallocated ? unwrap(preallocated) : Alloc_Stub(),
        utf8_size + 1,  // small sizes fit in a Stub (no dynamic allocation)
        SERIES_MASK_SYMBOL
    ));

    // Cache whether this is an arrow word.
    //
    // !!! Note: The scanner should already know this, and also we could
    // calculate it during the hash.  But it's not such a huge deal because
    // we only run this the first time a symbol is interned.
    //
  blockscope {
    assert(Get_Lex_Class(utf8[0]) != LEX_CLASS_NUMBER);  // no leading digit
    for (Offset i = 0; i < utf8_size; ++i) {
        assert(not IS_LEX_ANY_SPACE(utf8[i]));  // spaces/newlines illegal

        assert(
            utf8[i] != ':'
            and utf8[i] != '$'
            and utf8[i] != '@'
            and utf8[i] != '^'
            and utf8[i] != '&'
        );  // sigil characters not legal in symbols either

        if (
            utf8[i] == '/'
            or utf8[i] == '<'
            or utf8[i] == '>'
        ){
            Set_Subclass_Flag(SYMBOL, s, ILLEGAL_IN_ANY_SEQUENCE);
            continue;
        }

        if (utf8[i] == '.') {
            Set_Subclass_Flag(SYMBOL, s, ILLEGAL_IN_ANY_TUPLE);
            continue;
        }
    }
  }

    // The incoming string isn't always null terminated, e.g. if you are
    // interning `foo` in `foo: bar + 1` it would be colon-terminated.
    //
    memcpy(Binary_Head(s), utf8, utf8_size);
    Term_Binary_Len(s, utf8_size);

    // The UTF-8 series can be aliased with AS to become an ANY-STRING? or a
    // BINARY!.  If it is, then it should not be modified.
    //
    Freeze_Series(s);

    if (not synonym) {
        LINK(Synonym, s) = c_cast(Symbol*, s);  // 1-item circular list
        assert(SECOND_UINT16(&s->info) == SYM_0);  // Startup may assign [3]
    }
    else {
        // This is a synonym for an existing canon.  Link it into the synonyms
        // circularly linked list, and direct link the canon form.
        //
        LINK(Synonym, s) = LINK(Synonym, synonym);
        LINK(Synonym, synonym) = c_cast(Symbol*, s);

        // If the canon form had a SYM_XXX for quick comparison of %words.r
        // words in C switch statements, the synonym inherits that number.
        //
        assert(SECOND_UINT16(&s->info) == SYM_0);
        SET_SECOND_UINT16(&s->info, Symbol_Id(synonym));
    }

    MISC(Hitch, s) = s;  // circular list of module vars and bind info [4]

    if (deleted_slot) {
        *deleted_slot = cast(Symbol*, s);  // reuse the deleted slot
      #if !defined(NDEBUG)
        g_symbols.num_deleteds -= 1;  // note slot usage count stays constant
      #endif
    }
    else {
        symbols_by_hash[slot] = cast(Symbol*, s);
        ++g_symbols.num_slots_in_use;
    }

    return cast(Symbol*, s);
  }
}


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
    assert(Not_Subclass_Flag(SYMBOL, symbol, MISC_IS_BINDINFO));  // [1]

    const Symbol* synonym = LINK(Synonym, symbol);  // may be same as symbol
    const Symbol* temp = synonym;
    while (LINK(Synonym, temp) != symbol)
        temp = LINK(Synonym, temp);
    LINK(Synonym, temp) = synonym;  // cut symbol out of synonyms (maybe no-op)

    const Stub* patch = symbol;  // cut symbol out of module vars list
    while (node_MISC(Hitch, patch) != symbol) {
        assert(Not_Node_Marked(patch));  // no live vars with symbol's name [2]
        patch = cast(Stub*, node_MISC(Hitch, patch));
    }
    node_MISC(Hitch, patch) = node_MISC(Hitch, symbol);  // may be no-op

    Length num_slots = Series_Used(g_symbols.by_hash);
    Symbol** symbols_by_hash = Series_Head(Symbol*, g_symbols.by_hash);

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

  #if !defined(NDEBUG)
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
  #if !defined(NDEBUG)
    g_symbols.num_deleteds = 0;
  #endif

    Length n;
  #if defined(NDEBUG)
    n = Get_Hash_Prime_May_Fail(WORD_TABLE_SIZE * 4);  // *4 reduces rehashing
  #else
    n = 1; // forces exercise of rehashing logic in debug build
  #endif

    ensure(nullptr, g_symbols.by_hash) = Make_Series_Core(
        n, FLAG_FLAVOR(CANONTABLE) | SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(g_symbols.by_hash);  // all slots start as nullptr
    Set_Series_Len(g_symbols.by_hash, n);
}


//
//  Startup_Symbols: C
//
// Initializes a table for mapping from SYM_XXX => Symbol series.  This is used
// by Canon_Symbol(id) and Canon(XXX) to get the symbol from id.
//
// 1. All words that do not have a SYM_XXX get back Cell_Word_Id(w) == SYM_0.
//    Hence Canon(0) is illegal, to avoid `Canon(X) == Canon(Y)` being true
//    when X and Y are different symbols with no SYM_XXX id.  We turn it into
//    a freed series, so Detect_Rebol_Pointer() doesn't confuse the zeroed
//    memory with an empty UTF-8 string.
//
// 2. Symbol series store symbol number in the header's 2nd uint16_t.  Could
//    probably use less than 16 bits, but 8 is insufficient (there are more
//    than 256 SYM_XXX values)
//
void Startup_Symbols(void)
{
    size_t uncompressed_size;
    const int max = -1;  // trust size in gzip data
    Byte* bytes = Decompress_Alloc_Core(
        &uncompressed_size,
        Symbol_Strings_Compressed,
        Symbol_Strings_Compressed_Size,
        max,
        SYM_GZIP
    );

    assert(FIRST_BYTE(&g_symbols.builtin_canons[0]) == 0);  // no Canon(0) [1]
    FIRST_BYTE(&g_symbols.builtin_canons[0]) = FREE_POOLUNIT_BYTE;

    SymId id = cast(SymId, 1);  // SymId for debug watch

    Byte* tail = bytes + uncompressed_size;
    Byte* at = bytes;
    while (at != tail) {
        assert(at < tail);

        size_t size = *at;  // length prefix byte
        ++at;

        Symbol* canon = &g_symbols.builtin_canons[id];  // not a Symbol*...yet
        Intern_UTF8_Managed_Core(canon, at, size);  // now it is!
        at += size;

        assert(SECOND_UINT16(&canon->info) == 0);
        SET_SECOND_UINT16(&canon->info, id);  // store ID in canon [2]
        assert(id == unwrap(Symbol_Id(canon)));

        id = cast(SymId, cast(uint16_t, id) + 1);
    }

    rebFree(bytes);

    assert(id == ALL_SYMS_MAX);  // includes the + 1 for REB_0 slot

    if (0 != strcmp("blank!", String_UTF8(Canon(BLANK_X))))
        panic (Canon(BLANK_X));

    if (0 != strcmp("true", String_UTF8(Canon(TRUE))))
        panic (Canon(TRUE));

    if (0 != strcmp("open", String_UTF8(Canon(OPEN))))
        panic (Canon(OPEN));

    if (0 != strcmp("parse-reject", String_UTF8(Canon(PARSE_REJECT))))
        panic (Canon(PARSE_REJECT));
}


//
//  Shutdown_Symbols: C
//
// The Shutdown_Interning() code checks for g_symbols.by_hash to be empty...
// the necessary removal happens in Decay_Series().  (Note that a "dirty"
// shutdown--used in release builds--avoids all these balancing checks!)
//
void Shutdown_Symbols(void)
{
    assert(Is_Node_Free(&g_symbols.builtin_canons[SYM_0]));
    FIRST_BYTE(&g_symbols.builtin_canons[0]) = 0;  // pre-boot state

    for (uint16_t i = 1; i < ALL_SYMS_MAX; ++i) {
        Symbol* canon = &g_symbols.builtin_canons[i];
        Decay_Series(canon);
    }
}


//
//  Shutdown_Interning: C
//
void Shutdown_Interning(void)
{
  #if !defined(NDEBUG)
    if (g_symbols.num_slots_in_use - g_symbols.num_deleteds != 0) {
        //
        // !!! There needs to be a more user-friendly output for this,
        // and to detect if it really was an API problem or something else
        // that needs to be paid attention to in the core.  Right now the
        // two scenarios are conflated into this one panic.
        //
        printf(
            "!!! %d leaked canons found in shutdown\n",
            cast(int, g_symbols.num_slots_in_use - g_symbols.num_deleteds)
        );
        printf("!!! LIKELY rebUnmanage() without a rebRelease() in API\n");

        fflush(stdout);

        REBLEN slot;
        for (slot = 0; slot < Series_Used(g_symbols.by_hash); ++slot) {
            Symbol* symbol = *Series_At(Symbol*, g_symbols.by_hash, slot);
            if (symbol and symbol != DELETED_SYMBOL)
                panic (symbol);
        }
    }
  #endif

    Free_Unmanaged_Series(g_symbols.by_hash);
    g_symbols.by_hash = nullptr;
}
