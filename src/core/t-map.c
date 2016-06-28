//
//  File: %t-map.c
//  Summary: "map datatype"
//  Section: datatypes
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
/*
    A map is a SERIES that can also include a hash table for faster lookup.

    The hashing method used here is the same as that used for the
    REBOL symbol table, with the exception that this method must
    also store the value of the symbol (not just its word).

    The structure of the series header for a map is the same as other
    series, except that the opt series field is a pointer to a REBCNT
    series, the hash table.

    The hash table is an array of REBCNT integers that are index values
    into the map series. NOTE: They are one-based to avoid 0 which is an
    empty slot.

    Each value in the map consists of a word followed by its value.

    These functions are also used hashing SET operations (e.g. UNION).

    The series/tail / 2 is the number of values stored.

    The hash-series/tail is a prime number that is use for computing
    slots in the hash table.
*/

#include "sys-core.h"

//
//  CT_Map: C
//
REBINT CT_Map(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    return 0 == Cmp_Array(a, b, FALSE);
}


//
//  Make_Map: C
// 
// Makes a MAP block (that holds both keys and values).
// Capacity is measured in key-value pairings.
// A hash series is also created.
//
static REBMAP *Make_Map(REBCNT capacity)
{
    REBARR *array = Make_Array((capacity * 2) + 1); // + 1 for terminator
    REBMAP *map = AS_MAP(array);

    MAP_HASHLIST(map) = Make_Hash_Sequence(capacity);

    return map;
}


//
//  Find_Key_Hashed: C
// 
// Returns hash index (either the match or the new one).
// A return of zero is valid (as a hash index);
// 
// Wide: width of record (normally 2, a key and a value).
// 
// Modes:
//     0 - search, return hash if found or not
//     1 - search, return hash, else return -1 if not
//     2 - search, return hash, else append value and return -1
//
REBINT Find_Key_Hashed(
    REBARR *array,
    REBSER *hashlist,
    const RELVAL *key, // !!! assumes key is followed by value(s) via ++
    REBCTX *specifier,
    REBCNT wide,
    REBOOL cased,
    REBYTE mode
) {
    REBCNT *hashes;
    REBCNT skip;
    REBCNT hash;
    // a 'zombie' is a key with void value, that may be overwritten
    REBCNT zombie;
    REBCNT uncased;
    REBCNT len;
    REBCNT n;
    RELVAL *val;

    // Compute hash for value:
    len = SER_LEN(hashlist);
    assert(len > 0);
    hash = Hash_Value(key, specifier);

    // The REBCNT[] hash array size is chosen to try and make a large enough
    // table relative to the data that collisions will be hopefully not
    // frequent.  But they may still collide.  The method R3-Alpha chose to
    // deal with collisions was to have a "skip" amount that will go try
    // another hash bucket until the searched for key is found or a 0
    // entry in the hashlist is found.
    //
    // It is not--by inspection--completely clear how this is guaranteed to
    // terminate in finding the value with no false negatives.  One would hope
    // that something about the logic works that two hashings which wound up
    // overwriting the same buckets could only in a worst case scenario force
    // one another to visit all the positions.  Review to make sure this
    // method actually does have a coherent logic behind it.
    // EDIT: if len and skip are co-primes is guaranteed that repeatedly adding skip (and subtracting len when needed) all positions are visited. -- @giuliolunati
    skip = hash % (len - 1) + 1;
    // 1 <= skip < len, and len is prime, so skip and len are co-prime.
    hash = hash % len;
    zombie = len; // zombie not yet encountered
    // Scan hash table for match:
    uncased = len; // uncased match not yet encountered
    hashes = SER_HEAD(REBCNT, hashlist);
    if (ANY_WORD(key)) {
        while ((n = hashes[hash])) {
            val = ARR_AT(array, (n - 1) * wide);
            if (ANY_WORD(val) && VAL_WORD_CASED(key) == VAL_WORD_CASED(val))
                return hash;

            if (!cased && VAL_WORD_CANON(key) == VAL_WORD_CANON(val) && uncased == len) {
                uncased = hash;
            }
            else if (wide > 1 && IS_VOID(++val) && zombie == len) {
                zombie = hash;
            }
            hash += skip;
            if (hash >= len) hash -= len;
        }
    }
    else if (ANY_BINSTR(key)) {
        while ((n = hashes[hash])) {
            val = ARR_AT(array, (n - 1) * wide);
            if (VAL_TYPE(val) == VAL_TYPE(key)) {
                if (0 == Compare_String_Vals(val, key, FALSE)) return hash;
                if (
                    !cased && uncased == len
                    && 0 == Compare_String_Vals(
                        val, key, LOGICAL(!IS_BINARY(key))
                    )
                ) {
                    uncased = hash;
                }
            }
            if (wide > 1 && IS_VOID(++val) && zombie == len)  {
                zombie = hash;
            }
            hash += skip;
            if (hash >= len) hash -= len;
        }
    } else {
        while ((n = hashes[hash])) {
            val = ARR_AT(array, (n - 1) * wide);
            if (VAL_TYPE(val) == VAL_TYPE(key)) {
                if (0 == Cmp_Value(key, val, TRUE)) {
                    return hash;
                }
                if (
                    !cased && uncased == len
                    && REB_CHAR == VAL_TYPE(val)
                    && 0 == Cmp_Value(key, val, FALSE)
                ) {
                    uncased = hash;
                }
            }
            if (wide > 1 && IS_VOID(++val) && zombie == len) zombie = hash;
            hash += skip;
            if (hash >= len) hash -= len;
        }
    }

    //assert(n == 0);
    if (!cased && uncased < len) hash = uncased; // uncased< match 
    else if (zombie < len) { // zombie encountered!
        assert(mode == 0);
        hash = zombie;
        n = hashes[hash];
        // new key overwrite zombie
        *ARR_AT(array, (n - 1) * wide) = *key;
    }
    // Append new value the target series:
    if (mode > 1) {
        REBCNT index;
        const RELVAL *src = key;
        hashes[hash] = (ARR_LEN(array) / wide) + 1;

        // This used to use Append_Values_Len, but that is a REBVAL* interface
        // !!! Should there be an Append_Values_Core which takes RELVAL*?
        //
        for (index = 0; index < wide; ++src, ++index)
            Append_Value_Core(array, src, specifier);
    }

    return (mode > 0) ? NOT_FOUND : hash;
}


