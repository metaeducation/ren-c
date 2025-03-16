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

// "Zombie" keys in map, represent missing or deleted entries.
//
// We use unreadable (vs. void or null) because it's not an antiform, and we'd
// like to keep the arrays backing a MAP! free of antiforms (vs. making one
// exception for the zombie).  Also, unreadable has nice properties of erroring
// if you try to read it in the checked build.
//
#define Is_Zombie Not_Cell_Readable
#define Init_Zombie Init_Unreadable


//
//  Num_Map_Entries_Used: C
//
// Maps may have unused (zombie) slots in their capacity, so Array_Len() on
// the pairlist divided by 2 doesn't tell you how many entries in the map.
//
// This count could be cached.
//
Count Num_Map_Entries_Used(const Map* map)
{
    const Element* tail = Array_Tail(MAP_PAIRLIST(map));
    const Element* key = Array_Head(MAP_PAIRLIST(map));

    Count count = 0;
    for (; key != tail; key += 2) {
        if (not Is_Zombie(key + 1))
            ++count;
    }

    return count;
}


//
//  CT_Map: C
//
// !!! Was never implemented in R3-Alpha; called into raw array comparison,
// which is clearly incorrect.  Needs to be written.
//
REBINT CT_Map(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(strict);
    fail ("https://github.com/rebol/rebol-issues/issues/2340");
}


IMPLEMENT_GENERIC(equal_q, map)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Map(ARG(value1), ARG(value2), REF(strict)) == 0);
}


//
//  Make_Map: C
//
// Makes a MAP block (that holds both keys and values).
// Capacity is measured in key-value pairings.
// A hash Flex is also created.
//
Map* Make_Map(REBLEN capacity)
{
    Array* pairlist = Make_Array_Core(FLEX_MASK_PAIRLIST, capacity * 2);
    Tweak_Link_Hashlist(pairlist, Make_Hashlist(capacity));

    return cast(Map*, pairlist);
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
    Array* array,  // not always a pairlist, may group by amounts other than 2
    HashList* hashlist,
    const Element* key,  // !!! assumes ++key finds the values
    REBLEN wide,  // how much to group by (2 for MAP! and PairList arrays)
    bool strict,
    Byte mode
){
    // Hashlists store a indexes into the actual data array, of where the
    // first key corresponding to that hash is.  There may be more keys
    // indicated by that hash, vying for the same slot.  So the collisions
    // add a skip amount and keep trying:
    //
    // https://en.wikipedia.org/wiki/Linear_probing
    //
    // num_slots and skip are co-primes, so is guaranteed that by repeatedly
    // adding skip (and subtracting num_slots when needed) all positions are
    // visited.  1 <= skip < num_slots (which is prime), so this is guaranteed.
    //
    Count num_slots = Hashlist_Num_Slots(hashlist);
    REBLEN *indexes = Flex_Head(REBLEN, hashlist);

    uint32_t hash = Hash_Value(key);
    Offset slot = hash % num_slots;  // first slot to try for this hash
    Count skip = hash % (num_slots - 1) + 1;  // skip by this each collision

    // Zombie slots are those which are left behind by removing items, with
    // void values that are illegal in maps, and indicate they can be reused.
    //
    Offset zombie_slot = -1; // no zombies seen yet...

    // You can store information case-insensitively in a MAP!, and it will
    // overwrite the value for at most one other key.  Reading information
    // case-insensitively out of a map can only be done if there aren't two
    // keys with the same spelling.
    //
    Offset synonym_slot = -1; // no synonyms seen yet...

    REBLEN n;
    while ((n = indexes[slot]) != 0) {
        Value* k = Flex_At(Value, array, (n - 1) * wide); // stored key
        if (Equal_Values(k, key, true)) {
            if (strict)
                return slot; // don't need to check synonyms, stop looking
            goto found_synonym; // confirm exact match is the only match
        }

        if (not strict) {  // now do the non strict match (false)
            if (Equal_Values(k, key, false)) {

              found_synonym:;

                if (synonym_slot != -1) // another equivalent already matched
                    fail (Error_Conflicting_Key_Raw(key));
                synonym_slot = slot; // save and continue checking
            }
        }

        if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
            zombie_slot = slot;

        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
    }

    if (synonym_slot != -1) {
        assert(not strict);
        return synonym_slot; // there weren't other spellings of the same key
    }

    if (zombie_slot != -1) { // zombie encountered; overwrite with new key
        assert(mode == 0);
        slot = zombie_slot;
        Copy_Cell(
            Array_At(array, (indexes[slot] - 1) * wide),
            key
        );
    }

    if (mode > 1) { // append new value to the target array
        const Element* src = key;
        indexes[slot] = (Array_Len(array) / wide) + 1;

        REBLEN index;
        for (index = 0; index < wide; ++src, ++index)
            Append_Value(array, src);
    }

    return (mode > 0) ? -1 : slot;
}


