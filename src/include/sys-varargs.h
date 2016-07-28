//
//  File: %sys-varargs.h
//  Summary: {Definitions for Variadic Value Type}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
        (cast(REBUPT, 1) << (TYPE_SPECIFIC_BIT + (n))) 
#else
    #define VARARGS_FLAG(n) \
        ((cast(REBUPT, 1) << (TYPE_SPECIFIC_BIT + (n))) \
            | TYPE_SHIFT_LEFT_FOR_HEADER(REB_VARARGS))
#endif

// Was made with a call to MAKE VARARGS! with data from an ANY-ARRAY!
// If that is the case, it does not use the varargs payload at all,
// rather it uses the Reb_Any_Series payload.
//
#define VARARGS_FLAG_NO_FRAME VARARGS_FLAG(0)


inline static const REBVAL *VAL_VARARGS_PARAM(const RELVAL *v)
    { return v->payload.varargs.param; }

inline static REBVAL *VAL_VARARGS_ARG(const RELVAL *v)
    { return v->payload.varargs.arg; }

inline static REBCTX *VAL_VARARGS_FRAME_CTX(const RELVAL *v) {
    ASSERT_ARRAY_MANAGED(v->extra.binding);
    assert(GET_ARR_FLAG(v->extra.binding, ARRAY_FLAG_VARLIST));
    return AS_CONTEXT(v->extra.binding);
}

inline static REBARR *VAL_VARARGS_ARRAY1(const RELVAL *v) {
    assert(!GET_ARR_FLAG(v->extra.binding, ARRAY_FLAG_VARLIST));
    return v->extra.binding;
}


// The subfeed is either the varlist of the frame of another varargs that is
// being chained at the moment, or the `array1` of another varargs.  To
// be visible for all instances of the same vararg, it can't live in the
// payload bits--so it's in the `special` slot of a frame or the misc slot
// of the array1.
//
inline static REBOOL Is_End_Subfeed_Addr_Of_Feed(
    REBARR ***addr_out,
    REBARR *a
) {
    if (!GET_ARR_FLAG(a, ARRAY_FLAG_VARLIST)) {
        *addr_out = &ARR_SERIES(a)->link.subfeed;
        return LOGICAL(*addr_out == NULL);
    }

    REBFRM *f = CTX_FRAME(AS_CONTEXT(a));
    assert(f != NULL); // need to check frame independently and error on this

    // Be cautious with the strict aliasing implications of this conversion.
    //
    *addr_out = cast(REBARR**, &f->special);

    if (f->special->header.bits & NOT_END_MASK)
        return FALSE;

    return TRUE;
}

inline static void Mark_End_Subfeed_Addr_Of_Feed(REBARR *a) {
    if (!GET_ARR_FLAG(a, ARRAY_FLAG_VARLIST)) {
        ARR_SERIES(a)->link.subfeed = NULL;
        return;
    }

    REBFRM *f = CTX_FRAME(AS_CONTEXT(a));
    assert(f != NULL); // need to check frame independently and error on this
    f->special = c_cast(REBVAL*, END_CELL);
}
