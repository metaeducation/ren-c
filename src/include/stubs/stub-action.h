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
// an ANY-CONTEXT?) to find their "keylist".  It is stored in the Stub
// node of the varlist Array* vs. in the Cell of the ANY-CONTEXT? so
// that the keylist can be changed without needing to update all the
// REBVALs for that object.
//
// It may be a simple Flex* -or- in the case of the varlist of a running
// FRAME! on the stack, it points to a Level*.  If it's a FRAME! that
// is not running on the stack, it will be the function paramlist of the
// actual phase that function is for.  Since Level* all start with a
// Cell, this means NODE_FLAG_CELL can be used on the node to
// discern the case where it can be cast to a Level* vs. Array*.
//
// (Note: FRAME!s used to use a field `misc.L` to track the associated
// level...but that prevented the ability to SET-ADJUNCT on a frame.  While
// that feature may not be essential, it seems awkward to not allow it
// since it's allowed for other ANY-CONTEXT?s.  Also, it turns out that
// heap-based FRAME! values--such as those that come from MAKE FRAME!--
// have to get their keylist via the specifically applicable ->phase field
// anyway, and it's a faster test to check this for NODE_FLAG_CELL than to
// separately extract the CTX_TYPE() and treat frames differently.)
//
// It is done as a base-class Node* as opposed to a union in order to
// not run afoul of C's rules, by which you cannot assign one member of
// a union and then read from another.
//
#define BONUS_KeySource_TYPE        Node*
#define HAS_BONUS_KeySource         FLAVOR_VARLIST

INLINE void Tweak_Bonus_Keysource(Flex* varlist, Node* keysource) {
    if (keysource != nullptr) {
        if (Is_Node_A_Stub(keysource))
            assert(Is_Stub_Keylist(cast(Flex*, keysource)));
        else
            assert(Is_Non_Cell_Node_A_Level(keysource));
    }

    BONUS(KeySource, varlist) = keysource;
}

#define Tweak_Varlist_Keysource(varlist,keysource) \
    Tweak_Bonus_Keysource(Varlist_Array(varlist), (keysource))


//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into L->out.
//
// However, pseudotypes can be used to indicate special instructions to the
// evaluator.
//

INLINE Value* Init_Return_Signal_Untracked(Sink(Value) out, char ch) {
    Reset_Cell_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_T_RETURN_SIGNAL) | CELL_MASK_NO_NODES
    );
    BINDING(out) = nullptr;

    PAYLOAD(Any, out).first.u = ch;
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).second.corrupt = CORRUPTZERO;
  #endif
    return out;
}

#define Init_Return_Signal(out,ch) \
    TRACK(Init_Return_Signal_Untracked((out), (ch)))

INLINE char Cell_Return_Type(const Cell* cell) {
    assert(cast(Kind, Cell_Heart(cell)) == REB_T_RETURN_SIGNAL);
    return cast(char, PAYLOAD(Any, cell).first.u);
}

INLINE bool Is_Bounce_An_Atom(Bounce b)
  { return HEART_BYTE(cast(Value*, b)) != REB_T_RETURN_SIGNAL; }

INLINE char VAL_RETURN_SIGNAL(Bounce b) {
    assert(not Is_Bounce_An_Atom(b));
    return PAYLOAD(Any, cast(Value*, b)).first.u;
}

INLINE Atom* Atom_From_Bounce(Bounce b) {
    assert(Is_Bounce_An_Atom(b));
    return cast(Atom*, b);
}


#define Tweak_Cell_Action_Details                 Tweak_Cell_Node1
#define Extract_Cell_Action_Partials_Or_Label(c)  cast(Flex*, Cell_Node2(c))
#define Tweak_Cell_Action_Partials_Or_Label       Tweak_Cell_Node2


INLINE Phase* CTX_FRAME_PHASE(VarList* c);

INLINE Phase* ACT_IDENTITY(Action* action) {
    if (Is_Stub_Details(action))
        return cast(Phase*, action);  // don't want hijacked archetype details
    return CTX_FRAME_PHASE(x_cast(VarList*, action));  // always ACT_IDENTITY()
}


// An action's "archetype" is data in the head cell (index [0]) of the array
// that is the paramlist.  This is an ACTION! cell which must have its
// paramlist value match the paramlist it is in.  So when copying one array
// to make a new paramlist from another, you must ensure the new array's
// archetype is updated to match its container.
//
// Note that the details array represented by the identity is not guaranteed
// to be STUB_FLAG_DYNAMIC, so we use Flex_Data() that handles it.
//
#define ACT_ARCHETYPE(action) \
    cast(Element*, Flex_Data(ACT_IDENTITY(action)))

