//
//  File: %f-stubs.c
//  Summary: "miscellaneous little functions"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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

#include "sys-core.h"
#include "sys-deci-funcs.h"


//
//  Get_Num_From_Arg: C
//
// Get the amount to skip or pick.
// Allow multiple types. Throw error if not valid.
// Note that the result is one-based.
//
REBINT Get_Num_From_Arg(const REBVAL *val)
{
    REBINT n;

    if (IS_INTEGER(val)) {
        if (VAL_INT64(val) > INT32_MAX || VAL_INT64(val) < INT32_MIN)
            fail (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }
    else if (IS_DECIMAL(val) || IS_PERCENT(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX || VAL_DECIMAL(val) < INT32_MIN)
            fail (Error_Out_Of_Range(val));
        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else if (IS_LOGIC(val))
        n = (VAL_LOGIC(val) ? 1 : 2);
    else
        fail (Error_Invalid(val));

    return n;
}


//
//  Float_Int16: C
//
REBINT Float_Int16(REBD32 f)
{
    if (fabs(f) > cast(REBD32, 0x7FFF)) {
        DECLARE_LOCAL (temp);
        Init_Decimal(temp, f);

        fail (Error_Out_Of_Range(temp));
    }
    return cast(REBINT, f);
}


//
//  Int32: C
//
REBINT Int32(const RELVAL *val)
{
    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX || VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        return cast(REBINT, VAL_DECIMAL(val));
    }

    assert(IS_INTEGER(val));

    if (VAL_INT64(val) > INT32_MAX || VAL_INT64(val) < INT32_MIN)
        goto out_of_range;

    return VAL_INT32(val);

out_of_range:
    fail (Error_Out_Of_Range(const_KNOWN(val)));
}


//
//  Int32s: C
//
// Get integer as positive, negative 32 bit value.
// Sign field can be
//     0: >= 0
//     1: >  0
//    -1: <  0
//
REBINT Int32s(const RELVAL *val, REBINT sign)
{
    REBINT n;

    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX || VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        n = cast(REBINT, VAL_DECIMAL(val));
    } else {
        assert(IS_INTEGER(val));

        if (VAL_INT64(val) > INT32_MAX)
            goto out_of_range;

        n = VAL_INT32(val);
    }

    // More efficient to use positive sense:
    if (
        (sign == 0 && n >= 0) ||
        (sign > 0 && n > 0) ||
        (sign < 0 && n < 0)
    )
        return n;

out_of_range:
    fail (Error_Out_Of_Range(const_KNOWN(val)));
}


//
//  Int64: C
//
REBI64 Int64(const REBVAL *val)
{
    if (IS_INTEGER(val))
        return VAL_INT64(val);
    if (IS_DECIMAL(val) || IS_PERCENT(val))
        return cast(REBI64, VAL_DECIMAL(val));
    if (IS_MONEY(val))
        return deci_to_int(VAL_MONEY_AMOUNT(val));

    fail (Error_Invalid(val));
}


//
//  Dec64: C
//
REBDEC Dec64(const REBVAL *val)
{
    if (IS_DECIMAL(val) || IS_PERCENT(val))
        return VAL_DECIMAL(val);
    if (IS_INTEGER(val))
        return cast(REBDEC, VAL_INT64(val));
    if (IS_MONEY(val))
        return deci_to_decimal(VAL_MONEY_AMOUNT(val));

    fail (Error_Invalid(val));
}


//
//  Int64s: C
//
// Get integer as positive, negative 64 bit value.
// Sign field can be
//     0: >= 0
//     1: >  0
//    -1: <  0
//
REBI64 Int64s(const REBVAL *val, REBINT sign)
{
    REBI64 n;

    if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) > INT64_MAX || VAL_DECIMAL(val) < INT64_MIN)
            fail (Error_Out_Of_Range(val));
        n = (REBI64)VAL_DECIMAL(val);
    } else {
        n = VAL_INT64(val);
    }

    // More efficient to use positive sense:
    if (
        (sign == 0 && n >= 0) ||
        (sign >  0 && n >  0) ||
        (sign <  0 && n <  0)
    )
        return n;

    fail (Error_Out_Of_Range(val));
}