//
//  Rehash_Map: C
// 
// Recompute the entire hash table for a map. Table must be large enough.
//
static void Rehash_Map(REBMAP *map)
{
    REBVAL *key;
    REBCNT n;
    REBCNT *hashes;
    REBARR *pairlist;
    REBSER *hashlist = MAP_HASHLIST(map);

    if (!hashlist) return;

    hashes = SER_HEAD(REBCNT, hashlist);
    pairlist = MAP_PAIRLIST(map);

    key = KNOWN(ARR_HEAD(pairlist));
    for (n = 0; n < ARR_LEN(pairlist); n += 2, key += 2) {
        REBCNT hash;
        const REBOOL cased = TRUE; // cased=TRUE is always fine

        if (IS_VOID(key + 1)) {
            //
            // It's a "zombie", move last key to overwrite it
            //
            *key = *KNOWN(ARR_AT(pairlist, ARR_LEN(pairlist) - 2));
            *(key + 1) = *KNOWN(ARR_AT(pairlist, ARR_LEN(pairlist) - 1));
            SET_ARRAY_LEN(pairlist, ARR_LEN(pairlist) - 2);
        }

        hash = Find_Key_Hashed(
            pairlist, hashlist, key, SPECIFIED, 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (IS_VOID(ARR_AT(pairlist, ARR_LEN(pairlist) - 1))) {
            SET_ARRAY_LEN(pairlist, ARR_LEN(pairlist) - 2);
        }
    }
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
//  Find_Map_Entry: C
// 
// Try to find the entry in the map. If not found and val isn't void, create
// the entry and store the key and val.
//
// RETURNS: the index to the VALUE or zero if there is none.
//
static REBCNT Find_Map_Entry(
    REBMAP *map,
    const RELVAL *key,
    REBCTX *key_specifier,
    const RELVAL *val,
    REBCTX *val_specifier,
    REBOOL cased // case-sensitive if true
) {
    REBSER *hashlist = MAP_HASHLIST(map); // can be null
    REBARR *pairlist = MAP_PAIRLIST(map);
    REBCNT *hashes;
    REBCNT hash;
    REBVAL *v;
    REBCNT n;

    if (IS_BLANK(key)) return 0;

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (ARR_LEN(pairlist) > SER_LEN(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    hash = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, 2, cased, 0
    );
    hashes = SER_HEAD(REBCNT, hashlist);
    n = hashes[hash];
    // n==0 or pairlist[(n-1)*]=~key

    // Just a GET of value:
    if (!val) return n;

    // Must set the value:
    if (n) {  // re-set it:
        COPY_VALUE(ARR_AT(pairlist, ((n - 1) * 2) + 1), val, val_specifier);
        return n;
    }

    if (IS_VOID(val)) return 0; // trying to remove non-existing key

    // Create new entry.  Note that it does not copy underlying series (e.g.
    // the data of a string)
    //
    Append_Value_Core(pairlist, key, key_specifier);
    Append_Value_Core(pairlist, val, val_specifier);

    return (hashes[hash] = (ARR_LEN(pairlist) / 2));
}


//
//  Length_Map: C
//
REBINT Length_Map(REBMAP *map)
{
    REBCNT n, c = 0;
    REBVAL *v = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    for (n = 0; !IS_END(v); n += 2, v += 2) {
        if (!IS_BLANK(v + 1)) c++; // must have non-blank value (!!! void?)
    }

    assert(n == ARR_LEN(MAP_PAIRLIST(map)));

    return c;
}


//
//  PD_Map: C
//
REBINT PD_Map(REBPVS *pvs)
{
    REBVAL *val;
    REBINT n;

    REBOOL setting = LOGICAL(pvs->opt_setval && IS_END(pvs->item + 1));

    assert(IS_MAP(pvs->value));

    if (setting)
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(pvs->value));

    if (IS_BLANK(pvs->selector))
        return PE_NONE;

    n = Find_Map_Entry(
        VAL_MAP(pvs->value),
        pvs->selector,
        SPECIFIED,
        setting ? pvs->opt_setval : NULL,
        SPECIFIED,
        setting // `cased` flag for case-sensitivity--use when setting only
    );

    if (n == 0)
        return PE_NONE;

    val = KNOWN(ARR_AT(MAP_PAIRLIST(VAL_MAP(pvs->value)), ((n - 1) * 2) + 1));
    if (IS_VOID(val))
        return PE_NONE;

    pvs->value = val;
    pvs->value_specifier = SPECIFIED;

    return PE_OK;
}


//
//  Append_Map: C
//
static void Append_Map(
    REBMAP *map,
    REBARR *array,
    REBCNT index,
    REBCTX *specifier,
    REBCNT len
) {
    RELVAL *item = ARR_AT(array, index);
    REBCNT n = 0;

    while (n < len && NOT_END(item)) {
        if (IS_BAR(item)) {
            //
            // A BAR! between map pairs is okay, e.g. `make map! [a b | c d]`
            //
            ++item;
            ++n;
            continue;
        }

        if (IS_END(item + 1)) {
            //
            // Keys with no value not allowed, e.g. `make map! [1 "foo" 2]`
            //
            fail (Error(RE_PAST_END));
        }

        if (IS_BAR(item + 1)) {
            //
            // Expression barriers allowed between items but not as the
            // mapped-to value for a key, e.g. `make map! [1 "foo" 2 |]`
            //
            fail (Error(RE_EXPRESSION_BARRIER));
        }

        Find_Map_Entry(
            map,
            item,
            specifier,
            item + 1,
            specifier,
            TRUE
        );

        item += 2;
        n += 2;
    }
}


//
//  MAKE_Map: C
//
void MAKE_Map(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (ANY_NUMBER(arg)) {
        REBMAP *map = Make_Map(Int32s(arg, 0));
        Val_Init_Map(out, map);
    }
    else {
        // !!! R3-Alpha TO of MAP! was like MAKE but wouldn't accept just
        // being given a size.
        //
        TO_Map(out, kind, arg);
    }
}


//
//  TO_Map: C
//
void TO_Map(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    REBARR* array;
    REBCNT len;
    REBCNT index;

    if (IS_BLOCK(arg) || IS_GROUP(arg)) {
        //
        // make map! [word val word val]
        //
        array = VAL_ARRAY(arg);
        index = VAL_INDEX(arg);
        len = VAL_ARRAY_LEN_AT(arg);
    }
    else if (IS_MAP(arg)) {
        array = MAP_PAIRLIST(VAL_MAP(arg));
        index = 0;// maps don't have an index/"position"
        len = ARR_LEN(array);
    }
    else
        fail (Error_Invalid_Arg(arg));

    REBMAP *map = Make_Map(len / 2); // [key value key value...] + END
    Append_Map(map, array, index, VAL_SPECIFIER(arg), UNKNOWN);
    Rehash_Map(map);
    Val_Init_Map(out, map);
}


//
//  Map_To_Array: C
// 
// what: -1 - words, +1 - values, 0 -both
//
REBARR *Map_To_Array(REBMAP *map, REBINT what)
{
    REBVAL *val;
    REBCNT cnt = 0;
    REBARR *array;
    REBVAL *dest;

    // Count number of set entries:
    //
    val = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(val) && NOT_END(val + 1); val += 2) {
        if (!IS_BLANK(val + 1)) cnt++; // must have non-blank value !!! void?
    }

    // Copy entries to new block:
    //
    array = Make_Array(cnt * ((what == 0) ? 2 : 1));
    dest = SINK(ARR_HEAD(array));
    val = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(val) && NOT_END(val+1); val += 2) {
        if (!IS_BLANK(val + 1)) {
            if (what <= 0) *dest++ = val[0];
            if (what >= 0) *dest++ = val[1];
        }
    }

    SET_END(dest);
    SET_ARRAY_LEN(array, cast(RELVAL*, dest) - ARR_HEAD(array));
    return array;
}


