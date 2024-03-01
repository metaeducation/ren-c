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
#define Is_Zombie Is_Trash
#define ZOMBIE_CELL TRASH_CELL


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


//
//  Make_Map: C
//
// Makes a MAP block (that holds both keys and values).
// Capacity is measured in key-value pairings.
// A hash series is also created.
//
Map* Make_Map(REBLEN capacity)
{
    Array* pairlist = Make_Array_Core(capacity * 2, SERIES_MASK_PAIRLIST);
    LINK(Hashlist, pairlist) = Make_Hash_Series(capacity);

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
    Array* array,
    Series* hashlist,
    const Element* key,  // !!! assumes ++key finds the values
    REBLEN wide,
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
    // Used and skip are co-primes, so is guaranteed that repeatedly
    // adding skip (and subtracting len when needed) all positions are
    // visited.  1 <= skip < len, and len is prime, so this is guaranteed.
    //
    REBLEN used = Series_Used(hashlist);
    REBLEN *indexes = Series_Head(REBLEN, hashlist);

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
        Value* k = Series_At(Value, array, (n - 1) * wide); // stored key
        if (0 == Cmp_Value(k, key, true)) {
            if (strict)
                return slot; // don't need to check synonyms, stop looking
            goto found_synonym; // confirm exact match is the only match
        }

        if (not strict) {  // now do the non strict match (false)
            if (0 == Cmp_Value(k, key, false)) {

              found_synonym:;

                if (synonym_slot != -1) // another equivalent already matched
                    fail (Error_Conflicting_Key_Raw(key));
                synonym_slot = slot; // save and continue checking
            }
        }

        if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
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
        Copy_Cell(
            Array_At(array, (indexes[slot] - 1) * wide),
            key
        );
    }

    if (mode > 1) { // append new value to the target series
        const Element* src = key;
        indexes[slot] = (Array_Len(array) / wide) + 1;

        REBLEN index;
        for (index = 0; index < wide; ++src, ++index)
            Append_Value(array, src);
    }

    return (mode > 0) ? -1 : cast(REBINT, slot);
}


//
//  Rehash_Map: C
//
// Recompute the entire hash table for a map. Table must be large enough.
//
static void Rehash_Map(Map* map)
{
    Series* hashlist = MAP_HASHLIST(map);

    if (!hashlist) return;

    REBLEN *hashes = Series_Head(REBLEN, hashlist);
    Array* pairlist = MAP_PAIRLIST(map);

    Value* key = Array_Head(pairlist);
    REBLEN n;

    for (n = 0; n < Array_Len(pairlist); n += 2, key += 2) {
        const bool cased = true; // cased=true is always fine

        if (Is_Zombie(key + 1)) {
            //
            // It's a "zombie", move last key to overwrite it
            //
            Copy_Cell(
                key, Array_At(pairlist, Array_Len(pairlist) - 2)
            );
            Copy_Cell(
                &key[1], Array_At(pairlist, Array_Len(pairlist) - 1)
            );
            Set_Series_Len(pairlist, Array_Len(pairlist) - 2);
        }

        REBLEN hash = Find_Key_Hashed(
            pairlist, hashlist, cast(Element*, key), 2, cased, 0
        );
        hashes[hash] = n / 2 + 1;

        // discard zombies at end of pairlist
        //
        while (
            Is_Zombie(Series_At(Value, pairlist, Array_Len(pairlist) - 1))
        ){
            Set_Series_Len(pairlist, Array_Len(pairlist) - 2);
        }
    }
}


//
//  Expand_Hash: C
//
// Expand hash series. Clear it but set its tail.
//
void Expand_Hash(Series* ser)
{
    assert(not Is_Series_Array(ser));

    REBINT prime = Get_Hash_Prime_May_Fail(Series_Used(ser) + 1);
    Remake_Series(
        ser,
        prime + 1,
        SERIES_FLAG_POWER_OF_2  // not(NODE_FLAG_NODE) => don't keep data
    );

    Clear_Series(ser);
    Set_Series_Len(ser, prime);
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
    Series* hashlist = MAP_HASHLIST(map); // can be null
    Array* pairlist = MAP_PAIRLIST(map);

    assert(hashlist);

    // Get hash table, expand it if needed:
    if (Array_Len(pairlist) > Series_Used(hashlist) / 2) {
        Expand_Hash(hashlist); // modifies size value
        Rehash_Map(map);
    }

    const REBLEN wide = 2;
    const Byte mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, wide, strict, mode
    );

    REBLEN *indexes = Series_Head(REBLEN, hashlist);
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

    // Must set the value:
    if (n) {  // re-set it:
        Copy_Cell(
            Series_At(Value, pairlist, ((n - 1) * 2) + 1),
            Is_Void(unwrap(val)) ? ZOMBIE_CELL : unwrap(val)
        );
        return n;
    }

    if (Is_Void(unwrap(val)))
        return 0;  // trying to remove non-existing key

    assert(not Is_Antiform(unwrap(val)));

    // Create new entry.  Note that it does not copy underlying series (e.g.
    // the data of a string), which is why the immutability test is necessary
    //
    Append_Value(pairlist, key);
    Append_Value(pairlist, c_cast(Element*, unwrap(val)));  // val not void

    return (indexes[slot] = (Array_Len(pairlist) / 2));
}


