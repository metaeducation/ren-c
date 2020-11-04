//
//  File: %sys-action.h
//  Summary: {action! defs AFTER %tmp-internals.h (see: %sys-rebact.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// Using a technique strongly parallel to contexts, an action is identified
// by an array which acts as its "paramlist".  The 0th element of that array
// is an archetypal value of the ACTION!.  That is followed by 1..NUM_PARAMS
// cells that have REB_XXX types higher than REB_MAX (e.g. "pseudotypes").
// These PARAM cells are not intended to be leaked to the user...they
// indicate the parameter type (normal, quoted, local).  The parameter cell's
// payload holds a typeset, and the extra holds the symbol.
//
// Each ACTION! instance cell (including the one that can be found in the [0]
// slot of the parameter list) has also a "details" field.  This is another
// array that holds the instance data used by the C native "dispatcher"
// function, which lives in MISC(details).dispatcher).  The details are how
// the same dispatcher can have different effects:
//
// What the details array holds varies by dispatcher:
//
//     USER FUNCTIONS: 1-element array w/a BLOCK!, the body of the function
//     GENERICS: 1-element array w/WORD! "verb" (OPEN, APPEND, etc)
//     SPECIALIZATIONS: 1-element array containing an exemplar FRAME! value
//     ROUTINES/CALLBACKS: stylized array (REBRIN*)
//     TYPECHECKERS: the TYPESET! to check against
//
// Since plain natives only need the C function, the body is optionally used
// to store a block of Rebol code that is equivalent to the native, for
// illustrative purposes.  (a "fake" answer for SOURCE)
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
// * The `misc.meta` field of the paramlist holds a meta object (if any) that
//   describes the function.  This is read by help.
//
// * By storing the C function dispatcher pointer in the `details` array node
//   instead of in the value cell itself, it also means the dispatcher can be
//   HIJACKed--or otherwise hooked to affect all instances of a function.
//


// An underlying function is one whose frame is compatible with a
// derived function (e.g. the underlying function of a specialization or
// an adaptation).
//
#define LINK_UNDERLYING_NODE(s)  LINK(s).custom.node
#define LINK_UNDERLYING(s)       ACT(LINK_UNDERLYING_NODE(s))


// ACTION! paramlists and ANY-CONTEXT! varlists can store a "meta"
// object.  It's where information for HELP is saved, and it's how modules
// store out-of-band information that doesn't appear in their body.
//
#define MISC_META_NODE(s)       SER(s)->misc_private.custom.node
#define MISC_META(s)            CTX(MISC_META_NODE(s))


// REBACT uses this.  It can hold either the varlist of a frame containing
// specialized values (e.g. an "exemplar"), with ARRAY_FLAG_IS_VARLIST set.
// Or just hold the paramlist.  This speeds up Push_Action() because
// if this were `REBCTX *exemplar;` then it would have to test it for null
// explicitly to default f->special to f->param.
//
#define LINK_SPECIALTY_NODE(s)   LINK(s).custom.node
#define LINK_SPECIALTY(s)        ARR(LINK_SPECIALTY_NODE(s))


//=//// PARAMLIST_FLAG_HAS_RETURN /////////////////////////////////////////=//
//
// Has a definitional RETURN in the first paramlist slot.
//
#define PARAMLIST_FLAG_HAS_RETURN \
    ARRAY_FLAG_23


//=//// PARAMLIST_FLAG_POSTPONES_ENTIRELY /////////////////////////////////=//
//
// A postponing operator causes everything on its left to run before it will.
// Like a deferring operator, it is only allowed to appear after the last
// parameter of an expression except it closes out *all* the parameters on
// the stack vs. just one.
//
#define PARAMLIST_FLAG_POSTPONES_ENTIRELY \
    ARRAY_FLAG_24


//=//// PARAMLIST_FLAG_IS_BARRIER /////////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by |
//
// The "expression barrier" was once a built-in type (BAR!) in order to get
// a property not possible to achieve with functions...that it would error
// if it was used during FULFILL_ARG and would be transparent in evaluation.
//
// Transparency was eventually generalized as "invisibility".  But attempts
// to intuit the barrier-ness from another property (e.g. "enfix but no args")
// were confusing.  It seems an orthogonal feature in its own right, so it
// was added to the TWEAK list pending a notation in function specs.
//
#define PARAMLIST_FLAG_IS_BARRIER \
    ARRAY_FLAG_25

STATIC_ASSERT(PARAMLIST_FLAG_IS_BARRIER == EVAL_FLAG_FULFILLING_ARG);


