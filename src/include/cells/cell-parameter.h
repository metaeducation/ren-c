//
//  file: %cell-parameter.h
//  summary: "Definitions for PARAMETER! Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2026 Ren-C Open Source Contributors
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
// When a function is built from a spec block, each argument (or return) gets
// a PARAMETER! in a block called a "paramlist".  Each parameter! contains an
// array of the spec that was supplied to the parameter, and it encodes
// the parameter's class and other flags, as determined from the argument.
//
// So for example, for the paramlist generated from the following spec:
//
//     foo: func [
//         return: [integer!]  ; specialized to PARAMETER! (trick!)
//         arg [<opt> block!]  ; PARAMCLASS_NORMAL
//         'qarg [word!]       ; PARAMCLASS_QUOTED
//         earg [<hole> time!] ; PARAMCLASS_NORMAL + PARAMETER_FLAG_HOLE_OK
//         :refine [tag!]      ; PARAMCLASS_NORMAL + PARAMETER_FLAG_REFINEMENT
//         {loc}               ; not PARAMETER!, specialized to null antiform
//     ][
//        ...
//     ]
//
// Hence the parameter is a compressed digest of information gleaned from
// the properties of the named argument and its typechecking block.  The
// content of the typehecking block is also copied into an immutable array
// and stored in the parameter.  (Refinements with no arguments store
// a nullptr for the array.)
//
// The list of PARAMETER! cells in a function's parameter list are used for
// internal processing of function calls, and not exposed to the user.  It
// is seeming increasingly likely that the best way to give users control
// over building and inspecting functions will be to expose PARAMETER! as
// a kind of compressed object type (similar to R3-Alpha EVENT!).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. Parameters do not store the symbol for the parameter.  Those symbols
//    are in a separate series called a keylist.  The separation is due to
//    wanting to make common code paths for FRAME! and OBJECT!, where an
//    object only uses a compressed keylist with no PARAMETER! cells.
//
//    (R3-Alpha used a full WORD!-sized cell to describe each field of an
//    object, but Ren-C only uses a single pointer-to-symbol.)
//
// 2. We don't want to have a ton of parameter classes exposed to the user;
//    the fewer the better.  So features like PARAMCLASS_RETURN were pared
//    back, instead considering the fact that something like FUNC fills a
//    frame slot to RETURN to be an implementation detail of FUNC itself,
//    and the slot is a generic "local" holding a "specialized" PARAMETER!.
//    This reduces the number of bits needed to encode the class, leaving
//    more room for PARAMETER_FLAG_XXX.
//

/* DONT(#define CELL_PARAMETER_SYMBOL() ...) */  // [1]

#define CELL_PARAMETER_PAYLOAD_1_SPEC(c)  CELL_PAYLOAD_1(c)
#define CELL_PARAMETER_EXTRA_STRAND(c)  CELL_EXTRA(c)

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define CELL_PARAMETER_PAYLOAD_2_FLAGS(c)  (c)->payload.split.two.flags
#else
    INLINE uintptr_t& CELL_PARAMETER_PAYLOAD_2_FLAGS(Cell* v) {
        assert(Unchecked_Heart_Of(v) == TYPE_PARAMETER);
        return v->payload.split.two.flags;
    }

    INLINE const uintptr_t& CELL_PARAMETER_PAYLOAD_2_FLAGS(const Cell* v) {
        assert(Unchecked_Heart_Of(v) == TYPE_PARAMETER);
        return v->payload.split.two.flags;
    }
#endif

#define PARAMCLASS_BYTE(c)  FIRST_BYTE(&CELL_PARAMETER_PAYLOAD_2_FLAGS(c))
#define FLAG_PARAMCLASS_BYTE(b)  FLAG_FIRST_BYTE(b)

INLINE Option(const Source*) Parameter_Spec(const Cell* v) {
    assert(Heart_Of(v) == TYPE_PARAMETER);

    const Base* base = CELL_PARAMETER_PAYLOAD_1_SPEC(v);
    if (base != nullptr and Not_Base_Readable(base))
        panic (Error_Series_Data_Freed_Raw());

    assert(Get_Cell_Flag(v, DONT_MARK_PAYLOAD_1) == (base == nullptr));

    return cast(Source*, base);
}


//=//// PARAMETER_FLAG_REFINEMENT /////////////////////////////////////////=//
//
// Indicates that the parameter is optional, and if needed specified in the
// path that is used to call a function.
//
// The interpretation of a null Parameter_Spec() for a refinement is that
// it does not take an argument at a callsite--not that it takes ANY-STABLE!
//
#define PARAMETER_FLAG_REFINEMENT \
    FLAG_LEFT_BIT(8)


