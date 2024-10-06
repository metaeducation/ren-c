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
        DECLARE_ELEMENT (temp);
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
    Value* obj = Varlist_Slot(Cell_Varlist(Lib(SYSTEM)), i1);
    if (i2 == 0)
        return obj;

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


#if !defined(NDEBUG)

//
//  Extra_Init_Context_Cell_Checks_Debug: C
//
// !!! Overlaps with Assert_Varlist, review folding them together.
//
void Extra_Init_Context_Cell_Checks_Debug(Kind kind, VarList* v) {
    assert((v->leader.bits & FLEX_MASK_VARLIST) == FLEX_MASK_VARLIST);

    const Value* archetype = Varlist_Archetype(v);
    assert(Cell_Varlist(archetype) == v);
    assert(CTX_TYPE(v) == kind);

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(
        BINDING(archetype) == UNBOUND
        or CTX_TYPE(v) == REB_FRAME
    );

    // KeyLists are uniformly managed, or certain routines would return
    // "sometimes managed, sometimes not" keylists...a bad invariant.
    //
    if (CTX_TYPE(v) != REB_MODULE) {  // keylist is global symbol table
        KeyList* keylist = Keylist_Of_Varlist(v);
        Assert_Flex_Managed(keylist);
    }

    assert(not CTX_ADJUNCT(v) or Any_Context_Kind(CTX_TYPE(CTX_ADJUNCT(v))));

    // FRAME!s must always fill in the phase slot, but that piece of the
    // Cell is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
    Flex* archetype_phase = Extract_Cell_Frame_Phase_Or_Label(archetype);
    if (CTX_TYPE(v) == REB_FRAME)
        assert(Is_Stub_Details(archetype_phase));
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
        (keylist->leader.bits & FLEX_MASK_KEYLIST)
        == FLEX_MASK_KEYLIST
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
// :PART limit, so that the series index points to the beginning of the
// subsetted range and gives back a length to the end of that subset.
//
// It determines if the position for the part is before or after the series
// position.  If it is before (e.g. a negative integer limit was passed in,
// or a prior position) the series value will be updated to the earlier
// position, so that a positive length for the partial region is returned.
//
REBLEN Part_Len_May_Modify_Index(
    Value* series,  // ANY-SERIES? value whose index may be modified
    const Value* part  // :PART (number, position in value, or BLANK! cell)
){
    if (Any_Sequence(series)) {
        if (not Is_Nulled(part))
            fail (":PART cannot be used with ANY-SEQUENCE");

        return Cell_Sequence_Len(series);
    }

    assert(Is_Issue(series) or Any_Series(series));

    if (Is_Nulled(part)) {  // indicates :PART refinement unused
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
            or Cell_Flex(series) != Cell_Flex(part)
        ){
            fail (Error_Invalid_Part_Raw(part));
        }

        len = VAL_INDEX(part) - iseries;
    }

    // Restrict length to the size available
    //
    if (len >= 0) {
        REBINT maxlen = Cell_Series_Len_At(series);
        if (len > maxlen)
            len = maxlen;
    }
    else {
        if (Is_Issue(part))
            fail (Error_Invalid_Part_Raw(part));

        len = -len;
        if (len > cast(REBI64, iseries))
            len = iseries;
        VAL_INDEX_RAW(series) -= len;
    }

    if (len > UINT32_MAX) {
        //
        // Tests had `[1] = copy:part tail [1] -2147483648`, where trying to
        // do `len = -len` couldn't make a positive 32-bit version of that
        // negative value.  For now, use REBI64 to do the calculation.
        //
        fail ("Length out of range for :PART refinement");
    }

    assert(len >= 0);
    if (not Is_Issue(series))
        assert(Cell_Series_Len_Head(series) >= len);
    return len;
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
// This is for the specific cases of INSERT and APPEND interacting with :PART,
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
        return i;
    }

    fail ("APPEND and INSERT only take :PART limit as INTEGER!");
}


