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
        if (VAL_INT64(val) > INT32_MAX or VAL_INT64(val) < INT32_MIN)
            fail (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }
    else if (IS_DECIMAL(val) or IS_PERCENT(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
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
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        return cast(REBINT, VAL_DECIMAL(val));
    }

    assert(IS_INTEGER(val));

    if (VAL_INT64(val) > INT32_MAX or VAL_INT64(val) < INT32_MIN)
        goto out_of_range;

    return VAL_INT32(val);

out_of_range:
    fail (Error_Out_Of_Range(KNOWN(val)));
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
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else {
        assert(IS_INTEGER(val));

        if (VAL_INT64(val) > INT32_MAX)
            goto out_of_range;

        n = VAL_INT32(val);
    }

    // More efficient to use positive sense:
    if (
        (sign == 0 and n >= 0)
        or (sign > 0 and n > 0)
        or (sign < 0 and n < 0)
    ){
        return n;
    }

out_of_range:
    fail (Error_Out_Of_Range(KNOWN(val)));
}


//
//  Int64: C
//
REBI64 Int64(const REBVAL *val)
{
    if (IS_INTEGER(val))
        return VAL_INT64(val);
    if (IS_DECIMAL(val) or IS_PERCENT(val))
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
    if (IS_DECIMAL(val) or IS_PERCENT(val))
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
        if (VAL_DECIMAL(val) > INT64_MAX or VAL_DECIMAL(val) < INT64_MIN)
            fail (Error_Out_Of_Range(val));

        n = cast(REBI64, VAL_DECIMAL(val));
    }
    else
        n = VAL_INT64(val);

    // More efficient to use positive sense:
    if (
        (sign == 0 and n >= 0)
        or (sign > 0 and n > 0)
        or (sign < 0 and n < 0)
    ){
        return n;
    }

    fail (Error_Out_Of_Range(val));
}


//
//  Datatype_From_Kind: C
//
// Returns the specified datatype value from the system context.
// The datatypes are all at the head of the context.
//
const REBVAL *Datatype_From_Kind(enum Reb_Kind kind)
{
    assert(kind > REB_0 and kind < REB_MAX);
    REBVAL *type = CTX_VAR(Lib_Context, SYM_FROM_KIND(kind));
    assert(IS_DATATYPE(type));
    return type;
}


//
//  Init_Datatype: C
//
REBVAL *Init_Datatype(RELVAL *out, enum Reb_Kind kind)
{
    assert(kind > REB_0 and kind < REB_MAX);
    Move_Value(out, Datatype_From_Kind(kind));
    return KNOWN(out);
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
    REBSER *s,
    REBCNT index,
    REBNOD *binding
) {
    ENSURE_SERIES_MANAGED(s);

    if (type != REB_IMAGE and type != REB_VECTOR) {
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

        ASSERT_SERIES_TERM(s); // doesn't apply to image/vector
    }

    RESET_CELL(out, type);
    out->payload.any_series.series = s;
    VAL_INDEX(out) = index;
    INIT_BINDING(out, binding);

  #if !defined(NDEBUG)
    if (ANY_STRING_KIND(type)) {
        if (SER_WIDE(s) != 2)
            panic (s);
    }
    else if (type == REB_BINARY) {
        if (SER_WIDE(s) != 1)
            panic (s);
    }
    else if (ANY_PATH_KIND(type)) {
        if (ARR_LEN(ARR(s)) < 2)
            panic (s);
    }
  #endif

    return KNOWN(out);
}


//
//  Set_Tuple: C
//
void Set_Tuple(REBVAL *value, REBYTE *bytes, REBCNT len)
{
    REBYTE *bp;

    RESET_CELL(value, REB_TUPLE);
    VAL_TUPLE_LEN(value) = (REBYTE)len;
    for (bp = VAL_TUPLE(value); len > 0; len--)
        *bp++ = *bytes++;
}


#if !defined(NDEBUG)

//
//  Extra_Init_Any_Context_Checks_Debug: C
//
// !!! Overlaps with ASSERT_CONTEXT, review folding them together.
//
void Extra_Init_Any_Context_Checks_Debug(enum Reb_Kind kind, REBCTX *c) {
    assert(ALL_SER_FLAGS(c, SERIES_MASK_CONTEXT));

    REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_CONTEXT(archetype) == c);
    assert(CTX_TYPE(c) == kind);

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(VAL_BINDING(archetype) == UNBOUND or CTX_TYPE(c) == REB_FRAME);

    REBARR *varlist = CTX_VARLIST(c);
    REBARR *keylist = CTX_KEYLIST(c);
    assert(NOT_SER_FLAG(keylist, ARRAY_FLAG_FILE_LINE));

    assert(
        not MISC(varlist).meta
        or ANY_CONTEXT(CTX_ARCHETYPE(MISC(varlist).meta)) // current rule
    );

    // FRAME!s must always fill in the phase slot, but that piece of the
    // REBVAL is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
    if (CTX_TYPE(c) == REB_FRAME) {
        assert(IS_ACTION(CTX_ROOTKEY(c)));
        assert(archetype->payload.any_context.phase);
    }
    else {
      #ifdef DEBUG_UNREADABLE_BLANKS
        assert(IS_UNREADABLE_DEBUG(CTX_ROOTKEY(c)));
      #endif
        assert(not archetype->payload.any_context.phase);
    }

    // Keylists are uniformly managed, or certain routines would return
    // "sometimes managed, sometimes not" keylists...a bad invariant.
    //
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(c));
}


