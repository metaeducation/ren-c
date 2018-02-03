//
//  File: %sys-varargs.h
//  Summary: {Definitions for Variadic Value Type}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#ifdef NDEBUG
    #define VARARGS_FLAG(n) \
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n))
#else
    #define VARARGS_FLAG(n) \
        (FLAGIT_LEFT(TYPE_SPECIFIC_BIT + (n)) | HEADERIZE_KIND(REB_VARARGS))
#endif


inline static REBOOL Is_Block_Style_Varargs(
    REBVAL **shared_out,
    const RELVAL *vararg
){
    assert(IS_VARARGS(vararg));

    if (
        IS_CELL(vararg->extra.binding)
        || (vararg->extra.binding->header.bits & ARRAY_FLAG_VARLIST)
    ){
        TRASH_POINTER_IF_DEBUG(*shared_out);
        return FALSE; // it's an ordinary vararg, representing a FRAME!
    }

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <...> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    REBARR *array1 = ARR(vararg->extra.binding);
    *shared_out = KNOWN(ARR_HEAD(array1));
    assert(
        IS_END(*shared_out)
        || (IS_BLOCK(*shared_out) && ARR_LEN(array1) == 1)
    );

    return TRUE;
}


inline static REBOOL Is_Frame_Style_Varargs_May_Fail(
    REBFRM **f,
    const RELVAL *vararg
){
    assert(IS_VARARGS(vararg));

    if (
        NOT_CELL(vararg->extra.binding)
        && NOT(vararg->extra.binding->header.bits & ARRAY_FLAG_VARLIST)
    ){
        TRASH_POINTER_IF_DEBUG(*f);
        return FALSE; // it's a block varargs, made via MAKE VARARGS!
    }

    // "Ordinary" case... use the original frame implied by the VARARGS!
    // (so long as it is still live on the stack)

    if (IS_CELL(vararg->extra.binding))
        *f = cast(REBFRM*, vararg->extra.binding);
    else
        *f = CTX_FRAME_MAY_FAIL(CTX(vararg->extra.binding));

    return TRUE;
}