//=//// PARAMETER_FLAG_HOLE_OK ////////////////////////////////////////////=//
//
// If a parameter is marked with the <hole> annotation, then you don't have
// to provide a parameter for that slot in the frame.  Building a FRAME!
// manually you can put holes anywhere--but if you are calling a function via
// the evaluator you will get holes only when the evaluator reaches the end
// of input or a comma, or if an infix function is missing its left argument.
//
// (e.g. `eval [+ 5]` or an ordinary argument hit the end, like the trick
// used for `>> help` when the arity is 1 usually as `>> help foo`)
//
// When a hole is *NOT* okay, error parity is kept with GROUP!s, such that the
// same error is given for `eval [1 +,]` and `(1 +,)`.
//
// Holes are out of band of what evaluation can produce.  Hence they can't be
// a product of Stepper_Executor(), but are detected by the feeding mechanism
// before the stepper is called.
//
#define PARAMETER_FLAG_HOLE_OK \
    FLAG_LEFT_BIT(9)


//=//// PARAMETER_FLAG_CONST //////////////////////////////////////////////=//
//
// A parameter that has been marked <const> will give a value that behaves
// as an immutable view of the data it references--regardless of the
// underlying protection status of that data.  An important application of
// this idea is that loops take their bodies as [<const> block!] to prevent
// misunderstandings like:
//
//     repeat 2 [data: [], append data <a>, assert [data = [<a>]]
//
// While the [] assigned to data isn't intrinsically immutable, the const
// status propagated onto the `body` argument means REPEAT's view of the
// body block's content is const, so it won't allow the APPEND.
//
// See CELL_FLAG_CONST for more information.
//
#define PARAMETER_FLAG_CONST \
    FLAG_LEFT_BIT(10)


//=//// PARAMETER_FLAG_VARIADIC ///////////////////////////////////////////=//
//
// Indicates that when this parameter is fulfilled, it will do so with a
// value of type VARARGS!, that actually just holds a pointer to the level
// state and allows more arguments to be gathered at the callsite *while the
// function body is running*.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define PARAMETER_FLAG_VARIADIC \
    FLAG_LEFT_BIT(11)


//=//// PARAMETER_FLAG_FINAL_TYPECHECK ////////////////////////////////////=//
//
// When a Param in a ParamList is unspecialized (e.g. a PARAMETER! value)
// then if it does not carry this flag, then that means typechecking against
// it is not the last word.  There is a type underlying it which also needs
// to be checked.  Consider:
//
//     >> ap-int: copy unrun append/
//
//     >> ap-int.value: make parameter! [integer!]  ; or whatever syntax
//     == &[parameter! [integer!]]
//
//     >> /ap-int: anti ap-int
//     == ~&[frame! ...]~  ; anti
//
// You've just created a version of APPEND with a tighter type constraint.
// But what if that type were -looser-?  You must check this type, and also
// the type "underneath" it.
//
// So parameters don't get this bit by default, just when they are initially
// created.
//
#define PARAMETER_FLAG_FINAL_TYPECHECK \
    FLAG_LEFT_BIT(12)


//=//// PARAMETER_FLAG_WANT_VETO //////////////////////////////////////////=//
//
// If a parameter is marked with the `<veto>` annotation, then that means it
// wants to receive the VETO hot potato as an argument, as opposed to making
// the function return NULL after all parameters are gathered and typechecked.
//
// (At one time, you had to explicitly mark parameters as being VETOABLE to
// get vetoing behavior, due to concern about the semantics of NULL being
// confusing if logic was an expected result.  e.g. EVEN? being vetoed might
// seem to imply oddness, vs. veto'd ness.  But having to mark parameters as
// "opting-in" to vetoability was very noisy, and it's better to use <veto>
// to signal you're "opting out" of the automatic handling and want the veto
// hot potato.)
//
#define PARAMETER_FLAG_WANT_VETO \
    FLAG_LEFT_BIT(13)


//=//// PARAMETER_FLAG_TRASH_DEFINITELY_OK ////////////////////////////////=//
//
// See notes on NULL_DEFINITELY_OK
//
#define PARAMETER_FLAG_TRASH_DEFINITELY_OK \
    FLAG_LEFT_BIT(14)


