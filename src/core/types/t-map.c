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


IMPLEMENT_GENERIC(EQUAL_Q, Is_Map)
//
// !!! Was never implemented in R3-Alpha; called into raw array comparison,
// which is clearly incorrect.  Needs to be written.
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    UNUSED(strict);
    UNUSED(v1);
    UNUSED(v2);

    panic ("https://github.com/rebol/rebol-issues/issues/2340");
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
    Array* pairlist = Make_Array_Core(STUB_MASK_PAIRLIST, capacity * 2);
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
    const Value* key,  // !!! assumes ++key finds the values
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

    uint32_t hash = Hash_Cell(key);
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

        attempt {
            bool equal_strict = require (Equal_Values(k, key, true));
            if (equal_strict) {
                if (strict)
                    return slot; // don't need to check synonyms, stop looking
            }
            else {
                if (strict)
                    goto continue_checking;

                bool equal_lax = require (Equal_Values(k, key, false));
                if (not equal_lax)
                    goto continue_checking;
            }
        }
        then {  // found synonym
            if (synonym_slot != -1) // another equivalent already matched
                panic (Error_Conflicting_Key_Raw(key));
            synonym_slot = slot; // save and continue checking
        }

      continue_checking: {

        if (wide > 1 && Is_Zombie(k + 1) && zombie_slot == -1)
            zombie_slot = slot;

        slot += skip;
        if (slot >= num_slots)
            slot -= num_slots;
    }}

    if (synonym_slot != -1) {
        assert(not strict);
        return synonym_slot; // there weren't other spellings of the same key
    }

    if (zombie_slot != -1) { // zombie encountered; overwrite with new key
        assert(mode == 0);
        slot = zombie_slot;
        Copy_Cell(
            u_cast(Value*, Array_At(array, (indexes[slot] - 1) * wide)),
            key
        );
    }

    if (mode > 1) { // append new value to the target array
        const Value* src = key;
        indexes[slot] = (Array_Len(array) / wide) + 1;

        REBLEN index;
        for (index = 0; index < wide; ++src, ++index)
            Copy_Cell(u_cast(Value*, Alloc_Tail_Array(array)), src);
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
            pairlist, hashlist, key, 2, cased, 0
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

    REBINT prime = Get_Hash_Prime_May_Panic(Hashlist_Num_Slots(hashlist) + 1);
    Remake_Flex(
        hashlist,
        prime + 1,
        FLEX_FLAG_POWER_OF_2  // not(BASE_FLAG_BASE) => don't keep data
    );

    Clear_Flex(hashlist);
    Set_Flex_Len(hashlist, prime);
}


//
//  Find_Map_Entry: C
//
// Try to find the entry in the map.  Returns index to value if found.
//
// RETURNS: the index to the VALUE or zero if there is none.
//
Option(Index) Find_Map_Entry(
    Map* map,
    const Value* key,
    bool strict
) {
    HashList* hashlist = MAP_HASHLIST(map);
    PairList* pairlist = MAP_PAIRLIST(map);

    const REBLEN wide = 2;
    const Byte mode = 0; // just search for key, don't add it
    REBLEN slot = Find_Key_Hashed(
        pairlist, hashlist, key, wide, strict, mode
    );

    REBLEN *indexes = Flex_Head(REBLEN, hashlist);
    REBLEN n = indexes[slot];

    return n;  // n==0 or pairlist[(n-1)*]=~key
}


