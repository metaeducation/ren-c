//
//  file: %stub-action.h
//  summary: "action! defs AFTER %tmp-internals.h (see: %struct-action.h)"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Every action has an associated context known as the "exemplar" that defines
// the parameters and locals.  The keylist of this exemplar is reused for
// FRAME! instances of invocations (or pending invocations) of the action.
//
// The varlist of the exemplar context is referred to as a "paramlist".  It
// is an array that serves two overlapping purposes: any *unspecialized*
// slots in the paramlist holds the TYPESET! definition of legal types for
// that argument, as well as the PARAMETER_FLAG_XXX for other properties of the
// parameter.  But a *specialized* parameter slot holds the specialized value
// itself, which is presumed to have been type-checked upon specialization.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTES:
//
// * Unlike contexts, an ACTION! does not have values of its own, only
//   parameter definitions (or "params").  The arguments ("args") come from an
//   action's instantiation on the stack, viewed as a context using a FRAME!.
//
// * Paramlists may contain hidden fields, if they are specializations...
//   because they have to have the right number of slots to line up with the
//   frame of the underlying function.
//
// * The `misc.meta` field of the details may hold an adjunct object that
//   describes the function.  This is read by help.  A similar facility is
//   enabled by the `misc.meta` field of varlists.
//
// * By storing the C function dispatcher pointer in the `details` array base
//   instead of in the value cell itself, it also means the dispatcher can be
//   HIJACKed--or otherwise hooked to affect all instances of a function.
//


//=//// FRAME! Cell Slot Definitions //////////////////////////////////////=//
//
// These are used in the phase archetype slot, so they need to be defined
// earlier than %cell-frame.h
//
// 1. We choose to put the Lens/Label in the extra slot, because it's more
//    likely to be changed or tweaked over the lifetime of a single Cell,
//    and may be tweaked to or from null.

#define CELL_FRAME_EXTRA_LENS_OR_LABEL(c)  CELL_EXTRA(c)  // maybe null [1]
#define CELL_FRAME_PAYLOAD_1_PHASE(c)  CELL_PAYLOAD_1(c)  // never null
#define CELL_FRAME_PAYLOAD_2_COUPLING(c)  CELL_PAYLOAD_2(c)  // maybe null [1]




// For performance, all Details and VarList stubs are STUB_FLAG_DYNAMIC.
//
#define Phase_Archetype(phase) \
    Flex_Head_Dynamic(Element, ensure(Phase*, (phase)))

INLINE Details* Phase_Details(Phase* p) {
    while (not Is_Stub_Details(p)) {
        p = cast(Phase*, CELL_FRAME_PAYLOAD_1_PHASE(Phase_Archetype(p)));
    }
    return cast(Details*, p);
}


INLINE bool Is_Frame_Details(const Cell* v) {
    assert(Heart_Of(v) == TYPE_FRAME);
    return Is_Stub_Details(cast(Stub*, CELL_FRAME_PAYLOAD_1_PHASE(v)));
}

#define Is_Frame_Exemplar(v) (not Is_Frame_Details(v))


//=//// PARAMLIST, EXEMPLAR, AND PARTIALS /////////////////////////////////=//
//
// Since partial specialization is somewhat rare, it is an optional splice
// before the place where the exemplar is to be found.
//

INLINE void Tweak_Frame_Lens_Or_Label(Cell* c, Option(const Stub*) f) {
    assert(Heart_Of(c) == TYPE_FRAME);
    CELL_FRAME_EXTRA_LENS_OR_LABEL(c) = m_cast(Stub*, maybe f);  // no flag
}

INLINE ParamList* Phase_Paramlist(Phase* p) {
    while (Is_Stub_Details(p))
        p = u_cast(Phase*, CELL_FRAME_PAYLOAD_1_PHASE(Phase_Archetype(p)));
    return u_cast(ParamList*, p);
}

// More optimized version of Bonus_Keylist(Phase_Paramlist(a)),
// and also forward declared.
//
#define Phase_Keylist(p) \
    Bonus_Keylist(Phase_Paramlist(p))

