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


//
//  Get_Num_From_Arg: C
//
// Get the amount to skip or pick.
// Allow multiple types. Throw error if not valid.
// Note that the result is one-based.
//
REBINT Get_Num_From_Arg(const Value* val)
{
    REBINT n;

    if (Is_Integer(val)) {
        if (VAL_INT64(val) > INT32_MAX or VAL_INT64(val) < INT32_MIN)
            fail (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }
    else if (Is_Decimal(val) or Is_Percent(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            fail (Error_Out_Of_Range(val));
        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else if (Is_Logic(val))
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
        DECLARE_VALUE (temp);
        Init_Decimal(temp, f);

        fail (Error_Out_Of_Range(temp));
    }
    return cast(REBINT, f);
}


//
//  Int32: C
//
REBINT Int32(const Cell* val)
{
    if (Is_Decimal(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        return cast(REBINT, VAL_DECIMAL(val));
    }

    assert(Is_Integer(val));

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
REBINT Int32s(const Cell* val, REBINT sign)
{
    REBINT n;

    if (Is_Decimal(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            goto out_of_range;

        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else {
        assert(Is_Integer(val));

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
REBI64 Int64(const Value* val)
{
    if (Is_Integer(val))
        return VAL_INT64(val);
    if (Is_Decimal(val) or Is_Percent(val))
        return cast(REBI64, VAL_DECIMAL(val));

    fail (Error_Invalid(val));
}


//
//  Dec64: C
//
REBDEC Dec64(const Value* val)
{
    if (Is_Decimal(val) or Is_Percent(val))
        return VAL_DECIMAL(val);
    if (Is_Integer(val))
        return cast(REBDEC, VAL_INT64(val));

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
REBI64 Int64s(const Value* val, REBINT sign)
{
    REBI64 n;
    if (Is_Decimal(val)) {
        if (
            VAL_DECIMAL(val) > cast(REBDEC, INT64_MAX)
            or VAL_DECIMAL(val) < cast(REBDEC, INT64_MIN)
        ){
            fail (Error_Out_Of_Range(val));
        }
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
const Value* Datatype_From_Kind(enum Reb_Kind kind)
{
    assert(kind > REB_0 and kind < REB_MAX);
    Value* type = Varlist_Slot(Lib_Context, SYM_FROM_KIND(kind));
    assert(Is_Datatype(type));
    return type;
}


//
//  Init_Datatype: C
//
Value* Init_Datatype(Cell* out, enum Reb_Kind kind)
{
    assert(kind > REB_0 and kind < REB_MAX);
    Copy_Cell(out, Datatype_From_Kind(kind));
    return KNOWN(out);
}


//
//  Datatype_Of: C
//
// Returns the datatype value for the given value.
// The datatypes are all at the head of the context.
//
Value* Datatype_Of(const Cell* value)
{
    return Varlist_Slot(Lib_Context, SYM_FROM_KIND(Type_Of(value)));
}


//
//  Get_System: C
//
// Return a second level object field of the system object.
//
Value* Get_System(REBLEN i1, REBLEN i2)
{
    Value* obj;

    obj = Varlist_Slot(Cell_Varlist(Root_System), i1);
    if (i2 == 0) return obj;
    assert(Is_Object(obj));
    return Varlist_Slot(Cell_Varlist(obj), i2);
}


//
//  Get_System_Int: C
//
// Get an integer from system object.
//
REBINT Get_System_Int(REBLEN i1, REBLEN i2, REBINT default_int)
{
    Value* val = Get_System(i1, i2);
    if (Is_Integer(val)) return VAL_INT32(val);
    return default_int;
}


//
//  Init_Any_Series_At_Core: C
//
// Common function.
//
Value* Init_Any_Series_At_Core(
    Cell* out, // allows Cell slot as input, but will be filled w/Value
    enum Reb_Kind type,
    Flex* series,
    REBLEN index,
    Stub* binding
) {
    Force_Flex_Managed(series);

    // !!! Binaries are zero-terminated in modern Ren-C, so they can alias
    // as TEXT! if they are valid UTF-8.  That is not possible in this older
    // branch where strings are Ucs2(*).  But note the original Make_Binary()
    // comment from the open source release read:
    //
    //     Make a binary string series. For byte, C, and UTF8 strings.
    //     Add 1 extra for terminator.
    //
    Assert_Flex_Term(series);

    RESET_CELL(out, type);
    out->payload.any_series.series = series;
    VAL_INDEX(out) = index;
    INIT_BINDING(out, binding);

    if (Any_Path_Kind(type)) {
        if (Cell_Series_Len_At(out) < 2)
            fail ("ANY-PATH! must have at least 2 elements");
    }

  #if RUNTIME_CHECKS
    if (Any_String(out)) {
        if (Flex_Wide(series) != 2)
            panic(series);
    } else if (Is_Binary(out)) {
        if (Flex_Wide(series) != 1)
            panic(series);
    }
  #endif

    return KNOWN(out);
}


//
//  Set_Tuple: C
//
void Set_Tuple(Value* value, Byte *bytes, REBLEN len)
{
    Byte *bp;

    RESET_CELL(value, REB_TUPLE);
    VAL_TUPLE_LEN(value) = (Byte)len;
    for (bp = VAL_TUPLE(value); len > 0; len--)
        *bp++ = *bytes++;
}


#if RUNTIME_CHECKS

//
//  Extra_Init_Any_Context_Checks_Debug: C
//
// !!! Overlaps with ASSERT_CONTEXT, review folding them together.
//
void Extra_Init_Any_Context_Checks_Debug(enum Reb_Kind kind, VarList* c) {
    assert(Varlist_Array(c)->leader.bits & SERIES_MASK_CONTEXT);

    Value* archetype = Varlist_Archetype(c);
    assert(Cell_Varlist(archetype) == c);
    assert(CTX_TYPE(c) == kind);

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(VAL_BINDING(archetype) == UNBOUND or CTX_TYPE(c) == REB_FRAME);

    Array* varlist = Varlist_Array(c);
    Array* keylist = Keylist_Of_Varlist(c);
    assert(Not_Array_Flag(keylist, HAS_FILE_LINE));

    assert(
        not MISC(varlist).meta
        or Any_Context(Varlist_Archetype(MISC(varlist).meta)) // current rule
    );

    // FRAME!s must always fill in the phase slot, but that piece of the
    // cell is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
    if (CTX_TYPE(c) == REB_FRAME) {
        assert(Is_Action(CTX_ROOTKEY(c)));
        assert(archetype->payload.any_context.phase);
    }
    else {
        assert(Is_Cell_Unreadable(CTX_ROOTKEY(c)));
        assert(not archetype->payload.any_context.phase);
    }

    // Keylists are uniformly managed, or certain routines would return
    // "sometimes managed, sometimes not" keylists...a bad invariant.
    //
    Assert_Flex_Managed(Keylist_Of_Varlist(c));
}


//
//  Extra_Init_Action_Checks_Debug: C
//
// !!! Overlaps with ASSERT_ACTION, review folding them together.
//
void Extra_Init_Action_Checks_Debug(REBACT *a) {
    assert(ACT_PARAMLIST(a)->leader.bits & SERIES_MASK_ACTION);

    Value* archetype = ACT_ARCHETYPE(a);
    assert(VAL_ACTION(archetype) == a);

    Array* paramlist = ACT_PARAMLIST(a);
    assert(Not_Array_Flag(paramlist, HAS_FILE_LINE));

    // !!! Currently only a context can serve as the "meta" information,
    // though the interface may expand.
    //
    assert(
        MISC(paramlist).meta == nullptr
        or Any_Context(Varlist_Archetype(MISC(paramlist).meta))
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
static REBLEN Part_Len_Core(
    Value* series, // this is the series whose index may be modified
    const Value* limit // /PART (number, position in value, or NULLED cell)
){
    if (Is_Nulled(limit)) // limit is nulled when /PART refinement unused
        return Cell_Series_Len_At(series); // leave index alone, use plain length

    REBI64 len;
    if (Is_Integer(limit) or Is_Decimal(limit))
        len = Int32(limit); // may be positive or negative
    else {
        assert(Any_Series(limit)); // must be same series (same series, even)
        if (
            Type_Of(series) != Type_Of(limit) // !!! should AS be tolerated?
            or Cell_Flex(series) != Cell_Flex(limit)
        ){
            fail (Error_Invalid_Part_Raw(limit));
        }

        len = cast(REBINT, VAL_INDEX(limit)) - cast(REBINT, VAL_INDEX(series));
    }

    // Restrict length to the size available
    //
    if (len >= 0) {
        REBINT maxlen = cast(REBINT, Cell_Series_Len_At(series));
        if (len > maxlen)
            len = maxlen;
    }
    else {
        len = -len;
        if (len > cast(REBINT, VAL_INDEX(series)))
            len = cast(REBINT, VAL_INDEX(series));
        VAL_INDEX(series) -= cast(REBLEN, len);
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
    assert(VAL_LEN_HEAD(series) >= cast(REBLEN, len));
    return cast(REBLEN, len);
}


//
//  Part_Len_May_Modify_Index: C
//
// This is the common way of normalizing a series with a position against a
// /PART limit, so that the series index points to the beginning of the
// subsetted range and gives back a length to the end of that subset.
//
REBLEN Part_Len_May_Modify_Index(Value* series, const Value* limit) {
    assert(Any_Series(series));
    return Part_Len_Core(series, limit);
}


//
//  Part_Tail_May_Modify_Index: C
//
// Simple variation that instead of returning the length, returns the absolute
// tail position in the series of the partial sequence.
//
REBLEN Part_Tail_May_Modify_Index(Value* series, const Value* limit)
{
    REBLEN len = Part_Len_May_Modify_Index(series, limit);
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
REBLEN Part_Len_Append_Insert_May_Modify_Index(
    Value* value,
    const Value* limit
){
    if (Any_Series(value))
        return Part_Len_Core(value, limit);

    if (Is_Nulled(limit))
        return 1;

    if (Is_Integer(limit) or Is_Decimal(limit))
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