//=//// PARAMETER_FLAG_VOID_DEFINITELY_OK /////////////////////////////////=//
//
// See notes on NULL_DEFINITELY_OK
//
#define PARAMETER_FLAG_VOID_DEFINITELY_OK \
    FLAG_LEFT_BIT(15)


//=//// PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION ////////////////////////////=//
//
// To try and speed up parameter typechecking to not need to do word fetches
// on common cases, an array of bytes is built compacting types and typesets.
// But this array is a finite length (4 bytes on 32-bit, 8 on 64-bit) and so

#define PARAMETER_FLAG_INCOMPLETE_OPTIMIZATION \
    FLAG_LEFT_BIT(16)


//=//// PARAMETER_FLAG_NULL_DEFINITELY_OK /////////////////////////////////=//
//
// The NULL? type checking function adds overhead, even if called via an
// intrinsic optimization.  Yet it's common--especially unused refinements,
// so just fold it into a flag.
//
// This flag not being set doesn't mean nulls aren't ok (some non-optimized
// typechecker might accept nulls).
//
#define PARAMETER_FLAG_NULL_DEFINITELY_OK \
    FLAG_LEFT_BIT(17)


//=//// PARAMETER_FLAG_ANY_STABLE_OK //////////////////////////////////////=//
//
// The check for ANY-STABLE? (e.g. any element or stable antiform) is very
// common, and has an optimized flag if the ANY-STABLE? function is detected
// in the parameter spec.
//
#define PARAMETER_FLAG_ANY_STABLE_OK \
    FLAG_LEFT_BIT(18)


//=//// PARAMETER_FLAG_ANY_ATOM_OK ////////////////////////////////////////=//
//
// The ANY-VALUE? check takes its argument as a meta parameter, so it doesn't
// fit the TypesetByte optimization.  It's likely that TypesetByte should
// be rethought so that things like SPLICE? can be accelerated typesets.
//
#define PARAMETER_FLAG_ANY_ATOM_OK \
    FLAG_LEFT_BIT(19)


//=//// PARAMETER_FLAG_UNDO_OPT ///////////////////////////////////////////=//
//
// This is set by the <opt> parameter flag.  It helps avoid the need to
// make a function take ^META parameters just in order to test if something is
// a void, so long as there's no need to distinguish it from null.
//
#define PARAMETER_FLAG_UNDO_OPT \
    FLAG_LEFT_BIT(20)


//=//// PARAMETER_FLAG_SPACE_DEFINITELY_OK ////////////////////////////////=//
//
// We allow type specs to contain just [_] to signal space is ok.  It would
// probably be better as an optimization case for the 1-255 range of test
// (as many of these flags would be, if they're not multiplexed across all
// possible optimizations)
//
#define PARAMETER_FLAG_SPACE_DEFINITELY_OK \
    FLAG_LEFT_BIT(21)


//=//// PARAMETER_FLAG_22 /////////////////////////////////////////////////=//
//
#define PARAMETER_FLAG_22 \
    FLAG_LEFT_BIT(22)


//=//// PARAMETER_FLAG_AUTO_TRASH /////////////////////////////////////////=//
//
// There's a lighter notation than `return: [trash!]` to say that you return
// TRASH!, and that's [return ~].  (At one time this was [return: []] but that
// makes too much sense for meaning a function that can't return any type at
// all, e.g. a divergent function.)
//
// As far as clients of the function are concerned, `return: ~` will show them
// `return: [trash!]` if they ask for RETURN OF.  However, the mechanics of
// functions that were declared with the ~ syntax is that they automatically
// put the name of the function into any TRASH! they return--to help with
// debugging.  This flag guides that behavior in the various dispatchers.
//
// (Since natives are their own dispatchers, the only way to get them to run
// common code is through macros, so `return TRASH_OUT;` does the embedding of
// the name through code in the TRASH_OUT macro.)
//
#define PARAMETER_FLAG_AUTO_TRASH \
    FLAG_LEFT_BIT(23)


//=//// PARAMETER_FLAG_UNBIND_ARG /////////////////////////////////////////=//
//
// If you put a quote mark on a parameter in the function spec, it means you
// want the argument to have any bindings removed (if applicable).  This is
// a good choice if you can make it...because not propagating bindings reduces
// the GC burden of "leaky bindings".
//
#define PARAMETER_FLAG_UNBIND_ARG \
    FLAG_LEFT_BIT(23)


//=//// PARAMETER_FLAG_24 /////////////////////////////////////////////////=//

#define PARAMETER_FLAG_24 \
    FLAG_LEFT_BIT(24)



