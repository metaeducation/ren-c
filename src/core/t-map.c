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
// See %sys-map.h for an explanation of the map structure.
//

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
    REBARR *pairlist = Make_Array_Core(capacity * 2, ARRAY_FLAG_PAIRLIST);
    AS_SERIES(pairlist)->link.hashlist = Make_Hash_Sequence(capacity);

    return AS_MAP(pairlist);
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
    REBSPC *specifier,
    REBCNT wide,
    REBOOL cased,
    REBYTE mode
) {
    REBCNT len = SER_LEN(hashlist);
    assert(len > 0);

    REBCNT hash = Hash_Value(key);

    // The REBCNT[] hash array size is chosen to try and make a large enough
    // table relative to the data that collisions will be hopefully not
    // frequent.  But they may still collide.  The method R3-Alpha chose to
    // deal with collisions was to have a "skip" amount that will go try
    // another hash bucket until the searched for key is found or a 0
    // entry in the hashlist is found.
    //
    // Note: if len and skip are co-primes is guaranteed that repeatedly
    // adding skip (and subtracting len when needed) all positions are
    // visited.  1 <= skip < len, and len is prime, so this is guaranteed.

    REBCNT skip = hash % (len - 1) + 1;

    hash = hash % len;

    // a 'zombie' is a key with void value, that may be overwritten.  Set to
    // len to indicate zombie not yet encountered.
    //
    REBCNT zombie = len;

    REBCNT uncased = len; // uncased match not yet encountered

    // Scan hash table for match:

    REBCNT *hashes = SER_HEAD(REBCNT, hashlist);
    REBCNT n;
    RELVAL *val;

    if (ANY_WORD(key)) {
        while ((n = hashes[hash])) {
            val = ARR_AT(array, (n - 1) * wide);
            if (ANY_WORD(val) && VAL_WORD_SPELLING(key) == VAL_WORD_SPELLING(val))
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
    REBSER *hashlist = MAP_HASHLIST(map);

    if (!hashlist) return;

    REBCNT *hashes = SER_HEAD(REBCNT, hashlist);
    REBARR *pairlist = MAP_PAIRLIST(map);

    REBVAL *key = KNOWN(ARR_HEAD(pairlist));
    REBCNT n;

    for (n = 0; n < ARR_LEN(pairlist); n += 2, key += 2) {
        const REBOOL cased = TRUE; // cased=TRUE is always fine

        if (IS_VOID(key + 1)) {
            //
            // It's a "zombie", move last key to overwrite it
            //
            Move_Value(
                key, KNOWN(ARR_AT(pairlist, ARR_LEN(pairlist) - 2))
            );
            Move_Value(
                &key[1], KNOWN(ARR_AT(pairlist, ARR_LEN(pairlist) - 1))
            );
            SET_ARRAY_LEN_NOTERM(pairlist, ARR_LEN(pairlist) - 2);
        }

        REBCNT hash = Find_Key_Hashed(
            pairlist, hashlist, key, SPECIFIED, 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (IS_VOID(ARR_AT(pairlist, ARR_LEN(pairlist) - 1))) {
            SET_ARRAY_LEN_NOTERM(pairlist, ARR_LEN(pairlist) - 2);
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
    if (pnum == 0) {
        DECLARE_LOCAL (temp);
        Init_Integer(temp, SER_LEN(ser) + 1);
        fail (Error_Size_Limit_Raw(temp));
    }

    assert(Not_Ser_Flag(ser, SERIES_FLAG_ARRAY));
    Remake_Series(ser, pnum + 1, SER_WIDE(ser), SERIES_FLAG_POWER_OF_2);

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
    REBSPC *key_specifier,
    const RELVAL *val,
    REBSPC *val_specifier,
    REBOOL cased // case-sensitive if true
) {
    assert(!IS_VOID(key));

    REBSER *hashlist = MAP_HASHLIST(map); // can be null
    REBARR *pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (ARR_LEN(pairlist) > SER_LEN(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    REBCNT hash = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, 2, cased, 0
    );

    REBCNT *hashes = SER_HEAD(REBCNT, hashlist);
    REBCNT n = hashes[hash];

    // n==0 or pairlist[(n-1)*]=~key

    // Just a GET of value:
    if (!val) return n;

    // If not just a GET, it may try to set the value in the map.  Which means
    // the key may need to be stored.  Since copies of keys are never made,
    // a SET must always be done with an immutable key...because if it were
    // changed, there'd be no notification to rehash the map.
    //
    if (!Is_Value_Immutable(key))
        fail (Error_Map_Key_Unlocked_Raw(key));

    // Must set the value:
    if (n) {  // re-set it:
        Derelativize(
            ARR_AT(pairlist, ((n - 1) * 2) + 1),
            val,
            val_specifier
        );
        return n;
    }

    if (IS_VOID(val)) return 0; // trying to remove non-existing key

    // Create new entry.  Note that it does not copy underlying series (e.g.
    // the data of a string), which is why the immutability test is necessary
    //
    Append_Value_Core(pairlist, key, key_specifier);
    Append_Value_Core(pairlist, val, val_specifier);

    return (hashes[hash] = (ARR_LEN(pairlist) / 2));
}


//
//  PD_Map: C
//
REBINT PD_Map(REBPVS *pvs)
{
    REBOOL setting = LOGICAL(pvs->opt_setval && IS_END(pvs->item + 1));

    assert(IS_MAP(pvs->value));

    if (setting)
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(pvs->value));

    REBINT n = Find_Map_Entry(
        VAL_MAP(pvs->value),
        pvs->selector,
        SPECIFIED,
        setting ? pvs->opt_setval : NULL,
        SPECIFIED,
        setting // `cased` flag for case-sensitivity--use when setting only
    );

    if (n == 0) {
        Init_Void(pvs->store);
        return PE_USE_STORE;
    }

    REBVAL *val = KNOWN(
        ARR_AT(MAP_PAIRLIST(VAL_MAP(pvs->value)), ((n - 1) * 2) + 1)
    );
    if (IS_VOID(val)) {
        Init_Void(pvs->store);
        return PE_USE_STORE;
    }

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
    REBSPC *specifier,
    REBCNT len
) {
    RELVAL *item = ARR_AT(array, index);
    REBCNT n = 0;

    while (n < len && NOT_END(item)) {
        if (IS_END(item + 1)) {
            //
            // Keys with no value not allowed, e.g. `make map! [1 "foo" 2]`
            //
            fail (Error_Past_End_Raw());
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
        Init_Map(out, map);
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
    assert(kind == REB_MAP);
    UNUSED(kind);

    REBARR* array;
    REBCNT len;
    REBCNT index;
    REBSPC *specifier;

    if (IS_BLOCK(arg) || IS_GROUP(arg)) {
        //
        // make map! [word val word val]
        //
        array = VAL_ARRAY(arg);
        index = VAL_INDEX(arg);
        len = VAL_ARRAY_LEN_AT(arg);
        specifier = VAL_SPECIFIER(arg);
    }
    else if (IS_MAP(arg)) {
        array = MAP_PAIRLIST(VAL_MAP(arg));
        index = 0;// maps don't have an index/"position"
        len = ARR_LEN(array);
        specifier = SPECIFIED; // there should be no relative values in a MAP!
    }
    else
        fail (arg);

    REBMAP *map = Make_Map(len / 2); // [key value key value...] + END
    Append_Map(map, array, index, specifier, len);
    Rehash_Map(map);
    Init_Map(out, map);
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
REBARR *Map_To_Array(REBMAP *map, REBINT what)
{
    REBCNT count = Length_Map(map);

    // Copy entries to new block:
    //
    REBARR *array = Make_Array(count * ((what == 0) ? 2 : 1));
    REBVAL *dest = SINK(ARR_HEAD(array));
    REBVAL *val = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(val); val += 2) {
        assert(NOT_END(val + 1));
        if (!IS_VOID(val + 1)) {
            if (what <= 0) {
                Move_Value(dest, &val[0]);
                ++dest;
            }
            if (what >= 0) {
                Move_Value(dest, &val[1]);
                ++dest;
            }
        }
    }

    TERM_ARRAY_LEN(array, cast(RELVAL*, dest) - ARR_HEAD(array));
    assert(IS_END(dest));
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
REBMAP *Mutate_Array_Into_Map(REBARR *a)
{
    REBCNT size = ARR_LEN(a);

    // See note above--can't have this array be accessible via some ANY-BLOCK!
    //
    assert(NOT(IS_ARRAY_MANAGED(a)));

    Set_Ser_Flag(a, ARRAY_FLAG_PAIRLIST);

    REBMAP *map = AS_MAP(a);
    MAP_HASHLIST(map) = Make_Hash_Sequence(size);

    Rehash_Map(map);
    return map;
}


//
//  Alloc_Context_From_Map: C
//
REBCTX *Alloc_Context_From_Map(REBMAP *map)
{
    // Doesn't use Length_Map because it only wants to consider words.
    //
    // !!! Should this fail() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! make map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    REBVAL *mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    REBCNT count = 0;

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (ANY_WORD(mval) && !IS_VOID(mval + 1))
            ++count;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words

    REBCTX *context = Alloc_Context(REB_OBJECT, count);
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);

    mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (ANY_WORD(mval) && !IS_VOID(mval + 1)) {
            // !!! Used to leave SET_WORD typed values here... but why?
            // (Objects did not make use of the set-word vs. other distinctions
            // that function specs did.)
            Init_Typeset(
                key,
                // all types except void
                ~FLAGIT_KIND(REB_MAX_VOID),
                VAL_WORD_SPELLING(mval)
            );
            ++key;
            Move_Value(var, &mval[1]);
            ++var;
        }
    }

    TERM_ARRAY_LEN(CTX_VARLIST(context), count + 1);
    TERM_ARRAY_LEN(CTX_KEYLIST(context), count + 1);
    assert(IS_END(key));
    assert(IS_END(var));

    return context;
}


//
//  REBTYPE: C
//
REBTYPE(Map)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBMAP *map = VAL_MAP(val);
    REBCNT tail;

    switch (action) {

    case SYM_PICK_P:
        Pick_Block(D_OUT, val, arg);
        if (IS_VOID(D_OUT)) return R_BLANK;
        return R_OUT;

    case SYM_FIND:
    case SYM_SELECT_P: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // handled as `arg`

        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(only))
            fail (Error_Bad_Refines_Raw());
        if (REF(skip)) {
            UNUSED(ARG(size));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(last))
            fail (Error_Bad_Refines_Raw());
        if (REF(reverse))
            fail (Error_Bad_Refines_Raw());
        if (REF(tail))
            fail (Error_Bad_Refines_Raw());
        if (REF(match))
            fail (Error_Bad_Refines_Raw());

        REBINT n = Find_Map_Entry(
            map,
            arg,
            SPECIFIED,
            NULL,
            SPECIFIED,
            REF(case)
        );

        if (n == 0)
            return action == SYM_FIND ? R_FALSE : R_VOID;

        Move_Value(
            D_OUT,
            KNOWN(ARR_AT(MAP_PAIRLIST(map), ((n - 1) * 2) + 1))
        );

        if (action == SYM_FIND)
            return IS_VOID(D_OUT) ? R_FALSE : R_TRUE;

        return R_OUT; }

    case SYM_INSERT:
    case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;

        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // handled as arg

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (!IS_BLOCK(arg))
            fail (val);
        Move_Value(D_OUT, val);
        if (REF(dup)) {
            if (Int32(ARG(count)) <= 0) break;
        }

        UNUSED(REF(part));
        Partial1(arg, ARG(limit), &tail);
        Append_Map(
            map,
            VAL_ARRAY(arg),
            VAL_INDEX(arg),
            VAL_SPECIFIER(arg),
            tail
        );
        return R_OUT; }

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        UNUSED(PAR(series));

        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (NOT(REF(map)))
            fail (Error_Illegal_Action(REB_MAP, action));

        Move_Value(D_OUT, val);
        Find_Map_Entry(
            map, ARG(key), SPECIFIED, VOID_CELL, SPECIFIED, TRUE
        );
        return R_OUT; }

    case SYM_POKE: { // CHECK all pokes!!! to be sure they check args now !!!
        INCLUDE_PARAMS_OF_POKE;

        UNUSED(ARG(series)); // map
        UNUSED(ARG(index)); // arg

        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        const REBOOL cased = TRUE;
        Find_Map_Entry(
            map,
            arg, // key
            SPECIFIED,
            ARG(value), // value... since non-void, inserted if not there
            SPECIFIED,
            cased
        );
        Move_Value(D_OUT, ARG(value));
        return R_OUT; }

    case SYM_LENGTH:
        Init_Integer(D_OUT, Length_Map(map));
        return R_OUT;

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        // !!! the copying map case should probably not be a MAKE case, but
        // implemented here as copy.
        //
        MAKE_Map(D_OUT, REB_MAP, val); // may fail()
        return R_OUT; }

    case SYM_CLEAR:
        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        Reset_Array(MAP_PAIRLIST(map));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Series(MAP_HASHLIST(map));

        Init_Map(D_OUT, map);
        return R_OUT;

    case SYM_REFLECT: {
        REBSYM sym = VAL_WORD_SYM(arg);

        REBINT n;
        if (sym == SYM_VALUES)
            n = 1;
        else if (sym == SYM_WORDS)
            n = -1;
        else if (sym == SYM_BODY)
            n = 0;
        else
            fail (Error_Cannot_Reflect(REB_MAP, arg));

        REBARR *array = Map_To_Array(map, n);
        Init_Block(D_OUT, array);
        return R_OUT;
    }

    case SYM_TAIL_Q:
        return (Length_Map(map) == 0) ? R_TRUE : R_FALSE;

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_MAP, action));
}
