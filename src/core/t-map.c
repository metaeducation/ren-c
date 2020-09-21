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
// See %sys-map.h for an explanation of the map structure.
//

#include "sys-core.h"

//
//  CT_Map: C
//
// !!! Was never implemented in R3-Alpha; called into raw array comparison,
// which is clearly incorrect.  Needs to be written.
//
REBINT CT_Map(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(strict);
    fail ("https://github.com/rebol/rebol-issues/issues/2340");
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
    REBARR *pairlist = Make_Array_Core(capacity * 2, SERIES_MASK_PAIRLIST);
    LINK_HASHLIST_NODE(pairlist) = NOD(Make_Hash_Sequence(capacity));

    return MAP(pairlist);
}


static REBCTX *Error_Conflicting_Key(const RELVAL *key, REBSPC *specifier)
{
    DECLARE_LOCAL (specific);
    Derelativize(specific, key, specifier);
    return Error_Conflicting_Key_Raw(specific);
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
    REBLEN wide,
    bool strict,
    REBYTE mode
){
    // Hashlists store a indexes into the actual data array, of where the
    // first key corresponding to that hash is.  There may be more keys
    // indicated by that hash, vying for the same slot.  So the collisions
    // add a skip amount and keep trying:
    //
    // https://en.wikipedia.org/wiki/Linear_probing
    //
    // Used and skip are co-primes, so is guaranteed that repeatedly
    // adding skip (and subtracting len when needed) all positions are
    // visited.  1 <= skip < len, and len is prime, so this is guaranteed.
    //
    REBLEN used = SER_USED(hashlist);
    REBLEN *indexes = SER_HEAD(REBLEN, hashlist);

    uint32_t hash = Hash_Value(key);
    REBLEN slot = hash % used;  // first slot to try for this hash
    REBLEN skip = hash % (used - 1) + 1;  // skip by how much each collision

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

    REBLEN n;
    while ((n = indexes[slot]) != 0) {
        RELVAL *k = ARR_AT(array, (n - 1) * wide); // stored key
        if (0 == Cmp_Value(k, key, true)) { // exact match
            if (strict)
                return slot; // don't need to check synonyms, stop looking
            goto found_synonym; // confirm exact match is the only match
        }

        if (not strict) {
            if (0 == Cmp_Value(k, key, false)) { // non-strict match

              found_synonym:;

                if (synonym_slot != -1) // another equivalent already matched
                    fail (Error_Conflicting_Key(key, specifier));
                synonym_slot = slot; // save and continue checking
            }
        }

        if (wide > 1 && IS_NULLED(k + 1) && zombie_slot == -1)
            zombie_slot = slot;

        slot += skip;
        if (slot >= used)
            slot -= used;
    }

    if (synonym_slot != -1) {
        assert(not strict);
        return synonym_slot; // there weren't other spellings of the same key
    }

    if (zombie_slot != -1) { // zombie encountered; overwrite with new key
        assert(mode == 0);
        slot = zombie_slot;
        Derelativize(
            ARR_AT(array, (indexes[slot] - 1) * wide),
            key,
            specifier
        );
    }

    if (mode > 1) { // append new value to the target series
        const RELVAL *src = key;
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

    REBVAL *key = SPECIFIC(ARR_HEAD(pairlist));
    REBLEN n;

    for (n = 0; n < ARR_LEN(pairlist); n += 2, key += 2) {
        const bool cased = true; // cased=true is always fine

        if (IS_NULLED(key + 1)) {
            //
            // It's a "zombie", move last key to overwrite it
            //
            Move_Value(
                key, SPECIFIC(ARR_AT(pairlist, ARR_LEN(pairlist) - 2))
            );
            Move_Value(
                &key[1], SPECIFIC(ARR_AT(pairlist, ARR_LEN(pairlist) - 1))
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
    assert(not IS_SER_ARRAY(ser));

    REBINT prime = Get_Hash_Prime_May_Fail(SER_USED(ser) + 1);
    Remake_Series(
        ser,
        prime + 1,
        SER_WIDE(ser),
        SERIES_FLAG_POWER_OF_2  // not(NODE_FLAG_NODE) => don't keep data
    );

    Clear_Series(ser);
    SET_SERIES_LEN(ser, prime);
}


//
//  Find_Map_Entry: C
//
// Try to find the entry in the map. If not found and val isn't nullptr,
// create the entry and store the key and val.
//
// RETURNS: the index to the VALUE or zero if there is none.
//
REBLEN Find_Map_Entry(
    REBMAP *map,
    const RELVAL *key,
    REBSPC *key_specifier,
    const RELVAL *val,
    REBSPC *val_specifier,
    bool strict
) {
    assert(not IS_NULLED(key));

    REBSER *hashlist = MAP_HASHLIST(map); // can be null
    REBARR *pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (ARR_LEN(pairlist) > SER_USED(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    const REBLEN wide = 2;
    const REBYTE mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, key_specifier, wide, strict, mode
    );

    REBLEN *indexes = SER_HEAD(REBLEN, hashlist);
    REBLEN n = indexes[slot];

    // n==0 or pairlist[(n-1)*]=~key

    if (val == NULL)
        return n; // was just fetching the value

    // If not just a GET, it may try to set the value in the map.  Which means
    // the key may need to be stored.  Since copies of keys are never made,
    // a SET must always be done with an immutable key...because if it were
    // changed, there'd be no notification to rehash the map.
    //
    Force_Value_Frozen_Deep_Blame(key, MAP_PAIRLIST(map));

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
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    assert(IS_MAP(pvs->out));

    if (IS_NULLED(picker))  // best to error on a null picker
        return R_UNHANDLED;

    // Fetching and setting with path-based access is case-preserving for any
    // initial insertions.  However, the case-insensitivity means that all
    // writes after that to the same key will not be overriding the key,
    // it will just change the data value for the existing key.  SELECT and
    // the operation tentatively named PUT should be used if a map is to
    // distinguish multiple casings of the same key.
    //
    const bool cased = false;

    if (opt_setval) {
        REBMAP *m = VAL_MAP_ENSURE_MUTABLE(pvs->out);

        REBINT n = Find_Map_Entry(
            m,  // modified (if not located in map)
            picker,
            SPECIFIED,
            opt_setval,  // value to set
            SPECIFIED,
            cased
        );

        assert(n != 0);
        UNUSED(n);
        return R_INVISIBLE;
    }

    const REBMAP *m = VAL_MAP(pvs->out);

    REBINT n = Find_Map_Entry(
        m_cast(REBMAP*, m),  // not modified
        picker,
        SPECIFIED,
        nullptr,  // no value, so map not changed
        SPECIFIED,
        cased
    );

    if (n == 0)
        return nullptr;

    const REBVAL *val = SPECIFIC(ARR_AT(MAP_PAIRLIST(m), ((n - 1) * 2) + 1));
    if (IS_NULLED(val))  // zombie entry, means unused
        return nullptr;

    return Move_Value(pvs->out, val); // RETURN (...) uses `frame_`, not `pvs`
}


//
//  Append_Map: C
//
static void Append_Map(
    REBMAP *map,
    const REBARR *array,
    REBLEN index,
    REBSPC *specifier,
    REBLEN len
) {
    const RELVAL *item = ARR_AT(array, index);
    REBLEN n = 0;

    while (n < len && NOT_END(item)) {
        if (IS_END(item + 1)) {
            //
            // Keys with no value not allowed, e.g. `make map! [1 "foo" 2]`
            //
            fail (Error_Past_End_Raw());
        }

        bool strict = true;
        Find_Map_Entry(
            map,
            item,
            specifier,
            item + 1,
            specifier,
            strict
        );

        item += 2;
        n += 2;
    }
}


//
//  MAKE_Map: C
//
REB_R MAKE_Map(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

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


inline static REBMAP *Copy_Map(const REBMAP *map, REBU64 types) {
    REBARR *copy = Copy_Array_Shallow_Flags(
        MAP_PAIRLIST(map),
        SPECIFIED,
        SERIES_MASK_PAIRLIST
    );

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    LINK_HASHLIST_NODE(copy) = NOD(Copy_Sequence_Core(
        MAP_HASHLIST(map),
        SERIES_FLAGS_NONE // !!! No NODE_FLAG_MANAGED?
    ));

    if (types == 0)
        return MAP(copy); // no types have deep copy requested, shallow is OK

    // Even if the type flags request deep copies of series, none of the keys
    // need to be copied deeply.  This is because they are immutable at the
    // time of insertion.
    //
    assert(ARR_LEN(copy) % 2 == 0); // should be [key value key value]...

    REBVAL *key = SPECIFIC(ARR_HEAD(copy)); // all keys/values are specified
    for (; NOT_END(key); key += 2) {
        assert(Is_Value_Frozen_Deep(key));  // immutable key

        REBVAL *v = key + 1;
        if (IS_NULLED(v))
            continue; // "zombie" map element (not present)

        REBFLGS flags = NODE_FLAG_MANAGED;  // !!! Review
        Clonify(v, flags, types);
    }

    return MAP(copy);
}


//
//  TO_Map: C
//
REB_R TO_Map(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_MAP);
    UNUSED(kind);

    if (IS_BLOCK(arg) || IS_GROUP(arg)) {
        //
        // make map! [word val word val]
        //
        const REBARR* array = VAL_ARRAY(arg);
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

    fail (arg);
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
REBARR *Map_To_Array(const REBMAP *map, REBINT what)
{
    REBLEN count = Length_Map(map);
    REBARR *a = Make_Array(count * ((what == 0) ? 2 : 1));

    RELVAL *dest = ARR_HEAD(a);
    const REBVAL *val = SPECIFIC(ARR_HEAD(MAP_PAIRLIST(map)));
    for (; NOT_END(val); val += 2) {
        if (not IS_NULLED(val + 1)) {  // can't be END
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

    TERM_ARRAY_LEN(a, dest - ARR_HEAD(a));
    assert(IS_END(dest));
    return a;
}


//
//  Alloc_Context_From_Map: C
//
REBCTX *Alloc_Context_From_Map(const REBMAP *map)
{
    // Doesn't use Length_Map because it only wants to consider words.
    //
    // !!! Should this fail() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! make map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    const REBVAL *mval = SPECIFIC(ARR_HEAD(MAP_PAIRLIST(map)));
    REBLEN count = 0;

    for (; NOT_END(mval); mval += 2) {  // note mval must not be END
        if (ANY_WORD(mval) and not IS_NULLED(mval + 1))
            ++count;
    }

    // See Alloc_Context() - cannot use it directly because no Collect_Words

    REBCTX *context = Alloc_Context(REB_OBJECT, count);
    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);

    mval = SPECIFIC(ARR_HEAD(MAP_PAIRLIST(map)));

    for (; NOT_END(mval); mval += 2) {  // note mval must not be END
        if (ANY_WORD(mval) and not IS_NULLED(mval + 1)) {
            Init_Context_Key(key, VAL_WORD_SPELLING(mval));
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
void MF_Map(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    const REBMAP *m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Series(TG_Mold_Stack, m) != NOT_FOUND) {
        Append_Ascii(mo->series, "...]");
        return;
    }

    Push_Pointer_To_Series(TG_Mold_Stack, m);

    if (not form) {
        Pre_Mold(mo, v);
        Append_Codepoint(mo->series, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    const RELVAL *key = ARR_HEAD(MAP_PAIRLIST(m));
    for (; NOT_END(key); key += 2) {  // note value slot must not be END
        if (IS_NULLED(key + 1))
            continue; // if value for this key is void, key has been removed

        if (not form)
            New_Indented_Line(mo);
        Mold_Value(mo, key);
        Append_Codepoint(mo->series, ' ');
        Mold_Value(mo, key + 1);
        if (form)
            Append_Codepoint(mo->series, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Codepoint(mo->series, ']');
    }

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, m);
}


//
//  REBTYPE: C
//
REBTYPE(Map)
{
    REBVAL *map = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `v`

        const REBMAP *m = VAL_MAP(map);

        REBVAL *property = ARG(property);
        switch (VAL_WORD_SYM(property)) {
          case SYM_LENGTH:
            return Init_Integer(D_OUT, Length_Map(m));

          case SYM_VALUES:
            return Init_Block(D_OUT, Map_To_Array(m, 1));

          case SYM_WORDS:
            return Init_Block(D_OUT, Map_To_Array(m, -1));

          case SYM_BODY:
            return Init_Block(D_OUT, Map_To_Array(m, 0));

          case SYM_TAIL_Q:
            return Init_Logic(D_OUT, Length_Map(m) == 0);

          default:
            break;
        }
        fail (Error_Cannot_Reflect(REB_MAP, property)); }

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(PAR(series));  // covered by `v`

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        if (REF(part) or REF(only) or REF(skip) or REF(tail) or REF(match))
            fail (Error_Bad_Refines_Raw());

        const REBMAP *m = VAL_MAP(map);

        REBINT n = Find_Map_Entry(
            m_cast(REBMAP*, VAL_MAP(map)),  // should not modify, see below
            ARG(pattern),
            SPECIFIED,
            nullptr,  // nullptr indicates it will only search, not modify
            SPECIFIED,
            did REF(case)
        );

        if (n == 0)
            return nullptr;

        Move_Value(
            D_OUT,
            SPECIFIC(ARR_AT(MAP_PAIRLIST(m), ((n - 1) * 2) + 1))
        );

        if (VAL_WORD_SYM(verb) == SYM_FIND)
            return IS_NULLED(D_OUT) ? nullptr : Init_True(D_OUT);

        return D_OUT; }

      case SYM_PUT: {
        INCLUDE_PARAMS_OF_PUT;
        UNUSED(ARG(series)); // extracted to `map`

        REBINT n = Find_Map_Entry(
            VAL_MAP_ENSURE_MUTABLE(map),
            ARG(key),
            SPECIFIED,
            ARG(value),  // non-null indicates it will modify, vs. just search
            SPECIFIED,
            did REF(case)
        );
        UNUSED(n);

        RETURN (ARG(value)); }

      case SYM_INSERT:
      case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PAR(series));

        REBVAL *value = ARG(value);
        if (IS_NULLED_OR_BLANK(value))
            RETURN (map);  // don't fail on read only if it would be a no-op

        REBMAP *m = VAL_MAP_ENSURE_MUTABLE(map);

        if (REF(only) or REF(line) or REF(dup))
            fail (Error_Bad_Refines_Raw());

        if (not IS_BLOCK(value))
            fail (PAR(value));

        REBLEN len = Part_Len_May_Modify_Index(value, ARG(part));

        Append_Map(
            m,
            VAL_ARRAY(value),
            VAL_INDEX(value),
            VAL_SPECIFIER(value),
            len
        );

        return Init_Map(D_OUT, m); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        REBU64 types = 0; // which types to copy non-"shallowly"

        if (REF(deep))
            types |= REF(types) ? 0 : TS_CLONE;

        if (REF(types)) {
            if (IS_DATATYPE(ARG(types)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(types)));
            else {
                types |= VAL_TYPESET_LOW_BITS(ARG(types));
                types |= cast(REBU64, VAL_TYPESET_HIGH_BITS(ARG(types))) << 32;
            }
        }

        return Init_Map(D_OUT, Copy_Map(VAL_MAP(map), types)); }

      case SYM_CLEAR: {
        REBMAP *m = VAL_MAP_ENSURE_MUTABLE(map);

        Reset_Array(MAP_PAIRLIST(m));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Series(MAP_HASHLIST(m));

        return Init_Map(D_OUT, m); }

      default:
        break;
    }

    return R_UNHANDLED;
}