//
//  Update_Map_Entry: C
//
// Add or change/remove entry in the map.  Returns the index to the value.
//
// 1. Since copies of keys are never made, a SET must always be done with an
//    immutable key...because if it were changed, there'd be no notification
//    to rehash the map.  We don't force the caller do the work of freezing the
//    key since they often won't care it got frozen automatically (if they
//    don't want to freeze the key they have they can index into the map using
//    a copy).
//
//    We freeze unconditionally, even if the key is already in the map, since
//    variance in behavior based on the presence of the key is undesirable.
//
Option(Index) Update_Map_Entry(
    Map* map,
    const Value* key,
    Option(const Value*) val,  // nullptr (not nulled cell) is remove
    bool strict
){
    Force_Value_Frozen_Deep_Blame(key, MAP_PAIRLIST(map));  // freeze [1]

    HashList* hashlist = MAP_HASHLIST(map);
    PairList* pairlist = MAP_PAIRLIST(map);

    if (Array_Len(pairlist) > Hashlist_Num_Slots(hashlist) / 2) {  // expand
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

    if (n) {  // found, must set or overwrite the value
        Value* at = Flex_At(Value, pairlist, ((n - 1) * 2) + 1);
        if (not val)  // remove
            Init_Zombie(at);
        else
            Copy_Cell(at, unwrap val);
        return n;
    }

    if (not val)
        return 0;  // trying to remove non-existing key

    Copy_Cell(u_cast(Value*, Alloc_Tail_Array(pairlist)), key);
    Copy_Cell(u_cast(Value*, Alloc_Tail_Array(pairlist)), unwrap val);

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
            panic (Error_Index_Out_Of_Range_Raw());
        }

        bool strict = true;
        Update_Map_Entry(
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
IMPLEMENT_GENERIC(MAKE, Is_Map)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Type(ARG(TYPE)) == TYPE_MAP);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    enum {
        ST_MAKE_MAP_INITIAL_ENTRY,
        ST_MAKE_MAP_EVAL_STEP_KEY,
        ST_MAKE_MAP_EVAL_STEP_VALUE
    };

    switch (STATE) {
      case ST_MAKE_MAP_INITIAL_ENTRY: goto initial_entry;
      case ST_MAKE_MAP_EVAL_STEP_KEY: goto key_step_dual_in_out;
      case ST_MAKE_MAP_EVAL_STEP_VALUE: goto value_step_dual_in_out;
      default: assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    if (Any_Number(arg))
        return Init_Map(OUT, Make_Map(Int32s(arg, 0)));

    Executor* executor;
    if (Is_Pinned_Form_Of(BLOCK, arg))
        executor = &Inert_Stepper_Executor;
    else {
        if (not Is_Block(arg))
            panic (Error_Bad_Make(TYPE_MAP, arg));

        executor = &Stepper_Executor;
    }

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    Level* sub = Make_Level_At(executor, arg, flags);
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

} reduce_key: { /////////////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    STATE = ST_MAKE_MAP_EVAL_STEP_KEY;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} key_step_dual_in_out: { ////////////////////////////////////////////////////

    if (Is_Endlike_Unset(SPARE))  // no more key, not a problem, done
        goto finished;

    if (Is_Ghost(SPARE))
        goto reduce_key;  // try again...

    Value* key = require (Decay_If_Unstable(SPARE));
    if (Is_Nulled(key) or Is_Trash(key))
        panic ("Null or trash can't be used as key in MAP!");

    Copy_Cell(PUSH(), key);

    goto reduce_value;

} reduce_value: { ///////////////////////////////////////////////////////////

    STATE = ST_MAKE_MAP_EVAL_STEP_VALUE;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} value_step_dual_in_out: { //////////////////////////////////////////////////

    if (Is_Endlike_Unset(SPARE))  // no value for key, that's an error
        panic ("Key without value terminating MAKE MAP!");

    if (Is_Ghost(SPARE))
        goto reduce_value;  // try again...

    Value* val = require (Decay_If_Unstable(SPARE));
    if (Is_Nulled(val) or Is_Trash(val))
        panic ("Null or trash can't be used as value in MAP!");

    if (  // give label to action/frame if it's a word and doesn't have one
        Is_Word(TOP)
        and (Is_Action(val) or Is_Frame(val))
        and not Cell_Frame_Label_Deep(val)
    ){
        Update_Frame_Cell_Label(val, Word_Symbol(TOP));
    }

    Copy_Cell(PUSH(), val);

    goto reduce_key;

} finished: { ////////////////////////////////////////////////////////////////

    Array* pairlist = Pop_Stack_Values_Core(
        STUB_MASK_PAIRLIST | BASE_FLAG_MANAGED,
        STACK_BASE
    );
    assert(Array_Len(pairlist) % 2 == 0);  // is [key value key value...]
    Count capacity = Array_Len(pairlist) / 2;
    Tweak_Link_Hashlist(pairlist, Make_Hashlist(capacity));

    Map* map = cast(Map*, pairlist);
    Init_Map(OUT, map);  // !!! Note: hashlist invalid...

    Drop_Level(SUBLEVEL);

    Rehash_Map(map);  // !!! Rehash calls evaluator for equality testing!
    return OUT;
}}


