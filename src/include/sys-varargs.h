//
//  file: %sys-varargs.h
//  summary:{Definitions for Variadic Value Type}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A VARARGS! represents a point for parameter gathering inline at the
// callsite of a function.  The point is located *after* that function has
// gathered all of its arguments and started running.  It is implemented by
// holding a reference to a reified FRAME! series, which allows it to find
// the point of a running evaluation (as well as to safely check for when
// that call is no longer on the stack, and can't provide data.)
//
// A second VARARGS! form is implemented as a thin proxy over an ANY-ARRAY!.
// This mimics the interface of feeding forward through those arguments, to
// allow for "parameter packs" that can be passed to variadic functions.
//
// When the bits of a payload of a VARARGS! are copied from one item to
// another, they are still maintained in sync.  TAKE-ing a vararg off of one
// is reflected in the others.  This means that the "indexor" position of
// the vararg is located through the frame pointer.  If there is no frame,
// then a single element array (the `array`) holds an ANY-ARRAY! value that
// is shared between the instances, to reflect the state.
//


// While it would be possible to say that infixing a function whose first
// argument is a VARARGS! is plainly illegal, we experimentally allow the
// left hand side of an evaluation to be a source of "0 or 1" arguments for
// a VARARGS!.
//
// !!! This is a bit shady (in cases besides an <end> on the left being a
// varargs that reports TAIL? as TRUE).  That's because most variadics expect
// their evaluation to happen when they TAKE a VARARGS!, and not beforehand.
// But you can't defer the evaluation of a left-hand expression, because it's
// usually too late.  Even if it isn't technically too late for some reason
// (e.g. it's #tight, or quoted) there's still a bit of an oddity, because
// variadics on the right have the option to *not* do a TAKE and leave the
// value for consumption by the next operation.  That doesn't apply when the
// variadic is being "faked in" from the left.
//
// But despite the lack of "purity", one might argue it's better to do
// something vs. just give an error.  Especially since people are unlikely to
// infix a variadic on accident, and may be fine with these rules.
//
#define CELL_FLAG_VARARGS_INFIX FLAG_TYPE_SPECIFIC_BIT(0)


INLINE bool Is_Block_Style_Varargs(
    Value* *shared_out,
    const Cell* vararg
){
    assert(Is_Varargs(vararg));

    if (vararg->extra.binding->header.bits & ARRAY_FLAG_IS_VARLIST) {
        *shared_out = nullptr; // avoid compiler warning in -Og build
        return false; // it's an ordinary vararg, representing a FRAME!
    }

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <...> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    Array* array1 = cast_Array(vararg->extra.binding);
    *shared_out = KNOWN(Array_Head(array1));
    assert(
        IS_END(*shared_out)
        or (Is_Block(*shared_out) and Array_Len(array1) == 1)
    );

    return true;
}


INLINE bool Is_Level_Style_Varargs_Maybe_Null(
    Option(Level*)* L_out,
    const Cell* vararg
){
    assert(Is_Varargs(vararg));

    if (not (vararg->extra.binding->header.bits & ARRAY_FLAG_IS_VARLIST)) {
        *L_out = nullptr; // avoid compiler warning in -Og build
        return false; // it's a block varargs, made via MAKE VARARGS!
    }

    // "Ordinary" case... use the original frame implied by the VARARGS!
    // (so long as it is still live on the stack)

    *L_out = Level_Of_Varlist_If_Running(CTX(vararg->extra.binding));
    return true;
}


INLINE bool Is_Level_Style_Varargs_May_Panic(
    Level** L_out,
    const Cell* vararg
){
    Option(Level*) L;
    if (not Is_Level_Style_Varargs_Maybe_Null(&L, vararg))
        return false;

    if (not L)
        panic (Error_Frame_Not_On_Stack_Raw());

    *L_out = unwrap L;
    return true;
}


INLINE const Value* Param_For_Varargs_Maybe_Null(const Cell* v) {
    assert(Is_Varargs(v));

    REBACT *phase = v->payload.varargs.phase;
    if (phase) {
        Array* paramlist = ACT_PARAMLIST(phase);
        return KNOWN(Array_At(paramlist, v->payload.varargs.param_offset + 1));
    }

    // A vararg created from a block AND never passed as an argument so no
    // typeset or quoting settings available.  Treat as "normal" parameter.
    //
    assert(not (v->extra.binding->header.bits & ARRAY_FLAG_IS_VARLIST));
    return nullptr;
}
