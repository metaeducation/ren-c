//
//  file: %sys-arg.h
//  summary: "Helpers related to processing arguments to natives"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2026 Ren-C Open Source Contributors
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
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Level* passed to every
// DECLARE_NATIVE ('level_') and read the information out cleanly, using the
// ARG() macro to get the argument by name.
//
//     //
//     //  native: [
//     //
//     //  "Native documentation string"
//     //
//     //       return: [<return-type>]
//     //       arg "Argument description"
//     //            [<arg-types>]
//     //       :refine "Refinement description"
//     //            [<refine-types>]
//     //  ]
//     //
//     DECLARE_NATIVE(NATIVE_NAME)
//     {
//         INCLUDE_PARAMS_OF_NATIVE_NAME;  // <-- expands to #defines
//
//         if (Is_Integer(ARG(ARG))) { ... } // this file defines ARG()
//         if (ARG(REFINE)) { ... }  // ARG() knows to make this an Option(T)
//         ...
//     }
//
// For a long explanation of the evolution that brought about this design:
//
//   https://rebol.metaeducation.com/t/r3-alpha-reduce-in-ren-c-today/2599
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. By contract, Rebol functions are allowed to mutate their arguments and
//    refinements just as if they were locals...guaranteeing only their return
//    result as externally visible.  Hence the ARG() cells provide a GC-safe
//    slot for natives to hold values once they are no longer needed.
//


//=//// PARAMETER DECLARATION MACROS //////////////////////////////////////=//
//
// The EMIT-INCLUDE-PARAMS-MACRO code in %native-emitters makes a macro for
// each native that looks like this:
//
//   #define INCLUDE_PARAMS_OF_ENVELOP /  <- no backslash in comments
//      Set_Flex_Info(Varlist_Array(level_->varlist), HOLD); /
//      DECLARE_CHECKED_PARAM(Need(Stable*), EXAMPLE, 1, ARGMODE_REQUIRED); /
//      DECLARE_CHECKED_PARAM(Option(Stable*), CONTENT, 2, ARGMODE_OPTIONAL)
//
// These are the DECLARE_CHECKED_PARAM() and DECLARE_UNCHECKED_PARAM() macros
// that expands out into, which provides the information for the ARG() to
// know how what compile-time types to use, and what runtime code to use to
// extract the argument from the Level.
//
// 1. Using enum values here for the parameter numbers (as opposed to const
//    ints of some flavor) pretty much guarantees they will be compile-time
//    constants in all compilers.
//
// 2. Typically the rigor brought in by unused variable and unused argument
//    warnings is worth the cost.  But we suppress that here.  Reasoning:
//
//    * Many cases of using INCLUDE_PARAMS_OF_XXX are used to do partial
//      processing (e.g. generics) which pass on the work elsewhere.  It's
//      oppressive to have to mark USED() on variables you don't mention,
//      and discouraged using clearer ARG() vs ARG_N() accessors.
//
//    * There's no corresponding feature for librebol-based natives; e.g.
//      the INCLUDE_PARAMS_OF_XXX macro only enables rebValue("arg") but
//      can't warn you if you don't mention all the arguments.
//
//    * Conditional suppression of the local typedef is tricky to do when
//      you use accessors that don't want to use the type, and if it were
//      unconditionally suppressed then one of the other values would have
//      to serve as the warning trigger--and that would mean not using the
//      enum trick that enforces compile-time constancy.
//
//    * Being made to mark everything USED() or UNUSED() creates noise that
//      makes it hard to see the real cases of unused variables that matter.
//
//    So the primary idea here is that you get the unused variable warning
//    when you don't use the arguments that you explicitly create variables
//    for, e.g. when you say `Value* var = ARG(NAME);` and don't use that.
//    And we cross our fingers and hope that AI of the future could help
//    detect when you meant to use an argument but did not (and be able to
//    say that for librebol-based natives as well).
//

typedef enum {
    ARGMODE_REQUIRED,  // note: <hole> parameters act as required, give Param*
    ARGMODE_OPTIONAL,
    ARGMODE_INTRINSIC
} ArgMode;