#define Get_Parameter_Flag(v,name) \
    ((CELL_PARAMETER_PAYLOAD_2_FLAGS(v) & PARAMETER_FLAG_##name) != 0)

#define Not_Parameter_Flag(v,name) \
    ((CELL_PARAMETER_PAYLOAD_2_FLAGS(v) & PARAMETER_FLAG_##name) == 0)

#define Set_Parameter_Flag(v,name) \
    (CELL_PARAMETER_PAYLOAD_2_FLAGS(v) |= PARAMETER_FLAG_##name)

#define Clear_Param_Flag(v,name) \
    (CELL_PARAMETER_PAYLOAD_2_FLAGS(v) &= ~PARAMETER_FLAG_##name)



INLINE ParamClass Parameter_Class(const Cell* v) {
    assert(Heart_Of(v) == TYPE_PARAMETER);
    return i_cast(ParamClass, PARAMCLASS_BYTE(v));
}

INLINE Option(const Strand*) Parameter_Strand(const Cell* v) {
    assert(Heart_Of(v) == TYPE_PARAMETER);
    return cast(const Strand*, CELL_PARAMETER_EXTRA_STRAND(v));
}

INLINE void Set_Parameter_Strand(Cell* v, Option(const Strand*) s) {
    assert(Heart_Of(v) == TYPE_PARAMETER);
    CELL_PARAMETER_EXTRA_STRAND(v) = m_cast(Strand*, opt s);
}


//=//// CELL_FLAG_PARAM_NOTE_TYPECHECKED //////////////////////////////////=//
//
// This flag is set on specialized parameter values or arguments to say that
// they have been typechecked.
//
// 1. We use CELL_FLAG_NOTE because it is not copied by default.  Typecheck
//    status only matters on arguments that are sitting in FRAME! cells...
//    and we don't want to leak this bit other places that might have other
//    uses for that bit.
//
// 2. We want any changes to a Cell to wipe out the typechecked status.  This
//    way if you have an argument that hasn't changed, and you re-run a
//    function phase--you know you don't have to check it again.
//
//      >> bad-negate: adapt negate/ [value: to text! value]
//
//      >> bad-negate 1020
//      ** Error: Internal phase disallows TEXT! for its `value` argument
//
//    If you hadn't overwritten `value`, then it still has CELL_FLAG_NOTE from
//    the first typecheck, and won't run typechecking again:
//
//      good-negate: adapt negate/ [print "not modifying value, no check"]
//

#define CELL_FLAG_PARAM_NOTE_TYPECHECKED  CELL_FLAG_NOTE

STATIC_ASSERT(
    not (CELL_MASK_COPY & CELL_FLAG_PARAM_NOTE_TYPECHECKED)  // [1]
);
STATIC_ASSERT(
    (CELL_MASK_PERSIST & CELL_FLAG_PARAM_NOTE_TYPECHECKED) == 0  // [2]
);

INLINE bool Is_Typechecked(const Param* p) {
    return Get_Cell_Flag(p, PARAM_NOTE_TYPECHECKED);
}

INLINE void Mark_Typechecked(Param* p) {
    Set_Cell_Flag(p, PARAM_NOTE_TYPECHECKED);
}

INLINE bool Is_Parameter_Final_Type(const Param* p) {
    assert(Heart_Of(p) == TYPE_PARAMETER);
    return Get_Parameter_Flag(p, FINAL_TYPECHECK);
}


//=//// CELL_FLAG_PARAM_NOT_CHECKED_OR_COERCED ////////////////////////////=//
//
// Functions may want to display what types they accept in the interface for
// HELP, but not actually have the system pay the cost of typechecking them.
// A quote mark on the typespec accomplishes this goal in a subtle way (it
// may even be *too* subtle...but, the tradeoff seems worth it for the
// readability and efficiency.)
//
// If the implementation is native code, it needs to make sure that native is
// doing the typecheck work internally with whatever switch() statements it
// is doing, so as not to crash on invalid bit patterns.  Non-native code can
// be more trusting that things won't crash, but should still be careful if
// using this feature.
//
// This is made a CELL_FLAG_XXX rather than a PARAMETER_FLAG_XXX because when
// you extract a parameter from a function's paramlist into a variable to
// use for typechecking outside the native implementation, you want the flag
// be able to be flipped.  It is not necessarily guaranteed that parameter
// flags themselves live in the Cell (implementation detail may change) but
// CELL_FLAG_XXX are always in the Cell.
//

#define CELL_FLAG_PARAM_NOT_CHECKED_OR_COERCED \
    CELL_FLAG_TYPE_SPECIFIC_A

INLINE bool Not_Parameter_Checked_Or_Coerced(const Cell* cell) {
    assert(Heart_Of(cell) == TYPE_PARAMETER);
    return Get_Cell_Flag(cell, PARAM_NOT_CHECKED_OR_COERCED);
}


// A PARAMETER! that has not been typechecked represents an unspecialized
// parameter.  When the slot they are in is overwritten by another value, that
// indicates they are then fixed at a value and hence specialized--so not part
// of the public interface of the function.
//
// 1. CELL_FLAG_NOTE could be used for another meaning on PARAMETER! holes,
//    since typechecking them is not meaningful.  But that would be somewhat
//    obfuscating, so just assure it's not set for now.
//
// 2. CELL_FLAG_MARKED should also not be set, because you can't "seal" an
//    unspecialized parameter.  It might be harder to reuse that flag for
//    other purposes, as some generic enumerations check that flag and assume
//    things can't be unspecialized parameters as set.
//
INLINE bool Is_Specialized(const Param* p) {
    if (Is_Cell_A_Bedrock_Hole(p)) {
        assert(Not_Cell_Flag(p, PARAM_NOTE_TYPECHECKED));  // [1]
        assert(Not_Cell_Flag(p, PARAM_MARKED_SEALED));  // [2]
        return false;
    }
    return true;
}

#define Not_Specialized(v)      (not Is_Specialized(v))

INLINE const Slot* Known_Unspecialized(const Param* p) {
    assert(Not_Specialized(p));
    return u_cast(Slot*, p);
}


//=//// PARAMETER SHIELDING ///////////////////////////////////////////////=//
//
// Parameter lists we shouldn't write to are marked immutable, but we also
// shield the cells to prevent accidents with the internals.
//
// !!! Probably marking all varlists immutable should shield all the cells in
// them, this is something to consider.
//

#define Shield_Param_If_Debug(param) \
    Track_Shield_Cell(known(Param*, (param)));

#define Unshield_Param_If_Debug(param) \
    Track_Unshield_Cell(known(Param*, (param)));

#define Clear_Param_Shield_If_Debug(param) \
    Track_Clear_Cell_Shield(known(Param*, (param)));


//=//// FAST ANTI-WORD "BLITTING" /////////////////////////////////////////=//
//
// ~null~ and ~okay~ antiforms are put into varlist slots during argument
// fulfillment, where those slots have nothing to worry about overwriting.
// We can write them a bit faster.  Small benefit, but...it adds up.
//

#define Blit_Null_Typechecked(out) \
    TRACK(Blit_Word_Untracked( \
        (out), \
        FLAG_LIFT_BYTE(STABLE_ANTIFORM_2) \
            | (not CELL_FLAG_LOGIC_IS_OKAY) \
            | CELL_FLAG_PARAM_NOTE_TYPECHECKED, \
        CANON(NULL) \
    ))

#define Blit_Okay_Typechecked(out) \
    TRACK(Blit_Word_Untracked( \
        (out), \
        FLAG_LIFT_BYTE(STABLE_ANTIFORM_2) \
            | CELL_FLAG_LOGIC_IS_OKAY \
            | CELL_FLAG_PARAM_NOTE_TYPECHECKED, \
        CANON(OKAY) \
    ))


INLINE Element* Init_Unconstrained_Parameter_Untracked(
    Init(Element) out,
    Flags flags
){
    ParamClass pclass = u_cast(ParamClass, FIRST_BYTE(&flags));
    assert(pclass != PARAMCLASS_0);  // must have class
    if (flags & PARAMETER_FLAG_REFINEMENT) {
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
    }
    UNUSED(pclass);

    Reset_Cell_Header_Noquote(
        out,
        BASE_FLAG_BASE | BASE_FLAG_CELL
            | FLAG_HEART(TYPE_PARAMETER)
            | CELL_FLAG_DONT_MARK_PAYLOAD_1  // spec (starting off null here)
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // flags, never marked
    );
    CELL_PARAMETER_PAYLOAD_1_SPEC(out) = nullptr;
    CELL_PARAMETER_PAYLOAD_2_FLAGS(out) = flags;
    CELL_PARAMETER_EXTRA_STRAND(out) = nullptr;

    return out;
}

#define Init_Unconstrained_Parameter(out,param_flags) \
    TRACK(Init_Unconstrained_Parameter_Untracked((out), (param_flags)))


INLINE bool Is_Parameter_Unconstrained(const Cell* v) {
    return Parameter_Spec(v) == nullptr;  // e.g. `[:refine]`
}

INLINE bool Is_Parameter_Divergent(const Cell* v) {
    Option(const Source*) spec = Parameter_Spec(v);
    if (not spec)
        return false;
    return Array_Len(unwrap spec) == 0;  // e.g. `[]`, no legal return types
}

INLINE Param* Unspecialize_Parameter(Cell* p) {
    assert(Heart_Of(p) == TYPE_PARAMETER and LIFT_BYTE(p) == NOQUOTE_3);
    LIFT_BYTE(p) = BEDROCK_0;
    return u_cast(Param*, p);
}

// When it came to literal parameters that could be escaped, R3-Alpha and Red
// consider GROUP!, GET-WORD!, and GET-PATH! to be things that at the callsite
// will be evaluated.
//
// For a time Ren-C tried switching the GROUP! case to use GET-GROUP!, so that
// groups would still be passed literally.  This went along with the idea of
// using a colon on the parameter to indicate the escapability (':param), so it
// was quoted and colon'd.  It was more consistent...but it turned out that
// in practice, few escapable literal sites are interested in literal groups.
// So it was just consistently ugly.
//
// Given that leading colons have nothing to do with getting in the modern
// vision, it was switched around to where GROUP! is the only soft escape.
// (This could be supplemented by '{fence} or '[block] escapable choices, but
// there doesn't seem to be need for that.)
//
// This alias for Is_Group() is just provided to help find callsites that are
// testing for groups for the reason of soft escaping them.  But it also
// makes sure you're only using it on an Element--which is what you should
// have in your hands literally before soft escaping.
//
INLINE bool Is_Soft_Escapable_Group(const Element* e) {
    return Is_Group(e);  // should escape other groups, e.g. ('foo): -> foo:
}


// There are global MACRO-like definitions for things like RETURN that let
// you redefine what any RETURN would do.  You can override that in scopes
// as you wish.  But these macros dispatch to things like RETURN* that are
// instantiated on a per-function basis.
//
// 1. A similar trick is used to get RETURN-OF from RETURN, that adds 1.
//    So for these, we subtract 1, hence the symbol table can line up as:
//
//        return*  ; -1
//        return
//        return-of  ; +1
//
INLINE SymId Starred_Returner_Id(SymId id) {
    SymId starred_id = u_cast(SymId, u_cast(SymId16, id) - 1);  // subtract [1]

  #if RUNTIME_CHECKS
    switch (id) {
      case SYM_CONTINUE: assert(starred_id == SYM_CONTINUE_P); break;
      case SYM_RETURN: assert(starred_id == SYM_RETURN_P); break;
      case SYM_YIELD: assert(starred_id == SYM_YIELD_P); break;
      default: assert(false);
    }
  #endif

    return starred_id;
}


//=//// STORE RETURN PARAMETER! SPEC IN A "LOCAL" /////////////////////////=//
//
// There's a minor compression used by FUNC and YIELDER which stores the type
// information for RETURN as a plain PARAMETER! in the archetypal paramlist
// slot that defines the cell where the DEFINITIONAL-RETURN will be put in
// the instantiation.  (A similar tactic is used for a method's implicit `.`)
//
// Because it is a plain PARAMETER! (and not a BEDROCK_0 PARAMETER!, a.k.a.
// a "HOLE") the function machinery won't try to gather it as an argument at
// the callsite...since it thinks it is "specialized".  The dispatcher for
// the function will overwrite the cell with an actual ACTION! that does the
// returning before the body runs.
//

INLINE void Regularize_Parameter_Local(Param* param) {
    assert(Is_Cell_A_Bedrock_Hole(param));
    LIFT_BYTE(param) = NOQUOTE_3;
}

INLINE const Element* Returnlike_Parameter_In_Paramlist(
    ParamList* paramlist,
    SymId id
){
    Index slot_num = Get_Flavor_Flag(
        VARLIST,
        Varlist_Array(paramlist),
        METHODIZED
    ) ? 2 : 1;
    assert(Key_Id(Varlist_Key(paramlist, slot_num)) == Starred_Returner_Id(id));
    UNUSED(id);
    Stable* param = Stable_Slot_Hack(Varlist_Slot(paramlist, slot_num));
    assert(Is_Parameter(param));
    return As_Element(param);
}

#define Extract_Returnlike_Parameter(out,paramlist,id) \
    Copy_Cell((out), Returnlike_Parameter_In_Paramlist((paramlist), (id)))
