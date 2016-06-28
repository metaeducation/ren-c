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
// Copyright 2012-2016 Rebol Open Source Contributors
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
// Word table is a block composed of symbols, each of which contain
// a canon word number, alias word number (if it exists), and an
// index that refers to the string for the text itself.
//
// The canon number for a word is unique and is used to compare
// words. The word table is independent of context frames and
// words are never garbage collected.
//
// The alias is used mainly for upper and lower case equality,
// but can also be used to create ALIASes.
//
// The word strings are stored as a single large string series.
// NEVER CACHE A WORD NAME POINTER if new words may be added (e.g.
// LOAD), because the series may get moved in memory.
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
        REBVAL temp;
        SET_INTEGER(&temp, old_size + 1);
        fail (Error(RE_SIZE_LIMIT, &temp));
    }

    assert(SER_WIDE(PG_Canons_By_Hash) == sizeof(REBSTR*));

    REBSER *ser = Make_Series(new_size, sizeof(REBSTR*), MKS_POWER_OF_2);
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

        REBINT hash = Hash_Word(STR_HEAD(canon), LEN_BYTES(STR_HEAD(canon)));
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
REBSTR *Intern_UTF8_Managed(const REBYTE *utf8, REBCNT len)
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
    REBCNT size = SER_LEN(PG_Canons_By_Hash);
    if (PG_Num_Canon_Slots_In_Use > size / 2) {
        Expand_Word_Table();
        size = SER_LEN(PG_Canons_By_Hash); // got larger
    }

    REBSTR* *canons_by_hash = SER_HEAD(REBSER*, PG_Canons_By_Hash);

    // Calculate the starting hash slot to try--and the amount to skip to by
    // each time a slot is found that is occupied by a non-match.
    //
    REBCNT hash = Hash_Word(utf8, len);
    REBCNT skip = (hash & 0x0000FFFF) % size;
    if (skip == 0) skip = 1;
    hash = (hash & 0x00FFFF00) % size;

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
            if (hash >= size) hash -= size;
            continue;
        }

        assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

        // Compare_UTF8 returns 0 when the spelling is a case-sensitive match,
        // and is the exact interning to return.
        //
        REBINT cmp = Compare_UTF8(STR_HEAD(canon), utf8, len);
        if (cmp == 0) return canon;

        if (cmp < 0) {
            //
            // Compare_UTF8 returns less than zero when the canon value in the
            // slot isn't the same at all.  Since it's not a match, skip ahead
            // to the next candidate slot--wrapping around if necessary
            //
            hash += skip;
            if (hash >= size) hash -= size;
            continue;
        }

        // The > 0 result means that the canon word that was found is an
        // alternate casing ("synonym") for the string we're interning.  The
        // synonyms are attached to the canon form with a circularly linked
        // list.  Walk the list to see if any of the synonyms are a match.
        //
        REBSTR *synonym = canon->link.synonym;
        while (synonym != canon) {
            assert(synonym->misc.canon == canon);
            assert(!GET_SER_FLAG(synonym, STRING_FLAG_CANON));

            // Exact match for a synonym also means no new allocation needed.
            //
            cmp = Compare_UTF8(STR_HEAD(synonym), utf8, len);
            if (cmp == 0) return synonym;

            // Comparison should at least be a synonym, if in this list.
            // Keep checking for an exact match until a cycle is found.
            //
            assert(cmp > 0);
            synonym = synonym->link.synonym;
        }

        // If none of the synonyms matched, then this case variation needs
        // to get its own interning, and point to the canon found.

        /*assert(canon != NULL);*/
        goto new_interning; // break loop, make a new synonym
    }

    // normal loop fallthrough at canon == NULL - make a new canon form
    /*assert(canon == NULL)*/

