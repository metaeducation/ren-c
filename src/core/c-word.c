/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-word.c
**  Summary: symbol table and word related functions
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**    Word table is a block composed of symbols, each of which contain
**    a canon word number, alias word number (if it exists), and an
**    index that refers to the string for the text itself.
**
**    The canon number for a word is unique and is used to compare
**    words. The word table is independent of context frames and
**    words are never garbage collected.
**
**    The alias is used mainly for upper and lower case equality,
**    but can also be used to create ALIASes.
**
**    The word strings are stored as a single large string series.
**    NEVER CACHE A WORD NAME POINTER if new words may be added (e.g.
**    LOAD), because the series may get moved in memory.
**
***********************************************************************/

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


//
//  Expand_Hash: C
// 
// Expand hash series. Clear it but set its tail.
//
void Expand_Hash(REBSER *ser)
{
    REBINT pnum = Get_Hash_Prime(SER_LEN(ser) + 1);
    if (!pnum) {
        REBVAL temp;
        SET_INTEGER(&temp, SER_LEN(ser) + 1);
        fail (Error(RE_SIZE_LIMIT, &temp));
    }

    assert(!Is_Array_Series(ser));
    Remake_Series(ser, pnum + 1, SER_WIDE(ser), MKS_POWER_OF_2);

    Clear_Series(ser);
    SET_SERIES_LEN(ser, pnum);
}


//
//  Expand_Word_Table: C
// 
// Expand the hash table part of the word_table by allocating
// the next larger table size and rehashing all the words of
// the current table.  Free the old hash array.
//
static void Expand_Word_Table(void)
{
    REBCNT *hashes;
    REBVAL *word;
    REBINT hash;
    REBCNT size;
    REBINT skip;
    REBCNT n;

    // Allocate a new hash table:
    Expand_Hash(PG_Word_Table.hashes);
    // Debug_Fmt("WORD-TABLE: expanded (%d symbols, %d slots)", PG_Word_Table.array->tail, PG_Word_Table.hashes->tail);

    // Rehash all the symbols:
    word = ARR_AT(PG_Word_Table.array, 1);
    hashes = SER_HEAD(REBCNT, PG_Word_Table.hashes);
    size = SER_LEN(PG_Word_Table.hashes);
    for (n = 1; n < ARR_LEN(PG_Word_Table.array); n++, word++) {
        const REBYTE *name = VAL_SYM_NAME(word);
        hash = Hash_Word(name, LEN_BYTES(name));
        skip  = (hash & 0x0000FFFF) % size;
        if (skip == 0) skip = 1;
        hash = (hash & 0x00FFFF00) % size;
        while (hashes[hash]) {
            hash += skip;
            if (hash >= (REBINT)size) hash -= size;
        }
        hashes[hash] = n;
    }
}


//
//  Make_Word_Name: C
// 
// Allocates and copies the text string of the word.
//
static REBCNT Make_Word_Name(const REBYTE *str, REBCNT len)
{
    REBCNT pos = SER_LEN(PG_Word_Names);

    Append_Mem_Extra(PG_Word_Names, str, len, 1); // so we can do next line...

    // keep terminator for each string
    SET_SERIES_LEN(PG_Word_Names, SER_LEN(PG_Word_Names) + 1);

    return pos;
}


