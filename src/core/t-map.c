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
REBMAP *Make_Map(REBCNT capacity)
{
    REBARR *pairlist = Make_Array_Core(capacity * 2, ARRAY_FLAG_PAIRLIST);
    LINK(pairlist).hashlist = Make_Hash_Sequence(capacity);

    return MAP(pairlist);
}


static REBCTX *Error_Conflicting_Key(const RELVAL *key, REBSPC *specifier)
{
    DECLARE_LOCAL (specific);
    Derelativize(specific, key, specifier);
    return Error_Conflicting_Key_Raw(specific);
}

#define FOUND_SYNONYM \
    do { \
        if (synonym_slot != -1) /* another spelling already matched */ \
            fail (Error_Conflicting_Key(key, specifier)); \
        synonym_slot = slot; /* save and continue checking */ \
    } while (0)

#define FOUND_EXACT \
    do { \
        if (cased) \
            return slot; /* don't need to check synonyms, stop looking */ \
        FOUND_SYNONYM; /* need to confirm exact match is the only match */ \
    } while (0)


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
){
    // Hashlists store a indexes into the actual data array, of where the
    // first key corresponding to that hash is.  There may be more keys
    // indicated by that hash, vying for the same slot.  So the collisions
    // add a skip amount and keep trying:
    //
    // https://en.wikipedia.org/wiki/Linear_probing
    //
    // Len and skip are co-primes, so is guaranteed that repeatedly
    // adding skip (and subtracting len when needed) all positions are
    // visited.  1 <= skip < len, and len is prime, so this is guaranteed.
    //
    REBCNT len = SER_LEN(hashlist);
    REBCNT *indexes = SER_HEAD(REBCNT, hashlist);

    uint32_t hash = Hash_Value(key);
    REBCNT slot = hash % len; // first slot to try for this hash
    REBCNT skip = hash % (len - 1) + 1; // how much to skip by each collision

    // Zombie slots are those which are left behind by removing items, with
    // void values that are illegal in maps, and indicate they can be reused.
    //
    REBINT zombie_slot = -1; // no zombies seen yet...

    // You can store information case-insensitively in a MAP!, and it will
    // overwrite the value for at most one other key.  Reading information
    // case-insensitively out of a map can only be done if there aren't two
    // keys with the same spelling.
    //
    REBINT synonym_slot = -1; // no synonyms seen yet...

    if (ANY_WORD(key)) {
        REBCNT n;
        while ((n = indexes[slot]) != 0) {
            RELVAL *k = ARR_AT(array, (n - 1) * wide); // stored key
            if (ANY_WORD(k)) {
                if (VAL_WORD_SPELLING(key) == VAL_WORD_SPELLING(k))
                    FOUND_EXACT;
                else if (not cased)
                    if (VAL_WORD_CANON(key) == VAL_WORD_CANON(k))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && IS_VOID(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else if (ANY_BINSTR(key)) {
        REBCNT n;
        while ((n = indexes[slot]) != 0) {
            RELVAL *k = ARR_AT(array, (n - 1) * wide); // stored key
            if (VAL_TYPE(k) == VAL_TYPE(key)) {
                if (0 == Compare_String_Vals(k, key, FALSE))
                    FOUND_EXACT;
                else if (not cased and not IS_BINARY(key))
                    if (0 == Compare_String_Vals(k, key, TRUE))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && IS_VOID(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else {
        REBCNT n;
        while ((n = indexes[slot]) != 0) {
            RELVAL *k = ARR_AT(array, (n - 1) * wide); // stored key
            if (VAL_TYPE(k) == VAL_TYPE(key)) {
                if (0 == Cmp_Value(k, key, TRUE))
                    FOUND_EXACT;
                else if (not cased)
                    if (IS_CHAR(k) && 0 == Cmp_Value(k, key, FALSE))
                        FOUND_SYNONYM; // CHAR! is only non-STRING!/WORD! case
            }
            if (wide > 1 && IS_VOID(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }

    if (synonym_slot != -1) {
        assert(not cased);
        return synonym_slot; // there weren't other spellings of the same key
    }

    if (zombie_slot != -1) { // zombie encountered; overwrite with new key
        assert(mode == 0);
        slot = zombie_slot;
        REBCNT n = indexes[slot];
        Derelativize(ARR_AT(array, (n - 1) * wide), key, specifier);
    }

    if (mode > 1) { // append new value to the target series
        const RELVAL *src = key;
        indexes[slot] = (ARR_LEN(array) / wide) + 1;

        REBCNT index;
        for (index = 0; index < wide; ++src, ++index)
            Append_Value_Core(array, src, specifier);
    }

    return (mode > 0) ? -1 : cast(REBINT, slot);
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

    assert(NOT_SER_FLAG(ser, SERIES_FLAG_ARRAY));
    Remake_Series(
        ser,
        pnum + 1,
        SER_WIDE(ser),
        SERIES_FLAG_POWER_OF_2 // not(NODE_FLAG_NODE) => don't keep data
    );

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
REBCNT Find_Map_Entry(
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

    const REBCNT wide = 2;
    const REBYTE mode = 0; // just search for key, don't add it
    REBCNT slot = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, wide, cased, mode
    );

    REBCNT *indexes = SER_HEAD(REBCNT, hashlist);
    REBCNT n = indexes[slot];

    // n==0 or pairlist[(n-1)*]=~key

    if (val == NULL)
        return n; // was just fetching the value

    // If not just a GET, it may try to set the value in the map.  Which means
    // the key may need to be stored.  Since copies of keys are never made,
    // a SET must always be done with an immutable key...because if it were
    // changed, there'd be no notification to rehash the map.
    //
    REBSER *locker = SER(MAP_PAIRLIST(map));
    Ensure_Value_Immutable(key, locker);

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

    return (indexes[slot] = (ARR_LEN(pairlist) / 2));
}


//
//  PD_Map: C
//
REB_R PD_Map(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    assert(IS_MAP(pvs->out));

    if (opt_setval != NULL)
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(pvs->out));

    // Fetching and setting with path-based access is case-preserving for any
    // initial insertions.  However, the case-insensitivity means that all
    // writes after that to the same key will not be overriding the key,
    // it will just change the data value for the existing key.  SELECT and
    // the operation tentatively named PUT should be used if a map is to
    // distinguish multiple casings of the same key.
    //
    const REBOOL cased = FALSE;

    REBINT n = Find_Map_Entry(
        VAL_MAP(pvs->out),
        picker,
        SPECIFIED,
        opt_setval,
        SPECIFIED,
        cased
    );

    if (opt_setval != NULL) {
        assert(n != 0);
        return R_INVISIBLE;
    }

    if (n == 0)
        return R_VOID;

    REBVAL *val = KNOWN(
        ARR_AT(MAP_PAIRLIST(VAL_MAP(pvs->out)), ((n - 1) * 2) + 1)
    );
    if (IS_VOID(val)) // zombie entry, means unused
        return R_VOID;

    Move_Value(pvs->out, val);
    return R_OUT;
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


inline static REBMAP *Copy_Map(REBMAP *map, REBU64 types) {
    REBARR *copy = Copy_Array_Shallow(MAP_PAIRLIST(map), SPECIFIED);
    SET_SER_FLAG(copy, ARRAY_FLAG_PAIRLIST);

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    LINK(copy).hashlist = Copy_Sequence(MAP_HASHLIST(map));

    if (types == 0)
        return MAP(copy); // no types have deep copy requested, shallow is OK

    // Even if the type flags request deep copies of series, none of the keys
    // need to be copied deeply.  This is because they are immutable at the
    // time of insertion.
    //
    assert(ARR_LEN(copy) % 2 == 0); // should be [key value key value]...

    RELVAL *key = ARR_HEAD(copy);
    for (; NOT_END(key); key += 2) {
        assert(Is_Value_Immutable(key)); // immutable key

        RELVAL *v = key + 1;
        if (IS_VOID(v))
            continue; // "zombie" map element (not present)

        // No plain Clonify_Value() yet, call on values with length of 1.
        //
        Clonify_Values_Len_Managed(v, SPECIFIED, 1, SERIES_MASK_NONE, types);
    }

    return MAP(copy);
}


//
//  TO_Map: C
//
void TO_Map(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_MAP);
    UNUSED(kind);

    if (IS_BLOCK(arg) || IS_GROUP(arg)) {
        //
        // make map! [word val word val]
        //
        REBARR* array = VAL_ARRAY(arg);
        REBCNT len = VAL_ARRAY_LEN_AT(arg);
        REBCNT index = VAL_INDEX(arg);
        REBSPC *specifier = VAL_SPECIFIER(arg);

        REBMAP *map = Make_Map(len / 2); // [key value key value...] + END
        Append_Map(map, array, index, specifier, len);
        Rehash_Map(map);
        Init_Map(out, map);
    }
    else if (IS_MAP(arg)) {
        //
        // Values are not copied deeply by default.
        //
        // !!! Is there really a use in allowing MAP! to be converted TO a
        // MAP! as opposed to having people COPY it?
        //
        REBU64 types = 0;

        Init_Map(out, Copy_Map(VAL_MAP(arg), types));
    }
    else
        fail (Error_Invalid(arg));
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
    assert(not IS_ARRAY_MANAGED(a));

    SET_SER_FLAG(a, ARRAY_FLAG_PAIRLIST);

    REBMAP *map = MAP(a);
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
//  MF_Map: C
//
void MF_Map(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBMAP *m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Series(TG_Mold_Stack, m) != NOT_FOUND) {
        Append_Unencoded(mo->series, "...]");
        return;
    }

    Push_Pointer_To_Series(TG_Mold_Stack, m);

    if (not form) {
        Pre_Mold(mo, v);
        Append_Utf8_Codepoint(mo->series, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    RELVAL *key = ARR_HEAD(MAP_PAIRLIST(m));
    for (; NOT_END(key); key += 2) {
        assert(NOT_END(key + 1)); // value slot must not be END
        if (IS_VOID(key + 1))
            continue; // if value for this key is void, key has been removed

        if (not form)
            New_Indented_Line(mo);
        Emit(mo, "V V", key, key + 1);
        if (form)
            Append_Utf8_Codepoint(mo->series, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Utf8_Codepoint(mo->series, ']');
    }

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, m);
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

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `val`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            Init_Integer(D_OUT, Length_Map(map));
            return R_OUT;

        case SYM_VALUES:
            Init_Block(D_OUT, Map_To_Array(map, 1));
            return R_OUT;

        case SYM_WORDS:
            Init_Block(D_OUT, Map_To_Array(map, -1));
            return R_OUT;

        case SYM_BODY:
            Init_Block(D_OUT, Map_To_Array(map, 0));
            return R_OUT;

        case SYM_TAIL_Q:
            return R_FROM_BOOL(Length_Map(map) == 0);

        default:
            break;
        }

        fail (Error_Cannot_Reflect(REB_MAP, arg)); }

    case SYM_FIND:
    case SYM_SELECT: {
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
            return verb == SYM_FIND ? R_BLANK : R_VOID;

        Move_Value(
            D_OUT,
            KNOWN(ARR_AT(MAP_PAIRLIST(map), ((n - 1) * 2) + 1))
        );

        if (verb == SYM_FIND)
            return IS_VOID(D_OUT) ? R_BLANK : R_BAR;

        return R_OUT; }

    case SYM_PUT: {
        INCLUDE_PARAMS_OF_PUT;
        UNUSED(ARG(series)); // extracted to `map`

        REBINT n = Find_Map_Entry(
            map,
            ARG(key),
            SPECIFIED,
            ARG(value),
            SPECIFIED,
            REF(case)
        );
        UNUSED(n);

        Move_Value(D_OUT, ARG(value));
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
            fail (Error_Invalid(val));
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
        if (not REF(map))
            fail (Error_Illegal_Action(REB_MAP, verb));

        Move_Value(D_OUT, val);
        Find_Map_Entry(
            map, ARG(key), SPECIFIED, VOID_CELL, SPECIFIED, TRUE
        );
        return R_OUT; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }

        REBU64 types = 0; // which types to copy non-"shallowly"

        if (REF(deep))
            types |= REF(types) ? 0 : TS_CLONE;

        if (REF(types)) {
            if (IS_DATATYPE(ARG(kinds)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(kinds)));
            else
                types |= VAL_TYPESET_BITS(ARG(kinds));
        }

        Init_Map(D_OUT, Copy_Map(map, types));
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

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_MAP, verb));
}