//=//// PARAMLIST_FLAG_DEFERS_LOOKBACK ////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by THEN, ELSE, and ALSO.
//
// Tells you whether a function defers its first real argument when used as a
// lookback.  Because lookback dispatches cannot use refinements, the answer
// is always the same for invocation via a plain word.
//
#define PARAMLIST_FLAG_DEFERS_LOOKBACK \
    ARRAY_FLAG_26


//=//// PARAMLIST_FLAG_QUOTES_FIRST ///////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define PARAMLIST_FLAG_QUOTES_FIRST \
    ARRAY_FLAG_27


//=//// PARAMLIST_FLAG_SKIPPABLE_FIRST ////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// It is good for the evaluator to have a fast test for knowing if the first
// argument to a function is willing to be skipped, as this comes into play
// in quote resolution.  (It's why `x: default [10]` can have default looking
// for SET-WORD! and SET-PATH! to its left, but `case [... default [x]]` can
// work too when it doesn't see a SET-WORD! or SET-PATH! to the left.)
//
#define PARAMLIST_FLAG_SKIPPABLE_FIRST \
    ARRAY_FLAG_28


//=//// PARAMLIST_FLAG_IS_NATIVE //////////////////////////////////////////=//
//
// Native functions are flagged that their dispatcher represents a native in
// order to say that their ACT_DETAILS() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebValue() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
// Note: This is tactially set to be the same as SERIES_INFO_HOLD to make it
// possible to branchlessly mask in the bit to stop frames from being mutable
// by user code once native code starts running.
//
#define PARAMLIST_FLAG_IS_NATIVE \
    ARRAY_FLAG_29

STATIC_ASSERT(PARAMLIST_FLAG_IS_NATIVE == SERIES_INFO_HOLD);


//=//// PARAMLIST_FLAG_ENFIXED ////////////////////////////////////////////=//
//
// An enfix function gets its first argument from its left.  For a time, this
// was the property of a binding and not an ACTION! itself.  This was an
// attempt at simplification which caused more problems than it solved.
//
#define PARAMLIST_FLAG_ENFIXED \
    ARRAY_FLAG_30


//=//// PARAMLIST_FLAG_31 /////////////////////////////////////////////////=//
//
#define PARAMLIST_FLAG_31 \
    ARRAY_FLAG_31


// These are the flags which are scanned for and set during Make_Action
//
#define PARAMLIST_MASK_CACHED \
    (PARAMLIST_FLAG_QUOTES_FIRST | PARAMLIST_FLAG_SKIPPABLE_FIRST)

// These flags should be copied when specializing or adapting.  They may not
// be derivable from the paramlist (e.g. a native with no RETURN does not
// track if it requotes beyond the paramlist).
//
#define PARAMLIST_MASK_INHERIT \
    (PARAMLIST_FLAG_DEFERS_LOOKBACK | PARAMLIST_FLAG_POSTPONES_ENTIRELY)


