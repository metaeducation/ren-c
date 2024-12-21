//
//  File: %stub-action.h
//  Summary: "action! defs AFTER %tmp-internals.h (see: %struct-action.h)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// As in historical Rebol, Ren-C has several different kinds of functions...
// each of which have a different implementation path in the system.
// But in Ren-C there is only one user-visible datatype from the user's
// perspective for all of them, which is called "action" (FRAME! antiform).
//
// Each action has an associated C function that runs when it is invoked, and
// this is called the "dispatcher".  A dispatcher may be general and reused
// by many different actions.  For example: the same dispatcher code is used
// for most `FUNC [...] [...]` instances--but each one has a different body
// array and spec, so the behavior is different.  Other times a dispatcher can
// be for a single function, such as with natives like IF that have C code
// which is solely used to implement IF.
//
// The identity array for an action is called its "details".  It has an
// archetypal value for the action in its [0] slot, but the other slots are
// dispatcher-specific.  Different dispatchers lay out the details array with
// different values that define the action instance.
//
// Some examples:
//
//     USER FUNCTIONS: 1-element array w/a BLOCK!, the body of the function
//     GENERICS: 1-element array w/WORD! "verb" (OPEN, APPEND, etc)
//     SPECIALIZATIONS: no contents needed besides the archetype
//     ROUTINES/CALLBACKS: stylized array (REBRIN*)
//     TYPECHECKERS: the TYPESET! to check against
//
// (See the comments in the %src/core/functionals/ directory for each function
// variation for descriptions of how they use their details arrays.)
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
// * The `misc.meta` field of the details holds a meta object (if any) that
//   describes the function.  This is read by help.  A similar facility is
//   enabled by the `misc.meta` field of varlists.
//
// * By storing the C function dispatcher pointer in the `details` array node
//   instead of in the value cell itself, it also means the dispatcher can be
//   HIJACKed--or otherwise hooked to affect all instances of a function.
//


// Context types use this field of their varlist (which is the identity of
// an ANY-CONTEXT?) to find their "keylist".
//
// Note: At one time Level->varlist would swap in a Level* in this spot, in
// order to be able to find a running Level* from a VarList.  This was due to
// the belief that the Stub.misc field could not be sacrificed on FRAME! to
// store that Level*, because it was needed to store a link to the "adjunct
// object" which all VarList* wanted to offer.  It turns out that adjunct
// objects are not needed on running frame varlists, they can be on the phase.
//
#define BONUS_KeyList_TYPE        KeyList*
#define HAS_BONUS_KeyList         FLAVOR_VARLIST


//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into L->out.
//
// Other special instructions need to be encoded somehow:
//
// * We don't want to use UTF-8 signals like `return "C"` for BOUNCE_CONTINUE.
//   That would miss out on the opportunity to make `return "Some String"` a
//   synonym for `return rebText("Some String")` which is appealing.
//
// * Between "weird Cell" and "weird Stub" choices, "weird Cell" is smaller
//   (4 platform pointers instead of 8).  So we go with a cell using an
//   out-of-range HEART_BYTE.
//

INLINE Value* Init_Return_Signal_Untracked(Init(Value) out, char ch) {
    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART_BYTE(REB_T_RETURN_SIGNAL) | CELL_MASK_NO_NODES
    );
    Tweak_Cell_Binding(out, UNBOUND);
    out->payload.split.one.u = ch;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Return_Signal(out,ch) \
    TRACK(Init_Return_Signal_Untracked((out), (ch)))

INLINE char Cell_Return_Type(const Cell* cell) {
    assert(cast(Kind, Cell_Heart(cell)) == REB_T_RETURN_SIGNAL);
    return cast(char, cell->payload.split.one.u);
}

INLINE bool Is_Bounce_An_Atom(Bounce b)
  { return HEART_BYTE(cast(Value*, b)) != REB_T_RETURN_SIGNAL; }

INLINE char VAL_RETURN_SIGNAL(Bounce b) {
    assert(not Is_Bounce_An_Atom(b));
    return cast(Value*, b)->payload.split.one.u;
}

INLINE Atom* Atom_From_Bounce(Bounce b) {
    assert(Is_Bounce_An_Atom(b));
    return cast(Atom*, b);
}


#define Tweak_Cell_Frame_Phase  Tweak_Cell_Node1

// For performance, all Details and VarList stubs are STUB_FLAG_DYNAMIC.
//
#define Phase_Archetype(phase) \
    Flex_Head_Dynamic(Element, ensure(Phase*, (phase)))

INLINE Details* Phase_Details(Phase* p) {
    while (not Is_Stub_Details(p))
        p = cast(Phase*, Cell_Node1(Phase_Archetype(p)));
    return cast(Details*, p);
}