//
//  Int8u: C
//
REBINT Int8u(const REBVAL *val)
{
    if (VAL_INT64(val) > 255 || VAL_INT64(val) < 0)
        fail (Error_Out_Of_Range(val));

    return VAL_INT32(val);
}


//
//  Val_Init_Datatype: C
//
void Val_Init_Datatype(REBVAL *out, enum Reb_Kind kind)
{
    assert(kind > REB_0 && kind < REB_MAX);
    Move_Value(out, CTX_VAR(Lib_Context, SYM_FROM_KIND(kind)));
}


//
//  Get_Type: C
//
// Returns the specified datatype value from the system context.
// The datatypes are all at the head of the context.
//
REBVAL *Get_Type(enum Reb_Kind kind)
{
    assert(kind > REB_0 && kind < REB_MAX);
    return CTX_VAR(Lib_Context, SYM_FROM_KIND(kind));
}


//
//  Type_Of: C
//
// Returns the datatype value for the given value.
// The datatypes are all at the head of the context.
//
REBVAL *Type_Of(const RELVAL *value)
{
    return CTX_VAR(Lib_Context, SYM_FROM_KIND(VAL_TYPE(value)));
}


//
//  In_Object: C
//
// Get value from nested list of objects. List is null terminated.
// Returns object value, else returns 0 if not found.
//
REBVAL *In_Object(REBCTX *base, ...)
{
    REBVAL *context = NULL;
    REBCNT n;
    va_list va;

    va_start(va, base);
    while ((n = va_arg(va, REBCNT))) {
        if (n > CTX_LEN(base)) {
            va_end(va);
            return NULL;
        }
        context = CTX_VAR(base, n);
        if (!ANY_CONTEXT(context)) {
            va_end(va);
            return NULL;
        }
        base = VAL_CONTEXT(context);
    }
    va_end(va);

    return context;
}


//
//  Get_System: C
//
// Return a second level object field of the system object.
//
REBVAL *Get_System(REBCNT i1, REBCNT i2)
{
    REBVAL *obj;

    obj = CTX_VAR(VAL_CONTEXT(Root_System), i1);
    if (i2 == 0) return obj;
    assert(IS_OBJECT(obj));
    return CTX_VAR(VAL_CONTEXT(obj), i2);
}


//
//  Get_System_Int: C
//
// Get an integer from system object.
//
REBINT Get_System_Int(REBCNT i1, REBCNT i2, REBINT default_int)
{
    REBVAL *val = Get_System(i1, i2);
    if (IS_INTEGER(val)) return VAL_INT32(val);
    return default_int;
}


//
//  Init_Any_Series_At_Core: C
//
// Common function.
//
REBVAL *Init_Any_Series_At_Core(
    RELVAL *out, // allows RELVAL slot as input, but will be filled w/REBVAL
    enum Reb_Kind type,
    REBSER *series,
    REBCNT index,
    REBNOD *binding
) {
    ENSURE_SERIES_MANAGED(series);

    if (type != REB_IMAGE && type != REB_VECTOR) {
        // Code in various places seemed to have different opinions of
        // whether a BINARY needed to be zero terminated.  It doesn't
        // make a lot of sense to zero terminate a binary unless it
        // simplifies the code assumptions somehow--it's in the class
        // "ANY_BINSTR()" so that suggests perhaps it has a bit more
        // obligation to conform.  Also, the original Make_Binary comment
        // from the open source release read:
        //
        //     Make a binary string series. For byte, C, and UTF8 strings.
        //     Add 1 extra for terminator.
        //
        // Until that is consciously overturned, check the REB_BINARY too

        ASSERT_SERIES_TERM(series); // doesn't apply to image/vector
    }

    VAL_RESET_HEADER(out, type);
    out->payload.any_series.series = series;
    VAL_INDEX(out) = index;
    INIT_BINDING(out, binding);

#if !defined(NDEBUG)
    if (GET_SER_FLAG(series, SERIES_FLAG_ARRAY) && binding == UNBOUND) {
        //
        // If UNBOUND is used for an array, then that top level of the
        // array cannot have any relative values in it.  Catch it here vs.
        // waiting until a later assertion.
        //
        ASSERT_NO_RELATIVE(ARR(series), FALSE);
    }
    else if (ANY_STRING(out))
        assert(SER_WIDE(series) == 1 || SER_WIDE(series) == 2);
#endif

    return KNOWN(out);
}


