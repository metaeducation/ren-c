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
// In R3-Alpha, words were not garbage collected, and their UTF-8 data was
// kept in a separate table from the REBSERs.  In Ren-C, words use REBSERs,
// and are merely *indexed* by hashes of their canon forms via an external
// table.  This table grows and shrinks as canons are added and removed.
//

#include "sys-core.h"

#define WORD_TABLE_SIZE 1024  // initial size in words


//
// Prime numbers used for hash table sizes. Divide by 2 for
// number of words that can be held in the symbol table.
//
static REBLEN const Primes[] =
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
// see https://primes.utm.edu/lists/2small/0bit.html
};


//
//  Try_Get_Hash_Prime: C
//
// Given a value, return a prime number that is larger or equal.
//
REBINT Try_Get_Hash_Prime(REBLEN minimum)
{
    REBINT n = 0;
    while (minimum > Primes[n]) {
        ++n;
        if (Primes[n] == 0)
            return 0;
    }

    return Primes[n];
}


//
//  Get_Hash_Prime_May_Fail: C
//
REBINT Get_Hash_Prime_May_Fail(REBLEN minimum)
{
    REBINT prime = Try_Get_Hash_Prime(minimum);
    if (prime == 0) {  // larger than hash prime table
        DECLARE_LOCAL (temp);
        Init_Integer(temp, minimum);
        fail (Error_Size_Limit_Raw(temp));
    }
    return prime;
}


// Removals from linear probing lists can be complex, because the same
// overflow slot may be visited through different initial hashes:
//
// http://stackoverflow.com/a/279812/211160
//
// Since it's not enough to simply NULL out the spot when an interned string
// is GC'd, a special pointer signaling "deletedness" is used.  It does not
// cause a linear probe to terminate, but it is reused on insertions.
//
#define DELETED_SYMBOL &PG_Deleted_Symbol


