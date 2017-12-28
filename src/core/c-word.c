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
static REBCNT const Primes[] =
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
//  Get_Hash_Prime: C
//
// Given a size, return a prime number that is larger.
//
REBINT Get_Hash_Prime(REBCNT size)
{
    REBINT n;

    for (n = 0; Primes[n] && size > Primes[n]; n++);

    if (!Primes[n]) return 0;

    return Primes[n];
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
static REBSTR PG_Deleted_Canon;
#define DELETED_CANON &PG_Deleted_Canon


//
//  Expand_Word_Table: C
//
// Expand the hash table part of the word_table by allocating
// the next larger table size and rehashing all the words of
// the current table.  Free the old hash array.
//
static void Expand_Word_Table(void)
{
    // The only full list of canon words available is the old hash table.
    // Hold onto it while creating the new hash table.

    REBCNT old_size = SER_LEN(PG_Canons_By_Hash);
    REBSTR* *old_canons_by_hash = SER_HEAD(REBSER*, PG_Canons_By_Hash);

    REBCNT new_size = Get_Hash_Prime(old_size + 1);
    if (new_size == 0) {
        DECLARE_LOCAL (temp);
        Init_Integer(temp, old_size + 1);
        fail (Error_Size_Limit_Raw(temp));
    }

    assert(SER_WIDE(PG_Canons_By_Hash) == sizeof(REBSTR*));

    REBSER *ser = Make_Series_Core(
        new_size, sizeof(REBSTR*), SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(ser);
    SET_SERIES_LEN(ser, new_size);

    // Rehash all the symbols:

    REBSTR **new_canons_by_hash = SER_HEAD(REBSER*, ser);

    REBCNT n;
    for (n = 0; n < old_size; ++n) {
        REBSTR *canon = old_canons_by_hash[n];

        if (canon == NULL) continue;

        if (canon == DELETED_CANON) { // clean out any deleted canon entries
            --PG_Num_Canon_Slots_In_Use;
        #if !defined(NDEBUG)
            --PG_Num_Canon_Deleteds; // keep track for shutdown assert
        #endif
            continue;
        }

        REBINT hash = Hash_String(canon);
        REBINT skip = (hash & 0x0000FFFF) % new_size;
        if (skip == 0) skip = 1;
        hash = (hash & 0x00FFFF00) % new_size;

        while (new_canons_by_hash[hash] != NULL) {
            hash += skip;
            if (hash >= cast(REBINT, new_size))
                hash -= new_size;
        }
        new_canons_by_hash[hash] = canon;
    }

    Free_Series(PG_Canons_By_Hash);
    PG_Canons_By_Hash = ser;
}


//
//  Intern_UTF8_Managed: C
//
// This will "intern" a UTF-8 string, which is to store only one copy of each
// distinct string value:
//
// https://en.wikipedia.org/wiki/String_interning
//
// The interning is case-sensitive.  But a relationship is set up between
// instances that are just differently upper-or-lower-"cased".  This allows
// those instances to agree on a single "canon" interning that can be used for
// fast comparison between them.
//
// Interned UTF8 strings are stored as series, and are implicitly managed
// by the GC (because they are shared).  Individual synonyms can be GC'd,
// including canon forms--in which case the agreed-upon canon for the
// group will get bumped to one of the other synonyms.
//
REBSTR *Intern_UTF8_Managed(const REBYTE *utf8, size_t size)
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
    REBCNT num_slots = SER_LEN(PG_Canons_By_Hash);
    if (PG_Num_Canon_Slots_In_Use > num_slots / 2) {
        Expand_Word_Table();
        num_slots = SER_LEN(PG_Canons_By_Hash); // got larger
    }

    REBSTR* *canons_by_hash = SER_HEAD(REBSER*, PG_Canons_By_Hash);

    // Calculate the starting hash slot to try--and the amount to skip to by
    // each time a slot is found that is occupied by a non-match.
    //
    REBCNT hash = Hash_UTF8(utf8, size);
    REBCNT skip = (hash & 0x0000FFFF) % num_slots;
    if (skip == 0)
        skip = 1;
    hash = (hash & 0x00FFFF00) % num_slots;

    REBSTR **deleted_slot = NULL;

    // The hash table only indexes the canon form of each spelling.  So when
    // testing a slot to see if it's a match (or a collision that needs to
    // be skipped to try again) the search uses a comparison that is
    // case-insensitive...and returns a value > 0 for a match.
    //
    // However, the result also indicates whether it was an *exact* match, by
    // returning 0 if it is.
    //
    REBSTR* canon;
    while ((canon = canons_by_hash[hash]) != NULL) {
        if (canon == DELETED_CANON) {
            deleted_slot = &canons_by_hash[hash];
            hash += skip;
            if (hash >= num_slots)
                hash -= num_slots;
            continue;
        }

        assert(GET_SER_INFO(canon, STRING_INFO_CANON));

        // Compare_UTF8 returns 0 when the spelling is a case-sensitive match,
        // and is the exact interning to return.
        //
        REBINT cmp = Compare_UTF8(cb_cast(STR_HEAD(canon)), utf8, size);
        if (cmp == 0)
            return canon;

        if (cmp < 0) {
            //
            // Compare_UTF8 returns less than zero when the canon value in the
            // slot isn't the same at all.  Since it's not a match, skip ahead
            // to the next candidate slot--wrapping around if necessary
            //
            hash += skip;
            if (hash >= num_slots)
                hash -= num_slots;
            continue;
        }

        // The > 0 result means that the canon word that was found is an
        // alternate casing ("synonym") for the string we're interning.  The
        // synonyms are attached to the canon form with a circularly linked
        // list.  Walk the list to see if any of the synonyms are a match.
        //
        REBSTR *synonym = LINK(canon).synonym;
        while (synonym != canon) {
            assert(NOT_SER_INFO(synonym, STRING_INFO_CANON));

            // Exact match for a synonym also means no new allocation needed.
            //
            cmp = Compare_UTF8(cb_cast(STR_HEAD(synonym)), utf8, size);
            if (cmp == 0)
                return synonym;

            // Comparison should at least be a synonym, if in this list.
            // Keep checking for an exact match until a cycle is found.
            //
            assert(cmp > 0);
            synonym = LINK(synonym).synonym;
        }

        // If none of the synonyms matched, then this case variation needs
        // to get its own interning, and point to the canon found.

        assert(canon != NULL);
        goto new_interning; // break loop, make a new synonym
    }

    // normal loop fallthrough at canon == NULL - make a new canon form
    assert(canon == NULL);

new_interning: ; // semicolon needed for statement

    // If possible, the allocation should be fit into a REBSER node with no
    // separate allocation.  Because automatically doing this is a new
    // feature, double check with an assert that the behavior matches.
    //
    REBSTR *intern = Make_Series_Core(
        size + 1,
        sizeof(REBYTE),
        SERIES_FLAG_UTF8_STRING | SERIES_FLAG_FIXED_SIZE
    );

#if !defined(NDEBUG)
    if (size + 1 > sizeof(intern->content))
        assert(GET_SER_INFO(intern, SERIES_INFO_HAS_DYNAMIC));
    else
        assert(NOT_SER_INFO(intern, SERIES_INFO_HAS_DYNAMIC));
#endif

    // The incoming string isn't always null terminated, e.g. if you are
    // interning `foo` in `foo: bar + 1` it would be colon-terminated.
    //
    memcpy(BIN_HEAD(intern), utf8, size);
    TERM_BIN_LEN(intern, size);

    if (canon == NULL) {
        //
        // There was no canon symbol found, so this interning will be canon.
        // Add it to the hash table and mark it, reuse deleted slot (if any)
        //
        if (deleted_slot) {
            *deleted_slot = intern; // slot "usage" count stays constant

        #if !defined(NDEBUG)
            --PG_Num_Canon_Deleteds;
        #endif
        }
        else {
            canons_by_hash[hash] = intern;
            ++PG_Num_Canon_Slots_In_Use;
        }

        SET_SER_INFO(intern, STRING_INFO_CANON);

        LINK(intern).synonym = intern; // circularly linked list, empty state

        // Canon symbols don't need to cache a canon pointer to themselves.
        // So instead that slot is reserved for tracking associated information
        // for the canon word, e.g. the current bind index.  Because this
        // may be used by several threads, it would likely have to be an
        // atomic pointer that would "pop out" to a structure, but for now
        // it is just randomized to keep its information in high bits or low
        // bits as a poor-man's demo that there is an infrastructure in place
        // for sharing (start with 2, grow to N based on the functions for
        // 2 being in place)
        //
        MISC(intern).bind_index.high = 0;
        MISC(intern).bind_index.low = 0;

        // leave header.bits as 0 for SYM_0 as answer to VAL_WORD_SYM()
        // Startup_Symbols() tags values from %words.r after the fact.
    }
    else {
        // This is a synonym for an existing canon.  Link it into the synonyms
        // circularly linked list, and direct link the canon form.
        //
        MISC(intern).length = 0; // !!! TBD: codepoint count
        LINK(intern).synonym = LINK(canon).synonym;
        LINK(canon).synonym = intern;

        // If the canon form had a SYM_XXX for quick comparison of %words.r
        // words in C switch statements, the synonym inherits that number.
        //
        assert(RIGHT_16_BITS(intern->header.bits) == 0);
        intern->header.bits |= FLAGUINT16_RIGHT(STR_SYMBOL(canon));
    }

#if !defined(NDEBUG)
    REBUPT sym_canon = cast(REBUPT, STR_SYMBOL(STR_CANON(intern)));
    REBUPT sym = cast(REBUPT, STR_SYMBOL(intern));
    assert(sym == sym_canon);
#endif

    // Created series must be managed, because if they were not there could
    // be no clear contract on the return result--as it wouldn't be possible
    // to know if a shared instance had been managed by someone else or not.
    //
    MANAGE_SERIES(intern);
    assert(LEFT_N_BITS(intern->header.bits, 4) != 0);
    return intern;
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
    REBSER *synonym = LINK(intern).synonym;

    // Note synonym and intern may be the same here.
    //
    REBSER *temp = synonym;
    while (LINK(temp).synonym != intern)
        temp = LINK(temp).synonym;
    LINK(temp).synonym = synonym; // cut intern out of chain (or no-op)

    if (NOT_SER_INFO(intern, STRING_INFO_CANON))
        return; // for non-canon forms, removing from chain is all you need

    assert(MISC(intern).bind_index.high == 0); // shouldn't GC during binds?
    assert(MISC(intern).bind_index.low == 0);

    REBCNT num_slots = SER_LEN(PG_Canons_By_Hash);
    REBSTR* *canons_by_hash = SER_HEAD(REBSER*, PG_Canons_By_Hash);

    REBCNT hash = Hash_String(intern);
    REBCNT skip = (hash & 0x0000FFFF) % num_slots;
    if (skip == 0)
        skip = 1;
    hash = (hash & 0x00FFFF00) % num_slots;

    // We *will* find the canon form in the hash table.
    //
    while (canons_by_hash[hash] != intern) {
        hash += skip;
        if (hash >= num_slots)
            hash -= num_slots;
    }

    if (synonym != intern) {
        //
        // If there was a synonym in the circularly linked list distinct from
        // the canon form, then it gets a promotion to being the canon form.
        // It should hash the same, and be able to take over the hash slot.
        //
    #ifdef SLOW_INTERN_HASH_DOUBLE_CHECK
        assert(hash == Hash_String(synonym));
    #endif
        canons_by_hash[hash] = synonym;
        SET_SER_INFO(synonym, STRING_INFO_CANON);
        MISC(synonym).bind_index.low = 0;
        MISC(synonym).bind_index.high = 0;
    }
    else {
        // This canon form must be removed from the hash table.  Ripple the
        // collision slots back until a NULL is found, to reduce search times.
        //
        REBCNT previous_hash = hash;
        while (canons_by_hash[hash] != NULL) {
            hash += skip;
            if (hash >= num_slots)
                hash -= num_slots;
            canons_by_hash[previous_hash] = canons_by_hash[hash];
        }

        // Signal that the hash slot is "deleted" via a special pointer.
        // See notes on DELETED_SLOT for why the final slot in the collision
        // chain can't just be left NULL:
        //
        // http://stackoverflow.com/a/279812/211160
        //
        canons_by_hash[previous_hash] = DELETED_CANON;

    #if !defined(NDEBUG)
        ++PG_Num_Canon_Deleteds; // total use same (PG_Num_Canons_Or_Deleteds)
    #endif
    }
}


//
//  Compare_Word: C
//
// Compare the names of two words and return the difference.
// Note that words are kept UTF8 encoded.
// Positive result if s > t and negative if s < t.
//
REBINT Compare_Word(const RELVAL *s, const RELVAL *t, REBOOL strict)
{
    const REBYTE *sp = cb_cast(STR_HEAD(VAL_WORD_SPELLING(s)));
    const REBYTE *tp = cb_cast(STR_HEAD(VAL_WORD_SPELLING(t)));

    if (strict)
        return COMPARE_BYTES(sp, tp); // must match byte-for-byte

    if (VAL_WORD_CANON(s) == VAL_WORD_CANON(t))
        return 0; // equivalent canon forms are considered equal

    // They must differ by case....
    return Compare_UTF8(sp, tp, LEN_BYTES(tp)) + 2;
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
    PG_Num_Canon_Slots_In_Use = 0;
#if !defined(NDEBUG)
    PG_Num_Canon_Deleteds = 0;
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

    REBCNT n;
#if defined(NDEBUG)
    n = Get_Hash_Prime(WORD_TABLE_SIZE * 4); // extra reduces rehashing
#else
    n = 1; // forces exercise of rehashing logic in debug build
#endif

    PG_Canons_By_Hash = Make_Series_Core(
        n, sizeof(REBSTR*), SERIES_FLAG_POWER_OF_2
    );
    Clear_Series(PG_Canons_By_Hash); // all slots start at NULL
    SET_SERIES_LEN(PG_Canons_By_Hash, n);
}


//
//  Startup_Symbols: C
//
// By this point in the boot, the canon words have already been interned for
// everything in %words.r.
//
// This goes through the name series for %words.r words and tags them with
// SYM_XXX constants.  This allows the small number to be quickly extracted to
// use with VAL_WORD_SYM() in C switch statements.  These are the only words
// that have fixed symbol numbers--others are only managed and compared
// through their pointers.
//
// It also creates a table for mapping from SYM_XXX => REBSTR series.  This
// is used e.g. by Canon(SYM_XXX) to get the string name for a symbol.
//
void Startup_Symbols(REBARR *words)
{
    PG_Symbol_Canons = Make_Series_Core(
        ARR_LEN(words) + 1, // extra NULL at head for SYM_0
        sizeof(REBSTR*),
        SERIES_FLAG_FIXED_SIZE // can't ever add more SYM_XXX lookups
    );

    // All words that not in %words.r will get back VAL_WORD_SYM(w) == SYM_0
    // Hence, SYM_0 cannot be canonized.  Allowing Canon(SYM_0) to return NULL
    // and try and use that meaningfully is too risky, so it is simply
    // prohibited to canonize SYM_0, and trash the REBSTR* in the [0] slot.
    //
    REBSYM sym = SYM_0;
    TRASH_POINTER_IF_DEBUG(
        *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym))
    );

    RELVAL *word = ARR_HEAD(words);
    for (; NOT_END(word); ++word) {
        REBSTR *canon = VAL_WORD_CANON(word);
        assert(GET_SER_INFO(canon, STRING_INFO_CANON));

        sym = cast(REBSYM, cast(REBCNT, sym) + 1);
        *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym)) = canon;

        // More code was loaded than just the word list, and it might have
        // included alternate-case forms of the %words.r words.  Walk any
        // aliases and make sure they have the header bits too.

        REBSTR *name = canon;
        do {
            // The low 8 bits of the header are reserved for flags, including
            // those common between REBSER nodes and REBVALs.  The high 8 bits
            // are used for the size if the series has no dynamic content,
            // and reserved otherwise.  So the shifted-left-by-8 16 bits of
            // the header are free for the symbol number (could probably use
            // less than 16 bits, but 8 is insufficient, length %words.r > 256)
            //
            assert(RIGHT_16_BITS(name->header.bits) == 0);
            name->header.bits |= FLAGUINT16_RIGHT(sym);
            assert(SAME_SYM_NONZERO(STR_SYMBOL(name), sym));

            name = LINK(name).synonym;
        } while (name != canon); // circularly linked list, stop on a cycle
    }

    *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym)) = NULL; // terminate
    sym = cast(REBSYM, cast(REBCNT, sym) + 1);

    SET_SERIES_LEN(PG_Symbol_Canons, cast(REBCNT, sym));
    assert(SER_LEN(PG_Symbol_Canons) == ARR_LEN(words) + 1);

    // Do some sanity checks.  !!! Fairly critical, is debug-only appropriate?

    if (0 != strcmp("blank!", STR_HEAD(Canon(SYM_BLANK_X))))
        panic (Canon(SYM_BLANK_X));

    if (0 != strcmp("true", STR_HEAD(Canon(SYM_TRUE))))
        panic (Canon(SYM_TRUE));
}


//
//  Shutdown_Symbols: C
//
void Shutdown_Symbols(void)
{
    Free_Series(PG_Symbol_Canons);
}


//
//  Shutdown_Interning: C
//
void Shutdown_Interning(void)
{
    assert(PG_Num_Canon_Slots_In_Use - PG_Num_Canon_Deleteds == 0);
    Free_Series(PG_Canons_By_Hash);
}
