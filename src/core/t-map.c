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
REBINT CT_Map(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode < 0) return -1;
    return 0 == Cmp_Array(a, b, false);
}


//
//  Make_Map: C
//
// Makes a MAP block (that holds both keys and values).
// Capacity is measured in key-value pairings.
// A hash series is also created.
//
REBMAP *Make_Map(REBLEN capacity)
{
    REBARR *pairlist = Make_Arr_Core(capacity * 2, ARRAY_FLAG_PAIRLIST);
    LINK(pairlist).hashlist = Make_Hash_Sequence(capacity);

    return MAP(pairlist);
}


static REBCTX *Error_Conflicting_Key(const Cell* key, REBSPC *specifier)
{
    DECLARE_VALUE (specific);
    Derelativize(specific, key, specifier);
    return Error_Conflicting_Key_Raw(specific);
}

#define FOUND_SYNONYM \
    do { \
        if (synonym_slot != -1) /* another symbol already matched */ \
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
    const Cell* key, // !!! assumes key is followed by value(s) via ++
    REBSPC *specifier,
    REBLEN wide,
    bool cased,
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
    REBLEN len = SER_LEN(hashlist);
    REBLEN *indexes = SER_HEAD(REBLEN, hashlist);

    uint32_t hash = Hash_Value(key);
    REBLEN slot = hash % len; // first slot to try for this hash
    REBLEN skip = hash % (len - 1) + 1; // how much to skip by each collision

    // Zombie slots are those which are left behind by removing items, with
    // void values that are illegal in maps, and indicate they can be reused.
    //
    REBINT zombie_slot = -1; // no zombies seen yet...

    // You can store information case-insensitively in a MAP!, and it will
    // overwrite the value for at most one other key.  Reading information
    // case-insensitively out of a map can only be done if there aren't two
    // keys which are synonyms.
    //
    REBINT synonym_slot = -1; // no synonyms seen yet...

    if (ANY_WORD(key)) {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = ARR_AT(array, (n - 1) * wide); // stored key
            if (ANY_WORD(k)) {
                if (Cell_Word_Symbol(key) == Cell_Word_Symbol(k))
                    FOUND_EXACT;
                else if (not cased)
                    if (VAL_WORD_CANON(key) == VAL_WORD_CANON(k))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && IS_NULLED(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else if (ANY_BINSTR(key)) {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = ARR_AT(array, (n - 1) * wide); // stored key
            if (VAL_TYPE(k) == VAL_TYPE(key)) {
                if (0 == Compare_String_Vals(k, key, false))
                    FOUND_EXACT;
                else if (not cased and not IS_BINARY(key))
                    if (0 == Compare_String_Vals(k, key, true))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && IS_NULLED(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = ARR_AT(array, (n - 1) * wide); // stored key
            if (VAL_TYPE(k) == VAL_TYPE(key)) {
                if (0 == Cmp_Value(k, key, true))
                    FOUND_EXACT;
                else if (not cased)
                    if (IS_CHAR(k) && 0 == Cmp_Value(k, key, false))
                        FOUND_SYNONYM; // CHAR! is only non-STRING!/WORD! case
            }
            if (wide > 1 && IS_NULLED(k + 1) && zombie_slot == -1)
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
        REBLEN n = indexes[slot];
        Derelativize(ARR_AT(array, (n - 1) * wide), key, specifier);
    }

    if (mode > 1) { // append new value to the target series
        const Cell* src = key;
        indexes[slot] = (ARR_LEN(array) / wide) + 1;

        REBLEN index;
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

    REBLEN *hashes = SER_HEAD(REBLEN, hashlist);
    REBARR *pairlist = MAP_PAIRLIST(map);

    Value* key = KNOWN(ARR_HEAD(pairlist));
    REBLEN n;

    for (n = 0; n < ARR_LEN(pairlist); n += 2, key += 2) {
        const bool cased = true; // cased=true is always fine

        if (IS_NULLED(key + 1)) {
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

        REBLEN hash = Find_Key_Hashed(
            pairlist, hashlist, key, SPECIFIED, 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (IS_NULLED(ARR_AT(pairlist, ARR_LEN(pairlist) - 1))) {
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
        DECLARE_VALUE (temp);
        Init_Integer(temp, SER_LEN(ser) + 1);
        fail (Error_Size_Limit_Raw(temp));
    }

    assert(not IS_SER_ARRAY(ser));
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
REBLEN Find_Map_Entry(
    REBMAP *map,
    const Cell* key,
    REBSPC *key_specifier,
    const Cell* val,
    REBSPC *val_specifier,
    bool cased // case-sensitive if true
) {
    assert(not IS_NULLED(key));

    REBSER *hashlist = MAP_HASHLIST(map); // can be null
    REBARR *pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (ARR_LEN(pairlist) > SER_LEN(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    const REBLEN wide = 2;
    const REBYTE mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, wide, cased, mode
    );

    REBLEN *indexes = SER_HEAD(REBLEN, hashlist);
    REBLEN n = indexes[slot];

    // n==0 or pairlist[(n-1)*]=~key

    if (val == nullptr)
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

    if (IS_NULLED(val)) return 0; // trying to remove non-existing key

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
REB_R PD_Map(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    assert(IS_MAP(pvs->out));

    if (opt_setval != nullptr)
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(pvs->out));

    // Fetching and setting with path-based access is case-preserving for any
    // initial insertions.  However, the case-insensitivity means that all
    // writes after that to the same key will not be overriding the key,
    // it will just change the data value for the existing key.  SELECT and
    // the operation tentatively named PUT should be used if a map is to
    // distinguish multiple casings of the same key.
    //
    const bool cased = false;

    REBINT n = Find_Map_Entry(
        VAL_MAP(pvs->out),
        picker,
        SPECIFIED,
        opt_setval,
        SPECIFIED,
        cased
    );

    if (opt_setval != nullptr) {
        assert(n != 0);
        return R_INVISIBLE;
    }

    if (n == 0)
        return nullptr;

    Value* val = KNOWN(
        ARR_AT(MAP_PAIRLIST(VAL_MAP(pvs->out)), ((n - 1) * 2) + 1)
    );
    if (IS_NULLED(val)) // zombie entry, means unused
        return nullptr;

    return Move_Value(pvs->out, val); // RETURN (...) uses `frame_`, not `pvs`
}


//
//  Append_Map: C
//
static void Append_Map(
    REBMAP *map,
    REBARR *array,
    REBLEN index,
    REBSPC *specifier,
    REBLEN len
) {
    Cell* item = ARR_AT(array, index);
    REBLEN n = 0;

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
            true
        );

        item += 2;
        n += 2;
    }
}


//
//  MAKE_Map: C
//
REB_R MAKE_Map(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (ANY_NUMBER(arg)) {
        return Init_Map(out, Make_Map(Int32s(arg, 0)));
    }
    else {
        // !!! R3-Alpha TO of MAP! was like MAKE but wouldn't accept just
        // being given a size.
        //
        return TO_Map(out, kind, arg);
    }
}


INLINE REBMAP *Copy_Map(REBMAP *map, REBU64 types) {
    REBARR *copy = Copy_Array_Shallow(MAP_PAIRLIST(map), SPECIFIED);
    SET_SER_FLAG(copy, ARRAY_FLAG_PAIRLIST);

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    LINK(copy).hashlist = Copy_Sequence_Core(
        MAP_HASHLIST(map),
        SERIES_FLAGS_NONE // !!! No NODE_FLAG_MANAGED?
    );

    if (types == 0)
        return MAP(copy); // no types have deep copy requested, shallow is OK

    // Even if the type flags request deep copies of series, none of the keys
    // need to be copied deeply.  This is because they are immutable at the
    // time of insertion.
    //
    assert(ARR_LEN(copy) % 2 == 0); // should be [key value key value]...

    Cell* key = ARR_HEAD(copy);
    for (; NOT_END(key); key += 2) {
        assert(Is_Value_Immutable(key)); // immutable key

        Cell* v = key + 1;
        if (IS_NULLED(v))
            continue; // "zombie" map element (not present)

        // No plain Clonify_Value() yet, call on values with length of 1.
        //
        Clonify_Values_Len_Managed(v, SPECIFIED, 1, types);
    }

    return MAP(copy);
}


//
//  TO_Map: C
//
REB_R TO_Map(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_MAP);
    UNUSED(kind);

    if (IS_BLOCK(arg) || IS_GROUP(arg)) {
        //
        // make map! [word val word val]
        //
        REBARR* array = VAL_ARRAY(arg);
        REBLEN len = VAL_ARRAY_LEN_AT(arg);
        REBLEN index = VAL_INDEX(arg);
        REBSPC *specifier = VAL_SPECIFIER(arg);

        REBMAP *map = Make_Map(len / 2); // [key value key value...] + END
        Append_Map(map, array, index, specifier, len);
        Rehash_Map(map);
        return Init_Map(out, map);
    }
    else if (IS_MAP(arg)) {
        //
        // Values are not copied deeply by default.
        //
        // !!! Is there really a use in allowing MAP! to be converted TO a
        // MAP! as opposed to having people COPY it?
        //
        REBU64 types = 0;

        return Init_Map(out, Copy_Map(VAL_MAP(arg), types));
    }

    fail (Error_Invalid(arg));
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
REBARR *Map_To_Array(REBMAP *map, REBINT what)
{
    REBLEN count = Length_Map(map);
    REBARR *a = Make_Arr(count * ((what == 0) ? 2 : 1));

    Value* dest = KNOWN(ARR_HEAD(a));
    Value* val = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(val); val += 2) {
        assert(NOT_END(val + 1));
        if (not IS_NULLED(val + 1)) {
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

    TERM_ARRAY_LEN(a, cast(Cell*, dest) - ARR_HEAD(a));
    assert(IS_END(dest));
    return a;
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

    Value* mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));
    REBLEN count = 0;

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (ANY_WORD(mval) and not IS_NULLED(mval + 1))
            ++count;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words

    REBCTX *context = Alloc_Context(REB_OBJECT, count);
    Value* key = CTX_KEYS_HEAD(context);
    Value* var = CTX_VARS_HEAD(context);

    mval = KNOWN(ARR_HEAD(MAP_PAIRLIST(map)));

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (ANY_WORD(mval) and not IS_NULLED(mval + 1)) {
            // !!! Used to leave SET_WORD typed values here... but why?
            // (Objects did not make use of the set-word vs. other distinctions
            // that function specs did.)
            Init_Typeset(
                key,
                TS_OPT_VALUE, // !!! Not used at the moment
                Cell_Word_Symbol(mval)
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
void MF_Map(REB_MOLD *mo, const Cell* v, bool form)
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

    Cell* key = ARR_HEAD(MAP_PAIRLIST(m));
    for (; NOT_END(key); key += 2) {
        assert(NOT_END(key + 1)); // value slot must not be END
        if (IS_NULLED(key + 1))
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
    Value* val = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    REBMAP *map = VAL_MAP(val);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `val`
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(D_OUT, Length_Map(map));

        case SYM_VALUES:
            return Init_Block(D_OUT, Map_To_Array(map, 1));

        case SYM_WORDS:
            return Init_Block(D_OUT, Map_To_Array(map, -1));

        case SYM_BODY:
            return Init_Block(D_OUT, Map_To_Array(map, 0));

        case SYM_TAIL_Q:
            return Init_Logic(D_OUT, Length_Map(map) == 0);

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
            nullptr,
            SPECIFIED,
            REF(case)
        );

        if (n == 0)
            return nullptr;

        Move_Value(
            D_OUT,
            KNOWN(ARR_AT(MAP_PAIRLIST(map), ((n - 1) * 2) + 1))
        );

        if (Cell_Word_Id(verb) == SYM_FIND)
            return IS_NULLED(D_OUT) ? nullptr : Init_Bar(D_OUT);

        return D_OUT; }

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

        RETURN (ARG(value)); }

    case SYM_INSERT:
    case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;

        if (IS_NULLED_OR_BLANK(arg))
            RETURN (val); // don't fail on read only if it would be a no-op

        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // handled as arg

        if (REF(only))
            fail (Error_Bad_Refines_Raw());
        if (REF(line))
            fail (Error_Bad_Refines_Raw());
        if (REF(dup)) {
            UNUSED(ARG(count));
            fail (Error_Bad_Refines_Raw());
        }

        if (not IS_BLOCK(arg))
            fail (Error_Invalid(arg));

        REBLEN len = Part_Len_May_Modify_Index(arg, ARG(limit));
        UNUSED(REF(part)); // detected by if limit is nulled

        Append_Map(
            map,
            VAL_ARRAY(arg),
            VAL_INDEX(arg),
            VAL_SPECIFIER(arg),
            len
        );

        return Init_Map(D_OUT, map); }

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
            map, ARG(key), SPECIFIED, NULLED_CELL, SPECIFIED, true
        );
        return D_OUT; }

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

        return Init_Map(D_OUT, Copy_Map(map, types)); }

    case SYM_CLEAR:
        FAIL_IF_READ_ONLY_ARRAY(MAP_PAIRLIST(map));

        Reset_Array(MAP_PAIRLIST(map));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Series(MAP_HASHLIST(map));

        return Init_Map(D_OUT, map);

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_MAP, verb));
}
