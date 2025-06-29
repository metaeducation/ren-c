//
//  file: %f-stubs.c
//  summary: "miscellaneous little functions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
            abrupt_panic (Error_Out_Of_Range(val));
        n = VAL_INT32(val);
    }
    else if (Is_Decimal(val) or Is_Percent(val)) {
        if (VAL_DECIMAL(val) > INT32_MAX or VAL_DECIMAL(val) < INT32_MIN)
            abrupt_panic (Error_Out_Of_Range(val));
        n = cast(REBINT, VAL_DECIMAL(val));
    }
    else if (Is_Logic(val))
        n = (Cell_Logic(val) ? 1 : 2);
    else
        abrupt_panic (val);

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

        abrupt_panic (Error_Out_Of_Range(temp));
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
    abrupt_panic (Error_Out_Of_Range(val));
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
    abrupt_panic (Error_Out_Of_Range(val));
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

    abrupt_panic (val);
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

    abrupt_panic (val);
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
            abrupt_panic (Error_Out_Of_Range(val));

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

    abrupt_panic (Error_Out_Of_Range(val));
}


//
//  Datatype_From_Type: C
//
// Returns the specified datatype Value from the LIB context.
// The datatypes are all at the head of the LIB context.
//
const Value* Datatype_From_Type(Type type)
{
    assert(type <= MAX_TYPE);
    Patch* patch = &g_datatype_patches[cast(Byte, type)];
    const Value* datatype = c_cast(Value*, Stub_Cell(patch));
    assert(Is_Datatype(datatype));
    return datatype;
}


//
//  Datatype_Of: C
//
// Returns the datatype value for the given value.
// The datatypes are all at the head of the context.
//
const Value* Datatype_Of(const Atom* value)
{
    Option(Type) type = Type_Of(value);
    if (type)
        return Datatype_From_Type(unwrap type);

    const ExtraHeart* ext_heart = Cell_Extra_Heart(value);
    assert(Is_Stub_Patch(ext_heart));

    const Value* datatype = c_cast(Value*, Stub_Cell(ext_heart));
    assert(Is_Datatype(datatype));
    return datatype;
}


//
//  Datatype_Of_Fundamental: C
//
const Value* Datatype_Of_Fundamental(const Element* value)
{
    assert(Any_Fundamental(value));
    return Datatype_Of(value);
}


//
//  Datatype_Of_Builtin_Fundamental: C
//
const Value* Datatype_Of_Builtin_Fundamental(const Element* value)
{
    assert(Any_Fundamental(value));

    Option(Type) type = Type_Of(value);
    assert(type);
    return Datatype_From_Type(unwrap type);
}

//
//  Type_Of_Builtin_Fundamental: C
//
Type Type_Of_Builtin_Fundamental(const Element* value)
{
    assert(Any_Fundamental(value));

    Option(Type) type = Type_Of(value);
    assert(type);
    return unwrap type;
}


//
//  Get_System: C
//
// Return a second level object field of the system object.
//
Slot* Get_System(REBLEN i1, REBLEN i2)
{
    Slot* obj_slot = Varlist_Slot(Cell_Varlist(LIB(SYSTEM)), i1);
    if (i2 == 0)
        return obj_slot;

    DECLARE_VALUE (obj);
    Option(Error*) e = Trap_Read_Slot(obj, obj_slot);
    if (e)
        abrupt_panic (unwrap e);

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
    Slot* slot = Get_System(i1, i2);

    if (Is_Integer(Slot_Hack(slot)))
        return VAL_INT32(Slot_Hack(slot));

    return default_int;
}


#if RUNTIME_CHECKS