//
//  Expand_Word_Table: C
//
// Expand the hash table part of the word_table by allocating
// the next larger table size and rehashing all the words of
// the current table.  Free the old hash array.
//
static void Expand_Word_Table(void)
{
    // The only full list of symbol words available is the old hash table.
    // Hold onto it while creating the new hash table.

    REBLEN old_num_slots = SER_USED(PG_Symbols_By_Hash);
    REBSTR* *old_symbols_by_hash = SER_HEAD(REBSTR*, PG_Symbols_By_Hash);

    REBLEN num_slots = Get_Hash_Prime_May_Fail(old_num_slots + 1);
    assert(SER_WIDE(PG_Symbols_By_Hash) == sizeof(REBSTR*));

    REBSER *ser = Make_Series(
        num_slots, FLAG_FLAVOR(CANONTABLE) | SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(ser);
    SET_SERIES_LEN(ser, num_slots);

    // Rehash all the symbols:

    REBSTR **new_symbols_by_hash = SER_HEAD(REBSTR*, ser);

    REBLEN old_slot;
    for (old_slot = 0; old_slot != old_num_slots; ++old_slot) {
        REBSTR *symbol = old_symbols_by_hash[old_slot];
        if (not symbol)
            continue;

        if (symbol == DELETED_SYMBOL) {  // clean out deleted symbol entries
            --PG_Num_Symbol_Slots_In_Use;
          #if !defined(NDEBUG)
            --PG_Num_Symbol_Deleteds;  // keep track for shutdown assert
          #endif
            continue;
        }

        REBLEN skip;
        REBLEN slot = First_Hash_Candidate_Slot(
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

    Free_Unmanaged_Series(PG_Symbols_By_Hash);
    PG_Symbols_By_Hash = ser;
}


//
//  Intern_UTF8_Managed: C
//
// Makes only one copy of each distinct character string:
//
// https://en.wikipedia.org/wiki/String_interning
//
// Interned UTF8 strings are stored as series, and are implicitly managed
// by the GC (because they are shared).
//
// Interning is case-sensitive, but a "synonym" linkage is established between
// instances that are just differently upper-or-lower-"cased".  They agree on
// one "canon" interning to use for fast case-insensitive compares.  If that
// canon form is GC'd, the agreed upon canon for the group will change.
//
const REBSYM *Intern_UTF8_Managed(const REBYTE *utf8, size_t size)
{
    // The hashing technique used is called "linear probing":
    //
    // https://en.wikipedia.org/wiki/Linear_probing
    //
    // For the hash search to be guaranteed to terminate, the table must be
    // large enough that we are able to find a NULL if there's a miss.  (It's
    // actually kept larger than that, but to be on the right side of theory,
    // the table is always checked for expansion needs *before* the search.)
    //
    REBLEN num_slots = SER_USED(PG_Symbols_By_Hash);
    if (PG_Num_Symbol_Slots_In_Use > num_slots / 2) {
        Expand_Word_Table();
        num_slots = SER_USED(PG_Symbols_By_Hash);  // got larger
    }

    REBSYM* *symbols_by_hash = SER_HEAD(REBSYM*, PG_Symbols_By_Hash);

    REBLEN skip; // how many slots to skip when occupied candidates found
    REBLEN slot = First_Hash_Candidate_Slot(
        &skip,
        Hash_Scan_UTF8_Caseless_May_Fail(utf8, size),
        num_slots
    );

    // The hash table only indexes the canon form of each spelling.  So when
    // testing a slot to see if it's a match (or a collision that needs to
    // be skipped to try again) the search uses a comparison that is
    // case-insensitive...but reports if synonyms via > 0 results.
    //
    REBSYM *synonym = nullptr;
    REBSYM **deleted_slot = nullptr;
    REBSYM* symbol;
    while ((symbol = symbols_by_hash[slot])) {
        if (symbol == DELETED_SYMBOL) {
            deleted_slot = &symbols_by_hash[slot];
            goto next_candidate_slot;
        }

      blockscope {
        REBINT cmp = Compare_UTF8(STR_HEAD(symbol), utf8, size);
        if (cmp == 0)
            return symbol;  // was a case-sensitive match
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

    REBBIN *s = BIN(Make_Series(
        size + 1,  // if small, fits in a REBSER node (w/no data allocation)
        FLAG_FLAVOR(SYMBOL) | SERIES_FLAG_FIXED_SIZE
    ));

    // Cache whether this is an arrow word.
    //
    // !!! Note: The scanner should already know this, and also we could
    // calculate it during the hash.
    //
  blockscope {
    for (REBLEN i = 0; i < size; ++i) {
        if (utf8[i] == '<' or utf8[i] == '>') {
            SET_SUBCLASS_FLAG(SYMBOL, s, ARROW);
            break;
        }
    }
  }

    // The incoming string isn't always null terminated, e.g. if you are
    // interning `foo` in `foo: bar + 1` it would be colon-terminated.
    //
    memcpy(BIN_HEAD(s), utf8, size);
    TERM_BIN_LEN(s, size);

    // The UTF-8 series can be aliased with AS to become an ANY-STRING! or a
    // BINARY!.  If it is, then it should not be modified.
    //
    Freeze_Series(s);

    if (not synonym) {
        mutable_LINK(Synonym, s) = SYM(s);  // 1-item in circular list

        // leave header.bits as 0 for SYM_0 as answer to VAL_WORD_ID()
        // Startup_Lib() tags values from %words.r after the fact.
        //
        // Words that aren't in the bootup %words.r list don't have integer
        // IDs defined that can be used in compiled C switch() cases (e.g.
        // SYM_ANY, SYM_INTEGER_X, etc.)  So if we didn't find a pre-existing
        // synonym, and none is added, it will remain at 0.
        //
        // !!! It is proposed that a pre-published dictionary of small words
        // could be agreed on, and then extensions using those words could
        // request the numbers for those words.  Inconsistent requests that
        // didn't follow the published list could cause an error.  This would
        // give more integer values without more strings in the core.
        //
        assert(SECOND_UINT16(s->info) == 0);
    }
    else {
        // This is a synonym for an existing canon.  Link it into the synonyms
        // circularly linked list, and direct link the canon form.
        //
        mutable_LINK(Synonym, s) = LINK(Synonym, synonym);
        mutable_LINK(Synonym, synonym) = SYM(s);

        // If the canon form had a SYM_XXX for quick comparison of %words.r
        // words in C switch statements, the synonym inherits that number.
        //
        assert(SECOND_UINT16(s->info) == 0);
        SET_SECOND_UINT16(s->info, ID_OF_SYMBOL(synonym));
    }

    // Symbols use their MISC() as a linked list of binding information.  The
    // list is circular, so that the symbol can be found from any element.
    //
    // !!! This is expected to grow into a list that will include statically
    // the list of declared variables in things like `user` and `lib`.
    //
    mutable_MISC(Hitch, s) = s;

    if (deleted_slot) {
        *deleted_slot = SYM(s);  // reuse the deleted slot
      #if !defined(NDEBUG)
        --PG_Num_Symbol_Deleteds;  // note slot usage count stays constant
      #endif
    }
    else {
        symbols_by_hash[slot] = SYM(s);
        ++PG_Num_Symbol_Slots_In_Use;
    }

    // Created series must be managed, because if they were not there could
    // be no clear contract on the return result--as it wouldn't be possible
    // to know if a shared instance had been managed by someone else or not.
    //
    return SYM(Manage_Series(s));
  }
}


//
//  Intern_Any_String_Managed: C
//
// The main use of interning ANY-STRING! is FILE! for ARRAY_FLAG_FILE_LINE.
// It's important to make a copy, because you would not want the change of
// `file` to affect the filename references in already loaded sources:
//
//     file: copy %test
//     x: transcode/file data1 file
//     append file "-2"  ; shouldn't change FILE OF X answer
//     y: transcode/file data2 file
//
// So mutable series shouldn't be used directly.  Reusing the string interning
// mechanics also cuts down on storage of redundant data (though it needs to
// allow spaces).
//
// !!! With UTF-8 Everywhere, could locked strings be used here?  Should all
// locked strings become interned, and forward pointers to the old series in
// the background to the interned version?
//
const REBSTR *Intern_Any_String_Managed(const RELVAL *v) {
    REBSIZ utf8_size;
    REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&utf8_size, v);
    return Intern_UTF8_Managed(utf8, utf8_size);
}


//
//  GC_Kill_Interning: C
//
// Unlink this spelling out of the circularly linked list of synonyms.
// Further, if it happens to be canon, we need to re-point everything in the
// chain to a new entry.  Choose the synonym as a new canon if so.
//
void GC_Kill_Interning(REBSTR *intern)
{
    REBSYM *synonym = LINK(Synonym, intern);

    // Note synonym and intern may be the same here.
    //
    REBSYM *temp = synonym;
    while (LINK(Synonym, temp) != intern)
        temp = LINK(Synonym, temp);
    mutable_LINK(Synonym, temp) = synonym;  // cut the intern out (or no-op)

    // We should only be GC'ing a symbol if all the sea-of-words module
    // variables referring to it are also being freed.  Make sure that is
    // the case, and remove from the circularly linked list.
    //
    REBSER *patch = intern;
    while (SER(node_MISC(Hitch, patch)) != intern) {
        assert(NOT_SERIES_FLAG(patch, MARKED));
        patch = SER(node_MISC(Hitch, patch));
    }
    node_MISC(Hitch, patch) = node_MISC(Hitch, intern);  // may be no-op

    REBLEN num_slots = SER_USED(PG_Symbols_By_Hash);
    REBSTR* *symbols_by_hash = SER_HEAD(REBSTR*, PG_Symbols_By_Hash);

    REBLEN skip;
    REBLEN slot = First_Hash_Candidate_Slot(
        &skip,
        Hash_String(intern),
        num_slots
    );

    // We *will* find the canon form in the hash table.
    //
    while (symbols_by_hash[slot] != intern) {
        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
    }

    // This canon form must be removed from the hash table.  Ripple the
    // collision slots back until a NULL is found, to reduce search times.
    //
    REBLEN previous_slot = slot;
    while (symbols_by_hash[slot]) {
        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
        symbols_by_hash[previous_slot] = symbols_by_hash[slot];
    }

    // Signal that the hash slot is "deleted" via a special pointer.
    // See notes on DELETED_SLOT for why the final slot in the collision
    // chain can't just be left NULL:
    //
    // http://stackoverflow.com/a/279812/211160
    //
    symbols_by_hash[previous_slot] = DELETED_SYMBOL;

  #if !defined(NDEBUG)
    ++PG_Num_Symbol_Deleteds;  // total use same (PG_Num_Symbols_Or_Deleteds)
  #endif
}


//
//  Startup_Interning: C
//
// Get the engine ready to do Intern_UTF8_Managed(), which is required to
// get REBSTR* pointers generated during a scan of ANY-WORD!s.  Words of the
// same spelling currently look up and share the same REBSTR*, this process
// is referred to as "string interning":
//
// https://en.wikipedia.org/wiki/String_interning
//
void Startup_Interning(void)
{
    PG_Num_Symbol_Slots_In_Use = 0;
  #if !defined(NDEBUG)
    PG_Num_Symbol_Deleteds = 0;
  #endif

    // Start hash table out at a fixed size.  When collisions occur, it
    // causes a skipping pattern that continues until it finds the desired
    // slot.  The method is known as linear probing:
    //
    // https://en.wikipedia.org/wiki/Linear_probing
    //
    // It must always be at least as big as the total number of words, in order
    // for it to uniquely be able to locate each symbol pointer.  But to
    // reduce long probing chains, it should be significantly larger than that.
    // R3-Alpha used a heuristic of 4 times as big as the number of words.

    REBLEN n;
  #if defined(NDEBUG)
    n = Get_Hash_Prime_May_Fail(WORD_TABLE_SIZE * 4);  // *4 reduces rehashing
  #else
    n = 1; // forces exercise of rehashing logic in debug build
  #endif

    PG_Symbols_By_Hash = Make_Series(
        n, FLAG_FLAVOR(CANONTABLE) | SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(PG_Symbols_By_Hash);  // all slots start as nullptr
    SET_SERIES_LEN(PG_Symbols_By_Hash, n);
}


//
//  Startup_Symbols: C
//
// Initializes a table for mapping from SYM_XXX => REBSTR series.  This
// is used by Canon_Symbol(id) and Canon(XXX) to get the symbol from id.
//
void Startup_Symbols(void)
{
    size_t uncompressed_size;
    const int max = -1;  // trust size in gzip data
    REBYTE *bytes = Decompress_Alloc_Core(
        &uncompressed_size,
        Symbol_Strings_Compressed,
        Symbol_Strings_Compressed_Size,
        max,
        SYM_GZIP
    );

    // All words that do not have a SYM_XXX get back VAL_WORD_ID(w) == SYM_0
    // Hence Canon(0) is illegal, to avoid `Canon(X) == Canon(Y)` being
    // true when X and Y are different symbols with no SYM_XXX id.
    //
    TRASH_POINTER_IF_DEBUG(PG_Symbol_Canons[SYM_0]);

    SYMID id = cast(SYMID, cast(REBLEN, SYM_0 + 1));  // SYMID for debug watch

    // We assume no symbols will be larger than 256 characters, so instead
    // of delimiting them in the data we length-prefix them with a byte.
    //
    REBYTE *tail = bytes + uncompressed_size;
    REBYTE *at = bytes;
    while (at != tail) {
        assert(at < tail);

        size_t size = *at;  // length prefix byte
        ++at;

        REBSYM *canon = m_cast(REBSYM*, Intern_UTF8_Managed(at, size));
        at += size;

        // Symbol series store symbol number in the header's 2nd uint16_t.
        // Could probably use less than 16 bits, but 8 is insufficient (there
        // are more than 256 SYM_XXX values)
        //
        assert(SECOND_UINT16(canon->info) == 0);
        SET_SECOND_UINT16(canon->info, id);
        assert(Same_Nonzero_Symid(ID_OF_SYMBOL(canon), id));

        PG_Symbol_Canons[id] = canon;
        id = cast(SYMID, cast(REBLEN, id) + 1);
    }

    rebFree(bytes);

    assert(id == ALL_SYMS_MAX);  // includes the + 1 for REB_0 slot

    if (0 != strcmp("blank!", STR_UTF8(Canon(BLANK_X))))
        panic (Canon(BLANK_X));

    if (0 != strcmp("true", STR_UTF8(Canon(TRUE))))
        panic (Canon(TRUE));

    if (0 != strcmp("open", STR_UTF8(Canon(OPEN))))
        panic (Canon(OPEN));

    if (0 != strcmp("parse-reject", STR_UTF8(Canon(PARSE_REJECT))))
        panic (Canon(PARSE_REJECT));

    PG_Bar_Canon = Canon(BAR);
    PG_Bar_Bar_Canon = Canon(_B_B);
    PG_Slash_1_Canon = Canon(_SLASH_1_);
    PG_Dot_1_Canon = Canon(_DOT_1_);
}


//
//  Shutdown_Symbols: C
//
void Shutdown_Symbols(void)
{
    PG_Slash_1_Canon = nullptr;
    PG_Dot_1_Canon = nullptr;
    PG_Trash_Canon = nullptr;
}


//
//  Shutdown_Interning: C
//
void Shutdown_Interning(void)
{
  #if !defined(NDEBUG)
    if (PG_Num_Symbol_Slots_In_Use - PG_Num_Symbol_Deleteds != 0) {
        //
        // !!! There needs to be a more user-friendly output for this,
        // and to detect if it really was an API problem or something else
        // that needs to be paid attention to in the core.  Right now the
        // two scenarios are conflated into this one panic.
        //
        printf(
            "!!! %d leaked canons found in shutdown\n",
            cast(int, PG_Num_Symbol_Slots_In_Use - PG_Num_Symbol_Deleteds)
        );
        printf("!!! LIKELY rebUnmanage() without a rebRelease() in API\n");

        fflush(stdout);

        REBLEN slot;
        for (slot = 0; slot < SER_USED(PG_Symbols_By_Hash); ++slot) {
            REBSTR *symbol = *SER_AT(REBSTR*, PG_Symbols_By_Hash, slot);
            if (symbol and symbol != DELETED_SYMBOL)
                panic (symbol);
        }
    }
  #endif

    Free_Unmanaged_Series(PG_Symbols_By_Hash);
}