INLINE Map* Copy_Map(const Map* map, bool deeply) {
    Array* copy = Copy_Array_Shallow_Flags(
        STUB_MASK_PAIRLIST,
        MAP_PAIRLIST(map)
    );

    // So long as the copied pairlist is the same array size as the original,
    // a literal copy of the hashlist can still be used, as a start (needs
    // its own copy so new map's hashes will reflect its own mutations)
    //
    HashList* hashlist = cast(HashList*, Copy_Flex_Core(
        FLEX_FLAGS_NONE | FLAG_FLAVOR(FLAVOR_HASHLIST),  // !!! No BASE_FLAG_MANAGED?
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

        Flags flags = BASE_FLAG_MANAGED;  // !!! Review
        if (not Is_Antiform(v)) {
            Clonify(Known_Element(v), flags, deeply);
        }
    }

    return cast(Map*, copy);
}


//
//  Map_To_Array: C
//
// what: -1 => words, +1 => values, 0 => both
//
Source* Map_To_Array(const Map* map, REBINT what)
{
    assert(what == 0 or what == 1 or what == -1);

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
    // !!! Should this panic() if any of the keys aren't words?  It seems
    // a bit haphazard to have `make object! to map! [x 10 <y> 20]` and
    // just throw out the <y> 20 case...

    REBLEN count = 0;

  count_entries: {

    const Value* mval_tail = Flex_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Flex_Head(Value, MAP_PAIRLIST(map));
    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1))
            ++count;
    }

} copy_to_varlist: {

    // See Alloc_Varlist() - cannot use it directly because no Collect_Words

    VarList* c = Alloc_Varlist(TYPE_OBJECT, count);

    const Value* mval_tail = Flex_Tail(Value, MAP_PAIRLIST(map));
    const Value* mval = Flex_Head(Value, MAP_PAIRLIST(map));

    for (; mval != mval_tail; mval += 2) {  // note mval must not be END
        if (Any_Word(mval) and not Is_Zombie(mval + 1)) {
            Init(Slot) slot = Append_Context(c, Word_Symbol(mval));
            Copy_Cell(slot, mval + 1);
        }
    }

    return c;
}}


IMPLEMENT_GENERIC(MOLDIFY, Is_Map)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    const Map* m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Flex(g_mold.stack, m) != NOT_FOUND) {
        Append_Ascii(mo->strand, "...]");
        return TRIPWIRE;
    }

    Push_Pointer_To_Flex(g_mold.stack, m);

    if (not form) {
        Begin_Non_Lexical_Mold(mo, v);
        Append_Codepoint(mo->strand, '[');
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

        DECLARE_ELEMENT (lifted_key);
        Copy_Lifted_Cell(lifted_key, key);
        Mold_Element(mo, lifted_key);

        Append_Codepoint(mo->strand, ' ');

        DECLARE_ELEMENT (lifted_value);
        Copy_Lifted_Cell(lifted_value, key + 1);
        Mold_Element(mo, lifted_value);

        if (form)
            Append_Codepoint(mo->strand, '\n');
    }
    mo->indent--;

    if (not form) {
        New_Indented_Line(mo);
        Append_Codepoint(mo->strand, ']');
    }

    End_Non_Lexical_Mold(mo);

    Drop_Pointer_From_Flex(g_mold.stack, m);

    return TRIPWIRE;
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Map)
{
    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    Element* map = cast(Element*, ARG_N(1));
    assert(Is_Map(map));

    switch (maybe id) {
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;

        UNUSED(PARAM(SERIES));  // covered by `v`

        if (Bool_ARG(PART) or Bool_ARG(SKIP) or Bool_ARG(MATCH))
            panic (Error_Bad_Refines_Raw());

        const Map* m = VAL_MAP(map);

        Option(Index) n = Find_Map_Entry(
            m_cast(Map*, VAL_MAP(map)),  // should not modify, see below
            ARG(VALUE),
            Bool_ARG(CASE)
        );

        if (not n)
            return NULLED;

        const Value* val = Flex_At(
            Value,
            MAP_PAIRLIST(m),
            (((unwrap n) - 1) * 2) + 1
        );
        if (Is_Zombie(val))
            return NULLED;

        return Copy_Cell(OUT, val); }

      case SYM_INSERT:
      case SYM_APPEND: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PARAM(SERIES));

        if (Is_Undone_Opt_Nulled(ARG(VALUE)))
            return COPY(map);  // don't panic on read only if it would be no-op

        if (not Is_Splice(ARG(VALUE)))
            panic (
                "Appending to MAP! only accepts a splice block of key/value"
            );

        LIFT_BYTE(ARG(VALUE)) = NOQUOTE_2;
        Element* arg = Element_ARG(VALUE);

        Map* m = VAL_MAP_Ensure_Mutable(map);

        if (Bool_ARG(LINE) or Bool_ARG(DUP))
            panic (Error_Bad_Refines_Raw());

        REBLEN len = Part_Len_May_Modify_Index(arg, ARG(PART));
        const Element* tail;
        const Element* at = List_At(&tail, arg);  // w/modified index

        Append_Map(m, at, tail, len);

        return Init_Map(OUT, m); }

      case SYM_CLEAR: {
        Map* m = VAL_MAP_Ensure_Mutable(map);

        Reset_Array(MAP_PAIRLIST(m));

        // !!! Review: should the space for the hashlist be reclaimed?  This
        // clears all the indices but doesn't scale back the size.
        //
        Clear_Flex(MAP_HASHLIST(m));

        return Init_Map(OUT, m); }

      default:
        break;
    }

    panic (UNHANDLED);
}


