//
//  File: %cell-parameter.h
//  Summary: {Definitions for Parameter Values}
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
//     foo: func [
//         return: [integer!]  ; PARAMCLASS_RETURN
//         arg [<opt> block!]  ; PARAMCLASS_NORMAL
//         'qarg [word!]       ; PARAMCLASS_QUOTED
//         earg [<end> time!]  ; PARAMCLASS_NORMAL + PARAMETER_FLAG_ENDABLE
//         /refine [tag!]      ; PARAMCLASS_NORMAL + PARAMETER_FLAG_REFINEMENT
//         <local> loc         ; not a PARAMETER!, specialized to ~ isotope
//     ][
//        ...
//     ]
//
// Hence the parameter is a compressed digest of information gleaned from
// the properties of the named argument and its typechecking block.  The
// content of the typehecking block is also copied into an immutable array
// and stored inside the parameter.  (Refinements with no arguments store
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

INLINE Option(const Array*) Cell_Parameter_Spec(
    NoQuote(const Cell*) v
){
    assert(HEART_BYTE(v) == REB_PARAMETER);
    if (Cell_Node1(v) != nullptr and Not_Node_Accessible(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    return cast(Array*, Cell_Node1(v));
}

#define INIT_CELL_PARAMETER_SPEC(v, a) \
    Init_Cell_Node1((v), (a))



#if (! CPLUSPLUS_11) || (! DEBUG)
    #define PARAMETER_FLAGS(p) \
        *x_cast(uintptr_t*, &EXTRA(Parameter, (p)).parameter_flags)
#else
    INLINE uintptr_t& PARAMETER_FLAGS(const Cell* p) {
        assert(Is_Parameter(p));
        return const_cast<uintptr_t&>(EXTRA(Parameter, (p)).parameter_flags);
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
// missing (e.g. `do [+ 5]`) or an ordinary argument hit the end (e.g. the
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


//=//// PARAMETER_FLAG_SKIPPABLE //////////////////////////////////////////=//
//
// Skippability is used on quoted arguments to indicate that they are willing
// to "pass" on something that isn't a matching type.  This gives an ability
// that a variadic doesn't have, which is to make decisions about rejecting
// a parameter *before* the function body runs.
//
// Skippability is only applicable to literal arguments, because if an
// argument had to be evaluated to tell if it should be skipped or not then
// you could not undo the evaluation.  While there are some cases that could
// technically work (e.g. if there's another parameter afterward and it is
// evaluative) the semantics would be dodgy if you couldn't tell at source
// level whether something would be skipped or not.
//
#define PARAMETER_FLAG_SKIPPABLE \
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


//=//// PARAMETER_FLAG_TRASH_DEFINITELY_OK ////////////////////////////////=//
//
// See notes on NULL_DEFINITELY_OK
//
#define PARAMETER_FLAG_TRASH_DEFINITELY_OK \
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
// The check for ANY-ATOM? (e.g. literally any cell state) is relatively
// common, and has an optimized flag if the ANY-ATOM? function is detected
// in the parameter spec.
//
#define PARAMETER_FLAG_ANY_ATOM_OK \
    FLAG_LEFT_BIT(19)


#define PARAMETER_FLAG_20           FLAG_LEFT_BIT(20)
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



INLINE ParamClass Cell_ParamClass(const Param* param) {
    assert(HEART_BYTE(param) == REB_PARAMETER);
    ParamClass pclass = u_cast(ParamClass, PARAMCLASS_BYTE(param));
    return pclass;
}


// A "Param" can be any value (including isotopes) if it is specialized.
// But a typeset that does not have param class 0 is unspecialized.
//
INLINE bool Is_Specialized(const Param* param) {
    if (HEART_BYTE(param) == REB_PARAMETER) {
        assert(QUOTE_BYTE(param) == UNQUOTED_1);  // no quoteds
        if (Get_Cell_Flag_Unchecked(param, VAR_MARKED_HIDDEN))
            assert(!"Unspecialized parameter is marked hidden!");
        return false;
    }
    return not Is_Trash(param);
}


#define Init_Parameter(out,param_flags,spec,specifier) \
    TRACK(Init_Parameter_Untracked((out), (param_flags), (spec), (specifier)))


INLINE Param* Init_Unconstrained_Parameter_Untracked(
    Cell* out,
    Flags flags
){
    ParamClass pclass = u_cast(ParamClass, FIRST_BYTE(&flags));
    assert(pclass != PARAMCLASS_0);  // must have class
    if (flags & PARAMETER_FLAG_REFINEMENT) {
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
        assert(pclass != PARAMCLASS_RETURN and pclass != PARAMCLASS_OUTPUT);
    }
    UNUSED(pclass);

    Reset_Unquoted_Header_Untracked(out, CELL_MASK_PARAMETER);
    PARAMETER_FLAGS(out) = flags;
    INIT_CELL_PARAMETER_SPEC(out, nullptr);

    Param* param = cast(Param*, cast(REBVAL*, out));
    return param;
}

#define Init_Unconstrained_Parameter(out,param_flags) \
    TRACK(Init_Unconstrained_Parameter_Untracked((out), (param_flags)))


INLINE bool Is_Parameter_Unconstrained(NoQuote(const Cell*) param) {
    return Cell_Parameter_Spec(param) == nullptr;  // e.g. `[/refine]`
}