//
//  Mutate_Array_Into_Map: C
// 
// Convert existing array to a map.  The array is tested to make sure it is
// not managed, hence it has not been put into any REBVALs that might use
// a non-map-aware access to it.  (That would risk making changes to the
// array that did not keep the hashes in sync.)
//
REBMAP *Mutate_Array_Into_Map(REBARR *array)
{
    REBCNT size = ARR_LEN(array);

    // See note above--can't have this array be accessible via some ANY-BLOCK!
    //
    assert(NOT(IS_ARRAY_MANAGED(array)));

    REBMAP *map = AS_MAP(array);
    MAP_HASHLIST(map) = Make_Hash_Sequence(size);

    Rehash_Map(map);
    return map;
}


//
//  Alloc_Context_From_Map: C
//
REBCTX *Alloc_Context_From_Map(REBMAP *map)
{
    REBCNT cnt = 0;
    REBVAL *mval;

    REBCTX *context;
    REBVAL *key;
    REBVAL *var;

    // Count number of set entries:
    mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(mval) && NOT_END(mval + 1); mval += 2) {
        if (ANY_WORD(mval) && !IS_BLANK(mval + 1)) cnt++;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words
    context = Alloc_Context(cnt);
    key = CTX_KEYS_HEAD(context);
    var = CTX_VARS_HEAD(context);

    mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    for (; NOT_END(mval) && NOT_END(mval + 1); mval += 2) {
        if (ANY_WORD(mval) && !IS_BLANK(mval + 1)) {
            // !!! Used to leave SET_WORD typed values here... but why?
            // (Objects did not make use of the set-word vs. other distinctions
            // that function specs did.)
            Val_Init_Typeset(
                key,
                // all types except void
                ~FLAGIT_KIND(REB_0),
                VAL_WORD_CASED(mval)
            );
            key++;
            *var++ = mval[1];
        }
    }

    SET_END(key);
    SET_END(var);

    SET_ARRAY_LEN(CTX_VARLIST(context), cnt + 1);
    SET_ARRAY_LEN(CTX_KEYLIST(context), cnt + 1);

    return context;
}


