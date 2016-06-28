//
//  File: %c-value.c
//  Summary: "Generic REBVAL Support Services and Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016 Rebol Open Source Contributors
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
// These are mostly DEBUG-build routines to support the macros and definitions
// in %sys-value.h.
//
// These are not specific to any given type.  For the type-specific REBVAL
// code, see files with names like %t-word.c, %t-logic.c, %t-integer.c...
//

#include "sys-core.h"


#if !defined(NDEBUG)

//
//  Panic_Value_Debug: C
//
// This is a debug-only "error generator", which will hunt through all the
// series allocations and panic on the series that contains the value (if
// it can find it).  This will allow those using Address Sanitizer or
// Valgrind to know a bit more about where the value came from.
//
// Additionally, if it happens to be a void or trash, LOGIC!, BAR!, or NONE!
// it will dump out where the initialization happened if that information
// was stored.
//
ATTRIBUTE_NO_RETURN void Panic_Value_Debug(
    const RELVAL *value,
    const char *file,
    int line
) {
    REBSER *containing = Try_Find_Containing_Series_Debug(value);

    printf("PANIC VALUE called from %s:%d\n", file, line);
    fflush(stdout);

    switch (value->header.bits & HEADER_TYPE_MASK) {
    case REB_0:
    case REB_BLANK:
    case REB_LOGIC:
    case REB_BAR:
        printf(
            "REBVAL init on tick #%d at %s:%d\n",
            cast(unsigned int, value->extra.do_count),
            value->payload.track.filename,
            value->payload.track.line
        );
        fflush(stdout);
        break;
    }

    printf("Kind=%d\n", cast(int, value->header.bits & HEADER_TYPE_MASK));
    fflush(stdout);

    if (containing) {
        printf("Containing series for value pointer found, panicking it:\n");
        fflush(stdout);
        Panic_Series(containing);
    }

    printf("No containing series for value...panicking to make stack dump:\n");
    fflush(stdout);
    Panic_Array(EMPTY_ARRAY);
}


//
// Assert_Flags_Are_For_Value: C
//
void Assert_Flags_Are_For_Value(const RELVAL *v, REBUPT f) {
    if ((f & HEADER_TYPE_MASK) == REB_0)
        return; // flag applies to any value (or trash)

    if ((f & HEADER_TYPE_MASK) == REB_FUNCTION) {
        assert(IS_FUNCTION(v));
    }
    else if ((f & HEADER_TYPE_MASK) == REB_OBJECT) {
        assert(ANY_CONTEXT(v));
    }
    else if ((f & HEADER_TYPE_MASK) == REB_WORD) {
        assert(ANY_WORD(v));
    }
}


#if defined(__cplusplus)

//
//  Assert_Cell_Writable: C
//
// The check helps avoid very bad catastrophies that might ensue if "implicit
// end markers" could be overwritten.  These are the ENDs that are actually
// pointers doing double duty inside a data structure, and there is no REBVAL
// storage backing the position.
//
// (A fringe benefit is catching writes to other unanticipated locations.)
//
void Assert_Cell_Writable(const RELVAL *v, const char *file, int line)
{
    // REBVALs should not be written at addresses that do not match the
    // alignment of the processor.  This checks modulo the size of an unsigned
    // integer the same size as a platform pointer (REBUPT => uintptr_t)
    //
    assert(cast(REBUPT, (v)) % sizeof(REBUPT) == 0);

    if (NOT((v)->header.bits & CELL_MASK)) {
        printf("Non-cell passed to writing routine\n");
        fflush(stdout);
        Panic_Value_Debug(v, file, line);
    }
    if (NOT((v)->header.bits & VALUE_FLAG_WRITABLE_DEBUG)) {
        printf("Non-writable value passed to writing routine\n");
        fflush(stdout);
        Panic_Value_Debug(v, file, line);
    }
}

#endif


//
//  SET_END_Debug: C
//
// Uses REB_MAX instead of just REB_0 for the type, to help cue debugging.
//
// When SET_END is used, it uses the whole cell.  Implicit termination is
// done by the raw creation of a Reb_Value_Header in the containing structure.
//
void SET_END_Debug(RELVAL *v, const char *file, int line) {
    ASSERT_CELL_WRITABLE_IF_DEBUG(v, file, line);
    (v)->header.bits = REB_MAX | CELL_MASK;
    MARK_CELL_WRITABLE_IF_DEBUG(v);
    Set_Track_Payload_Debug(v, file, line);
}


//
//  IS_END_Debug: C
//
REBOOL IS_END_Debug(const RELVAL *v, const char *file, int line) {
#ifdef __cplusplus
    if (
        (v->header.bits & CELL_MASK)
        //
        // Note: a non-writable value could have any bit pattern in the
        // type slot, so we only check for trash in writable ones.
        //
        && (v->header.bits & HEADER_TYPE_MASK) == REB_0
        && NOT(v->header.bits & VOID_FLAG_NOT_TRASH)
        && NOT(v->header.bits & VOID_FLAG_SAFE_TRASH)
    ) {
        printf("IS_END() called on value marked as TRASH\n");
        fflush(stdout);
        Panic_Value_Debug(v, file, line);
    }
#endif
    return IS_END_MACRO(v);
}