//
//  Set_Tuple: C
//
void Set_Tuple(REBVAL *value, REBYTE *bytes, REBCNT len)
{
    REBYTE *bp;

    VAL_RESET_HEADER(value, REB_TUPLE);
    VAL_TUPLE_LEN(value) = (REBYTE)len;
    for (bp = VAL_TUPLE(value); len > 0; len--)
        *bp++ = *bytes++;
}


//
//  Init_Any_Context_Core: C
//
// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT! REBVAL that
// is its canon form from a single pointer...the REBVAL sitting in the 0 slot
// of the context's varlist.
//
REBVAL *Init_Any_Context_Core(
    RELVAL *out, // allows RELVAL slot as input, but will be filled w/REBVAL
    enum Reb_Kind kind,
    REBCTX *c
){
  #if defined(NDEBUG)
    UNUSED(kind);
  #else
    //
    // In a debug build we check to make sure the type of the embedded value
    // matches the type of what is intended (so someone who thinks they are
    // initializing a REB_OBJECT from a CONTEXT does not accidentally get a
    // REB_ERROR, for instance.)  It's a point for several other integrity
    // checks as well.
    //
    REBVAL *archetype = CTX_VALUE(c);
    assert(VAL_CONTEXT(archetype) == c);

    assert(CTX_TYPE(c) == kind);

    REBARR *varlist = CTX_VARLIST(c);
    REBARR *keylist = CTX_KEYLIST(c);

    assert(GET_SER_FLAG(varlist, ARRAY_FLAG_VARLIST));

    assert(NOT_SER_FLAG(varlist, ARRAY_FLAG_FILE_LINE));
    assert(NOT_SER_FLAG(keylist, ARRAY_FLAG_FILE_LINE));

    if (IS_FRAME(CTX_VALUE(c)))
        assert(IS_FUNCTION(CTX_FRAME_FUNC_VALUE(c)));

    // !!! Currently only a context can serve as the "meta" information,
    // though the interface may expand.
    //
    assert(
        MISC(varlist).meta == NULL
        || ANY_CONTEXT(CTX_VALUE(MISC(varlist).meta))
    );
  #endif

    // Some contexts (stack frames in particular) start out unmanaged, and
    // then check to see if an operation like Init_Any_Context set them to
    // managed.  If not, they will free the context.  This avoids the need
    // for the garbage collector to have to deal with the series if there's
    // no reason too.
    //
    // Here is a case of where we mark the context as having an extant usage,
    // so that at minimum this value must become unreachable from the root GC
    // set before they are GC'd.  For another case, see INIT_WORD_CONTEXT(),
    // where an ANY-WORD! can mark a context as in use.
    //
    ENSURE_ARRAY_MANAGED(CTX_VARLIST(c));

    // Keylists are different, because they may-or-may-not-be-reused by some
    // operations.  There needs to be a uniform policy on their management,
    // or certain routines would return "sometimes managed, sometimes not"
    // keylist series...a bad invariant.
    //
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(c));

    Move_Value(out, CTX_VALUE(c));

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(VAL_BINDING(out) == UNBOUND || CTX_TYPE(c) == REB_FRAME);

    // FRAME!s must always fill in the phase slot, but that piece of the
    // REBVAL is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
#if !defined(NDEBUG)
    if (CTX_TYPE(c) == REB_FRAME)
        assert(out->payload.any_context.phase != NULL);
    else
        assert(out->payload.any_context.phase == NULL);
#endif

    return KNOWN(out);
}