//
//  Rehash_Map: C
//
// Recompute the entire hash table for a map. Table must be large enough.
//
void Rehash_Map(Map* map)
{
    HashList* hashlist = MAP_HASHLIST(map);

    REBLEN *hashes = Flex_Head(REBLEN, hashlist);
    PairList* pairlist = MAP_PAIRLIST(map);

    Value* key = Array_Head(pairlist);
    REBLEN n;

    for (n = 0; n < Array_Len(pairlist); n += 2, key += 2) {
        const bool cased = true; // cased=true is always fine

        if (Is_Zombie(key + 1)) {  // zombie: move last key to overwrite
            Copy_Cell(key, Array_At(pairlist, Array_Len(pairlist) - 2));
            Copy_Cell(&key[1], Array_At(pairlist, Array_Len(pairlist) - 1));
            Set_Flex_Len(pairlist, Array_Len(pairlist) - 2);
        }

        REBLEN hash = Find_Key_Hashed(
            pairlist, hashlist, cast(Element*, key), 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (Is_Zombie(Array_At(pairlist, Array_Len(pairlist) - 1)))
            Set_Flex_Len(pairlist, Array_Len(pairlist) - 2);
    }
}


//
//  Expand_Hashlist: C
//
// Expand hash flex. Clear it but set its tail.
//
void Expand_Hashlist(HashList* hashlist)
{
    assert(Stub_Flavor(hashlist) == FLAVOR_HASHLIST);

    REBINT prime = Get_Hash_Prime_May_Fail(Hashlist_Num_Slots(hashlist) + 1);
    Remake_Flex(
        hashlist,
        prime + 1,
        FLEX_FLAG_POWER_OF_2  // not(NODE_FLAG_NODE) => don't keep data
    );

    Clear_Flex(hashlist);
    Set_Flex_Len(hashlist, prime);
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
    Map* map,
    const Element* key,
    Option(const Value*) val,  // nullptr is fetch only, void is remove
    bool strict
) {
    HashList* hashlist = MAP_HASHLIST(map); // can be null
    PairList* pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (Array_Len(pairlist) > Hashlist_Num_Slots(hashlist) / 2) {
        Expand_Hashlist(hashlist);  // modifies size value
        Rehash_Map(map);
    }

    const REBLEN wide = 2;
    const Byte mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, wide, strict, mode
    );

    REBLEN *indexes = Flex_Head(REBLEN, hashlist);
    REBLEN n = indexes[slot];

    // n==0 or pairlist[(n-1)*]=~key

    if (not val)
        return n; // was just fetching the value

    // If not just a GET, it may try to set the value in the map.  Which means
    // the key may need to be stored.  Since copies of keys are never made,
    // a SET must always be done with an immutable key...because if it were
    // changed, there'd be no notification to rehash the map.
    //
    Force_Value_Frozen_Deep_Blame(key, MAP_PAIRLIST(map));

    if (n) {  // found, must set or overwrite the value
        Element* at = Array_At(pairlist, ((n - 1) * 2) + 1);
        if (Is_Void(unwrap val))
            Init_Zombie(at);
        else {
            assert(Not_Antiform(unwrap val));
            Copy_Cell(at, cast(const Element*, unwrap val));
        }
        return n;
    }

    if (Is_Void(unwrap val))
        return 0;  // trying to remove non-existing key

    assert(Not_Antiform(unwrap val));

    // Create new entry.  Note that it does not copy the underlying Flex (e.g.
    // the data of a String), which is why the immutability test is necessary
    //
    Append_Value(pairlist, key);
    Append_Value(pairlist, c_cast(Element*, unwrap val));  // val not void

    return (indexes[slot] = (Array_Len(pairlist) / 2));
}