//
//  REBTYPE: C
//
REBTYPE(Map)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBINT n;
    REBMAP *map = VAL_MAP(val);
    REBCNT  args;

    switch (action) {

    case SYM_PICK:
        Pick_Block(D_OUT, val, arg);
        if (IS_VOID(D_OUT)) return R_BLANK;
        return R_OUT;

    case SYM_FIND:
    case SYM_SELECT:
        args = Find_Refines(frame_, ALL_FIND_REFS);
        n = Find_Map_Entry(
            map,
            arg,
            SPECIFIED,
            NULL,
            SPECIFIED,
            LOGICAL(args & AM_FIND_CASE)
        );
        if (!n) return R_BLANK;
        *D_OUT = *KNOWN(ARR_AT(MAP_PAIRLIST(map), ((n - 1) * 2) + 1));
        if (IS_VOID(D_OUT)) return R_BLANK;
        if (action == SYM_FIND) *D_OUT = *val;
        return R_OUT;

    case SYM_INSERT:
    case SYM_APPEND:
        FAIL_IF_LOCKED_ARRAY(MAP_PAIRLIST(map));

        if (!IS_BLOCK(arg)) fail (Error_Invalid_Arg(val));
        *D_OUT = *val;
        if (D_REF(AN_DUP)) {
            n = Int32(D_ARG(AN_COUNT));
            if (n <= 0) break;
        }
        Append_Map(
            map,
            VAL_ARRAY(arg),
            VAL_INDEX(arg),
            VAL_SPECIFIER(arg),
            Partial1(arg, D_ARG(AN_LIMIT))
        );
        return R_OUT;

    case SYM_REMOVE:
        FAIL_IF_LOCKED_ARRAY(MAP_PAIRLIST(map));

        if (!D_REF(4)) { // /MAP
            fail (Error_Illegal_Action(REB_MAP, action));
        }
        *D_OUT = *val;
        Find_Map_Entry(
            map, D_ARG(5), SPECIFIED, VOID_CELL, SPECIFIED, TRUE
        );
        return R_OUT;

    case SYM_POKE:  // CHECK all pokes!!! to be sure they check args now !!!
        FAIL_IF_LOCKED_ARRAY(MAP_PAIRLIST(map));

        n = Find_Map_Entry(
            map, arg, SPECIFIED, D_ARG(3), SPECIFIED, TRUE
        );
        *D_OUT = *D_ARG(3);
        return R_OUT;

    case SYM_LENGTH:
        SET_INTEGER(D_OUT, Length_Map(map));
        return R_OUT;

    case SYM_COPY:
        //
        // !!! the copying map case should probably not be a MAKE case, but
        // implemented here as copy.
        //
        MAKE_Map(D_OUT, REB_MAP, val); // may fail()
        return R_OUT;

    case SYM_CLEAR:
        FAIL_IF_LOCKED_ARRAY(MAP_PAIRLIST(map));

        Reset_Array(MAP_PAIRLIST(map));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        CLEAR(
            MAP_HASHLIST(map)->content.dynamic.data,
            SER_SPACE(MAP_HASHLIST(map))
        );
        TERM_SERIES(MAP_HASHLIST(map));

        Val_Init_Map(D_OUT, map);
        return R_OUT;

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);

        if (sym == SYM_VALUES)
            n = 1;
        else if (sym == SYM_WORDS)
            n = -1;
        else if (sym == SYM_BODY)
            n = 0;
        else
            fail (Error_Cannot_Reflect(REB_MAP, arg));

        REBARR *array = Map_To_Array(map, n);
        Val_Init_Block(D_OUT, array);
        return R_OUT;
    }

    case SYM_TAIL_Q:
        return (Length_Map(map) == 0) ? R_TRUE : R_FALSE;
    }

    fail (Error_Illegal_Action(REB_MAP, action));
}