#define SET_ACTION_FLAG(s,name) \
    (cast(REBSER*, ACT(s))->header.bits |= PARAMLIST_FLAG_##name)

#define GET_ACTION_FLAG(s,name) \
    ((cast(REBSER*, ACT(s))->header.bits & PARAMLIST_FLAG_##name) != 0)

#define CLEAR_ACTION_FLAG(s,name) \
    (cast(REBSER*, ACT(s))->header.bits &= ~PARAMLIST_FLAG_##name)

#define NOT_ACTION_FLAG(s,name) \
    ((cast(REBSER*, ACT(s))->header.bits & PARAMLIST_FLAG_##name) == 0)


//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into f->out.
//
// However, pseudotypes can be used to indicate special instructions to the
// evaluator.
//

// This signals that the evaluator is in a "thrown state".
//
#define R_THROWN \
    cast(REBVAL*, &PG_R_Thrown)

// See PARAMLIST_FLAG_INVISIBLE...this is what any function with that flag
// needs to return.
//
// It is also used by path dispatch when it has taken performing a SET-PATH!
// into its own hands, but doesn't want to bother saying to move the value
// into the output slot...instead leaving that to the evaluator (as a
// SET-PATH! should always evaluate to what was just set)
//
#define R_INVISIBLE \
    cast(REBVAL*, &PG_R_Invisible)

// If Eval_Core gets back an REB_R_REDO from a dispatcher, it will re-execute
// the f->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// If EXTRA(Any).flag is not set on the cell, then the types will be checked
// again.  Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define R_REDO_UNCHECKED \
    cast(REBVAL*, &PG_R_Redo_Unchecked)

#define R_REDO_CHECKED \
    cast(REBVAL*, &PG_R_Redo_Checked)


// Path dispatch used to have a return value PE_SET_IF_END which meant that
// the dispatcher itself should realize whether it was doing a path get or
// set, and if it were doing a set then to write the value to set into the
// target cell.  That means it had to keep track of a pointer to a cell vs.
// putting the bits of the cell into the output.  This is now done with a
// special REB_R_REFERENCE type which holds in its payload a RELVAL and a
// specifier, which is enough to be able to do either a read or a write,
// depending on the need.
//
// !!! See notes in %c-path.c of why the R3-Alpha path dispatch is hairier
// than that.  It hasn't been addressed much in Ren-C yet, but needs a more
// generalized design.
//
#define R_REFERENCE \
    cast(REBVAL*, &PG_R_Reference)

// This is used in path dispatch, signifying that a SET-PATH! assignment
// resulted in the updating of an immediate expression in pvs->out, meaning
// it will have to be copied back into whatever reference cell it had been in.
//
#define R_IMMEDIATE \
    cast(REBVAL*, &PG_R_Immediate)

#define R_UNHANDLED \
    cast(REBVAL*, &PG_End_Node)


#define CELL_MASK_ACTION \
    (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)

#define VAL_ACT_PARAMLIST_NODE(v) \
    PAYLOAD(Any, (v)).first.node  // lvalue, but a node

#define VAL_ACTION_DETAILS_OR_LABEL_NODE(v) \
    PAYLOAD(Any, (v)).second.node  // lvalue, but a node


inline static REBARR *ACT_PARAMLIST(REBACT *a) {
    assert(GET_ARRAY_FLAG(&a->paramlist, IS_PARAMLIST));
    return &a->paramlist;
}


// An action's "archetype" is data in the head cell (index [0]) of the array
// that is the paramlist.  This is an ACTION! cell which must have its
// paramlist value match the paramlist it is in.  So when copying one array
// to make a new paramlist from another, you must ensure the new array's
// archetype is updated to match its container.

#define ACT_ARCHETYPE(a) \
    VAL(SER(ACT_PARAMLIST(a))->content.dynamic.data)

inline static void Sync_Paramlist_Archetype(REBARR *paramlist)
  { VAL_ACT_PARAMLIST_NODE(ARR_HEAD(paramlist)) = NOD(paramlist); }


#define ACT_DETAILS(a) \
    ARR(VAL_ACTION_DETAILS_OR_LABEL_NODE(ACT_ARCHETYPE(a)))

#define ACT_DISPATCHER(a) \
    (MISC(VAL_ACTION_DETAILS_OR_LABEL_NODE(ACT_ARCHETYPE(a))).dispatcher)

// These are indices into the details array agreed upon by actions which have
// the PARAMLIST_FLAG_IS_NATIVE set.
//
#define IDX_NATIVE_BODY 0 // text string source code of native (for SOURCE)
#define IDX_NATIVE_CONTEXT 1 // libRebol binds strings here (and lib)
#define IDX_NATIVE_MAX (IDX_NATIVE_CONTEXT + 1)

inline static REBVAL *ACT_PARAM(REBACT *a, REBLEN n) {
    assert(n != 0 and n < ARR_LEN(ACT_PARAMLIST(a)));
    return SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), n);
}

#define ACT_NUM_PARAMS(a) \
    (cast(REBSER*, ACT_PARAMLIST(a))->content.dynamic.used - 1) // dynamic

#define ACT_META(a) \
    MISC_META(a)


// The concept of the "underlying" function is the one which has the actual
// correct paramlist identity to use for binding in adaptations.
//
// e.g. if you adapt an adaptation of a function, the keylist referred to in
// the frame has to be the one for the inner function.  Using the adaptation's
// parameter list would write variables the adapted code wouldn't read.
//
#define ACT_UNDERLYING(a) \
    LINK_UNDERLYING(a)


// An efficiency trick makes functions that do not have exemplars NOT store
// nullptr in the LINK_SPECIALTY(info) node in that case--instead the params.
// This makes Push_Action() slightly faster in assigning f->special.
//
inline static REBCTX *ACT_EXEMPLAR(REBACT *a) {
    REBARR *specialty = LINK_SPECIALTY(ACT_DETAILS(a));
    if (GET_ARRAY_FLAG(specialty, IS_VARLIST))
        return CTX(specialty);

    return nullptr;
}

inline static REBVAL *ACT_SPECIALTY_HEAD(REBACT *a) {
    REBARR *details = ACT_DETAILS(a);
    REBSER *s = SER(LINK_SPECIALTY_NODE(details));
    return cast(REBVAL*, s->content.dynamic.data) + 1; // skip archetype/root
}


// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
#define ACT_PARAMS_HEAD(a) \
    (cast(REBVAL*, SER(ACT_PARAMLIST(a))->content.dynamic.data) + 1)

inline static REBACT *VAL_ACTION(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_ACTION); // so it works on literals
    REBSER *s = SER(VAL_ACT_PARAMLIST_NODE(v));
    if (GET_SERIES_INFO(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ACT(s);
}

#define VAL_ACT_PARAMLIST(v) \
    ACT_PARAMLIST(VAL_ACTION(v))

// When an ACTION! is stored in a cell (e.g. not an "archetype"), it can
// contain a label of the ANY-WORD! it was taken from.  If it is an array
// node, it is presumed an archetype and has no label.
//
// !!! Theoretically, longer forms like `.not.equal?` for PREDICATE! could
// use an array node here.  But since CHAINs store ACTION!s that can cache
// the words, you get the currently executing label instead...which may
// actually make more sense.
//
inline static const REBSTR *VAL_ACTION_LABEL(const RELVAL *v) {
    assert(IS_ACTION(v));
    REBSER *s = SER(VAL_ACTION_DETAILS_OR_LABEL_NODE(v));
    if (IS_SER_ARRAY(s))
        return ANONYMOUS;  // archetype (e.g. may live in paramlist[0] itself)
    return STR(s);
}

inline static void INIT_ACTION_LABEL(RELVAL *v, const REBSTR *label)
{
    // !!! How to be certain this isn't an archetype node?  The GC should
    // catch any violations when a paramlist[0] isn't an array...
    //
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v, __FILE__, __LINE__);
    assert(label != nullptr);  // avoid needing to worry about null case
    VAL_ACTION_DETAILS_OR_LABEL_NODE(v) = NOD(m_cast(REBSTR*, label));
}


// Native values are stored in an array at boot time.  These are convenience
// routines for accessing them, which should compile to be as efficient as
// fetching any global pointer.

#define NATIVE_ACT(name) \
    Natives[N_##name##_ID]

#define NATIVE_VAL(name) \
    ACT_ARCHETYPE(NATIVE_ACT(name))


// A fully constructed action can reconstitute the ACTION! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the action's paramlist.  That action has no binding and
// no label.
//
static inline REBVAL *Init_Action(
    RELVAL *out,
    REBACT *a,
    const REBSTR *label,  // allowed to be ANONYMOUS
    REBNOD *binding  // allowed to be UNBOUND
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    Force_Array_Managed(ACT_PARAMLIST(a));
    Move_Value(out, ACT_ARCHETYPE(a));
    if (label)
        INIT_ACTION_LABEL(out, label);
    else {
        // leave as the array from the archetype (array means not a label)
    }
    assert(VAL_BINDING(out) == UNBOUND);
    INIT_BINDING(out, binding);
    return cast(REBVAL*, out);
}


inline static REB_R Run_Generic_Dispatch(
    const REBVAL *first_arg,  // !!! Is this always same as FRM_ARG(f, 1)?
    REBFRM *f,
    const REBVAL *verb
){
    assert(IS_WORD(verb));

    GENERIC_HOOK *hook = IS_QUOTED(first_arg)
        ? &T_Quoted  // a few things like COPY are supported by QUOTED!
        : Generic_Hook_For_Type_Of(first_arg);

    REB_R r = hook(f, verb);  // Note that QUOTED! has its own hook & handling
    if (r == R_UNHANDLED) {
        //
        // !!! Improve this error message when used with REB_CUSTOM (right now
        // will just say "cannot use verb with CUSTOM!", regardless of if it
        // is an IMAGE! or VECTOR! or GOB!...)
        //
        fail (Error_Cannot_Use_Raw(
            verb,
            Datatype_From_Kind(VAL_TYPE(first_arg))
        ));
    }

    return r;
}


inline static bool Process_Action_Throws(REBFRM *f) {
    Init_Void(f->out, SYM_EMPTY);
    SET_CELL_FLAG(f->out, OUT_MARKED_STALE);
    bool threw = Process_Action_Maybe_Stale_Throws(f);
    CLEAR_CELL_FLAG(f->out, OUT_MARKED_STALE);
    return threw;
}