//
//  Extra_Init_Context_Cell_Checks_Debug: C
//
// !!! Overlaps with Assert_Varlist, review folding them together.
//
void Extra_Init_Context_Cell_Checks_Debug(Heart heart, VarList* v) {
    assert(CTX_TYPE(v) == heart);

    if (heart == TYPE_FRAME)  // may not have Misc_Varlist_Adjunct()
        assert(
            (v->leader.bits & STUB_MASK_LEVEL_VARLIST)
            == STUB_MASK_LEVEL_VARLIST
        );
    else
        assert((v->leader.bits & STUB_MASK_VARLIST) == STUB_MASK_VARLIST);

    const Value* archetype = Varlist_Archetype(v);
    assert(Cell_Varlist(archetype) == v);

    // Currently only FRAME! uses the extra field, in order to capture the
    // ->coupling of the function value it links to (which is in ->phase)
    //
    assert(archetype->extra.base == nullptr or heart == TYPE_FRAME);

    // KeyLists are uniformly managed, or certain routines would return
    // "sometimes managed, sometimes not" keylists...a bad invariant.
    //
    KeyList* keylist = Bonus_Keylist(v);
    Assert_Flex_Managed(keylist);

    assert(
        not Misc_Varlist_Adjunct(v)
        or Any_Context_Type(CTX_TYPE(unwrap Misc_Varlist_Adjunct(v)))
    );
}


//
//  Extra_Init_Frame_Checks_Debug: C
//
void Extra_Init_Frame_Checks_Debug(Phase* phase) {
    assert(Is_Frame(Phase_Archetype(phase)));

    assert(Is_Stub_Details(Phase_Details(phase)));
    assert(Is_Stub_Varlist(Phase_Paramlist(phase)));

    KeyList* keylist = Phase_Keylist(phase);
    assert(
        (keylist->leader.bits & STUB_MASK_KEYLIST)
        == STUB_MASK_KEYLIST
    );

    if (Get_Stub_Flag(phase, MISC_NEEDS_MARK)) {
        assert(
            Misc_Phase_Adjunct(phase) == nullptr
            or Any_Context_Type(CTX_TYPE(unwrap Misc_Phase_Adjunct(phase)))
        );
    }
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
    const Value* part  // :PART (number, position in value, or nulled cell)
){
    assert(Is_Rune(series) or Any_Series(series));

    if (Is_Nulled(part)) {  // indicates :PART refinement unused
        if (not Is_Rune(series))
            return Series_Len_At(series);  // use plain length

        Size size;
        Cell_Utf8_Size_At(&size, series);
        return size;
    }

    // Series_Index() checks to make sure it's for in-bounds
    //
    REBLEN iseries = Is_Rune(series) ? 0 : Series_Index(series);

    REBI64 len;
    if (Is_Integer(part) or Is_Decimal(part))
        len = Int32(part);  // may be positive or negative
    else {  // must be same series
        if (
            Is_Rune(part)
            or Type_Of(series) != Type_Of(part)  // !!! allow AS aliases?
            or Cell_Flex(series) != Cell_Flex(part)
        ){
            abrupt_panic (Error_Invalid_Part_Raw(part));
        }

        len = Series_Index(part) - iseries;
    }

    // Restrict length to the size available
    //
    if (len >= 0) {
        REBINT maxlen = Series_Len_At(series);
        if (len > maxlen)
            len = maxlen;
    }
    else {
        if (Is_Rune(part))
            abrupt_panic (Error_Invalid_Part_Raw(part));

        len = -len;
        if (len > cast(REBI64, iseries))
            len = iseries;
        SERIES_INDEX_UNBOUNDED(series) -= len;
    }

    if (len > UINT32_MAX) {
        //
        // Tests had `[1] = copy:part tail of [1] -2147483648`, where trying
        // to do `len = -len` couldn't make a positive 32-bit version of that
        // negative value.  For now, use REBI64 to do the calculation.
        //
        abrupt_panic ("Length out of range for :PART refinement");
    }

    assert(len >= 0);
    if (not Is_Rune(series))
        assert(Series_Len_Head(series) >= len);
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
    return len + Series_Index(series); // uses the possibly-updated index
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
            return 0;  // !!! Would it be better to warning?
        return i;
    }

    abrupt_panic ("APPEND and INSERT only take :PART limit as INTEGER!");
}


//
//  Add_Max: C
//
int64_t Add_Max(Option(Heart) heart, int64_t n, int64_t m, int64_t maxi)
{
    int64_t r = n + m;
    if (r < -maxi or r > maxi) {
        if (heart)
            abrupt_panic (Error_Type_Limit_Raw(Datatype_From_Type(unwrap heart)));
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
        abrupt_panic (Error_Type_Limit_Raw(Datatype_From_Type(heart)));
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
        out, TYPE_CHAIN, CELL_MASK_ERASED_0
    );
    if (error)
        abrupt_panic (unwrap error);
    return out;
}