#define Phase_Keys_Head(p) \
    Flex_Head(const Key, Phase_Keylist(p))

#define Phase_Keys(tail,p) \
    Varlist_Keys((tail), Phase_Paramlist(p))

INLINE Param* Phase_Params_Head(Phase* p) {
    ParamList* list = Phase_Paramlist(p);
    return Flex_Head_Dynamic(Param, list) + 1;  // skip archetype
}

INLINE Dispatcher* Details_Dispatcher(Details* details)
  { return f_cast(Dispatcher*, LINK_DETAILS_DISPATCHER(details)); }

INLINE void Tweak_Details_Dispatcher(Details* details, Dispatcher* dispatcher)
 { LINK_DETAILS_DISPATCHER(details) = f_cast(CFunction*, dispatcher); }


// The Array is the details identity itself.
//
INLINE Array* Details_Array(Details* details) {
    assert(Is_Stub_Details(details));
    return u_cast(Array*, details);  // performance critical, u_cast()
}


// Details is not a subclass of Array, because it's a subclass of Phase...
// and Phase isn't a subclass of Array.  So accessing the items of the array
// is done with Details_At().
//
// The Details_Array() isn't guaranteed to be STUB_FLAG_DYNAMIC (it may hold
// only the archetype).  *BUT* if you are asking for elements in the details
// array, you must know it is dynamic.  So we can take advantage of that for
// better performance.
//
INLINE Value* Details_At(Details* details, Index idx) {
    Array* a = Details_Array(details);
    assert(idx != 0 and idx < Array_Len(a));
    return Flex_Head_Dynamic(Value, a) + idx;
}

#define Details_Element_At(details,idx) \
    Known_Element(Details_At(details, idx))

#define Details_Max(details) \
    (Array_Len(Details_Array(details)) - 1)

#define IDX_DETAILS_1 1  // Common index used for code body location

// These are indices into the details array used by actions which have
// the DETAILS_RAW_NATIVE set.
//
enum {
    IDX_RAW_NATIVE_RETURN = 1,  // return type for natives
    MAX_IDX_RAW_NATIVE = IDX_RAW_NATIVE_RETURN
};

enum {
    IDX_COMBINATOR_BODY = 1,  // either native or BLOCK!
    MAX_IDX_COMBINATOR = IDX_COMBINATOR_BODY
};

// If you use Make_Interpreted_Action(), it will automatically put the
// relativized body into slot 1 of the details.  Referring to this IDX value
// in the IDX enums of things like LAMBDA and FUNC helps to make it clearer
// that the index position is reserved.
//
#define IDX_INTERPRETED_BODY  1

enum {
    IDX_TYPECHECKER_TYPESET_BYTE = 1,  // datatype or type constraint to check
    MAX_IDX_TYPECHECKER = IDX_TYPECHECKER_TYPESET_BYTE
};


INLINE const Symbol* Key_Symbol(const Key* key)
  { return *key; }


INLINE void Init_Key(Key* dest, const Symbol* symbol)
  { *dest = symbol; }

#define Key_Id(key) \
    Symbol_Id(Key_Symbol(key))

#define Phase_Key(a,n) \
    Varlist_Key(Phase_Paramlist(a), (n))

#define Phase_Param(a,n) \
    cast(Param*, Varlist_Slot(Phase_Paramlist(a), (n)))

#define Phase_Num_Params(a) \
    Varlist_Len(Phase_Paramlist(a))


//=//// ADJUNCT OBJECT ////////////////////////////////////////////////////=//
//
// ACTION! details and ANY-CONTEXT? varlists can store an "adjunct" object.
// The description for HELP is saved there for functions, and it's how modules
// store out-of-band information that doesn't appear in their body.
//

INLINE Option(VarList*) Misc_Details_Adjunct(Details* details)
  { return cast(VarList*, MISC_DETAILS_ADJUNCT(details)); }

