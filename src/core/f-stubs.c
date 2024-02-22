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
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

#include "cells/cell-money.h"


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
        n = (Cell_Logic(val) ? 1 : 2);
    else
        fail (val);

    return n;
}


//
//  Float_Int16: C
//
REBINT Float_Int16(REBD32 f)
{
    if (fabs(f) > cast(REBD32, 0x7FFF)) {
        DECLARE_ATOM (temp);
        Init_Decimal(temp, f);

        fail (Error_Out_Of_Range(temp));
    }
    return cast(REBINT, f);
}


//
//  Int32: C
//
REBINT Int32(const Value* val)
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
    fail (Error_Out_Of_Range(val));
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
REBINT Int32s(const Value* val, REBINT sign)
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
    fail (Error_Out_Of_Range(val));
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
    if (Is_Money(val))
        return deci_to_int(VAL_MONEY_AMOUNT(val));

    fail (val);
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
    if (Is_Money(val))
        return deci_to_decimal(VAL_MONEY_AMOUNT(val));

    fail (val);
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
const Value* Datatype_From_Kind(Kind kind)
{
    assert(kind < REB_MAX);
    Offset n = cast(Offset, kind);
    SymId datatype_sym = cast(SymId, REB_MAX + ((n - 1) * 2) + 1);
    const Value* type = Try_Lib_Var(datatype_sym);
    assert(Is_Type_Block(type));
    return type;
}


//
//  Type_Of: C
//
// Returns the datatype value for the given value.
// The datatypes are all at the head of the context.
//
const Value* Type_Of(const Atom* value)
{
    return Datatype_From_Kind(VAL_TYPE(value));
}