//
//  Partial1: C
//
// Process the /PART (or /SKIP) and other length modifying arguments.
//
// Adjusts the value's index if necessary, and returns the length indicated.
// Hence if a negative limit is passed in, it will adjust value to the
// position that negative limit would seek to...and save the length of
// the span to get to the original index.
//
void Partial1(
    REBVAL *value, // Note: Might be modified, see above!
    const REBVAL *limit,
    REBCNT *span // 32-bit, see #853
){
    REBOOL is_series = ANY_SERIES(value);

    if (IS_VOID(limit)) { // use current length of the target value
        if (!is_series) {
            *span = 1;
        }
        else if (VAL_INDEX(value) >= VAL_LEN_HEAD(value)) {
            *span = 0;
        }
        else {
            *span = (VAL_LEN_HEAD(value) - VAL_INDEX(value));
        }
        return;
    }

    REBI64 len;
    if (IS_INTEGER(limit) || IS_DECIMAL(limit))
        len = Int32(limit); // will error if out of range; see #853
    else {
        if (
            !is_series
            || VAL_TYPE(value) != VAL_TYPE(limit)
            || VAL_SERIES(value) != VAL_SERIES(limit)
        ){
            fail (Error_Invalid_Part_Raw(limit));
        }

        len = cast(REBINT, VAL_INDEX(limit)) - cast(REBINT, VAL_INDEX(value));

    }

    if (is_series) {
        // Restrict length to the size available:
        if (len >= 0) {
            REBCNT maxlen = VAL_LEN_AT(value);
            if (len > cast(REBINT, maxlen))
                len = maxlen;
        }
        else {
            len = -len;
            if (len > cast(REBINT, VAL_INDEX(value)))
                len = VAL_INDEX(value);
            assert(len >= 0);
            VAL_INDEX(value) -= cast(REBCNT, len);
        }
    }

    assert(len >= 0);
    *span = cast(REBCNT, len);
}


//
//  Partial: C
//
// Args:
//     aval: target value
//     bval: argument to modify target (optional)
//     lval: length value (or blank)
//
// Determine the length of a /PART value. It can be:
//     1. integer or decimal
//     2. relative to A value (bval is null)
//     3. relative to B value
//
// NOTE: Can modify the value's index!
// The result can be negative. ???
//
REBINT Partial(REBVAL *aval, REBVAL *bval, REBVAL *lval)
{
    REBVAL *val;
    REBINT len;
    REBINT maxlen;

    // If lval is unset, use the current len of the target value:
    if (IS_VOID(lval)) {
        val = (bval && ANY_SERIES(bval)) ? bval : aval;
        if (VAL_INDEX(val) >= VAL_LEN_HEAD(val)) return 0;
        return (VAL_LEN_HEAD(val) - VAL_INDEX(val));
    }

    if (IS_INTEGER(lval) || IS_DECIMAL(lval)) {
        len = Int32(lval);
        val = bval;
    }
    else {
        // So, lval must be relative to aval or bval series:
        if (
            VAL_TYPE(aval) == VAL_TYPE(lval)
            && VAL_SERIES(aval) == VAL_SERIES(lval)
        ) {
            val = aval;
        }
        else if (
            bval
            && VAL_TYPE(bval) == VAL_TYPE(lval)
            && VAL_SERIES(bval) == VAL_SERIES(lval)
        ) {
            val = bval;
        }
        else
            fail (Error_Invalid_Part_Raw(lval));

        len = cast(REBINT, VAL_INDEX(lval)) - cast(REBINT, VAL_INDEX(val));
    }

    if (!val) val = aval;

    // Restrict length to the size available
    //
    if (len >= 0) {
        maxlen = (REBINT)VAL_LEN_AT(val);
        if (len > maxlen) len = maxlen;
    }
    else {
        len = -len;
        if (len > cast(REBINT, VAL_INDEX(val)))
            len = cast(REBINT, VAL_INDEX(val));
        VAL_INDEX(val) -= (REBCNT)len;
    }

    return len;
}


//
//  Clip_Int: C
//
int Clip_Int(int val, int mini, int maxi)
{
    if (val < mini) val = mini;
    else if (val > maxi) val = maxi;
    return val;
}


//
//  Add_Max: C
//
int64_t Add_Max(enum Reb_Kind type, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n + m;
    if (r < -maxi || r > maxi) {
        if (type != REB_0)
            fail (Error_Type_Limit_Raw(Get_Type(type)));
        r = r > 0 ? maxi : -maxi;
    }
    return r;
}


//
//  Mul_Max: C
//
int64_t Mul_Max(enum Reb_Kind type, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n * m;
    if (r < -maxi || r > maxi)
        fail (Error_Type_Limit_Raw(Get_Type(type)));
    return (int)r; // !!! (?) review this cast
}

