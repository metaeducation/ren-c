//
//  file: %cell-parameter.h
//  summary: "Definitions for PARAMETER! Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//         return: [integer!]  ; specialized to plain PARAMETER! (not antiform)
//         arg [<opt> block!]  ; PARAMCLASS_NORMAL
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

#define CELL_PARAMETER_PAYLOAD_1_SPEC(c)    CELL_PAYLOAD_1(c)
#define CELL_PARAMETER_EXTRA_STRAND(c)  CELL_EXTRA(c)

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define CELL_PARAMETER_PAYLOAD_2_FLAGS(c)  (c)->payload.split.two.flags
#else
    INLINE const uintptr_t& CELL_PARAMETER_PAYLOAD_2_FLAGS(const Cell* c) {
        assert(Unchecked_Heart_Of(c) == TYPE_PARAMETER);
        return c->payload.split.two.flags;
    }

    INLINE uintptr_t& CELL_PARAMETER_PAYLOAD_2_FLAGS(Cell* c) {
        assert(Unchecked_Heart_Of(c) == TYPE_PARAMETER);
        return c->payload.split.two.flags;
    }
#endif

#define PARAMCLASS_BYTE(c)  FIRST_BYTE(&CELL_PARAMETER_PAYLOAD_2_FLAGS(c))
#define FLAG_PARAMCLASS_BYTE(b)     FLAG_FIRST_BYTE(b)