// 1. MAP! does not retain order at this time.  It also allows you to
//    add duplicates in TO MAP!.  These undermine the reversibility
//    requirement, so that's currently disabled in To_Checker_Dispatcher()
//
IMPLEMENT_GENERIC(TO, Is_Map) {
    INCLUDE_PARAMS_OF_TO;

    Element* map = Element_ARG(ELEMENT);
    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_List_Type(to))  // !!! not ordered! [1]
        return Init_Any_List(OUT, to, Map_To_Array(VAL_MAP(map), 0));

    if (to == TYPE_MAP) {
        bool deep = false;
        return Init_Map(OUT, Copy_Map(VAL_MAP(map), deep));
    }

    panic (UNHANDLED);
}


IMPLEMENT_GENERIC(COPY, Is_Map)
{
    INCLUDE_PARAMS_OF_COPY;

    const Element* map = Element_ARG(VALUE);

    if (Bool_ARG(PART))
        panic (Error_Bad_Refines_Raw());

    return Init_Map(OUT, Copy_Map(VAL_MAP(map), Bool_ARG(DEEP)));
}


// 1. Fetching and setting with path-based access is case-preserving for
//    initial insertions.  However, the case-insensitivity means that all
//    writes after that to the same key will not be overriding the key,
//    it will just change the data value for the existing key.  SELECT and
//    the operation tentatively named PUT should be used if a map is to
//    distinguish multiple casings of the same key.
//
IMPLEMENT_GENERIC(TWEAK_P, Is_Map)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* map = Element_ARG(LOCATION);

    const Value* picker = ARG(PICKER);
    assert(not Is_Keyword(picker) and not Is_Trash(picker));

    bool strict = false;  // case-preserving [1]

    Option(Value*) poke;

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        if (Is_Dual_Word_Remove_Signal(dual)) {
            poke = nullptr;  // remove signal
            goto handle_poke;
        }

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    Unliftify_Known_Stable(dual);

    if (Is_Nulled(dual) or Is_Trash(dual))
        panic (Error_Bad_Antiform(dual));

    poke = dual;

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    Option(Index) n = Find_Map_Entry(
        m_cast(Map*, VAL_MAP(map)),  // not modified
        picker,
        strict
    );

    if (not n)
        return DUAL_SIGNAL_NULL_ABSENT;

    const Value* val = Flex_At(
        Value,
        MAP_PAIRLIST(VAL_MAP(map)),
        (((unwrap n) - 1) * 2) + 1
    );
    if (Is_Zombie(val))
        return DUAL_SIGNAL_NULL_ABSENT;

    return DUAL_LIFTED(Copy_Cell(OUT, val));

} handle_poke: { /////////////////////////////////////////////////////////////

    Update_Map_Entry(
        VAL_MAP_Ensure_Mutable(map),  // modified
        picker,
        poke,
        strict
    );

    return NO_WRITEBACK_NEEDED;  // no upstream change for Map* reference
}}


IMPLEMENT_GENERIC(LENGTH_OF, Is_Map)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* map = Element_ARG(ELEMENT);
    const Map* m = VAL_MAP(map);

    return Init_Integer(OUT, Num_Map_Entries_Used(m));
}


IMPLEMENT_GENERIC(WORDS_OF, Is_Map)
{
    INCLUDE_PARAMS_OF_WORDS_OF;

    Element* map = Element_ARG(ELEMENT);
    const Map* m = VAL_MAP(map);

    return Init_Block(OUT, Map_To_Array(m, -1));
}


IMPLEMENT_GENERIC(VALUES_OF, Is_Map)
{
    INCLUDE_PARAMS_OF_VALUES_OF;

    Element* map = Element_ARG(ELEMENT);
    const Map* m = VAL_MAP(map);

    return Init_Block(OUT, Map_To_Array(m, 1));
}


IMPLEMENT_GENERIC(TAIL_Q, Is_Map)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* map = Element_ARG(ELEMENT);
    const Map* m = VAL_MAP(map);

    return LOGIC(Num_Map_Entries_Used(m) == 0);
}
