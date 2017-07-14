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
    REBVAL **position_out,
    REBVAL *varargs
){
    assert(IS_VARARGS(varargs));

    if (GET_SER_FLAG(varargs->payload.varargs.feed, ARRAY_FLAG_VARLIST)) {
        assert(varargs->extra.binding != NULL);
        return FALSE; // it's an ordinary vararg, representing a FRAME!
    }

    assert(varargs->extra.binding == NULL);

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <...> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    assert(ARR_LEN(varargs->payload.varargs.feed) == 1);
    *position_out = KNOWN(ARR_HEAD(varargs->payload.varargs.feed));
    return TRUE;
}


inline static REBOOL Is_Frame_Style_Varargs_May_Fail(
    REBFRM **f,
    REBVAL *varargs
){
    assert(IS_VARARGS(varargs));

    if (NOT_SER_FLAG(varargs->payload.varargs.feed, ARRAY_FLAG_VARLIST)) {
        assert(varargs->extra.binding == NULL);
        return FALSE; // it's a block varargs, made via MAKE VARARGS!
    }

    REBCTX *c = CTX(varargs->extra.binding);
    *f = CTX_FRAME_IF_ON_STACK(c);

    // If the VARARGS! has a call frame, then ensure that the call frame
    // where the VARARGS! originated is still on the stack.
    //
    if (*f == NULL)
        fail (Error_Varargs_No_Stack_Raw());

    return TRUE;
}