#define Phase_Archetype(phase) \
    cast(Element*, Flex_Data(ensure(Phase*, phase)))


INLINE bool Is_Frame_Details(const Cell* v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    return Is_Stub_Details(cast(Stub*, Cell_Node1(v)));
}

#define Is_Frame_Exemplar(v) (not Is_Frame_Details(v))


// An action's details array is stored in the archetype, which is the first
// element of the action array.  That's *usually* the same thing as the
// action array itself, -but not always-:
//
// * When you COPY an action, it creates a minimal details array of length 1
//   whose archetype points at the details array of what it copied...not
//   back to itself.  So the dispatcher of the original funciton may run for a
//   phase with this mostly-empty-array, but expect Phase_Details() to give
//   it the original details.
//
// * HIJACK swaps out the archetype in the 0 details slot and puts in the
//   archetype of the hijacker.  (It leaves the rest of the array alone.)
//   When the hijacking function runs, it wants Phase_Details() for the phase
//   to give the details that the hijacking dispatcher wants.
//
// So consequently, all phases have to look in the archetype, in case they
// are running the implementation of a copy or are spliced in as a hijacker.
//
INLINE Details* Phase_Details(Phase* a) {
    assert(Is_Stub_Details(a));
    return x_cast(Details*, Phase_Archetype(a)->payload.Any.first.node);
}


//=//// PARAMLIST, EXEMPLAR, AND PARTIALS /////////////////////////////////=//
//
// Since partial specialization is somewhat rare, it is an optional splice
// before the place where the exemplar is to be found.
//

#define INODE_Exemplar_TYPE     VarList*
#define INODE_Exemplar_CAST     CTX
#define HAS_INODE_Exemplar      FLAVOR_DETAILS


INLINE Option(Array*) ACT_PARTIALS(Action* a) {
    if (Is_Stub_Details(a))
        return x_cast(Array*, Cell_Node2(ACT_ARCHETYPE(a)));
    return nullptr;  // !!! how to preserve partials in exemplars?
}

INLINE VarList* ACT_EXEMPLAR(Action* a) {
    if (Is_Stub_Details(a))
        return INODE(Exemplar, a);
    return x_cast(VarList*, a);
}

// Note: This is a more optimized version of Keylist_Of_Varlist(ACT_EXEMPLAR(a)),
// and also forward declared.
//
#define ACT_KEYLIST(a) \
    cast(KeyList*, node_BONUS(KeySource, ACT_EXEMPLAR(a)))

#define ACT_KEYS_HEAD(a) \
    Flex_Head(const Key, ACT_KEYLIST(a))

#define ACT_KEYS(tail,a) \
    Varlist_Keys((tail), ACT_EXEMPLAR(a))

#define ACT_PARAMLIST(a)            Varlist_Array(ACT_EXEMPLAR(a))

INLINE Param* ACT_PARAMS_HEAD(Action* a) {
    Array* list = Varlist_Array(ACT_EXEMPLAR(a));
    return cast(Param*, list->content.dynamic.data) + 1;  // skip archetype
}

#define LINK_DISPATCHER(a)              cast(Dispatcher*, (a)->link.any.cfunc)
#define mutable_LINK_DISPATCHER(a)      (a)->link.any.cfunc

#define ACT_DISPATCHER(a) \
    LINK_DISPATCHER(ACT_IDENTITY(a))

#define INIT_ACT_DISPATCHER(a,cfunc) \
    mutable_LINK_DISPATCHER(ACT_IDENTITY(a)) = cast(CFunction*, (cfunc))


// The DETAILS array isn't guaranteed to be STUB_FLAG_DYNAMIC (it may hold
// only the archetype, e.g. with a specialized function).  *BUT* if you are
// asking for elements in the details array, you must know it is dynamic.
//
INLINE Value* Details_At(Details* details, Length n) {
    assert(n != 0 and n < Flex_Dynamic_Used(details));
    Cell* at = cast(Cell*, details->content.dynamic.data) + n;
    return cast(Value*, at);
}

#define IDX_DETAILS_1 1  // Common index used for code body location

// These are indices into the details array agreed upon by actions which have
// the PARAMLIST_FLAG_IS_NATIVE set.
//
enum {
    // !!! Originally the body was introduced as a feature to let natives
    // specify "equivalent usermode code".  As the types of natives expanded,
    // it was used for things like storing the text source of C user natives...
    // or the "verb" WORD! of a "generic" (like APPEND).  So ordinary natives
    // just store blank here, and the usages are sometimes dodgy (e.g. a user
    // native checks to see if it's a user native if this is a TEXT!...which
    // might collide with other natives in the future).  The idea needs review.
    //
    IDX_NATIVE_BODY = 1,

    IDX_NATIVE_CONTEXT,  // libRebol binds strings here (and lib)