//
//  Make_Word: C
// 
// Given a string and its length, compute its hash value,
// search for a match, and if not found, add it to the table.
// Return the table index for the word (whether found or new).
//
REBCNT Make_Word(const REBYTE *str, REBCNT len)
{
    REBINT  hash;
    REBINT  size;
    REBINT  skip;
    REBINT  n;
    REBCNT  h;
    REBCNT  *hashes;
    REBVAL  *words;
    REBVAL  *w;

    //REBYTE *sss = Get_Sym_Name(1);    // (Debugging method)

    // Prior convention was to assume zero termination if length was zero,
    // but that creates problems.  Caller should use LEN_BYTES for that.

    assert(len != 0);

    // !!! ...but should the zero length word be a valid word?

    // If hash part of word table is too dense, expand it:
    if (
        ARR_LEN(PG_Word_Table.array) > SER_LEN(PG_Word_Table.hashes) / 2
    ) {
        Expand_Word_Table();
    }

    assert(ARR_LEN(PG_Word_Table.array) == SER_LEN(Bind_Table));

    // If word symbol part of word table is full, expand it:
    if (SER_FULL(ARR_SERIES(PG_Word_Table.array))) {
        Extend_Series(ARR_SERIES(PG_Word_Table.array), 256);
    }
    if (SER_FULL(Bind_Table)) {
        Extend_Series(Bind_Table, 256);
        CLEAR_SEQUENCE(Bind_Table);
    }

    size = cast(REBINT, SER_LEN(PG_Word_Table.hashes));
    words = ARR_HEAD(PG_Word_Table.array);
    hashes = SER_HEAD(REBCNT, PG_Word_Table.hashes);

    // Hash the word, including a skip factor for lookup:
    hash  = Hash_Word(str, len);
    skip  = (hash & 0x0000FFFF) % size;
    if (skip == 0) skip = 1;
    hash = (hash & 0x00FFFF00) % size;
    //Debug_Fmt("%s hash %d skip %d", str, hash, skip);

    // Search hash table for word match:
    while ((h = hashes[hash])) {
        while ((n = Compare_UTF8(VAL_SYM_NAME(words+h), str, len)) >= 0) {
            //if (Match_String("script", str, len))
            //  Debug_Fmt("---- %s %d %d\n", VAL_SYM_NAME(&words[h]), n, h);
            if (n == 0) return h; // direct hit
            if (VAL_SYM_ALIAS(words+h)) h = VAL_SYM_ALIAS(words+h);
            else goto make_sym; // Create new alias for word
        }
        hash += skip;
        if (hash >= size) hash -= size;
    }

make_sym:
    n = ARR_LEN(PG_Word_Table.array);
    w = words + n;
    if (h) {
        // Alias word (h = canon word)
        VAL_SYM_ALIAS(words+h) = n;
        VAL_SYM_CANON(w) = VAL_SYM_CANON(words+h);
    } else {
        // Canon (base version of) word (h == 0)
        hashes[hash] = n;
        VAL_SYM_CANON(w) = n;
    }
    VAL_SYM_ALIAS(w) = 0;
    VAL_SYM_NINDEX(w) = Make_Word_Name(str, len);
    VAL_RESET_HEADER(w, REB_HANDLE);

    // These are allowed because of the SER_FULL checks above which
    // add one extra to the TAIL check comparision. However, their
    // termination values (nulls) will be missing.
    SET_ARRAY_LEN(PG_Word_Table.array, ARR_LEN(PG_Word_Table.array) + 1);
    SET_SERIES_LEN(Bind_Table, SER_LEN(Bind_Table) + 1);

    assert(n != SYM_0);
    return n;
}


//
//  Last_Word_Num: C
// 
// Return the number of the last word created.  Used to
// mark a range of canon-words (e.g. operators).
//
REBCNT Last_Word_Num(void)
{
    return ARR_LEN(PG_Word_Table.array) - 1;
}


//
//  Val_Init_Word_Bound: C
// 
// Initialize an ANY-WORD! type with a binding to a context.
//
void Val_Init_Word_Bound(
    REBVAL *out,
    enum Reb_Kind type,
    REBSYM sym,
    REBCTX *context,
    REBCNT index
) {
    assert(sym != SYM_0);
    assert(context);

    VAL_RESET_HEADER(out, type);
    SET_VAL_FLAG(out, WORD_FLAG_BOUND_SPECIFIC);
    INIT_WORD_SYM(out, sym);
    INIT_WORD_SPECIFIC(out, context);
    INIT_WORD_INDEX(out, index);

    assert(ANY_WORD(out));

    // !!! Assert that the key in that position matches?!  Seems sensible
    // (add it when other changes done)
}