INLINE void Tweak_Misc_Details_Adjunct(
    Stub* details,  // may not be formed yet
    Option(VarList*) adjunct
){
    assert(Is_Stub_Details(details));
    MISC_DETAILS_ADJUNCT(details) = maybe adjunct;
    if (adjunct)
        Set_Stub_Flag(details, MISC_NEEDS_MARK);
    else
        Clear_Stub_Flag(details, MISC_NEEDS_MARK);
}


//=//// ANCESTRY / FRAME COMPATIBILITY ////////////////////////////////////=//
//
// Keylist.link.base points at a keylist which has the same number of keys or
// fewer, which represents an object which this object is derived from.  Note
// that when new object instances are created which do not require expanding
// the object, their keylist will be the same as the object derived from.
//
// Paramlists have the same relationship, with each expansion (e.g. via
// AUGMENT) having larger frames pointing to the potentially shorter frames.
// (Something that reskins a paramlist might have the same size frame, with
// members that have different properties.)
//
// When you build a frame for an expanded action (e.g. with an AUGMENT) then
// it can be used to run phases that are from before it in the ancestry chain.
// This informs low-level asserts in the specific binding machinery, as
// well as determining whether higher-level actions can be taken (like if a
// sibling tail call would be legal, or if a certain HIJACK would be safe).
//
// !!! When ancestors were introduced, it was prior to AUGMENT and so frames
// did not have a concept of expansion.  So they only applied to keylists.
// The code for processing derivation is slightly different; it should be
// unified more if possible.

INLINE KeyList* Bonus_Keylist(VarList* c);

INLINE KeyList* Link_Keylist_Ancestor(KeyList* keylist) {
    KeyList* ancestor = cast(KeyList*, LINK_KEYLIST_ANCESTOR(keylist));
    assert(Is_Stub_Keylist(ancestor));
    possibly(ancestor == keylist);
    return ancestor;
}

INLINE void Tweak_Link_Keylist_Ancestor(KeyList* keylist, KeyList* ancestor) {
    possibly(keylist == ancestor);  // keylists terminate on self
    LINK_KEYLIST_ANCESTOR(keylist) = ancestor;
}

INLINE bool Action_Is_Derived_From(Phase* derived, Phase* base) {
    if (derived == base)
        return true;  // fast common case (review how common)

    if (Phase_Details(derived) == Phase_Details(base))
        return true;  // Covers COPY + HIJACK cases (seemingly)

    KeyList* keylist_test = Phase_Keylist(derived);
    KeyList* keylist_base = Phase_Keylist(base);
    while (true) {
        if (keylist_test == keylist_base)
            return true;

        KeyList* ancestor = Link_Keylist_Ancestor(keylist_test);
        if (ancestor == keylist_test)
            return false;  // signals end of the chain, no match found

        keylist_test = ancestor;
    }
}



#define Set_Details_Flag(p,name) \
    Set_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Get_Details_Flag(p,name) \
    Get_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Clear_Details_Flag(p,name) \
    Clear_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)

#define Not_Details_Flag(p,name) \
    Not_Flavor_Flag(DETAILS, ensure(Details*, (p)), name)


INLINE const Element* Quoted_Returner_Of_Paramlist(
    ParamList* paramlist,
    SymId returner
){
    assert(Key_Id(Phase_Keys_Head(paramlist)) == returner);
    UNUSED(returner);
    Value* param = Phase_Params_Head(paramlist);
    assert(
        LIFT_BYTE(param) == ONEQUOTE_NONQUASI_4
        and Heart_Of(param) == TYPE_PARAMETER
    );
    return cast(Element*, param);
}

// There's a minor compression used by FUNC and YIELDER which stores the type
// information for RETURN as a quoted PARAMETER! in the paramlist slot that
// defines the cell where the DEFINITIONAL-RETURN is put.
//
INLINE void Extract_Paramlist_Returner(
    Sink(Element) out,
    ParamList* paramlist,
    SymId returner
){
    const Element* param = Quoted_Returner_Of_Paramlist(paramlist, returner);
    Copy_Cell(out, param);
    LIFT_BYTE(out) = NOQUOTE_2;
}
