//
//  file: %t-map.c
//  summary: "map datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    Array* pairlist = Make_Array_Core(capacity * 2, ARRAY_FLAG_IS_PAIRLIST);
    LINK(pairlist).hashlist = Make_Hash_Sequence(capacity);

    return MAP(pairlist);
}


static Error* Error_Conflicting_Key(const Cell* key, Specifier* specifier)
{
    DECLARE_VALUE (specific);
    Derelativize(specific, key, specifier);
    return Error_Conflicting_Key_Raw(specific);
}

#define FOUND_SYNONYM \
    do { \
        if (synonym_slot != -1) /* another symbol already matched */ \
            panic (Error_Conflicting_Key(key, specifier)); \
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
    Array* array,
    Flex* hashlist,
    const Cell* key, // !!! assumes key is followed by value(s) via ++
    Specifier* specifier,
    REBLEN wide,
    bool cased,
    Byte mode
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
    REBLEN len = Flex_Len(hashlist);
    REBLEN *indexes = Flex_Head(REBLEN, hashlist);

    uint32_t hash = Hash_Value(key);
    REBLEN slot = hash % len; // first slot to try for this hash
    REBLEN skip = hash % (len - 1) + 1; // how much to skip by each collision

    // Zombie slots are those which are left behind by removing items, with
    // null values that are illegal in maps, and indicate they can be reused.
    //
    REBINT zombie_slot = -1; // no zombies seen yet...

    // You can store information case-insensitively in a MAP!, and it will
    // overwrite the value for at most one other key.  Reading information
    // case-insensitively out of a map can only be done if there aren't two
    // keys which are synonyms.
    //
    REBINT synonym_slot = -1; // no synonyms seen yet...

    if (Any_Word(key)) {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = Array_At(array, (n - 1) * wide); // stored key
            if (Any_Word(k)) {
                if (Word_Symbol(key) == Word_Symbol(k))
                    FOUND_EXACT;
                else if (not cased)
                    if (VAL_WORD_CANON(key) == VAL_WORD_CANON(k))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else if (Is_Binary(key) or Any_String(key)) {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = Array_At(array, (n - 1) * wide); // stored key
            if (Type_Of(k) == Type_Of(key)) {
                if (0 == Compare_String_Vals(k, key, false))
                    FOUND_EXACT;
                else if (not cased and not Is_Binary(key))
                    if (0 == Compare_String_Vals(k, key, true))
                        FOUND_SYNONYM;
            }
            if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
                zombie_slot = slot;

            slot += skip;
            if (slot >= len)
                slot -= len;
        }
    }
    else {
        REBLEN n;
        while ((n = indexes[slot]) != 0) {
            Cell* k = Array_At(array, (n - 1) * wide); // stored key
            if (Type_Of(k) == Type_Of(key)) {
                if (0 == Cmp_Value(k, key, true))
                    FOUND_EXACT;
                else if (not cased)
                    if (Is_Char(k) && 0 == Cmp_Value(k, key, false))
                        FOUND_SYNONYM; // CHAR! is only non-STRING!/WORD! case
            }
            if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
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
        Derelativize(Array_At(array, (n - 1) * wide), key, specifier);
    }

    if (mode > 1) { // append new value to the target series
        const Cell* src = key;
        indexes[slot] = (Array_Len(array) / wide) + 1;

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
    Flex* hashlist = MAP_HASHLIST(map);

    if (!hashlist) return;

    REBLEN *hashes = Flex_Head(REBLEN, hashlist);
    Array* pairlist = MAP_PAIRLIST(map);

    Value* key = KNOWN(Array_Head(pairlist));
    REBLEN n;

    for (n = 0; n < Array_Len(pairlist); n += 2, key += 2) {
        const bool cased = true; // cased=true is always fine

        if (Is_Zombie(key + 1)) {  // move last key to overwrite it
            Copy_Cell(
                key, KNOWN(Array_At(pairlist, Array_Len(pairlist) - 2))
            );
            Copy_Cell(
                &key[1], KNOWN(Array_At(pairlist, Array_Len(pairlist) - 1))
            );
            SET_ARRAY_LEN_NOTERM(pairlist, Array_Len(pairlist) - 2);
        }

        REBLEN hash = Find_Key_Hashed(
            pairlist, hashlist, key, SPECIFIED, 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (Is_Zombie(Array_At(pairlist, Array_Len(pairlist) - 1))) {
            SET_ARRAY_LEN_NOTERM(pairlist, Array_Len(pairlist) - 2);
        }
    }
}


//
//  Expand_Hash: C
//
// Expand hash series. Clear it but set its tail.
//
void Expand_Hash(Flex* flex)
{
    REBINT pnum = Get_Hash_Prime(Flex_Len(flex) + 1);
    if (pnum == 0) {
        DECLARE_VALUE (temp);
        Init_Integer(temp, Flex_Len(flex) + 1);
        panic (Error_Size_Limit_Raw(temp));
    }

    assert(not Is_Flex_Array(flex));
    Remake_Flex(
        flex,
        pnum + 1,
        Flex_Wide(flex),
        FLEX_FLAG_POWER_OF_2 // not(NODE_FLAG_NODE) => don't keep data
    );

    Clear_Flex(flex);
    Set_Flex_Len(flex, pnum);
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
    Specifier* key_specifier,
    const Cell* val,
    Specifier* val_specifier,
    bool cased // case-sensitive if true
) {
    assert(
        not Is_Nulled(key)
        and not Is_Void(key)
        and not Is_Trash(key)
    );

    Flex* hashlist = MAP_HASHLIST(map); // can be null
    Array* pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (Array_Len(pairlist) > Flex_Len(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    const REBLEN wide = 2;
    const Byte mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, wide, cased, mode
    );

    REBLEN *indexes = Flex_Head(REBLEN, hashlist);
    REBLEN n = indexes[slot];

    // n==0 or pairlist[(n-1)*]=~key

    if (val == nullptr)
        return n; // was just fetching the value

    assert(not Is_Antiform(val) or Is_Void(val));

    // If not just a GET, it may try to set the value in the map.  Which means
    // the key may need to be stored.  Since copies of keys are never made,
    // a SET must always be done with an immutable key...because if it were
    // changed, there'd be no notification to rehash the map.
    //
    Flex* locker = MAP_PAIRLIST(map);
    Force_Value_Frozen_Deep(key, locker);

    // Must set the value:
    if (n) {  // re-set it:
        Cell* dest = Array_At(pairlist, ((n - 1) * 2) + 1);
        if (Is_Void(val))
            Init_Zombie(dest);
        else
            Derelativize(dest, val, val_specifier);
        return n;
    }

    if (Is_Void(val))
        return 0; // trying to remove non-existing key

    // Create new entry.  Note that it does not copy underlying series (e.g.
    // the data of a string), which is why the immutability test is necessary
    //
    Append_Value_Core(pairlist, key, key_specifier);
    Append_Value_Core(pairlist, val, val_specifier);

    return (indexes[slot] = (Array_Len(pairlist) / 2));
}


//
//  PD_Map: C
//
Bounce PD_Map(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    assert(Is_Map(pvs->out));

    if (opt_setval != nullptr) {
        if (Is_Antiform(opt_setval) and not Is_Void(opt_setval))
            panic ("Can't set map entries to antiforms, use void to unset");

        Panic_If_Read_Only_Flex(Cell_Flex(pvs->out));
    }

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
        assert(n != 0 or Is_Void(opt_setval));
        return BOUNCE_INVISIBLE;
    }

    if (n == 0)
        return nullptr;

    Value* val = KNOWN(
        Array_At(MAP_PAIRLIST(VAL_MAP(pvs->out)), ((n - 1) * 2) + 1)
    );
    if (Is_Zombie(val))  // zombie entry, means unused
        return nullptr;

    return Copy_Cell(pvs->out, val); // RETURN (...) uses `level_`, not `pvs`
}


//
//  Append_Map: C
//
static void Append_Map(
    REBMAP *map,
    Array* array,
    REBLEN index,
    Specifier* specifier,
    REBLEN len
) {
    Cell* item = Array_At(array, index);
    REBLEN n = 0;

    while (n < len && NOT_END(item)) {
        if (IS_END(item + 1)) {
            //
            // Keys with no value not allowed, e.g. `make map! [1 "foo" 2]`
            //
            panic (Error_Past_End_Raw());
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
Bounce MAKE_Map(Value* out, enum Reb_Kind kind, const Value* arg)
{
    if (Any_Number(arg)) {
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
    Array* copy = Copy_Array_Shallow(MAP_PAIRLIST(map), SPECIFIED);
    Set_Array_Flag(copy, IS_PAIRLIST);

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    LINK(copy).hashlist = Copy_Non_Array_Flex_Core(
        MAP_HASHLIST(map),
        FLEX_FLAGS_NONE // !!! No NODE_FLAG_MANAGED?
    );

    if (types == 0)
        return MAP(copy); // no types have deep copy requested, shallow is OK

    // Even if the type flags request deep copies of series, none of the keys
    // need to be copied deeply.  This is because they are immutable at the
    // time of insertion.
    //
    assert(Array_Len(copy) % 2 == 0); // should be [key value key value]...

    Cell* key = Array_Head(copy);
    for (; NOT_END(key); key += 2) {
        assert(Is_Value_Immutable(key)); // immutable key

        Cell* v = key + 1;
        if (Is_Zombie(v))
            continue;  // "zombie" map element (not present)

        // No plain Clonify_Value() yet, call on values with length of 1.
        //
        Clonify_Values_Len_Managed(v, SPECIFIED, 1, types);
    }

    return MAP(copy);
}


//
//  TO_Map: C
//
Bounce TO_Map(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_MAP);
    UNUSED(kind);

    if (Is_Block(arg) || Is_Group(arg)) {
        //
        // make map! [word val word val]
        //
        Array* array = Cell_Array(arg);
        REBLEN len = VAL_ARRAY_LEN_AT(arg);
        REBLEN index = VAL_INDEX(arg);
        Specifier* specifier = VAL_SPECIFIER(arg);

        REBMAP *map = Make_Map(len / 2); // [key value key value...] + END
        Append_Map(map, array, index, specifier, len);
        Rehash_Map(map);
        return Init_Map(out, map);
    }
    else if (Is_Map(arg)) {
        //
        // Values are not copied deeply by default.
        //
        // !!! Is there really a use in allowing MAP! to be converted TO a
        // MAP! as opposed to having people COPY it?
        //
        REBU64 types = 0;

        return Init_Map(out, Copy_Map(VAL_MAP(arg), types));
    }

    panic (Error_Invalid(arg));
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
Array* Map_To_Array(REBMAP *map, REBINT what)
{
    REBLEN count = Length_Map(map);
    Array* a = Make_Array(count * ((what == 0) ? 2 : 1));

    Value* dest = KNOWN(Array_Head(a));
    Value* val = KNOWN(Array_Head(MAP_PAIRLIST(map)));
    for (; NOT_END(val); val += 2) {
        assert(NOT_END(val + 1));
        if (Is_Zombie(val + 1))
            continue;
        if (what <= 0) {
            Copy_Cell(dest, &val[0]);
            ++dest;
        }
        if (what >= 0) {
            Copy_Cell(dest, &val[1]);
            ++dest;
        }
    }

    Term_Array_Len(a, cast(Cell*, dest) - Array_Head(a));
    assert(IS_END(dest));
    return a;
}


//
//  Alloc_Context_From_Map: C
//
VarList* Alloc_Context_From_Map(REBMAP *map)
{
    // Doesn't use Length_Map because it only wants to consider words.
    //
    // !!! Should this panic() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! make map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    Value* mval = KNOWN(Array_Head(MAP_PAIRLIST(map)));
    REBLEN count = 0;

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (Any_Word(mval) and not Is_Zombie(mval + 1))
            ++count;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words

    VarList* context = Alloc_Context(TYPE_OBJECT, count);
    Value* key = Varlist_Keys_Head(context);
    Value* var = Varlist_Slots_Head(context);

    mval = KNOWN(Array_Head(MAP_PAIRLIST(map)));

    for (; NOT_END(mval); mval += 2) {
        assert(NOT_END(mval + 1));
        if (Any_Word(mval) and not Is_Zombie(mval + 1)) {
            // !!! Used to leave SET_WORD typed values here... but why?
            // (Objects did not make use of the set-word vs. other distinctions
            // that function specs did.)
            Init_Typeset(
                key,
                TS_VALUE, // !!! Not used at the moment
                Word_Symbol(mval)
            );
            ++key;
            Copy_Cell(var, &mval[1]);
            ++var;
        }
    }

    Term_Array_Len(Varlist_Array(context), count + 1);
    Term_Array_Len(Keylist_Of_Varlist(context), count + 1);
    assert(IS_END(key));
    assert(IS_END(var));

    return context;
}


//
//  MF_Map: C
//
void MF_Map(Molder* mo, const Cell* v, bool form)
{
    REBMAP *m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Flex(TG_Mold_Stack, m) != NOT_FOUND) {
        Append_Unencoded(mo->utf8flex, "...]");
        return;
    }

    Push_Pointer_To_Flex(TG_Mold_Stack, m);

    if (not form) {
        Begin_Non_Lexical_Mold(mo, v);
        Append_Codepoint(mo->utf8flex, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    Cell* key = Array_Head(MAP_PAIRLIST(m));
    for (; NOT_END(key); key += 2) {
        assert(NOT_END(key + 1)); // value slot must not be END
        if (Is_Zombie(key + 1))
            continue; // if value for this key is void, key has been removed

        if (not form)
            New_Indented_Line(mo);
        Emit(mo, "V V", key, key + 1);
        if (form)
            Append_Codepoint(mo->utf8flex, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Codepoint(mo->utf8flex, ']');
    }

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(TG_Mold_Stack, m);
}


//
//  REBTYPE: C
//
REBTYPE(Map)
{
    Value* val = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    REBMAP *map = VAL_MAP(val);

    switch (Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE)); // covered by `val`
        Option(SymId) property = Word_Id(ARG(PROPERTY));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(OUT, Length_Map(map));

        case SYM_VALUES:
            return Init_Block(OUT, Map_To_Array(map, 1));

        case SYM_WORDS:
            return Init_Block(OUT, Map_To_Array(map, -1));

        case SYM_BODY:
            return Init_Block(OUT, Map_To_Array(map, 0));

        case SYM_TAIL_Q:
            return Init_Logic(OUT, Length_Map(map) == 0);

        default:
            break;
        }

        panic (Error_Cannot_Reflect(TYPE_MAP, arg)); }

    case SYM_FIND:
    case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(PARAM(SERIES));
        UNUSED(PARAM(VALUE)); // handled as `arg`

        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(ONLY))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(SKIP)) {
            UNUSED(ARG(SIZE));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(LAST))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(REVERSE))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(TAIL))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(MATCH))
            panic (Error_Bad_Refines_Raw());

        REBINT n = Find_Map_Entry(
            map,
            arg,
            SPECIFIED,
            nullptr,
            SPECIFIED,
            Bool_ARG(CASE)
        );

        if (n == 0)
            return nullptr;

        Copy_Cell(
            OUT,
            KNOWN(Array_At(MAP_PAIRLIST(map), ((n - 1) * 2) + 1))
        );

        if (Word_Id(verb) == SYM_FIND)
            return Is_Zombie(OUT) ? nullptr : Init_Logic(OUT, true);

        return OUT; }

    case SYM_PUT: {
        INCLUDE_PARAMS_OF_PUT;
        UNUSED(ARG(SERIES)); // extracted to `map`

        REBINT n = Find_Map_Entry(
            map,
            ARG(KEY),
            SPECIFIED,
            ARG(VALUE),
            SPECIFIED,
            Bool_ARG(CASE)
        );
        UNUSED(n);

        RETURN (ARG(VALUE)); }

    case SYM_INSERT:
    case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;

        PANIC_IF_ERROR(arg);

        if (not Is_Block(arg))
            panic (Error_Invalid(arg));

        Panic_If_Read_Only_Flex(MAP_PAIRLIST(map));

        UNUSED(PARAM(SERIES));
        UNUSED(PARAM(VALUE)); // handled as arg

        if (Bool_ARG(ONLY))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(LINE))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(DUP)) {
            UNUSED(ARG(COUNT));
            panic (Error_Bad_Refines_Raw());
        }

        REBLEN len = Part_Len_May_Modify_Index(arg, ARG(LIMIT));
        UNUSED(Bool_ARG(PART)); // detected by if limit is nulled

        Append_Map(
            map,
            Cell_Array(arg),
            VAL_INDEX(arg),
            VAL_SPECIFIER(arg),
            len
        );

        return Init_Map(OUT, map); }

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        Panic_If_Read_Only_Flex(MAP_PAIRLIST(map));

        UNUSED(PARAM(SERIES));

        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (not Bool_ARG(MAP))
            panic (Error_Illegal_Action(TYPE_MAP, verb));

        Copy_Cell(OUT, val);
        Find_Map_Entry(
            map, ARG(KEY), SPECIFIED, NULLED_CELL, SPECIFIED, true
        );
        return OUT; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(VALUE));
        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }

        REBU64 types = 0; // which types to copy non-"shallowly"

        if (Bool_ARG(DEEP))
            types |= Bool_ARG(TYPES) ? 0 : TS_CLONE;

        if (Bool_ARG(TYPES)) {
            if (Is_Datatype(ARG(KINDS)))
                types |= FLAGIT_KIND(Type_Of(ARG(KINDS)));
            else
                types |= Cell_Typeset_Bits(ARG(KINDS));
        }

        return Init_Map(OUT, Copy_Map(map, types)); }

    case SYM_CLEAR:
        Panic_If_Read_Only_Flex(MAP_PAIRLIST(map));

        Reset_Array(MAP_PAIRLIST(map));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Flex(MAP_HASHLIST(map));

        return Init_Map(OUT, map);

    default:
        break;
    }

    panic (Error_Illegal_Action(TYPE_MAP, verb));
}