//
//  Add_Max: C
//
int64_t Add_Max(Option(Heart) heart, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n + m;
    if (r < -maxi or r > maxi) {
        if (heart)
            fail (Error_Type_Limit_Raw(Datatype_From_Kind(unwrap heart)));
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
Element* Setify(Element* out) {  // called on stack values; can't call eval
    Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
        out, REB_CHAIN, CELL_MASK_0
    );
    if (error)
        fail (unwrap error);
    return out;
}


//
//  Trap_Unchain: C
//
// Evolve a cell containing a chain that's just an element and a blank into
// the element alone, e.g. `a:` -> `a` or `:[a b]` -> `[a b]`
//
Option(Error*) Trap_Unchain(Element* out) {
    assert(Any_Chain_Kind(Cell_Heart(out)));
    assert(Get_Cell_Flag(out, SEQUENCE_HAS_NODE));  // not compressed bytes

    const Node* node1 = Cell_Node1(out);
    if (Is_Node_A_Cell(node1)) {  // compressed 2-elements, sizeof(Stub)
        const Pairing* pairing = c_cast(Pairing*, node1);
        if (Is_Blank(Pairing_First(pairing))) {
            assert(not Is_Blank(Pairing_Second(pairing)));
            Derelativize(out, Pairing_Second(pairing), Cell_Binding(out));
            return nullptr;
        }
        if (Is_Blank(Pairing_Second(pairing))) {
            Derelativize(out, Pairing_First(pairing), Cell_Binding(out));
            return nullptr;
        }
        goto unchain_error;
    }

  { //////////////////////////////////////////////////////////////////////////

    const Stub* s = c_cast(Stub*, node1);
    if (Is_Stub_Symbol(s)) {
        HEART_BYTE(out) = REB_WORD;
        Clear_Cell_Flag(out, REFINEMENT_LIKE);  // !!! necessary?
        return nullptr;
    }

    Heart h = u_cast(Heart, MIRROR_BYTE(s));
    if (h != REB_0) {  // no length 2 sequence arrays unless mirror
        HEART_BYTE(out) = h;
        Clear_Cell_Flag(out, REFINEMENT_LIKE);  // !!! necessary
        return nullptr;
    }

} unchain_error: {

    return Error_User(
        "Can only UNCHAIN length 2 chains (when 1 item is blank)"
    );
}}


//
//  Unchain: C
//
// Version of Unchain when you don't expect it to fail.
//
Element* Unchain(Element* out) {
    Option(Error*) error = Trap_Unchain(out);
    assert(not error);
    UNUSED(error);
    return out;
}


//
//  setify: native [
//
//  "If possible, convert a value to a SET-XXX! representation"
//
//      return: [~null~ any-set-value? set-word?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(setify)
{
    INCLUDE_PARAMS_OF_SETIFY;

    Element* e = cast(Element*, ARG(element));

    return COPY(Setify(e));
}


//
//  Getify: C
//
// Like Setify() but Makes GET-XXX! instead of SET-XXX!.
//
Element* Getify(Element* out) {  // called on stack values; can't call eval
    Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
        out, REB_CHAIN, CELL_FLAG_REFINEMENT_LIKE
    );
    if (error)
        fail (unwrap error);
    return out;
}


//
//  getify: native [
//
//  "If possible, convert a value to a GET-XXX! representation"
//
//      return: [~null~ any-get-value? get-word?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(getify)
{
    INCLUDE_PARAMS_OF_GETIFY;

    Element* e = cast(Element*, ARG(element));

    return COPY(Getify(e));
}


//
//  Metafy: C
//
// Turn a value into its META-XXX! equivalent, if possible.
//
Value* Metafy(Value* out) {  // called on stack values; can't call evaluator
    if (Is_Void(out))
        return Init_Sigil(out, SIGIL_META);

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
    else
        fail ("Cannot METAFY");

    return out;
}


//
//  metafy: native [
//
//  "If possible, convert a value to a META-XXX! representation"
//
//      return: [any-meta-value? sigil!]
//      value [~void~ element?]  ; void makes @ as a SIGIL!
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
    if (Is_Void(out))
        return Init_Sigil(out, SIGIL_THE);

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
    else
        fail ("Cannot THEIFY");

    return out;
}


//
//  inert: native [
//
//  "If possible, convert a value to a THE-XXX! representation"
//
//      return: [~null~ any-the-value?]
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
// Turn a value into its "plain" equivalent.  This works for all Elements.
//
Element* Plainify(Element* e) {
    Heart heart = Cell_Heart(e);
    if (Any_Word_Kind(heart)) {
        HEART_BYTE(e) = REB_WORD;
    }
    else if (Any_Path_Kind(heart)) {
        HEART_BYTE(e) = REB_PATH;
    }
    else if (Any_Tuple_Kind(heart)) {
        HEART_BYTE(e) = REB_TUPLE;
    }
    else if (Any_Block_Kind(heart)) {
        HEART_BYTE(e) = REB_BLOCK;
    }
    else if (Any_Group_Kind(heart)) {
        HEART_BYTE(e) = REB_GROUP;
    }
    return e;
}


//
//  plain: native [
//
//  "Convert a value into its plain representation"
//
//      return: [~null~ element?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(plain)
{
    INCLUDE_PARAMS_OF_PLAIN;

    Element* e = cast(Element*, ARG(element));

    return COPY(Plainify(e));
}


//
//  unchain: native [
//
//  "Remove CHAIN!, e.g. leading colon or trailing colon from an element"
//
//      return: [~null~ element?]
//      chain [<maybe> chain! set-word? set-tuple?]
//  ]
//
DECLARE_NATIVE(unchain)
{
    INCLUDE_PARAMS_OF_UNCHAIN;

    Element* e = cast(Element*, ARG(chain));

    Option(Error*) error = Trap_Unchain(e);
    if (error)
        return RAISE(unwrap error);

    return COPY(e);
}