//
//  Unsingleheart_Sequence: C
//
// Evolve a cell containing a sequence that's just an element and a SPACE into
// the element alone, e.g. `a:` -> `a` or `:[a b]` -> `[a b]`
//
Result(Element*) Unsingleheart_Sequence(Element* out)
{
    assert(Any_Sequence_Type(Heart_Of(out)));
    if (not Sequence_Has_Pointer(out))
        goto report_error;  // compressed bytes don't encode blanks

  extract_payload_1: {

    const Base* payload1 = CELL_PAYLOAD_1(out);

  test_for_pairing_sequence: {

    if (Is_Base_A_Cell(payload1)) {  // compressed 2-elements, sizeof(Stub)
        const Pairing* pairing = c_cast(Pairing*, payload1);
        if (Is_Space(Pairing_First(pairing))) {
            assert(not Is_Space(Pairing_Second(pairing)));
            Derelativize(out, Pairing_Second(pairing), Cell_Binding(out));
            return out;
        }
        if (Is_Space(Pairing_Second(pairing))) {
            Derelativize(out, Pairing_First(pairing), Cell_Binding(out));
            return out;
        }
        goto report_error;
    }

} test_for_flex_sequence: {

    const Flex* f = c_cast(Flex*, payload1);
    if (Is_Stub_Symbol(f)) {
        KIND_BYTE(out) = TYPE_WORD;
        Clear_Cell_Flag(out, LEADING_SPACE);  // !!! necessary?
        return out;
    }

    Option(Heart) mirror = Mirror_Of(c_cast(Source*, f));
    if (mirror) {  // no length 2 sequence arrays unless mirror
        KIND_BYTE(out) = unwrap mirror;
        Clear_Cell_Flag(out, LEADING_SPACE);  // !!! necessary
        return out;
    }

}} report_error: { ///////////////////////////////////////////////////////////

    return fail (
        "UNCHAIN/UNPATH/UNTUPLE only on length 2 chains (when 1 item is SPACE)"
    );
}}


//
//  setify: native [
//
//  "If possible, convert a value to a SET-XXX! representation"
//
//      return: [null? any-set-value? set-word?]
//      element [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(SETIFY)
{
    INCLUDE_PARAMS_OF_SETIFY;

    Element* e = Element_ARG(ELEMENT);

    return COPY(Setify(e));
}


//
//  Getify: C
//
// Like Setify() but Makes GET-XXX! instead of SET-XXX!.
//
Element* Getify(Element* out) {  // called on stack values; can't call eval
    Option(Error*) error = Trap_Blank_Head_Or_Tail_Sequencify(
        out, TYPE_CHAIN, CELL_FLAG_LEADING_SPACE
    );
    if (error)
        abrupt_panic (unwrap error);
    return out;
}


//
//  getify: native [
//
//  "If possible, convert a value to a GET-XXX! representation"
//
//      return: [null? any-get-value? get-word?]
//      element [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(GETIFY)
{
    INCLUDE_PARAMS_OF_GETIFY;

    Element* e = Element_ARG(ELEMENT);

    return COPY(Getify(e));
}


// Right now if we used SPECIALIZE of a SIGILIZE native, it would not be able
// to take advantage of the intrinsic optimization...since the sigil would
// have to live in a frame cell.  So the natives are made by hand here.
//
static Bounce Sigilize_Native_Core(Level* level_, Sigil sigil)
{
    INCLUDE_PARAMS_OF_META;  // META, PIN, TIE all same signature.

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Opt_Out_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    attempt {
        if (Any_Plain(e))
            continue;

        if (Not_Level_Flag(LEVEL, DISPATCHING_INTRINSIC))
            if (Bool_ARG(FORCE)) {
                Plainify(e);
                continue;
            }

        return FAIL(
            "Trying to add Sigil to already metaform/tied/pinned value"
        );
    }

    return COPY(Sigilize(e, sigil));
}


//
//  meta: native:intrinsic [
//
//  "Convert a value to its ^XXX metaform representation"
//
//      return: "Error if already metaform/tied/pinned and not :FORCE"
//          [error! ^plain?]
//      value [<opt-out> fundamental?]
//      :force "Apply lift, even if already metaform/tied/pinned"
//  ]
//
DECLARE_NATIVE(META)
{
    return Sigilize_Native_Core(LEVEL, SIGIL_META);
}