//
//  Extra_Init_Action_Checks_Debug: C
//
// !!! Overlaps with ASSERT_ACTION, review folding them together.
//
void Extra_Init_Action_Checks_Debug(REBACT *a) {
    assert(ALL_SER_FLAGS(a, SERIES_MASK_ACTION));

    REBVAL *archetype = ACT_ARCHETYPE(a);
    assert(VAL_ACTION(archetype) == a);

    REBARR *paramlist = ACT_PARAMLIST(a);
    assert(NOT_SER_FLAG(paramlist, ARRAY_FLAG_FILE_LINE));

    // !!! Currently only a context can serve as the "meta" information,
    // though the interface may expand.
    //
    assert(
        MISC(paramlist).meta == NULL
        or ANY_CONTEXT(CTX_ARCHETYPE(MISC(paramlist).meta))
    );
}

#endif


//
//  Part_Len_Core: C
//
// When an ACTION! that takes a series also takes a /PART argument, this
// determines if the position for the part is before or after the series
// position.  If it is before (e.g. a negative integer limit was passed in,
// or a prior position) the series value will be updated to the earlier
// position, so that a positive length for the partial region is returned.
//
static REBCNT Part_Len_Core(
    REBVAL *series, // this is the series whose index may be modified
    const REBVAL *limit // /PART (number, position in value, or NULLED cell)
){
    if (IS_NULLED(limit)) // limit is nulled when /PART refinement unused
        return VAL_LEN_AT(series); // leave index alone, use plain length

    REBI64 len;
    if (IS_INTEGER(limit) or IS_DECIMAL(limit))
        len = Int32(limit); // may be positive or negative
    else {
        assert(ANY_SERIES(limit)); // must be same series (same series, even)
        if (
            VAL_TYPE(series) != VAL_TYPE(limit) // !!! should AS be tolerated?
            or VAL_SERIES(series) != VAL_SERIES(limit)
        ){
            fail (Error_Invalid_Part_Raw(limit));
        }

        len = cast(REBINT, VAL_INDEX(limit)) - cast(REBINT, VAL_INDEX(series));
    }

    // Restrict length to the size available
    //
    if (len >= 0) {
        REBINT maxlen = cast(REBINT, VAL_LEN_AT(series));
        if (len > maxlen)
            len = maxlen;
    }
    else {
        len = -len;
        if (len > cast(REBINT, VAL_INDEX(series)))
            len = cast(REBINT, VAL_INDEX(series));
        VAL_INDEX(series) -= cast(REBCNT, len);
    }

    if (len > UINT32_MAX) {
        //
        // Tests had `[1] = copy/part tail [1] -2147483648`, where trying to
        // do `len = -len` couldn't make a positive 32-bit version of that
        // negative value.  For now, use REBI64 to do the calculation.
        //
        fail ("Length out of range for /PART refinement");
    }

    assert(len >= 0);
    assert(VAL_LEN_HEAD(series) >= cast(REBCNT, len));
    return cast(REBCNT, len);
}


//
//  Part_Len_May_Modify_Index: C
//
// This is the common way of normalizing a series with a position against a
// /PART limit, so that the series index points to the beginning of the
// subsetted range and gives back a length to the end of that subset.
//
REBCNT Part_Len_May_Modify_Index(REBVAL *series, const REBVAL *limit) {
    assert(ANY_SERIES(series) or ANY_PATH(series));
    return Part_Len_Core(series, limit);
}


//
//  Part_Tail_May_Modify_Index: C
//
// Simple variation that instead of returning the length, returns the absolute
// tail position in the series of the partial sequence.
//
REBCNT Part_Tail_May_Modify_Index(REBVAL *series, const REBVAL *limit)
{
    REBCNT len = Part_Len_May_Modify_Index(series, limit);
    return len + VAL_INDEX(series); // uses the possibly-updated index
}


//
//  Part_Len_Append_Insert_May_Modify_Index: C
//
// This is for the specific cases of INSERT and APPEND interacting with /PART:
//
// https://github.com/rebol/rebol-issues/issues/2096
//
// It captures behavior that in R3-Alpha was done in "Partial1()", as opposed
// to the "Partial()" routine...which allows for the use of an integer
// length limit even when the change argument is not a series.
//
// Note: the calculation for CHANGE is done based on the series being changed,
// not the properties of the argument:
//
// https://github.com/rebol/rebol-issues/issues/1570
//
REBCNT Part_Len_Append_Insert_May_Modify_Index(
    REBVAL *value,
    const REBVAL *limit
){
    if (ANY_SERIES(value))
        return Part_Len_Core(value, limit);

    if (IS_NULLED(limit))
        return 1;

    if (IS_INTEGER(limit) or IS_DECIMAL(limit))
        return Part_Len_Core(value, limit);

    fail ("Invalid /PART specified for non-series APPEND/INSERT argument");
}


//
//  Add_Max: C
//
int64_t Add_Max(enum Reb_Kind kind_or_0, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n + m;
    if (r < -maxi or r > maxi) {
        if (kind_or_0 != REB_0)
            fail (Error_Type_Limit_Raw(Datatype_From_Kind(kind_or_0)));
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
    if (r < -maxi or r > maxi)
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(type)));
    return cast(int, r); // !!! (?) review this cast
}