//
//  Append_Map: C
//
void Append_Map(
    Map* map,
    const Element* head,
    const Element* tail,
    REBLEN len
){
    const Element* item = head;
    REBLEN n = 0;

    while (n < len and item != tail) {
        if (item + 1 == tail) {
            //
            // Keys with no value not allowed, e.g. `to map! [1 "foo" 2]`
            //
            fail (Error_Index_Out_Of_Range_Raw());
        }

        bool strict = true;
        Find_Map_Entry(
            map,
            item,
            item + 1,
            strict
        );

        item += 2;
        n += 2;
    }
}


// !!! R3-Alpha TO of MAP! was like MAKE but wouldn't accept just a size.
// Since TO MAP! doesn't do any evaluation, drop MAKE MAP! for now...it may
// return as an evaluating or otherwise interesting form.
//
IMPLEMENT_GENERIC(make, map)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(VAL_TYPE_KIND(ARG(type)) == REB_MAP);
    UNUSED(ARG(type));

    Element* arg = Element_ARG(def);

    if (Any_Number(arg))
        return Init_Map(OUT, Make_Map(Int32s(arg, 0)));

    return FAIL(Error_Bad_Make(REB_MAP, arg));
}


INLINE Map* Copy_Map(const Map* map, bool deeply) {
    Array* copy = Copy_Array_Shallow_Flags(
        FLEX_MASK_PAIRLIST,
        MAP_PAIRLIST(map)
    );

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    HashList* hashlist = cast(HashList*, Copy_Flex_Core(
        FLEX_FLAGS_NONE | FLAG_FLAVOR(HASHLIST),  // !!! No NODE_FLAG_MANAGED?
        MAP_HASHLIST(map)
    ));
    Tweak_Link_Hashlist(copy, hashlist);

    if (not deeply)
        return cast(Map*, copy);  // shallow is ok

    // Even if the type flags request deep copies of Arrays, none of the keys
    // need to be copied deeply.  This is because they are immutable at the
    // time of insertion.
    //
    assert(Array_Len(copy) % 2 == 0); // should be [key value key value]...

    const Cell* tail = Array_Tail(copy);
    Value* key = Array_Head(copy);  // keys/vals specified
    for (; key != tail; key += 2) {
        assert(Is_Value_Frozen_Deep(key));  // immutable key

        Value* v = key + 1;
        assert(v != tail);
        if (Is_Zombie(v))
            continue;

        Flags flags = NODE_FLAG_MANAGED;  // !!! Review
        Clonify(v, flags, deeply);
    }

    return cast(Map*, copy);
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
Source* Map_To_Array(const Map* map, REBINT what)
{
    Count count = Num_Map_Entries_Used(map);
    Length len = count * ((what == 0) ? 2 : 1);
    Source* a = Make_Source(len);
    Set_Flex_Len(a, len);

    Element* dest = Array_Head(a);
    const Element* tail = Array_Tail(MAP_PAIRLIST(map));
    const Element* at = Array_Head(MAP_PAIRLIST(map));

    for (; at != tail; at += 2) {
        assert(at + 1 < tail);
        if (Is_Zombie(at + 1))
            continue;

        if (what <= 0) {
            Copy_Cell(dest, at);  // no keys are zombies or antiforms
            ++dest;
        }
        if (what >= 0) {
            Copy_Cell(dest, at + 1);  // value was tested and isn't a zombie
            ++dest;
        }
    }
    return a;
}


//
//  Alloc_Varlist_From_Map: C
//
VarList* Alloc_Varlist_From_Map(const Map* map)
{
    // Doesn't use Num_Map_Entries_Used() because it only considers words.
    //
    // !!! Should this fail() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! to map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    REBLEN count = 0;

  blockscope {
    const Value* mval_tail = Flex_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Flex_Head(Value, MAP_PAIRLIST(map));
    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1))
            ++count;
    }
  }

    // See Alloc_Varlist() - cannot use it directly because no Collect_Words

    VarList* c = Alloc_Varlist(REB_OBJECT, count);

    const Value* mval_tail = Flex_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Flex_Head(Value, MAP_PAIRLIST(map));

    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1)) {
            Value* var = Append_Context(c, Cell_Word_Symbol(mval));
            Copy_Cell(var, mval + 1);
        }
    }

    return c;
}