//
//  Get_System: C
//
// Return a second level object field of the system object.
//
Value* Get_System(REBLEN i1, REBLEN i2)
{
    Value* obj = CTX_VAR(VAL_CONTEXT(Lib(SYSTEM)), i1);
    if (i2 == 0)
        return obj;

    assert(Is_Object(obj));
    return CTX_VAR(VAL_CONTEXT(obj), i2);
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


#if !defined(NDEBUG)

//
//  Extra_Init_Context_Cell_Checks_Debug: C
//
// !!! Overlaps with Assert_Context, review folding them together.
//
void Extra_Init_Context_Cell_Checks_Debug(Kind kind, Context* c) {
    assert(
        (CTX_VARLIST(c)->leader.bits & SERIES_MASK_VARLIST)
        == SERIES_MASK_VARLIST
    );

    const Value* archetype = CTX_ARCHETYPE(c);
    assert(VAL_CONTEXT(archetype) == c);
    assert(CTX_TYPE(c) == kind);

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(
        BINDING(archetype) == UNBOUND
        or CTX_TYPE(c) == REB_FRAME
    );

    // KeyLists are uniformly managed, or certain routines would return
    // "sometimes managed, sometimes not" keylists...a bad invariant.
    //
    if (CTX_TYPE(c) != REB_MODULE) {  // keylist is global symbol table
        KeyList* keylist = CTX_KEYLIST(c);
        Assert_Series_Managed(keylist);
    }

    assert(not CTX_ADJUNCT(c) or Any_Context_Kind(CTX_TYPE(CTX_ADJUNCT(c))));

    // FRAME!s must always fill in the phase slot, but that piece of the
    // Cell is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
    Node* archetype_phase = VAL_FRAME_PHASE_OR_LABEL_NODE(archetype);
    if (CTX_TYPE(c) == REB_FRAME)
        assert(IS_DETAILS(cast(Array*, archetype_phase)));
    else
        assert(archetype_phase == nullptr);
}


//
//  Extra_Init_Frame_Details_Checks_Debug: C
//
// !!! Overlaps with ASSERT_ACTION, review folding them together.
//
void Extra_Init_Frame_Details_Checks_Debug(Phase* a) {
    Value* archetype = Phase_Archetype(a);

    // Once it was true that `VAL_ACTION(archetype) == a`.  That's no longer
    // true, but there might be some checks that apply regarding the two?
    //
    UNUSED(archetype);

    KeyList* keylist = ACT_KEYLIST(a);
    assert(
        (keylist->leader.bits & SERIES_MASK_KEYLIST)
        == SERIES_MASK_KEYLIST
    );

    // !!! Currently only a context can serve as the "meta" information,
    // though the interface may expand.
    //
    assert(not ACT_ADJUNCT(a) or Any_Context_Kind(CTX_TYPE(ACT_ADJUNCT(a))));
}

#endif


//
//  Part_Len_May_Modify_Index: C
//
// This is the common way of normalizing a series with a position against a
// /PART limit, so that the series index points to the beginning of the
// subsetted range and gives back a length to the end of that subset.
//
// It determines if the position for the part is before or after the series
// position.  If it is before (e.g. a negative integer limit was passed in,
// or a prior position) the series value will be updated to the earlier
// position, so that a positive length for the partial region is returned.
//
REBLEN Part_Len_May_Modify_Index(
    Value* series,  // ANY-SERIES? value whose index may be modified
    const Value* part  // /PART (number, position in value, or BLANK! cell)
){
    if (Any_Sequence(series)) {
        if (not Is_Nulled(part))
            fail ("/PART cannot be used with ANY-SEQUENCE");

        return Cell_Sequence_Len(series);
    }

    assert(Is_Issue(series) or Any_Series(series));

    if (Is_Nulled(part)) {  // indicates /PART refinement unused
        if (not Is_Issue(series))
            return Cell_Series_Len_At(series);  // leave index alone, use plain length

        Size size;
        Cell_Utf8_Size_At(&size, series);
        return size;
    }

    // VAL_INDEX() checks to make sure it's for in-bounds
    //
    REBLEN iseries = Is_Issue(series) ? 0 : VAL_INDEX(series);

    REBI64 len;
    if (Is_Integer(part) or Is_Decimal(part))
        len = Int32(part);  // may be positive or negative
    else {  // must be same series
        if (
            Is_Issue(part)
            or VAL_TYPE(series) != VAL_TYPE(part)  // !!! allow AS aliases?
            or Cell_Series(series) != Cell_Series(part)
        ){
            fail (Error_Invalid_Part_Raw(part));
        }

        len = cast(REBINT, VAL_INDEX(part)) - cast(REBINT, iseries);
    }

    // Restrict length to the size available
    //
    if (len >= 0) {
        REBINT maxlen = cast(REBINT, Cell_Series_Len_At(series));
        if (len > maxlen)
            len = maxlen;
    }
    else {
        if (Is_Issue(part))
            fail (Error_Invalid_Part_Raw(part));

        len = -len;
        if (len > cast(REBI64, iseries))
            len = iseries;
        VAL_INDEX_RAW(series) -= cast(REBLEN, len);
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
    if (not Is_Issue(series))
        assert(Cell_Series_Len_Head(series) >= cast(REBLEN, len));
    return cast(REBLEN, len);
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
//  Part_Limit_Append_Insert: C
//
// This is for the specific cases of INSERT and APPEND interacting with /PART,
// implementing a somewhat controversial behavior of only accepting an
// INTEGER! and only speaking in terms of units limited to:
//
// https://github.com/rebol/rebol-issues/issues/2096
// https://github.com/rebol/rebol-issues/issues/2383
//
// Note: the calculation for CHANGE is done based on the series being changed,
// not the properties of the argument:
//
// https://github.com/rebol/rebol-issues/issues/1570
//
REBLEN Part_Limit_Append_Insert(const Value* part) {
    if (Is_Nulled(part))
        return UINT32_MAX;  // treat as no limit

    if (Is_Integer(part)) {
        REBINT i = Int32(part);
        if (i < 0)  // Clip negative numbers to mean 0
            return 0;  // !!! Would it be better to error?
        return cast(REBLEN, i);
    }

    fail ("APPEND and INSERT only take /PART limit as INTEGER!");
}


//
//  Add_Max: C
//
int64_t Add_Max(Option(Heart) heart, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n + m;
    if (r < -maxi or r > maxi) {
        if (heart)
            fail (Error_Type_Limit_Raw(Datatype_From_Kind(unwrap(heart))));
        r = r > 0 ? maxi : -maxi;
    }
    return r;
}


//
//  Mul_Max: C
//
int64_t Mul_Max(Heart heart, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n * m;
    if (r < -maxi or r > maxi)
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(heart)));
    return cast(int, r); // !!! (?) review this cast
}


//
//  Setify: C
//
// Turn a value into its SET-XXX! equivalent, if possible.  This tries to
// "be smart" so even a TEXT! can be turned into a SET-WORD! (just an
// unbound one).
//
Value* Setify(Value* out) {  // called on stack values; can't call evaluator
    Heart heart = Cell_Heart(out);
    if (Any_Word_Kind(heart)) {
        HEART_BYTE(out) = REB_SET_WORD;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(out) = REB_SET_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(out) = REB_SET_TUPLE;
    }
    else if (Any_Block_Kind(heart)) {
        HEART_BYTE(out) = REB_SET_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(out) = REB_SET_GROUP;
    }
    else
        fail ("Cannot SETIFY");

    return out;
}


//
//  setify: native [
//
//  "If possible, convert a value to a SET-XXX! representation"
//
//      return: [<opt> set-word! set-path! set-tuple! set-group! set-block!]
//      value [<maybe> element?]
//  ]
//
DECLARE_NATIVE(setify)
{
    INCLUDE_PARAMS_OF_SETIFY;

    return COPY(Setify(ARG(value)));
}


//
//  Getify: C
//
// Like Setify() but Makes GET-XXX! instead of SET-XXX!.
//
Value* Getify(Value* out) {  // called on stack values; can't call evaluator
    Heart heart = Cell_Heart(out);
    if (Any_Block_Kind(heart)) {
        HEART_BYTE(out) = REB_GET_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(out) = REB_GET_GROUP;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(out) = REB_GET_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(out) = REB_GET_TUPLE;
    }
    else if (Any_Word_Kind(heart)) {
        HEART_BYTE(out) = REB_GET_WORD;
    }
    else
        fail ("Cannot GETIFY");

    return out;
}


//
//  getify: native [
//
//  "If possible, convert a value to a GET-XXX! representation"
//
//      return: [<opt> get-word! get-path! get-tuple! get-group! get-block!]
//      value [<maybe> element?]
//  ]
//
DECLARE_NATIVE(getify)
{
    INCLUDE_PARAMS_OF_GETIFY;

    return COPY(Getify(ARG(value)));
}


//
//  Metafy: C
//
// Turn a value into its META-XXX! equivalent, if possible.
//
Value* Metafy(Value* out) {  // called on stack values; can't call evaluator
    Heart heart = Cell_Heart(out);
    if (Any_Word_Kind(heart)) {
        HEART_BYTE(out) = REB_META_WORD;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(out) = REB_META_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(out) = REB_META_TUPLE;
    }
    else if (Any_Block_Kind(heart)) {
        HEART_BYTE(out) = REB_META_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(out) = REB_META_GROUP;
    }
    else if (heart == REB_VOID) {
        Init_Sigil(out, SIGIL_META);
    }
    else
        fail ("Cannot METAFY");

    return out;
}


//
//  metafy: native [
//
//  "If possible, convert a value to a META-XXX! representation"
//
//      return: [word! meta-word! meta-path! meta-tuple! meta-group! meta-block!]
//      value [<void> element?]  ; void makes @ as a WORD!
//  ]
//
DECLARE_NATIVE(metafy)
{
    INCLUDE_PARAMS_OF_METAFY;

    return COPY(Metafy(ARG(value)));
}


//
//  Theify: C
//
// Turn a value into its THE-XXX! equivalent, if possible.
//
Value* Theify(Value* out) {  // called on stack values; can't call evaluator
    Heart heart = Cell_Heart(out);
    if (Any_Word_Kind(heart)) {
        HEART_BYTE(out) = REB_THE_WORD;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(out) = REB_THE_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(out) = REB_THE_TUPLE;
    }
    else if (Any_Block_Kind(heart)) {
        HEART_BYTE(out) = REB_THE_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(out) = REB_THE_GROUP;
    }
    else if (heart == REB_VOID) {
        Init_Sigil(out, SIGIL_THE);
    }
    else
        fail ("Cannot THEIFY");

    return out;
}


//
//  inert: native [
//
//  "If possible, convert a value to a THE-XXX! representation"
//
//      return: [<opt> the-word! the-path! the-tuple! the-group! the-block!]
//      value [<maybe> element?]
//  ]
//
DECLARE_NATIVE(inert)
//
// Operators such as ANY and ALL have a behavior variation for @[xxx] blocks
// that assume the content is already reduced.  This helps to produce that
// form of value from a regular value.
{
    INCLUDE_PARAMS_OF_INERT;

    return COPY(Theify(ARG(value)));
}


//
//  Plainify: C
//
// Turn a value into its "plain" equivalent.  This works for all values.
//
Value* Plainify(Value* out) {
    Heart heart = Cell_Heart(out);
    if (Any_Word_Kind(heart)) {
        HEART_BYTE(out) = REB_WORD;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(out) = REB_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(out) = REB_TUPLE;
    }
    else if (Any_Block_Kind(heart)) {
        HEART_BYTE(out) = REB_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(out) = REB_GROUP;
    }

    return out;
}


//
//  plain: native [
//
//  "Convert a value into its plain representation"
//
//      return: [<opt> element?]
//      value [<maybe> element?]
//  ]
//
DECLARE_NATIVE(plain)
{
    INCLUDE_PARAMS_OF_PLAIN;

    return COPY(Plainify(ARG(value)));
}