INLINE bool Is_Frame_Details(const Cell* v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    return Is_Stub_Details(cast(Stub*, Cell_Node1(v)));
}

#define Is_Frame_Exemplar(v) (not Is_Frame_Details(v))


//=//// PARAMLIST, EXEMPLAR, AND PARTIALS /////////////////////////////////=//
//
// Since partial specialization is somewhat rare, it is an optional splice
// before the place where the exemplar is to be found.
//

INLINE void Tweak_Cell_Frame_Lens_Or_Label(Cell* c, Option(const Flex*) f)
  { Tweak_Cell_Node2(c, maybe f); }

#define Extract_Cell_Frame_Lens_Or_Label(v)  cast(Flex*, Cell_Node2(v))


INLINE ParamList* Phase_Paramlist(Phase* p) {
    while (Is_Stub_Details(p))
        p = u_cast(Phase*, Cell_Node1(Phase_Archetype(p)));
    return u_cast(ParamList*, p);
}

// More optimized version of Keylist_Of_Varlist(Phase_Paramlist(a)),
// and also forward declared.
//
#define Phase_Keylist(p) \
    BONUS(KeyList, Phase_Paramlist(p))

#define Phase_Keys_Head(p) \
    Flex_Head(const Key, Phase_Keylist(p))

#define Phase_Keys(tail,p) \
    Varlist_Keys((tail), Phase_Paramlist(p))

INLINE Param* Phase_Params_Head(Phase* p) {
    ParamList* list = Phase_Paramlist(p);
    return Flex_Head_Dynamic(Param, list) + 1;  // skip archetype
}

#define Details_Dispatcher(a) \
    ensure(Details*, (a))->link.dispatcher

#define Tweak_Details_Dispatcher(p,cfunc) \
    (ensure(Details*, (p))->link.dispatcher = (cfunc))


// The Array is the details identity itself.
//
INLINE Array* Details_Array(Details* details) {
    assert(Is_Stub_Details(details));
    return x_cast(Array*, details);
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
INLINE Value* Details_At(Details* details, Length n) {
    Array* a = Details_Array(details);
    assert(n != 0 and n < Array_Len(a));
    return Flex_Head_Dynamic(Value, a) + n;
}

#define Details_Max(details) \
    Array_Len(Details_Array(details))

#define IDX_DETAILS_1 1  // Common index used for code body location

// These are indices into the details array used by actions which have
// the DETAILS_RAW_NATIVE set.
//
enum {
    IDX_RAW_NATIVE_RETURN = 1,  // return type for natives
    IDX_RAW_NATIVE_MAX
};

enum {
    IDX_COMBINATOR_BODY = 1,  // either native or BLOCK!
    IDX_COMBINATOR_MAX
};

// If you use Make_Interpreted_Action_May_Fail() then it will automatically
// put the relativized body into slot 1 of the details.  Referring to this
// IDX value in the IDX enums of things like LAMBDA and FUNC helps to make
// it clearer that the index position is reserved.
//
#define IDX_INTERPRETED_BODY  1

enum {
    IDX_TYPECHECKER_DECIDER_INDEX = 1,  // datatype or type constraint to check
    IDX_TYPECHECKER_MAX
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


//=//// META OBJECT ///////////////////////////////////////////////////////=//
//
// ACTION! details and ANY-CONTEXT? varlists can store a "meta" object.  It's
// where information for HELP is saved, and it's how modules store out-of-band
// information that doesn't appear in their body.

INLINE Option(VarList*) Misc_Details_Adjunct(Details* details) {
    return cast(VarList*, m_cast(Node*, Details_Array(details)->misc.node));
}

INLINE void Tweak_Misc_Details_Adjunct(
    Stub* details,  // may not be formed yet
    Option(VarList*) adjunct
){
    assert(Is_Stub_Details(details));
    details->misc.node = maybe adjunct;
}


//=//// ANCESTRY / FRAME COMPATIBILITY ////////////////////////////////////=//
//
// Keylist.link.node points at a keylist which has the same number of keys or
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

INLINE KeyList* Link_Keylist_Ancestor(KeyList* keylist) {
    KeyList* ancestor = cast(KeyList*, m_cast(Node*, keylist->link.node));
    assert(Is_Stub_Keylist(ancestor));
    possibly(ancestor == keylist);
    return ancestor;
}

INLINE void Tweak_Link_Keylist_Ancestor(KeyList* keylist, KeyList* ancestor) {
    possibly(keylist == ancestor);  // keylists terminate on self
    keylist->link.node = ancestor;
}

INLINE bool Action_Is_Base_Of(Phase* base, Phase* derived) {
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
        QUOTE_BYTE(param) == ONEQUOTE_NONQUASI_3
        and HEART_BYTE(param) == REB_PARAMETER
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
    QUOTE_BYTE(out) = NOQUOTE_1;
}