//
//  Val_Init_Word: C
// 
// Initialize a value as a word. Set frame as unbound--no context.  (See
// also Val_Init_Word_Bound)
//
void Val_Init_Word(REBVAL *out, enum Reb_Kind type, REBSYM sym)
{
    assert(sym != SYM_0);

    VAL_RESET_HEADER(out, type);
    INIT_WORD_SYM(out, sym);

#if !defined(NDEBUG)
    out->payload.any_word.index = 0;
#endif

    assert(ANY_WORD(out));
    assert(IS_WORD_UNBOUND(out));
}


//
//  Get_Sym_Name: C
//
const REBYTE *Get_Sym_Name(REBCNT num)
{
    if (num == 0 || num >= ARR_LEN(PG_Word_Table.array))
        return cb_cast("???");

    return VAL_SYM_NAME(ARR_AT(PG_Word_Table.array, num));
}


//
//  Get_Word_Name: C
//
const REBYTE *Get_Word_Name(const REBVAL *value)
{
    if (value) return Get_Sym_Name(VAL_WORD_SYM(value));
    return cb_cast("(unnamed)");
}


//
//  Get_Type_Name: C
//
const REBYTE *Get_Type_Name(const REBVAL *value)
{
    return Get_Sym_Name(SYM_FROM_KIND(VAL_TYPE(value)));
}


//
//  Compare_Word: C
// 
// Compare the names of two words and return the difference.
// Note that words are kept UTF8 encoded.
// Positive result if s > t and negative if s < t.
//
REBINT Compare_Word(const REBVAL *s, const REBVAL *t, REBOOL is_case)
{
    REBYTE *sp = VAL_WORD_NAME(s);
    REBYTE *tp = VAL_WORD_NAME(t);

    // Use a more strict comparison than normal:
    if (is_case) return COMPARE_BYTES(sp, tp);

    // They are the equivalent words:
    if (VAL_WORD_CANON(s) == VAL_WORD_CANON(t)) return 0;

    // They must be differ by case:
    return Compare_UTF8(sp, tp, LEN_BYTES(tp)) + 2;
}


//
//  Init_Words: C
// 
// Only flags BIND_Table creation only (for threads).
//
void Init_Words(REBOOL only)
{
    REBCNT n = Get_Hash_Prime(WORD_TABLE_SIZE * 4); // extra to reduce rehashing

    if (!only) {
        // Create the hash for locating words quickly:
        // Note that the TAIL is never changed for this series.
        PG_Word_Table.hashes = Make_Series(n + 1, sizeof(REBCNT), MKS_NONE);
        Clear_Series(PG_Word_Table.hashes);
        SET_SERIES_LEN(PG_Word_Table.hashes, n);

        // The word (symbol) table itself:
        PG_Word_Table.array = Make_Array(WORD_TABLE_SIZE);

        // !!! R3-Alpha would "Put a NONE at the head" here.  Why?  It seemed
        // to later think it needed to be able to read a symbol out of a none,
        // which it cannot do.  Changed to a typeset with symbol 0--which
        // seems to work as intended, but review what the intent is.
        //
        Val_Init_Typeset(ARR_HEAD(PG_Word_Table.array), ALL_64, SYM_0);

        SET_ARRAY_LEN(PG_Word_Table.array, 1);  // prevent the zero case

        // A normal char array to hold symbol names:
        PG_Word_Names = Make_Binary(6 * WORD_TABLE_SIZE); // average word size
    }

    // The bind table. Used to cache context indexes for given symbols.
    Bind_Table = Make_Series(
        SER_REST(ARR_SERIES(PG_Word_Table.array)),
        sizeof(REBCNT),
        MKS_NONE
    );
    CLEAR_SEQUENCE(Bind_Table);
    SET_SERIES_LEN(Bind_Table, ARR_LEN(PG_Word_Table.array));
}