IMPLEMENT_GENERIC(moldify, map)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(element);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(molder));
    bool form = REF(form);

    const Map* m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Flex(g_mold.stack, m) != NOT_FOUND) {
        Append_Ascii(mo->string, "...]");
        return NOTHING;
    }

    Push_Pointer_To_Flex(g_mold.stack, m);

    if (not form) {
        Begin_Non_Lexical_Mold(mo, v);
        Append_Codepoint(mo->string, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    const Value* tail = Flex_Tail(Value, MAP_PAIRLIST(m));
    const Value* key = Flex_Head(Value, MAP_PAIRLIST(m));
    for (; key != tail; key += 2) {  // note value slot must not be END
        assert(key + 1 != tail);
        if (Is_Zombie(key + 1))
            continue;  // key has been removed

        if (not form)
            New_Indented_Line(mo);
        Mold_Element(mo, c_cast(Element*, key));
        Append_Codepoint(mo->string, ' ');
        Mold_Element(mo, c_cast(Element*, key + 1));
        if (form)
            Append_Codepoint(mo->string, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Codepoint(mo->string, ']');
    }

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, m);

    return NOTHING;
}


IMPLEMENT_GENERIC(oldgeneric, map)
{
    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    Element* map = cast(Element*, ARG_N(1));
    assert(Is_Map(map));

    switch (id) {
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        if (Is_Antiform(ARG(value)))
            return FAIL(ARG(value));

        UNUSED(PARAM(series));  // covered by `v`

        if (REF(part) or REF(skip) or REF(match))
            return FAIL(Error_Bad_Refines_Raw());

        const Map* m = VAL_MAP(map);

        REBINT n = Find_Map_Entry(
            m_cast(Map*, VAL_MAP(map)),  // should not modify, see below
            Element_ARG(value),
            nullptr,  // nullptr indicates it will only search, not modify
            REF(case)
        );

        if (n == 0)
            return nullptr;

        const Value* val = Flex_At(Value, MAP_PAIRLIST(m), ((n - 1) * 2) + 1);
        if (Is_Zombie(val))
            return nullptr;

        return Copy_Cell(OUT, val); }

      case SYM_PUT: {
        INCLUDE_PARAMS_OF_PUT;
        UNUSED(ARG(series)); // extracted to `map`

        Value* key = ARG(key);
        Value* val = ARG(value);

        if (Is_Void(key))
            return FAIL(Error_Bad_Void());  // tolerate?
        if (Is_Antiform(key))
            return FAIL(Error_Bad_Antiform(key));

        if (Is_Antiform(val))  // Note: void is remove
            return FAIL(Error_Bad_Antiform(val));

        REBINT n = Find_Map_Entry(
            VAL_MAP_Ensure_Mutable(map),
            Element_ARG(key),
            val,  // non-nullptr means modify
            REF(case)
        );
        UNUSED(n);

        return COPY(ARG(value)); }

      case SYM_INSERT:
      case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PARAM(series));

        Value* value = ARG(value);
        if (Is_Void(value))
            return COPY(map);  // don't fail on read only if it would be a no-op

        if (not Is_Splice(value))
            return FAIL(
                "Appending to MAP! only accepts a splice block of key/value"
            );

        QUOTE_BYTE(value) = NOQUOTE_1;

        Map* m = VAL_MAP_Ensure_Mutable(map);

        if (REF(line) or REF(dup))
            return FAIL(Error_Bad_Refines_Raw());

        REBLEN len = Part_Len_May_Modify_Index(value, ARG(part));
        const Element* tail;
        const Element* at = Cell_List_At(&tail, value);  // w/modified index

        Append_Map(m, at, tail, len);

        return Init_Map(OUT, m); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        if (REF(part))
            return FAIL(Error_Bad_Refines_Raw());

        return Init_Map(OUT, Copy_Map(VAL_MAP(map), REF(deep))); }

      case SYM_CLEAR: {
        Map* m = VAL_MAP_Ensure_Mutable(map);

        Reset_Array(MAP_PAIRLIST(m));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Flex(MAP_HASHLIST(m));

        return Init_Map(OUT, m); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (Is_Antiform(picker))
            return RAISE(Error_Bad_Antiform(picker));

        bool strict = false;

        REBINT n = Find_Map_Entry(
            m_cast(Map*, VAL_MAP(map)),  // not modified
            c_cast(Element*, picker),
            nullptr,  // no value, so map not changed
            strict
        );

        if (n == 0)
            return nullptr;

        const Value* val = Array_At(
            MAP_PAIRLIST(VAL_MAP(map)),
            ((n - 1) * 2) + 1
        );
        if (Is_Zombie(val))
            return nullptr;

        return Copy_Cell(OUT, val); }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE: {
        INCLUDE_PARAMS_OF_POKE;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (Is_Antiform(picker))
            return RAISE(Error_Bad_Antiform(picker));

        // Fetching and setting with path-based access is case-preserving for
        // initial insertions.  However, the case-insensitivity means that all
        // writes after that to the same key will not be overriding the key,
        // it will just change the data value for the existing key.  SELECT and
        // the operation tentatively named PUT should be used if a map is to
        // distinguish multiple casings of the same key.
        //
        bool strict = false;

        Value* setval = ARG(value);  // Note: VOID interpreted as remove key

        if (Is_Void(setval)) {
            // removal signal
        }
        else if (Is_Antiform(setval))  // other antiforms not allowed in maps
            return RAISE(Error_Bad_Antiform(setval));

        REBINT n = Find_Map_Entry(
            VAL_MAP_Ensure_Mutable(map),  // modified
            c_cast(Element*, picker),
            setval,  // value to set (either ARG(value) or L->out)
            strict
        );

        assert(n != 0);
        UNUSED(n);

        return nullptr; }  // no upstream changes needed for Map* reference

      default:
        break;
    }

    return UNHANDLED;
}