INLINE Option(const Source*) Parameter_Spec(const Cell* c) {
    assert(Heart_Of(c) == TYPE_PARAMETER);

    const Base* base = CELL_PARAMETER_PAYLOAD_1_SPEC(c);
    if (base != nullptr and Not_Base_Readable(base))
        panic (Error_Series_Data_Freed_Raw());

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


//=//// PARAMETER_FLAG_FINAL_TYPECHECK ////////////////////////////////////=//
//
// When a Param in a ParamList is unspecialized (e.g. a PARAMETER! value)
// then if it does not carry this flag, then that means typechecking against
// it is not the last word.  There is a type underlying it which also needs
// to be checked.  Consider:
//
//     >> ap-int: copy lift:lite append/
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


//=//// PARAMETER_FLAG_OPT_OUT ////////////////////////////////////////////=//
//
// If a parameter is marked with the `<opt-out>` annotation, then that means
// if that argument is void in a function invocation, the dispatcher for the
// function won't be run at all--and ~null~ will be returned by the call.
//
// This helps avoid the need to take the argument as ^META just to do the
// test for void, if this is the intent.  Beyond convenience, it doesn't speed
// natives up all that much, as they could test `Is_Void(arg)` and then
// `return Init_Nulled(OUT); Unliftify_Undecayed(arg);`...which would
// be fairly fast.  But it speeds up usermode code much more, considering that
// `if void? ^arg [return null]` needs several frames and lookups to run.
//
// Plus the <opt-out> annotation helps convey the "void-in-null-out" contract
// more clearly than just being willing to take void and able to return null,
// which doesn't connect the two states.
//
#define PARAMETER_FLAG_OPT_OUT \
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
// a void, so long as there's no need to distinguish it from null.  See also
// the <opt-out> parameter flag, which can be used if the only processing
// for a void would be to return null as the overall function result with
// no further side-effects.
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


#define PARAMETER_FLAG_22           FLAG_LEFT_BIT(22)
#define PARAMETER_FLAG_23           FLAG_LEFT_BIT(23)


#define Get_Parameter_Flag(v,name) \
    ((CELL_PARAMETER_PAYLOAD_2_FLAGS(v) & PARAMETER_FLAG_##name) != 0)

#define Not_Parameter_Flag(v,name) \
    ((CELL_PARAMETER_PAYLOAD_2_FLAGS(v) & PARAMETER_FLAG_##name) == 0)

#define Set_Parameter_Flag(v,name) \
    (CELL_PARAMETER_PAYLOAD_2_FLAGS(v) |= PARAMETER_FLAG_##name)

#define Clear_Param_Flag(v,name) \
    (CELL_PARAMETER_PAYLOAD_2_FLAGS(v) &= ~PARAMETER_FLAG_##name)



INLINE ParamClass Parameter_Class(const Cell* param) {
    assert(Heart_Of(param) == TYPE_PARAMETER);
    return cast(ParamClass, PARAMCLASS_BYTE(param));
}

INLINE Option(const Strand*) Parameter_Strand(const Cell* param) {
    assert(Heart_Of(param) == TYPE_PARAMETER);
    return cast(const Strand*, CELL_PARAMETER_EXTRA_STRAND(param));
}

INLINE void Set_Parameter_Strand(Cell* param, Option(const Strand*) string) {
    assert(Heart_Of(param) == TYPE_PARAMETER);
    CELL_PARAMETER_EXTRA_STRAND(param) = m_cast(Strand*, opt string);
}


// Antiform parameters are used to represent unspecialized parameters.  When
// the slot they are in is overwritten by another value, that indicates they
// are then fixed at a value and hence specialized--so not part of the public
// interface of the function.
//
INLINE bool Is_Specialized(const Param* p) {
    if (Is_Parameter(p)) {
        if (Get_Cell_Flag_Unchecked(p, VAR_MARKED_HIDDEN))
            assert(!"Unspecialized parameter is marked hidden!");
        return false;
    }
    return true;
}

#define Not_Specialized(v)      (not Is_Specialized(v))


//=//// CELL_FLAG_PARAM_NOTE_TYPECHECKED //////////////////////////////////=//
//
// For specialized or fulfilled values, a parameter which is checked does not
// need to be checked again.  This bit encodes that knowledge in a way that
// any new overwriting will signal need for another check:
//
//    >> /bad-negate: adapt negate/ [value: to text! value]
//
//    >> bad-negate 1020
//    ** Error: Internal phase disallows TEXT! for its `value` argument
//
// If you hadn't overwritten `value`, then it would still have CELL_FLAG_NOTE
// and not run type checking again:
//
//    good-negate: adapt negate/ [print "not modifying value, no check"]
//

#define CELL_FLAG_PARAM_NOTE_TYPECHECKED  CELL_FLAG_NOTE

INLINE bool Is_Typechecked(const Atom* arg) {
    return Get_Cell_Flag(arg, PARAM_NOTE_TYPECHECKED);
}

INLINE void Mark_Typechecked(const Atom* arg) {
    Set_Cell_Flag(arg, PARAM_NOTE_TYPECHECKED);
}

INLINE bool Is_Parameter_Final_Type(const Param* p) {
    assert(Heart_Of(p) == TYPE_PARAMETER);
    return Get_Parameter_Flag(p, FINAL_TYPECHECK);
}


//=//// PARAMETER "BLITTING" //////////////////////////////////////////////=//
//
// There's a not-insignificant optimization when building function call frames
// to assume the target slot is uninitialized, and overwrite its bits without
// doing masking operations to preserve CELL_MASK_PERSIST.  So when proxying
// specialized slots we can just take the bits directly.
//
// 1. "sealed" parameters in the ParamList carry CELL_FLAG_VAR_MARKED_HIDDEN.
//    If there were more free Cell bits, we could make this something that
//    only had significance in the "phase" slot of a frame.  But since we
//    don't, the flag does double duty--and we don't want running frames
//    to confuse it with PROTECT:HIDE slots.  We have to clear it if the
//    slot is being filled on behalf of a running varlist.
//

INLINE Cell* Blit_Param_Drop_Mark_Untracked(Cell* out, const Param* p) {
    Blit_Cell_Untracked(out, p);  // checked build ensures out is poison/erased
    Clear_Cell_Flag(out, VAR_MARKED_HIDDEN);  // sealed params marked [1]
  #if DEBUG_PROTECT_PARAM_CELLS
    Clear_Cell_Flag(out, PROTECTED);
  #endif
    return out;
}

#define Blit_Param_Drop_Mark(out,p) \
    TRACK(Blit_Param_Drop_Mark_Untracked(out,p))

INLINE Cell* Blit_Param_Unmarked_Untracked(Cell* out, const Param* p) {
    assert(Not_Cell_Flag(p, VAR_MARKED_HIDDEN));
    Blit_Cell_Untracked(out, p);  // checked build ensures out is poison/erased
  #if DEBUG_PROTECT_PARAM_CELLS
    Clear_Cell_Flag(out, PROTECTED);
  #endif
    return out;
}

#define Blit_Param_Unmarked(out,p) \
    TRACK(Blit_Param_Unmarked_Untracked(out,p));

#define Blit_Param_Keep_Mark(out,p) \
    Blit_Cell(out,p)  // for when not making a running varlist [1]


//=//// FAST ANTI-WORD "BLITTING" /////////////////////////////////////////=//
//
// ~null~ and ~okay~ antiforms are put into varlist slots during argument
// fulfillment, where those slots have nothing to worry about overwriting.
// We can write them a bit faster.  Small benefit, but...it adds up.
//

#define Blit_Null_Typechecked(out) \
    TRACK(Blit_Word_Untracked( \
        (out), \
        FLAG_LIFT_BYTE(ANTIFORM_1) \
            | CELL_FLAG_KEYWORD_IS_NULL | CELL_FLAG_PARAM_NOTE_TYPECHECKED, \
        CANON(NULL) \
    ))

#define Blit_Okay_Typechecked(out) \
    TRACK(Blit_Word_Untracked( \
        (out), \
        FLAG_LIFT_BYTE(ANTIFORM_1) | CELL_FLAG_PARAM_NOTE_TYPECHECKED, \
        CANON(OKAY) \
    ))


INLINE Param* Init_Unconstrained_Parameter_Untracked(
    Init(Element) out,
    Flags flags
){
    Init(Param) param = u_cast(Param*, out);

    ParamClass pclass = u_cast(ParamClass, FIRST_BYTE(&flags));
    assert(pclass != PARAMCLASS_0);  // must have class
    if (flags & PARAMETER_FLAG_REFINEMENT) {
        assert(flags & PARAMETER_FLAG_NULL_DEFINITELY_OK);
    }
    UNUSED(pclass);

    Reset_Cell_Header_Noquote(
        param,
        BASE_FLAG_BASE | BASE_FLAG_CELL
            | FLAG_HEART(TYPE_PARAMETER)
            | CELL_FLAG_DONT_MARK_PAYLOAD_1  // spec (starting off null here)
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // flags, never marked
    );
    CELL_PARAMETER_PAYLOAD_1_SPEC(param) = nullptr;
    CELL_PARAMETER_PAYLOAD_2_FLAGS(param) = flags;
    CELL_PARAMETER_EXTRA_STRAND(param) = nullptr;

    return param;
}

#define Init_Unconstrained_Parameter(out,param_flags) \
    TRACK(Init_Unconstrained_Parameter_Untracked((out), (param_flags)))


INLINE bool Is_Parameter_Unconstrained(const Cell* param) {
    return Parameter_Spec(param) == nullptr;  // e.g. `[/refine]`
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
