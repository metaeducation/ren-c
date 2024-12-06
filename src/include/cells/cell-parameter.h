//
//  File: %cell-parameter.h
//  Summary: "Definitions for PARAMETER! Cells"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
//     /foo: func [
//         return: [integer!]  ; specialized to plain PARAMETER! (not antiform)
//         arg [~null~ block!]  ; PARAMCLASS_NORMAL
//         'qarg [word!]       ; PARAMCLASS_QUOTED
//         earg [<end> time!]  ; PARAMCLASS_NORMAL + PARAMETER_FLAG_ENDABLE
//         :refine [tag!]      ; PARAMCLASS_NORMAL + PARAMETER_FLAG_REFINEMENT
//         <local> loc         ; not a PARAMETER!, specialized to ~ antiform
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
// * Parameters do not store the symbol for the parameter.  Those symbols
//   are in a separate series called a keylist.  The separation is due to
//   wanting to make common code paths for FRAME! and OBJECT!, where an
//   object only uses a compressed keylist with no PARAMETER! cells.
//
//   (R3-Alpha used a full WORD!-sized cell to describe each field of an
//   object, but Ren-C only uses a single pointer-to-symbol.)
//

INLINE Option(const Source*) Cell_Parameter_Spec(const Cell* v) {
    assert(HEART_BYTE(v) == REB_PARAMETER);
    if (Cell_Node1(v) != nullptr and Not_Node_Readable(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    return cast(Source*, Cell_Node1(v));
}

#define Tweak_Cell_Parameter_Spec(v,a) \
    Tweak_Cell_Node1((v), (a))



#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define PARAMETER_FLAGS(p) \
        *x_cast(uintptr_t*, &EXTRA(p).flags)
#else
    INLINE uintptr_t& PARAMETER_FLAGS(const Cell* p) {
        assert(Cell_Heart_Unchecked(p) == REB_PARAMETER);
        return const_cast<uintptr_t&>(EXTRA(p).flags);
    }
#endif

#define PARAMCLASS_BYTE(p)          FIRST_BYTE(&PARAMETER_FLAGS(p))
#define FLAG_PARAMCLASS_BYTE(b)     FLAG_FIRST_BYTE(b)


//=//// PARAMETER_FLAG_REFINEMENT /////////////////////////////////////////=//
//
// Indicates that the parameter is optional, and if needed specified in the
// path that is used to call a function.
//
// The interpretation of a null Cell_Parameter_Spec() for a refinement is that
// it does not take an argument at a callsite--not that it takes ANY-VALUE!
//
#define PARAMETER_FLAG_REFINEMENT \
    FLAG_LEFT_BIT(8)


//=//// PARAMETER_FLAG_ENDABLE ////////////////////////////////////////////=//
//
// Endability means that a parameter is willing to accept being at the end
// of the input.  This means either an infix dispatch's left argument is
// missing (e.g. `eval [+ 5]`) or an ordinary argument hit the end (e.g. the
// trick used for `>> help` when the arity is 1 usually as `>> help foo`)
//
// ~null~ is used to represent the end state in all parameter types.  In the
// case of quoted arguments, this is unambiguous--as there can be no nulls
// in the input array to quote.  In the meta parameter case it's also not
// ambiguous, as all other meta parameter types are either quoted or quasi.
// With normal parameters it will collide with if the parameter can take
// nulls... but we assume anyone bothered by that would switch to using a
// meta parameter.
//
#define PARAMETER_FLAG_ENDABLE \
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


//=//// PARAMETER_FLAG_12 /////////////////////////////////////////////////=//
//
#define PARAMETER_FLAG_12 \
    FLAG_LEFT_BIT(12)


//=//// PARAMETER_FLAG_NOOP_IF_VOID ///////////////////////////////////////=//
//
// If a parameter is marked with the `<maybe>` annotation, then that means
// if that argument is void in a function invocation, the dispatcher for the
// function won't be run at all--and ~null~ will be returned by the call.
//
// The optimization this represents isn't significant for natives--as they
// could efficiently test `Is_Void(arg)` and `return Init_Nulled(OUT)`.  But
// usermode functions benefit more since `if void? arg [return null]` needs
// several frames and lookups to run.
//
// In both the native and usermode cases, the <maybe> annotation helps convey
// the contract of "void-in-null-out" more clearly than just being willing to
// take a void and able to return null--which doesn't connect the two states.
//
#define PARAMETER_FLAG_NOOP_IF_VOID \
    FLAG_LEFT_BIT(13)


//=//// PARAMETER_FLAG_NOTHING_DEFINITELY_OK //////////////////////////////=//
//
// See notes on NULL_DEFINITELY_OK
//
#define PARAMETER_FLAG_NOTHING_DEFINITELY_OK \
    FLAG_LEFT_BIT(14)


//=//// PARAMETER_FLAG_NIHIL_DEFINITELY_OK ////////////////////////////////=//
//
// See notes on NULL_DEFINITELY_OK
//
#define PARAMETER_FLAG_NIHIL_DEFINITELY_OK \
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
// This flag not being set doesn't mean nulls aren't ok (some unoptimized
// typechecker might accept nulls).
//
#define PARAMETER_FLAG_NULL_DEFINITELY_OK \
    FLAG_LEFT_BIT(17)


//=//// PARAMETER_FLAG_ANY_VALUE_OK ///////////////////////////////////////=//
//
// The check for ANY-VALUE? (e.g. any element or stable isotope) is very
// common, and has an optimized flag if the ANY-VALUE? function is detected
// in the parameter spec.
//
#define PARAMETER_FLAG_ANY_VALUE_OK \
    FLAG_LEFT_BIT(18)


//=//// PARAMETER_FLAG_ANY_ATOM ///////////////////////////////////////////=//
//
// The ANY-ATOM? check takes its argument as a meta parameter, so it doesn't
// fit into the "Decider" optimization.  It's likely that the deciders should
// be rethought so that things like SPLICE? can be deciders, probably by
// grouping all the meta deciders together at the end of the list.
//
#define PARAMETER_FLAG_ANY_ATOM_OK \
    FLAG_LEFT_BIT(19)


//=//// PARAMETER_FLAG_VOID_DEFINITELY_OK /////////////////////////////////=//
//
// This may or may not be a great use of a bit in the flags, but we haven't
// run out of flags yet.  When we do, rethink the optimizations.
//
#define PARAMETER_FLAG_VOID_DEFINITELY_OK \
    FLAG_LEFT_BIT(20)


#define PARAMETER_FLAG_21           FLAG_LEFT_BIT(21)
#define PARAMETER_FLAG_22           FLAG_LEFT_BIT(22)
#define PARAMETER_FLAG_23           FLAG_LEFT_BIT(23)


#define Get_Parameter_Flag(v,name) \
    ((PARAMETER_FLAGS(v) & PARAMETER_FLAG_##name) != 0)

#define Not_Parameter_Flag(v,name) \
    ((PARAMETER_FLAGS(v) & PARAMETER_FLAG_##name) == 0)

#define Set_Parameter_Flag(v,name) \
    (PARAMETER_FLAGS(v) |= PARAMETER_FLAG_##name)

#define Clear_Param_Flag(v,name) \
    (PARAMETER_FLAGS(v) &= ~PARAMETER_FLAG_##name)



INLINE ParamClass Cell_ParamClass(const Cell* param) {
    assert(HEART_BYTE(param) == REB_PARAMETER);
    ParamClass pclass = u_cast(ParamClass, PARAMCLASS_BYTE(param));
    return pclass;
}

INLINE Option(const String*) Cell_Parameter_String(const Cell* param) {
    assert(HEART_BYTE(param) == REB_PARAMETER);
    return cast(const String*, Cell_Node2(param));
}

INLINE void Set_Parameter_String(Cell* param, Option(const String*) string) {
    assert(HEART_BYTE(param) == REB_PARAMETER);
    Tweak_Cell_Node2(param, maybe string);
}


// Antiform parameters are used to represent unspecialized parameters.  When
// the slot they are in is overwritten by another value, that indicates they
// are then fixed at a value and hence specialized--so not part of the public
// interface of the function.
//
INLINE bool Is_Specialized(const Param* p) {
    if (Is_Hole(c_cast(Value*, p))) {
        if (Get_Cell_Flag_Unchecked(p, VAR_MARKED_HIDDEN))
            assert(!"Unspecialized parameter is marked hidden!");
        return false;
    }
    return true;
}

#define Not_Specialized(v)      (not Is_Specialized(v))


//=//// CELL_FLAG_PARAM_NOTE_CHECKED //////////////////////////////////////=//
//
// For specialized or fulfilled values, a parameter which is checked does not
// need to be checked again.  This bit encodes that knowledge in a way that
// any new overwriting will signal need for another check:
//
//    >> /bad-negate: adapt negate/ [number: to text! number]
//
//    >> bad-negate 1020
//    ** Error: Internal phase disallows TEXT! for its `number` argument
//
// If you hadn't overwritten `number`, then it would still have CELL_FLAG_NOTE
// and not run type checking again:
//
//    good-negate: adapt negate/ [print "not modifying number, no check"]
//
// When a Param in a ParamList is unspecialized (e.g. antiform PARAMETER!, aka
// a "Hole") then if it does not carry CELL_FLAG_NOTE, then that means type
// checking against it is not the last word: there is a type underlying it
// which also needs to be checked.  Consider:
//
//     >> ap-int: copy meta:lite append/
//
//     >> ap-int.value: anti make parameter! [integer!]  ; or whatever syntax
//     == ~#[parameter! [integer!]]~  ; anti
//
//     >> /ap-int: anti ap-int
//     == ~#[frame! ...]~  ; anti
//
// You've just created a version of APPEND with a tighter type constraint.
// But what if that type were -looser-?  You must check this type, and also
// the type "underneath" it.
//

#define CELL_FLAG_PARAM_NOTE_TYPECHECKED  CELL_FLAG_NOTE

#define CELL_MASK_COPY_PARAM \
    (CELL_MASK_COPY | CELL_FLAG_PARAM_NOTE_TYPECHECKED)

INLINE bool Is_Typechecked(const Value* v) {
    Assert_Cell_Stable(v);
    assert(not Is_Hole(v));
    return Get_Cell_Flag(v, PARAM_NOTE_TYPECHECKED);
}

INLINE void Mark_Typechecked(const Value* v) {
    Assert_Cell_Stable(v);
    assert(not Is_Hole(v));
    Set_Cell_Flag(v, PARAM_NOTE_TYPECHECKED);
}

INLINE bool Is_Hole_Final_Type(const Param* p) {
    assert(Is_Hole(c_cast(Value*, p)));
    return Get_Cell_Flag(p, PARAM_NOTE_TYPECHECKED);
}



INLINE Param* Init_Unconstrained_Hole_Untracked(
    Init(Value) out,
    Flags flags
){
    ParamClass pclass = u_cast(ParamClass, FIRST_BYTE(&flags));
    assert(pclass != PARAMCLASS_0);  // must have class
    if (flags & PARAMETER_FLAG_REFINEMENT) {
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
    }
    UNUSED(pclass);

    Reset_Cell_Header_Noquote(out, CELL_MASK_PARAMETER);
    PARAMETER_FLAGS(out) = flags;
    Tweak_Cell_Parameter_Spec(out, nullptr);
    Tweak_Cell_Node2(out, nullptr);  // parameter string

    return cast(Param*, Coerce_To_Stable_Antiform(out));
}

#define Init_Unconstrained_Hole(out,param_flags) \
    TRACK(Init_Unconstrained_Hole_Untracked((out), (param_flags)))


INLINE bool Is_Parameter_Unconstrained(const Cell* param) {
    return Cell_Parameter_Spec(param) == nullptr;  // e.g. `[/refine]`
}


// There's no facility for making automatic typesets that include antiforms
// in the %types.r table.  If there were, this would be defined there.
//
INLINE bool Any_Vacancy(Need(const Value*) a) {
    if (Not_Antiform(a))
        return false;

    Heart heart = Cell_Heart(a);
    if (heart == REB_BLANK or heart == REB_PARAMETER or heart == REB_TAG)
        return true;

    return false;
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