// 1. MAP! does not retain order at this time.  It also allows you to
//    add duplicates in TO MAP!.  These undermine the reversibility
//    requirement, so that's currently disabled in To_Checker_Dispatcher()
//
IMPLEMENT_GENERIC(to, map) {
    INCLUDE_PARAMS_OF_TO;

    Element* map = Element_ARG(element);
    Heart to = VAL_TYPE_HEART(ARG(type));

    if (Any_List_Kind(to))  // !!! not ordered! [1]
        return Init_Any_List(OUT, to, Map_To_Array(VAL_MAP(map), 0));

    if (to == REB_MAP) {
        bool deep = false;
        return Init_Map(OUT, Copy_Map(VAL_MAP(map), deep));
    }

    return UNHANDLED;
}


IMPLEMENT_GENERIC(reflect, map)
{
    INCLUDE_PARAMS_OF_REFLECT;

    Element* map = Element_ARG(value);
    const Map* m = VAL_MAP(map);

    Option(SymId) id = Cell_Word_Id(ARG(property));

    switch (id) {
      case SYM_LENGTH:
        return Init_Integer(OUT, Num_Map_Entries_Used(m));

      case SYM_VALUES:
        return Init_Block(OUT, Map_To_Array(m, 1));

      case SYM_WORDS:
        return Init_Block(OUT, Map_To_Array(m, -1));

      case SYM_BODY:
        return Init_Block(OUT, Map_To_Array(m, 0));

      case SYM_TAIL_Q:
        return Init_Logic(OUT, Num_Map_Entries_Used(m) == 0);

      default:
        break;
    }

    return UNHANDLED;
}


//
//  /put: native:generic [
//
//  "Replaces the value following a key, and returns the new value"
//
//      return: [element?]
//      series [map!]
//      key [element?]
//      value [<maybe> element?]
//      :case "Perform a case-sensitive search"
//  ]
//
DECLARE_NATIVE(put)
//
// !!! PUT was added by Red as the complement to SELECT, which offers a /CASE
// refinement for adding keys to MAP!s case-sensitively.  The name may not
// be ideal, but it's something you can't do with path access, so adopting it
// for the time-being.  Only implemented for MAP!s at the moment
//
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(PUT));
}
