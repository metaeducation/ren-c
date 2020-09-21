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

#include "datatypes/sys-money.h"


//
//  Get_Num_From_Arg: C
//
// Get the amount to skip or pick.
// Allow multiple types. Throw error if not valid.
// Note that the result is one-based.
//
REBINT Get_Num_From_Arg(const RELVAL *val)
{
    REBINT n;

    if (IS_INTEGER(val)) {
        if (VAL_INT64(val) > INT32_MAX or VAL_INT64(val) < INT32_MIN)
            fail (Error_Out_Of_Range(SPECIFIC(val)));
        n = VAL_INT32(val);
    }
    else if (IS_DECIMAL(val) or IS_PERCENT(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            fail (Error_Out_Of_Range(SPECIFIC(val)));
        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else if (IS_LOGIC(val))
        n = (VAL_LOGIC(val) ? 1 : 2);
    else
        fail (rebUnrelativize(val));

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
    fail (Error_Out_Of_Range(SPECIFIC(val)));
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
    fail (Error_Out_Of_Range(SPECIFIC(val)));
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

    fail (val);
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
REBVAL *Get_System(REBLEN i1, REBLEN i2)
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
REBINT Get_System_Int(REBLEN i1, REBLEN i2, REBINT default_int)
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
    RELVAL *out,
    enum Reb_Kind type,
    const REBSER *s,  // ensured managed by calling macro
    REBLEN index,
    REBNOD *binding
){
    assert(ANY_SERIES_KIND(type));
    assert(GET_SERIES_FLAG(s, MANAGED));

    // Note: a R3-Alpha Make_Binary() comment said:
    //
    //     Make a binary string series. For byte, C, and UTF8 strings.
    //     Add 1 extra for terminator.
    //
    // One advantage of making all binaries terminate in 0 is that it means
    // that if they were valid UTF-8, they could be aliased as Rebol strings,
    // which are zero terminated.  So it's the rule.
    //
    ASSERT_SERIES_TERM(s);

    RESET_CELL(out, type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, s);
    VAL_INDEX(out) = index;
    if (ANY_ARRAY_KIND(type))
        INIT_BINDING(out, binding);
    else
        assert(binding == UNBOUND);

    return SPECIFIC(out);
}


//
//  Init_Any_String_At_Core: C
//
REBVAL *Init_Any_String_At_Core(  // also used with ANY-WORD!
    RELVAL *out,
    enum Reb_Kind type,
    REBSER *s,  // calling macro ensures STR and forces to be managed
    REBLEN index
){
    assert(GET_SERIES_FLAG(s, MANAGED));
    assert(SER_WIDE(s) == 1);
    ASSERT_SERIES_TERM(s);

    if (ANY_WORD_KIND(type))
        assert(NOT_SERIES_FLAG(s, UTF8_NONWORD));
    else
        assert(ANY_STRING_KIND(type));

    RESET_CELL(out, type, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, s);
    VAL_INDEX(out) = index;
    return SPECIFIC(out);
}


#if !defined(NDEBUG)

//
//  Extra_Init_Any_Context_Checks_Debug: C
//
// !!! Overlaps with ASSERT_CONTEXT, review folding them together.
//
void Extra_Init_Any_Context_Checks_Debug(enum Reb_Kind kind, REBCTX *c) {
    assert(
        (SER(c)->header.bits & SERIES_MASK_VARLIST) == SERIES_MASK_VARLIST
    );

    const REBVAL *archetype = CTX_ARCHETYPE(c);
    assert(VAL_CONTEXT(archetype) == c);
    assert(CTX_TYPE(c) == kind);

    // Currently only FRAME! uses the ->binding field, in order to capture the
    // ->binding of the function value it links to (which is in ->phase)
    //
    assert(VAL_BINDING(archetype) == UNBOUND or CTX_TYPE(c) == REB_FRAME);

    REBARR *varlist = CTX_VARLIST(c);
    REBARR *keylist = CTX_KEYLIST(c);
    assert(NOT_ARRAY_FLAG(keylist, HAS_FILE_LINE_UNMASKED));

    assert(
        not MISC_META(varlist)
        or ANY_CONTEXT(CTX_ARCHETYPE(MISC_META(varlist)))  // current rule
    );

    // FRAME!s must always fill in the phase slot, but that piece of the
    // REBVAL is reserved for future use in other context types...so make
    // sure it's null at this point in time.
    //
    if (CTX_TYPE(c) == REB_FRAME) {
        assert(IS_ACTION(CTX_ROOTKEY(c)));
        assert(VAL_PHASE(archetype) != nullptr);
    }
    else {
      #ifdef DEBUG_UNREADABLE_VOIDS
        assert(IS_UNREADABLE_DEBUG(CTX_ROOTKEY(c)));
      #endif
        assert(VAL_PHASE(archetype) == nullptr);
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
    assert((SER(a)->header.bits & SERIES_MASK_PARAMLIST) == SERIES_MASK_PARAMLIST);

    REBVAL *archetype = ACT_ARCHETYPE(a);
    assert(VAL_ACTION(archetype) == a);

    REBARR *paramlist = ACT_PARAMLIST(a);
    assert(NOT_ARRAY_FLAG(paramlist, HAS_FILE_LINE_UNMASKED));

    // !!! Currently only a context can serve as the "meta" information,
    // though the interface may expand.
    //
    assert(
        not MISC_META(paramlist)
        or ANY_CONTEXT(CTX_ARCHETYPE(MISC_META(paramlist)))
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
    REBVAL *series,  // ANY-SERIES! value whose index may be modified
    const REBVAL *part  // /PART (number, position in value, or BLANK! cell)
){
    if (IS_NULLED(part))  // indicates /PART refinement unused
        return VAL_LEN_AT(series);  // leave index alone, use plain length

    REBI64 len;
    if (IS_INTEGER(part) or IS_DECIMAL(part))
        len = Int32(part);  // may be positive or negative
    else {  // must be same series
        if (
            VAL_TYPE(series) != VAL_TYPE(part)  // !!! allow AS aliases?
            or VAL_SERIES(series) != VAL_SERIES(part)
        ){
            fail (Error_Invalid_Part_Raw(part));
        }

        len = cast(REBINT, VAL_INDEX(part)) - cast(REBINT, VAL_INDEX(series));
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
REBLEN Part_Len_May_Modify_Index(REBVAL *series, const REBVAL *limit) {
    assert(ANY_SERIES(series) or ANY_PATH(series));
    return Part_Len_Core(series, limit);
}


//
//  Part_Tail_May_Modify_Index: C
//
// Simple variation that instead of returning the length, returns the absolute
// tail position in the series of the partial sequence.
//
REBLEN Part_Tail_May_Modify_Index(REBVAL *series, const REBVAL *limit)
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
REBLEN Part_Limit_Append_Insert(const REBVAL *part) {
    if (IS_NULLED(part))
        return UINT32_MAX;  // treat as no limit

    if (IS_INTEGER(part)) {
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


//
//  Setify: C
//
// Turn a value into its SET-XXX! equivalent, if possible.  This tries to
// "be smart" so even a TEXT! can be turned into a SET-WORD! (just an
// unbound one).
//
REBVAL *Setify(REBVAL *out) {
    REBLEN quotes = Dequotify(out);

    enum Reb_Kind kind = VAL_TYPE(out);
    if (ANY_WORD_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SET_WORD;
    }
    else if (ANY_PATH_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SET_PATH;
    }
    else if (ANY_BLOCK_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SET_BLOCK;
    }
    else if (ANY_GROUP_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SET_GROUP;
    }
    else if (kind == REB_NULLED) {
        fail ("Cannot SETIFY a NULL");
    }
    else {
        // !!! For everything else, as en experiment see if there's some
        // kind of logic to turn into a SET-WORD!  Calling through the
        // API is slow, but easy to do for a test.
        //
        REBVAL *set = rebValueQ("to set-word!", out, rebEND);
        Move_Value(out, set);
        rebRelease(set);
    }

    return Quotify(out, quotes);
}


//
//  setify: native [
//
//  {If possible, convert a value to a SET-XXX! representation}
//
//      return: [<opt> set-word! set-path! set-group! set-block!]
//      value [<blank> any-value!]
//  ]
//
REBNATIVE(setify)
{
    INCLUDE_PARAMS_OF_SETIFY;

    RETURN (Setify(ARG(value)));
}


//
//  Getify: C
//
// Like Setify() but Makes GET-XXX! instead of SET-XXX!.
//
REBVAL *Getify(REBVAL *out) {
    REBLEN quotes = Dequotify(out);

    enum Reb_Kind kind = VAL_TYPE(out);
    if (ANY_BLOCK_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_GET_BLOCK;
    }
    else if (ANY_GROUP_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_GET_GROUP;
    }
    else if (ANY_PATH_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_GET_PATH;
    }
    else if (ANY_WORD_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_GET_WORD;
    }
    else if (kind == REB_NULLED) {
        fail ("Cannot GETIFY a NULL");
    }
    else {
        // !!! Experiment...see what happens if we fall back on GET-WORD!
        //
        REBVAL *get = rebValueQ("to get-word!", out, rebEND);
        Move_Value(out, get);
        rebRelease(get);
    }

    return Quotify(out, quotes);
}


//
//  getify: native [
//
//  {If possible, convert a value to a GET-XXX! representation}
//
//      return: [<opt> get-word! get-path! get-group! get-block!]
//      value [<blank> any-value!]
//  ]
//
REBNATIVE(getify)
{
    INCLUDE_PARAMS_OF_GETIFY;

    RETURN (Getify(ARG(value)));
}


//
//  Symify: C
//
// Turn a value into its SYM-XXX! equivalent, if possible.  This tries to
// "be smart" so even a TEXT! can be turned into a SYM-WORD! (just an
// unbound one).
//
REBVAL *Symify(REBVAL *out) {
    REBLEN quotes = Dequotify(out);

    enum Reb_Kind kind = VAL_TYPE(out);
    if (ANY_WORD_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SYM_WORD;
    }
    else if (ANY_PATH_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SYM_PATH;
    }
    else if (ANY_BLOCK_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SYM_BLOCK;
    }
    else if (ANY_GROUP_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_SYM_GROUP;
    }
    else if (kind == REB_NULLED) {
        fail ("Cannot SYMIFY a NULL");
    }
    else {
        // !!! For everything else, as en experiment see if there's some
        // kind of logic to turn into a SET-WORD!  Calling through the
        // API is slow, but easy to do for a test.
        //
        REBVAL *set = rebValueQ("to sym-word!", out, rebEND);
        Move_Value(out, set);
        rebRelease(set);
    }

    return Quotify(out, quotes);
}


//
//  symify: native [
//
//  {If possible, convert a value to a SYM-XXX! representation}
//
//      return: [<opt> sym-word! sym-path! sym-group! sym-block!]
//      value [<blank> any-value!]
//  ]
//
REBNATIVE(symify)
{
    INCLUDE_PARAMS_OF_SYMIFY;

    RETURN (Symify(ARG(value)));
}


//
//  Plainify: C
//
// Turn a value into its "plain" equivalent.  This works for all values,
// since 
//
REBVAL *Plainify(REBVAL *out) {
    REBLEN quotes = Dequotify(out);

    enum Reb_Kind kind = VAL_TYPE(out);
    if (ANY_WORD_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_WORD;
    }
    else if (ANY_PATH_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_PATH;
    }
    else if (ANY_BLOCK_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_BLOCK;
    }
    else if (ANY_GROUP_KIND(kind)) {
        mutable_KIND_BYTE(out) = mutable_MIRROR_BYTE(out) = REB_GROUP;
    }
    else if (kind == REB_NULLED)
        fail ("Cannot PLAINIFY a NULL");

    return Quotify(out, quotes);
}