#define DECLARE_CHECKED_PARAM(T,name,n,argmode) \
    enum { checked_##name##_ = (n) }; /* enum for compile-time const [1] */ \
    enum { checked_argmode_##name##_ = (argmode) }; \
    typedef T checked_type_##name##_; \
    USED(sizeof(checked_type_##name##_)) /* trust author :-/ [2] */

#define DECLARE_UNCHECKED_PARAM(T,name,n,argmode) \
    enum { unchecked_##name##_ = (n) }; /* enum for compile-time const [1] */ \
    enum { unchecked_argmode_##name##_ = (argmode) }; \
    typedef T unchecked_type_##name##_; \
    USED(sizeof(unchecked_type_##name##_)) /* trust author :-/ [2] */


//=//// LEVEL ARGUMENT FETCHING ///////////////////////////////////////////=//
//
// What we would *ideally* like would be to have ARG() dispatch directly to
// different functions, based on the modality of the argument, e.g.:
//
//    #define ARGMODE_REQUIRED_OF_LEVEL(...)  Required_Arg_Of_Level(...)
//    #define ARGMODE_OPTIONAL_OF_LEVEL(...)  Optional_Arg_Of_Level(...)
//    #define ARGMODE_INTRINSIC_OF_LEVEL(...)  Intrinsic_Arg_Of_Level(...)
//
//    #define ARG_DISPATCH(name, argmode, ...) /  <- no backslash in comments
//        cast(checked_type_##name##_, argmode##_OF_LEVEL#(...))
//
//    #define ARG(name)  ARG_DISPATCH(name, ???, ...)  // no way to write ???
//
// This could avoid what we are doing now (passing the modality to a single
// Arg_Of_Level_Inline() function with a switch() statement inside of it).
// But to do this we would need the argument modality to be token-pasteable by
// preprocessing, and that would mean being able to navigate from the argument
// name to some per-native global declaration for that name.
//
// Unfortunately, we are limited by the C preprocessor, and at compile-time
// can't get a token-pasteable answer for what native we are inside.  So we
// hope that having compile-time constants for the modality and argument
// number will lead to aggressive inlining in any compiler worth using, to
// where it's as efficient as if we used the above dispatching scheme.
//
// 1. It was once the case that typechecking intrinsics protected their args,
//    but the typechecking process proved destructive anyways (e.g. stripping
//    off quotes etc.) and always copied the checked value back into the
//    spare cell for each dispatch.  So now it's always mutable.
//

INLINE Cell* Arg_Of_Level_Inline(
    Level* L,
    Unchecked(Ordinal) n,  // most callsites pass as *compile-time constant*
    ArgMode argmode  // most callsites pass as *compile-time constant*
){
    switch (argmode) {  // hope switch gets constant-folded by optimizer!
      case ARGMODE_REQUIRED:
        assert(n != 0 and n <= Level_Num_Slots(L));
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        break;

      case ARGMODE_OPTIONAL:
        assert(n != 0 and n <= Level_Num_Slots(L));
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        if (Is_Light_Null(u_cast(Value*, L->rootvar) + n))
            return nullptr;  // should get wrapped by an Option(T)
        break;

      case ARGMODE_INTRINSIC:
        assert(n == 1);
        if (Get_Level_Flag(L, DISPATCHING_INTRINSIC)) {
            assert(Not_Cell_Flag(Level_Spare(L), SLOT_AURA_PROTECTED));  // see [1]
            return Level_Spare(L);
        }
        break;
    }

    return L->rootvar + n;  // hope this gets inlined by optimizer as well!
}

#if NO_RUNTIME_CHECKS
    #define Required_Arg_Of_Level(L,n) \
        (u_cast(Param*, (L)->rootvar) + (n))
#else
    #define Required_Arg_Of_Level(L,n) \
        Arg_Of_Level_Inline((L), (n), ARGMODE_REQUIRED)
#endif

#define Optional_Arg_Of_Level(L,n) \
    u_cast(Option(Cell*), Arg_Of_Level_Inline((L), (n), ARGMODE_OPTIONAL))

#define Intrinsic_Arg_Of_Level(L,n) \
    Arg_Of_Level_Inline((L), (n), ARGMODE_INTRINSIC)


//=//// ARGUMENT ACCESSOR MACROS //////////////////////////////////////////=//
//
// ARG() uses info from DECLARE_CHECKED_PARAM() and DECLARE_UNCHECKED_PARAM()
// to have polymorphic behavior, giving the right type and doing the right
// extraction for the parameter.
//
// By having it produce an Option(T) when the parameter is optional, this
// helps avoid glossing over reading a refinement or <opt> argument without
// checking if it was provided first.  It also eliminates the need for a
// separate refinement accessor for the boolean state of whether it is
// present or not, because the conversion of nulled cells to nullptr gives
// a boolean test for that as simply `if (ARG(REFINE)) {...}`.
//
// For safety, *non*-optional parameters are wrapped in Need(T), which does
// not -allow- testing with `if (ARG(REQUIRED)) {...}`.  Though note these
// checks are only done in C++ builds when the wrapper behavior is enabled.
//
// If a parameter is marked as <hole>, then ARG() will give back a Param*.
// This needs to be triaged as not being missing before accessed with normal
// Value* accessors.
//

#define Level_Args_Head(L) \
    (u_cast(Arg*, (L)->rootvar) + 1)  // 1-based indexing

#define ARG(name) \
    cast(checked_type_##name##_, Arg_Of_Level_Inline( \
        level_, \
        checked_##name##_, \
        u_cast(ArgMode, checked_argmode_##name##_)))

#define Unchecked_ARG(name) \
    cast(unchecked_type_##name##_, Arg_Of_Level_Inline( \
        level_, \
        unchecked_##name##_, \
        u_cast(ArgMode, unchecked_argmode_##name##_)))

#define Element_ARG(name) \
    As_Element(ARG(name))

#define ARG_N(n) \
    As_Stable(Required_Arg_Of_Level(level_, (n)))

#define Element_ARG_N(n) \
    As_Element(Required_Arg_Of_Level(level_, (n)))

#define Erase_ARG(name) \
    u_cast(Init(Slot), \
        Erase_Cell(Required_Arg_Of_Level(level_, checked_##name##_)))


//=//// PARAMETER ACCESSOR MACROS /////////////////////////////////////////=//
//
// Note that PARAM() on a slot that is not in the parameter list will give
// you the specialized value that lives in that slot.
//

#define PARAM_INDEX(name) /* can't use in some macro expansion orders */ \
    (checked_##name##_)

#define PARAM(name) /* a PARAMETER! (don't use on LOCALs) */ \
    Phase_Param(Level_Phase(level_), checked_##name##_)

#define PARAM_N(n) \
    Phase_Param(Level_Phase(level_), (n))


//=//// LOCAL VARIABLE (or UNCHECKED ARGUMENT) ACCESS /////////////////////=//
//
// The LOCAL(name) macro originated as a way to get at slots that were not
// on the public parameter list of a native.  However, as the ARG() macro
// became more sophisticated it limited the interface for using the slot...
// in ways that may cease to be applicable (e.g. if the slot is modified).
//
// So LOCAL() became a way to get generalized access to a native frame slot
// without the typechecking or conversions of ARG().
//
// (This may be better called SLOT() or something else in the future, but it
// is called LOCAL() for now to help associate it with FRAME! specifically,
// since SLOT() is more generically applicable to OBJECT! slots.)

#define LOCAL(name) \
    cast(Value*, Required_Arg_Of_Level(level_, PARAM_INDEX(name)))

#define Sink_LOCAL(name) \
    u_cast(Sink(Value), Required_Arg_Of_Level(level_, PARAM_INDEX(name)))

#define Element_LOCAL(name) \
    cast(Element*, Required_Arg_Of_Level(level_, PARAM_INDEX(name)))

#define Stable_LOCAL(name) \
    cast(Stable*, Required_Arg_Of_Level(level_, PARAM_INDEX(name)))


//=//// HELPERS TO PROCESS UNPROCESSED ARGUMENTS //////////////////////////=//
//
// ** WHEN RUN AS AN INTRINSIC, THE ARG IN THE SPARE CELL CONTAINS A FULLY NON
// TYPECHECKED META REPRESENTATION, AND THE NATIVE IS RESPONSIBLE FOR ALL
// ARGUMENT PROCESSING (WITH THE EXCEPTION OF VETO and <opt>).**
//
// Not only that, but the special case of typechecking intrinsics (that
// return LOGIC_OUT?) is that they can't write to L->out...because if you were
// typechecking the argument in the output cell, checking would overwrite it.
// Instead they have to communicate their result with BOUNCE_OKAY or nullptr
// as the native return result (use LOGIC(true), LOGIC(false) to be clear).
//
// The goal is making intrinsic dispatch cheap.  And quite simply, it won't
// be cheap if you turn around and have to do typechecking on the argument,
// because that would entail calling more natives.  Furthermore, more natives
// could not use the intrinsic trick...because the SPARE and SCRATCH are
// already committed to the intrinsic that's running.  It would undermine
// the whole point of intrinsics to typecheck their argument.
//
// These helpers are used to perform the argument processing.
//
// 1. There's an unusual situation arising due to the fact that we're doing
//    the typecheck "inside the function call": we *might* or *might not* want
//    to execute a panic() if the typecheck fails.  The case where we do not
//    is when we've dispatched an intrinsic to do a typecheck, and it's
//    enough to just return nullptr as if the typecheck didn't succeed.
//
// 2. If this returns nullptr, then the caller should return nullptr.
//

INLINE Details* Level_Intrinsic_Details(Level* L) {
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Ensure_Level_Details(L);

    Stable* frame = As_Stable(Level_Scratch(L));
    possibly(Is_Antiform(frame));  // LIFT_BYTE() is not canonized
    return Ensure_Frame_Details(frame);
}

INLINE Option(const Symbol*) Level_Intrinsic_Label(Level* L) {
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Try_Get_Action_Level_Label(L);

    Stable* frame = As_Stable(Level_Scratch(L));
    possibly(Is_Antiform(frame));  // LIFT_BYTE() is not canonized
    return Frame_Label_Deep(frame);
}

INLINE Result(Option(Element*)) Typecheck_Element_Intrinsic_Arg(
    Level* L
){
    Value* arg = As_Value(Intrinsic_Arg_Of_Level(L, 1));

    if (Is_Antiform(arg)) {
        if (Get_Level_Flag(L, RUNNING_TYPECHECK))  // [1]
            return nullptr;  // [2]
        return fail (Error_Bad_Intrinsic_Arg_1(L));
    }

    return As_Element(arg);
}

INLINE bool Is_Intrinsic_Typechecker(Details* details) {
    const Element* param = As_Element(
        Details_At(details, IDX_RAW_NATIVE_RETURN)
    );
    const Source* array = opt Parameter_Spec(param);
    return (
        array
        and Array_Len(array) == 1
        and Is_Word(Array_At(array, 0))
        and Word_Id(Array_At(array, 0)) == SYM_LOGIC_Q
    );
}


//=//// DEFINITIONAL RETURN/YIELD INJECTION ///////////////////////////////=//
//
// Shared code for putting a definitional RETURN or YIELD into the first slot
// (or second slot) of a Level's frame.
//

INLINE void Inject_Definitional_Returner(
    Level* L,
    const Value* definitional,  // LIB(DEFINITIONAL_RETURN), or YIELD
    SymId id  // SYM_YIELD, SYM_RETURN
){
    Details* details = Ensure_Level_Details(L);

    Index slot_num = Get_Details_Flag(details, METHODIZED) ? 2 : 1;

    assert(
        Key_Id(Varlist_Key(L->varlist, slot_num)) == Starred_Returner_Id(id)
    );
    assert(Is_Base_Managed(L->varlist));

    Value* returner = As_Value(
        Required_Arg_Of_Level(L, slot_num)  // should start out specialized
    );
    assert(Is_Possibly_Unstable_Value_Parameter(returner));

    assert(Not_Cell_Flag(returner, PARAM_MARKED_SEALED));  // should be seen
    Init_Action(
        returner,
        Frame_Phase(definitional),  // DEFINITIONAL-RETURN or YIELD
        Canon_Symbol(Starred_Returner_Id(id)),
        L->varlist  // so knows where to RETURN/YIELD from
    );
}


//=//// METHODIZATION INJECTION ////////////////////////////////////////////=//
//
// If DETAILS_FLAG_METHODIZED is set, we need to initialize the `.` slot in
// the frame with the coupling object.  It will always be the first frame
// slot if it's there, because Pop_Paramlist() ensures that.
//

INLINE void Inject_Methodization_If_Any(Level* L)
{
    Details* details = Ensure_Level_Details(L);

    if (Not_Details_Flag(details, METHODIZED))
        return;

    assert(Key_Id(Phase_Keys_Head(L->varlist)) == SYM_DOT_1);

    Arg* methodization = Level_Args_Head(L);

    Context* coupling = opt Level_Coupling(L);

    // !!! TBD: apply typecheck of methodization against coupled object

    Init_Object(methodization, cast(VarList*, coupling));
}