    IDX_NATIVE_MAX
};

enum {
    IDX_INTRINSIC_CFUNC = 1,
    IDX_INTRINSIC_MAX
};

enum {
    IDX_TYPECHECKER_CFUNC = IDX_INTRINSIC_CFUNC,  // uses Intrinsic_Dispatcher()
    IDX_TYPECHECKER_DECIDER_INDEX,  // datatype or typeset to check
    IDX_TYPECHECKER_MAX
};


INLINE const Symbol* Key_Symbol(const Key* key)
  { return *key; }


INLINE void Init_Key(Key* dest, const Symbol* symbol)
  { *dest = symbol; }

#define KEY_SYM(key) \
    Symbol_Id(Key_Symbol(key))

#define ACT_KEY(a,n)            Varlist_Key(ACT_EXEMPLAR(a), (n))
#define ACT_PARAM(a,n)          cast_PAR(Varlist_Slot(ACT_EXEMPLAR(a), (n)))

#define ACT_NUM_PARAMS(a) \
    Varlist_Len(ACT_EXEMPLAR(a))


//=//// META OBJECT ///////////////////////////////////////////////////////=//
//
// ACTION! details and ANY-CONTEXT? varlists can store a "meta" object.  It's
// where information for HELP is saved, and it's how modules store out-of-band
// information that doesn't appear in their body.

#define mutable_ACT_ADJUNCT(a)     MISC(DetailsAdjunct, ACT_IDENTITY(a))
#define ACT_ADJUNCT(a)             MISC(DetailsAdjunct, ACT_IDENTITY(a))


//=//// ANCESTRY / FRAME COMPATIBILITY ////////////////////////////////////=//
//
// On the keylist of an object, LINK_ANCESTOR points at a keylist which has
// the same number of keys or fewer, which represents an object which this
// object is derived from.  Note that when new object instances are
// created which do not require expanding the object, their keylist will
// be the same as the object they are derived from.
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

#define LINK_Ancestor_TYPE              KeyList*
#define HAS_LINK_Ancestor               FLAVOR_KEYLIST

INLINE bool Action_Is_Base_Of(Action* base, Action* derived) {
    if (derived == base)
        return true;  // fast common case (review how common)

    if (ACT_IDENTITY(derived) == ACT_IDENTITY(base))
        return true;  // Covers COPY + HIJACK cases (seemingly)

    KeyList* keylist_test = ACT_KEYLIST(derived);
    KeyList* keylist_base = ACT_KEYLIST(base);
    while (true) {
        if (keylist_test == keylist_base)
            return true;

        KeyList* ancestor = LINK(Ancestor, keylist_test);
        if (ancestor == keylist_test)
            return false;  // signals end of the chain, no match found

        keylist_test = ancestor;
    }
}


//=//// RETURN HANDLING (WIP) /////////////////////////////////////////////=//
//
// The well-understood and working part of definitional return handling is
// that function frames have a local slot named RETURN.  This slot is filled
// by the dispatcher before running the body, with a function bound to the
// executing frame.  This way it knows where to return to.
//
// !!! Lots of other things are not worked out (yet):
//
// * How do function derivations share this local cell (or do they at all?)
//   e.g. if an ADAPT has prelude code, that code runs before the original
//   dispatcher would fill in the RETURN.  Does the cell hold a return whose
//   phase meaning changes based on which phase is running (which the user
//   could not do themselves)?  Or does ADAPT need its own RETURN?  Or do
//   ADAPTs just not have returns?
//
// * The typeset in the RETURN local key is where legal return types are
//   stored (in lieu of where a parameter would store legal argument types).
//   Derivations may wish to change this.  Needing to generate a whole new
//   paramlist just to change the return type seems excessive.
//
// * To make the position of RETURN consistent and easy to find, it is moved
//   to the first parameter slot of the paramlist (regardless of where it
//   is declared).  This complicates the paramlist building code, and being
//   at that position means it often needs to be skipped over (e.g. by a
//   GENERIC which wants to dispatch on the type of the first actual argument)
//   The ability to create functions that don't have a return complicates
//   this mechanic as well.
//
// The only bright idea in practice right now is that parameter lists which
// have a definitional return in the first slot have a flag saying so.  Much
// more design work on this is needed.
//

#define ACT_HAS_RETURN(a) \
    Get_Subclass_Flag(VARLIST, ACT_PARAMLIST(a), PARAMLIST_HAS_RETURN)


// The exemplar alone is sufficient information for the specialization frame.
// Hence a compact "singular" array of 1 cell can be used for the details.
//
enum {
    IDX_SPECIALIZER_MAX = 1  // has just Phase_Details[0], the ACT_ARCHETYPE()
};