new_interning:

    // If possible, try to fit the data into the REBSER node without doing a
    // separate dynamic allocation.
    //
    REBSTR *intern;
    if (len + 1 > sizeof(intern->content)) {
        intern = Make_Series(len + 1, sizeof(REBYTE), MKS_NONE);
        SET_SERIES_LEN(intern, len);
    }
    else {
        intern = Make_Series(
            1, // !!! no length stored--size measured by LEN_BYTES(data)
            sizeof(REBYTE),
            MKS_NO_DYNAMIC // don't alloc (or free) any data, trust us
        );
    }
    memcpy(BIN_HEAD(intern), utf8, len);

    // The incoming string isn't always null terminated, e.g. if you are
    // interning `foo` in `foo: bar + 1` it would be colon-terminated.
    //
    BIN_HEAD(intern)[len] = '\0';

    SET_SER_FLAGS(intern, SERIES_FLAG_STRING | SERIES_FLAG_FIXED_SIZE);

    assert(intern->header.bits == 0); // SYM_XXX (if any) winds up in high bits

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

        SET_SER_FLAG(intern, STRING_FLAG_CANON);

        intern->link.synonym = intern; // circularly linked list, empty state

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
        intern->misc.bind_index.high = 0;
        intern->misc.bind_index.low = 0;

        // leave header.bits as 0 for SYM_0 as answer to VAL_WORD_SYM()
        // Init_Symbols() tags values from %words.r after the fact.
    }
    else {
        // This is a synonym for an existing canon.  Link it into the synonyms
        // circularly linked list, and direct link the canon form.
        //
        intern->misc.canon = canon;
        intern->link.synonym = canon->link.synonym;
        canon->link.synonym = intern;

        // If the canon form had a SYM_XXX for quick comparison of %words.r
        // words in C switch statements, the synonym inherits that number.
        //
        intern->header.bits |= ((canon->header.bits >> 16) << 16);
    }

    // Created series must be managed, because if they were not there could
    // be no clear contract on the return result--as it wouldn't be possible
    // to know if a shared instance had been managed by someone else or not.
    //
    MANAGE_SERIES(intern);
    return intern;
}