//
//  Append_Map: C
//
static void Append_Map(
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
            // Keys with no value not allowed, e.g. `make map! [1 "foo" 2]`
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


//
//  MAKE_Map: C
//
Bounce MAKE_Map(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (Any_Number(arg)) {
        return Init_Map(OUT, Make_Map(Int32s(arg, 0)));
    }
    else {
        // !!! R3-Alpha TO of MAP! was like MAKE but wouldn't accept just
        // being given a size.
        //
        return TO_Map(level_, kind, arg);
    }
}


inline static Map* Copy_Map(const Map* map, bool deeply) {
    Array* copy = Copy_Array_Shallow_Flags(
        MAP_PAIRLIST(map),
        SERIES_MASK_PAIRLIST
    );

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    Series* hashlist = Copy_Series_Core(
        MAP_HASHLIST(map),
        SERIES_FLAGS_NONE | FLAG_FLAVOR(HASHLIST)
            // ^-- !!! No NODE_FLAG_MANAGED?
    );
    LINK(Hashlist, copy) = hashlist;

    if (not deeply)
        return cast(Map*, copy);  // shallow is ok

    // Even if the type flags request deep copies of series, none of the keys
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
//  TO_Map: C
//
Bounce TO_Map(Level* level_, Kind kind, const Value* arg)
{
    assert(kind == REB_MAP);
    UNUSED(kind);

    if (Is_Block(arg) || Is_Group(arg)) {
        //
        // make map! [word val word val]
        //
        REBLEN len = Cell_Series_Len_At(arg);
        const Element* tail;
        const Element* at = Cell_Array_At(&tail, arg);

        Map* map = Make_Map(len / 2); // [key value key value...] + END
        Append_Map(map, at, tail, len);
        Rehash_Map(map);
        return Init_Map(OUT, map);
    }
    else if (Is_Map(arg)) {
        //
        // Values are not copied deeply by default.
        //
        // !!! Is there really a use in allowing MAP! to be converted TO a
        // MAP! as opposed to having people COPY it?
        //
        bool deeply = false;

        return Init_Map(OUT, Copy_Map(VAL_MAP(arg), deeply));
    }

    return RAISE(arg);
}


//
//  Map_To_Array: C
//
// what: -1 - words, +1 - values, 0 -both
//
Array* Map_To_Array(const Map* map, REBINT what)
{
    REBLEN count = Length_Map(map);
    Array* a = Make_Array(count * ((what == 0) ? 2 : 1));

    Element* dest = Array_Head(a);
    const Value* val_tail = Series_Tail(Value, MAP_PAIRLIST(map));
    const Value* val = Series_Head(Value, MAP_PAIRLIST(map));
    for (; val != val_tail; val += 2) {
        if (Is_Zombie(val + 1))  // val + 1 can't be past tail
            continue;

        if (what <= 0) {
            Copy_Cell(dest, c_cast(Element*, &val[0]));  // no keys void
            ++dest;
        }
        if (what >= 0) {
            Copy_Cell(dest, c_cast(Element*, &val[1]));  // val tested non void
            ++dest;
        }
    }

    Set_Series_Len(a, dest - Array_Head(a));
    return a;
}


//
//  Alloc_Context_From_Map: C
//
Context* Alloc_Context_From_Map(const Map* map)
{
    // Doesn't use Length_Map because it only wants to consider words.
    //
    // !!! Should this fail() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! make map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    REBLEN count = 0;

  blockscope {
    const Value* mval_tail = Series_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Series_Head(Value, MAP_PAIRLIST(map));
    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1))
            ++count;
    }
  }

    // See Alloc_Context() - cannot use it directly because no Collect_Words

    Context* c = Alloc_Context(REB_OBJECT, count);

    const Value* mval_tail = Series_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Series_Head(Value, MAP_PAIRLIST(map));

    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1)) {
            Value* var = Append_Context(c, Cell_Word_Symbol(mval));
            Copy_Cell(var, mval + 1);
        }
    }

    return c;
}