//
//  pin: native:intrinsic [
//
//  "Convert a value to its @XXX pinned representation"
//
//      return: "Error if already metaform/tied/pinned and not :FORCE"
//          [error! @plain?]
//      value [<opt-out> fundamental?]
//      :force "Apply pin, even if already metaform/tied/pinned"
//  ]
//
DECLARE_NATIVE(PIN)
{
    return Sigilize_Native_Core(LEVEL, SIGIL_PIN);
}


//
//  tie: native:intrinsic [
//
//  "Convert a value to its $XXX tied representation"
//
//      return: "Error if already metaform/tied/pinned and not :FORCE"
//          [error! $plain?]
//      value [<opt-out> fundamental?]
//      :force "Apply tie, even if already metaform/tied/pinned"
//  ]
//
DECLARE_NATIVE(TIE)
{
    return Sigilize_Native_Core(LEVEL, SIGIL_TIE);
}


// Right now if we used SPECIALIZE of a UNSIGILIZE native, it would not be able
// to take advantage of the intrinsic optimization...since the sigil would
// have to live in a frame cell.  So the natives are made by hand here.
//
static Bounce Unsigilize_Native_Core(Level* level_, Sigil sigil)
{
    INCLUDE_PARAMS_OF_UNMETA;  // same signature as UNPIN, UNTIE

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Opt_Out_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    if (Sigil_Of(e) != sigil)
        return FAIL("Trying to remove Sigil from value without that Sigil");

    return COPY(Plainify(e));
}


//
//  unmeta: native:intrinsic [
//
//  "Convert ^XXX metaform representation to plain XXX"
//
//      return: "Error if value not metaform"
//          [null? plain? error!]
//      value [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(UNMETA)
{
    return Unsigilize_Native_Core(LEVEL, SIGIL_META);
}


//
//  unpin: native:intrinsic [
//
//  "Convert @XXX pinned representation to plain XXX"
//
//      return: "Error if value not pinned"
//          [null? plain? error!]
//      value [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(UNPIN)
{
    return Unsigilize_Native_Core(LEVEL, SIGIL_PIN);
}


//
//  untie: native:intrinsic [
//
//  "Convert ^XXX tied representation to plain XXX"
//
//      return: "Error if value not tied"
//          [null? plain? error!]
//      value [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(UNTIE)
{
    return Unsigilize_Native_Core(LEVEL, SIGIL_TIE);
}


//
//  plain: native:intrinsic [
//
//  "Convert a value into its plain representation"
//
//      return: [null? plain?]
//      element [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(PLAIN)
{
    INCLUDE_PARAMS_OF_PLAIN;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Opt_Out_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return COPY(Plainify(e));
}


//
//  unchain: native [
//
//  "Remove CHAIN!, e.g. leading colon or trailing colon from an element"
//
//      return: [null? element?]
//      chain [<opt-out> chain!]
//  ]
//
DECLARE_NATIVE(UNCHAIN)
{
    INCLUDE_PARAMS_OF_UNCHAIN;

    Element* elem = Element_ARG(CHAIN);

    trap (Unsingleheart_Sequence(elem));

    return COPY(elem);
}


//
//  unpath: native [
//
//  "Remove PATH!, e.g. leading slash or trailing slash from an element"
//
//      return: [null? element?]
//      path [<opt-out> path!]
//  ]
//
DECLARE_NATIVE(UNPATH)
{
    INCLUDE_PARAMS_OF_UNPATH;

    Element* elem = Element_ARG(PATH);

    trap (Unsingleheart_Sequence(elem));

    return COPY(elem);
}


//
//  untuple: native [
//
//  "Remove TUPLE!, e.g. leading dot or trailing dot from a tuple"
//
//      return: [null? element?]
//      tuple [<opt-out> tuple!]
//  ]
//
DECLARE_NATIVE(UNTUPLE)
{
    INCLUDE_PARAMS_OF_UNTUPLE;

    Element* elem = Element_ARG(TUPLE);

    trap (Unsingleheart_Sequence(elem));

    return COPY(elem);
}