//
//  GC_Kill_Interning: C
//
void GC_Kill_Interning(REBSTR *intern)
{
    REBSER *synonym = intern->link.synonym;

    // We need to unlink this spelling out of the circularly linked list of
    // synonyms.  Further, if it happens to be canon, we need to re-point
    // everything in the chain to a new entry.  Choose the synonym if so.
    // (Note synonym and intern may be the same here.)
    //
    REBSER *temp = synonym;
    while (temp->link.synonym != intern) {
        if (GET_SER_FLAG(intern, STRING_FLAG_CANON))
            temp->misc.canon = synonym;
        temp = temp->link.synonym;
    }
    temp->link.synonym = synonym; // cut intern out of chain (or no-op)

    if (!GET_SER_FLAG(intern, STRING_FLAG_CANON))
        return; // for non-canon forms, removing from chain is all you need

    assert(intern->misc.bind_index.high == 0); // shouldn't GC during binds?
    assert(intern->misc.bind_index.low == 0);

    REBCNT size = SER_LEN(PG_Canons_By_Hash);
    REBSTR* *canons_by_hash = SER_HEAD(REBSER*, PG_Canons_By_Hash);
    assert(canons_by_hash != NULL);

    REBCNT len = LEN_BYTES(STR_HEAD(intern));
    REBCNT hash = Hash_Word(STR_HEAD(intern), len);
    REBCNT skip = (hash & 0x0000FFFF) % size;
    if (skip == 0) skip = 1;
    hash = (hash & 0x00FFFF00) % size;

    // We *will* find the canon form in the hash table.
    //
    while (canons_by_hash[hash] != intern) {
        hash += skip;
        if (hash >= size) hash -= size;
    }

    if (synonym != intern) {
        //
        // If there was a synonym in the circularly linked list distinct from
        // the canon form, then it gets a promotion to being the canon form.
        // It should hash the same, and be able to take over the hash slot.
        //
        /*assert(hash == Hash_Word(STR_HEAD(synonym)));*/
        canons_by_hash[hash] = synonym;
        SET_SER_FLAG(synonym, STRING_FLAG_CANON);
        synonym->misc.bind_index.low = 0;
        synonym->misc.bind_index.high = 0;
    }
    else {
        // This canon form must be removed from the hash table.  Ripple the
        // collision slots back until a NULL is found, to reduce search times.
        //
        REBCNT previous_hash = hash;
        while (canons_by_hash[hash] != NULL) {
            hash += skip;
            if (hash >= size) hash -= size;
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
//  Val_Init_Word_Bound: C
// 
// Initialize an ANY-WORD! type with a binding to a context.
//
void Val_Init_Word_Bound(
    REBVAL *out,
    enum Reb_Kind type,
    REBSTR *name,
    REBCTX *context,
    REBCNT index
) {
    assert(name != NULL);
    assert(context);

    VAL_RESET_HEADER(out, type);
    SET_VAL_FLAG(out, WORD_FLAG_BOUND);
    INIT_WORD_SPELLING(out, name);
    INIT_WORD_CONTEXT(out, context);
    INIT_WORD_INDEX(out, index);

    assert(ANY_WORD(out));

    // !!! Assert that the key in that position matches?!  Seems sensible
    // (add it when other changes done)
}


//
//  Get_Type_Name: C
//
const REBYTE *Get_Type_Name(const REBVAL *value)
{
    return STR_HEAD(Canon(SYM_FROM_KIND(VAL_TYPE(value))));
}


//
//  Compare_Word: C
// 
// Compare the names of two words and return the difference.
// Note that words are kept UTF8 encoded.
// Positive result if s > t and negative if s < t.
//
REBINT Compare_Word(const RELVAL *s, const RELVAL *t, REBOOL is_case)
{
    const REBYTE *sp = STR_HEAD(VAL_WORD_CASED(s));
    const REBYTE *tp = STR_HEAD(VAL_WORD_CASED(t));

    // Use a more strict comparison than normal:
    if (is_case) return COMPARE_BYTES(sp, tp);

    // They are the equivalent words:
    if (VAL_WORD_CANON(s) == VAL_WORD_CANON(t)) return 0;

    // They must be differ by case:
    return Compare_UTF8(sp, tp, LEN_BYTES(tp)) + 2;
}


//
//  Init_Symbols: C
//
// By this point in the boot, the canon words have already been created for
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
void Init_Symbols(REBARR *words)
{
    PG_Symbol_Canons = Make_Series(
        ARR_LEN(words) + 1, // extra NULL at head for SYM_0 (END maps to NULL)
        sizeof(REBSTR*),
        MKS_NONE
    );

    REBSYM sym = SYM_0;
    *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym)) = NULL; // Canon(REB_0)

    RELVAL *word = ARR_HEAD(words);
    for (; NOT_END(word); ++word) {
        REBSTR *canon = VAL_WORD_CASED(word);
        assert(GET_SER_FLAG(canon, STRING_FLAG_CANON));

        sym = cast(REBSYM, cast(REBCNT, sym) + 1);
        *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym)) = canon;

        // More code was loaded than just the word list, and it might have
        // included alternate-case forms of the %words.r words.  Walk any
        // aliases and make sure they have the header bits too.

        REBSTR *name = canon;
        do {
            // The low bits of the header are reserved for flags, including
            // those common between singulars and "doubulars".  The symbol
            // numbers are shifted by 16 bits
            //
            assert((name->header.bits >> 16) == 0);
            name->header.bits |= cast(REBUPT, sym << 16);
            name = name->link.synonym;
        } while (name != canon); // circularly linked list, stop on a cycle
    }

    *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym)) = NULL; // terminate
    sym = cast(REBSYM, cast(REBCNT, sym) + 1);

    SET_SERIES_LEN(PG_Symbol_Canons, cast(REBCNT, sym));
    assert(SER_LEN(PG_Symbol_Canons) == ARR_LEN(words) + 1);

    // Do some sanity checks

    if (COMPARE_BYTES(cb_cast("blank!"), STR_HEAD(Canon(SYM_BLANK_X))) != 0)
        panic (Error(RE_BAD_BOOT_STRING));
    if (COMPARE_BYTES(cb_cast("true"), STR_HEAD(Canon(SYM_TRUE))) != 0)
        panic (Error(RE_BAD_BOOT_STRING));
}


//
//  Init_Words: C
//
void Init_Words(void)
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

    PG_Canons_By_Hash = Make_Series(n, sizeof(REBSTR*), MKS_POWER_OF_2);
    Clear_Series(PG_Canons_By_Hash); // all slots start at NULL
    SET_SERIES_LEN(PG_Canons_By_Hash, n);
}