//
//  MF_Map: C
//
void MF_Map(REB_MOLD *mo, const Cell* v, bool form)
{
    const Map* m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Series(g_mold.stack, m) != NOT_FOUND) {
        Append_Ascii(mo->series, "...]");
        return;
    }

    Push_Pointer_To_Series(g_mold.stack, m);

    if (not form) {
        Pre_Mold(mo, v);
        Append_Codepoint(mo->series, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    const Value* tail = Series_Tail(Value, MAP_PAIRLIST(m));
    const Value* key = Series_Head(Value, MAP_PAIRLIST(m));
    for (; key != tail; key += 2) {  // note value slot must not be END
        assert(key + 1 != tail);
        if (Is_Zombie(key + 1))
            continue;  // key has been removed

        if (not form)
            New_Indented_Line(mo);
        Mold_Value(mo, c_cast(Element*, key));
        Append_Codepoint(mo->series, ' ');
        Mold_Value(mo, c_cast(Element*, key + 1));
        if (form)
            Append_Codepoint(mo->series, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Codepoint(mo->series, ']');
    }

    End_Mold(mo);

    Drop_Pointer_From_Series(g_mold.stack, m);
}


//
//  REBTYPE: C
//
REBTYPE(Map)
{
    Value* map = D_ARG(1);

    switch (Symbol_Id(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `v`

        const Map* m = VAL_MAP(map);

        Value* property = ARG(property);
        switch (Cell_Word_Id(property)) {
          case SYM_LENGTH:
            return Init_Integer(OUT, Length_Map(m));

          case SYM_VALUES:
            return Init_Block(OUT, Map_To_Array(m, 1));

          case SYM_WORDS:
            return Init_Block(OUT, Map_To_Array(m, -1));

          case SYM_BODY:
            return Init_Block(OUT, Map_To_Array(m, 0));

          case SYM_TAIL_Q:
            return Init_Logic(OUT, Length_Map(m) == 0);

          default:
            break;
        }
        fail (Error_Cannot_Reflect(REB_MAP, property)); }

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        if (Is_Antiform(ARG(value)))
            fail (ARG(value));

        UNUSED(PARAM(series));  // covered by `v`
        UNUSED(PARAM(tail));  // returning tail not supported

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        const Map* m = VAL_MAP(map);

        REBINT n = Find_Map_Entry(
            m_cast(Map*, VAL_MAP(map)),  // should not modify, see below
            cast(Element*, ARG(value)),
            nullptr,  // nullptr indicates it will only search, not modify
            REF(case)
        );

        if (n == 0)
            return nullptr;

        const Value* val = Series_At(Value, MAP_PAIRLIST(m), ((n - 1) * 2) + 1);
        if (Is_Zombie(val))
            return nullptr;

        return Copy_Cell(OUT, val); }

      case SYM_PUT: {
        INCLUDE_PARAMS_OF_PUT;
        UNUSED(ARG(series)); // extracted to `map`

        Value* key = ARG(key);
        Value* val = ARG(value);

        if (Is_Void(key))
            fail (Error_Bad_Void());  // tolerate?
        if (Is_Antiform(key))
            fail (Error_Bad_Antiform(key));

        if (Is_Antiform(val))  // Note: void is remove
            fail (Error_Bad_Antiform(val));

        REBINT n = Find_Map_Entry(
            VAL_MAP_Ensure_Mutable(map),
            cast(Element*, ARG(key)),
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
            fail ("Appending to MAP! only accepts a splice block of key/value");

        QUOTE_BYTE(value) = NOQUOTE_1;

        Map* m = VAL_MAP_Ensure_Mutable(map);

        if (REF(line) or REF(dup))
            fail (Error_Bad_Refines_Raw());

        REBLEN len = Part_Len_May_Modify_Index(value, ARG(part));
        const Element* tail;
        const Element* at = Cell_Array_At(&tail, value);  // w/modified index

        Append_Map(m, at, tail, len);

        return Init_Map(OUT, m); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        return Init_Map(OUT, Copy_Map(VAL_MAP(map), did REF(deep))); }

      case SYM_CLEAR: {
        Map* m = VAL_MAP_Ensure_Mutable(map);

        Reset_Array(MAP_PAIRLIST(m));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Series(MAP_HASHLIST(m));

        return Init_Map(OUT, m); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
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

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
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

    fail (UNHANDLED);
}