//
//  VAL_SPECIFIC_Debug: C
//
REBCTX *VAL_SPECIFIC_Debug(const REBVAL *v)
{
    REBCTX *specific;

    assert(NOT(GET_VAL_FLAG(v, VALUE_FLAG_RELATIVE)));
    assert(
        ANY_WORD(v)
        || ANY_ARRAY(v)
        || IS_VARARGS(v)
        || IS_FUNCTION(v)
        || ANY_CONTEXT(v)
    );

    specific = VAL_SPECIFIC_COMMON(v);

    if (specific != SPECIFIED) {
        //
        // Basic sanity check: make sure it's a context at all
        //
        if (!GET_CTX_FLAG(specific, ARRAY_FLAG_CONTEXT_VARLIST)) {
            printf("Non-CONTEXT found as specifier in specific value\n");
            Panic_Series(cast(REBSER*, specific)); // may not be series either
        }

        // While an ANY-WORD! can be bound specifically to an arbitrary
        // object, an ANY-ARRAY! only becomes bound specifically to frames.
        // The keylist for a frame's context should come from a function's
        // paramlist, which should have a FUNCTION! value in keylist[0]
        //
        if (ANY_ARRAY(v))
            assert(IS_FUNCTION(CTX_ROOTKEY(specific)));
    }

    return specific;
}


//
//  INIT_WORD_INDEX_Debug: C
//
void INIT_WORD_INDEX_Debug(RELVAL *v, REBCNT i)
{
    assert(ANY_WORD(v));
    assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND));
    if (IS_RELATIVE(v))
        assert(
            SAME_STR(VAL_WORD_CASED(v), FUNC_PARAM_CASED(VAL_WORD_FUNC(v), i))
        );
    else
        assert(SAME_STR(
            VAL_WORD_CASED(v), CTX_KEY_CASED(VAL_WORD_CONTEXT(KNOWN(v)), i))
        );
    (v)->payload.any_word.index = (i);
}


//
//  IS_RELATIVE_Debug: C
//
// One should only be testing relvals for their relativeness or specificness,
// because all REBVAL* should be guaranteed to be speciic!
//
REBOOL IS_RELATIVE_Debug(const RELVAL *value)
{
    return GET_VAL_FLAG(value, VALUE_FLAG_RELATIVE);
}


//
//  Assert_No_Relative: C
//
// Check to make sure there are no relative values in an array, maybe deeply.
//
// !!! What if you have an ANY-ARRAY! inside your array at a position N,
// but there is a relative value in the VAL_ARRAY() of that value at an
// index earlier than N?  This currently considers that an error since it
// checks the whole array...which is more conservative (asserts on more
// cases).  But should there be a flag to ask to honor the index?
//
void Assert_No_Relative(REBARR *array, REBOOL deep)
{
    RELVAL *item = ARR_HEAD(array);
    while (NOT_END(item)) {
        if (IS_RELATIVE(item)) {
            Debug_Fmt("Array contained relative item and wasn't supposed to.");
            PROBE_MSG(item, "relative item");
            Panic_Array(array);
        }
        if (!IS_VOID_OR_SAFE_TRASH(item) && ANY_ARRAY(item) && deep)
             Assert_No_Relative(VAL_ARRAY(item), deep);
        ++item;
    }
}


//
//  COPY_VALUE_Debug: C
//
// The implementation of COPY_VALUE_CORE is designed to be fairly optimal
// (since it is being called in lieu of what would have been a memcpy() or
// plain assignment).  It is left in its raw form as an inline function to
// to help convey that it is nearly as efficient as an assignment.
//
// This adds some verbose checking in the debug build to help debug cases
// where the relative information bits are incorrect.
//
void COPY_VALUE_Debug(
    REBVAL *dest,
    const RELVAL *src,
    REBCTX *specifier
) {
    assert(!IS_END(src));
    assert(!IS_TRASH_DEBUG(src));

#ifdef __cplusplus
    Assert_Cell_Writable(dest, __FILE__, __LINE__);
#endif

    if (IS_RELATIVE(src)) {
        assert(ANY_WORD(src) || ANY_ARRAY(src));
        if (specifier == SPECIFIED) {
            Debug_Fmt("Internal Error: Relative item used with SPECIFIED");
            PROBE_MSG(src, "word or array");
            PROBE_MSG(FUNC_VALUE(VAL_RELATIVE(src)), "func");
            assert(FALSE);
        }
        else if (
            VAL_RELATIVE(src)
            != VAL_FUNC(CTX_FRAME_FUNC_VALUE(specifier))
        ) {
            Debug_Fmt("Internal Error: Function mismatch in specific binding");
            PROBE_MSG(src, "word or array");
            PROBE_MSG(FUNC_VALUE(VAL_RELATIVE(src)), "expected func");
            PROBE_MSG(CTX_FRAME_FUNC_VALUE(specifier), "actual func");
            assert(FALSE);
        }
    }
    COPY_VALUE_CORE(dest, src, specifier);
}


//
//  Probe_Core_Debug: C
//
void Probe_Core_Debug(
    const char *msg,
    const char *file,
    int line,
    const RELVAL *val
) {
    if (msg)
        printf("\n** PROBE_MSG(\"%s\") ", msg);
    else
        printf("\n** PROBE() ");

    printf("tick %d %s:%d\n", cast(int, TG_Do_Count), file, line);

    fflush(stdout);

    Debug_Fmt("%r\n", val);
}

#endif
