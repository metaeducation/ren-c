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
**  Module:  t-map.c
**  Summary: map datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/
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
REBINT CT_Map(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    if (mode < 0) return -1;
    if (mode == 3) return VAL_SERIES(a) == VAL_SERIES(b);
    return 0 == Cmp_Block(a, b, FALSE);
}


//
//  Make_Map: C
// 
// Makes a MAP block (that holds both keys and values).
// Size is the number of key-value pairs.
// A hash series is also created.
//
static REBMAP *Make_Map(REBINT size)
{
    REBARR *array = Make_Array(size * 2);
    REBMAP *map = AS_MAP(array);

    MAP_HASHLIST(map) = Make_Hash_Sequence(size);

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
    const REBVAL *key,
    REBINT wide,
    REBOOL cased,
    REBYTE mode
) {
    REBCNT *hashes;
    REBCNT skip;
    REBCNT hash;
    // a 'zombie' is a key with UNSET! value, that may be overwritten
    REBCNT zombie;
    REBCNT len;
    REBCNT n;
    REBVAL *val;

    // Compute hash for value:
    len = SERIES_LEN(hashlist);
    assert(len > 0);
    hash = Hash_Value(key);

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
    hashes = SERIES_HEAD(REBCNT, hashlist);
    if (ANY_WORD(key)) {
        while ((n = hashes[hash])) {
            val = ARRAY_AT(array, (n - 1) * wide);
            if (
                ANY_WORD(val) &&
                (VAL_WORD_SYM(key) == VAL_WORD_SYM(val)
                || (!cased && VAL_WORD_CANON(key) == VAL_WORD_CANON(val))
                )
            ) {
                return hash;
            }
            if (IS_UNSET(++val)) zombie = hash;
            hash += skip;
            if (hash >= len) hash -= len;
        }
    }
    else if (ANY_BINSTR(key)) {
        while ((n = hashes[hash])) {
            val = ARRAY_AT(array, (n - 1) * wide);
            if (
                VAL_TYPE(val) == VAL_TYPE(key)
                && 0 == Compare_String_Vals(
                    val, key, LOGICAL(!IS_BINARY(key) && !cased)
                )
            ) {
                return hash;
            }
            if (IS_UNSET(++val)) zombie = hash;
            hash += skip;
            if (hash >= len) hash -= len;
        }
    } else {
        while ((n = hashes[hash])) {
            val = ARRAY_AT(array, (n - 1) * wide);
            if (
                VAL_TYPE(val) == VAL_TYPE(key)
                && 0 == Cmp_Value(key, val, cased)
            ) {
                return hash;
            }
            if (IS_UNSET(++val)) zombie = hash;
            hash += skip;
            if (hash >= len) hash -= len;
        }
    }

    if (zombie < len) { // zombie encountered!
        assert(mode == 0);
        hash = zombie;
        n = hashes[hash];
        // new key overwrite zombie
        *ARRAY_AT(array, (n - 1) * wide) = *key;
    }
    // Append new value the target series:
    if (mode > 1) {
        hashes[hash] = (ARRAY_LEN(array) / wide) + 1;
        Append_Values_Len(array, key, wide);
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

    hashes = SERIES_HEAD(REBCNT, hashlist);
    pairlist = MAP_PAIRLIST(map);

    key = ARRAY_HEAD(pairlist);
    for (n = 0; n < ARRAY_LEN(pairlist); n += 2, key += 2) {
        REBCNT hash;
        const REBOOL cased = TRUE; // cased=TRUE is always fine

        if (IS_UNSET(key + 1)) {
            //
            // It's a "zombie", move last key to overwrite it
            //
            *key = *ARRAY_AT(pairlist, ARRAY_LEN(pairlist) - 2);
            *(key + 1) = *ARRAY_AT(pairlist, ARRAY_LEN(pairlist) - 1);
            SET_ARRAY_LEN(pairlist, ARRAY_LEN(pairlist) - 2);
        }

        hash = Find_Key_Hashed(pairlist, hashlist, key, 2, cased, 0);
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (IS_UNSET(ARRAY_AT(pairlist, ARRAY_LEN(pairlist) - 1))) {
            SET_ARRAY_LEN(pairlist, ARRAY_LEN(pairlist) - 2);
        }
    }
}


//
//  Find_Map_Entry: C
// 
// Try to find the entry in the map. If not found and val IS_SET(), create the
// entry and store the key and val.
//
// RETURNS: the index to the VALUE or zero if there is none.
//
static REBCNT Find_Map_Entry(
    REBMAP *map,
    REBVAL *key,
    REBVAL *val,
    REBOOL cased // case-sensitive if true
) {
    REBSER *hashlist = MAP_HASHLIST(map); // can be null
    REBARR *pairlist = MAP_PAIRLIST(map);
    REBCNT *hashes;
    REBCNT hash;
    REBVAL *v;
    REBCNT n;

    if (IS_NONE(key)) return 0;

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (ARRAY_LEN(pairlist) > SERIES_LEN(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    hash = Find_Key_Hashed(pairlist, hashlist, key, 2, cased, 0);
    hashes = SERIES_HEAD(REBCNT, hashlist);
    n = hashes[hash];

    // Just a GET of value:
    if (!val) return n;

    // Must set the value:
    if (n) {  // re-set it:
        *ARRAY_AT(pairlist, ((n - 1) * 2) + 1) = *val; // set it
        return n;
    }

    if (IS_NONE(val)) return 0; // trying to remove non-existing key

    // Create new entry:
    Append_Value(pairlist, key);
    Append_Value(pairlist, val);  // does not copy value, e.g. if string

    return (hashes[hash] = (ARRAY_LEN(pairlist) / 2));
}


//
//  Length_Map: C
//
REBINT Length_Map(REBMAP *map)
{
    REBCNT n, c = 0;
    REBVAL *v = ARRAY_HEAD(MAP_PAIRLIST(map));

    for (n = 0; !IS_END(v); n += 2, v += 2) {
        if (!IS_NONE(v + 1)) c++; // must have non-none value
    }

    assert(n == ARRAY_LEN(MAP_PAIRLIST(map)));

    return c;
}


//
//  PD_Map: C
//
REBINT PD_Map(REBPVS *pvs)
{
    REBVAL *data = pvs->value;
    REBVAL *val = 0;
    REBINT n = 0;

    if (IS_END(pvs->path+1)) val = pvs->setval;
    if (IS_NONE(pvs->select)) return PE_NONE;

    if (!ANY_WORD(pvs->select)
        && !ANY_BINSTR(pvs->select)
        && !IS_SCALAR(pvs->select)
        && !IS_OBJECT(pvs->select)
        && !IS_DATATYPE(pvs->select)
    ) return PE_BAD_SELECT;

    {
        const REBOOL cased = (val ? TRUE : FALSE); // cased when *setting*
        n = Find_Map_Entry(VAL_MAP(data), pvs->select, val, cased);
    }

    if (!n) return PE_NONE;

    FAIL_IF_LOCKED_SERIES(VAL_SERIES(data));
    pvs->value = VAL_ARRAY_AT_HEAD(data, ((n - 1) * 2) + 1);
    return PE_OK;
}


//
//  Append_Map: C
//
static void Append_Map(REBMAP *map, REBVAL *any_array, REBCNT len)
{
    REBVAL *value = VAL_ARRAY_AT(any_array);
    REBCNT n = 0;

    while (n < len && NOT_END(value) && NOT_END(value + 1)) {
        Find_Map_Entry(map, value, value + 1, TRUE);
        value += 2;
        n += 2;
    }
}


//
//  MT_Map: C
//
REBOOL MT_Map(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    REBCNT n;
    REBMAP *map;

    if (!IS_BLOCK(data) && !IS_MAP(data)) return FALSE;

    n = VAL_ARRAY_LEN_AT(data);
    if (n & 1) return FALSE;

    map = Make_Map(n / 2);

    Append_Map(map, data, UNKNOWN);

    Rehash_Map(map);

    Val_Init_Map(out, map);

    return TRUE;
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
    REBVAL *out;

    // Count number of set entries:
    //
    val = ARRAY_HEAD(MAP_PAIRLIST(map));
    for (; NOT_END(val) && NOT_END(val + 1); val += 2) {
        if (!IS_NONE(val + 1)) cnt++; // must have non-none value
    }

    // Copy entries to new block:
    //
    array = Make_Array(cnt * ((what == 0) ? 2 : 1));
    out = ARRAY_HEAD(array);
    val = ARRAY_HEAD(MAP_PAIRLIST(map));
    for (; NOT_END(val) && NOT_END(val+1); val += 2) {
        if (!IS_NONE(val+1)) {
            if (what <= 0) *out++ = val[0];
            if (what >= 0) *out++ = val[1];
        }
    }

    SET_END(out);
    SET_ARRAY_LEN(array, out - ARRAY_HEAD(array));
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
    REBCNT size = ARRAY_LEN(array);
    REBMAP *map;

    // See note above--can't have this array be accessible via some ANY-BLOCK!
    //
    assert(!ARRAY_GET_FLAG(array, OPT_SER_MANAGED));

    map = AS_MAP(array);

    MAP_HASHLIST(map) = Make_Hash_Sequence(size);

    Rehash_Map(map);
    return map;
}


//
//  Alloc_Context_From_Map: C
//
REBCON *Alloc_Context_From_Map(REBMAP *map)
{
    REBCNT cnt = 0;
    REBVAL *mval;

    REBCON *context;
    REBVAL *key;
    REBVAL *var;

    // Count number of set entries:
    mval = ARRAY_HEAD(MAP_PAIRLIST(map));
    for (; NOT_END(mval) && NOT_END(mval + 1); mval += 2) {
        if (ANY_WORD(mval) && !IS_NONE(mval + 1)) cnt++;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words
    context = Alloc_Context(cnt);
    key = CONTEXT_KEYS_HEAD(context);
    var = CONTEXT_VARS_HEAD(context);

    mval = ARRAY_HEAD(MAP_PAIRLIST(map));

    for (; NOT_END(mval) && NOT_END(mval + 1); mval += 2) {
        if (ANY_WORD(mval) && !IS_NONE(mval + 1)) {
            // !!! Used to leave SET_WORD typed values here... but why?
            // (Objects did not make use of the set-word vs. other distinctions
            // that function specs did.)
            Val_Init_Typeset(
                key,
                // all types except UNSET
                ~FLAGIT_KIND(REB_UNSET),
                VAL_WORD_SYM(mval)
            );
            key++;
            *var++ = mval[1];
        }
    }

    SET_END(key);
    SET_END(var);

    SET_ARRAY_LEN(CONTEXT_VARLIST(context), cnt + 1);
    SET_ARRAY_LEN(CONTEXT_KEYLIST(context), cnt + 1);

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
    REBMAP *map;
    REBCNT  args;

    if (action != A_MAKE && action != A_TO)
        map = VAL_MAP(val);

    // Check must be in this order (to avoid checking a non-series value);
    if (action >= A_TAKE && action <= A_SORT)
        FAIL_IF_LOCKED_ARRAY(MAP_PAIRLIST(map));

    switch (action) {

    case A_PICK:        // same as SELECT for MAP! datatype
    case A_SELECT:
        args = Find_Refines(call_, ALL_FIND_REFS);
        n = Find_Map_Entry(map, arg, 0, LOGICAL(args & AM_FIND_CASE));
        if (!n) return R_NONE;
        *D_OUT = *VAL_ARRAY_AT_HEAD(val, ((n-1)*2)+1);
        return R_OUT;

    case A_INSERT:
    case A_APPEND:
        if (!IS_BLOCK(arg)) fail (Error_Invalid_Arg(val));
        *D_OUT = *val;
        if (D_REF(AN_DUP)) {
            n = Int32(D_ARG(AN_COUNT));
            if (n <= 0) break;
        }
        Append_Map(map, arg, Partial1(arg, D_ARG(AN_LIMIT)));
        return R_OUT;

    case A_POKE:  // CHECK all pokes!!! to be sure they check args now !!!
        n = Find_Map_Entry(map, arg, D_ARG(3), TRUE);
        *D_OUT = *D_ARG(3);
        return R_OUT;

    case A_LENGTH:
        SET_INTEGER(D_OUT, Length_Map(map));
        return R_OUT;

    case A_MAKE:
    case A_TO:
        // make map! [word val word val]
        if (IS_BLOCK(arg) || IS_GROUP(arg) || IS_MAP(arg)) {
            if (MT_Map(D_OUT, arg, REB_MAP)) return R_OUT;
            fail (Error_Invalid_Arg(arg));
//      } else if (IS_NONE(arg)) {
//          n = 3; // just a start
        // make map! 10000
        } else if (IS_NUMBER(arg)) {
            if (action == A_TO) fail (Error_Invalid_Arg(arg));
            n = Int32s(arg, 0);
        }
        else
            fail (Error_Bad_Make(REB_MAP, Type_Of(arg)));

        // positive only
        map = Make_Map(n);
        Val_Init_Map(D_OUT, map);
        return R_OUT;

    case A_COPY:
        if (MT_Map(D_OUT, val, REB_MAP)) return R_OUT;
        fail (Error_Invalid_Arg(val));

    case A_CLEAR:
        Reset_Array(MAP_PAIRLIST(map));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        CLEAR(
            MAP_HASHLIST(map)->content.dynamic.data,
            SERIES_SPACE(MAP_HASHLIST(map))
        );
        TERM_SERIES(MAP_HASHLIST(map));

        Val_Init_Map(D_OUT, map);
        return R_OUT;

    case A_REFLECT: {
        REBARR *array;

        action = What_Reflector(arg); // zero on error

        // Adjust for compatibility with PICK:
        if (action == OF_VALUES)
            n = 1;
        else if (action == OF_WORDS)
            n = -1;
        else if (action == OF_BODY)
            n = 0;
        else
            fail (Error_Cannot_Reflect(REB_MAP, arg));

        array = Map_To_Array(map, n);
        Val_Init_Block(D_OUT, array);
        return R_OUT;
    }

    case A_TAIL_Q:
        return (Length_Map(map) == 0) ? R_TRUE : R_FALSE;
    }

    fail (Error_Illegal_Action(REB_MAP, action));
}


#if !defined(NDEBUG)

//
//  VAL_MAP_Ptr_Debug: C
//
// Debug-Only version of VAL_MAP() that makes sure you actually are getting
// a REBMAP out of a value initialized as type REB_MAP.
//
REBMAP **VAL_MAP_Ptr_Debug(const REBVAL *v) {
    assert(VAL_TYPE(v) == REB_MAP);
    assert(SERIES_GET_FLAG(VAL_SERIES(v), OPT_SER_ARRAY));

    // Note: hashlist may or may not be present

    return &AS_MAP(VAL_SERIES(v));
}

#endif
